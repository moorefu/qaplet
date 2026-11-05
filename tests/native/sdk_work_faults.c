/* SPDX-License-Identifier: MIT */
/* Controlled OS/allocation seam; consumes only the supplied public SDK. */
#include "qaplet.h"
#include <errno.h>
#include <pthread.h>
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#define CHECK(c) do { if (!(c)) { fprintf(stderr,"work system seam line %d: %s\n",__LINE__,#c); return 1; } } while (0)
static int fail_allocation, fail_thread, fail_join, fail_send, executions;
/* Exercise the shipped file consumer when no global /tmp has been supplied.
 * The redirected parent is deliberately absent below a directory we own;
 * mkstemp itself remains the real OS operation. Relative fixtures are untouched. */
static char unavailable_tmp[128];
int sdk_work(const char *);
int __real_mkstemp(char *);
int __wrap_mkstemp(char *path) {
  return __real_mkstemp(!strncmp(path,"/tmp/",5) ? unavailable_tmp : path);
}
void *__real_calloc(size_t,size_t);
void *__wrap_calloc(size_t n,size_t size) {
  if (fail_allocation) { fail_allocation=0;errno=ENOMEM;return NULL; }
  return __real_calloc(n,size);
}
int __real_pthread_create(pthread_t *,const pthread_attr_t *,void *(*)(void *),void *);
int __wrap_pthread_create(pthread_t *thread,const pthread_attr_t *attr,void *(*function)(void *),void *data) {
  if (fail_thread) { fail_thread=0;return EAGAIN; }
  return __real_pthread_create(thread,attr,function,data);
}
int __real_pthread_tryjoin_np(pthread_t,void **);
int __wrap_pthread_tryjoin_np(pthread_t thread,void **value) {
  if (fail_join) return EIO;
  return __real_pthread_tryjoin_np(thread,value);
}
ssize_t __real_send(int,const void *,size_t,int);
ssize_t __wrap_send(int fd,const void *data,size_t length,int flags) {
  if (fail_send) {fail_send=0;errno=EIO;return -1;}
  return __real_send(fd,data,length,flags);
}
static qaplet_code copy(qaplet_work_context *context,const void *input,size_t length,
                         qaplet_work_output *output,void *userdata) {
  (void)context;(void)userdata;
  ++executions;
  if (length) memcpy(output->data,input,length);
  output->length=length;return QAPLET_OK;
}
typedef struct {pthread_mutex_t mutex;pthread_cond_t condition;int entered,released;} gate;
static qaplet_code held(qaplet_work_context *context,const void *input,size_t length,
                         qaplet_work_output *output,void *userdata) {
  gate *g=userdata;pthread_mutex_lock(&g->mutex);g->entered=1;pthread_cond_broadcast(&g->condition);
  while (!g->released) pthread_cond_wait(&g->condition,&g->mutex);
  pthread_mutex_unlock(&g->mutex);return copy(context,input,length,output,NULL);
}
static int make(qaplet_runtime **runtime) {
  qaplet_config base=QAPLET_CONFIG_INIT;qaplet_m2_config config=QAPLET_M2_CONFIG_INIT;
  CHECK(qaplet_create_m2(&base,&config,runtime,NULL)==QAPLET_OK);
  qaplet_work_handler h=QAPLET_WORK_HANDLER_INIT;
  h.name="copy";h.name_length=4;h.function=copy;h.input_bytes=1;h.output_bytes=1;
  CHECK(qaplet_register_work_handler(*runtime,&h,NULL)==QAPLET_OK);return 0;
}
static int finish(qaplet_runtime *runtime) {
  qaplet_drive_result drive=QAPLET_DRIVE_RESULT_INIT;
  CHECK(qaplet_close(runtime,NULL)==QAPLET_OK);
  CHECK(qaplet_quiesce(runtime,100,&drive,NULL)==QAPLET_OK && drive.reason==QAPLET_DRIVE_QUIESCENT);
  CHECK(qaplet_destroy(runtime,NULL)==QAPLET_OK);return 0;
}
static qaplet_code arm_allocation(qaplet_runtime *runtime,uint64_t call,size_t argc,const qaplet_value *argv,void *userdata) {
  (void)runtime;(void)call;(void)argv;(void)userdata;
  if(argc)return QAPLET_INVALID_ARGUMENT;
  fail_allocation=1;return QAPLET_OK;
}
static int js_allocation_contract(void) {
  qaplet_runtime *runtime=NULL;CHECK(!make(&runtime));
  qaplet_host_export e=QAPLET_HOST_EXPORT_INIT;e.name="arm";e.name_length=3;e.kind=QAPLET_HOST_FUNCTION;e.function=arm_allocation;
  qaplet_host_module m=QAPLET_HOST_MODULE_INIT;m.name="host:fault";m.name_length=10;m.exports=&e;m.export_count=1;
  CHECK(qaplet_register_host_module(runtime,&m,NULL)==QAPLET_OK);
  const char *source="Promise.all([import('qaplet:work'),import('host:fault')]).then(([w,h])=>{globalThis.submit=w.submit;globalThis.arm=h.arm})";
  qaplet_progress progress=QAPLET_PROGRESS_INIT;
  CHECK(qaplet_eval(runtime,source,strlen(source),&progress,NULL)==QAPLET_OK);
  while(progress.phase==QAPLET_PHASE_PENDING)CHECK(qaplet_pump(runtime,&progress,NULL)==QAPLET_OK);
  source="arm();try{submit('copy',new Uint8Array([17]))}catch(e){}42";
  CHECK(qaplet_eval(runtime,source,strlen(source),&progress,NULL)==QAPLET_MEMORY_LIMIT && !fail_allocation);
  qaplet_work_snapshot work=QAPLET_WORK_SNAPSHOT_INIT;qaplet_m2_snapshot state=QAPLET_M2_SNAPSHOT_INIT;
  CHECK(qaplet_get_work_snapshot(runtime,&work,NULL)==QAPLET_OK && !work.accepted && !work.operations && !work.data_bytes && !work.worker_threads);
  CHECK(qaplet_get_m2_snapshot(runtime,&state,NULL)==QAPLET_OK && state.lifecycle==QAPLET_FAILED && state.termination_reason==QAPLET_MEMORY_LIMIT);
  CHECK(qaplet_request_stop(runtime,NULL)==QAPLET_OK);
  CHECK(qaplet_get_m2_snapshot(runtime,&state,NULL)==QAPLET_OK && state.termination_reason==QAPLET_MEMORY_LIMIT && !finish(runtime));
  puts("controlled M2 work JS allocation: actual SDK calloc refusal from caught JS submit remains permanent; no accepted task; first cause preserved; native and engine cleanup: pass");return 0;
}
int main(void) {
  CHECK(!js_allocation_contract());
  char directory[]="./qaplet-work-no-tmp-XXXXXX";
  CHECK(mkdtemp(directory));
  CHECK(snprintf(unavailable_tmp,sizeof(unavailable_tmp),"%s/absent/work-XXXXXX",directory)>0);
  CHECK(!sdk_work("--m2-work-file"));
  CHECK(!rmdir(directory));
  qaplet_runtime *runtime=NULL;qaplet_work_task task=QAPLET_WORK_TASK_INIT;unsigned char input=17;
  CHECK(!make(&runtime));fail_thread=1;
  CHECK(qaplet_work_submit(runtime,"copy",4,&input,1,&task,NULL)==QAPLET_HOST_ERROR && !task.runtime_id);
  qaplet_work_snapshot stats=QAPLET_WORK_SNAPSHOT_INIT;
  qaplet_m2_snapshot snapshot=QAPLET_M2_SNAPSHOT_INIT;
  CHECK(qaplet_get_work_snapshot(runtime,&stats,NULL)==QAPLET_OK && !stats.accepted && !stats.operations && !stats.worker_threads && stats.start_error_domain==QAPLET_ERROR_DOMAIN_ERRNO && stats.start_error==EAGAIN);
  CHECK(qaplet_get_m2_snapshot(runtime,&snapshot,NULL)==QAPLET_OK && snapshot.lifecycle==QAPLET_OPEN && !snapshot.termination_reason);
  CHECK(qaplet_work_submit(runtime,"copy",4,&input,1,&task,NULL)==QAPLET_OK);
  qaplet_drive_result drive=QAPLET_DRIVE_RESULT_INIT;
  CHECK(qaplet_run(runtime,&drive,NULL)==QAPLET_OK && !finish(runtime));
  CHECK(!make(&runtime));task=(qaplet_work_task)QAPLET_WORK_TASK_INIT;fail_allocation=1;
  CHECK(qaplet_work_submit(runtime,"copy",4,&input,1,&task,NULL)==QAPLET_MEMORY_LIMIT && !task.runtime_id);
  CHECK(qaplet_get_work_snapshot(runtime,&stats,NULL)==QAPLET_OK && !stats.operations && !stats.accepted && !stats.worker_threads);
  CHECK(qaplet_get_m2_snapshot(runtime,&snapshot,NULL)==QAPLET_OK && snapshot.lifecycle==QAPLET_FAILED && snapshot.termination_reason==QAPLET_MEMORY_LIMIT);
  CHECK(qaplet_request_stop(runtime,NULL)==QAPLET_OK);
  CHECK(qaplet_get_m2_snapshot(runtime,&snapshot,NULL)==QAPLET_OK && snapshot.termination_reason==QAPLET_MEMORY_LIMIT);
  CHECK(!finish(runtime));
  CHECK(!make(&runtime));task=(qaplet_work_task)QAPLET_WORK_TASK_INIT;executions=0;fail_send=1;
  CHECK(qaplet_work_submit(runtime,"copy",4,&input,1,&task,NULL)==QAPLET_OK && task.runtime_id);
  CHECK(qaplet_get_work_snapshot(runtime,&stats,NULL)==QAPLET_OK && stats.accepted==1 && stats.operations==1);
  CHECK(qaplet_get_m2_snapshot(runtime,&snapshot,NULL)==QAPLET_OK && snapshot.termination_reason==QAPLET_CONTROL_FAILURE && snapshot.control_error_domain==QAPLET_ERROR_DOMAIN_ERRNO && snapshot.control_error==EIO);
  CHECK(qaplet_run_once(runtime,0,&drive,NULL)==QAPLET_CONTROL_FAILURE && !executions);
  CHECK(!finish(runtime));
  CHECK(!make(&runtime));
  gate g={.mutex=PTHREAD_MUTEX_INITIALIZER,.condition=PTHREAD_COND_INITIALIZER};
  qaplet_work_handler handler=QAPLET_WORK_HANDLER_INIT;
  handler.name="held";handler.name_length=4;handler.function=held;handler.userdata=&g;handler.input_bytes=1;handler.output_bytes=1;
  CHECK(qaplet_register_work_handler(runtime,&handler,NULL)==QAPLET_OK);
  CHECK(qaplet_work_submit(runtime,"held",4,&input,1,&task,NULL)==QAPLET_OK);
  pthread_mutex_lock(&g.mutex);while (!g.entered) pthread_cond_wait(&g.condition,&g.mutex);pthread_mutex_unlock(&g.mutex);
  CHECK(qaplet_run_once(runtime,0,&drive,NULL)==QAPLET_OK && drive.reason==QAPLET_DRIVE_WAIT_TIMEOUT);
  fail_send=1;pthread_mutex_lock(&g.mutex);g.released=1;pthread_cond_broadcast(&g.condition);pthread_mutex_unlock(&g.mutex);
  do {CHECK(qaplet_get_work_snapshot(runtime,&stats,NULL)==QAPLET_OK);sched_yield();} while (!stats.completed);
  CHECK(stats.accepted==1 && stats.retained_results==1 && stats.pending_handoffs==1);
  qaplet_work_result result=QAPLET_WORK_RESULT_INIT;unsigned char bytes=0;result.buffer=&bytes;result.capacity=1;
  CHECK(qaplet_work_copy_result(runtime,&task,&result,NULL)==QAPLET_OK && result.state==QAPLET_WORK_SUCCEEDED && bytes==17);
  CHECK(qaplet_get_m2_snapshot(runtime,&snapshot,NULL)==QAPLET_OK && snapshot.termination_reason==QAPLET_CONTROL_FAILURE && snapshot.control_error==EIO);
  CHECK(qaplet_run_once(runtime,0,&drive,NULL)==QAPLET_CONTROL_FAILURE);
  CHECK(!finish(runtime) && !pthread_cond_destroy(&g.condition) && !pthread_mutex_destroy(&g.mutex));
  /* Cancellation has already been published when its real waker send fails.
   * Preserve acceptance and the logical result, with control failure separate. */
  CHECK(!make(&runtime));
  gate cancel_gate={.mutex=PTHREAD_MUTEX_INITIALIZER,.condition=PTHREAD_COND_INITIALIZER};
  handler.userdata=&cancel_gate;
  CHECK(qaplet_register_work_handler(runtime,&handler,NULL)==QAPLET_OK);
  CHECK(qaplet_work_submit(runtime,"held",4,&input,1,&task,NULL)==QAPLET_OK);
  pthread_mutex_lock(&cancel_gate.mutex);while (!cancel_gate.entered) pthread_cond_wait(&cancel_gate.condition,&cancel_gate.mutex);pthread_mutex_unlock(&cancel_gate.mutex);
  CHECK(qaplet_run_once(runtime,0,&drive,NULL)==QAPLET_OK && drive.reason==QAPLET_DRIVE_WAIT_TIMEOUT);
  fail_send=1;
  CHECK(qaplet_work_cancel(runtime,&task,NULL)==QAPLET_OK && qaplet_work_cancel(runtime,&task,NULL)==QAPLET_OK);
  qaplet_work_status status=QAPLET_WORK_STATUS_INIT;
  CHECK(qaplet_get_work_status(runtime,&task,&status,NULL)==QAPLET_OK && status.state==QAPLET_WORK_CANCELLED && status.code==QAPLET_CANCELLED && status.physical_pending && status.pending_handoff && !status.error_domain && !status.error);
  CHECK(qaplet_work_copy_result(runtime,&task,&result,NULL)==QAPLET_OK && result.state==QAPLET_WORK_CANCELLED && result.code==QAPLET_CANCELLED && !result.length);
  CHECK(qaplet_get_m2_snapshot(runtime,&snapshot,NULL)==QAPLET_OK && snapshot.termination_reason==QAPLET_CONTROL_FAILURE && snapshot.control_error==EIO);
  CHECK(qaplet_request_stop(runtime,NULL)==QAPLET_OK && qaplet_work_release(runtime,&task,NULL)==QAPLET_OK && qaplet_close(runtime,NULL)==QAPLET_OK);
  pthread_mutex_lock(&cancel_gate.mutex);cancel_gate.released=1;pthread_cond_broadcast(&cancel_gate.condition);pthread_mutex_unlock(&cancel_gate.mutex);
  CHECK(!finish(runtime) && !pthread_cond_destroy(&cancel_gate.condition) && !pthread_mutex_destroy(&cancel_gate.mutex));
  /* Deliberately uncertain join: no fake reclamation or second join attempt.
   * The Runtime/worker storage remains owned through process termination. */
  CHECK(!make(&runtime));
  CHECK(qaplet_work_submit(runtime,"copy",4,&input,1,&task,NULL)==QAPLET_OK);
  CHECK(qaplet_run(runtime,&drive,NULL)==QAPLET_OK);fail_join=1;
  qaplet_code closed=qaplet_close(runtime,NULL);
  CHECK(closed==QAPLET_OK || closed==QAPLET_CONTROL_FAILURE);
  CHECK(qaplet_quiesce(runtime,20,&drive,NULL)==QAPLET_CONTROL_FAILURE && drive.reason==QAPLET_DRIVE_WAIT_TIMEOUT);
  qaplet_m2_diagnostic detail=QAPLET_M2_DIAGNOSTIC_INIT;
  CHECK(qaplet_get_m2_diagnostic(runtime,&detail,NULL)==QAPLET_OK && !detail.primary_reason && detail.cleanup_reason==QAPLET_CONTROL_FAILURE && detail.cleanup_error_domain==QAPLET_ERROR_DOMAIN_ERRNO && detail.cleanup_error==EIO && detail.native_state==QAPLET_NATIVE_CLEANING);
  CHECK(qaplet_get_work_snapshot(runtime,&stats,NULL)==QAPLET_OK && stats.worker_threads==1 && stats.operations==1 && stats.data_bytes==2 && stats.handlers==1);
  CHECK(qaplet_get_m2_snapshot(runtime,&snapshot,NULL)==QAPLET_OK && snapshot.lifecycle==QAPLET_CLOSED && !snapshot.engine_current_bytes && !snapshot.termination_reason);
  CHECK(qaplet_destroy(runtime,NULL)==QAPLET_BUSY);
  fail_join=0; /* A now-successful backend must not revisit uncertain identity. */
  CHECK(qaplet_quiesce(runtime,0,&drive,NULL)==QAPLET_CONTROL_FAILURE && drive.reason==QAPLET_DRIVE_WAIT_TIMEOUT);
  puts("controlled M2 work: pthread create errno refuses atomically and recovers; actual calloc refusal permanent; accepted submit/completion/cancel survives wake failure; join error independently retained CLOSED/BUSY without identity retry or false cleanup: pass");
  return 0;
}
