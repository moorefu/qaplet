/* SPDX-License-Identifier: MIT */
#include "runtime_internal.h"
#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

typedef struct work_handler work_handler;
typedef struct work_task work_task;
typedef struct work_worker work_worker;
struct work_handler {
  work_handler *next;
  qaplet_work_function function;
  void *userdata;
  size_t name_length, input_bytes, output_bytes;
  uint64_t concurrency, running;
  char name[];
};
struct work_task {
  uint64_t generation, sequence;
  work_handler *handler;
  unsigned char *data;
  size_t input_length, data_size, length;
  qaplet_work_state state;
  qaplet_code code;
  uint32_t error_domain;
  int32_t error;
  int active, physical, cancelled, handoff, released;
};
struct work_worker {
  qaplet_runtime *runtime;
  pthread_t thread;
  int active, exited, join_attempted;
};
struct qaplet_work_pool {
  work_handler *handlers;
  work_task *tasks;
  work_worker *workers;
  size_t task_bytes, worker_bytes;
  uint64_t sequence;
  int stopping, closing;
};
struct qaplet_work_context { qaplet_runtime *runtime; work_task *task; };
static void retire_task(qaplet_runtime *,work_task *);
static void increment(uint64_t *value) { if (*value != UINT64_MAX) ++*value; }
static void peak(uint64_t current, uint64_t *value) { if (current > *value) *value=current; }
static int terminal(work_task *task) { return task->state >= QAPLET_WORK_SUCCEEDED; }
static int valid_name(const char *name, size_t length) {
  return name && length && length < SIZE_MAX && !memchr(name,0,length) && qaplet_utf8_valid(name,length);
}
static int fits_locked(qaplet_runtime *runtime, size_t bytes) {
  return bytes <= runtime->snapshot.limits.native_storage_bytes-runtime->snapshot.native_current_bytes;
}
static qaplet_code capacity(qaplet_runtime *runtime, size_t bytes) {
  pthread_mutex_lock(&runtime->mutex);
  qaplet_code code=fits_locked(runtime,bytes) ? QAPLET_OK : QAPLET_RESOURCE_LIMIT;
  pthread_mutex_unlock(&runtime->mutex);
  return code;
}
static void free_locked(qaplet_runtime *runtime, void *data, size_t bytes) {
  if (data) { free(data); runtime->snapshot.native_current_bytes-=bytes; }
}
static work_handler *find_handler(qaplet_work_pool *pool, const char *name, size_t length) {
  for (work_handler *h=pool ? pool->handlers : NULL; h; h=h->next)
    if (h->name_length==length && !memcmp(h->name,name,length)) return h;
  return NULL;
}
static qaplet_code open_owner(qaplet_runtime *runtime) {
  qaplet_code code=qaplet_owner(runtime);
  if (!code && !runtime->m2_enabled) code=QAPLET_UNSUPPORTED;
  if (!code && qaplet_get_lifecycle(runtime)!=QAPLET_OPEN) code=QAPLET_BAD_STATE;
  return code;
}
static qaplet_code ensure_pool(qaplet_runtime *runtime) {
  if (runtime->work_pool) return QAPLET_OK;
  qaplet_code code=capacity(runtime,sizeof(qaplet_work_pool));
  if (code) return code;
  qaplet_work_pool *pool=qaplet_native_alloc(runtime,sizeof(*pool));
  if (!pool) return QAPLET_MEMORY_LIMIT;
  pthread_mutex_lock(&runtime->mutex);
  runtime->work_pool=pool;
  /* The provider also owns declarations and retained C results after workers
   * exit. Their lifetime is separate from loop ref and thread retention. */
  ++runtime->native_retained;
  pthread_mutex_unlock(&runtime->mutex);
  return QAPLET_OK;
}
qaplet_code qaplet_register_work_handler(qaplet_runtime *runtime, const qaplet_work_handler *input,
                                         qaplet_diagnostic *diagnostic) {
  qaplet_code code=qaplet_check_diagnostic(diagnostic);
  if (code) return code;
  code=qaplet_check_structure(input,sizeof(*input));
  if (!code && (!valid_name(input->name,input->name_length) || !input->function || input->flags ||
                input->reserved[0] || input->reserved[1])) code=QAPLET_INVALID_ARGUMENT;
  if (!code) code=open_owner(runtime);
  if (code) return qaplet_report(diagnostic,code);
  uint64_t in=input->input_bytes ? input->input_bytes : runtime->m2_limits.work_input_bytes;
  uint64_t out=input->output_bytes ? input->output_bytes : runtime->m2_limits.work_output_bytes;
  uint64_t concurrency=input->concurrency ? input->concurrency : runtime->m2_limits.handler_concurrency;
  if (in>runtime->m2_limits.work_input_bytes || out>runtime->m2_limits.work_output_bytes ||
      concurrency>runtime->m2_limits.worker_threads || input->name_length>SIZE_MAX-sizeof(work_handler)-1)
    return qaplet_report(diagnostic,QAPLET_INVALID_ARGUMENT);
  if (runtime->host_registration_locked) return qaplet_report(diagnostic,QAPLET_BAD_STATE);
  if (find_handler(runtime->work_pool,input->name,input->name_length)) return qaplet_report(diagnostic,QAPLET_BAD_STATE);
  if (runtime->work_snapshot.handlers>=runtime->m2_limits.work_handlers) return qaplet_report(diagnostic,QAPLET_RESOURCE_LIMIT);
  size_t bytes=sizeof(work_handler)+input->name_length+1;
  size_t pool_bytes=runtime->work_pool ? 0 : sizeof(qaplet_work_pool);
  if (bytes>SIZE_MAX-pool_bytes || capacity(runtime,bytes+pool_bytes)) return qaplet_report(diagnostic,QAPLET_RESOURCE_LIMIT);
  code=ensure_pool(runtime);
  if (code) return qaplet_report(diagnostic,code);
  work_handler *handler=qaplet_native_alloc(runtime,bytes);
  if (!handler) return qaplet_report(diagnostic,QAPLET_MEMORY_LIMIT);
  handler->function=input->function; handler->userdata=input->userdata;
  handler->input_bytes=in; handler->output_bytes=out; handler->concurrency=concurrency;
  handler->name_length=input->name_length; memcpy(handler->name,input->name,input->name_length);
  pthread_mutex_lock(&runtime->mutex);
  if (runtime->snapshot.lifecycle!=QAPLET_OPEN) code=QAPLET_BAD_STATE;
  else {
    handler->next=runtime->work_pool->handlers; runtime->work_pool->handlers=handler;
    ++runtime->work_snapshot.handlers; peak(runtime->work_snapshot.handlers,&runtime->work_snapshot.handlers_peak);
  }
  pthread_mutex_unlock(&runtime->mutex);
  if (code) qaplet_native_free(runtime,handler,bytes);
  return qaplet_report(diagnostic,code);
}
static work_task *next_task(qaplet_runtime *runtime) {
  qaplet_work_pool *pool=runtime->work_pool;
  work_task *selected=NULL;
  for (uint64_t i=0;i<runtime->m2_limits.operations;++i) {
    work_task *task=&pool->tasks[i];
    if (!task->active || task->state!=QAPLET_WORK_QUEUED ||
        task->handler->running>=task->handler->concurrency) continue;
    if (!selected || task->sequence<selected->sequence) selected=task;
  }
  return selected;
}
static void *worker_main(void *opaque) {
  work_worker *worker=opaque;
  qaplet_runtime *runtime=worker->runtime;
  pthread_mutex_lock(&runtime->mutex);
  qaplet_work_pool *pool=runtime->work_pool;
  for (;;) {
    work_task *task=pool->stopping ? NULL : next_task(runtime);
    if (!task) {
      if (pool->stopping) break;
      int error=pthread_cond_wait(&runtime->native_condition,&runtime->mutex);
      if (error) { qaplet_control_failure_locked(runtime,error); qaplet_wake_locked(runtime); break; }
      continue;
    }
    work_handler *handler=task->handler;
    task->physical=1; task->state=QAPLET_WORK_RUNNING; ++handler->running;
    --runtime->work_snapshot.queued; ++runtime->work_snapshot.running;
    qaplet_work_context context={runtime,task};
    qaplet_work_output output={task->data+task->input_length,handler->output_bytes,0,0,0};
    pthread_mutex_unlock(&runtime->mutex);
    qaplet_code code=handler->function(&context,task->data,task->input_length,&output,handler->userdata);
    pthread_mutex_lock(&runtime->mutex);
    task->physical=0; --handler->running; --runtime->work_snapshot.running;
    if (!task->cancelled) {
      if (output.length>handler->output_bytes) { code=QAPLET_VALUE_TOO_LARGE; output.length=0; }
      task->code=code; task->length=output.length;
      task->error_domain=output.error_domain; task->error=output.error;
      task->state=code ? QAPLET_WORK_FAILED : QAPLET_WORK_SUCCEEDED;
      ++runtime->work_snapshot.retained_results; increment(&runtime->work_snapshot.completed);
    }
    if (task->released) retire_task(runtime,task);
    /* The pre-reserved record is the completion. No business event or malloc. */
    qaplet_wake_locked(runtime);
    pthread_cond_broadcast(&runtime->native_condition);
  }
  worker->exited=1;
  pthread_cond_broadcast(&runtime->native_condition);
  pthread_mutex_unlock(&runtime->mutex);
  return NULL;
}
static qaplet_code storage(qaplet_runtime *runtime) {
  qaplet_work_pool *pool=runtime->work_pool;
  if (pool->tasks) return QAPLET_OK;
  if (runtime->m2_limits.operations>SIZE_MAX/sizeof(work_task) ||
      runtime->m2_limits.worker_threads>SIZE_MAX/sizeof(work_worker)) return QAPLET_RESOURCE_LIMIT;
  size_t tasks=runtime->m2_limits.operations*sizeof(work_task), workers=runtime->m2_limits.worker_threads*sizeof(work_worker);
  if (tasks>SIZE_MAX-workers || capacity(runtime,tasks+workers)) return QAPLET_RESOURCE_LIMIT;
  work_task *slots=qaplet_native_alloc(runtime,tasks);
  if (!slots) return QAPLET_MEMORY_LIMIT;
  work_worker *threads=qaplet_native_alloc(runtime,workers);
  if (!threads) { qaplet_native_free(runtime,slots,tasks); return QAPLET_MEMORY_LIMIT; }
  pthread_mutex_lock(&runtime->mutex);
  pool->tasks=slots; pool->workers=threads; pool->task_bytes=tasks; pool->worker_bytes=workers;
  pthread_mutex_unlock(&runtime->mutex);
  return QAPLET_OK;
}
static qaplet_code start_worker_locked(qaplet_runtime *runtime) {
  qaplet_work_pool *pool=runtime->work_pool;
  if (runtime->work_snapshot.worker_threads>=runtime->m2_limits.worker_threads) return QAPLET_OK;
  work_worker *worker=&pool->workers[runtime->work_snapshot.worker_threads];
  pthread_attr_t attr;
  int error=pthread_attr_init(&attr);
  if (!error) {
    error=pthread_attr_setstacksize(&attr,runtime->m2_limits.worker_stack_bytes);
    worker->runtime=runtime;
    if (!error) error=pthread_create(&worker->thread,&attr,worker_main,worker);
    pthread_attr_destroy(&attr);
  }
  if (error) {
    runtime->work_snapshot.start_error_domain=QAPLET_ERROR_DOMAIN_ERRNO;
    runtime->work_snapshot.start_error=error;
    return QAPLET_HOST_ERROR;
  }
  worker->active=1; ++runtime->native_retained;
  ++runtime->work_snapshot.worker_threads;
  peak(runtime->work_snapshot.worker_threads,&runtime->work_snapshot.worker_threads_peak);
  runtime->work_snapshot.worker_stack_bytes+=runtime->m2_limits.worker_stack_bytes;
  peak(runtime->work_snapshot.worker_stack_bytes,&runtime->work_snapshot.worker_stack_bytes_peak);
  return QAPLET_OK;
}
qaplet_code qaplet_work_submit(qaplet_runtime *runtime,const char *name,size_t name_length,
                               const void *input,size_t input_length,qaplet_work_task *output,qaplet_diagnostic *diagnostic) {
  qaplet_code code=qaplet_check_diagnostic(diagnostic);
  if (code) return code;
  if (!valid_name(name,name_length) || (!input && input_length) || !output) code=QAPLET_INVALID_ARGUMENT;
  if (!code) code=open_owner(runtime);
  if (code) return qaplet_report(diagnostic,code);
  work_handler *handler=find_handler(runtime->work_pool,name,name_length);
  if (!handler) return qaplet_report(diagnostic,QAPLET_NO_HANDLER);
  if (input_length>handler->input_bytes) return qaplet_report(diagnostic,QAPLET_VALUE_TOO_LARGE);
  if (input_length>SIZE_MAX-handler->output_bytes) return qaplet_report(diagnostic,QAPLET_RESOURCE_LIMIT);
  size_t bytes=input_length+handler->output_bytes;
  qaplet_work_pool *pool=runtime->work_pool;
  size_t metadata=0;
  if (!pool->tasks) {
    if (runtime->m2_limits.operations>SIZE_MAX/sizeof(work_task) ||
        runtime->m2_limits.worker_threads>SIZE_MAX/sizeof(work_worker))
      return qaplet_report(diagnostic,QAPLET_RESOURCE_LIMIT);
    size_t slots=runtime->m2_limits.operations*sizeof(work_task), workers=runtime->m2_limits.worker_threads*sizeof(work_worker);
    if (slots>SIZE_MAX-workers || bytes>SIZE_MAX-slots-workers)
      return qaplet_report(diagnostic,QAPLET_RESOURCE_LIMIT);
    metadata=slots+workers;
  }
  pthread_mutex_lock(&runtime->mutex);
  if (runtime->work_snapshot.operations>=runtime->m2_limits.operations ||
      bytes>runtime->m2_limits.operation_data_bytes-runtime->work_snapshot.data_bytes ||
      !fits_locked(runtime,bytes+metadata) || pool->sequence==UINT64_MAX) code=QAPLET_RESOURCE_LIMIT;
  pthread_mutex_unlock(&runtime->mutex);
  if (!code) code=storage(runtime);
  if (code) goto rejected;
  /* Native allocations are the only potentially failing storage step. Once
   * acceptance is published, completion has no allocation dependency. */
  if (capacity(runtime,bytes)) { code=QAPLET_RESOURCE_LIMIT; goto rejected; }
  unsigned char *data=qaplet_native_alloc(runtime,bytes);
  if (bytes && !data) { code=QAPLET_MEMORY_LIMIT; goto rejected; }
  if (input_length) memcpy(data,input,input_length);
  pthread_mutex_lock(&runtime->mutex);
  work_task *task=NULL; uint64_t slot=0;
  if (runtime->snapshot.lifecycle!=QAPLET_OPEN) code=QAPLET_BAD_STATE;
  else for (;slot<runtime->m2_limits.operations;++slot)
    if (!pool->tasks[slot].active && pool->tasks[slot].generation!=UINT64_MAX) { task=&pool->tasks[slot]; break; }
  if (!code && !task) code=QAPLET_RESOURCE_LIMIT;
  if (!code) code=start_worker_locked(runtime);
  if (!code) {
    runtime->host_registration_locked=1;
    uint64_t generation=task->generation+1;
    *task=(work_task){.generation=generation,.sequence=++pool->sequence,.handler=handler,
      .data=data,.input_length=input_length,.data_size=bytes,.state=QAPLET_WORK_QUEUED,.active=1,.handoff=1};
    *output=(qaplet_work_task){runtime->runtime_id,slot,generation};
    ++runtime->work_snapshot.operations; ++runtime->work_snapshot.queued; ++runtime->work_snapshot.pending_handoffs;
    runtime->work_snapshot.data_bytes+=bytes;
    peak(runtime->work_snapshot.operations,&runtime->work_snapshot.operations_peak);
    peak(runtime->work_snapshot.data_bytes,&runtime->work_snapshot.data_bytes_peak);
    increment(&runtime->work_snapshot.accepted);
    pthread_cond_broadcast(&runtime->native_condition);
    qaplet_wake_locked(runtime);
  }
  pthread_mutex_unlock(&runtime->mutex);
  if (code) qaplet_native_free(runtime,data,bytes);
rejected:
  if (code) { pthread_mutex_lock(&runtime->mutex); increment(&runtime->work_snapshot.rejected); pthread_mutex_unlock(&runtime->mutex); }
  return qaplet_report(diagnostic,code);
}
static work_task *lookup(qaplet_runtime *runtime,const qaplet_work_task *token) {
  qaplet_work_pool *pool=runtime->work_pool;
  if (!pool || !pool->tasks || runtime->snapshot.lifecycle==QAPLET_CLOSED || token->runtime_id!=runtime->runtime_id ||
      token->slot>=runtime->m2_limits.operations || !token->generation) return NULL;
  work_task *task=&pool->tasks[token->slot];
  return task->active && !task->released && task->generation==token->generation ? task : NULL;
}
qaplet_code qaplet_work_copy_result(qaplet_runtime *runtime,const qaplet_work_task *token,
                                    qaplet_work_result *output,qaplet_diagnostic *diagnostic) {
  qaplet_code code=qaplet_check_diagnostic(diagnostic);
  if (code) return code;
  code=qaplet_check_structure(output,sizeof(*output));
  if (!code && (!token || (!output->buffer && output->capacity))) code=QAPLET_INVALID_ARGUMENT;
  if (!code) code=qaplet_owner(runtime);
  if (!code && !runtime->m2_enabled) code=QAPLET_UNSUPPORTED;
  if (code) return qaplet_report(diagnostic,code);
  pthread_mutex_lock(&runtime->mutex);
  work_task *task=lookup(runtime,token);
  if (!task) code=QAPLET_INVALID_HANDLE;
  else if (!terminal(task)) code=QAPLET_BAD_STATE;
  else {
    output->state=task->state; output->code=task->code; output->length=task->length;
    output->error_domain=task->error_domain; output->error=task->error;
    if (task->length>output->capacity) code=QAPLET_BUFFER_TOO_SMALL;
    else if (task->length) memcpy(output->buffer,task->data+task->input_length,task->length);
  }
  pthread_mutex_unlock(&runtime->mutex);
  return qaplet_report(diagnostic,code);
}
static void retire_task(qaplet_runtime *runtime,work_task *task) {
  free_locked(runtime,task->data,task->data_size);
  runtime->work_snapshot.data_bytes-=task->data_size; --runtime->work_snapshot.operations;
  if (terminal(task)) --runtime->work_snapshot.retained_results;
  if (task->handoff) --runtime->work_snapshot.pending_handoffs;
  task->active=0; task->data=NULL;
}
qaplet_code qaplet_work_release(qaplet_runtime *runtime,const qaplet_work_task *token,qaplet_diagnostic *diagnostic) {
  qaplet_code code=qaplet_check_diagnostic(diagnostic);
  if (code) return code;
  if (!token) code=QAPLET_INVALID_ARGUMENT;
  if (!code) code=qaplet_owner(runtime);
  if (!code && !runtime->m2_enabled) code=QAPLET_UNSUPPORTED;
  if (code) return qaplet_report(diagnostic,code);
  pthread_mutex_lock(&runtime->mutex);
  work_task *task=lookup(runtime,token);
  if (!task) code=QAPLET_INVALID_HANDLE;
  else if (!terminal(task)) code=QAPLET_BAD_STATE;
  else {
    task->released=1;
    if (task->handoff) { task->handoff=0; --runtime->work_snapshot.pending_handoffs; }
    if (!task->physical) retire_task(runtime,task);
  }
  pthread_mutex_unlock(&runtime->mutex);
  return qaplet_report(diagnostic,code);
}
int qaplet_work_cancelled(qaplet_work_context *context) {
  if (!context) return 1;
  pthread_mutex_lock(&context->runtime->mutex);
  int result=context->task->cancelled;
  pthread_mutex_unlock(&context->runtime->mutex);
  return result;
}
qaplet_code qaplet_get_work_snapshot(qaplet_runtime *runtime,qaplet_work_snapshot *output,qaplet_diagnostic *diagnostic) {
  qaplet_code code=qaplet_check_diagnostic(diagnostic);
  if (code) return code;
  code=qaplet_check_structure(output,sizeof(*output));
  if (!code && !runtime) code=QAPLET_INVALID_ARGUMENT;
  if (code) return qaplet_report(diagnostic,code);
  pthread_mutex_lock(&runtime->mutex);
  if (!runtime->m2_enabled) code=QAPLET_UNSUPPORTED;
  else memcpy((char *)output+8,(char *)&runtime->work_snapshot+8,sizeof(*output)-8);
  pthread_mutex_unlock(&runtime->mutex);
  return qaplet_report(diagnostic,code);
}
int qaplet_work_ready_locked(qaplet_runtime *runtime) {
  qaplet_work_pool *pool=runtime->work_pool;
  if (pool && pool->tasks) for (uint64_t i=0;i<runtime->m2_limits.operations;++i)
    if (pool->tasks[i].active && pool->tasks[i].handoff && terminal(&pool->tasks[i])) return 1;
  return 0;
}
unsigned qaplet_work_handoff(qaplet_runtime *runtime,uint64_t maximum) {
  unsigned count=0;
  pthread_mutex_lock(&runtime->mutex);
  qaplet_work_pool *pool=runtime->work_pool;
  if (pool && pool->tasks) for (uint64_t i=0;i<runtime->m2_limits.operations && count<maximum;++i) {
    work_task *task=&pool->tasks[i];
    if (!task->active || !task->handoff || !terminal(task)) continue;
    /* A native-only handoff creates no JS execution. An existing entry still
     * owns its cumulative dispatch/deadline ledger across this completion. */
    if (runtime->execution_active && qaplet_execution_dispatch_locked(runtime)) break;
    task->handoff=0; --runtime->work_snapshot.pending_handoffs; ++count;
  }
  pthread_mutex_unlock(&runtime->mutex);
  return count;
}
void qaplet_work_shutdown(qaplet_runtime *runtime) {
  pthread_mutex_lock(&runtime->mutex);
  qaplet_work_pool *pool=runtime->work_pool;
  if (pool && runtime->snapshot.lifecycle==QAPLET_CLOSED) pool->closing=1;
  if (pool && !pool->stopping) {
    pool->stopping=1;
    if (pool->tasks) for (uint64_t i=0;i<runtime->m2_limits.operations;++i) {
      work_task *task=&pool->tasks[i];
      if (!task->active) continue;
      if (!terminal(task)) {
        if (task->state==QAPLET_WORK_QUEUED) --runtime->work_snapshot.queued;
        task->cancelled=1; task->state=QAPLET_WORK_CANCELLED; task->length=0;
        ++runtime->work_snapshot.retained_results; increment(&runtime->work_snapshot.cancelled);
      }
      if (task->handoff) { task->handoff=0; --runtime->work_snapshot.pending_handoffs; }
    }
    pthread_cond_broadcast(&runtime->native_condition);
  }
  qaplet_work_reap_locked(runtime);
  pthread_mutex_unlock(&runtime->mutex);
}
void qaplet_work_close(qaplet_runtime *runtime) {
  pthread_mutex_lock(&runtime->mutex);
  if (runtime->work_pool) runtime->work_pool->closing=1;
  pthread_mutex_unlock(&runtime->mutex);
}
void qaplet_work_reap_locked(qaplet_runtime *runtime) {
  qaplet_work_pool *pool=runtime->work_pool;
  if (!pool || !pool->stopping) return;
  if (pool->workers) for (uint64_t i=0;i<runtime->m2_limits.worker_threads;++i) {
    work_worker *worker=&pool->workers[i];
    if (!worker->active || !worker->exited || worker->join_attempted) continue;
    worker->join_attempted=1;
    pthread_mutex_unlock(&runtime->mutex);
    /* Returning from worker_main does not prove exit: Host code can install
     * TLS destructors. A zero/finite quiesce must never block in their join. */
    int error=pthread_tryjoin_np(worker->thread,NULL);
    pthread_mutex_lock(&runtime->mutex);
    if (error==EBUSY) { worker->join_attempted=0; continue; }
    if (error) {
      if (!runtime->cleanup_reason) { runtime->cleanup_reason=QAPLET_CONTROL_FAILURE; runtime->cleanup_error=error; }
      continue; /* Retain uncertain thread ownership; no false quiescence. */
    }
    worker->active=0; --runtime->native_retained; --runtime->work_snapshot.worker_threads;
    runtime->work_snapshot.worker_stack_bytes-=runtime->m2_limits.worker_stack_bytes;
  }
  if (runtime->work_snapshot.worker_threads || !pool->closing) return;
  if (pool->tasks) for (uint64_t i=0;i<runtime->m2_limits.operations;++i)
    if (pool->tasks[i].active) retire_task(runtime,&pool->tasks[i]);
  work_handler *handler=pool->handlers;
  while (handler) { work_handler *next=handler->next; free_locked(runtime,handler,sizeof(*handler)+handler->name_length+1); handler=next; }
  runtime->work_snapshot.handlers=0;
  free_locked(runtime,pool->tasks,pool->task_bytes); free_locked(runtime,pool->workers,pool->worker_bytes);
  free_locked(runtime,pool,sizeof(*pool)); runtime->work_pool=NULL;
  --runtime->native_retained;
  if (!runtime->native_retained && !runtime->native_handles && !runtime->native_uncertain_handles && runtime->loop_closed)
    runtime->native_state=QAPLET_NATIVE_QUIESCENT;
}
