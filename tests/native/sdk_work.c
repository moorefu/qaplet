/* SPDX-License-Identifier: MIT */
/* Only the delivered public header and real native system interfaces. */
#include "qaplet.h"
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <sched.h>
#include <stdatomic.h>
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
  /* Own the fixture below the runner's writable cwd; a Windows host need not
   * provide a POSIX /tmp directory. Never change cwd after Runtime creation. */
  char directory[]="./qaplet-work-XXXXXX";
  char *created=mkdtemp(directory);
  if (!created) fprintf(stderr,"work file fixture: mkdtemp errno=%d\n",errno);
  CHECK(created);
  char path[sizeof(directory)+sizeof("/input-XXXXXX")];
  int path_length=snprintf(path,sizeof(path),"%s/input-XXXXXX",directory);
  CHECK(path_length>0 && path_length<sizeof(path));
  int fd=mkstemp(path); unsigned char expected[]={0,255,17,42,99};
  if (fd<0) fprintf(stderr,"work file fixture: mkstemp fd=%d errno=%d\n",fd,errno);
  CHECK(fd>=0);
  ssize_t written=write(fd,expected,sizeof(expected));int write_error=errno;
  int closed=close(fd);int close_error=errno;
  if (written!=sizeof(expected)) fprintf(stderr,"work file fixture: write bytes=%zd errno=%d\n",written,write_error);
  if (closed) fprintf(stderr,"work file fixture: close result=%d errno=%d\n",closed,close_error);
  CHECK(written==sizeof(expected) && !closed);
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
  CHECK(!rmdir(directory));
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
static void release_from_timer(qaplet_runtime *runtime,const qaplet_timer *timer,void *userdata) {
  (void)runtime;(void)timer;release_gate(userdata);
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
  base=(qaplet_config)QAPLET_CONFIG_INIT;
  CHECK(qaplet_create_m2(&base,&config,&runtime,NULL)==QAPLET_OK);
  gate waiting={.mutex=PTHREAD_MUTEX_INITIALIZER,.condition=PTHREAD_COND_INITIALIZER};h.userdata=&waiting;
  CHECK(qaplet_register_work_handler(runtime,&h,NULL)==QAPLET_OK);
  CHECK(qaplet_work_submit(runtime,"two",3,&input,1,&a,NULL)==QAPLET_OK);wait_entered(&waiting,1,0);
  qaplet_timer ready=QAPLET_TIMER_INIT,later=QAPLET_TIMER_INIT;
  CHECK(qaplet_timer_create(runtime,0,release_from_timer,&waiting,&ready,NULL)==QAPLET_OK);
  CHECK(qaplet_timer_ref(runtime,&ready,0,NULL)==QAPLET_OK);
  CHECK(qaplet_timer_create(runtime,5000,release_from_timer,&waiting,&later,NULL)==QAPLET_OK);
  CHECK(qaplet_timer_ref(runtime,&later,0,NULL)==QAPLET_OK);
  CHECK(qaplet_run(runtime,&drive,NULL)==QAPLET_OK && drive.reason==QAPLET_DRIVE_IDLE);
  qaplet_timer_snapshot timers=QAPLET_TIMER_SNAPSHOT_INIT;
  CHECK(qaplet_get_timer_snapshot(runtime,&timers,NULL)==QAPLET_OK && timers.current==1 && !timers.referenced && !timers.ready);
  CHECK(qaplet_get_work_snapshot(runtime,&stats,NULL)==QAPLET_OK && stats.completed==1 && !stats.pending_handoffs && stats.operations==1);
  CHECK(qaplet_timer_cancel(runtime,&later,NULL)==QAPLET_OK);
  CHECK(!finish(runtime) && !destroy_gate(&waiting));
  puts("M2 work completion: explicit concurrency; saturated events retain native records; owner handoff retains ref; work permits ready unref timer; last handoff leaves unref timer idle; exact results: pass");
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
  base=(qaplet_config)QAPLET_CONFIG_INIT;base.limits.pump_jobs=1;
  CHECK(qaplet_create_m2(&base,&config,&runtime,NULL)==QAPLET_OK);
  gate mixed={.mutex=PTHREAD_MUTEX_INITIALIZER,.condition=PTHREAD_COND_INITIALIZER};h.userdata=&mixed;
  CHECK(qaplet_register_work_handler(runtime,&h,NULL)==QAPLET_OK);
  const char prepare[]="Promise.all([import('qaplet:events'),import('qaplet:timers')]).then(([e,t])=>{e.onEvent(()=>{});globalThis.install=()=>{for(let i=0;i<32;++i)t.setTimeout(()=>{let p=Promise.resolve();for(let j=0;j<3;++j)p=p.then(()=>{})},0)}})";
  CHECK(qaplet_eval(runtime,prepare,sizeof(prepare)-1,&progress,NULL)==QAPLET_OK);
  while (progress.phase==QAPLET_PHASE_PENDING) CHECK(qaplet_pump(runtime,&progress,NULL)==QAPLET_OK);
  CHECK(qaplet_eval(runtime,"install();42",12,&progress,NULL)==QAPLET_OK && progress.phase==QAPLET_PHASE_DONE);
  for (unsigned i=0;i<32;++i) {
    CHECK(qaplet_post_event(runtime,i,0,NULL,0,NULL)==QAPLET_OK);
    CHECK(qaplet_work_submit(runtime,"quick",5,NULL,0,&tasks[i],NULL)==QAPLET_OK);
  }
  do {CHECK(qaplet_get_work_snapshot(runtime,&stats,NULL)==QAPLET_OK);sched_yield();} while (stats.completed<32);
  qaplet_timer_snapshot timers=QAPLET_TIMER_SNAPSHOT_INIT;
  CHECK(qaplet_pump(runtime,&progress,NULL)==QAPLET_OK && progress.phase==QAPLET_PHASE_PENDING);
  CHECK(qaplet_get_work_snapshot(runtime,&stats,NULL)==QAPLET_OK && stats.pending_handoffs==31);
  CHECK(qaplet_get_timer_snapshot(runtime,&timers,NULL)==QAPLET_OK && timers.current==31);
  CHECK(qaplet_get_snapshot(runtime,&events,NULL)==QAPLET_OK && !events.events_consumed);
  CHECK(qaplet_pump(runtime,&progress,NULL)==QAPLET_OK && progress.phase==QAPLET_PHASE_PENDING);
  CHECK(qaplet_get_work_snapshot(runtime,&stats,NULL)==QAPLET_OK && stats.pending_handoffs==31);
  CHECK(qaplet_get_timer_snapshot(runtime,&timers,NULL)==QAPLET_OK && timers.current==31);
  CHECK(qaplet_pump(runtime,&progress,NULL)==QAPLET_OK && progress.phase==QAPLET_PHASE_PENDING);
  CHECK(qaplet_get_snapshot(runtime,&events,NULL)==QAPLET_OK && events.events_consumed==1);
  CHECK(qaplet_get_work_snapshot(runtime,&stats,NULL)==QAPLET_OK && stats.pending_handoffs==30);
  CHECK(qaplet_get_timer_snapshot(runtime,&timers,NULL)==QAPLET_OK && timers.current==30);
  qaplet_drive_result drive=QAPLET_DRIVE_RESULT_INIT;
  CHECK(qaplet_run(runtime,&drive,NULL)==QAPLET_OK && drive.reason==QAPLET_DRIVE_IDLE);
  CHECK(qaplet_get_work_snapshot(runtime,&stats,NULL)==QAPLET_OK && !stats.pending_handoffs);
  CHECK(qaplet_get_timer_snapshot(runtime,&timers,NULL)==QAPLET_OK && !timers.current);
  CHECK(qaplet_get_snapshot(runtime,&events,NULL)==QAPLET_OK && events.events_consumed==32);
  CHECK(!finish(runtime) && !destroy_gate(&mixed));
  puts("M2 work scheduling: completion/timer/event bounded rotation; complete jobs before each handoff; persisted cursor across jobs-only pumps; all categories drain: pass");
  return 0;
}
typedef struct { qaplet_runtime *runtime; qaplet_work_task task; qaplet_code code; } cancel_call;
static void *foreign_cancel(void *opaque) {
  cancel_call *call=opaque;
  call->code=qaplet_work_cancel(call->runtime,&call->task,NULL);
  return NULL;
}
static int cancel_contract(void) {
  qaplet_config base=QAPLET_CONFIG_INIT;qaplet_m2_config config=QAPLET_M2_CONFIG_INIT;
  config.limits.worker_threads=1;
  qaplet_runtime *runtime=NULL;CHECK(qaplet_create_m2(&base,&config,&runtime,NULL)==QAPLET_OK);
  gate g={.mutex=PTHREAD_MUTEX_INITIALIZER,.condition=PTHREAD_COND_INITIALIZER};
  qaplet_work_handler h=QAPLET_WORK_HANDLER_INIT;
  h.name="held";h.name_length=4;h.function=held;h.userdata=&g;h.input_bytes=1;h.output_bytes=1;
  CHECK(qaplet_register_work_handler(runtime,&h,NULL)==QAPLET_OK);
  qaplet_progress progress=QAPLET_PROGRESS_INIT;
  CHECK(qaplet_eval(runtime,"42",2,&progress,NULL)==QAPLET_OK);
  qaplet_work_task running=QAPLET_WORK_TASK_INIT,queued=QAPLET_WORK_TASK_INIT;unsigned char input=17;
  CHECK(qaplet_work_submit(runtime,"held",4,&input,1,&running,NULL)==QAPLET_OK);wait_entered(&g,1,0);
  CHECK(qaplet_work_submit(runtime,"held",4,&input,1,&queued,NULL)==QAPLET_OK);
  CHECK(qaplet_work_release(runtime,&running,NULL)==QAPLET_BAD_STATE);
  cancel_call call={runtime,queued,QAPLET_BAD_STATE};pthread_t thread;
  CHECK(!pthread_create(&thread,NULL,foreign_cancel,&call) && !pthread_join(thread,NULL) && call.code==QAPLET_OK);
  CHECK(qaplet_work_cancel(runtime,&queued,NULL)==QAPLET_OK);
  CHECK(qaplet_work_cancel(runtime,&running,NULL)==QAPLET_OK);
  CHECK(qaplet_work_cancel(runtime,&running,NULL)==QAPLET_OK);
  qaplet_work_result result=QAPLET_WORK_RESULT_INIT;
  CHECK(qaplet_work_copy_result(runtime,&queued,&result,NULL)==QAPLET_OK && result.state==QAPLET_WORK_CANCELLED && result.code==QAPLET_CANCELLED && !result.length && !result.error_domain && !result.error);
  CHECK(qaplet_work_copy_result(runtime,&running,&result,NULL)==QAPLET_OK && result.state==QAPLET_WORK_CANCELLED && result.code==QAPLET_CANCELLED);
  qaplet_work_snapshot stats=QAPLET_WORK_SNAPSHOT_INIT;
  CHECK(qaplet_get_work_snapshot(runtime,&stats,NULL)==QAPLET_OK && stats.cancelled==2 && !stats.completed && !stats.queued && stats.running==1 && stats.pending_handoffs==2 && stats.operations==2 && stats.data_bytes==4);
  qaplet_drive_result drive=QAPLET_DRIVE_RESULT_INIT;
  CHECK(qaplet_run(runtime,&drive,NULL)==QAPLET_OK && drive.reason==QAPLET_DRIVE_IDLE);
  CHECK(qaplet_get_work_snapshot(runtime,&stats,NULL)==QAPLET_OK && !stats.pending_handoffs && stats.running==1);
  CHECK(qaplet_work_release(runtime,&queued,NULL)==QAPLET_OK && qaplet_work_release(runtime,&running,NULL)==QAPLET_OK);
  CHECK(qaplet_work_cancel(runtime,&queued,NULL)==QAPLET_INVALID_HANDLE && qaplet_work_cancel(runtime,&running,NULL)==QAPLET_INVALID_HANDLE);
  CHECK(qaplet_get_work_snapshot(runtime,&stats,NULL)==QAPLET_OK && stats.operations==1 && stats.data_bytes==2 && stats.running==1);
  release_gate(&g);
  do {CHECK(qaplet_get_work_snapshot(runtime,&stats,NULL)==QAPLET_OK);sched_yield();} while (stats.running);
  CHECK(stats.cancelled==2 && !stats.completed && !stats.operations && !stats.data_bytes && g.entered==1 && g.exited==1 && g.observed_cancel && g.seen==17);
  qaplet_result js=QAPLET_RESULT_INIT;CHECK(qaplet_get_result(runtime,&js,NULL)==QAPLET_OK && js.number==42);
  CHECK(!finish(runtime) && !destroy_gate(&g));
  puts("M2 work cancel: queued never starts; foreign accepted cancellation wins once; running side effect survives; repeat idempotent; owner handoff ends ref; release retains physical bytes; late result discarded: pass");
  return 0;
}
static int cancel_result_contract(void) {
  qaplet_config base=QAPLET_CONFIG_INIT;base.limits.pump_jobs=1;
  qaplet_m2_config config=QAPLET_M2_CONFIG_INIT;qaplet_runtime *runtime=NULL;
  CHECK(qaplet_create_m2(&base,&config,&runtime,NULL)==QAPLET_OK);
  gate g={.mutex=PTHREAD_MUTEX_INITIALIZER,.condition=PTHREAD_COND_INITIALIZER};
  qaplet_work_handler h=QAPLET_WORK_HANDLER_INIT;
  h.name="held";h.name_length=4;h.function=held;h.userdata=&g;h.input_bytes=1;h.output_bytes=1;
  CHECK(qaplet_register_work_handler(runtime,&h,NULL)==QAPLET_OK);
  qaplet_work_task task=QAPLET_WORK_TASK_INIT;unsigned char input=17;
  CHECK(qaplet_work_submit(runtime,"held",4,&input,1,&task,NULL)==QAPLET_OK);wait_entered(&g,1,0);
  release_gate(&g);
  qaplet_work_snapshot stats=QAPLET_WORK_SNAPSHOT_INIT;
  do {CHECK(qaplet_get_work_snapshot(runtime,&stats,NULL)==QAPLET_OK);sched_yield();} while (!stats.completed);
  CHECK(stats.pending_handoffs==1 && !stats.running && stats.cancelled==0);
  char diagnostic_byte='x';qaplet_diagnostic diagnostic=QAPLET_DIAGNOSTIC_INIT;
  diagnostic.message=&diagnostic_byte;diagnostic.capacity=1;
  CHECK(qaplet_work_cancel(runtime,&task,&diagnostic)==QAPLET_ALREADY_COMPLETED && diagnostic.code==QAPLET_ALREADY_COMPLETED && diagnostic.length && (diagnostic.flags & QAPLET_DIAGNOSTIC_TRUNCATED) && !diagnostic_byte);
  CHECK(qaplet_work_cancel(runtime,&task,NULL)==QAPLET_ALREADY_COMPLETED);
  qaplet_work_result result=QAPLET_WORK_RESULT_INIT;unsigned char bytes[2]={99,98};result.buffer=bytes;
  CHECK(qaplet_work_copy_result(runtime,&task,&result,NULL)==QAPLET_BUFFER_TOO_SMALL && result.length==1 && bytes[0]==99 && bytes[1]==98);
  result.capacity=sizeof(bytes);
  CHECK(qaplet_work_copy_result(runtime,&task,&result,NULL)==QAPLET_OK && result.state==QAPLET_WORK_SUCCEEDED && result.code==QAPLET_OK && result.length==1 && bytes[0]==17 && bytes[1]==98);
  bytes[0]=99;CHECK(qaplet_work_copy_result(runtime,&task,&result,NULL)==QAPLET_OK && bytes[0]==17 && g.entered==1 && g.exited==1);
  const char jobs[]="let p=Promise.resolve();for(let i=0;i<8;++i)p=p.then(()=>{});42";
  qaplet_progress progress=QAPLET_PROGRESS_INIT;
  CHECK(qaplet_eval(runtime,jobs,sizeof(jobs)-1,&progress,NULL)==QAPLET_OK && progress.phase==QAPLET_PHASE_PENDING);
  CHECK(qaplet_work_release(runtime,&task,NULL)==QAPLET_OK);
  CHECK(qaplet_work_cancel(runtime,&task,NULL)==QAPLET_INVALID_HANDLE && qaplet_work_copy_result(runtime,&task,&result,NULL)==QAPLET_INVALID_HANDLE && qaplet_work_release(runtime,&task,NULL)==QAPLET_INVALID_HANDLE);
  CHECK(qaplet_get_work_snapshot(runtime,&stats,NULL)==QAPLET_OK && stats.pending_handoffs==1 && stats.operations==1 && stats.data_bytes==2);
  CHECK(qaplet_pump(runtime,&progress,NULL)==QAPLET_OK && progress.phase==QAPLET_PHASE_PENDING);
  CHECK(qaplet_get_work_snapshot(runtime,&stats,NULL)==QAPLET_OK && stats.pending_handoffs==1);
  qaplet_drive_result drive=QAPLET_DRIVE_RESULT_INIT;
  CHECK(qaplet_run(runtime,&drive,NULL)==QAPLET_OK && drive.reason==QAPLET_DRIVE_IDLE);
  CHECK(qaplet_get_work_snapshot(runtime,&stats,NULL)==QAPLET_OK && stats.completed==1 && !stats.cancelled && !stats.pending_handoffs && !stats.operations && !stats.data_bytes);
  qaplet_result js=QAPLET_RESULT_INIT;CHECK(qaplet_get_result(runtime,&js,NULL)==QAPLET_OK && js.number==42);
  CHECK(!finish(runtime) && !destroy_gate(&g));
  /* STOPPED abandons further JS delivery, so an already released, physically
   * completed record has no remaining handoff or storage responsibility. */
  CHECK(qaplet_create_m2(&base,&config,&runtime,NULL)==QAPLET_OK);
  gate stopped={.mutex=PTHREAD_MUTEX_INITIALIZER,.condition=PTHREAD_COND_INITIALIZER,.released=1};h.userdata=&stopped;
  CHECK(qaplet_register_work_handler(runtime,&h,NULL)==QAPLET_OK);
  CHECK(qaplet_work_submit(runtime,"held",4,&input,1,&task,NULL)==QAPLET_OK);
  do {CHECK(qaplet_get_work_snapshot(runtime,&stats,NULL)==QAPLET_OK);sched_yield();} while (!stats.completed);
  CHECK(qaplet_work_release(runtime,&task,NULL)==QAPLET_OK);
  CHECK(qaplet_get_work_snapshot(runtime,&stats,NULL)==QAPLET_OK && stats.operations==1 && stats.pending_handoffs==1);
  CHECK(qaplet_request_stop(runtime,NULL)==QAPLET_OK && qaplet_run_once(runtime,0,&drive,NULL)==QAPLET_EXECUTION_STOPPED);
  CHECK(qaplet_get_work_snapshot(runtime,&stats,NULL)==QAPLET_OK && !stats.operations && !stats.data_bytes && !stats.pending_handoffs && !stats.retained_results);
  CHECK(!finish(runtime) && !destroy_gate(&stopped));
  puts("M2 work cancel result: native completion wins before owner; already-completed stable; short result retry never reruns; release invalidates immediately; complete jobs precede ref/capacity handoff: pass");
  return 0;
}
static void *cancel_waiting_owner(void *opaque) {
  cancel_call *call=opaque;qaplet_m2_snapshot state=QAPLET_M2_SNAPSHOT_INIT;
  qaplet_work_status status=QAPLET_WORK_STATUS_INIT;
  if (qaplet_get_work_status(call->runtime,&call->task,&status,NULL)!=QAPLET_OK ||
      status.state!=QAPLET_WORK_RUNNING || !status.physical_pending || !status.pending_handoff) return NULL;
  do {
    if (qaplet_get_m2_snapshot(call->runtime,&state,NULL)!=QAPLET_OK) return NULL;
    sched_yield();
  } while (!state.driving || !state.wait_calls);
  call->code=qaplet_work_cancel(call->runtime,&call->task,NULL);
  return NULL;
}
static int cancel_status_contract(void) {
  qaplet_config base=QAPLET_CONFIG_INIT;qaplet_m2_config config=QAPLET_M2_CONFIG_INIT;config.limits.worker_threads=1;
  qaplet_runtime *runtime=NULL,*other=NULL,*legacy=NULL;
  CHECK(qaplet_create_m2(&base,&config,&runtime,NULL)==QAPLET_OK && qaplet_create_m2(&base,&config,&other,NULL)==QAPLET_OK && qaplet_create(&base,&legacy,NULL)==QAPLET_OK);
  gate g={.mutex=PTHREAD_MUTEX_INITIALIZER,.condition=PTHREAD_COND_INITIALIZER};
  qaplet_work_handler h=QAPLET_WORK_HANDLER_INIT;
  h.name="held";h.name_length=4;h.function=held;h.userdata=&g;h.input_bytes=1;h.output_bytes=1;
  CHECK(qaplet_register_work_handler(runtime,&h,NULL)==QAPLET_OK);
  qaplet_work_task running=QAPLET_WORK_TASK_INIT,queued=QAPLET_WORK_TASK_INIT;unsigned char input=17;
  CHECK(qaplet_work_submit(runtime,"held",4,&input,1,&running,NULL)==QAPLET_OK);wait_entered(&g,1,0);
  CHECK(qaplet_work_submit(runtime,"held",4,&input,1,&queued,NULL)==QAPLET_OK);
  qaplet_work_status status=QAPLET_WORK_STATUS_INIT;
  CHECK(qaplet_get_work_status(runtime,&queued,&status,NULL)==QAPLET_OK && status.state==QAPLET_WORK_QUEUED && !status.physical_pending && status.pending_handoff && !status.length);
  status.struct_size=8;status.state=QAPLET_WORK_FAILED;
  CHECK(qaplet_get_work_status(runtime,&running,&status,NULL)==QAPLET_ABI_MISMATCH && status.state==QAPLET_WORK_FAILED);
  status=(qaplet_work_status)QAPLET_WORK_STATUS_INIT;status.abi_version=99;
  CHECK(qaplet_get_work_status(runtime,&running,&status,NULL)==QAPLET_ABI_MISMATCH);
  struct { qaplet_work_status value; uint64_t tail; } extended={QAPLET_WORK_STATUS_INIT,0};extended.value.struct_size=sizeof(extended);
  CHECK(qaplet_get_work_status(runtime,&running,&extended.value,NULL)==QAPLET_OK && extended.value.physical_pending && !extended.tail);
  extended.tail=1;CHECK(qaplet_get_work_status(runtime,&running,&extended.value,NULL)==QAPLET_ABI_MISMATCH);
  status=(qaplet_work_status)QAPLET_WORK_STATUS_INIT;
  CHECK(qaplet_get_work_status(other,&running,&status,NULL)==QAPLET_INVALID_HANDLE && qaplet_work_cancel(other,&running,NULL)==QAPLET_INVALID_HANDLE);
  CHECK(qaplet_get_work_status(legacy,&running,&status,NULL)==QAPLET_UNSUPPORTED && qaplet_work_cancel(legacy,&running,NULL)==QAPLET_UNSUPPORTED);
  CHECK(qaplet_get_work_status(NULL,&running,&status,NULL)==QAPLET_INVALID_ARGUMENT && qaplet_work_cancel(NULL,&running,NULL)==QAPLET_INVALID_ARGUMENT);
  CHECK(qaplet_get_work_status(runtime,NULL,&status,NULL)==QAPLET_INVALID_ARGUMENT && qaplet_work_cancel(runtime,NULL,NULL)==QAPLET_INVALID_ARGUMENT);
  CHECK(qaplet_work_cancel(runtime,&queued,NULL)==QAPLET_OK && qaplet_work_release(runtime,&queued,NULL)==QAPLET_OK);
  qaplet_drive_result drive=QAPLET_DRIVE_RESULT_INIT;
  CHECK(qaplet_run_once(runtime,0,&drive,NULL)==QAPLET_OK && drive.progressed);
  cancel_call call={runtime,running,QAPLET_BAD_STATE};pthread_t thread;
  CHECK(!pthread_create(&thread,NULL,cancel_waiting_owner,&call));
  CHECK(qaplet_run_once(runtime,3000,&drive,NULL)==QAPLET_OK && drive.reason==QAPLET_DRIVE_PROGRESS && drive.progressed);
  CHECK(!pthread_join(thread,NULL) && call.code==QAPLET_OK);
  CHECK(qaplet_run_once(runtime,0,&drive,NULL)==QAPLET_OK && drive.reason==QAPLET_DRIVE_IDLE && !drive.progressed);
  CHECK(qaplet_get_work_status(runtime,&running,&status,NULL)==QAPLET_OK && status.state==QAPLET_WORK_CANCELLED && status.code==QAPLET_CANCELLED && status.physical_pending && !status.pending_handoff && !status.error_domain && !status.error);
  CHECK(qaplet_get_work_status(runtime,&queued,&status,NULL)==QAPLET_INVALID_HANDLE);
  CHECK(qaplet_work_submit(runtime,"held",4,&input,1,&queued,NULL)==QAPLET_OK);
  qaplet_work_task stale=queued;--stale.generation;
  CHECK(qaplet_get_work_status(runtime,&stale,&status,NULL)==QAPLET_INVALID_HANDLE && qaplet_work_cancel(runtime,&stale,NULL)==QAPLET_INVALID_HANDLE);
  CHECK(qaplet_close(runtime,NULL)==QAPLET_OK && qaplet_get_work_status(runtime,&running,&status,NULL)==QAPLET_INVALID_HANDLE && qaplet_work_cancel(runtime,&running,NULL)==QAPLET_INVALID_HANDLE);
  CHECK(qaplet_quiesce(runtime,0,&drive,NULL)==QAPLET_OK && drive.reason==QAPLET_DRIVE_WAIT_TIMEOUT && qaplet_destroy(runtime,NULL)==QAPLET_BUSY);
  release_gate(&g);CHECK(!finish(runtime) && !finish(other) && !destroy_gate(&g));
  CHECK(qaplet_close(legacy,NULL)==QAPLET_OK && qaplet_destroy(legacy,NULL)==QAPLET_OK);
  puts("M2 work cancel status: versioned zero-tail POD; any-thread query/cancel wakes waiting owner; queued/running/logical/physical/ref distinct; instance and generation rejection; CLOSED invalidation; native retry: pass");
  return 0;
}
typedef struct {
  pthread_mutex_t mutex;
  pthread_cond_t condition;
  unsigned stage, allow_read, allow_return, observed_cancel;
  qaplet_code native_code;
  unsigned char bytes[4];
} file_gate;
static qaplet_code gated_file(qaplet_work_context *context,const void *input,size_t length,
                               qaplet_work_output *output,void *opaque) {
  file_gate *g=opaque;
  pthread_mutex_lock(&g->mutex);g->stage=1;pthread_cond_broadcast(&g->condition);
  while (!g->allow_read) pthread_cond_wait(&g->condition,&g->mutex);
  pthread_mutex_unlock(&g->mutex);
  qaplet_code code=read_file(context,input,length,output,NULL);
  pthread_mutex_lock(&g->mutex);g->native_code=code;
  if (!code && output->length==sizeof(g->bytes)) memcpy(g->bytes,output->data,sizeof(g->bytes));
  g->observed_cancel=qaplet_work_cancelled(context);g->stage=2;pthread_cond_broadcast(&g->condition);
  while (!g->allow_return) pthread_cond_wait(&g->condition,&g->mutex);
  pthread_mutex_unlock(&g->mutex);
  return code;
}
static void file_stage(file_gate *g,unsigned stage) {
  pthread_mutex_lock(&g->mutex);while (g->stage<stage) pthread_cond_wait(&g->condition,&g->mutex);pthread_mutex_unlock(&g->mutex);
}
static void file_allow(file_gate *g,int returning) {
  pthread_mutex_lock(&g->mutex);if (returning) g->allow_return=1;else g->allow_read=1;
  pthread_cond_broadcast(&g->condition);pthread_mutex_unlock(&g->mutex);
}
static int cancel_file_contract(void) {
  for (unsigned order=0;order<3;++order) {
    char directory[]="./qaplet-cancel-file-XXXXXX";CHECK(mkdtemp(directory));
    char path[sizeof(directory)+sizeof("/input-XXXXXX")];
    CHECK(snprintf(path,sizeof(path),"%s/input-XXXXXX",directory)>0);
    int fd=mkstemp(path);const unsigned char expected[]={0,255,17,42};
    CHECK(fd>=0 && write(fd,expected,sizeof(expected))==sizeof(expected) && !close(fd));
    qaplet_config base=QAPLET_CONFIG_INIT;qaplet_m2_config config=QAPLET_M2_CONFIG_INIT;qaplet_runtime *runtime=NULL;
    CHECK(qaplet_create_m2(&base,&config,&runtime,NULL)==QAPLET_OK);
    file_gate g={.mutex=PTHREAD_MUTEX_INITIALIZER,.condition=PTHREAD_COND_INITIALIZER};
    qaplet_work_handler h=QAPLET_WORK_HANDLER_INIT;
    h.name="file";h.name_length=4;h.function=gated_file;h.userdata=&g;h.output_bytes=4;
    CHECK(qaplet_register_work_handler(runtime,&h,NULL)==QAPLET_OK);
    qaplet_work_task task=QAPLET_WORK_TASK_INIT;char input[sizeof(path)];memcpy(input,path,sizeof(path));
    CHECK(qaplet_work_submit(runtime,"file",4,input,strlen(input)+1,&task,NULL)==QAPLET_OK);memset(input,'?',sizeof(input));file_stage(&g,1);
    qaplet_work_status status=QAPLET_WORK_STATUS_INIT;
    CHECK(qaplet_get_work_status(runtime,&task,&status,NULL)==QAPLET_OK && status.state==QAPLET_WORK_RUNNING && status.physical_pending);
    if (!order) CHECK(qaplet_work_cancel(runtime,&task,NULL)==QAPLET_OK);
    file_allow(&g,0);file_stage(&g,2);
    CHECK(!g.native_code && !memcmp(g.bytes,expected,sizeof(expected)) && g.observed_cancel==(!order));
    if (order==1) CHECK(qaplet_work_cancel(runtime,&task,NULL)==QAPLET_OK);
    if (order==2) {
      file_allow(&g,1);
      do {CHECK(qaplet_get_work_status(runtime,&task,&status,NULL)==QAPLET_OK);sched_yield();} while (status.physical_pending);
      CHECK(status.state==QAPLET_WORK_SUCCEEDED && status.pending_handoff && qaplet_work_cancel(runtime,&task,NULL)==QAPLET_ALREADY_COMPLETED);
    }
    qaplet_work_result result=QAPLET_WORK_RESULT_INIT;unsigned char bytes[4]={9,9,9,9};result.buffer=bytes;result.capacity=sizeof(bytes);
    CHECK(qaplet_work_copy_result(runtime,&task,&result,NULL)==QAPLET_OK && !result.error_domain && !result.error);
    if (order==2) CHECK(result.state==QAPLET_WORK_SUCCEEDED && !result.code && result.length==4 && !memcmp(bytes,expected,4));
    else CHECK(result.state==QAPLET_WORK_CANCELLED && result.code==QAPLET_CANCELLED && !result.length && bytes[0]==9);
    CHECK(qaplet_work_release(runtime,&task,NULL)==QAPLET_OK && qaplet_get_work_status(runtime,&task,&status,NULL)==QAPLET_INVALID_HANDLE);
    qaplet_drive_result drive=QAPLET_DRIVE_RESULT_INIT;
    CHECK(qaplet_run(runtime,&drive,NULL)==QAPLET_OK && drive.reason==QAPLET_DRIVE_IDLE);
    qaplet_work_snapshot stats=QAPLET_WORK_SNAPSHOT_INIT;
    CHECK(qaplet_get_work_snapshot(runtime,&stats,NULL)==QAPLET_OK && !stats.pending_handoffs && stats.operations==(order!=2) && stats.running==(order!=2));
    if (order!=2) {
      CHECK(stats.data_bytes==strlen(path)+1+4 && stats.cancelled==1 && !stats.completed);
      CHECK(qaplet_close(runtime,NULL)==QAPLET_OK && qaplet_quiesce(runtime,0,&drive,NULL)==QAPLET_OK && drive.reason==QAPLET_DRIVE_WAIT_TIMEOUT && qaplet_destroy(runtime,NULL)==QAPLET_BUSY);
      file_allow(&g,1);
    }
    CHECK(!finish(runtime));
    CHECK(!pthread_cond_destroy(&g.condition) && !pthread_mutex_destroy(&g.mutex) && !unlink(path) && !rmdir(directory));
  }
  puts("M2 work cancel file: real bounded binary read with copied path; cancellation before syscall and after bytes never rolls back; completion publication wins independently of owner; released physical buffers survive close and join: pass");
  return 0;
}
typedef struct { qaplet_runtime *runtime; qaplet_work_task task; _Atomic unsigned hooks,failed; unsigned callback; } cancel_permissions;
static void forbidden_cancel_notify(void *opaque) {
  cancel_permissions *p=opaque;qaplet_work_status status=QAPLET_WORK_STATUS_INIT;
  if (qaplet_work_cancel(p->runtime,&p->task,NULL)!=QAPLET_BUSY ||
      qaplet_get_work_status(p->runtime,&p->task,&status,NULL)!=QAPLET_BUSY) p->failed=1;
  ++p->hooks;
}
static void cancel_timer_callback(qaplet_runtime *runtime,const qaplet_timer *timer,void *opaque) {
  (void)timer;cancel_permissions *p=opaque;qaplet_work_status status=QAPLET_WORK_STATUS_INIT;
  qaplet_work_result result=QAPLET_WORK_RESULT_INIT;
  p->callback=qaplet_work_cancel(runtime,&p->task,NULL)==QAPLET_OK &&
    qaplet_get_work_status(runtime,&p->task,&status,NULL)==QAPLET_OK && status.state==QAPLET_WORK_CANCELLED && status.physical_pending &&
    qaplet_work_copy_result(runtime,&p->task,&result,NULL)==QAPLET_BUSY && qaplet_work_release(runtime,&p->task,NULL)==QAPLET_BUSY;
}
static int cancel_permissions_contract(void) {
  qaplet_config base=QAPLET_CONFIG_INIT;qaplet_m2_config config=QAPLET_M2_CONFIG_INIT;
  cancel_permissions p={0};CHECK(qaplet_create_m2(&base,&config,&p.runtime,NULL)==QAPLET_OK);
  qaplet_host_notify notify=QAPLET_HOST_NOTIFY_INIT;notify.function=forbidden_cancel_notify;notify.userdata=&p;
  CHECK(qaplet_set_host_notify(p.runtime,&notify,NULL)==QAPLET_OK);
  gate g={.mutex=PTHREAD_MUTEX_INITIALIZER,.condition=PTHREAD_COND_INITIALIZER};
  qaplet_work_handler h=QAPLET_WORK_HANDLER_INIT;
  h.name="held";h.name_length=4;h.function=held;h.userdata=&g;h.input_bytes=1;h.output_bytes=1;
  CHECK(qaplet_register_work_handler(p.runtime,&h,NULL)==QAPLET_OK);
  CHECK(qaplet_work_submit(p.runtime,"held",4,NULL,0,&p.task,NULL)==QAPLET_OK);wait_entered(&g,1,0);
  qaplet_timer timer=QAPLET_TIMER_INIT;
  CHECK(qaplet_timer_create(p.runtime,0,cancel_timer_callback,&p,&timer,NULL)==QAPLET_OK);
  qaplet_drive_result drive=QAPLET_DRIVE_RESULT_INIT;
  CHECK(qaplet_run(p.runtime,&drive,NULL)==QAPLET_OK && drive.reason==QAPLET_DRIVE_IDLE && p.callback && p.hooks && !p.failed);
  release_gate(&g);CHECK(!finish(p.runtime) && !destroy_gate(&g) && !p.failed);
  puts("M2 work cancel permissions: owner callback controls and POD query allowed; owner result/release remain BUSY; HostNotify rejects both APIs through TLS guard; native lifetime joined: pass");
  return 0;
}
static int cancel_never_exit_contract(void) {
  qaplet_config base=QAPLET_CONFIG_INIT;qaplet_m2_config config=QAPLET_M2_CONFIG_INIT;
  qaplet_runtime *runtime=NULL;CHECK(qaplet_create_m2(&base,&config,&runtime,NULL)==QAPLET_OK);
  /* Deliberately retained heap userdata: no return/scope lifetime can free it
   * while the native call remains held through process termination. */
  gate *g=calloc(1,sizeof(*g));CHECK(g && !pthread_mutex_init(&g->mutex,NULL) && !pthread_cond_init(&g->condition,NULL));
  qaplet_work_handler h=QAPLET_WORK_HANDLER_INIT;
  h.name="held";h.name_length=4;h.function=held;h.userdata=g;h.input_bytes=1;h.output_bytes=1;
  CHECK(qaplet_register_work_handler(runtime,&h,NULL)==QAPLET_OK);
  qaplet_work_task task=QAPLET_WORK_TASK_INIT;unsigned char input=17;
  CHECK(qaplet_work_submit(runtime,"held",4,&input,1,&task,NULL)==QAPLET_OK);wait_entered(g,1,0);
  CHECK(qaplet_work_cancel(runtime,&task,NULL)==QAPLET_OK && qaplet_work_release(runtime,&task,NULL)==QAPLET_OK && qaplet_close(runtime,NULL)==QAPLET_OK);
  qaplet_drive_result drive=QAPLET_DRIVE_RESULT_INIT;
  CHECK(qaplet_quiesce(runtime,0,&drive,NULL)==QAPLET_OK && drive.reason==QAPLET_DRIVE_WAIT_TIMEOUT);
  CHECK(qaplet_quiesce(runtime,5,&drive,NULL)==QAPLET_OK && drive.reason==QAPLET_DRIVE_WAIT_TIMEOUT && qaplet_destroy(runtime,NULL)==QAPLET_BUSY);
  CHECK(qaplet_quiesce(runtime,0,&drive,NULL)==QAPLET_OK && drive.reason==QAPLET_DRIVE_WAIT_TIMEOUT);
  qaplet_work_snapshot stats=QAPLET_WORK_SNAPSHOT_INIT;qaplet_m2_snapshot state=QAPLET_M2_SNAPSHOT_INIT;
  CHECK(qaplet_get_work_snapshot(runtime,&stats,NULL)==QAPLET_OK && stats.operations==1 && stats.data_bytes==2 && stats.running==1 && stats.handlers==1);
  CHECK(qaplet_get_m2_snapshot(runtime,&state,NULL)==QAPLET_OK && state.lifecycle==QAPLET_CLOSED && state.native_state==QAPLET_NATIVE_CLEANING && !state.engine_current_bytes);
  puts("M2 work cancel nonreturning: gate remains held through process termination; CLOSED retains physical bytes/code/userdata; zero and finite quiesce retry pending; destroy BUSY; no cleanup success claimed: pass");
  return 0;
}
static int cancel_limits_contract(void) {
  for (unsigned limit=0;limit<3;++limit) {
    qaplet_config base=QAPLET_CONFIG_INIT;qaplet_m2_config config=QAPLET_M2_CONFIG_INIT;
    if (limit==2) {base.limits.event_queue_slots=128;base.limits.event_payload_bytes=98304;}
    qaplet_runtime *runtime=NULL;CHECK(qaplet_create_m2(&base,&config,&runtime,NULL)==QAPLET_OK);
    gate g={.mutex=PTHREAD_MUTEX_INITIALIZER,.condition=PTHREAD_COND_INITIALIZER};
    size_t input_length=limit ? 1048576 : 1;unsigned count=limit==0 ? 128 : limit==1 ? 4 : 1;
    unsigned char *input=malloc(input_length);CHECK(input);memset(input,17,input_length);
    qaplet_work_handler h=QAPLET_WORK_HANDLER_INIT;
    h.name="held";h.name_length=4;h.function=held;h.userdata=&g;h.input_bytes=input_length;h.output_bytes=input_length;
    CHECK(qaplet_register_work_handler(runtime,&h,NULL)==QAPLET_OK);
    qaplet_work_task tasks[128]={{0}},refused={99,98,97};
    for (unsigned i=0;i<count;++i) CHECK(qaplet_work_submit(runtime,"held",4,input,input_length,&tasks[i],NULL)==QAPLET_OK);
    wait_entered(&g,1,0);
    CHECK(qaplet_work_submit(runtime,"held",4,input,input_length,&refused,NULL)==QAPLET_RESOURCE_LIMIT && refused.runtime_id==99 && refused.slot==98 && refused.generation==97);
    for (unsigned i=0;i<count;++i) CHECK(qaplet_work_cancel(runtime,&tasks[i],NULL)==QAPLET_OK && qaplet_work_release(runtime,&tasks[i],NULL)==QAPLET_OK);
    CHECK(qaplet_work_submit(runtime,"held",4,input,input_length,&refused,NULL)==QAPLET_RESOURCE_LIMIT);
    qaplet_work_snapshot stats=QAPLET_WORK_SNAPSHOT_INIT;qaplet_m2_snapshot state=QAPLET_M2_SNAPSHOT_INIT;
    CHECK(qaplet_get_work_snapshot(runtime,&stats,NULL)==QAPLET_OK && stats.operations==count && stats.data_bytes==count*input_length*2 && stats.running==1 && !stats.queued && stats.pending_handoffs==count);
    CHECK(qaplet_get_m2_snapshot(runtime,&state,NULL)==QAPLET_OK && state.lifecycle==QAPLET_OPEN && !state.termination_reason && state.m2_limits.operations==128 && state.m2_limits.operation_data_bytes==8388608 && state.limits.native_storage_bytes==16777216 && state.native_current_bytes<=16777216);
    if (limit==1) CHECK(stats.data_bytes==8388608);
    if (limit==2) CHECK(stats.data_bytes==2097152 && state.native_current_bytes>12582912);
    qaplet_drive_result drive=QAPLET_DRIVE_RESULT_INIT;
    CHECK(qaplet_run(runtime,&drive,NULL)==QAPLET_OK && drive.reason==QAPLET_DRIVE_IDLE);
    CHECK(qaplet_get_work_snapshot(runtime,&stats,NULL)==QAPLET_OK && stats.operations==1 && stats.data_bytes==input_length*2 && stats.running==1 && !stats.pending_handoffs);
    if (limit==2) CHECK(qaplet_work_submit(runtime,"held",4,input,input_length,&refused,NULL)==QAPLET_RESOURCE_LIMIT);
    else {
      unsigned available=limit ? 3 : 127;
      for (unsigned i=0;i<available;++i) CHECK(qaplet_work_submit(runtime,"held",4,input,input_length,&tasks[i],NULL)==QAPLET_OK);
      CHECK(qaplet_work_submit(runtime,"held",4,input,input_length,&refused,NULL)==QAPLET_RESOURCE_LIMIT);
      for (unsigned i=0;i<available;++i) CHECK(qaplet_work_cancel(runtime,&tasks[i],NULL)==QAPLET_OK && qaplet_work_release(runtime,&tasks[i],NULL)==QAPLET_OK);
      CHECK(qaplet_run(runtime,&drive,NULL)==QAPLET_OK && drive.reason==QAPLET_DRIVE_IDLE);
    }
    release_gate(&g);
    do {CHECK(qaplet_get_work_snapshot(runtime,&stats,NULL)==QAPLET_OK);sched_yield();} while (stats.running);
    CHECK(!stats.operations && !stats.data_bytes && g.entered==1 && g.exited==1 && g.seen==17 && g.observed_cancel);
    CHECK(qaplet_work_submit(runtime,"held",4,input,input_length,&refused,NULL)==QAPLET_OK && qaplet_run(runtime,&drive,NULL)==QAPLET_OK && qaplet_work_release(runtime,&refused,NULL)==QAPLET_OK);
    free(input);CHECK(!finish(runtime) && !destroy_gate(&g));
  }
  puts("M2 work cancel limits: actual default 128 operations and 8MiB copied/reserved data within 16MiB total; cancel/release waits for owner and physical use; saturation atomic; physical bytes remain charged; ordinary capacity recovers: pass");
  return 0;
}
int sdk_work(const char *mode) {
  if (!strcmp(mode,"--m2-work-cancel-limits")) return cancel_limits_contract();
  if (!strcmp(mode,"--m2-work-cancel-file")) return cancel_file_contract();
  if (!strcmp(mode,"--m2-work-cancel-permissions")) return cancel_permissions_contract();
  if (!strcmp(mode,"--m2-work-cancel-nonreturning")) return cancel_never_exit_contract();
  if (!strcmp(mode,"--m2-work-cancel-status")) return cancel_status_contract();
  if (!strcmp(mode,"--m2-work-cancel-result")) return cancel_result_contract();
  if (!strcmp(mode,"--m2-work-cancel")) return cancel_contract();
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
