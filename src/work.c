/* SPDX-License-Identifier: MIT */
#include "runtime_internal.h"
#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

typedef struct work_handler work_handler;
typedef struct work_task work_task;
typedef struct work_worker work_worker;
/* Only POD lives in the JS wrapper. The native slot owns Promise capabilities
 * until owner delivery; workers never access any of those owner-only fields. */
typedef struct { qaplet_work_task token; unsigned refs; int terminal; qaplet_code code; } work_js;
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
  int active, physical, cancelled, handoff, released, referenced;
  work_js *js;
  JSValue resolve, reject;
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
    work_task *task=pool->stopping || runtime->snapshot.lifecycle!=QAPLET_OPEN ? NULL : next_task(runtime);
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
    if (!task->cancelled && runtime->snapshot.lifecycle!=QAPLET_OPEN) {
      task->cancelled=1;task->state=QAPLET_WORK_CANCELLED;task->length=0;
      task->code=QAPLET_CANCELLED;task->error_domain=0;task->error=0;
      ++runtime->work_snapshot.retained_results;increment(&runtime->work_snapshot.cancelled);
    }
    if (!task->cancelled) {
      if (output.length>handler->output_bytes) { code=QAPLET_VALUE_TOO_LARGE; output.length=0; }
      task->code=code; task->length=output.length;
      task->error_domain=output.error_domain; task->error=output.error;
      task->state=code ? QAPLET_WORK_FAILED : QAPLET_WORK_SUCCEEDED;
      ++runtime->work_snapshot.retained_results; increment(&runtime->work_snapshot.completed);
    }
    if (task->released && !task->handoff) retire_task(runtime,task);
    /* The pre-reserved record is the completion. No business event or malloc. */
    qaplet_wake_locked(runtime);
    qaplet_notification notification=qaplet_notify_prepare_locked(runtime);
    pthread_cond_broadcast(&runtime->native_condition);
    pthread_mutex_unlock(&runtime->mutex);
    qaplet_notify_dispatch(runtime,notification);
    pthread_mutex_lock(&runtime->mutex);
  }
  worker->exited=1;
  qaplet_notification notification=qaplet_notify_prepare_locked(runtime);
  pthread_cond_broadcast(&runtime->native_condition);
  pthread_mutex_unlock(&runtime->mutex);
  qaplet_notify_dispatch(runtime,notification);
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
static qaplet_code submit(qaplet_runtime *runtime,const char *name,size_t name_length,
    const void *input,size_t input_length,qaplet_work_task *output,work_js *js,JSValue *capabilities,qaplet_diagnostic *diagnostic) {
  qaplet_code code=qaplet_check_diagnostic(diagnostic);
  if (code) return code;
  if (!valid_name(name,name_length) || (!input && input_length) || !output) code=QAPLET_INVALID_ARGUMENT;
  if (!code && !js) code=open_owner(runtime);
  if (!code && js && (!runtime->m2_enabled || qaplet_get_lifecycle(runtime)!=QAPLET_OPEN)) code=QAPLET_BAD_STATE;
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
  if (runtime->operations_current>=runtime->m2_limits.operations ||
      bytes>runtime->m2_limits.operation_data_bytes-runtime->operation_data_current ||
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
  qaplet_notification notification={0};
  pthread_mutex_lock(&runtime->mutex);
  work_task *task=NULL; uint64_t slot=0;
  if (runtime->snapshot.lifecycle!=QAPLET_OPEN) code=QAPLET_BAD_STATE;
  else for (;slot<runtime->m2_limits.operations;++slot)
    if (!pool->tasks[slot].active && pool->tasks[slot].generation!=UINT64_MAX) { task=&pool->tasks[slot]; break; }
  if (!code && !task) code=QAPLET_RESOURCE_LIMIT;
  if (!code) code=start_worker_locked(runtime);
  if (!code) code=qaplet_operation_reserve_locked(runtime,bytes);
  if (!code) {
    runtime->host_registration_locked=1;
    uint64_t generation=task->generation+1;
    *task=(work_task){.generation=generation,.sequence=++pool->sequence,.handler=handler,
      .data=data,.input_length=input_length,.data_size=bytes,.state=QAPLET_WORK_QUEUED,.active=1,.handoff=1,.referenced=1,
      .js=js,.resolve=js ? capabilities[0] : JS_UNDEFINED,.reject=js ? capabilities[1] : JS_UNDEFINED};
    *output=(qaplet_work_task){runtime->runtime_id,slot,generation};
    if (js) {js->token=*output;++js->refs;}
    ++runtime->work_snapshot.operations; ++runtime->work_snapshot.queued; ++runtime->work_snapshot.pending_handoffs;
    runtime->work_snapshot.data_bytes+=bytes;
    peak(runtime->work_snapshot.operations,&runtime->work_snapshot.operations_peak);
    peak(runtime->work_snapshot.data_bytes,&runtime->work_snapshot.data_bytes_peak);
    increment(&runtime->work_snapshot.accepted);
    pthread_cond_broadcast(&runtime->native_condition);
    qaplet_wake_locked(runtime);
    notification=qaplet_notify_prepare_locked(runtime);
  }
  pthread_mutex_unlock(&runtime->mutex);
  qaplet_notify_dispatch(runtime,notification);
  if (code) qaplet_native_free(runtime,data,bytes);
rejected:
  if (code) { pthread_mutex_lock(&runtime->mutex); increment(&runtime->work_snapshot.rejected); pthread_mutex_unlock(&runtime->mutex); }
  return qaplet_report(diagnostic,code);
}
qaplet_code qaplet_work_submit(qaplet_runtime *runtime,const char *name,size_t name_length,
    const void *input,size_t input_length,qaplet_work_task *output,qaplet_diagnostic *diagnostic) {
  return submit(runtime,name,name_length,input,input_length,output,NULL,NULL,diagnostic);
}
static work_task *lookup(qaplet_runtime *runtime,const qaplet_work_task *token) {
  qaplet_work_pool *pool=runtime->work_pool;
  if (!pool || !pool->tasks || runtime->snapshot.lifecycle==QAPLET_CLOSED || token->runtime_id!=runtime->runtime_id ||
      token->slot>=runtime->m2_limits.operations || !token->generation) return NULL;
  work_task *task=&pool->tasks[token->slot];
  return task->active && !task->released && task->generation==token->generation ? task : NULL;
}
qaplet_code qaplet_work_cancel(qaplet_runtime *runtime,const qaplet_work_task *token,qaplet_diagnostic *diagnostic) {
  qaplet_code code=qaplet_check_diagnostic(diagnostic);
  if (code) return code;
  if (!runtime || !token) return qaplet_report(diagnostic,QAPLET_INVALID_ARGUMENT);
  qaplet_notification notification={0};
  pthread_mutex_lock(&runtime->mutex);
  work_task *task=lookup(runtime,token);
  if (!runtime->m2_enabled) code=QAPLET_UNSUPPORTED;
  else if (!task) code=QAPLET_INVALID_HANDLE;
  else if (terminal(task)) code=task->state==QAPLET_WORK_CANCELLED ? QAPLET_OK : QAPLET_ALREADY_COMPLETED;
  else {
    if (task->state==QAPLET_WORK_QUEUED) --runtime->work_snapshot.queued;
    task->cancelled=1; task->state=QAPLET_WORK_CANCELLED;
    task->code=QAPLET_CANCELLED; task->length=0; task->error_domain=0; task->error=0;
    ++runtime->work_snapshot.retained_results; increment(&runtime->work_snapshot.cancelled);
    /* Publication is accepted even if its following wake records failure. */
    qaplet_wake_locked(runtime);
    notification=qaplet_notify_prepare_locked(runtime);
    pthread_cond_broadcast(&runtime->native_condition);
  }
  pthread_mutex_unlock(&runtime->mutex);
  qaplet_notify_dispatch(runtime,notification);
  return qaplet_report(diagnostic,code);
}
qaplet_code qaplet_get_work_status(qaplet_runtime *runtime,const qaplet_work_task *token,
                                   qaplet_work_status *output,qaplet_diagnostic *diagnostic) {
  qaplet_code code=qaplet_check_diagnostic(diagnostic);
  if (code) return code;
  code=qaplet_check_structure(output,sizeof(*output));
  if (!code && (!runtime || !token)) code=QAPLET_INVALID_ARGUMENT;
  if (code) return qaplet_report(diagnostic,code);
  pthread_mutex_lock(&runtime->mutex);
  work_task *task=lookup(runtime,token);
  if (!runtime->m2_enabled) code=QAPLET_UNSUPPORTED;
  else if (!task) code=QAPLET_INVALID_HANDLE;
  else {
    output->state=task->state; output->code=task->code; output->length=task->length;
    output->error_domain=task->error_domain; output->error=task->error;
    output->physical_pending=task->physical; output->pending_handoff=task->handoff;
  }
  pthread_mutex_unlock(&runtime->mutex);
  return qaplet_report(diagnostic,code);
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
  qaplet_operation_retire_locked(runtime,task->data_size);
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
  qaplet_notification notification={0};
  pthread_mutex_lock(&runtime->mutex);
  work_task *task=lookup(runtime,token);
  if (!task) code=QAPLET_INVALID_HANDLE;
  else if (!terminal(task)) code=QAPLET_BAD_STATE;
  else {
    task->released=1;
    /* Token invalidation is immediate. Ref/record retirement remains an owner
     * handoff after a complete jobs checkpoint, even if C has copied/released. */
    if (!task->physical && !task->handoff) retire_task(runtime,task);
    qaplet_wake_locked(runtime);
    notification=qaplet_notify_prepare_locked(runtime);
  }
  pthread_mutex_unlock(&runtime->mutex);
  qaplet_notify_dispatch(runtime,notification);
  return qaplet_report(diagnostic,code);
}
int qaplet_work_cancelled(qaplet_work_context *context) {
  if (!context) return 1;
  pthread_mutex_lock(&context->runtime->mutex);
  int result=context->task->cancelled || context->runtime->snapshot.lifecycle!=QAPLET_OPEN;
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
    if (pool->tasks[i].active && pool->tasks[i].handoff==1 && terminal(&pool->tasks[i])) return 1;
  return 0;
}
int qaplet_work_referenced_locked(qaplet_runtime *runtime) {
  qaplet_work_pool *pool=runtime->work_pool;
  if (pool && pool->tasks) for(uint64_t i=0;i<runtime->m2_limits.operations;++i)
    if (pool->tasks[i].active && pool->tasks[i].handoff && pool->tasks[i].referenced) return 1;
  return 0;
}
static JSValue work_error(JSContext *context,qaplet_code code,uint32_t domain,int32_t native) {
  JSValue error=JS_NewError(context);
  if (JS_IsException(error)) return error;
  if (JS_DefinePropertyValueStr(context,error,"code",JS_NewInt32(context,code),JS_PROP_C_W_E)<0 ||
      JS_DefinePropertyValueStr(context,error,"errorDomain",JS_NewUint32(context,domain),JS_PROP_C_W_E)<0 ||
      JS_DefinePropertyValueStr(context,error,"error",JS_NewInt32(context,native),JS_PROP_C_W_E)<0 ||
      JS_DefinePropertyValueStr(context,error,"message",JS_NewString(context,"Work operation failed"),JS_PROP_C_W_E)<0) {
    JS_FreeValue(context,error);return JS_EXCEPTION;
  }
  return error;
}
static void release_js(qaplet_runtime *runtime,work_task *task) {
  JS_FreeValue(runtime->context,task->resolve);JS_FreeValue(runtime->context,task->reject);
  task->resolve=task->reject=JS_UNDEFINED;
  if (task->js && !--task->js->refs) js_free(runtime->context,task->js);
  task->js=NULL;
}
void qaplet_work_checkpoint(qaplet_runtime *runtime) {
  qaplet_work_pool *pool=runtime->work_pool;
  if (!pool || !pool->tasks) return;
  for (uint64_t i=0;i<runtime->m2_limits.operations;++i) {
    work_task *task=&pool->tasks[i];
    pthread_mutex_lock(&runtime->mutex);
    int pending=task->active && task->handoff==2;
    pthread_mutex_unlock(&runtime->mutex);
    if (!pending) continue;
    release_js(runtime,task);
    pthread_mutex_lock(&runtime->mutex);
    task->handoff=0;task->released=1;--runtime->work_snapshot.pending_handoffs;
    if (!task->physical) retire_task(runtime,task);
    pthread_mutex_unlock(&runtime->mutex);
  }
}
unsigned qaplet_work_handoff(qaplet_runtime *runtime,uint64_t maximum) {
  unsigned count=0;
  pthread_mutex_lock(&runtime->mutex);
  qaplet_work_pool *pool=runtime->work_pool;
  if (pool && pool->tasks) for (uint64_t i=0;i<runtime->m2_limits.operations && count<maximum;++i) {
    work_task *task=&pool->tasks[i];
    if (!task->active || task->handoff!=1 || !terminal(task)) continue;
    if (task->js && !runtime->execution_active) {
      pthread_mutex_unlock(&runtime->mutex);
      qaplet_execution_budget budget;
      qaplet_code code=qaplet_execution_prepare(runtime,NULL,&budget);
      if (!code) code=qaplet_execution_begin(runtime,&budget);
      if (!code) runtime->module_execution=0;
      pthread_mutex_lock(&runtime->mutex);
      if (code) break;
    }
    /* A native-only handoff creates no JS execution. An existing entry still
     * owns its cumulative dispatch/deadline ledger across this completion. */
    if (runtime->execution_active && qaplet_execution_dispatch_locked(runtime)) break;
    ++count;
    if (task->js) {
      task->handoff=2;task->js->terminal=1;task->js->code=task->code;
      pthread_mutex_unlock(&runtime->mutex);
      JSContext *context=runtime->context;
      JSValue value;
      if (task->code) value=work_error(context,task->code,task->error_domain,task->error);
      else {
        JSValue buffer=JS_NewArrayBufferCopy(context,task->data+task->input_length,task->length);
        JSValue arguments[]={buffer,JS_UNDEFINED,JS_UNDEFINED};
        value=JS_IsException(buffer) ? JS_EXCEPTION : JS_NewTypedArray(context,3,arguments,JS_TYPED_ARRAY_UINT8);
        JS_FreeValue(context,buffer);
      }
      JSValue settled=JS_IsException(value) ? JS_EXCEPTION : JS_Call(context,task->code ? task->reject : task->resolve,JS_UNDEFINED,1,&value);
      JS_FreeValue(context,value);
      if (JS_IsException(settled)) {
        JSValue exception=JS_GetException(context);
        if (!qaplet_execution_check(runtime) && !runtime->execution_code) {
          qaplet_capture_exception(runtime,exception);
          runtime->execution_code=qaplet_module_exception_code(runtime,exception);
        }
        JS_FreeValue(context,exception);
      }
      JS_FreeValue(context,settled);
      pthread_mutex_lock(&runtime->mutex);
    } else {task->handoff=0;--runtime->work_snapshot.pending_handoffs;}
    if (task->released && !task->physical) retire_task(runtime,task);
  }
  pthread_mutex_unlock(&runtime->mutex);
  return count;
}
void qaplet_work_shutdown(qaplet_runtime *runtime) {
  /* Owner-only JS teardown precedes any native retirement. STOPPED/FAILED
   * never run the remaining Promise callbacks, including finally handlers. */
  qaplet_work_pool *owner_pool=runtime->work_pool;
  if (owner_pool && owner_pool->tasks) for(uint64_t i=0;i<runtime->m2_limits.operations;++i) {
    work_task *task=&owner_pool->tasks[i];
    pthread_mutex_lock(&runtime->mutex);
    int pending=task->active && task->js;
    pthread_mutex_unlock(&runtime->mutex);
    if (pending) {
      release_js(runtime,task);
      pthread_mutex_lock(&runtime->mutex);task->released=1;pthread_mutex_unlock(&runtime->mutex);
    }
  }
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
        task->code=QAPLET_CANCELLED; task->error_domain=0; task->error=0;
        ++runtime->work_snapshot.retained_results; increment(&runtime->work_snapshot.cancelled);
      }
      if (task->handoff) { task->handoff=0; --runtime->work_snapshot.pending_handoffs; }
      if (task->released && !task->physical) retire_task(runtime,task);
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
  if (!runtime->native_retained && !runtime->notify_inflight && !runtime->native_handles && !runtime->native_uncertain_handles && runtime->loop_closed)
    runtime->native_state=QAPLET_NATIVE_QUIESCENT;
}

static JSClassID work_class;
static pthread_once_t work_class_once=PTHREAD_ONCE_INIT;
static void allocate_work_class(void) {JS_NewClassID(&work_class);}
static void work_finalizer(JSRuntime *engine,JSValue value) {
  work_js *handle=JS_GetOpaque(value,work_class);
  if (handle && !--handle->refs) js_free_rt(engine,handle);
}
static JSValue throw_work(JSContext *context,qaplet_code code) {
  JSValue error=work_error(context,code,0,0);
  return JS_IsException(error) ? error : JS_Throw(context,error);
}
static JSValue work_method(JSContext *context,JSValueConst self,int argc,JSValueConst *argv,int magic) {
  (void)argc;(void)argv;
  qaplet_runtime *runtime=JS_GetContextOpaque(context);
  work_js *handle=JS_GetOpaque(self,work_class);
  if (!handle) return throw_work(context,QAPLET_INVALID_HANDLE);
  if (magic) {
    pthread_mutex_lock(&runtime->mutex);
    work_task *task=lookup(runtime,&handle->token);
    if (task && magic!=3) task->referenced=magic==1;
    int referenced=task && task->referenced;
    pthread_mutex_unlock(&runtime->mutex);
    return magic==3 ? JS_NewBool(context,referenced) : JS_UNDEFINED;
  }
  qaplet_code code=handle->terminal ? (handle->code==QAPLET_CANCELLED ? QAPLET_OK : QAPLET_ALREADY_COMPLETED) :
    qaplet_work_cancel(runtime,&handle->token,NULL);
  return code ? throw_work(context,code) : JS_UNDEFINED;
}
/* Borrow a strict Uint8Array span only during synchronous acceptance. The
 * saved intrinsic getter avoids user properties and handles resized views. */
static qaplet_code input_bytes(qaplet_runtime *runtime,JSValueConst value,JSValue *buffer,unsigned char **bytes,size_t *length) {
  JSContext *context=runtime->context;
  if (!JS_IsObject(value) || JS_GetClassID(value)!=runtime->uint8_array_class) return QAPLET_VALUE_TYPE_UNSUPPORTED;
  size_t offset,ignored,element,backing;
  *buffer=JS_GetTypedArrayBuffer(context,value,&offset,&ignored,&element);
  if (JS_IsException(*buffer)) {JS_FreeValue(context,JS_GetException(context));*buffer=JS_UNDEFINED;return QAPLET_VALUE_TYPE_UNSUPPORTED;}
  if (JS_GetClassID(*buffer)!=runtime->array_buffer_class) return QAPLET_VALUE_TYPE_UNSUPPORTED;
  JSValue size=JS_Call(context,runtime->typed_array_byte_length,value,0,NULL);
  double count=0;
  if (JS_IsException(size) || JS_ToFloat64(context,&count,size)) {JS_FreeValue(context,size);return QAPLET_MEMORY_LIMIT;}
  JS_FreeValue(context,size);
  if (count>runtime->m2_limits.work_input_bytes) return QAPLET_VALUE_TOO_LARGE;
  *bytes=JS_GetArrayBuffer(context,&backing,*buffer);*length=(size_t)count;
  if (offset>backing || *length>backing-offset || (!*bytes && *length)) return QAPLET_VALUE_TYPE_UNSUPPORTED;
  if (*length) *bytes+=offset;
  return QAPLET_OK;
}
static JSValue js_submit(JSContext *context,JSValueConst self,int argc,JSValueConst *argv) {
  (void)self;
  qaplet_runtime *runtime=JS_GetContextOpaque(context);
  if (argc!=2 || !JS_IsString(argv[0])) return throw_work(context,QAPLET_INVALID_ARGUMENT);
  size_t name_length,length=0;
  qaplet_code code=qaplet_js_string_size(runtime,argv[0],&name_length);
  if (code) return JS_HasException(context) ? JS_EXCEPTION : throw_work(context,code);
  const char *name=JS_ToCStringLen(context,&name_length,argv[0]);
  if (!name) return JS_EXCEPTION;
  JSValue buffer=JS_UNDEFINED;unsigned char *bytes=NULL;
  code=input_bytes(runtime,argv[1],&buffer,&bytes,&length);
  JSValue handle=JS_UNDEFINED,capabilities[]={JS_UNDEFINED,JS_UNDEFINED};
  if (!code) {
    handle=JS_NewObjectClass(context,work_class);
    if (JS_IsException(handle)) code=QAPLET_MEMORY_LIMIT;
  }
  work_js *token=NULL;
  if (!code) {
    token=js_mallocz(context,sizeof(*token));
    if (!token) code=QAPLET_MEMORY_LIMIT;
    else {token->refs=1;JS_SetOpaque(handle,token);}
  }
  if (!code) {
    JSValue promise=JS_NewPromiseCapability(context,capabilities);
    if (JS_IsException(promise) || JS_DefinePropertyValueStr(context,handle,"promise",promise,JS_PROP_ENUMERABLE)<0) code=QAPLET_MEMORY_LIMIT;
  }
  if (!code) code=submit(runtime,name,name_length,bytes,length,&token->token,token,capabilities,NULL);
  JS_FreeCString(context,name);JS_FreeValue(context,buffer);
  if (code) {
    JS_FreeValue(context,capabilities[0]);JS_FreeValue(context,capabilities[1]);JS_FreeValue(context,handle);
    if (JS_HasException(context)) return JS_EXCEPTION;
    return throw_work(context,code);
  }
  return handle;
}
static int initialize_work(JSContext *context,JSModuleDef *module) {
  return JS_SetModuleExport(context,module,"submit",JS_NewCFunction(context,js_submit,"submit",2));
}
static const JSCFunctionListEntry work_methods[]={
  JS_CFUNC_MAGIC_DEF("cancel",0,work_method,0),JS_CFUNC_MAGIC_DEF("ref",0,work_method,1),
  JS_CFUNC_MAGIC_DEF("unref",0,work_method,2),JS_CFUNC_MAGIC_DEF("hasRef",0,work_method,3),
};
JSModuleDef *qaplet_work_load(qaplet_runtime *runtime) {
  pthread_once(&work_class_once,allocate_work_class);
  JSClassDef definition={"QapletWorkTask",.finalizer=work_finalizer};
  if (JS_NewClass(runtime->engine,work_class,&definition)<0) return NULL;
  JSValue prototype=JS_NewObject(runtime->context);
  if (JS_IsException(prototype)) return NULL;
  if (JS_SetPropertyFunctionList(runtime->context,prototype,work_methods,sizeof(work_methods)/sizeof(*work_methods))<0) {
    JS_FreeValue(runtime->context,prototype);return NULL;
  }
  JS_SetClassProto(runtime->context,work_class,prototype);
  JSModuleDef *module=JS_NewCModule(runtime->context,"qaplet:work",initialize_work);
  if (module && JS_AddModuleExport(runtime->context,module,"submit")<0) return NULL;
  return module;
}
