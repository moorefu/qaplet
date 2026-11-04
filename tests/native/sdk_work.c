/* SPDX-License-Identifier: MIT */
/* Only the delivered public header and real native system interfaces. */
#include "qaplet.h"
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#define CHECK(c) do { if (!(c)) { fprintf(stderr,"work contract line %d: %s\n",__LINE__,#c); return 1; } } while (0)

static qaplet_code read_file(qaplet_work_context *context, const void *input, size_t length,
                             qaplet_work_output *output, void *userdata) {
  (void)context; (void)userdata;
  if (!length || ((const char *)input)[length-1]) return QAPLET_INVALID_ARGUMENT;
  int fd = open(input, O_RDONLY | O_CLOEXEC);
  if (fd < 0) { output->error_domain=QAPLET_ERROR_DOMAIN_ERRNO; output->error=errno; return QAPLET_HOST_ERROR; }
  ssize_t got = read(fd, output->data, output->capacity);
  int error = got < 0 ? errno : 0;
  if (close(fd) && !error) error=errno;
  if (error) { output->error_domain=QAPLET_ERROR_DOMAIN_ERRNO; output->error=error; return QAPLET_HOST_ERROR; }
  output->length=got;
  return QAPLET_OK;
}
static int finish(qaplet_runtime *runtime) {
  qaplet_drive_result drive=QAPLET_DRIVE_RESULT_INIT;
  CHECK(qaplet_close(runtime,NULL)==QAPLET_OK);
  CHECK(qaplet_quiesce(runtime,2000,&drive,NULL)==QAPLET_OK && drive.reason==QAPLET_DRIVE_QUIESCENT);
  qaplet_work_snapshot snapshot=QAPLET_WORK_SNAPSHOT_INIT;
  CHECK(qaplet_get_work_snapshot(runtime,&snapshot,NULL)==QAPLET_OK);
  CHECK(!snapshot.operations && !snapshot.data_bytes && !snapshot.worker_threads && !snapshot.worker_stack_bytes && !snapshot.handlers);
  CHECK(qaplet_destroy(runtime,NULL)==QAPLET_OK);
  return 0;
}
static int file_contract(void) {
  qaplet_config base=QAPLET_CONFIG_INIT;
  qaplet_m2_config config=QAPLET_M2_CONFIG_INIT;
  qaplet_runtime *runtime=NULL;
  CHECK(qaplet_create_m2(&base,&config,&runtime,NULL)==QAPLET_OK);
  char name[]="read";
  qaplet_work_handler handler=QAPLET_WORK_HANDLER_INIT;
  handler.name=name; handler.name_length=4; handler.function=read_file; handler.output_bytes=4;
  CHECK(qaplet_register_work_handler(runtime,&handler,NULL)==QAPLET_OK);
  memset(name,'x',4); handler.function=NULL;
  char path[]="/tmp/qaplet-work-XXXXXX";
  int fd=mkstemp(path); unsigned char expected[]={0,255,17,42,99};
  CHECK(fd>=0 && write(fd,expected,sizeof(expected))==sizeof(expected) && !close(fd));
  qaplet_work_task task=QAPLET_WORK_TASK_INIT;
  CHECK(qaplet_work_submit(runtime,"read",4,path,strlen(path)+1,&task,NULL)==QAPLET_OK);
  char saved[sizeof(path)]; memcpy(saved,path,sizeof(path)); memset(path,'?',sizeof(path));
  qaplet_drive_result drive=QAPLET_DRIVE_RESULT_INIT;
  CHECK(qaplet_run(runtime,&drive,NULL)==QAPLET_OK && drive.reason==QAPLET_DRIVE_IDLE && drive.progressed);
  qaplet_work_result result=QAPLET_WORK_RESULT_INIT;
  unsigned char bytes[5]={9,9,9,9,9}; result.buffer=bytes; result.capacity=3;
  CHECK(qaplet_work_copy_result(runtime,&task,&result,NULL)==QAPLET_BUFFER_TOO_SMALL && result.length==4 && bytes[0]==9);
  result.capacity=sizeof(bytes);
  CHECK(qaplet_work_copy_result(runtime,&task,&result,NULL)==QAPLET_OK && result.state==QAPLET_WORK_SUCCEEDED && !result.code && result.length==4 && !memcmp(bytes,expected,4) && bytes[4]==9);
  CHECK(qaplet_work_release(runtime,&task,NULL)==QAPLET_OK);
  CHECK(qaplet_work_copy_result(runtime,&task,&result,NULL)==QAPLET_INVALID_HANDLE);
  CHECK(qaplet_work_submit(runtime,"read",4,saved,strlen(saved)+1,&task,NULL)==QAPLET_OK);
  CHECK(qaplet_run(runtime,&drive,NULL)==QAPLET_OK);
  CHECK(qaplet_work_release(runtime,&task,NULL)==QAPLET_OK && !unlink(saved));
  CHECK(qaplet_work_submit(runtime,"read",4,saved,strlen(saved)+1,&task,NULL)==QAPLET_OK);
  CHECK(qaplet_run(runtime,&drive,NULL)==QAPLET_OK);
  CHECK(qaplet_work_copy_result(runtime,&task,&result,NULL)==QAPLET_OK && result.state==QAPLET_WORK_FAILED && result.code==QAPLET_HOST_ERROR && result.error_domain==QAPLET_ERROR_DOMAIN_ERRNO && result.error==ENOENT);
  CHECK(qaplet_work_release(runtime,&task,NULL)==QAPLET_OK);
  CHECK(!finish(runtime));
  puts("M2 work file: copied declaration/input; real bounded bytes; independent completion; retry result; ordinary errno; release; joined cleanup: pass");
  return 0;
}
typedef struct {
  pthread_mutex_t mutex;
  pthread_cond_t condition;
  unsigned entered, released, quick, exited, observed_cancel;
  unsigned tls_entered, tls_released;
  unsigned char seen;
} gate;
static qaplet_code held(qaplet_work_context *context,const void *input,size_t length,
                        qaplet_work_output *output,void *opaque) {
  gate *g=opaque;
  pthread_mutex_lock(&g->mutex);
  ++g->entered; pthread_cond_broadcast(&g->condition);
  while (!g->released) pthread_cond_wait(&g->condition,&g->mutex);
  g->seen=length ? *(const unsigned char *)input : 0;
  if (output->capacity) { *(unsigned char *)output->data=g->seen; output->length=1; }
  g->observed_cancel=qaplet_work_cancelled(context);
  ++g->exited; pthread_cond_broadcast(&g->condition);
  pthread_mutex_unlock(&g->mutex);
  return QAPLET_OK;
}
static qaplet_code quick(qaplet_work_context *context,const void *input,size_t length,
                         qaplet_work_output *output,void *opaque) {
  (void)context; (void)input; (void)length;
  gate *g=opaque;
  pthread_mutex_lock(&g->mutex); ++g->quick; pthread_cond_broadcast(&g->condition); pthread_mutex_unlock(&g->mutex);
  memset(output->data,42,output->capacity); output->length=output->capacity;
  return QAPLET_OK;
}
static void release_gate(gate *g) {
  pthread_mutex_lock(&g->mutex); g->released=1; pthread_cond_broadcast(&g->condition); pthread_mutex_unlock(&g->mutex);
}
static void wait_entered(gate *g,unsigned count,int fast) {
  pthread_mutex_lock(&g->mutex);
  while ((fast ? g->quick : g->entered)<count) pthread_cond_wait(&g->condition,&g->mutex);
  pthread_mutex_unlock(&g->mutex);
}
static int destroy_gate(gate *g) { CHECK(!pthread_cond_destroy(&g->condition) && !pthread_mutex_destroy(&g->mutex)); return 0; }
typedef struct { qaplet_runtime *runtime; int passed; } foreign_call;
static void *foreign_work(void *opaque) {
  foreign_call *call=opaque;qaplet_work_handler h=QAPLET_WORK_HANDLER_INIT;
  h.name="foreign";h.name_length=7;h.function=quick;
  qaplet_work_task task=QAPLET_WORK_TASK_INIT;qaplet_work_result result=QAPLET_WORK_RESULT_INIT;
  qaplet_work_snapshot snapshot=QAPLET_WORK_SNAPSHOT_INIT;
  call->passed=qaplet_register_work_handler(call->runtime,&h,NULL)==QAPLET_WRONG_THREAD &&
    qaplet_work_submit(call->runtime,"foreign",7,NULL,0,&task,NULL)==QAPLET_WRONG_THREAD &&
    qaplet_work_copy_result(call->runtime,&task,&result,NULL)==QAPLET_WRONG_THREAD &&
    qaplet_work_release(call->runtime,&task,NULL)==QAPLET_WRONG_THREAD &&
    qaplet_get_work_snapshot(call->runtime,&snapshot,NULL)==QAPLET_OK;
  return NULL;
}
static int registration_contract(void) {
  qaplet_config base=QAPLET_CONFIG_INIT; qaplet_m2_config config=QAPLET_M2_CONFIG_INIT;
  qaplet_runtime *runtime=NULL,*legacy=NULL;
  CHECK(qaplet_create_m2(&base,&config,&runtime,NULL)==QAPLET_OK);
  CHECK(qaplet_create(&base,&legacy,NULL)==QAPLET_OK);
  qaplet_abi abi=QAPLET_ABI_INIT;qaplet_m2_snapshot capabilities=QAPLET_M2_SNAPSHOT_INIT;
  CHECK(qaplet_query_abi(&abi)==QAPLET_OK && (abi.capabilities & QAPLET_CAP_M2_WORK));
  CHECK(qaplet_get_m2_snapshot(runtime,&capabilities,NULL)==QAPLET_OK && (capabilities.capabilities & QAPLET_CAP_M2_WORK));
  CHECK(qaplet_get_m2_snapshot(legacy,&capabilities,NULL)==QAPLET_OK && !(capabilities.capabilities & QAPLET_CAP_M2_WORK));
  qaplet_work_snapshot stats=QAPLET_WORK_SNAPSHOT_INIT;
  CHECK(qaplet_get_work_snapshot(runtime,&stats,NULL)==QAPLET_OK && !stats.worker_threads && !stats.handlers);
  foreign_call call={runtime,0};pthread_t foreign;
  CHECK(!pthread_create(&foreign,NULL,foreign_work,&call) && !pthread_join(foreign,NULL) && call.passed);
  qaplet_work_handler h=QAPLET_WORK_HANDLER_INIT;
  h.name="a";h.name_length=1;h.function=quick;h.input_bytes=1;h.output_bytes=1;
  CHECK(qaplet_register_work_handler(legacy,&h,NULL)==QAPLET_UNSUPPORTED);
  CHECK(qaplet_get_work_snapshot(legacy,&stats,NULL)==QAPLET_UNSUPPORTED);
  CHECK(qaplet_close(legacy,NULL)==QAPLET_OK && qaplet_destroy(legacy,NULL)==QAPLET_OK);
  h.concurrency=3;CHECK(qaplet_register_work_handler(runtime,&h,NULL)==QAPLET_INVALID_ARGUMENT);h.concurrency=0;
  h.flags=1;CHECK(qaplet_register_work_handler(runtime,&h,NULL)==QAPLET_INVALID_ARGUMENT);h.flags=0;
  h.struct_size=8;CHECK(qaplet_register_work_handler(runtime,&h,NULL)==QAPLET_ABI_MISMATCH);h.struct_size=sizeof(h);
  CHECK(qaplet_register_work_handler(runtime,&h,NULL)==QAPLET_OK);
  CHECK(qaplet_register_work_handler(runtime,&h,NULL)==QAPLET_BAD_STATE);
  qaplet_progress progress=QAPLET_PROGRESS_INIT;
  CHECK(qaplet_eval(runtime,"42",2,&progress,NULL)==QAPLET_OK);
  h.name="b";CHECK(qaplet_register_work_handler(runtime,&h,NULL)==QAPLET_BAD_STATE);
  CHECK(!finish(runtime));
  puts("M2 work registration: explicit capability; lazy threads; copied finite declarations; invalid ABI/flags/concurrency; duplicate/frozen rejection: pass");
  return 0;
}
static int fairness_contract(void) {
  qaplet_config base=QAPLET_CONFIG_INIT; qaplet_m2_config config=QAPLET_M2_CONFIG_INIT;
  qaplet_runtime *runtime=NULL;
  CHECK(qaplet_create_m2(&base,&config,&runtime,NULL)==QAPLET_OK);
  gate g={.mutex=PTHREAD_MUTEX_INITIALIZER,.condition=PTHREAD_COND_INITIALIZER};
  qaplet_work_handler h=QAPLET_WORK_HANDLER_INIT;
  h.name="held";h.name_length=4;h.function=held;h.userdata=&g;h.input_bytes=1;h.output_bytes=1;
  CHECK(qaplet_register_work_handler(runtime,&h,NULL)==QAPLET_OK);
  h.name="quick";h.name_length=5;h.function=quick;
  CHECK(qaplet_register_work_handler(runtime,&h,NULL)==QAPLET_OK);
  qaplet_work_task a=QAPLET_WORK_TASK_INIT,b=QAPLET_WORK_TASK_INIT,c=QAPLET_WORK_TASK_INIT;
  unsigned char input=17;
  CHECK(qaplet_work_submit(runtime,"held",4,&input,1,&a,NULL)==QAPLET_OK);wait_entered(&g,1,0);
  CHECK(qaplet_work_submit(runtime,"held",4,&input,1,&b,NULL)==QAPLET_OK);
  CHECK(qaplet_work_submit(runtime,"quick",5,&input,1,&c,NULL)==QAPLET_OK);wait_entered(&g,1,1);
  qaplet_work_snapshot stats=QAPLET_WORK_SNAPSHOT_INIT;
  CHECK(qaplet_get_work_snapshot(runtime,&stats,NULL)==QAPLET_OK && stats.operations==3 && stats.queued==1 && stats.worker_threads==2 && stats.worker_threads_peak==2 && stats.worker_stack_bytes==2097152 && stats.data_bytes==6);
  pthread_mutex_lock(&g.mutex);unsigned entered=g.entered;pthread_mutex_unlock(&g.mutex);CHECK(entered==1);
  CHECK(qaplet_work_release(runtime,&a,NULL)==QAPLET_BAD_STATE);
  h.name="late";h.name_length=4;CHECK(qaplet_register_work_handler(runtime,&h,NULL)==QAPLET_BAD_STATE);
  input=99;release_gate(&g);
  qaplet_drive_result drive=QAPLET_DRIVE_RESULT_INIT;
  CHECK(qaplet_run(runtime,&drive,NULL)==QAPLET_OK && drive.reason==QAPLET_DRIVE_IDLE);
  CHECK(qaplet_get_work_snapshot(runtime,&stats,NULL)==QAPLET_OK && stats.retained_results==3 && !stats.pending_handoffs && !stats.running && stats.operations==3 && stats.data_bytes==6);
  CHECK(g.entered==2 && g.exited==2 && g.seen==17 && !g.observed_cancel);
  CHECK(qaplet_work_release(runtime,&a,NULL)==QAPLET_OK && qaplet_work_release(runtime,&b,NULL)==QAPLET_OK && qaplet_work_release(runtime,&c,NULL)==QAPLET_OK);
  CHECK(!finish(runtime) && !destroy_gate(&g));
  puts("M2 work fairness: two bounded workers; default handler serial; runnable handler bypasses blocked head; copied input; owner handoff; retained capacity: pass");
  return 0;
}
static int close_contract(void) {
  for (unsigned stopped=0;stopped<2;++stopped) {
    qaplet_config base=QAPLET_CONFIG_INIT; qaplet_m2_config config=QAPLET_M2_CONFIG_INIT;
    qaplet_runtime *runtime=NULL;
    CHECK(qaplet_create_m2(&base,&config,&runtime,NULL)==QAPLET_OK);
    gate g={.mutex=PTHREAD_MUTEX_INITIALIZER,.condition=PTHREAD_COND_INITIALIZER};
    qaplet_work_handler h=QAPLET_WORK_HANDLER_INIT;
    h.name="held";h.name_length=4;h.function=held;h.userdata=&g;h.input_bytes=1;h.output_bytes=1;
    CHECK(qaplet_register_work_handler(runtime,&h,NULL)==QAPLET_OK);
    qaplet_work_task a=QAPLET_WORK_TASK_INIT,b=QAPLET_WORK_TASK_INIT;
    unsigned char input=71;
    CHECK(qaplet_work_submit(runtime,"held",4,&input,1,&a,NULL)==QAPLET_OK);wait_entered(&g,1,0);
    CHECK(qaplet_work_submit(runtime,"held",4,&input,1,&b,NULL)==QAPLET_OK);input=99;
    qaplet_drive_result drive=QAPLET_DRIVE_RESULT_INIT;
    if (stopped) {
      CHECK(qaplet_request_stop(runtime,NULL)==QAPLET_OK);
      CHECK(qaplet_run_once(runtime,0,&drive,NULL)==QAPLET_EXECUTION_STOPPED);
      qaplet_work_snapshot before=QAPLET_WORK_SNAPSHOT_INIT;
      CHECK(qaplet_get_work_snapshot(runtime,&before,NULL)==QAPLET_OK && before.cancelled==2 && !before.queued && before.running==1);
    }
    CHECK(qaplet_close(runtime,NULL)==QAPLET_OK && qaplet_close(runtime,NULL)==QAPLET_OK);
    qaplet_m2_snapshot state=QAPLET_M2_SNAPSHOT_INIT;
    qaplet_work_snapshot stats=QAPLET_WORK_SNAPSHOT_INIT;
    CHECK(qaplet_get_m2_snapshot(runtime,&state,NULL)==QAPLET_OK && state.lifecycle==QAPLET_CLOSED && !state.engine_current_bytes && state.native_state==QAPLET_NATIVE_CLEANING);
    CHECK(qaplet_get_work_snapshot(runtime,&stats,NULL)==QAPLET_OK && stats.operations==2 && stats.data_bytes==4 && stats.handlers==1 && stats.running==1 && !stats.queued && stats.cancelled==2 && stats.worker_threads>=1);
    CHECK(qaplet_quiesce(runtime,0,&drive,NULL)==QAPLET_OK && drive.reason==QAPLET_DRIVE_WAIT_TIMEOUT);
    CHECK(qaplet_quiesce(runtime,5,&drive,NULL)==QAPLET_OK && drive.reason==QAPLET_DRIVE_WAIT_TIMEOUT);
    CHECK(qaplet_destroy(runtime,NULL)==QAPLET_BUSY);
    qaplet_work_result result=QAPLET_WORK_RESULT_INIT;
    CHECK(qaplet_work_copy_result(runtime,&a,&result,NULL)==QAPLET_INVALID_HANDLE);
    release_gate(&g);
    CHECK(qaplet_quiesce(runtime,2000,&drive,NULL)==QAPLET_OK && drive.reason==QAPLET_DRIVE_QUIESCENT);
    CHECK(g.entered==1 && g.exited==1 && g.seen==71 && g.observed_cancel);
    CHECK(!finish(runtime) && !destroy_gate(&g));
  }
  puts("M2 work close: running native data/code retained; queued suppressed; stop starts cancellation; CLOSED engine gone; finite quiesce retry; destroy BUSY; physical join: pass");
  return 0;
}
static qaplet_code overflow(qaplet_work_context *context,const void *input,size_t length,
                            qaplet_work_output *output,void *opaque) {
  (void)context;(void)input;(void)length;(void)opaque;
  output->length=output->capacity+1;return QAPLET_OK;
}
static int capacity_contract(void) {
  qaplet_config base=QAPLET_CONFIG_INIT; qaplet_m2_config config=QAPLET_M2_CONFIG_INIT;
  config.limits.operations=2;config.limits.operation_data_bytes=8;config.limits.io_block_bytes=1;
  qaplet_runtime *runtime=NULL,*other=NULL;
  CHECK(qaplet_create_m2(&base,&config,&runtime,NULL)==QAPLET_OK);
  CHECK(qaplet_create_m2(&base,&config,&other,NULL)==QAPLET_OK);
  gate g={.mutex=PTHREAD_MUTEX_INITIALIZER,.condition=PTHREAD_COND_INITIALIZER};
  qaplet_work_handler h=QAPLET_WORK_HANDLER_INIT;
  h.name="held";h.name_length=4;h.function=held;h.userdata=&g;h.input_bytes=1;h.output_bytes=3;
  CHECK(qaplet_register_work_handler(runtime,&h,NULL)==QAPLET_OK);
  h.name="overflow";h.name_length=8;h.function=overflow;h.output_bytes=8;
  CHECK(qaplet_register_work_handler(runtime,&h,NULL)==QAPLET_OK);
  qaplet_progress progress=QAPLET_PROGRESS_INIT;
  CHECK(qaplet_eval(runtime,"42",2,&progress,NULL)==QAPLET_OK);
  qaplet_work_task a=QAPLET_WORK_TASK_INIT,b=QAPLET_WORK_TASK_INIT,untouched=QAPLET_WORK_TASK_INIT;
  unsigned char input=17;
  CHECK(qaplet_work_submit(runtime,"held",4,&input,1,&a,NULL)==QAPLET_OK);wait_entered(&g,1,0);
  CHECK(qaplet_work_submit(runtime,"held",4,&input,1,&b,NULL)==QAPLET_OK);
  CHECK(qaplet_work_submit(runtime,"held",4,&input,1,&untouched,NULL)==QAPLET_RESOURCE_LIMIT && !untouched.runtime_id);
  qaplet_work_snapshot stats=QAPLET_WORK_SNAPSHOT_INIT;
  CHECK(qaplet_get_work_snapshot(runtime,&stats,NULL)==QAPLET_OK && stats.operations==2 && stats.data_bytes==8 && stats.accepted==2 && stats.rejected==1);
  qaplet_work_result result=QAPLET_WORK_RESULT_INIT;
  CHECK(qaplet_work_copy_result(other,&a,&result,NULL)==QAPLET_INVALID_HANDLE);
  CHECK(qaplet_get_work_snapshot(other,&stats,NULL)==QAPLET_OK && !stats.operations && !stats.worker_threads);
  release_gate(&g);qaplet_drive_result drive=QAPLET_DRIVE_RESULT_INIT;
  CHECK(qaplet_run(runtime,&drive,NULL)==QAPLET_OK);
  qaplet_result js=QAPLET_RESULT_INIT;
  CHECK(qaplet_get_result(runtime,&js,NULL)==QAPLET_OK && js.number==42);
  CHECK(qaplet_work_submit(runtime,"held",4,&input,1,&untouched,NULL)==QAPLET_RESOURCE_LIMIT);
  CHECK(qaplet_work_release(runtime,&a,NULL)==QAPLET_OK);
  CHECK(qaplet_work_submit(runtime,"overflow",8,NULL,0,&untouched,NULL)==QAPLET_RESOURCE_LIMIT);
  CHECK(qaplet_work_release(runtime,&b,NULL)==QAPLET_OK);
  CHECK(qaplet_work_submit(runtime,"overflow",8,NULL,0,&untouched,NULL)==QAPLET_OK);
  CHECK(qaplet_run(runtime,&drive,NULL)==QAPLET_OK);
  CHECK(qaplet_work_copy_result(runtime,&untouched,&result,NULL)==QAPLET_OK && result.state==QAPLET_WORK_FAILED && result.code==QAPLET_VALUE_TOO_LARGE && !result.length);
  CHECK(qaplet_work_release(runtime,&untouched,NULL)==QAPLET_OK);
  CHECK(qaplet_work_release(runtime,&a,NULL)==QAPLET_INVALID_HANDLE);
  CHECK(qaplet_get_result(runtime,&js,NULL)==QAPLET_OK && js.number==42);
  qaplet_m2_snapshot state=QAPLET_M2_SNAPSHOT_INIT;
  CHECK(qaplet_get_m2_snapshot(runtime,&state,NULL)==QAPLET_OK && state.lifecycle==QAPLET_OPEN && !state.termination_reason && state.native_current_bytes<=state.limits.native_storage_bytes);
  CHECK(!finish(runtime) && !finish(other) && !destroy_gate(&g));
  puts("M2 work capacity: count/data reservations; atomic refusal; instance/generation isolation; retained results charged; output bound; JS result preserved; ordinary reuse: pass");
  return 0;
}
static int ledger_contract(void) {
  for (unsigned deadline=0;deadline<2;++deadline) {
    qaplet_config base=QAPLET_CONFIG_INIT; qaplet_m2_config config=QAPLET_M2_CONFIG_INIT;
    qaplet_runtime *runtime=NULL;
    CHECK(qaplet_create_m2(&base,&config,&runtime,NULL)==QAPLET_OK);
    gate g={.mutex=PTHREAD_MUTEX_INITIALIZER,.condition=PTHREAD_COND_INITIALIZER};
    qaplet_work_handler h=QAPLET_WORK_HANDLER_INIT;
    h.name="held";h.name_length=4;h.function=held;h.userdata=&g;h.input_bytes=1;h.output_bytes=1;
    CHECK(qaplet_register_work_handler(runtime,&h,NULL)==QAPLET_OK);
    qaplet_execution_options options=QAPLET_EXECUTION_OPTIONS_INIT;
    if (deadline) options.timeout_ms=40; else options.max_dispatches=1;
    qaplet_progress progress=QAPLET_PROGRESS_INIT;
    const char pending[]="new Promise(()=>{})";
    CHECK(qaplet_eval_with_options(runtime,pending,sizeof(pending)-1,&options,&progress,NULL)==QAPLET_OK && progress.phase==QAPLET_PHASE_PENDING);
    unsigned char input=17;qaplet_work_task a=QAPLET_WORK_TASK_INIT,b=QAPLET_WORK_TASK_INIT;
    CHECK(qaplet_work_submit(runtime,"held",4,&input,1,&a,NULL)==QAPLET_OK);wait_entered(&g,1,0);
    CHECK(qaplet_work_submit(runtime,"held",4,&input,1,&b,NULL)==QAPLET_OK);
    if (!deadline) release_gate(&g);
    qaplet_drive_result drive=QAPLET_DRIVE_RESULT_INIT;
    qaplet_code expected=deadline ? QAPLET_DEADLINE_EXCEEDED : QAPLET_DISPATCH_LIMIT;
    CHECK(qaplet_run(runtime,&drive,NULL)==expected && drive.reason==QAPLET_DRIVE_ERROR);
    qaplet_m2_snapshot state=QAPLET_M2_SNAPSHOT_INIT;
    CHECK(qaplet_get_m2_snapshot(runtime,&state,NULL)==QAPLET_OK && state.lifecycle==QAPLET_FAILED && state.termination_reason==expected);
    if (!deadline) { qaplet_snapshot snapshot=QAPLET_SNAPSHOT_INIT;CHECK(qaplet_get_snapshot(runtime,&snapshot,NULL)==QAPLET_OK && snapshot.execution_dispatches==1); }
    else { qaplet_work_snapshot stats=QAPLET_WORK_SNAPSHOT_INIT;CHECK(qaplet_get_work_snapshot(runtime,&stats,NULL)==QAPLET_OK && stats.cancelled==2 && stats.running==1 && !stats.queued);release_gate(&g); }
    CHECK(!finish(runtime) && !destroy_gate(&g));
  }
  puts("M2 work ledger: active entry cumulative dispatch; absolute pending deadline; terminal native cancellation; no ledger reset: pass");
  return 0;
}
static pthread_key_t worker_key;
static void worker_destructor(void *opaque) {
  gate *g=opaque;
  pthread_mutex_lock(&g->mutex);g->tls_entered=1;pthread_cond_broadcast(&g->condition);
  while (!g->tls_released) pthread_cond_wait(&g->condition,&g->mutex);
  pthread_mutex_unlock(&g->mutex);
}
static qaplet_code tls_held(qaplet_work_context *context,const void *input,size_t length,
                            qaplet_work_output *output,void *opaque) {
  if (pthread_setspecific(worker_key,opaque)) return QAPLET_HOST_ERROR;
  return held(context,input,length,output,opaque);
}
static int physical_contract(void) {
  CHECK(!pthread_key_create(&worker_key,worker_destructor));
  qaplet_config base=QAPLET_CONFIG_INIT;qaplet_m2_config config=QAPLET_M2_CONFIG_INIT;
  qaplet_runtime *runtime=NULL;CHECK(qaplet_create_m2(&base,&config,&runtime,NULL)==QAPLET_OK);
  gate g={.mutex=PTHREAD_MUTEX_INITIALIZER,.condition=PTHREAD_COND_INITIALIZER};
  qaplet_work_handler h=QAPLET_WORK_HANDLER_INIT;
  h.name="tls";h.name_length=3;h.function=tls_held;h.userdata=&g;h.input_bytes=1;h.output_bytes=1;
  CHECK(qaplet_register_work_handler(runtime,&h,NULL)==QAPLET_OK);
  qaplet_work_task task=QAPLET_WORK_TASK_INIT;unsigned char input=17;
  CHECK(qaplet_work_submit(runtime,"tls",3,&input,1,&task,NULL)==QAPLET_OK);wait_entered(&g,1,0);
  CHECK(qaplet_close(runtime,NULL)==QAPLET_OK);release_gate(&g);
  pthread_mutex_lock(&g.mutex);while (!g.tls_entered) pthread_cond_wait(&g.condition,&g.mutex);pthread_mutex_unlock(&g.mutex);
  qaplet_drive_result drive=QAPLET_DRIVE_RESULT_INIT;
  CHECK(qaplet_quiesce(runtime,0,&drive,NULL)==QAPLET_OK && drive.reason==QAPLET_DRIVE_WAIT_TIMEOUT);
  CHECK(qaplet_quiesce(runtime,5,&drive,NULL)==QAPLET_OK && drive.reason==QAPLET_DRIVE_WAIT_TIMEOUT);
  CHECK(qaplet_destroy(runtime,NULL)==QAPLET_BUSY);
  pthread_mutex_lock(&g.mutex);g.tls_released=1;pthread_cond_broadcast(&g.condition);pthread_mutex_unlock(&g.mutex);
  CHECK(qaplet_quiesce(runtime,2000,&drive,NULL)==QAPLET_OK && drive.reason==QAPLET_DRIVE_QUIESCENT);
  CHECK(!finish(runtime) && !destroy_gate(&g) && !pthread_key_delete(worker_key));
  puts("M2 work physical exit: callback return precedes blocked native TLS destructor; zero/finite quiesce never joins live thread; retained BUSY; actual exit joined: pass");
  return 0;
}
static int retention_contract(void) {
  qaplet_config base=QAPLET_CONFIG_INIT;qaplet_m2_config config=QAPLET_M2_CONFIG_INIT;
  qaplet_runtime *runtime=NULL;CHECK(qaplet_create_m2(&base,&config,&runtime,NULL)==QAPLET_OK);
  gate g={.mutex=PTHREAD_MUTEX_INITIALIZER,.condition=PTHREAD_COND_INITIALIZER};
  qaplet_work_handler h=QAPLET_WORK_HANDLER_INIT;
  h.name="held";h.name_length=4;h.function=held;h.userdata=&g;h.input_bytes=1;h.output_bytes=1;
  CHECK(qaplet_register_work_handler(runtime,&h,NULL)==QAPLET_OK);
  qaplet_work_task task=QAPLET_WORK_TASK_INIT;unsigned char input=17;
  CHECK(qaplet_work_submit(runtime,"held",4,&input,1,&task,NULL)==QAPLET_OK);wait_entered(&g,1,0);
  CHECK(qaplet_request_stop(runtime,NULL)==QAPLET_OK);
  qaplet_drive_result drive=QAPLET_DRIVE_RESULT_INIT;
  CHECK(qaplet_run_once(runtime,0,&drive,NULL)==QAPLET_EXECUTION_STOPPED);
  qaplet_work_result result=QAPLET_WORK_RESULT_INIT;
  CHECK(qaplet_work_copy_result(runtime,&task,&result,NULL)==QAPLET_OK && result.state==QAPLET_WORK_CANCELLED && !result.length);
  CHECK(qaplet_work_release(runtime,&task,NULL)==QAPLET_OK);
  CHECK(qaplet_work_copy_result(runtime,&task,&result,NULL)==QAPLET_INVALID_HANDLE);
  qaplet_work_snapshot stats=QAPLET_WORK_SNAPSHOT_INIT;
  CHECK(qaplet_get_work_snapshot(runtime,&stats,NULL)==QAPLET_OK && stats.operations==1 && stats.data_bytes==2 && stats.running==1);
  CHECK(qaplet_close(runtime,NULL)==QAPLET_OK);release_gate(&g);
  CHECK(!finish(runtime) && !destroy_gate(&g));
  CHECK(qaplet_create_m2(&base,&config,&runtime,NULL)==QAPLET_OK);
  gate complete={.mutex=PTHREAD_MUTEX_INITIALIZER,.condition=PTHREAD_COND_INITIALIZER,.released=1};h.userdata=&complete;
  CHECK(qaplet_register_work_handler(runtime,&h,NULL)==QAPLET_OK);
  CHECK(qaplet_work_submit(runtime,"held",4,&input,1,&task,NULL)==QAPLET_OK);
  CHECK(qaplet_run(runtime,&drive,NULL)==QAPLET_OK);
  CHECK(qaplet_request_stop(runtime,NULL)==QAPLET_OK && qaplet_run_once(runtime,0,&drive,NULL)==QAPLET_EXECUTION_STOPPED);
  result.buffer=&input;result.capacity=1;
  CHECK(qaplet_work_copy_result(runtime,&task,&result,NULL)==QAPLET_OK && result.state==QAPLET_WORK_SUCCEEDED && input==17);
  CHECK(qaplet_get_work_snapshot(runtime,&stats,NULL)==QAPLET_OK && stats.operations==1 && stats.data_bytes==2 && stats.retained_results==1);
  CHECK(qaplet_work_release(runtime,&task,NULL)==QAPLET_OK);
  CHECK(!finish(runtime) && !destroy_gate(&complete));
  puts("M2 work retention: STOPPED preserves terminal C results; logical release invalidates token while physical buffers stay charged; close retires only after native exit: pass");
  return 0;
}
static int limits_contract(void) {
  qaplet_config base=QAPLET_CONFIG_INIT;qaplet_m2_config config=QAPLET_M2_CONFIG_INIT;
  qaplet_runtime *runtime=NULL;CHECK(qaplet_create_m2(&base,&config,&runtime,NULL)==QAPLET_OK);
  gate g={.mutex=PTHREAD_MUTEX_INITIALIZER,.condition=PTHREAD_COND_INITIALIZER};
  qaplet_work_handler h=QAPLET_WORK_HANDLER_INIT;
  h.name="large";h.name_length=5;h.function=held;h.userdata=&g;
  CHECK(qaplet_register_work_handler(runtime,&h,NULL)==QAPLET_OK);
  unsigned char *input=malloc(1048576);CHECK(input);memset(input,17,1048576);
  qaplet_work_task tasks[5]={{0}};
  for (unsigned i=0;i<4;++i) CHECK(qaplet_work_submit(runtime,"large",5,input,1048576,&tasks[i],NULL)==QAPLET_OK);
  wait_entered(&g,1,0);
  CHECK(qaplet_work_submit(runtime,"large",5,input,1048576,&tasks[4],NULL)==QAPLET_RESOURCE_LIMIT && !tasks[4].runtime_id);
  qaplet_work_snapshot stats=QAPLET_WORK_SNAPSHOT_INIT;qaplet_m2_snapshot state=QAPLET_M2_SNAPSHOT_INIT;
  CHECK(qaplet_get_work_snapshot(runtime,&stats,NULL)==QAPLET_OK && stats.operations==4 && stats.data_bytes==8388608 && stats.worker_threads==2);
  CHECK(qaplet_get_m2_snapshot(runtime,&state,NULL)==QAPLET_OK && state.m2_limits.operations==128 && state.m2_limits.operation_data_bytes==8388608 && state.limits.native_storage_bytes==16777216 && state.native_current_bytes>=stats.data_bytes && state.native_current_bytes<=16777216);
  memset(input,99,1048576);release_gate(&g);
  qaplet_drive_result drive=QAPLET_DRIVE_RESULT_INIT;CHECK(qaplet_run(runtime,&drive,NULL)==QAPLET_OK);
  CHECK(qaplet_work_submit(runtime,"large",5,input,1048576,&tasks[4],NULL)==QAPLET_RESOURCE_LIMIT);
  CHECK(qaplet_work_release(runtime,&tasks[0],NULL)==QAPLET_OK);
  CHECK(qaplet_work_submit(runtime,"large",5,input,1048576,&tasks[4],NULL)==QAPLET_OK);
  CHECK(qaplet_run(runtime,&drive,NULL)==QAPLET_OK);
  for (unsigned i=1;i<5;++i) CHECK(qaplet_work_release(runtime,&tasks[i],NULL)==QAPLET_OK);
  free(input);CHECK(!finish(runtime) && !destroy_gate(&g));
  /* The native total is a separate simultaneous constraint, with no allocator
   * refusal: three 2MiB operations cannot coexist with this 4MiB event store. */
  base=(qaplet_config)QAPLET_CONFIG_INIT;base.limits.native_storage_bytes=10485760;
  base.limits.event_queue_slots=64;base.limits.event_payload_bytes=65536;
  CHECK(qaplet_create_m2(&base,&config,&runtime,NULL)==QAPLET_OK);
  gate total={.mutex=PTHREAD_MUTEX_INITIALIZER,.condition=PTHREAD_COND_INITIALIZER};h.userdata=&total;
  CHECK(qaplet_register_work_handler(runtime,&h,NULL)==QAPLET_OK);
  input=malloc(1048576);CHECK(input);memset(input,17,1048576);
  CHECK(qaplet_work_submit(runtime,"large",5,input,1048576,&tasks[0],NULL)==QAPLET_OK);
  CHECK(qaplet_work_submit(runtime,"large",5,input,1048576,&tasks[1],NULL)==QAPLET_OK);
  tasks[2]=(qaplet_work_task)QAPLET_WORK_TASK_INIT;
  CHECK(qaplet_work_submit(runtime,"large",5,input,1048576,&tasks[2],NULL)==QAPLET_RESOURCE_LIMIT && !tasks[2].runtime_id);
  CHECK(qaplet_get_m2_snapshot(runtime,&state,NULL)==QAPLET_OK && state.lifecycle==QAPLET_OPEN && !state.termination_reason && state.native_current_bytes<=10485760);
  CHECK(qaplet_get_work_snapshot(runtime,&stats,NULL)==QAPLET_OK && stats.operations==2 && stats.data_bytes==4194304);
  release_gate(&total);CHECK(qaplet_run(runtime,&drive,NULL)==QAPLET_OK);
  free(input);CHECK(!finish(runtime) && !destroy_gate(&total));
  puts("M2 work limits: real default 8MiB subquota within 16MiB total; simultaneous native metadata/data preflight; retained data charged; count independent; no permanent capacity failure: pass");
  return 0;
}
static int completion_contract(void) {
  qaplet_config base=QAPLET_CONFIG_INIT;base.limits.event_queue_slots=1;
  qaplet_m2_config config=QAPLET_M2_CONFIG_INIT;qaplet_runtime *runtime=NULL;
  CHECK(qaplet_create_m2(&base,&config,&runtime,NULL)==QAPLET_OK);
  gate g={.mutex=PTHREAD_MUTEX_INITIALIZER,.condition=PTHREAD_COND_INITIALIZER};
  qaplet_work_handler h=QAPLET_WORK_HANDLER_INIT;
  h.name="two";h.name_length=3;h.function=held;h.userdata=&g;h.input_bytes=1;h.output_bytes=1;h.concurrency=2;
  CHECK(qaplet_register_work_handler(runtime,&h,NULL)==QAPLET_OK);
  const char source[]="import('qaplet:events').then(m=>m.onEvent(()=>{globalThis.received=(globalThis.received||0)+1}))";
  qaplet_progress progress=QAPLET_PROGRESS_INIT;
  CHECK(qaplet_eval(runtime,source,sizeof(source)-1,&progress,NULL)==QAPLET_OK);
  while (progress.phase==QAPLET_PHASE_PENDING) CHECK(qaplet_pump(runtime,&progress,NULL)==QAPLET_OK);
  CHECK(qaplet_post_event(runtime,1,0,NULL,0,NULL)==QAPLET_OK);
  CHECK(qaplet_post_event(runtime,2,0,NULL,0,NULL)==QAPLET_QUEUE_FULL);
  qaplet_work_task a=QAPLET_WORK_TASK_INIT,b=QAPLET_WORK_TASK_INIT;unsigned char input=17;
  CHECK(qaplet_work_submit(runtime,"two",3,&input,1,&a,NULL)==QAPLET_OK);
  CHECK(qaplet_work_submit(runtime,"two",3,&input,1,&b,NULL)==QAPLET_OK);wait_entered(&g,2,0);
  release_gate(&g);
  qaplet_work_snapshot stats=QAPLET_WORK_SNAPSHOT_INIT;
  do { CHECK(qaplet_get_work_snapshot(runtime,&stats,NULL)==QAPLET_OK);sched_yield(); } while (stats.completed<2);
  CHECK(stats.pending_handoffs==2 && stats.retained_results==2 && !stats.running);
  qaplet_snapshot events=QAPLET_SNAPSHOT_INIT;
  CHECK(qaplet_get_snapshot(runtime,&events,NULL)==QAPLET_OK && events.queue_depth==1 && !events.events_consumed);
  qaplet_drive_result drive=QAPLET_DRIVE_RESULT_INIT;
  CHECK(qaplet_run(runtime,&drive,NULL)==QAPLET_OK && drive.reason==QAPLET_DRIVE_IDLE && drive.progressed);
  CHECK(qaplet_get_work_snapshot(runtime,&stats,NULL)==QAPLET_OK && !stats.pending_handoffs && stats.operations==2);
  CHECK(qaplet_get_snapshot(runtime,&events,NULL)==QAPLET_OK && !events.queue_depth && events.events_consumed==1 && !events.events_dropped);
  qaplet_work_result result=QAPLET_WORK_RESULT_INIT;result.buffer=&input;result.capacity=1;
  CHECK(qaplet_work_copy_result(runtime,&a,&result,NULL)==QAPLET_OK && input==17 && result.state==QAPLET_WORK_SUCCEEDED);
  CHECK(qaplet_work_copy_result(runtime,&b,&result,NULL)==QAPLET_OK && input==17 && result.state==QAPLET_WORK_SUCCEEDED);
  CHECK(!finish(runtime) && !destroy_gate(&g));
  puts("M2 work completion: explicit handler concurrency; saturated business queue cannot lose native records; worker publication retains ref; owner handoff; exact results: pass");
  return 0;
}
typedef struct { pthread_mutex_t mutex;pthread_cond_t condition;pthread_t owner;int ready,hold,released; } host_hint;
static void host_notify(void *opaque) {
  host_hint *hint=opaque;
  pthread_mutex_lock(&hint->mutex);hint->ready=1;pthread_cond_broadcast(&hint->condition);
  while (hint->hold && !hint->released && !pthread_equal(pthread_self(),hint->owner))
    pthread_cond_wait(&hint->condition,&hint->mutex);
  pthread_mutex_unlock(&hint->mutex);
}
static int notify_contract(void) {
  qaplet_config base=QAPLET_CONFIG_INIT;qaplet_m2_config config=QAPLET_M2_CONFIG_INIT;
  qaplet_runtime *runtime=NULL;CHECK(qaplet_create_m2(&base,&config,&runtime,NULL)==QAPLET_OK);
  host_hint hint={.mutex=PTHREAD_MUTEX_INITIALIZER,.condition=PTHREAD_COND_INITIALIZER,.owner=pthread_self()};
  qaplet_host_notify hook=QAPLET_HOST_NOTIFY_INIT;hook.function=host_notify;hook.userdata=&hint;
  CHECK(qaplet_set_host_notify(runtime,&hook,NULL)==QAPLET_OK);
  gate g={.mutex=PTHREAD_MUTEX_INITIALIZER,.condition=PTHREAD_COND_INITIALIZER};
  qaplet_work_handler h=QAPLET_WORK_HANDLER_INIT;
  h.name="held";h.name_length=4;h.function=held;h.userdata=&g;h.input_bytes=1;h.output_bytes=1;
  CHECK(qaplet_register_work_handler(runtime,&h,NULL)==QAPLET_OK);
  qaplet_work_task task=QAPLET_WORK_TASK_INIT;unsigned char input=17;
  CHECK(qaplet_work_submit(runtime,"held",4,&input,1,&task,NULL)==QAPLET_OK);wait_entered(&g,1,0);
  pthread_mutex_lock(&hint.mutex);hint.ready=0;hint.hold=1;pthread_mutex_unlock(&hint.mutex);
  release_gate(&g);
  /* A real external native wait, with no SDK run/poll loop or onEvent. */
  pthread_mutex_lock(&hint.mutex);while (!hint.ready) pthread_cond_wait(&hint.condition,&hint.mutex);pthread_mutex_unlock(&hint.mutex);
  qaplet_drive_result drive=QAPLET_DRIVE_RESULT_INIT;
  CHECK(qaplet_run_once(runtime,0,&drive,NULL)==QAPLET_OK && drive.progressed);
  qaplet_work_result result=QAPLET_WORK_RESULT_INIT;result.buffer=&input;result.capacity=1;
  CHECK(qaplet_work_copy_result(runtime,&task,&result,NULL)==QAPLET_OK && input==17 && result.state==QAPLET_WORK_SUCCEEDED);
  qaplet_notify_snapshot notification=QAPLET_NOTIFY_SNAPSHOT_INIT;
  CHECK(qaplet_get_notify_snapshot(runtime,&notification,NULL)==QAPLET_OK && notification.inflight);
  CHECK(qaplet_close(runtime,NULL)==QAPLET_OK);
  CHECK(qaplet_quiesce(runtime,0,&drive,NULL)==QAPLET_OK && drive.reason==QAPLET_DRIVE_WAIT_TIMEOUT);
  CHECK(qaplet_destroy(runtime,NULL)==QAPLET_BUSY);
  pthread_mutex_lock(&hint.mutex);hint.released=1;pthread_cond_broadcast(&hint.condition);pthread_mutex_unlock(&hint.mutex);
  CHECK(!finish(runtime) && !destroy_gate(&g));
  CHECK(!pthread_cond_destroy(&hint.condition) && !pthread_mutex_destroy(&hint.mutex));
  puts("M2 work HostNotify: completion wakes real external Host condition; owner handoff and copied result; inflight lease survives close; nonblocking quiesce and BUSY destroy; joined code/userdata lifetime: pass");
  return 0;
}
static int request_contract(void) {
  qaplet_config base=QAPLET_CONFIG_INIT;qaplet_m2_config config=QAPLET_M2_CONFIG_INIT;config.limits.worker_threads=1;
  qaplet_runtime *runtime=NULL;CHECK(qaplet_create_m2(&base,&config,&runtime,NULL)==QAPLET_OK);
  gate g={.mutex=PTHREAD_MUTEX_INITIALIZER,.condition=PTHREAD_COND_INITIALIZER};
  qaplet_work_handler h=QAPLET_WORK_HANDLER_INIT;
  h.name="held";h.name_length=4;h.function=held;h.userdata=&g;h.input_bytes=1;h.output_bytes=1;
  CHECK(qaplet_register_work_handler(runtime,&h,NULL)==QAPLET_OK);
  qaplet_work_task a=QAPLET_WORK_TASK_INIT,b=QAPLET_WORK_TASK_INIT;unsigned char input=17;
  CHECK(qaplet_work_submit(runtime,"held",4,&input,1,&a,NULL)==QAPLET_OK);wait_entered(&g,1,0);
  CHECK(qaplet_work_submit(runtime,"held",4,&input,1,&b,NULL)==QAPLET_OK);
  CHECK(qaplet_request_stop(runtime,NULL)==QAPLET_OK);release_gate(&g);
  pthread_mutex_lock(&g.mutex);while (!g.exited) pthread_cond_wait(&g.condition,&g.mutex);unsigned cancelled=g.observed_cancel;pthread_mutex_unlock(&g.mutex);
  CHECK(cancelled);
  qaplet_drive_result drive=QAPLET_DRIVE_RESULT_INIT;
  CHECK(qaplet_run_once(runtime,0,&drive,NULL)==QAPLET_EXECUTION_STOPPED);
  CHECK(!finish(runtime));
  CHECK(g.entered==1 && g.exited==1 && !destroy_gate(&g));
  puts("M2 work stop request: cooperative marker observes accepted stop before owner drive; queued worker claim shares stop order; no late callback: pass");
  return 0;
}
static int scheduling_contract(void) {
  qaplet_config base=QAPLET_CONFIG_INIT;qaplet_m2_config config=QAPLET_M2_CONFIG_INIT;
  qaplet_runtime *runtime=NULL;CHECK(qaplet_create_m2(&base,&config,&runtime,NULL)==QAPLET_OK);
  gate g={.mutex=PTHREAD_MUTEX_INITIALIZER,.condition=PTHREAD_COND_INITIALIZER};
  qaplet_work_handler h=QAPLET_WORK_HANDLER_INIT;
  h.name="quick";h.name_length=5;h.function=quick;h.userdata=&g;h.input_bytes=1;h.output_bytes=1;
  CHECK(qaplet_register_work_handler(runtime,&h,NULL)==QAPLET_OK);
  const char source[]="import('qaplet:events').then(m=>m.onEvent(()=>{}))";
  qaplet_progress progress=QAPLET_PROGRESS_INIT;
  CHECK(qaplet_eval(runtime,source,sizeof(source)-1,&progress,NULL)==QAPLET_OK);
  while (progress.phase==QAPLET_PHASE_PENDING) CHECK(qaplet_pump(runtime,&progress,NULL)==QAPLET_OK);
  CHECK(qaplet_post_event(runtime,1,0,NULL,0,NULL)==QAPLET_OK);
  qaplet_work_task tasks[32]={{0}};
  for (unsigned i=0;i<32;++i) CHECK(qaplet_work_submit(runtime,"quick",5,NULL,0,&tasks[i],NULL)==QAPLET_OK);
  qaplet_work_snapshot stats=QAPLET_WORK_SNAPSHOT_INIT;
  do {CHECK(qaplet_get_work_snapshot(runtime,&stats,NULL)==QAPLET_OK);sched_yield();} while (stats.completed<32);
  CHECK(qaplet_pump(runtime,&progress,NULL)==QAPLET_OK && progress.progressed);
  qaplet_snapshot events=QAPLET_SNAPSHOT_INIT;
  CHECK(qaplet_get_snapshot(runtime,&events,NULL)==QAPLET_OK && events.events_consumed==1);
  CHECK(qaplet_get_work_snapshot(runtime,&stats,NULL)==QAPLET_OK && stats.pending_handoffs==1);
  CHECK(qaplet_pump(runtime,&progress,NULL)==QAPLET_OK && progress.progressed);
  CHECK(qaplet_get_work_snapshot(runtime,&stats,NULL)==QAPLET_OK && !stats.pending_handoffs);
  CHECK(!finish(runtime) && !destroy_gate(&g));
  puts("M2 work scheduling: native completions and business events share bounded delivery fairly; jobs checkpoints precede each handoff; cursor survives pumps: pass");
  return 0;
}
int sdk_work(const char *mode) {
  if (!strcmp(mode,"--m2-work-file")) return file_contract();
  if (!strcmp(mode,"--m2-work-registration")) return registration_contract();
  if (!strcmp(mode,"--m2-work-fairness")) return fairness_contract();
  if (!strcmp(mode,"--m2-work-close")) return close_contract();
  if (!strcmp(mode,"--m2-work-capacity")) return capacity_contract();
  if (!strcmp(mode,"--m2-work-ledger")) return ledger_contract();
  if (!strcmp(mode,"--m2-work-physical")) return physical_contract();
  if (!strcmp(mode,"--m2-work-limits")) return limits_contract();
  if (!strcmp(mode,"--m2-work-completion")) return completion_contract();
  if (!strcmp(mode,"--m2-work-retention")) return retention_contract();
  if (!strcmp(mode,"--m2-work-scheduling")) return scheduling_contract();
  if (!strcmp(mode,"--m2-work-stop-request")) return request_contract();
  if (!strcmp(mode,"--m2-work-notify")) return notify_contract();
  return 2;
}
#ifdef SDK_STANDALONE
int main(int argc,char **argv) { return argc==2 ? sdk_work(argv[1]) : 2; }
#endif
