/* SPDX-License-Identifier: MIT */
/* Cross-resource contracts use only the distributed public SDK. */
#define _COSMO_SOURCE 1
#include "qaplet.h"
#include <pthread.h>
#include <sched.h>
#include <fcntl.h>
#include <limits.h>
#include <stdlib.h>
#include "sdk_temp.h"
#include <unistd.h>
#include <time.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#include "libc/dce.h"
#include "libc/nt/runtime.h"
extern int64_t CreateFile(const char16_t *,uint32_t,uint32_t,void *,uint32_t,uint32_t,int64_t);
extern int __mkntpath(const char *,char16_t *);

#define CHECK(c) do { if (!(c)) { fprintf(stderr,"aggregate public contract line %d: %s\n",__LINE__,#c); return 1; } } while (0)

typedef struct { qaplet_runtime *runtime; unsigned calls,failures; } diagnostic_probe;
static unsigned refused_io(qaplet_runtime *r,qaplet_code expected,int short_prefix) {
  struct { qaplet_diagnostic d; unsigned char canary[32]; } storage;
  memset(&storage,0x5a,sizeof(storage));
  storage.d=(qaplet_diagnostic)QAPLET_DIAGNOSTIC_INIT;
  storage.d.struct_size=short_prefix ? 8 : sizeof(storage.d);
  storage.d.code=QAPLET_HOST_ERROR;storage.d.flags=0x1234;storage.d.length=123;
  unsigned char original[sizeof(storage)];memcpy(original,&storage,sizeof(storage));
  qaplet_io_endpoint e=QAPLET_IO_ENDPOINT_INIT;qaplet_io_operation op=QAPLET_IO_OPERATION_INIT;
  qaplet_io_endpoint_status endpoint=QAPLET_IO_ENDPOINT_STATUS_INIT;qaplet_io_status status=QAPLET_IO_STATUS_INIT;
  qaplet_io_result result=QAPLET_IO_RESULT_INIT;qaplet_io_snapshot snapshot=QAPLET_IO_SNAPSHOT_INIT;
  qaplet_resource_snapshot resources=QAPLET_RESOURCE_SNAPSHOT_INIT;
  qaplet_stdio_info info=QAPLET_STDIO_INFO_INIT;qaplet_process process=QAPLET_PROCESS_INIT;
  unsigned failures=0;
#define REFUSAL(call) do { memcpy(&storage,original,sizeof(storage)); qaplet_code c=(call); if(c!=expected || memcmp(&storage,original,sizeof(storage)))++failures; } while(0)
  REFUSAL(qaplet_stdio_inspect(r,0,&info,&storage.d));
  REFUSAL(qaplet_stdio_attach(r,0,&e,&info,&storage.d));
  REFUSAL(qaplet_stdio_get(r,0,&e,&info,&storage.d));
  REFUSAL(qaplet_stdio_detach(r,0,&storage.d));
  REFUSAL(qaplet_process_get_stdio(r,&process,0,&e,&storage.d));
  REFUSAL(qaplet_io_read(r,&e,1,&op,&storage.d));
  REFUSAL(qaplet_io_write(r,&e,"x",1,&op,&storage.d));
  REFUSAL(qaplet_io_finish(r,&e,&storage.d));
  REFUSAL(qaplet_io_close(r,&e,&storage.d));
  REFUSAL(qaplet_io_set_ref(r,&e,0,&storage.d));
  REFUSAL(qaplet_get_io_endpoint_status(r,&e,&endpoint,&storage.d));
  REFUSAL(qaplet_io_cancel(r,&op,&storage.d));
  REFUSAL(qaplet_get_io_status(r,&op,&status,&storage.d));
  REFUSAL(qaplet_io_copy_result(r,&op,&result,&storage.d));
  REFUSAL(qaplet_io_release(r,&op,&storage.d));
  REFUSAL(qaplet_get_io_snapshot(r,&snapshot,&storage.d));
  REFUSAL(qaplet_get_resource_snapshot(r,&resources,&storage.d));
#undef REFUSAL
  return failures;
}
static void forbidden_io_notify(void *opaque) {
  diagnostic_probe *p=opaque;++p->calls;p->failures+=refused_io(p->runtime,QAPLET_BUSY,0);
}
static int diagnostics(void) {
  qaplet_config b=QAPLET_CONFIG_INIT;qaplet_m2_config m=QAPLET_M2_CONFIG_INIT;qaplet_runtime *r=NULL;
  CHECK(qaplet_create_m2(&b,&m,&r,NULL)==QAPLET_OK);
  unsigned short_failures=refused_io(r,QAPLET_ABI_MISMATCH,1);
  diagnostic_probe probe={r,0,0};qaplet_host_notify hook=QAPLET_HOST_NOTIFY_INIT;hook.function=forbidden_io_notify;hook.userdata=&probe;
  CHECK(qaplet_set_host_notify(r,&hook,NULL)==QAPLET_OK && qaplet_wake(r,NULL)==QAPLET_OK && probe.calls);
  qaplet_io_snapshot io=QAPLET_IO_SNAPSHOT_INIT;CHECK(qaplet_get_io_snapshot(r,&io,NULL)==QAPLET_OK && !io.endpoints && !io.operations && !io.data_bytes);
  CHECK(qaplet_close(r,NULL)==QAPLET_OK && qaplet_destroy(r,NULL)==QAPLET_OK);
  if(short_failures || probe.failures)fprintf(stderr,"ordinary IO diagnostic refusal: short=%u notify=%u callbacks=%u; Runtime cleaned\n",short_failures,probe.failures,probe.calls);
  CHECK(!short_failures && !probe.failures);
  puts("M2 aggregate diagnostics: public short-prefix POD/canary and HostNotify refusals preserve caller diagnostics; no resource acceptance: pass");return 0;
}
typedef struct {pthread_mutex_t mutex;pthread_cond_t condition;unsigned entered,release;} work_gate;
static qaplet_code gated_work(qaplet_work_context *context,const void *input,size_t length,qaplet_work_output *out,void *opaque) {
  (void)context;work_gate *g=opaque;pthread_mutex_lock(&g->mutex);++g->entered;pthread_cond_broadcast(&g->condition);
  while(!g->release)pthread_cond_wait(&g->condition,&g->mutex);pthread_mutex_unlock(&g->mutex);
  if(length)memcpy(out->data,input,length);out->length=length;return QAPLET_OK;
}
static double milliseconds(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return t.tv_sec*1000.0+t.tv_nsec/1000000.0;}
typedef struct {qaplet_runtime *r;_Atomic unsigned ready,closed,done,failures;unsigned open_samples,closed_samples;} query_probe;
static void *query_pods(void *opaque){
  query_probe *p=opaque;
  for(unsigned phase=0;phase<2;++phase){
    if(phase){atomic_store(&p->ready,1);while(!atomic_load(&p->closed))sched_yield();}
    for(unsigned i=0;i<1000;++i){
      qaplet_snapshot b=QAPLET_SNAPSHOT_INIT;qaplet_m2_snapshot m=QAPLET_M2_SNAPSHOT_INIT;qaplet_m2_diagnostic d=QAPLET_M2_DIAGNOSTIC_INIT;
      qaplet_work_snapshot w=QAPLET_WORK_SNAPSHOT_INIT;qaplet_process_snapshot c=QAPLET_PROCESS_SNAPSHOT_INIT;qaplet_timer_snapshot t=QAPLET_TIMER_SNAPSHOT_INIT;
      qaplet_io_snapshot io=QAPLET_IO_SNAPSHOT_INIT;qaplet_notify_snapshot n=QAPLET_NOTIFY_SNAPSHOT_INIT;qaplet_resource_snapshot r=QAPLET_RESOURCE_SNAPSHOT_INIT;
      if(qaplet_get_snapshot(p->r,&b,NULL) || qaplet_get_m2_snapshot(p->r,&m,NULL) || qaplet_get_m2_diagnostic(p->r,&d,NULL) || qaplet_get_work_snapshot(p->r,&w,NULL) || qaplet_get_process_snapshot(p->r,&c,NULL) || qaplet_get_timer_snapshot(p->r,&t,NULL) || qaplet_get_io_snapshot(p->r,&io,NULL) || qaplet_get_notify_snapshot(p->r,&n,NULL) || qaplet_get_resource_snapshot(p->r,&r,NULL) || b.native_current_bytes>b.native_peak_bytes || r.operations>r.operations_peak || r.helper_threads>r.helper_threads_peak || (phase && (b.lifecycle!=QAPLET_CLOSED || b.engine_current_bytes)))atomic_fetch_add(&p->failures,1);
      if(phase)++p->closed_samples;else ++p->open_samples;
    }
  }atomic_store(&p->done,1);return NULL;
}
static int pod(void){
  qaplet_config b=QAPLET_CONFIG_INIT;qaplet_m2_config m=QAPLET_M2_CONFIG_INIT;qaplet_runtime *r=NULL;CHECK(qaplet_create_m2(&b,&m,&r,NULL)==QAPLET_OK);
  struct {qaplet_resource_snapshot value;uint64_t tail;} extended={QAPLET_RESOURCE_SNAPSHOT_INIT,0};extended.value.struct_size=sizeof(extended);
  CHECK(qaplet_get_resource_snapshot(r,&extended.value,NULL)==QAPLET_OK && !extended.tail && extended.value.struct_size==sizeof(extended));
  extended.tail=1;unsigned char original[sizeof(extended)];memcpy(original,&extended,sizeof(extended));
  CHECK(qaplet_get_resource_snapshot(r,&extended.value,NULL)==QAPLET_ABI_MISMATCH && !memcmp(original,&extended,sizeof(extended)));
  extended.tail=0;extended.value.struct_size=8;memcpy(original,&extended,sizeof(extended));
  CHECK(qaplet_get_resource_snapshot(r,&extended.value,NULL)==QAPLET_ABI_MISMATCH && !memcmp(original,&extended,sizeof(extended)));
  qaplet_progress progress=QAPLET_PROGRESS_INIT;CHECK(qaplet_eval(r,"globalThis.queryMark=42",23,&progress,NULL)==QAPLET_OK);
  query_probe probe={.r=r};pthread_t thread;CHECK(!pthread_create(&thread,NULL,query_pods,&probe));while(!atomic_load(&probe.ready))sched_yield();
  qaplet_result result=QAPLET_RESULT_INIT;CHECK(qaplet_eval(r,"queryMark",9,&progress,NULL)==QAPLET_OK && qaplet_get_result(r,&result,NULL)==QAPLET_OK && result.number==42);
  CHECK(qaplet_close(r,NULL)==QAPLET_OK);atomic_store(&probe.closed,1);CHECK(!pthread_join(thread,NULL) && atomic_load(&probe.done) && !atomic_load(&probe.failures) && probe.open_samples==1000 && probe.closed_samples==1000);
  CHECK(qaplet_destroy(r,NULL)==QAPLET_OK && qaplet_create(&b,&r,NULL)==QAPLET_OK);
  qaplet_resource_snapshot resources=QAPLET_RESOURCE_SNAPSHOT_INIT;CHECK(qaplet_get_resource_snapshot(r,&resources,NULL)==QAPLET_UNSUPPORTED && qaplet_close(r,NULL)==QAPLET_OK && qaplet_destroy(r,NULL)==QAPLET_OK);
  puts("M2 aggregate POD: versioned short/zero-tail/nonzero-tail canaries; foreign thread 1000 OPEN and 1000 CLOSED samples of nine pure POD queries; JS state preserved; old instance unsupported: pass");return 0;
}
static int metrics(void) {
  const unsigned char payload[]={0,1,127,128,255,10};char path[4096];int fd=sdk_temporary_file(path,sizeof(path),"qaplet-aggregate"),saved=-1;int64_t native=0,saved_native=0;
  CHECK(fd>=0 && write(fd,payload,sizeof(payload))==sizeof(payload) && lseek(fd,0,SEEK_SET)==0);
  if(IsWindows()){char16_t name[4096];CHECK(__mkntpath(path,name)>=0);native=CreateFile(name,0xc0000000u,7,NULL,3,0x80,0);CHECK(native!=-1);saved_native=GetStdHandle(-10);CHECK(SetStdHandle(-10,native));}
  else{saved=dup(0);CHECK(saved>=0 && dup2(fd,0)==0);}
  qaplet_config b=QAPLET_CONFIG_INIT;qaplet_m2_config m=QAPLET_M2_CONFIG_INIT;qaplet_runtime *r=NULL;
  m.limits.worker_threads=1;m.limits.helper_threads=1;m.limits.operations=3;m.limits.operation_data_bytes=24;m.limits.work_input_bytes=4;m.limits.work_output_bytes=4;m.limits.io_block_bytes=8;
  CHECK(qaplet_create_m2(&b,&m,&r,NULL)==QAPLET_OK);
  qaplet_resource_snapshot resources=QAPLET_RESOURCE_SNAPSHOT_INIT;
  CHECK(qaplet_get_resource_snapshot(r,&resources,NULL)==QAPLET_OK && !resources.operations && !resources.helper_threads && !resources.io_endpoints);
  qaplet_io_endpoint endpoint=QAPLET_IO_ENDPOINT_INIT;qaplet_stdio_info info=QAPLET_STDIO_INFO_INIT;
  CHECK(qaplet_stdio_attach(r,0,&endpoint,&info,NULL)==QAPLET_OK && info.kind==QAPLET_STDIO_FILE);
  work_gate gate={PTHREAD_MUTEX_INITIALIZER,PTHREAD_COND_INITIALIZER,0,0};qaplet_work_handler handler=QAPLET_WORK_HANDLER_INIT;
  handler.name="gate";handler.name_length=4;handler.function=gated_work;handler.userdata=&gate;handler.input_bytes=4;handler.output_bytes=4;
  CHECK(qaplet_register_work_handler(r,&handler,NULL)==QAPLET_OK);
  qaplet_work_task first=QAPLET_WORK_TASK_INIT,queued=QAPLET_WORK_TASK_INIT,refused=QAPLET_WORK_TASK_INIT;
  CHECK(qaplet_work_submit(r,"gate",4,"copy",4,&first,NULL)==QAPLET_OK);
  pthread_mutex_lock(&gate.mutex);while(!gate.entered)pthread_cond_wait(&gate.condition,&gate.mutex);pthread_mutex_unlock(&gate.mutex);
  CHECK(qaplet_work_submit(r,"gate",4,"next",4,&queued,NULL)==QAPLET_OK);
  qaplet_io_operation io=QAPLET_IO_OPERATION_INIT;CHECK(qaplet_io_read(r,&endpoint,8,&io,NULL)==QAPLET_OK);
  CHECK(qaplet_get_resource_snapshot(r,&resources,NULL)==QAPLET_OK && resources.operations==3 && resources.operations_peak==3 && resources.operation_data_bytes==24 && resources.operation_data_bytes_peak==24 && resources.io_operations==1 && resources.io_operations_peak==1 && resources.io_data_bytes==8 && resources.io_data_bytes_peak==8 && resources.io_endpoints==1 && resources.io_endpoints_peak==1 && resources.helper_threads==1 && resources.helper_threads_peak==1 && resources.helper_stack_bytes==1048576 && resources.helper_stack_bytes_peak==1048576);
  qaplet_work_snapshot work=QAPLET_WORK_SNAPSHOT_INIT;CHECK(qaplet_get_work_snapshot(r,&work,NULL)==QAPLET_OK && work.running==1 && work.queued==1 && work.worker_threads==1 && work.worker_stack_bytes==1048576);
  CHECK(qaplet_work_submit(r,"gate",4,"nope",4,&refused,NULL)==QAPLET_RESOURCE_LIMIT && !refused.generation);
  CHECK(qaplet_work_cancel(r,&first,NULL)==QAPLET_OK && qaplet_work_release(r,&first,NULL)==QAPLET_OK);
  CHECK(qaplet_get_resource_snapshot(r,&resources,NULL)==QAPLET_OK && resources.operations==3 && resources.operation_data_bytes==24);
  CHECK(qaplet_work_submit(r,"gate",4,"nope",4,&refused,NULL)==QAPLET_RESOURCE_LIMIT && !refused.generation);
  CHECK(qaplet_work_cancel(r,&queued,NULL)==QAPLET_OK && qaplet_work_release(r,&queued,NULL)==QAPLET_OK);
  qaplet_drive_result drive=QAPLET_DRIVE_RESULT_INIT;qaplet_io_status ios=QAPLET_IO_STATUS_INIT;double end=milliseconds()+5000;
  do{CHECK(qaplet_run_once(r,10,&drive,NULL)==QAPLET_OK);CHECK(qaplet_get_io_status(r,&io,&ios,NULL)==QAPLET_OK);}while((ios.state==QAPLET_IO_PENDING || ios.helper_active) && milliseconds()<end);
  CHECK(ios.state==QAPLET_IO_SUCCEEDED && !ios.physical_pending && !ios.helper_active);
  CHECK(qaplet_get_resource_snapshot(r,&resources,NULL)==QAPLET_OK && resources.operations==2 && resources.operation_data_bytes==16 && !resources.helper_threads);
  unsigned char copied[8]={0};qaplet_io_result result=QAPLET_IO_RESULT_INIT;result.buffer=copied;result.capacity=sizeof(copied);
  CHECK(qaplet_io_copy_result(r,&io,&result,NULL)==QAPLET_OK && result.length==sizeof(payload) && !memcmp(copied,payload,sizeof(payload)));
  CHECK(qaplet_io_release(r,&io,NULL)==QAPLET_OK && qaplet_stdio_detach(r,0,NULL)==QAPLET_OK);
  CHECK(qaplet_work_submit(r,"gate",4,"last",4,&queued,NULL)==QAPLET_OK);
  pthread_mutex_lock(&gate.mutex);gate.release=1;pthread_cond_broadcast(&gate.condition);pthread_mutex_unlock(&gate.mutex);
  CHECK(qaplet_run(r,&drive,NULL)==QAPLET_OK && drive.reason==QAPLET_DRIVE_IDLE);
  qaplet_work_result wr=QAPLET_WORK_RESULT_INIT;wr.buffer=copied;wr.capacity=sizeof(copied);
  CHECK(qaplet_work_copy_result(r,&queued,&wr,NULL)==QAPLET_OK && wr.length==4 && !memcmp(copied,"last",4));
  CHECK(qaplet_get_resource_snapshot(r,&resources,NULL)==QAPLET_OK && resources.operations==1 && resources.operation_data_bytes==8);
  CHECK(qaplet_work_release(r,&queued,NULL)==QAPLET_OK);
  CHECK(qaplet_close(r,NULL)==QAPLET_OK && qaplet_quiesce(r,5000,&drive,NULL)==QAPLET_OK && drive.reason==QAPLET_DRIVE_QUIESCENT);
  CHECK(qaplet_get_resource_snapshot(r,&resources,NULL)==QAPLET_OK && !resources.operations && !resources.operation_data_bytes && !resources.io_endpoints && !resources.io_operations && !resources.io_data_bytes && !resources.helper_threads && !resources.helper_stack_bytes && resources.operations_peak==3 && resources.io_endpoints_peak==1 && resources.helper_threads_peak==1);
  CHECK(qaplet_destroy(r,NULL)==QAPLET_OK);
  if(IsWindows())CHECK(SetStdHandle(-10,saved_native) && CloseHandle(native));else CHECK(fcntl(0,F_GETFD)>=0 && dup2(saved,0)==0 && !close(saved));
  CHECK(!close(fd) && !unlink(path) && !pthread_cond_destroy(&gate.condition) && !pthread_mutex_destroy(&gate.mutex));
  puts("M2 aggregate metrics: mixed Work/file IO share small count/data; cancelled physical and C results retain capacity; real helper/worker requested stacks and persistent peaks; cleanup and Host handle: pass");return 0;
}
static void never_timer(qaplet_runtime *r,const qaplet_timer *timer,void *opaque){(void)r;(void)timer;unsigned *calls=opaque;++*calls;}
static int counts(const char *self,const char *launcher){
  char path[4096];int fd=sdk_temporary_file(path,sizeof(path),"qaplet-counts"),saved=-1;int64_t native=0,saved_native=0;CHECK(fd>=0 && write(fd,"A",1)==1 && lseek(fd,0,SEEK_SET)==0);
  if(IsWindows()){char16_t name[4096];CHECK(__mkntpath(path,name)>=0);native=CreateFile(name,0xc0000000u,7,NULL,3,0x80,0);CHECK(native!=-1);saved_native=GetStdHandle(-10);CHECK(SetStdHandle(-10,native));}else{saved=dup(0);CHECK(saved>=0 && dup2(fd,0)==0);}
  qaplet_config b=QAPLET_CONFIG_INIT;qaplet_m2_config m=QAPLET_M2_CONFIG_INIT;qaplet_runtime *r=NULL;
  m.limits.worker_threads=1;m.limits.helper_threads=1;m.limits.work_handlers=1;m.limits.timers=1;m.limits.processes=1;m.limits.keepalive_tokens=1;m.limits.reactor_registrations=6;m.limits.native_handles=12;
  m.limits.operations=4;m.limits.operation_data_bytes=32;m.limits.work_input_bytes=4;m.limits.work_output_bytes=4;m.limits.io_block_bytes=8;
  CHECK(qaplet_create_m2(&b,&m,&r,NULL)==QAPLET_OK);
  qaplet_process_snapshot ps=QAPLET_PROCESS_SNAPSHOT_INIT;CHECK(qaplet_get_process_snapshot(r,&ps,NULL)==QAPLET_OK && ps.environment_bytes>0);
  qaplet_m2_snapshot before=QAPLET_M2_SNAPSHOT_INIT;CHECK(qaplet_get_m2_snapshot(r,&before,NULL)==QAPLET_OK);
  work_gate gate={PTHREAD_MUTEX_INITIALIZER,PTHREAD_COND_INITIALIZER,0,0};qaplet_work_handler h=QAPLET_WORK_HANDLER_INIT;h.name="gate";h.name_length=4;h.function=gated_work;h.userdata=&gate;h.input_bytes=4;h.output_bytes=4;
  CHECK(qaplet_register_work_handler(r,&h,NULL)==QAPLET_OK);h.name="other";h.name_length=5;CHECK(qaplet_register_work_handler(r,&h,NULL)==QAPLET_RESOURCE_LIMIT);
  unsigned timer_calls=0;qaplet_timer timer=QAPLET_TIMER_INIT,no_timer=QAPLET_TIMER_INIT;CHECK(qaplet_timer_create(r,60000,never_timer,&timer_calls,&timer,NULL)==QAPLET_OK && qaplet_immediate_create(r,never_timer,&timer_calls,&no_timer,NULL)==QAPLET_RESOURCE_LIMIT && !no_timer.generation);
  qaplet_keepalive token=QAPLET_KEEPALIVE_INIT,no_token=QAPLET_KEEPALIVE_INIT;CHECK(qaplet_keepalive_create(r,&token,NULL)==QAPLET_OK && qaplet_keepalive_create(r,&no_token,NULL)==QAPLET_RESOURCE_LIMIT && !no_token.generation);
  qaplet_io_endpoint file=QAPLET_IO_ENDPOINT_INIT;qaplet_stdio_info info=QAPLET_STDIO_INFO_INIT;CHECK(qaplet_stdio_attach(r,0,&file,&info,NULL)==QAPLET_OK);
  qaplet_process_options options=QAPLET_PROCESS_OPTIONS_INIT;options.path=(qaplet_string){launcher,strlen(launcher)};
  qaplet_string args[4]={{launcher,strlen(launcher)},{self,strlen(self)},{"--m2-process-child",18},{"--hang",6}};
  if(IsWindows()){args[1]=args[3];options.argc=2;}else options.argc=4;options.argv=args;options.stdio[0]=QAPLET_PROCESS_PIPE;options.stdio[1]=options.stdio[2]=QAPLET_PROCESS_IGNORE;
  qaplet_process process=QAPLET_PROCESS_INIT,refused=QAPLET_PROCESS_INIT;qaplet_process_status status=QAPLET_PROCESS_STATUS_INIT;CHECK(qaplet_process_spawn(r,&options,&process,&status,NULL)==QAPLET_OK);
  CHECK(qaplet_process_spawn(r,&options,&refused,&status,NULL)==QAPLET_RESOURCE_LIMIT && !refused.generation);
  qaplet_work_task task=QAPLET_WORK_TASK_INIT;CHECK(qaplet_work_submit(r,"gate",4,"copy",4,&task,NULL)==QAPLET_OK);
  pthread_mutex_lock(&gate.mutex);while(!gate.entered)pthread_cond_wait(&gate.condition,&gate.mutex);pthread_mutex_unlock(&gate.mutex);
  qaplet_io_operation io=QAPLET_IO_OPERATION_INIT;CHECK(qaplet_io_read(r,&file,8,&io,NULL)==QAPLET_RESOURCE_LIMIT && !io.generation);
  qaplet_resource_snapshot resources=QAPLET_RESOURCE_SNAPSHOT_INIT;CHECK(qaplet_get_resource_snapshot(r,&resources,NULL)==QAPLET_OK && resources.operations==1 && resources.helper_threads==1 && resources.io_endpoints==2 && resources.helper_stack_bytes==1048576);
  qaplet_m2_snapshot live=QAPLET_M2_SNAPSHOT_INIT;CHECK(qaplet_get_m2_snapshot(r,&live,NULL)==QAPLET_OK && live.lifecycle==QAPLET_OPEN && !live.termination_reason && live.keepalive_tokens==1 && live.keepalive_tokens_peak==1 && live.native_handles>before.native_handles && live.native_handles<=12 && live.reactor_registrations==before.reactor_registrations+3 && live.native_current_bytes>=before.native_current_bytes+8);
  qaplet_timer_snapshot timers=QAPLET_TIMER_SNAPSHOT_INIT;CHECK(qaplet_get_timer_snapshot(r,&timers,NULL)==QAPLET_OK && timers.current==1 && timers.peak==1);
  qaplet_work_snapshot ws=QAPLET_WORK_SNAPSHOT_INIT;CHECK(qaplet_get_work_snapshot(r,&ws,NULL)==QAPLET_OK && ws.handlers==1 && ws.handlers_peak==1 && ws.running==1 && ws.worker_threads==1);
  CHECK(qaplet_get_process_snapshot(r,&ps,NULL)==QAPLET_OK && ps.processes==1 && ps.processes_peak==1 && ps.accepted==1 && ps.helper_threads==1);
  qaplet_io_endpoint input=QAPLET_IO_ENDPOINT_INIT;CHECK(qaplet_process_get_stdio(r,&process,0,&input,NULL)==QAPLET_OK && qaplet_process_cancel(r,&process,NULL)==QAPLET_OK && qaplet_io_close(r,&input,NULL)==QAPLET_OK);
  qaplet_drive_result drive=QAPLET_DRIVE_RESULT_INIT;double end=milliseconds()+5000;
  do{CHECK(qaplet_run_once(r,10,&drive,NULL)==QAPLET_OK && qaplet_get_process_status(r,&process,&status,NULL)==QAPLET_OK);}while((!status.reaped || status.helper_active) && milliseconds()<end);
  CHECK(status.reaped && !status.helper_active && status.code==QAPLET_CANCELLED && qaplet_process_release(r,&process,NULL)==QAPLET_OK);
  CHECK(qaplet_io_read(r,&file,8,&io,NULL)==QAPLET_OK);
  CHECK(qaplet_get_resource_snapshot(r,&resources,NULL)==QAPLET_OK && resources.helper_threads==1 && resources.helper_threads_peak==1 && resources.operations==2 && resources.operation_data_bytes==16);
  CHECK(qaplet_timer_cancel(r,&timer,NULL)==QAPLET_OK && qaplet_keepalive_release(r,&token,NULL)==QAPLET_OK);
  pthread_mutex_lock(&gate.mutex);gate.release=1;pthread_cond_broadcast(&gate.condition);pthread_mutex_unlock(&gate.mutex);
  CHECK(qaplet_run(r,&drive,NULL)==QAPLET_OK && qaplet_close(r,NULL)==QAPLET_OK && qaplet_quiesce(r,5000,&drive,NULL)==QAPLET_OK && drive.reason==QAPLET_DRIVE_QUIESCENT);
  CHECK(qaplet_get_resource_snapshot(r,&resources,NULL)==QAPLET_OK && !resources.operations && !resources.helper_threads && !resources.io_endpoints && resources.io_endpoints_peak==2 && resources.helper_threads_peak==1);
  CHECK(qaplet_get_m2_snapshot(r,&live,NULL)==QAPLET_OK && !live.native_handles && !live.reactor_registrations && live.native_handles_peak>before.native_handles && live.native_current_bytes>0 && live.native_current_bytes<=live.native_peak_bytes && live.native_peak_bytes<=live.limits.native_storage_bytes && !timer_calls && qaplet_destroy(r,NULL)==QAPLET_OK);
  if(IsWindows())CHECK(SetStdHandle(-10,saved_native) && CloseHandle(native));else CHECK(fcntl(0,F_GETFD)>=0 && dup2(saved,0)==0 && !close(saved));CHECK(!close(fd) && !unlink(path) && !pthread_cond_destroy(&gate.condition) && !pthread_mutex_destroy(&gate.mutex));
  puts("M2 aggregate counts: timer/token/handler/process small limits coexist with Work and stdio; helper shared across real process and file IO; metadata/environment, handles/registrations and retained control charged; atomic refusals recover after actual reap/join: pass");return 0;
}
static int native_limits(void){
  char path[4096];int fd=sdk_temporary_file(path,sizeof(path),"qaplet-data"),saved=-1;int64_t native=0,saved_native=0;CHECK(fd>=0 && write(fd,"A",1)==1 && lseek(fd,0,SEEK_SET)==0);
  if(IsWindows()){char16_t name[4096];CHECK(__mkntpath(path,name)>=0);native=CreateFile(name,0xc0000000u,7,NULL,3,0x80,0);CHECK(native!=-1);saved_native=GetStdHandle(-10);CHECK(SetStdHandle(-10,native));}else{saved=dup(0);CHECK(saved>=0 && dup2(fd,0)==0);}
  unsigned char *input=malloc(1048576);CHECK(input);memset(input,0x5a,1048576);
  for(unsigned total_first=0;total_first<2;++total_first){
    qaplet_config b=QAPLET_CONFIG_INIT;qaplet_m2_config m=QAPLET_M2_CONFIG_INIT;qaplet_runtime *r=NULL;
    if(total_first){b.limits.native_storage_bytes=10485760;b.limits.event_queue_slots=64;b.limits.event_payload_bytes=65536;}
    CHECK(qaplet_create_m2(&b,&m,&r,NULL)==QAPLET_OK);
    work_gate gate={PTHREAD_MUTEX_INITIALIZER,PTHREAD_COND_INITIALIZER,0,0};qaplet_work_handler h=QAPLET_WORK_HANDLER_INIT;h.name="large";h.name_length=5;h.function=gated_work;h.userdata=&gate;CHECK(qaplet_register_work_handler(r,&h,NULL)==QAPLET_OK);
    qaplet_io_endpoint e=QAPLET_IO_ENDPOINT_INIT;qaplet_stdio_info info=QAPLET_STDIO_INFO_INIT;CHECK(qaplet_stdio_attach(r,0,&e,&info,NULL)==QAPLET_OK);
    qaplet_io_operation io=QAPLET_IO_OPERATION_INIT;CHECK(qaplet_io_read(r,&e,65536,&io,NULL)==QAPLET_OK);
    qaplet_work_task tasks[4]={{0}};unsigned accepted=total_first ? 2 : 3;
    for(unsigned i=0;i<accepted;++i)CHECK(qaplet_work_submit(r,"large",5,input,1048576,&tasks[i],NULL)==QAPLET_OK);
    pthread_mutex_lock(&gate.mutex);while(!gate.entered)pthread_cond_wait(&gate.condition,&gate.mutex);pthread_mutex_unlock(&gate.mutex);
    CHECK(qaplet_work_submit(r,"large",5,input,1048576,&tasks[accepted],NULL)==QAPLET_RESOURCE_LIMIT && !tasks[accepted].generation);
    qaplet_resource_snapshot resources=QAPLET_RESOURCE_SNAPSHOT_INIT;qaplet_m2_snapshot live=QAPLET_M2_SNAPSHOT_INIT;
    CHECK(qaplet_get_resource_snapshot(r,&resources,NULL)==QAPLET_OK && resources.operations==accepted+1 && resources.operation_data_bytes==(uint64_t)accepted*2097152+65536 && resources.operation_data_bytes<8388608);
    CHECK(qaplet_get_m2_snapshot(r,&live,NULL)==QAPLET_OK && live.lifecycle==QAPLET_OPEN && !live.termination_reason && live.m2_limits.operation_data_bytes==8388608 && live.native_current_bytes>=resources.operation_data_bytes && live.native_current_bytes<=live.limits.native_storage_bytes);
    if(total_first)CHECK(live.limits.native_storage_bytes==10485760 && live.native_current_bytes>=resources.operation_data_bytes+4194304);else CHECK(live.limits.native_storage_bytes==16777216 && resources.operation_data_bytes+2097152>8388608);
    pthread_mutex_lock(&gate.mutex);gate.release=1;pthread_cond_broadcast(&gate.condition);pthread_mutex_unlock(&gate.mutex);
    qaplet_drive_result drive=QAPLET_DRIVE_RESULT_INIT;CHECK(qaplet_run(r,&drive,NULL)==QAPLET_OK && qaplet_close(r,NULL)==QAPLET_OK && qaplet_quiesce(r,5000,&drive,NULL)==QAPLET_OK && drive.reason==QAPLET_DRIVE_QUIESCENT && qaplet_destroy(r,NULL)==QAPLET_OK);
    CHECK(!pthread_cond_destroy(&gate.condition) && !pthread_mutex_destroy(&gate.mutex));
  }
  free(input);if(IsWindows())CHECK(SetStdHandle(-10,saved_native) && CloseHandle(native));else CHECK(dup2(saved,0)==0 && !close(saved));CHECK(!close(fd) && !unlink(path));
  puts("M2 aggregate native budget: mixed retained Work plus file IO hit 8MiB data subquota inside 16MiB; separate 10MiB total includes real 4MiB event store and metadata/environment before data quota; ordinary atomic refusal and cleanup: pass");return 0;
}
int sdk_aggregate(int argc,char **argv) {
  if(argc<2)return 2;const char *mode=argv[1];
  if(!strcmp(mode,"--m2-process-child")){char byte;while(read(0,&byte,1)>0){}return 37;}
  if(!strcmp(mode,"--m2-aggregate-diagnostics"))return diagnostics();
  if(!strcmp(mode,"--m2-aggregate-metrics"))return metrics();
  if(!strcmp(mode,"--m2-aggregate-pod"))return pod();
  if(!strcmp(mode,"--m2-aggregate-native"))return native_limits();
  if(argc==4 && !strcmp(mode,"--m2-aggregate-counts")){
    char launcher[PATH_MAX];if(IsWindows()){CHECK(strlen(argv[2])+24<sizeof(launcher));strcpy(launcher,argv[2]);char *slash=strrchr(launcher,'/');if(!slash)slash=strrchr(launcher,'\\');CHECK(slash);strcpy(slash+1,"sdk/process-child.exe");return counts(argv[2],launcher);}return counts(argv[2],argv[3]);
  }
  return 2;
}
#ifdef QAPLET_AGGREGATE_STANDALONE
int main(int argc,char **argv){return sdk_aggregate(argc,argv);}
#endif
