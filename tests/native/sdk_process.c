/* SPDX-License-Identifier: MIT */
/* Public SDK + real OS boundary; never inspect private Runtime/engine data. */
#define _COSMO_SOURCE 1
#include "qaplet.h"
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <pthread.h>
#include <sched.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <time.h>
#include <unistd.h>
#include "libc/dce.h"
#include "libc/nt/runtime.h"
#include "libc/str/str.h"
#include "libc/nt/struct/byhandlefileinformation.h"
extern int64_t CreateFile(const char16_t *,uint32_t,uint32_t,void *,uint32_t,uint32_t,int64_t);
extern bool32 GetFileInformationByHandle(int64_t,struct NtByHandleFileInformation *);
#define CHECK(c) do { if (!(c)) { fprintf(stderr,"process contract line %d: %s errno=%d\n",__LINE__,#c,errno); return 1; } } while (0)
static const char *self,*launcher;
static char executable[PATH_MAX],root[PATH_MAX],cwd[PATH_MAX],native_cwd[PATH_MAX];
static const char *special[]={"","参数 空格","two words","quote\"double","slash\\tail\\","literal&|;$()<>*","apostrophe'"};
static qaplet_string arguments[16];
static double milliseconds(void) { struct timespec ts; clock_gettime(CLOCK_MONOTONIC,&ts); return ts.tv_sec*1000.0+ts.tv_nsec/1000000.0; }
static void spawn_failure(qaplet_code code,const qaplet_process_status *status) {
  if (code!=QAPLET_OK) {
    int saved_errno=errno;struct rlimit limits={0};int result=getrlimit(RLIMIT_NOFILE,&limits),limit_error=result ? errno : 0;
    fprintf(stderr,"process spawn failure: return_code=%d status_code=%d error_domain=%u native_error=%d helper_active=%u nofile_result=%d nofile_error=%d nofile_soft=%llu nofile_hard=%llu int_max=%d\n",
      code,status->code,status->error_domain,status->error,status->helper_active,result,limit_error,
      (unsigned long long)limits.rlim_cur,(unsigned long long)limits.rlim_max,INT_MAX);
    errno=saved_errno;
  }
}
static qaplet_code spawn_ok(qaplet_runtime *r,const qaplet_process_options *options,qaplet_process *p,qaplet_process_status *status,qaplet_diagnostic *diagnostic) {
  qaplet_code code=qaplet_process_spawn(r,options,p,status,diagnostic);
  spawn_failure(code,status);
  return code;
}
static void status_failure(const char *stage,qaplet_code code,const qaplet_process_status *s,const qaplet_drive_result *drive) {
  fprintf(stderr,"process status failure: stage=%s return_code=%d status_code=%d error_domain=%u native_error=%d exited=%u reaped=%u delivered=%u referenced=%u helper_active=%u exit_code=%u signal=%u raw_wait_status=%d drive_reason=%u drive_code=%d progressed=%u\n",
    stage,code,s->code,s->error_domain,s->error,s->exited,s->reaped,s->delivered,s->referenced,s->helper_active,s->native_exit_code,s->native_signal,s->raw_wait_status,
    drive ? drive->reason : 0,drive ? drive->code : 0,drive ? drive->progressed : 0);
}
static int copy_file(const char *from,const char *to) {
  int in=open(from,O_RDONLY|O_CLOEXEC),out=open(to,O_WRONLY|O_CREAT|O_TRUNC|O_CLOEXEC,0700);
  CHECK(in>=0 && out>=0); char data[16384]; ssize_t n;
  while ((n=read(in,data,sizeof(data)))>0) CHECK(write(out,data,n)==n);
  CHECK(n==0 && !close(in) && !close(out)); return 0;
}
static void prepare(qaplet_process_options *options,const char *mode) {
  *options=(qaplet_process_options)QAPLET_PROCESS_OPTIONS_INIT;
  options->path=(qaplet_string){executable,strlen(executable)};
  for (int i=0;i<3;++i) options->stdio[i]=QAPLET_PROCESS_IGNORE;
  size_t n=0;
  arguments[n++]=(qaplet_string){executable,strlen(executable)};
  if (!IsWindows()) { arguments[n++]=(qaplet_string){self,strlen(self)}; arguments[n++]=(qaplet_string){"--m2-process-child",18}; }
  arguments[n++]=(qaplet_string){mode,strlen(mode)};
  for (size_t i=0;i<7;++i) arguments[n++]=(qaplet_string){special[i],strlen(special[i])};
  options->argv=arguments; options->argc=n;
}
static int finish(qaplet_runtime *runtime) {
  qaplet_drive_result drive=QAPLET_DRIVE_RESULT_INIT;
  CHECK(qaplet_close(runtime,NULL)==QAPLET_OK);
  CHECK(qaplet_quiesce(runtime,5000,&drive,NULL)==QAPLET_OK && drive.reason==QAPLET_DRIVE_QUIESCENT);
  qaplet_process_snapshot p=QAPLET_PROCESS_SNAPSHOT_INIT; qaplet_m2_snapshot m=QAPLET_M2_SNAPSHOT_INIT;
  CHECK(qaplet_get_process_snapshot(runtime,&p,NULL)==QAPLET_OK && !p.processes && !p.helper_threads && !p.helper_stack_bytes && !p.environment_bytes);
  CHECK(qaplet_get_m2_snapshot(runtime,&m,NULL)==QAPLET_OK && !m.native_handles && !m.reactor_registrations && !m.native_uncertain_handles && !m.engine_current_bytes);
  CHECK(qaplet_destroy(runtime,NULL)==QAPLET_OK); return 0;
}
static int pipe_defaults(void) {
  qaplet_config b=QAPLET_CONFIG_INIT; qaplet_m2_config m=QAPLET_M2_CONFIG_INIT;
  qaplet_runtime *r=NULL; CHECK(qaplet_create_m2(&b,&m,&r,NULL)==QAPLET_OK);
  qaplet_process_options o; prepare(&o,"--hang");
  for (int i=0;i<3;++i) o.stdio[i]=QAPLET_PROCESS_PIPE;
  qaplet_process p=QAPLET_PROCESS_INIT; qaplet_process_status s=QAPLET_PROCESS_STATUS_INIT;
  CHECK(spawn_ok(r,&o,&p,&s,NULL)==QAPLET_OK);
  CHECK(!finish(r));
  puts("M2 process pipe defaults: three owned pipes; direct child; close/quiesce cleanup: pass");return 0;
}
static int io_result(qaplet_runtime *r,qaplet_io_operation *o,qaplet_io_result *result) {
  double until=milliseconds()+5000;qaplet_io_status status=QAPLET_IO_STATUS_INIT;
  do {
    CHECK(qaplet_get_io_status(r,o,&status,NULL)==QAPLET_OK);
    if(status.state!=QAPLET_IO_PENDING && !status.physical_pending && !status.pending_handoff)break;
    qaplet_drive_result drive=QAPLET_DRIVE_RESULT_INIT;
    CHECK(qaplet_run_once(r,1000,&drive,NULL)==QAPLET_OK);
  }while(milliseconds()<until);
  if(status.state==QAPLET_IO_PENDING || status.physical_pending || status.pending_handoff)
    fprintf(stderr,"I/O stalled state=%u physical=%u handoff=%u helper=%u bytes=%llu code=%u error=%d\n",status.state,status.physical_pending,status.pending_handoff,status.helper_active,(unsigned long long)status.length,status.code,status.error);
  CHECK(status.state!=QAPLET_IO_PENDING && !status.physical_pending && !status.pending_handoff);
  CHECK(qaplet_io_copy_result(r,o,result,NULL)==QAPLET_OK);return 0;
}
static int pipe_bytes(void) {
  qaplet_config b=QAPLET_CONFIG_INIT;qaplet_m2_config m=QAPLET_M2_CONFIG_INIT;qaplet_runtime *r=NULL;
  CHECK(qaplet_create_m2(&b,&m,&r,NULL)==QAPLET_OK);
  qaplet_process_options options;prepare(&options,"--pipe");for(int i=0;i<3;++i)options.stdio[i]=QAPLET_PROCESS_PIPE;
  qaplet_process p=QAPLET_PROCESS_INIT;qaplet_process_status process=QAPLET_PROCESS_STATUS_INIT;
  CHECK(spawn_ok(r,&options,&p,&process,NULL)==QAPLET_OK);
  qaplet_io_endpoint endpoints[3]={QAPLET_IO_ENDPOINT_INIT,QAPLET_IO_ENDPOINT_INIT,QAPLET_IO_ENDPOINT_INIT};
  for(int i=0;i<3;++i)CHECK(qaplet_process_get_stdio(r,&p,i,&endpoints[i],NULL)==QAPLET_OK);
  qaplet_io_operation out=QAPLET_IO_OPERATION_INIT,err=QAPLET_IO_OPERATION_INIT,in=QAPLET_IO_OPERATION_INIT,rejected=QAPLET_IO_OPERATION_INIT;
  CHECK(qaplet_io_read(r,&endpoints[1],64,&out,NULL)==QAPLET_OK);
  CHECK(qaplet_io_read(r,&endpoints[1],1,&rejected,NULL)==QAPLET_BUSY && !rejected.runtime_id);
  qaplet_io_status pending=QAPLET_IO_STATUS_INIT;CHECK(qaplet_get_io_status(r,&out,&pending,NULL)==QAPLET_OK && pending.state==QAPLET_IO_PENDING && !pending.eof);
  CHECK(qaplet_io_read(r,&endpoints[2],64,&err,NULL)==QAPLET_OK);
  unsigned char input[]={'I',0,255,'N','\n'},output[64],errors[64];
  CHECK(qaplet_io_write(r,&endpoints[0],input,sizeof(input),&in,NULL)==QAPLET_OK);memset(input,'?',sizeof(input));
  CHECK(qaplet_io_write(r,&endpoints[0],"X",1,&rejected,NULL)==QAPLET_BUSY);
  CHECK(qaplet_io_finish(r,&endpoints[0],NULL)==QAPLET_OK);
  qaplet_io_result result=QAPLET_IO_RESULT_INIT;CHECK(!io_result(r,&in,&result) && !result.code && result.length==5 && !result.eof);
  CHECK(qaplet_io_release(r,&in,NULL)==QAPLET_OK && qaplet_get_io_status(r,&in,&pending,NULL)==QAPLET_INVALID_HANDLE);
  result=(qaplet_io_result)QAPLET_IO_RESULT_INIT;result.buffer=output;result.capacity=sizeof(output);
  CHECK(!io_result(r,&out,&result) && !result.code && result.length==5 && !result.eof && !memcmp(output,"I\0\xffN\n",5));
  unsigned char short_bytes[4]={0xaa,0xaa,0xaa,0xaa};qaplet_io_result short_copy=QAPLET_IO_RESULT_INIT;short_copy.buffer=short_bytes;short_copy.capacity=sizeof(short_bytes);
  CHECK(qaplet_io_copy_result(r,&out,&short_copy,NULL)==QAPLET_BUFFER_TOO_SMALL && short_copy.length==5);
  for(unsigned i=0;i<sizeof(short_bytes);++i)CHECK(short_bytes[i]==0xaa);
  CHECK(qaplet_io_copy_result(r,&out,&result,NULL)==QAPLET_OK && !memcmp(output,"I\0\xffN\n",5));
  CHECK(qaplet_io_release(r,&out,NULL)==QAPLET_OK);
  result=(qaplet_io_result)QAPLET_IO_RESULT_INIT;result.buffer=errors;result.capacity=sizeof(errors);
  CHECK(!io_result(r,&err,&result) && !result.code && result.length==4 && !result.eof && !memcmp(errors,"E\0\xfeR",4));
  CHECK(qaplet_io_release(r,&err,NULL)==QAPLET_OK);
  double until=milliseconds()+5000;
  do{CHECK(qaplet_get_process_status(r,&p,&process,NULL)==QAPLET_OK);if(process.reaped)break;sched_yield();}while(milliseconds()<until);
  CHECK(process.reaped && process.native_exit_code==37);
  qaplet_progress progress=QAPLET_PROGRESS_INIT;CHECK(qaplet_pump(r,&progress,NULL)==QAPLET_OK);
  CHECK(qaplet_get_process_status(r,&p,&process,NULL)==QAPLET_OK && process.delivered && process.referenced);
  CHECK(qaplet_process_release(r,&p,NULL)==QAPLET_BUSY);
  CHECK(qaplet_io_read(r,&endpoints[1],64,&out,NULL)==QAPLET_OK && qaplet_io_read(r,&endpoints[2],64,&err,NULL)==QAPLET_OK);
  result=(qaplet_io_result)QAPLET_IO_RESULT_INIT;result.buffer=output;result.capacity=sizeof(output);
  CHECK(!io_result(r,&out,&result) && !result.code && result.eof && !result.length);
  CHECK(qaplet_io_release(r,&out,NULL)==QAPLET_OK);
  result=(qaplet_io_result)QAPLET_IO_RESULT_INIT;result.buffer=errors;result.capacity=sizeof(errors);
  CHECK(!io_result(r,&err,&result) && !result.code && result.eof && !result.length);
  CHECK(qaplet_io_release(r,&err,NULL)==QAPLET_OK);
  qaplet_drive_result drive=QAPLET_DRIVE_RESULT_INIT;CHECK(qaplet_run(r,&drive,NULL)==QAPLET_OK && drive.reason==QAPLET_DRIVE_IDLE);
  CHECK(qaplet_process_release(r,&p,NULL)==QAPLET_OK);
  CHECK(!finish(r));
  puts("M2 process pipe bytes: copied binary stdin; independent short stdout/stderr; BUSY; temporary unavailability; finish; exit before EOF retains ref; drain and release: pass");return 0;
}
static volatile sig_atomic_t pipe_signals;
static void pipe_signal(int number){if(number==SIGPIPE)++pipe_signals;}
static int pipe_write_failure(void) {
  for(int blocked=0;blocked<(IsWindows() ? 1 : 2);++blocked){
  qaplet_config b=QAPLET_CONFIG_INIT;qaplet_m2_config m=QAPLET_M2_CONFIG_INIT;qaplet_runtime *r=NULL;
  CHECK(qaplet_create_m2(&b,&m,&r,NULL)==QAPLET_OK);
  qaplet_process_options options;prepare(&options,"--closed-input");for(int i=0;i<3;++i)options.stdio[i]=QAPLET_PROCESS_PIPE;
  qaplet_process p=QAPLET_PROCESS_INIT;qaplet_process_status s=QAPLET_PROCESS_STATUS_INIT;
  CHECK(spawn_ok(r,&options,&p,&s,NULL)==QAPLET_OK);
  qaplet_io_endpoint ep[3]={QAPLET_IO_ENDPOINT_INIT,QAPLET_IO_ENDPOINT_INIT,QAPLET_IO_ENDPOINT_INIT};
  for(int i=0;i<3;++i)CHECK(qaplet_process_get_stdio(r,&p,i,&ep[i],NULL)==QAPLET_OK);
  qaplet_io_operation ready=QAPLET_IO_OPERATION_INIT,write=QAPLET_IO_OPERATION_INIT;
  CHECK(qaplet_io_read(r,&ep[1],1,&ready,NULL)==QAPLET_OK);unsigned char byte=0;
  qaplet_io_result result=QAPLET_IO_RESULT_INIT;result.buffer=&byte;result.capacity=1;
  CHECK(!io_result(r,&ready,&result) && result.length==1 && byte=='R');CHECK(qaplet_io_release(r,&ready,NULL)==QAPLET_OK);
  void (*previous)(int)=SIG_DFL;if(!IsWindows()){pipe_signals=0;previous=signal(SIGPIPE,pipe_signal);CHECK(previous!=SIG_ERR);}
  sigset_t original_mask,expected_mask,after_mask,pending_mask,pipe_mask;sigemptyset(&pipe_mask);sigaddset(&pipe_mask,SIGPIPE);
  if(!IsWindows()){
    CHECK(!pthread_sigmask(SIG_SETMASK,NULL,&original_mask));
    if(blocked){CHECK(!pthread_sigmask(SIG_BLOCK,&pipe_mask,NULL) && !raise(SIGPIPE));CHECK(!sigpending(&pending_mask) && sigismember(&pending_mask,SIGPIPE));}
    CHECK(!pthread_sigmask(SIG_SETMASK,NULL,&expected_mask));
  }
  unsigned char bytes[65536];memset(bytes,0x5a,sizeof(bytes));
  CHECK(qaplet_io_write(r,&ep[0],bytes,sizeof(bytes),&write,NULL)==QAPLET_OK);memset(bytes,0,sizeof(bytes));
  result=(qaplet_io_result)QAPLET_IO_RESULT_INIT;
  CHECK(!io_result(r,&write,&result));
  CHECK(result.state==QAPLET_IO_FAILED && result.code==QAPLET_IO_ERROR && result.error>0 && !result.length);
  CHECK(result.error_domain==(IsWindows() ? QAPLET_ERROR_DOMAIN_WIN32 : QAPLET_ERROR_DOMAIN_ERRNO));
  if(!IsWindows()){
    struct sigaction current;CHECK(!sigaction(SIGPIPE,NULL,&current) && current.sa_handler==pipe_signal && !pipe_signals);
    CHECK(!pthread_sigmask(SIG_SETMASK,NULL,&after_mask));
    for(int signal=1;signal<NSIG;++signal)CHECK(sigismember(&expected_mask,signal)==sigismember(&after_mask,signal));
    if(blocked){int got=0;CHECK(!sigpending(&pending_mask) && sigismember(&pending_mask,SIGPIPE) && !sigwait(&pipe_mask,&got) && got==SIGPIPE);}
    CHECK(!pthread_sigmask(SIG_SETMASK,&original_mask,NULL) && signal(SIGPIPE,previous)!=SIG_ERR);
  }
  qaplet_snapshot snapshot=QAPLET_SNAPSHOT_INIT;CHECK(qaplet_get_snapshot(r,&snapshot,NULL)==QAPLET_OK && snapshot.lifecycle==QAPLET_OPEN && !snapshot.termination_reason);
  CHECK(qaplet_io_release(r,&write,NULL)==QAPLET_OK);
  for(int i=0;i<3;++i)CHECK(qaplet_io_close(r,&ep[i],NULL)==QAPLET_OK);
  qaplet_drive_result drive=QAPLET_DRIVE_RESULT_INIT;CHECK(qaplet_run(r,&drive,NULL)==QAPLET_OK && drive.reason==QAPLET_DRIVE_IDLE);
  CHECK(qaplet_get_process_status(r,&p,&s,NULL)==QAPLET_OK && s.reaped && s.native_exit_code==37);
  CHECK(qaplet_process_release(r,&p,NULL)==QAPLET_OK && !finish(r));
  }
  puts("M2 process pipe write failure: child closes stdin before ready; zero accepted bytes; Host SIGPIPE handler/mask/pending preserved; ordinary native error keeps Runtime OPEN; cleanup: pass");return 0;
}
typedef struct {
  qaplet_runtime *runtime;qaplet_io_endpoint endpoint;qaplet_io_operation operation;
  _Atomic int error,notifications;
} io_observer;
static void io_notify(void *opaque){
  io_observer *o=opaque;atomic_fetch_add(&o->notifications,1);
  qaplet_io_status status=QAPLET_IO_STATUS_INIT;qaplet_io_result result=QAPLET_IO_RESULT_INIT;qaplet_io_snapshot snapshot=QAPLET_IO_SNAPSHOT_INIT;
  qaplet_io_operation token=QAPLET_IO_OPERATION_INIT;
  if(qaplet_get_io_status(o->runtime,&o->operation,&status,NULL)!=QAPLET_BUSY || qaplet_io_cancel(o->runtime,&o->operation,NULL)!=QAPLET_BUSY ||
     qaplet_io_read(o->runtime,&o->endpoint,1,&token,NULL)!=QAPLET_BUSY || qaplet_io_finish(o->runtime,&o->endpoint,NULL)!=QAPLET_BUSY ||
     qaplet_io_close(o->runtime,&o->endpoint,NULL)!=QAPLET_BUSY || qaplet_io_copy_result(o->runtime,&o->operation,&result,NULL)!=QAPLET_BUSY ||
     qaplet_io_release(o->runtime,&o->operation,NULL)!=QAPLET_BUSY || qaplet_get_io_snapshot(o->runtime,&snapshot,NULL)!=QAPLET_BUSY)atomic_store(&o->error,1);
}
static void *io_foreign(void *opaque){
  io_observer *o=opaque;qaplet_io_operation token=QAPLET_IO_OPERATION_INIT;qaplet_io_result result=QAPLET_IO_RESULT_INIT;
  qaplet_io_status status=QAPLET_IO_STATUS_INIT;
  struct {qaplet_io_status value;uint64_t tail;} extended={QAPLET_IO_STATUS_INIT,0};extended.value.struct_size=sizeof(extended);
  if(qaplet_get_io_status(o->runtime,&o->operation,&extended.value,NULL)!=QAPLET_OK || extended.value.struct_size!=sizeof(extended) || extended.tail)atomic_store(&o->error,1);
  extended.tail=1;if(qaplet_get_io_status(o->runtime,&o->operation,&extended.value,NULL)!=QAPLET_ABI_MISMATCH)atomic_store(&o->error,1);
  qaplet_io_operation wrong=o->operation;++wrong.runtime_id;
  if(qaplet_get_io_status(o->runtime,&wrong,&status,NULL)!=QAPLET_INVALID_HANDLE)atomic_store(&o->error,1);
  if(qaplet_io_read(o->runtime,&o->endpoint,1,&token,NULL)!=QAPLET_WRONG_THREAD ||
     qaplet_io_write(o->runtime,&o->endpoint,"X",1,&token,NULL)!=QAPLET_WRONG_THREAD ||
     qaplet_io_finish(o->runtime,&o->endpoint,NULL)!=QAPLET_WRONG_THREAD || qaplet_io_close(o->runtime,&o->endpoint,NULL)!=QAPLET_WRONG_THREAD ||
     qaplet_io_copy_result(o->runtime,&o->operation,&result,NULL)!=QAPLET_WRONG_THREAD || qaplet_io_release(o->runtime,&o->operation,NULL)!=QAPLET_WRONG_THREAD ||
     qaplet_io_cancel(o->runtime,&o->operation,NULL)!=QAPLET_OK || qaplet_io_cancel(o->runtime,&o->operation,NULL)!=QAPLET_OK ||
     qaplet_get_io_status(o->runtime,&o->operation,&status,NULL)!=QAPLET_OK || status.state!=QAPLET_IO_CANCELLED)atomic_store(&o->error,1);
  return NULL;
}
static int pipe_control(void){
  qaplet_config b=QAPLET_CONFIG_INIT;qaplet_m2_config m=QAPLET_M2_CONFIG_INIT;m.limits.processes=1;qaplet_runtime *r=NULL;
  CHECK(qaplet_create_m2(&b,&m,&r,NULL)==QAPLET_OK);
  io_observer observer={.runtime=r};qaplet_host_notify hook=QAPLET_HOST_NOTIFY_INIT;hook.function=io_notify;hook.userdata=&observer;
  CHECK(qaplet_set_host_notify(r,&hook,NULL)==QAPLET_OK);
  qaplet_process_options options;prepare(&options,"--hang");for(int i=0;i<3;++i)options.stdio[i]=QAPLET_PROCESS_PIPE;
  qaplet_process p=QAPLET_PROCESS_INIT;qaplet_process_status s=QAPLET_PROCESS_STATUS_INIT;CHECK(spawn_ok(r,&options,&p,&s,NULL)==QAPLET_OK);
  qaplet_io_endpoint ep[3]={QAPLET_IO_ENDPOINT_INIT,QAPLET_IO_ENDPOINT_INIT,QAPLET_IO_ENDPOINT_INIT};
  for(int i=0;i<3;++i)CHECK(qaplet_process_get_stdio(r,&p,i,&ep[i],NULL)==QAPLET_OK);
  qaplet_io_operation read=QAPLET_IO_OPERATION_INIT;CHECK(qaplet_io_read(r,&ep[1],1,&read,NULL)==QAPLET_OK);
  unsigned char byte=0;qaplet_io_result result=QAPLET_IO_RESULT_INIT;result.buffer=&byte;result.capacity=1;
  CHECK(!io_result(r,&read,&result) && result.length==1 && byte=='R' && qaplet_io_release(r,&read,NULL)==QAPLET_OK);
  CHECK(qaplet_io_read(r,&ep[1],1,&read,NULL)==QAPLET_OK);
  qaplet_progress progress=QAPLET_PROGRESS_INIT;CHECK(qaplet_pump(r,&progress,NULL)==QAPLET_OK);
  observer.endpoint=ep[1];observer.operation=read;
  pthread_t thread;CHECK(!pthread_create(&thread,NULL,io_foreign,&observer) && !pthread_join(thread,NULL) && !observer.error);
  CHECK(qaplet_io_release(r,&read,NULL)==QAPLET_OK && qaplet_io_close(r,&ep[1],NULL)==QAPLET_OK);
  qaplet_io_status status=QAPLET_IO_STATUS_INIT;CHECK(qaplet_get_io_status(r,&read,&status,NULL)==QAPLET_INVALID_HANDLE);
  CHECK(qaplet_io_finish(r,&ep[0],NULL)==QAPLET_OK && qaplet_io_close(r,&ep[2],NULL)==QAPLET_OK);
  qaplet_drive_result drive=QAPLET_DRIVE_RESULT_INIT;CHECK(qaplet_run(r,&drive,NULL)==QAPLET_OK && drive.reason==QAPLET_DRIVE_IDLE);
  CHECK(qaplet_process_release(r,&p,NULL)==QAPLET_OK);
  qaplet_process next=QAPLET_PROCESS_INIT;double until=milliseconds()+5000;qaplet_code accepted;
  do{accepted=qaplet_process_spawn(r,&options,&next,&s,NULL);if(accepted!=QAPLET_RESOURCE_LIMIT)break;sched_yield();}while(milliseconds()<until);
  CHECK(accepted==QAPLET_OK && next.generation!=p.generation);
  qaplet_io_operation rejected=QAPLET_IO_OPERATION_INIT;CHECK(qaplet_io_read(r,&ep[1],1,&rejected,NULL)==QAPLET_INVALID_HANDLE);
  for(int i=0;i<3;++i){qaplet_io_endpoint endpoint=QAPLET_IO_ENDPOINT_INIT;CHECK(qaplet_process_get_stdio(r,&next,i,&endpoint,NULL)==QAPLET_OK && qaplet_io_close(r,&endpoint,NULL)==QAPLET_OK);}
  CHECK(!finish(r) && observer.notifications && !observer.error);
  puts("M2 process pipe control: foreign cancel/POD; owner mutations; HostNotify BUSY; release invalidates while cancel/close retires physically; reused process endpoints reject stale tokens: pass");return 0;
}
static qaplet_code io_quota_work(qaplet_work_context *context,const void *input,size_t n,qaplet_work_output *out,void *userdata){
  (void)context;(void)input;(void)n;(void)userdata;((unsigned char *)out->data)[0]='W';out->length=1;return QAPLET_OK;
}
static int pipe_shared_limits(void){
  for(int count_limit=0;count_limit<2;++count_limit){
    qaplet_config b=QAPLET_CONFIG_INIT;qaplet_m2_config m=QAPLET_M2_CONFIG_INIT;qaplet_runtime *r=NULL;
    m.limits.operations=count_limit ? 1 : 2;m.limits.operation_data_bytes=4;m.limits.io_block_bytes=1;
    CHECK(qaplet_create_m2(&b,&m,&r,NULL)==QAPLET_OK);
    qaplet_work_handler handler=QAPLET_WORK_HANDLER_INIT;handler.name="quota";handler.name_length=5;handler.function=io_quota_work;handler.input_bytes=3;handler.output_bytes=1;
    CHECK(qaplet_register_work_handler(r,&handler,NULL)==QAPLET_OK);
    qaplet_process_options options;prepare(&options,"--hang");for(int i=0;i<3;++i)options.stdio[i]=QAPLET_PROCESS_PIPE;
    qaplet_process p=QAPLET_PROCESS_INIT;qaplet_process_status s=QAPLET_PROCESS_STATUS_INIT;CHECK(spawn_ok(r,&options,&p,&s,NULL)==QAPLET_OK);
    qaplet_io_endpoint output=QAPLET_IO_ENDPOINT_INIT;CHECK(qaplet_process_get_stdio(r,&p,1,&output,NULL)==QAPLET_OK);
    qaplet_work_task work=QAPLET_WORK_TASK_INIT;CHECK(qaplet_work_submit(r,"quota",5,"ABC",3,&work,NULL)==QAPLET_OK);
    qaplet_work_status ws=QAPLET_WORK_STATUS_INIT;double until=milliseconds()+5000;
    do{CHECK(qaplet_get_work_status(r,&work,&ws,NULL)==QAPLET_OK);if(ws.state==QAPLET_WORK_SUCCEEDED)break;sched_yield();}while(milliseconds()<until);
    CHECK(ws.state==QAPLET_WORK_SUCCEEDED);
    qaplet_io_snapshot snapshot=QAPLET_IO_SNAPSHOT_INIT;CHECK(qaplet_get_io_snapshot(r,&snapshot,NULL)==QAPLET_OK && snapshot.shared_operations==1 && snapshot.shared_data_bytes==4);
    qaplet_io_operation read=QAPLET_IO_OPERATION_INIT;CHECK(qaplet_io_read(r,&output,1,&read,NULL)==QAPLET_RESOURCE_LIMIT && !read.runtime_id);
    CHECK(qaplet_work_release(r,&work,NULL)==QAPLET_OK && qaplet_io_read(r,&output,1,&read,NULL)==QAPLET_RESOURCE_LIMIT);
    qaplet_progress progress=QAPLET_PROGRESS_INIT;CHECK(qaplet_pump(r,&progress,NULL)==QAPLET_OK);
    CHECK(qaplet_io_read(r,&output,1,&read,NULL)==QAPLET_OK);
    unsigned char byte=0;qaplet_io_result result=QAPLET_IO_RESULT_INIT;result.buffer=&byte;result.capacity=1;
    CHECK(!io_result(r,&read,&result) && result.length==1 && byte=='R');
    CHECK(qaplet_work_submit(r,"quota",5,"ABC",3,&work,NULL)==QAPLET_RESOURCE_LIMIT);
    CHECK(qaplet_get_io_snapshot(r,&snapshot,NULL)==QAPLET_OK && snapshot.shared_operations==1 && snapshot.shared_data_bytes==1);
    CHECK(qaplet_io_release(r,&read,NULL)==QAPLET_OK);
    CHECK(qaplet_work_submit(r,"quota",5,"ABC",3,&work,NULL)==QAPLET_OK);
    qaplet_snapshot state=QAPLET_SNAPSHOT_INIT;CHECK(qaplet_get_snapshot(r,&state,NULL)==QAPLET_OK && state.lifecycle==QAPLET_OPEN && !state.termination_reason);
    CHECK(!finish(r));
  }
  puts("M2 process pipe shared limits: Work and I/O share count and data; retained results and owner handoff keep reservations; ordinary saturation refuses atomically and recovers: pass");return 0;
}
static int pipe_backpressure(void){
  qaplet_config b=QAPLET_CONFIG_INIT;qaplet_m2_config m=QAPLET_M2_CONFIG_INIT;m.limits.pump_io_bytes=4096;qaplet_runtime *r=NULL;
  CHECK(qaplet_create_m2(&b,&m,&r,NULL)==QAPLET_OK);
  qaplet_process_options options;prepare(&options,"--flood");for(int i=0;i<3;++i)options.stdio[i]=QAPLET_PROCESS_PIPE;
  qaplet_process p=QAPLET_PROCESS_INIT;qaplet_process_status process=QAPLET_PROCESS_STATUS_INIT;CHECK(spawn_ok(r,&options,&p,&process,NULL)==QAPLET_OK);
  qaplet_io_endpoint ep[3]={QAPLET_IO_ENDPOINT_INIT,QAPLET_IO_ENDPOINT_INIT,QAPLET_IO_ENDPOINT_INIT};for(int i=0;i<3;++i)CHECK(qaplet_process_get_stdio(r,&p,i,&ep[i],NULL)==QAPLET_OK);
  qaplet_io_operation error=QAPLET_IO_OPERATION_INIT,output=QAPLET_IO_OPERATION_INIT;unsigned char byte=0;
  CHECK(qaplet_io_read(r,&ep[2],1,&error,NULL)==QAPLET_OK);qaplet_io_result result=QAPLET_IO_RESULT_INIT;result.buffer=&byte;result.capacity=1;
  CHECK(!io_result(r,&error,&result) && result.length==1 && byte=='R' && qaplet_io_release(r,&error,NULL)==QAPLET_OK);
  CHECK(qaplet_io_finish(r,&ep[0],NULL)==QAPLET_OK);
  qaplet_io_snapshot snapshot=QAPLET_IO_SNAPSHOT_INIT;CHECK(qaplet_get_io_snapshot(r,&snapshot,NULL)==QAPLET_OK && snapshot.data_bytes<=1 && snapshot.operations<=1);
  CHECK(qaplet_io_read(r,&ep[2],1,&error,NULL)==QAPLET_OK);
  unsigned char data[65536];size_t total=0;int eof=0;
  while(!eof){
    qaplet_code accepted;double until=milliseconds()+5000;
    do{
      accepted=qaplet_io_read(r,&ep[1],sizeof(data),&output,NULL);if(accepted!=QAPLET_RESOURCE_LIMIT)break;
      /* A released result does not retire a helper before physical thread
       * exit. Retry ordinary capacity after owner collection, without sleeps. */
      qaplet_progress progress=QAPLET_PROGRESS_INIT;CHECK(qaplet_pump(r,&progress,NULL)==QAPLET_OK);sched_yield();
    }while(milliseconds()<until);
    CHECK(accepted==QAPLET_OK);
    result=(qaplet_io_result)QAPLET_IO_RESULT_INIT;result.buffer=data;result.capacity=sizeof(data);
    CHECK(!io_result(r,&output,&result) && !result.code && result.length<=4096);
    for(size_t i=0;i<result.length;++i)CHECK(data[i]==(unsigned char)((total+i)%256));
    total+=result.length;eof=result.eof;CHECK(qaplet_io_release(r,&output,NULL)==QAPLET_OK);
    CHECK(qaplet_get_io_snapshot(r,&snapshot,NULL)==QAPLET_OK && snapshot.shared_data_bytes<=4*65536 && total<=1048576);
  }
  CHECK(total==1048576);
  result=(qaplet_io_result)QAPLET_IO_RESULT_INIT;result.buffer=&byte;result.capacity=1;
  CHECK(!io_result(r,&error,&result) && !result.code && result.length==1 && byte=='D' && qaplet_io_release(r,&error,NULL)==QAPLET_OK);
  CHECK(qaplet_io_read(r,&ep[2],1,&error,NULL)==QAPLET_OK);
  result=(qaplet_io_result)QAPLET_IO_RESULT_INIT;result.buffer=&byte;result.capacity=1;
  CHECK(!io_result(r,&error,&result) && !result.code && result.eof && !result.length && qaplet_io_release(r,&error,NULL)==QAPLET_OK);
  qaplet_drive_result drive=QAPLET_DRIVE_RESULT_INIT;CHECK(qaplet_run(r,&drive,NULL)==QAPLET_OK && drive.reason==QAPLET_DRIVE_IDLE);
  CHECK(qaplet_process_release(r,&p,NULL)==QAPLET_OK && !finish(r));
  puts("M2 process pipe backpressure: no output demand allocates no collection buffer; independent stderr; ordered 1MiB binary pull; per-pump byte quota; bounded retained data; EOF and cleanup: pass");return 0;
}
static int pipe_lifetime(void){
  for(int cancel=0;cancel<2;++cancel){
    qaplet_config b=QAPLET_CONFIG_INIT;qaplet_m2_config m=QAPLET_M2_CONFIG_INIT;qaplet_runtime *r=NULL;CHECK(qaplet_create_m2(&b,&m,&r,NULL)==QAPLET_OK);
    qaplet_process_options options;prepare(&options,cancel ? "--cancel-output" : "--eof-before-exit");for(int i=0;i<3;++i)options.stdio[i]=QAPLET_PROCESS_PIPE;
    qaplet_process p=QAPLET_PROCESS_INIT;qaplet_process_status process=QAPLET_PROCESS_STATUS_INIT;CHECK(spawn_ok(r,&options,&p,&process,NULL)==QAPLET_OK);
    qaplet_io_endpoint ep[3]={QAPLET_IO_ENDPOINT_INIT,QAPLET_IO_ENDPOINT_INIT,QAPLET_IO_ENDPOINT_INIT};for(int i=0;i<3;++i)CHECK(qaplet_process_get_stdio(r,&p,i,&ep[i],NULL)==QAPLET_OK);
    qaplet_io_operation output=QAPLET_IO_OPERATION_INIT,error=QAPLET_IO_OPERATION_INIT;unsigned char bytes[64];qaplet_io_result result=QAPLET_IO_RESULT_INIT;
    if(cancel){
      CHECK(qaplet_io_read(r,&ep[1],1,&output,NULL)==QAPLET_OK);result.buffer=bytes;result.capacity=sizeof(bytes);
      CHECK(!io_result(r,&output,&result) && result.length==1 && bytes[0]=='R' && qaplet_io_release(r,&output,NULL)==QAPLET_OK);
      CHECK(qaplet_process_cancel(r,&p,NULL)==QAPLET_OK);
      CHECK(qaplet_io_read(r,&ep[1],sizeof(bytes),&output,NULL)==QAPLET_OK);result=(qaplet_io_result)QAPLET_IO_RESULT_INIT;result.buffer=bytes;result.capacity=sizeof(bytes);
      CHECK(!io_result(r,&output,&result) && !result.code && !result.eof && result.length==5 && !memcmp(bytes,"\0\1\xffT\n",5));
      CHECK(qaplet_io_release(r,&output,NULL)==QAPLET_OK);
    }
    CHECK(qaplet_io_read(r,&ep[1],sizeof(bytes),&output,NULL)==QAPLET_OK && qaplet_io_read(r,&ep[2],sizeof(bytes),&error,NULL)==QAPLET_OK);
    result=(qaplet_io_result)QAPLET_IO_RESULT_INIT;result.buffer=bytes;result.capacity=sizeof(bytes);
    CHECK(!io_result(r,&output,&result) && !result.code && result.eof && !result.length && qaplet_io_release(r,&output,NULL)==QAPLET_OK);
    result=(qaplet_io_result)QAPLET_IO_RESULT_INIT;result.buffer=bytes;result.capacity=sizeof(bytes);
    CHECK(!io_result(r,&error,&result) && !result.code && result.eof && !result.length && qaplet_io_release(r,&error,NULL)==QAPLET_OK);
    CHECK(qaplet_get_process_status(r,&p,&process,NULL)==QAPLET_OK && process.referenced);
    if(!cancel)CHECK(!process.exited && !process.reaped && qaplet_process_release(r,&p,NULL)==QAPLET_BUSY);
    CHECK(qaplet_io_finish(r,&ep[0],NULL)==QAPLET_OK);
    qaplet_drive_result drive=QAPLET_DRIVE_RESULT_INIT;CHECK(qaplet_run(r,&drive,NULL)==QAPLET_OK && drive.reason==QAPLET_DRIVE_IDLE);
    CHECK(qaplet_get_process_status(r,&p,&process,NULL)==QAPLET_OK && process.reaped && !process.referenced && process.code==(cancel ? QAPLET_CANCELLED : QAPLET_OK));
    CHECK(qaplet_process_release(r,&p,NULL)==QAPLET_OK && !finish(r));
  }
  puts("M2 process pipe lifetime: EOF before child exit; stdin finish gates reap; process cancel preserves buffered binary output; native exit and managed EOF remain separate through ref handoff: pass");return 0;
}
static int completed(qaplet_runtime *r,qaplet_process *p,uint32_t expected) {
  qaplet_drive_result d=QAPLET_DRIVE_RESULT_INIT; qaplet_process_status s=QAPLET_PROCESS_STATUS_INIT;
  qaplet_code code=qaplet_run(r,&d,NULL);
  if (code!=QAPLET_OK || d.reason!=QAPLET_DRIVE_IDLE || !d.progressed) {
    qaplet_get_process_status(r,p,&s,NULL);status_failure("run",code,&s,&d);
  }
  CHECK(code==QAPLET_OK && d.reason==QAPLET_DRIVE_IDLE && d.progressed);
  code=qaplet_process_get_status(r,p,&s,NULL);
  if (code!=QAPLET_OK || !s.exited || !s.reaped || !s.delivered || s.referenced || s.code || s.native_exit_code!=expected || s.native_signal)
    status_failure("completed",code,&s,&d);
  CHECK(code==QAPLET_OK && s.exited && s.reaped && s.delivered && !s.referenced && !s.code && s.native_exit_code==expected && !s.native_signal);
  CHECK(qaplet_process_release(r,p,NULL)==QAPLET_OK);
  CHECK(qaplet_process_release(r,p,NULL)==QAPLET_INVALID_HANDLE);
  return 0;
}
static int inputs(void) {
  CHECK(!setenv("QAPLET_SNAPSHOT","frozen",1));
  qaplet_config b=QAPLET_CONFIG_INIT; qaplet_m2_config m=QAPLET_M2_CONFIG_INIT;
  b.cwd=cwd; b.cwd_length=strlen(cwd);
  qaplet_runtime *r=NULL; CHECK(qaplet_create_m2(&b,&m,&r,NULL)==QAPLET_OK);
  CHECK(!setenv("QAPLET_SNAPSHOT","changed",1)); CHECK(!setenv("QAPLET_AFTER_CREATE","leak",1));
  qaplet_process_options o; prepare(&o,"--snapshot");
  o.path.data=strrchr(executable,'/')+1; o.path.length=strlen(o.path.data);
  qaplet_process p=QAPLET_PROCESS_INIT; qaplet_process_status s=QAPLET_PROCESS_STATUS_INIT;
  CHECK(spawn_ok(r,&o,&p,&s,NULL)==QAPLET_OK); CHECK(!completed(r,&p,37));
  prepare(&o,"--check");
  char envcwd[PATH_MAX+32]; snprintf(envcwd,sizeof(envcwd),"QAPLET_CWD=%s",native_cwd);
  qaplet_string env[]={ {"QAPLET_ONLY=完整 值",strlen("QAPLET_ONLY=完整 值")},{envcwd,strlen(envcwd)} };
  o.flags=QAPLET_PROCESS_REPLACE_ENV; o.env=env; o.env_count=2;
  CHECK(spawn_ok(r,&o,&p,&s,NULL)==QAPLET_OK);
  memset(envcwd,'?',strlen(envcwd)); arguments[0]=(qaplet_string){"modified",8};
  CHECK(!completed(r,&p,37));
  prepare(&o,"--empty"); o.flags=QAPLET_PROCESS_REPLACE_ENV;
  CHECK(spawn_ok(r,&o,&p,&s,NULL)==QAPLET_OK); CHECK(!completed(r,&p,37));
  CHECK(!finish(r)); unsetenv("QAPLET_AFTER_CREATE"); unsetenv("QAPLET_SNAPSHOT");
  puts("M2 process inputs: explicit Unicode path; copied argv/env/cwd; factory snapshot and complete replacement; actual exit/reap: pass"); return 0;
}
static int inherit_descriptors(int sentinel,int fds[3],struct rlimit *original,struct rlimit *prepared) {
  CHECK(!getrlimit(RLIMIT_NOFILE,original));
  /* F_DUPFD uses the current soft limit. Prepare real high descriptors before
   * lowering it, without asking for a larger hard limit or retaining the raise. */
  int high=original->rlim_max>8192 ? 8192 : original->rlim_max>256 ? (int)original->rlim_max-1 : 0;
  int minimum[]={197,high>1023 ? 1023 : high-1,high};
  *prepared=*original;
  if (prepared->rlim_cur<(rlim_t)high+1) prepared->rlim_cur=(rlim_t)high+1;
  int error=0;
  if (high<=256) error=EMFILE;
  else if (setrlimit(RLIMIT_NOFILE,prepared)) error=errno;
  else for(unsigned i=0;i<3;++i) {
    fds[i]=fcntl(sentinel,F_DUPFD,minimum[i]);
    if (fds[i]<minimum[i] || fcntl(fds[i],F_SETFD,0)) { error=errno;break; }
  }
  int restore_error=setrlimit(RLIMIT_NOFILE,original) ? errno : 0;
  struct rlimit restored={0};
  int restored_ok=!getrlimit(RLIMIT_NOFILE,&restored) && restored.rlim_cur==original->rlim_cur && restored.rlim_max==original->rlim_max;
  if (error || restore_error || !restored_ok) {
    fprintf(stderr,"inherit fixture preparation: original=%llu/%llu prepared=%llu/%llu selected=%d,%d,%d minimum=%d,%d,%d error=%d restore_error=%d restored=%llu/%llu requires_high_fd_above=256\n",
      (unsigned long long)original->rlim_cur,(unsigned long long)original->rlim_max,
      (unsigned long long)prepared->rlim_cur,(unsigned long long)prepared->rlim_max,
      fds[0],fds[1],fds[2],minimum[0],minimum[1],minimum[2],error,restore_error,
      (unsigned long long)restored.rlim_cur,(unsigned long long)restored.rlim_max);
    for(unsigned i=0;i<3;++i) if (fds[i]>=0) { close(fds[i]);fds[i]=-1; }
    return 1;
  }
  CHECK(fds[2]>256 && (original->rlim_max<=8192 || fds[2]>=8192));
  return 0;
}
static int inherit(void) {
  char name[PATH_MAX],identity[128]="197";
  snprintf(name,sizeof(name),"%s/sentinel",cwd);
  int sentinel=-1; int64_t native=0;
  int inherited_fds[3]={-1,-1,-1};struct rlimit original={0},prepared={0},lowered={0},after={0},restored={0};
  if (IsWindows()) {
    char16_t path[PATH_MAX]; tprecode8to16(path,PATH_MAX,native_cwd);
    size_t n=0; while(path[n]) ++n; const char16_t suffix[]=u"\\sentinel"; memcpy(path+n,suffix,sizeof(suffix));
    struct { uint32_t size; void *descriptor; int32_t inherited; } security={sizeof(security),NULL,1};
    native=CreateFile(path,0xc0000000,7,&security,2,128,0); CHECK(native && native!=-1);
    struct NtByHandleFileInformation info; CHECK(GetFileInformationByHandle(native,&info));
    snprintf(identity,sizeof(identity),"%lld:%u:%u:%u",(long long)native,info.dwVolumeSerialNumber,info.nFileIndexHigh,info.nFileIndexLow);
  } else {
    sentinel=open(name,O_CREAT|O_TRUNC|O_RDWR,0600);CHECK(sentinel>=0);
    if (inherit_descriptors(sentinel,inherited_fds,&original,&prepared)) { close(sentinel);unlink(name);return 1; }
    snprintf(identity,sizeof(identity),"%d,%d,%d",inherited_fds[0],inherited_fds[1],inherited_fds[2]);
  }
  qaplet_config b=QAPLET_CONFIG_INIT; qaplet_m2_config m=QAPLET_M2_CONFIG_INIT; qaplet_runtime *r=NULL;
  CHECK(qaplet_create_m2(&b,&m,&r,NULL)==QAPLET_OK);
  int io[3][2],saved[3];
  for(int i=0;i<3;++i) { saved[i]=dup(i); CHECK(saved[i]>=0 && !pipe2(io[i],O_CLOEXEC)); CHECK(dup2(io[i][i ? 1 : 0],i)==i); }
  qaplet_process_options o; prepare(&o,"--inherit"); for (int i=0;i<3;++i) o.stdio[i]=QAPLET_PROCESS_INHERIT;
  arguments[IsWindows() ? 2 : 4]=(qaplet_string){identity,strlen(identity)};
  qaplet_process p=QAPLET_PROCESS_INIT; qaplet_process_status s=QAPLET_PROCESS_STATUS_INIT;
  if (!IsWindows()) { lowered=original;lowered.rlim_cur=256;CHECK(!setrlimit(RLIMIT_NOFILE,&lowered)); }
  /* Report only after restoring fd2: a failed spawn's diagnostic must not be
   * stranded in the child's redirected stderr pipe. */
  qaplet_code code=qaplet_process_spawn(r,&o,&p,&s,NULL);
  int limit_unchanged=1,restore_limit_error=0;
  if (!IsWindows()) {
    limit_unchanged=!getrlimit(RLIMIT_NOFILE,&after) && after.rlim_cur==lowered.rlim_cur && after.rlim_max==original.rlim_max;
    restore_limit_error=setrlimit(RLIMIT_NOFILE,&original) ? errno : 0;
    limit_unchanged=limit_unchanged && !getrlimit(RLIMIT_NOFILE,&restored) && restored.rlim_cur==original.rlim_cur && restored.rlim_max==original.rlim_max;
    for(unsigned i=0;i<3;++i) limit_unchanged=limit_unchanged && fcntl(inherited_fds[i],F_GETFD)==0;
  }
  for(int i=0;i<3;++i) { CHECK(dup2(saved[i],i)==i); close(saved[i]); close(io[i][i ? 1 : 0]); }
  spawn_failure(code,&s);
  if (code!=QAPLET_OK || !limit_unchanged || restore_limit_error)
    fprintf(stderr,"inherit fixture spawn: original=%llu/%llu prepared=%llu/%llu lowered=%llu/%llu selected=%d,%d,%d observed=%llu/%llu restored=%llu/%llu parent_unchanged=%d restore_error=%d\n",
      (unsigned long long)original.rlim_cur,(unsigned long long)original.rlim_max,
      (unsigned long long)prepared.rlim_cur,(unsigned long long)prepared.rlim_max,
      (unsigned long long)lowered.rlim_cur,(unsigned long long)lowered.rlim_max,
      inherited_fds[0],inherited_fds[1],inherited_fds[2],(unsigned long long)after.rlim_cur,(unsigned long long)after.rlim_max,
      (unsigned long long)restored.rlim_cur,(unsigned long long)restored.rlim_max,limit_unchanged,restore_limit_error);
  CHECK(code==QAPLET_OK && limit_unchanged && !restore_limit_error);
  const unsigned char input[]={'I',0,255,'N','\n'},errors[]={'E',0,254,'R'};
  CHECK(write(io[0][1],input,sizeof(input))==sizeof(input)); close(io[0][1]);
  unsigned char output[16]={0},error[16]={0};
  ssize_t output_bytes=read(io[1][0],output,sizeof(output)),error_bytes=read(io[2][0],error,sizeof(error));
  if (output_bytes!=sizeof(input) || error_bytes!=sizeof(errors)) {
    fprintf(stderr,"inherit child stdout=%ld stderr=%ld: ",(long)output_bytes,(long)error_bytes);
    if (error_bytes>0) fwrite(error,1,error_bytes,stderr);
    char diagnostic[256]; ssize_t n; while ((n=read(io[2][0],diagnostic,sizeof(diagnostic)))>0) fwrite(diagnostic,1,n,stderr);
    fputc('\n',stderr);
  }
  CHECK(output_bytes==sizeof(input) && !memcmp(output,input,sizeof(input)) && read(io[1][0],output,sizeof(output))==0);
  CHECK(error_bytes==sizeof(errors) && !memcmp(error,errors,sizeof(errors)) && read(io[2][0],error,sizeof(error))==0);
  close(io[1][0]); close(io[2][0]); CHECK(!completed(r,&p,37)); CHECK(!finish(r));
  if (IsWindows()) CHECK(CloseHandle(native)); else {
    CHECK(!getrlimit(RLIMIT_NOFILE,&restored) && restored.rlim_cur==original.rlim_cur && restored.rlim_max==original.rlim_max);
    for(unsigned i=0;i<3;++i) CHECK(fcntl(inherited_fds[i],F_GETFD)==0 && !close(inherited_fds[i]));
    CHECK(!close(sentinel));
  }
  CHECK(!unlink(name));
  puts("M2 process inherit: binary authorized stdio; extra native descriptor excluded; actual exit/reap: pass"); return 0;
}
static int errors(void) {
  qaplet_config b=QAPLET_CONFIG_INIT; qaplet_m2_config m=QAPLET_M2_CONFIG_INIT; m.limits.process_arguments=16;
  qaplet_runtime *r=NULL; CHECK(qaplet_create_m2(&b,&m,&r,NULL)==QAPLET_OK);
  qaplet_progress progress=QAPLET_PROGRESS_INIT; CHECK(qaplet_eval(r,"42",2,&progress,NULL)==QAPLET_OK);
  qaplet_process_options o; prepare(&o,"--empty");
  qaplet_process p=QAPLET_PROCESS_INIT; qaplet_process_status s=QAPLET_PROCESS_STATUS_INIT;
  o.stdio[0]=3; CHECK(qaplet_process_spawn(r,&o,&p,&s,NULL)==QAPLET_INVALID_ARGUMENT && !p.runtime_id);
  prepare(&o,"--empty"); o.path=(qaplet_string){"bad\0path",8}; CHECK(qaplet_process_spawn(r,&o,&p,&s,NULL)==QAPLET_INVALID_ARGUMENT);
  o.path=(qaplet_string){"\xff",1}; CHECK(qaplet_process_spawn(r,&o,&p,&s,NULL)==QAPLET_INVALID_UTF8);
  prepare(&o,"--empty"); o.argc=17; CHECK(qaplet_process_spawn(r,&o,&p,&s,NULL)==QAPLET_RESOURCE_LIMIT);
  prepare(&o,"--empty"); qaplet_string env[]={ {"NAME=a",6},{"NAME=b",6} }; o.flags=QAPLET_PROCESS_REPLACE_ENV; o.env=env; o.env_count=2;
  CHECK(qaplet_process_spawn(r,&o,&p,&s,NULL)==QAPLET_INVALID_ARGUMENT);
  env[0]=(qaplet_string){"=bad",4}; o.env_count=1; CHECK(qaplet_process_spawn(r,&o,&p,&s,NULL)==QAPLET_INVALID_ARGUMENT);
  prepare(&o,"--empty"); o.path=(qaplet_string){"sh",2}; CHECK(qaplet_process_spawn(r,&o,&p,&s,NULL)==QAPLET_PROCESS_ERROR && s.error>0 && s.error_domain==(IsWindows() ? QAPLET_ERROR_DOMAIN_WIN32 : QAPLET_ERROR_DOMAIN_ERRNO));
  prepare(&o,"--empty"); o.cwd=(qaplet_string){"missing-cwd",11}; CHECK(qaplet_process_spawn(r,&o,&p,&s,NULL)==QAPLET_PROCESS_ERROR && s.error>0);
  if (IsWindows()) {
    char *long_argument=malloc(32768); CHECK(long_argument); memset(long_argument,'x',32768);
    prepare(&o,"--empty"); arguments[2]=(qaplet_string){long_argument,32768};
    CHECK(qaplet_process_spawn(r,&o,&p,&s,NULL)==QAPLET_VALUE_TOO_LARGE);
    free(long_argument);
  }
  qaplet_result result=QAPLET_RESULT_INIT; CHECK(qaplet_get_result(r,&result,NULL)==QAPLET_OK && result.number==42);
  qaplet_snapshot snap=QAPLET_SNAPSHOT_INIT; CHECK(qaplet_get_snapshot(r,&snap,NULL)==QAPLET_OK && snap.lifecycle==QAPLET_OPEN);
  CHECK(!finish(r));
  /* A missing Host stdin must remain missing. An internal waker allocated
   * after this close cannot become an implicitly authorized child stream. */
  int saved=dup(0); CHECK(saved>=0 && !close(0));
  CHECK(qaplet_create_m2(&b,&m,&r,NULL)==QAPLET_OK);
  prepare(&o,"--empty"); o.stdio[0]=QAPLET_PROCESS_INHERIT;
  qaplet_code invalid_stdio=qaplet_process_spawn(r,&o,&p,&s,NULL);
  CHECK(dup2(saved,0)==0 && !close(saved));
  CHECK(!finish(r)); CHECK(invalid_stdio==QAPLET_PROCESS_ERROR);
  puts("M2 process errors: strict UTF-8/NUL/limits; no PATH search; missing cwd/program native errors; result retained; safe rollback: pass"); return 0;
}
static int hanging(qaplet_runtime *r,qaplet_process *p) {
  int in[2],out[2],savein=dup(0),saveout=dup(1); CHECK(savein>=0 && saveout>=0 && !pipe2(in,O_CLOEXEC) && !pipe2(out,O_CLOEXEC));
  CHECK(dup2(in[0],0)==0 && dup2(out[1],1)==1);
  qaplet_process_options o; prepare(&o,"--hang"); o.stdio[0]=o.stdio[1]=QAPLET_PROCESS_INHERIT;
  qaplet_process_status s=QAPLET_PROCESS_STATUS_INIT; qaplet_code code=spawn_ok(r,&o,p,&s,NULL);
  CHECK(dup2(savein,0)==0 && dup2(saveout,1)==1); close(savein);close(saveout);close(in[0]);close(out[1]);
  CHECK(code==QAPLET_OK); char byte=0; CHECK(read(out[0],&byte,1)==1 && byte=='R'); close(out[0]);
  /* Parent keeps input open until the process is reaped. This explicit gate
   * proves the child is running without random sleeps or private state. */
  return in[1];
}
static int cleanup(void) {
  for (int unref=0;unref<2;++unref) {
    qaplet_config b=QAPLET_CONFIG_INIT; qaplet_m2_config m=QAPLET_M2_CONFIG_INIT; m.limits.processes=1; m.limits.helper_threads=1; m.limits.native_handles=16; m.limits.reactor_registrations=2;
    qaplet_runtime *r=NULL; CHECK(qaplet_create_m2(&b,&m,&r,NULL)==QAPLET_OK);
    qaplet_process p=QAPLET_PROCESS_INIT; int gate=hanging(r,&p); CHECK(gate>=0);
    qaplet_process_snapshot ps=QAPLET_PROCESS_SNAPSHOT_INIT; CHECK(qaplet_get_process_snapshot(r,&ps,NULL)==QAPLET_OK && ps.processes==1 && ps.referenced==1 && ps.helper_threads==1 && ps.helper_stack_bytes==1024*1024);
    qaplet_process_options o; prepare(&o,"--empty"); qaplet_process extra=QAPLET_PROCESS_INIT; qaplet_process_status s=QAPLET_PROCESS_STATUS_INIT;
    CHECK(qaplet_process_spawn(r,&o,&extra,&s,NULL)==QAPLET_RESOURCE_LIMIT);
    CHECK(qaplet_process_release(r,&p,NULL)==QAPLET_BUSY);
    qaplet_drive_result drive=QAPLET_DRIVE_RESULT_INIT;
    if (unref) { CHECK(qaplet_process_set_ref(r,&p,0,NULL)==QAPLET_OK); CHECK(qaplet_run(r,&drive,NULL)==QAPLET_OK && drive.reason==QAPLET_DRIVE_IDLE); }
    else {
      CHECK(qaplet_run_once(r,0,&drive,NULL)==QAPLET_OK && drive.reason==QAPLET_DRIVE_WAIT_TIMEOUT);
      CHECK(qaplet_request_stop(r,NULL)==QAPLET_OK);
      qaplet_progress progress=QAPLET_PROGRESS_INIT; CHECK(qaplet_pump(r,&progress,NULL)==QAPLET_EXECUTION_STOPPED);
      double until=milliseconds()+5000;
      do { CHECK(qaplet_process_get_status(r,&p,&s,NULL)==QAPLET_OK); if (s.reaped) break; sched_yield(); } while (milliseconds()<until);
      CHECK(s.reaped && (IsWindows() ? s.native_exit_code==0xD00D : s.native_signal==9));
    }
    CHECK(!finish(r)); close(gate);
  }
  puts("M2 process cleanup: default ref; independent process/helper/handle limits; unref stays alive; stop force/reap; close/quiesce/destroy: pass"); return 0;
}
static int limits(void) {
  for (int kind=0;kind<4;++kind) {
    qaplet_config b=QAPLET_CONFIG_INIT; qaplet_m2_config m=QAPLET_M2_CONFIG_INIT;
    m.limits.processes=kind==0 ? 1 : 2; m.limits.helper_threads=kind==1 ? 1 : 2;
    m.limits.native_handles=kind==2 ? (IsWindows() ? 11 : 8) : 32;
    m.limits.reactor_registrations=kind==3 ? 2 : 4;
    qaplet_runtime *r=NULL; CHECK(qaplet_create_m2(&b,&m,&r,NULL)==QAPLET_OK);
    qaplet_process p=QAPLET_PROCESS_INIT; int gate=hanging(r,&p); CHECK(gate>=0);
    qaplet_process_options o; prepare(&o,"--empty");
    qaplet_process extra=QAPLET_PROCESS_INIT; qaplet_process_status status=QAPLET_PROCESS_STATUS_INIT;
    CHECK(qaplet_process_spawn(r,&o,&extra,&status,NULL)==QAPLET_RESOURCE_LIMIT && !extra.runtime_id);
    qaplet_m2_snapshot snapshot=QAPLET_M2_SNAPSHOT_INIT;
    CHECK(qaplet_get_m2_snapshot(r,&snapshot,NULL)==QAPLET_OK && snapshot.lifecycle==QAPLET_OPEN && snapshot.native_handles_peak<=m.limits.native_handles && snapshot.reactor_registrations<=m.limits.reactor_registrations);
    CHECK(!finish(r)); close(gate);
  }
  qaplet_config b=QAPLET_CONFIG_INIT; qaplet_m2_config m=QAPLET_M2_CONFIG_INIT;
  b.limits.native_storage_bytes=1048576; m.limits.operation_data_bytes=262144; m.limits.process_data_bytes=1048576;
  qaplet_runtime *r=NULL; CHECK(qaplet_create_m2(&b,&m,&r,NULL)==QAPLET_OK);
  qaplet_m2_snapshot snapshot=QAPLET_M2_SNAPSHOT_INIT; CHECK(qaplet_get_m2_snapshot(r,&snapshot,NULL)==QAPLET_OK);
  size_t length=snapshot.limits.native_storage_bytes-snapshot.native_current_bytes+1;
  CHECK(length<snapshot.m2_limits.process_data_bytes);
  char *large=malloc(1048577); CHECK(large); memset(large,'a',1048577);
  qaplet_process_options o; prepare(&o,"--empty"); qaplet_string argument={large,length}; o.argv=&argument; o.argc=1;
  qaplet_process p=QAPLET_PROCESS_INIT; qaplet_process_status status=QAPLET_PROCESS_STATUS_INIT;
  CHECK(qaplet_process_spawn(r,&o,&p,&status,NULL)==QAPLET_RESOURCE_LIMIT);
  argument.length=1048577;
  CHECK(qaplet_process_spawn(r,&o,&p,&status,NULL)==QAPLET_RESOURCE_LIMIT);
  CHECK(qaplet_get_m2_snapshot(r,&snapshot,NULL)==QAPLET_OK && snapshot.lifecycle==QAPLET_OPEN && !snapshot.termination_reason);
  free(large); CHECK(!finish(r));
  char environment[16385];memset(environment,'v',sizeof(environment)-1);environment[sizeof(environment)-1]=0;
  CHECK(!setenv("QAPLET_PROCESS_BUDGET_ONLY",environment,1));
  extern char **environ;size_t environment_bytes=0;
  for(size_t i=0;environ[i];++i) environment_bytes+=strlen(environ[i])+1;
  b=(qaplet_config)QAPLET_CONFIG_INIT;m=(qaplet_m2_config)QAPLET_M2_CONFIG_INIT;m.limits.process_data_bytes=environment_bytes+1024;
  CHECK(qaplet_create_m2(&b,&m,&r,NULL)==QAPLET_OK);unsetenv("QAPLET_PROCESS_BUDGET_ONLY");
  char argument_bytes[1024];memset(argument_bytes,'a',sizeof(argument_bytes));argument=(qaplet_string){argument_bytes,sizeof(argument_bytes)};
  prepare(&o,"--empty");o.argv=&argument;o.argc=1;
  qaplet_code combined=qaplet_process_spawn(r,&o,&p,&status,NULL);
  CHECK(!finish(r));CHECK(combined==QAPLET_RESOURCE_LIMIT);
  puts("M2 process limits: independent process/helper/native handle/registration quotas; encoded bytes and simultaneous native total; ordinary refusal: pass"); return 0;
}
static int observed_exit(qaplet_runtime *r,qaplet_process *p,qaplet_process_status *s) {
  double until=milliseconds()+5000;
  do {
    qaplet_code code=qaplet_process_get_status(r,p,s,NULL);
    if (code!=QAPLET_OK || s->code) status_failure("observe",code,s,NULL);
    CHECK(code==QAPLET_OK && !s->code);
    if (s->reaped && !s->helper_active) return 0;
    sched_yield();
  } while (milliseconds()<until);
  status_failure("observe-deadline",QAPLET_OK,s,NULL);
  CHECK(0); return 1;
}
static int ledger(void) {
  for (int pending=0;pending<2;++pending) {
    qaplet_config b=QAPLET_CONFIG_INIT; qaplet_m2_config m=QAPLET_M2_CONFIG_INIT;
    b.limits.execution_dispatches=1;
    qaplet_runtime *r=NULL; CHECK(qaplet_create_m2(&b,&m,&r,NULL)==QAPLET_OK);
    const char *source=pending ? "new Promise(()=>{})" : "42";
    qaplet_progress progress=QAPLET_PROGRESS_INIT;
    CHECK(qaplet_eval(r,source,strlen(source),&progress,NULL)==QAPLET_OK);
    qaplet_process_options o; prepare(&o,"--empty"); o.flags=QAPLET_PROCESS_REPLACE_ENV;
    qaplet_process p=QAPLET_PROCESS_INIT; qaplet_process_status status=QAPLET_PROCESS_STATUS_INIT;
    CHECK(spawn_ok(r,&o,&p,&status,NULL)==QAPLET_OK && !observed_exit(r,&p,&status));
    CHECK(status.reaped && !status.delivered && status.referenced);
    qaplet_drive_result drive=QAPLET_DRIVE_RESULT_INIT;
    CHECK(qaplet_run_once(r,0,&drive,NULL)==QAPLET_OK && drive.progressed);
    qaplet_snapshot snapshot=QAPLET_SNAPSHOT_INIT; CHECK(qaplet_get_snapshot(r,&snapshot,NULL)==QAPLET_OK);
    CHECK(snapshot.execution_dispatches==(pending ? 1 : 0));
    CHECK(qaplet_process_get_status(r,&p,&status,NULL)==QAPLET_OK && status.delivered && !status.referenced);
    CHECK(qaplet_process_release(r,&p,NULL)==QAPLET_OK);
    if (pending) {
      CHECK(spawn_ok(r,&o,&p,&status,NULL)==QAPLET_OK && !observed_exit(r,&p,&status));
      CHECK(qaplet_run_once(r,0,&drive,NULL)==QAPLET_DISPATCH_LIMIT);
      CHECK(qaplet_get_snapshot(r,&snapshot,NULL)==QAPLET_OK && snapshot.lifecycle==QAPLET_FAILED && snapshot.termination_reason==QAPLET_DISPATCH_LIMIT && snapshot.execution_dispatches==1);
    } else {
      qaplet_result result=QAPLET_RESULT_INIT; CHECK(qaplet_get_result(r,&result,NULL)==QAPLET_OK && result.number==42);
      CHECK(spawn_ok(r,&o,&p,&status,NULL)==QAPLET_OK);
      CHECK(qaplet_process_set_ref(r,&p,0,NULL)==QAPLET_OK && !observed_exit(r,&p,&status));
      CHECK(qaplet_run(r,&drive,NULL)==QAPLET_OK && drive.reason==QAPLET_DRIVE_IDLE && !drive.progressed);
      CHECK(qaplet_pump(r,&progress,NULL)==QAPLET_OK && progress.progressed);
      CHECK(qaplet_process_release(r,&p,NULL)==QAPLET_OK);
    }
    CHECK(!finish(r));
  }
  puts("M2 process ledger: native observation precedes owner handoff; pending entry cumulative dispatch; pure C preserves completed JS result: pass"); return 0;
}
static qaplet_code quick_work(qaplet_work_context *context,const void *input,size_t length,qaplet_work_output *output,void *userdata) {
  (void)context;(void)input;(void)length;(void)output;(void)userdata;return QAPLET_OK;
}
static void counted_timer(qaplet_runtime *r,const qaplet_timer *timer,void *userdata) { (void)r;(void)timer;++*(unsigned *)userdata; }
static int scheduling(void) {
  qaplet_config b=QAPLET_CONFIG_INIT; qaplet_m2_config m=QAPLET_M2_CONFIG_INIT;
  b.limits.event_queue_slots=1; m.limits.processes=40;
  qaplet_runtime *r=NULL; CHECK(qaplet_create_m2(&b,&m,&r,NULL)==QAPLET_OK);
  qaplet_work_handler handler=QAPLET_WORK_HANDLER_INIT;
  handler.name="ready";handler.name_length=5;handler.function=quick_work;handler.input_bytes=handler.output_bytes=1;
  CHECK(qaplet_register_work_handler(r,&handler,NULL)==QAPLET_OK);
  const char source[]="Promise.all([import('qaplet:events'),import('qaplet:timers')]).then(([e,t])=>{e.onEvent(()=>{globalThis.processEvent=1});globalThis.installImmediate=()=>{t.setImmediate(()=>{globalThis.processImmediate=1})}})";
  qaplet_progress progress=QAPLET_PROGRESS_INIT; CHECK(qaplet_eval(r,source,sizeof(source)-1,&progress,NULL)==QAPLET_OK);
  while(progress.phase==QAPLET_PHASE_PENDING) CHECK(qaplet_pump(r,&progress,NULL)==QAPLET_OK);
  qaplet_process processes[40]; qaplet_process_status status=QAPLET_PROCESS_STATUS_INIT;
  qaplet_process_options o; prepare(&o,"--empty");o.flags=QAPLET_PROCESS_REPLACE_ENV;
  for(unsigned i=0;i<40;++i) {
    processes[i]=(qaplet_process)QAPLET_PROCESS_INIT;
    CHECK(spawn_ok(r,&o,&processes[i],&status,NULL)==QAPLET_OK);
    CHECK(!observed_exit(r,&processes[i],&status) && !status.delivered);
  }
  qaplet_work_task tasks[16];
  for(unsigned i=0;i<16;++i) { tasks[i]=(qaplet_work_task)QAPLET_WORK_TASK_INIT;CHECK(qaplet_work_submit(r,"ready",5,NULL,0,&tasks[i],NULL)==QAPLET_OK); }
  qaplet_work_snapshot work=QAPLET_WORK_SNAPSHOT_INIT;double until=milliseconds()+5000;
  do { CHECK(qaplet_get_work_snapshot(r,&work,NULL)==QAPLET_OK);if(work.completed==16)break;sched_yield(); } while(milliseconds()<until);
  CHECK(work.completed==16 && work.pending_handoffs==16);
  unsigned timers=0; qaplet_timer timer=QAPLET_TIMER_INIT;CHECK(qaplet_timer_create(r,0,counted_timer,&timers,&timer,NULL)==QAPLET_OK);
  CHECK(qaplet_eval(r,"installImmediate()",18,&progress,NULL)==QAPLET_OK);
  CHECK(qaplet_post_event(r,1,0,NULL,0,NULL)==QAPLET_OK && qaplet_post_event(r,2,0,NULL,0,NULL)==QAPLET_QUEUE_FULL);
  CHECK(qaplet_pump(r,&progress,NULL)==QAPLET_OK && progress.progressed);
  qaplet_process_snapshot native=QAPLET_PROCESS_SNAPSHOT_INIT;qaplet_snapshot events=QAPLET_SNAPSHOT_INIT;
  CHECK(qaplet_get_process_snapshot(r,&native,NULL)==QAPLET_OK && native.completed==14 && native.referenced==26);
  CHECK(qaplet_get_work_snapshot(r,&work,NULL)==QAPLET_OK && work.pending_handoffs==1);
  CHECK(qaplet_get_snapshot(r,&events,NULL)==QAPLET_OK && events.events_consumed==1 && !events.queue_depth && timers==1);
  CHECK(qaplet_pump(r,&progress,NULL)==QAPLET_OK && progress.progressed);
  CHECK(qaplet_get_process_snapshot(r,&native,NULL)==QAPLET_OK && native.completed==40 && !native.referenced);
  CHECK(qaplet_get_work_snapshot(r,&work,NULL)==QAPLET_OK && !work.pending_handoffs);
  for(unsigned i=0;i<40;++i) CHECK(qaplet_process_release(r,&processes[i],NULL)==QAPLET_OK);
  for(unsigned i=0;i<16;++i) CHECK(qaplet_work_release(r,&tasks[i],NULL)==QAPLET_OK);
  const char result_source[]="globalThis.processEvent===1&&globalThis.processImmediate===1";
  CHECK(qaplet_eval(r,result_source,sizeof(result_source)-1,&progress,NULL)==QAPLET_OK);
  qaplet_result result=QAPLET_RESULT_INIT;CHECK(qaplet_get_result(r,&result,NULL)==QAPLET_OK && result.boolean);
  CHECK(!finish(r));
  puts("M2 process scheduling: 40 process and 16 work completions share 32 deliveries with timer/immediate/event; fair provider cursor; reliable saturated queue: pass");return 0;
}
static int liveness(void) {
  qaplet_config b=QAPLET_CONFIG_INIT; qaplet_m2_config m=QAPLET_M2_CONFIG_INIT;
  qaplet_runtime *r=NULL;CHECK(qaplet_create_m2(&b,&m,&r,NULL)==QAPLET_OK);
  qaplet_progress progress=QAPLET_PROGRESS_INIT;
  const char setup[]="import('qaplet:timers').then(t=>{globalThis.hits='';globalThis.install=()=>{t.setTimeout(()=>{hits+='T'},0).unref();t.setImmediate(()=>{hits+='I'}).unref()}})";
  CHECK(qaplet_eval(r,setup,sizeof(setup)-1,&progress,NULL)==QAPLET_OK);
  while(progress.phase==QAPLET_PHASE_PENDING) CHECK(qaplet_pump(r,&progress,NULL)==QAPLET_OK);
  qaplet_process p=QAPLET_PROCESS_INIT;int gate=hanging(r,&p);CHECK(gate>=0);
  CHECK(qaplet_eval(r,"install()",9,&progress,NULL)==QAPLET_OK);
  qaplet_drive_result drive=QAPLET_DRIVE_RESULT_INIT;
  CHECK(qaplet_run_once(r,0,&drive,NULL)==QAPLET_OK && drive.progressed);
  qaplet_result result=QAPLET_RESULT_INIT;
  CHECK(qaplet_eval(r,"hits==='TI'",11,&progress,NULL)==QAPLET_OK && qaplet_get_result(r,&result,NULL)==QAPLET_OK && result.boolean);
  CHECK(qaplet_process_set_ref(r,&p,0,NULL)==QAPLET_OK);
  CHECK(qaplet_eval(r,"hits='';install()",17,&progress,NULL)==QAPLET_OK);
  CHECK(qaplet_run(r,&drive,NULL)==QAPLET_OK && drive.reason==QAPLET_DRIVE_IDLE && !drive.progressed);
  CHECK(qaplet_eval(r,"hits==='';",10,&progress,NULL)==QAPLET_OK && qaplet_get_result(r,&result,NULL)==QAPLET_OK && result.boolean);
  CHECK(qaplet_pump(r,&progress,NULL)==QAPLET_OK && progress.progressed);
  CHECK(qaplet_eval(r,"hits==='TI'",11,&progress,NULL)==QAPLET_OK && qaplet_get_result(r,&result,NULL)==QAPLET_OK && result.boolean);
  CHECK(!finish(r));close(gate);
  puts("M2 process liveness: referenced child permits ready unref timer/immediate; unref child returns naturally; explicit pump delivers; physical cleanup: pass");return 0;
}
static int identity(void) {
  qaplet_config b=QAPLET_CONFIG_INIT; qaplet_m2_config m=QAPLET_M2_CONFIG_INIT;m.limits.processes=1;
  qaplet_runtime *r=NULL,*other=NULL;CHECK(qaplet_create_m2(&b,&m,&r,NULL)==QAPLET_OK && qaplet_create_m2(&b,&m,&other,NULL)==QAPLET_OK);
  qaplet_process_options o;prepare(&o,"--empty");o.flags=QAPLET_PROCESS_REPLACE_ENV;
  qaplet_process p=QAPLET_PROCESS_INIT;qaplet_process_status status=QAPLET_PROCESS_STATUS_INIT;
  CHECK(spawn_ok(r,&o,&p,&status,NULL)==QAPLET_OK && !observed_exit(r,&p,&status));
  qaplet_process old=p;CHECK(!completed(r,&p,37));
  CHECK(spawn_ok(r,&o,&p,&status,NULL)==QAPLET_OK);
  CHECK(qaplet_process_get_status(r,&old,&status,NULL)==QAPLET_INVALID_HANDLE && qaplet_process_get_status(other,&p,&status,NULL)==QAPLET_INVALID_HANDLE);
  CHECK(!completed(r,&p,37) && !finish(r) && !finish(other));
  puts("M2 process identity: one-slot reuse; stale generation and cross-Runtime tokens refused; current child unaffected: pass");return 0;
}
typedef struct {
  qaplet_runtime *runtime,*other;
  qaplet_process process,stale;
  _Atomic int ready,error,notifications,notify_error;
} process_observer;
typedef struct { qaplet_runtime *runtime; qaplet_process process; qaplet_code code; } process_canceller;
static void *cancel_running(void *opaque) {
  process_canceller *c=opaque;
  c->code=qaplet_process_cancel(c->runtime,&c->process,NULL);
  return NULL;
}
static int cancellation(void) {
  qaplet_config b=QAPLET_CONFIG_INIT; qaplet_m2_config m=QAPLET_M2_CONFIG_INIT; m.limits.processes=1;
  qaplet_runtime *r=NULL,*other=NULL;
  CHECK(qaplet_create_m2(&b,&m,&r,NULL)==QAPLET_OK && qaplet_create_m2(&b,&m,&other,NULL)==QAPLET_OK);
  qaplet_process invalid=QAPLET_PROCESS_INIT; invalid.struct_size=8;
  CHECK(qaplet_process_cancel(NULL,&invalid,NULL)==QAPLET_ABI_MISMATCH && qaplet_process_cancel(r,NULL,NULL)==QAPLET_INVALID_ARGUMENT);
  qaplet_progress progress=QAPLET_PROGRESS_INIT; CHECK(qaplet_eval(r,"42",2,&progress,NULL)==QAPLET_OK);
  qaplet_process p=QAPLET_PROCESS_INIT; int gate=hanging(r,&p); CHECK(gate>=0);
  qaplet_process stale=p; ++stale.generation;
  CHECK(qaplet_process_cancel(other,&p,NULL)==QAPLET_INVALID_HANDLE && qaplet_process_cancel(r,&stale,NULL)==QAPLET_INVALID_HANDLE);
  process_canceller c={r,p,QAPLET_BAD_STATE}; pthread_t thread;
  CHECK(!pthread_create(&thread,NULL,cancel_running,&c) && !pthread_join(thread,NULL) && c.code==QAPLET_OK);
  CHECK(qaplet_process_cancel(r,&p,NULL)==QAPLET_OK);
  qaplet_drive_result drive=QAPLET_DRIVE_RESULT_INIT;
  CHECK(qaplet_run(r,&drive,NULL)==QAPLET_OK && drive.reason==QAPLET_DRIVE_IDLE);
  qaplet_process_status status=QAPLET_PROCESS_STATUS_INIT;
  CHECK(qaplet_process_get_status(r,&p,&status,NULL)==QAPLET_OK && status.code==QAPLET_CANCELLED && status.exited && status.reaped && status.delivered && !status.referenced);
  CHECK(IsWindows() ? status.native_exit_code==0xD00D && !status.native_signal : !status.native_exit_code && status.native_signal==SIGKILL);
  CHECK(!status.error_domain && !status.error && qaplet_process_cancel(r,&p,NULL)==QAPLET_OK);
  double until=milliseconds()+5000;
  do { CHECK(qaplet_process_get_status(r,&p,&status,NULL)==QAPLET_OK); if (!status.helper_active) break; sched_yield(); } while (milliseconds()<until);
  CHECK(!status.helper_active);
  CHECK(!close(gate)); stale=p;
  CHECK(qaplet_process_release(r,&p,NULL)==QAPLET_OK && qaplet_process_cancel(r,&stale,NULL)==QAPLET_INVALID_HANDLE);
  gate=hanging(r,&p); CHECK(gate>=0);
  CHECK(qaplet_process_cancel(r,&stale,NULL)==QAPLET_INVALID_HANDLE);
  CHECK(qaplet_get_process_status(r,&p,&status,NULL)==QAPLET_OK && !status.exited && !status.code);
  CHECK(!close(gate) && !observed_exit(r,&p,&status));
  CHECK(qaplet_process_cancel(r,&p,NULL)==QAPLET_ALREADY_COMPLETED);
  CHECK(!completed(r,&p,37));
  qaplet_result result=QAPLET_RESULT_INIT; CHECK(qaplet_get_result(r,&result,NULL)==QAPLET_OK && result.number==42);
  CHECK(!finish(r) && !finish(other));
  puts("M2 process cancel: any-thread acceptance; idempotent CANCELLED; actual force exit/reap; completion-first ALREADY_COMPLETED; stale/Runtime isolation; retained JS result: pass"); return 0;
}
static qaplet_code cleanup_mark(qaplet_runtime *r,uint64_t call,size_t argc,const qaplet_value *argv,void *opaque) {
  (void)r; (void)call; (void)argc; (void)argv; ++*(unsigned *)opaque; return QAPLET_OK;
}
static int terminal_cleanup(void) {
  for (unsigned mode=0;mode<4;++mode) {
    qaplet_config b=QAPLET_CONFIG_INIT; qaplet_m2_config m=QAPLET_M2_CONFIG_INIT;
    qaplet_runtime *r=NULL; CHECK(qaplet_create_m2(&b,&m,&r,NULL)==QAPLET_OK);
    unsigned marks=0;
    qaplet_host_export item=QAPLET_HOST_EXPORT_INIT; item.name="mark"; item.name_length=4; item.kind=QAPLET_HOST_FUNCTION; item.function=cleanup_mark; item.userdata=&marks;
    qaplet_host_module module=QAPLET_HOST_MODULE_INIT; module.name="host:process-cleanup"; module.name_length=20; module.exports=&item; module.export_count=1;
    CHECK(qaplet_register_host_module(r,&module,NULL)==QAPLET_OK);
    qaplet_progress progress=QAPLET_PROGRESS_INIT;
    const char setup[]="import('host:process-cleanup').then(h=>{globalThis.h=h})";
    CHECK(qaplet_eval(r,setup,sizeof(setup)-1,&progress,NULL)==QAPLET_OK);
    while (progress.phase==QAPLET_PHASE_PENDING) CHECK(qaplet_pump(r,&progress,NULL)==QAPLET_OK);
    qaplet_process p=QAPLET_PROCESS_INIT; int gate=hanging(r,&p); CHECK(gate>=0);
    if (mode==1) CHECK(qaplet_eval(r,"throw Error()",13,&progress,NULL)==QAPLET_JS_EXCEPTION);
    const char pending[]="Promise.resolve().then(()=>1).then(()=>h.mark()).finally(()=>h.mark());new Promise(()=>{})";
    qaplet_execution_options options=QAPLET_EXECUTION_OPTIONS_INIT; if (mode==3) options.max_dispatches=1;
    CHECK(qaplet_eval_with_options(r,pending,sizeof(pending)-1,&options,&progress,NULL)==QAPLET_OK && progress.phase==QAPLET_PHASE_PENDING);
    if (mode==2 || mode==3) {
      if (mode==2) CHECK(qaplet_request_stop(r,NULL)==QAPLET_OK);
      CHECK(qaplet_pump(r,&progress,NULL)==(mode==2 ? QAPLET_EXECUTION_STOPPED : QAPLET_DISPATCH_LIMIT));
      qaplet_process_status s=QAPLET_PROCESS_STATUS_INIT; double until=milliseconds()+5000;
      do { CHECK(qaplet_process_get_status(r,&p,&s,NULL)==QAPLET_OK); if (s.reaped) break; sched_yield(); } while (milliseconds()<until);
      CHECK(s.code==QAPLET_CANCELLED && s.reaped && (IsWindows() ? s.native_exit_code==0xD00D : s.native_signal==SIGKILL));
      qaplet_snapshot snapshot=QAPLET_SNAPSHOT_INIT;
      CHECK(qaplet_get_snapshot(r,&snapshot,NULL)==QAPLET_OK && snapshot.lifecycle==(mode==2 ? QAPLET_STOPPED : QAPLET_FAILED) && snapshot.termination_reason==(mode==2 ? QAPLET_EXECUTION_STOPPED : QAPLET_DISPATCH_LIMIT));
    }
    CHECK(!marks && !finish(r) && !marks && !close(gate));
  }
  puts("M2 process terminal cleanup: PENDING/ordinary-error close; owner STOPPED/FAILED force/reap before close; first cause retained; queued jobs/finally never run; native cleanup drained: pass"); return 0;
}
static void query_notify(void *opaque) {
  process_observer *o=opaque;
  qaplet_process_status status=QAPLET_PROCESS_STATUS_INIT;
  /* Deliberately violate the HostNotify rule to verify its TLS guard also
   * rejects the otherwise thread-safe POD entry before acquiring a lock. */
  if (qaplet_get_process_status(o->runtime,&o->process,&status,NULL)!=QAPLET_BUSY)
    o->notify_error=1;
  if (qaplet_process_cancel(o->runtime,&o->process,NULL)!=QAPLET_BUSY) o->notify_error=1;
  ++o->notifications;
}
static void *query_running(void *opaque) {
  process_observer *o=opaque;
  qaplet_process_status status=QAPLET_PROCESS_STATUS_INIT;
  if (qaplet_get_process_status(o->runtime,&o->process,&status,NULL)!=QAPLET_OK ||
      status.exited || status.reaped || status.delivered || !status.referenced || !status.helper_active)
    o->error=1;
  qaplet_process_status unchanged=status;
  if (qaplet_process_get_status(o->runtime,&o->process,&status,NULL)!=QAPLET_WRONG_THREAD ||
      qaplet_get_process_status(o->other,&o->process,&status,NULL)!=QAPLET_INVALID_HANDLE ||
      qaplet_get_process_status(o->runtime,&o->stale,&status,NULL)!=QAPLET_INVALID_HANDLE ||
      memcmp(&status,&unchanged,sizeof(status))) o->error=1;
  return NULL;
}
static void *query_released(void *opaque) {
  process_observer *o=opaque;
  qaplet_process_status status=QAPLET_PROCESS_STATUS_INIT;
  if (qaplet_get_process_status(o->runtime,&o->process,&status,NULL)!=QAPLET_OK ||
      !status.reaped || !status.delivered || status.referenced) { o->error=1; o->ready=1; return NULL; }
  o->ready=1;
  double until=milliseconds()+5000;
  qaplet_code code;
  do {
    qaplet_process_status previous=status;
    code=qaplet_get_process_status(o->runtime,&o->process,&status,NULL);
    if (code==QAPLET_INVALID_HANDLE) {
      if (memcmp(&previous,&status,sizeof(status))) o->error=1;
      return NULL;
    }
    if (code!=QAPLET_OK || !status.reaped || !status.delivered || status.referenced) { o->error=1; return NULL; }
    sched_yield();
  } while (milliseconds()<until);
  o->error=1; return NULL;
}
static int query(void) {
  qaplet_config b=QAPLET_CONFIG_INIT; qaplet_m2_config m=QAPLET_M2_CONFIG_INIT;
  m.limits.processes=1;
  qaplet_runtime *r=NULL,*other=NULL;
  CHECK(qaplet_create_m2(&b,&m,&r,NULL)==QAPLET_OK && qaplet_create_m2(&b,&m,&other,NULL)==QAPLET_OK);
  process_observer observer={0};observer.runtime=r;observer.other=other;
  qaplet_host_notify hook=QAPLET_HOST_NOTIFY_INIT;hook.function=query_notify;hook.userdata=&observer;
  CHECK(qaplet_set_host_notify(r,&hook,NULL)==QAPLET_OK);
  qaplet_progress progress=QAPLET_PROGRESS_INIT;
  CHECK(qaplet_eval(r,"42",2,&progress,NULL)==QAPLET_OK);
  qaplet_process p=QAPLET_PROCESS_INIT;int gate=hanging(r,&p);CHECK(gate>=0);
  observer.process=p;observer.stale=p;++observer.stale.generation;
  pthread_t thread;CHECK(!pthread_create(&thread,NULL,query_running,&observer) && !pthread_join(thread,NULL) && !observer.error);
  CHECK(!close(gate));
  qaplet_process_status status=QAPLET_PROCESS_STATUS_INIT;
  double until=milliseconds()+5000;
  do {
    CHECK(qaplet_get_process_status(r,&p,&status,NULL)==QAPLET_OK);
    if (status.reaped) break;
    sched_yield();
  } while(milliseconds()<until);
  CHECK(status.exited && status.reaped && status.native_exit_code==37 && !status.code &&
        !status.delivered && status.referenced && status.helper_active);
  qaplet_process_snapshot snapshot=QAPLET_PROCESS_SNAPSHOT_INIT;
  CHECK(qaplet_get_process_snapshot(r,&snapshot,NULL)==QAPLET_OK && snapshot.referenced==1 &&
        !snapshot.completed && snapshot.helper_threads==1);
  qaplet_result result=QAPLET_RESULT_INIT;
  CHECK(qaplet_get_result(r,&result,NULL)==QAPLET_OK && result.number==42);
  qaplet_drive_result drive=QAPLET_DRIVE_RESULT_INIT;
  CHECK(qaplet_run(r,&drive,NULL)==QAPLET_OK && drive.reason==QAPLET_DRIVE_IDLE);
  CHECK(!observed_exit(r,&p,&status));
  CHECK(!pthread_create(&thread,NULL,query_released,&observer));
  while (!observer.ready) sched_yield();
  CHECK(qaplet_process_release(r,&p,NULL)==QAPLET_OK && !pthread_join(thread,NULL) && !observer.error);
  qaplet_process_options options;prepare(&options,"--empty");options.flags=QAPLET_PROCESS_REPLACE_ENV;
  qaplet_process next=QAPLET_PROCESS_INIT;
  CHECK(spawn_ok(r,&options,&next,&status,NULL)==QAPLET_OK);
  CHECK(qaplet_get_process_status(r,&p,&status,NULL)==QAPLET_INVALID_HANDLE &&
        qaplet_get_process_status(r,&next,&status,NULL)==QAPLET_OK);
  CHECK(!completed(r,&next,37));
  CHECK(qaplet_close(r,NULL)==QAPLET_OK);
  CHECK(qaplet_get_process_status(r,&next,&status,NULL)==QAPLET_INVALID_HANDLE);
  CHECK(!finish(r) && !finish(other));
  CHECK(observer.notifications && !observer.notify_error);
  puts("M2 process POD query: any-thread token status; no collection/delivery/JS; Runtime/generation/release isolation; HostNotify BUSY; closed cleanup: pass");return 0;
}
static int js_basic(int quiet) {
  qaplet_process_options o;prepare(&o,"--empty");
  qaplet_config b=QAPLET_CONFIG_INIT;b.argv=o.argv;b.argc=o.argc;
  qaplet_m2_config m=QAPLET_M2_CONFIG_INIT;qaplet_runtime *r=NULL;
  CHECK(qaplet_create_m2(&b,&m,&r,NULL)==QAPLET_OK);
  const char *source="Promise.all([import('qaplet:process'),import('qaplet:runtime')]).then(async ([{spawn},{argv}])=>{"
    "const p=spawn(argv[0],{argv:[...argv],env:[],stdio:['ignore','ignore','ignore']});"
    "const s=await p.exited;await p.closed;if(!s.reaped||s.exitCode!==37)throw Error('exit');return 42})";
  char message[1024];qaplet_diagnostic d=QAPLET_DIAGNOSTIC_INIT;d.message=message;d.capacity=sizeof(message);
  qaplet_progress p=QAPLET_PROGRESS_INIT;qaplet_code code=qaplet_eval(r,source,strlen(source),&p,&d);
  qaplet_drive_result drive=QAPLET_DRIVE_RESULT_INIT;if(!code)code=qaplet_run(r,&drive,&d);
  if(code)fprintf(stderr,"JS process code=%d diagnostic=%s\n",code,message);
  qaplet_result result=QAPLET_RESULT_INIT;int passed=code==QAPLET_OK && drive.phase==QAPLET_PHASE_DONE && qaplet_get_result(r,&result,NULL)==QAPLET_OK && result.type==QAPLET_VALUE_DOUBLE && result.number==42;
  int cleanup=finish(r);CHECK(!cleanup);CHECK(passed);if(!quiet)puts("M2 process JS basic: explicit spawn; owner exit/reap Promise; managed closure; result and cleanup: pass");return 0;
}
static int js_issue(void) {
  qaplet_process_options o;prepare(&o,"--hang");qaplet_config b=QAPLET_CONFIG_INIT;b.argv=o.argv;b.argc=o.argc;b.limits.execution_timeout_ms=1000;
  qaplet_m2_config m=QAPLET_M2_CONFIG_INIT;qaplet_runtime *r=NULL;CHECK(qaplet_create_m2(&b,&m,&r,NULL)==QAPLET_OK);
  const char *source="Promise.all([import('qaplet:process'),import('qaplet:runtime')]).then(async ([{spawn},{argv}])=>{const p=spawn(argv[0],{argv:[...argv]});"
    "const w=p.stdin.write(new Uint8Array([1]));p.stdin.finish();if((await w.promise).bytesWritten!==1)throw Error('write');p.stdout.close();p.stderr.close();await p.exited;await p.closed;return 42})";
  qaplet_progress p=QAPLET_PROGRESS_INIT;qaplet_code code=qaplet_eval(r,source,strlen(source),&p,NULL);qaplet_drive_result drive=QAPLET_DRIVE_RESULT_INIT;
  if(!code)code=qaplet_run(r,&drive,NULL);qaplet_result result=QAPLET_RESULT_INIT;int passed=code==QAPLET_OK && drive.phase==QAPLET_PHASE_DONE && qaplet_get_result(r,&result,NULL)==QAPLET_OK && result.type==QAPLET_VALUE_DOUBLE && result.number==42;
  int cleanup=finish(r);CHECK(!cleanup);CHECK(passed);
  puts("M2 process JS issue: IO accepted from a JS job issues before native wait without an auxiliary timer; actual write and reap: pass");return 0;
}
static int js_closed_case_limits(const char *mode,const char *source,int tiny) {
  qaplet_process_options o;prepare(&o,mode);qaplet_config b=QAPLET_CONFIG_INIT;b.argv=o.argv;b.argc=o.argc;
  qaplet_m2_config m=QAPLET_M2_CONFIG_INIT;if(tiny){m.limits.operations=2;m.limits.operation_data_bytes=8;m.limits.io_block_bytes=4;}else if(!strcmp(mode,"--partial-input"))m.limits.io_block_bytes=1048576;qaplet_runtime *r=NULL;CHECK(qaplet_create_m2(&b,&m,&r,NULL)==QAPLET_OK);
  if(tiny){qaplet_work_handler h=QAPLET_WORK_HANDLER_INIT;h.name="quota";h.name_length=5;h.function=io_quota_work;h.input_bytes=3;h.output_bytes=1;CHECK(qaplet_register_work_handler(r,&h,NULL)==QAPLET_OK);}
  char message[1024];qaplet_diagnostic d=QAPLET_DIAGNOSTIC_INIT;d.message=message;d.capacity=sizeof(message);qaplet_progress p=QAPLET_PROGRESS_INIT;
  qaplet_code code=qaplet_eval(r,source,strlen(source),&p,&d);qaplet_drive_result drive=QAPLET_DRIVE_RESULT_INIT;if(!code)code=qaplet_run(r,&drive,&d);
  if(code)fprintf(stderr,"JS contract code=%d diagnostic=%s\n",code,message);
  qaplet_result result=QAPLET_RESULT_INIT;int passed=code==QAPLET_OK && drive.phase==QAPLET_PHASE_DONE && qaplet_get_result(r,&result,NULL)==QAPLET_OK && result.type==QAPLET_VALUE_DOUBLE && result.number==42;
  qaplet_io_snapshot io=QAPLET_IO_SNAPSHOT_INIT;qaplet_process_snapshot ps=QAPLET_PROCESS_SNAPSHOT_INIT;
  passed=passed && qaplet_get_io_snapshot(r,&io,NULL)==QAPLET_OK && !io.pending_handoffs && !io.shared_operations && !io.shared_data_bytes && qaplet_get_process_snapshot(r,&ps,NULL)==QAPLET_OK && !ps.referenced && !ps.retained_results;
  if(tiny)passed=passed && io.shared_operations_peak==2 && io.shared_data_bytes_peak==8;
  int cleanup=finish(r);CHECK(!cleanup);CHECK(passed);return 0;
}
static int js_closed_case(const char *mode,const char *source){return js_closed_case_limits(mode,source,0);}
static int js_io_cancel(int quiet) {
  const char *source="Promise.all([import('qaplet:process'),import('qaplet:runtime')]).then(async ([{spawn},{argv}])=>{"
    "const p=spawn(argv[0],{argv:[...argv]});function rejects(f,c){try{f()}catch(e){if(e.code===c)return;throw e}throw Error('missing refusal')}"
    "rejects(()=>p.stdout.read('1'),1);rejects(()=>p.stdout.read(0),1);rejects(()=>p.stdout.read(65537),13);"
    "rejects(()=>p.stdin.write(new Uint16Array(1)),8);rejects(()=>p.stdin.write(new Uint8Array(0)),1);rejects(()=>p.stdin.write(new Uint8Array(65537)),13);"
    "rejects(()=>p.stdout.read.call({},1),25);const t=p.stdout.read(1);t.cancel();t.cancel();"
    "const error=await t.promise.then(()=>{throw Error('cancel resolved')},e=>e);if(error.code!==28||error.errorDomain!==0||!(error.bytes instanceof Uint8Array)||error.bytes.length!==0)throw Error('cancel data');"
    "if((await p.stdout.read(1).promise).bytes[0]!==82)throw Error('cancel consumed bytes');p.cancel();p.cancel();"
    "const exit=await p.exited.then(()=>{throw Error('process cancel')},e=>e);if(exit.code!==28||!exit.reaped)throw Error('reap');"
    "p.stdin.close();p.stdout.close();p.stderr.close();rejects(()=>p.stdout.finish(),5);await p.closed;return 42})";
  CHECK(!js_closed_case("--hang",source));if(!quiet)puts("M2 process JS IO cancel: strict uncoerced byte endpoints; deterministic cancel-before-issue; copied read error bytes; no lost pipe data; actual child reap and closure: pass");return 0;
}
static int js_cycles(void) {
  for(unsigned i=0;i<1000;++i)CHECK(!js_basic(1));
  for(unsigned i=0;i<100;++i)CHECK(!js_io_cancel(1));
  puts("M2 process JS lifecycle: 1000 actual create/spawn/reap/close/quiesce/destroy; 100 mixed binary IO/cancel/strict-error cycles; native resources returned each cycle: pass");return 0;
}
#ifdef SDK_JS_START_FAULT
static _Atomic int js_fail_next_thread;
int __real_pthread_create(pthread_t *,const pthread_attr_t *,void *(*)(void *),void *);
int __wrap_pthread_create(pthread_t *thread,const pthread_attr_t *attr,void *(*function)(void *),void *userdata) {
  if(atomic_exchange(&js_fail_next_thread,0))return EAGAIN;
  return __real_pthread_create(thread,attr,function,userdata);
}
static qaplet_code js_arm_start(qaplet_runtime *r,uint64_t call,size_t argc,const qaplet_value *argv,void *opaque) {
  (void)r;(void)call;(void)argc;(void)argv;(void)opaque;atomic_store(&js_fail_next_thread,1);return QAPLET_OK;
}
static int js_start_fault(void) {
  CHECK(IsWindows());qaplet_process_options o;prepare(&o,"--hang");qaplet_config b=QAPLET_CONFIG_INIT;b.argv=o.argv;b.argc=o.argc;
  qaplet_m2_config m=QAPLET_M2_CONFIG_INIT;qaplet_runtime *r=NULL;CHECK(qaplet_create_m2(&b,&m,&r,NULL)==QAPLET_OK);
  qaplet_host_export item=QAPLET_HOST_EXPORT_INIT;item.name="arm";item.name_length=3;item.kind=QAPLET_HOST_FUNCTION;item.function=js_arm_start;
  qaplet_host_module module=QAPLET_HOST_MODULE_INIT;module.name="host:io-start";module.name_length=13;module.exports=&item;module.export_count=1;CHECK(qaplet_register_host_module(r,&module,NULL)==QAPLET_OK);
  char source[1800];snprintf(source,sizeof(source),"Promise.all([import('qaplet:process'),import('qaplet:runtime'),import('host:io-start')]).then(async ([{spawn},{argv},{arm}])=>{"
    "const p=spawn(argv[0],{argv:[...argv]});arm();try{p.stdout.read(1);throw Error('missing OS start refusal')}catch(e){if(e.code!==%d||e.errorDomain!==%u||e.error!==%d)throw Error('start error fields '+e.code+':'+e.errorDomain+':'+e.error)}"
    "try{p.stdout.read(0);throw Error('missing input refusal')}catch(e){if(e.code!==1||e.errorDomain!==0||e.error!==0)throw Error('stale OS error')}"
    "if((await p.stdout.read(1).promise).bytes[0]!==82)throw Error('recovery');p.stdin.finish();for(const endpoint of [p.stdout,p.stderr])for(;;){if((await endpoint.read(1).promise).eof)break}"
    "if((await p.exited).exitCode!==37)throw Error('exit');await p.closed;return 42})",QAPLET_IO_ERROR,QAPLET_ERROR_DOMAIN_ERRNO,EAGAIN);
  char message[1024];qaplet_diagnostic d=QAPLET_DIAGNOSTIC_INIT;d.message=message;d.capacity=sizeof(message);qaplet_progress p=QAPLET_PROGRESS_INIT;qaplet_drive_result drive=QAPLET_DRIVE_RESULT_INIT;
  qaplet_code code=qaplet_eval(r,source,strlen(source),&p,&d);if(!code)code=qaplet_run(r,&drive,&d);if(code)fprintf(stderr,"JS IO start code=%d diagnostic=%s\n",code,message);
  qaplet_io_snapshot io=QAPLET_IO_SNAPSHOT_INIT;int passed=code==QAPLET_OK && drive.phase==QAPLET_PHASE_DONE && qaplet_get_io_snapshot(r,&io,NULL)==QAPLET_OK && io.accepted==3 && !io.shared_operations && !io.shared_data_bytes;
  int cleanup=finish(r);CHECK(!cleanup);CHECK(passed && !atomic_load(&js_fail_next_thread));
  puts("M2 process JS IO start failure: controlled pthread EAGAIN; synchronous Error preserves original domain/value; atomic refusal; next IO recovers; actual native cleanup: pass");return 0;
}
#endif
static int js_liveness(void) {
  qaplet_process_options o;prepare(&o,"--hang");qaplet_config b=QAPLET_CONFIG_INIT;b.argv=o.argv;b.argc=o.argc;b.limits.engine_heap_bytes=1024*1024;
  qaplet_m2_config m=QAPLET_M2_CONFIG_INIT;qaplet_runtime *r=NULL;CHECK(qaplet_create_m2(&b,&m,&r,NULL)==QAPLET_OK);
  const char *source="Promise.all([import('qaplet:process'),import('qaplet:runtime')]).then(async ([{spawn},{argv}])=>{globalThis.p=spawn(argv[0],{argv:[...argv]});"
    "await p.stdout.read(1).promise;p.unref();globalThis.exited=p.exited;globalThis.closed=p.closed;globalThis.input=p.stdin;globalThis.out=p.stdout;globalThis.err=p.stderr;return 42})";
  qaplet_progress progress=QAPLET_PROGRESS_INIT;qaplet_drive_result drive=QAPLET_DRIVE_RESULT_INIT;CHECK(qaplet_eval(r,source,strlen(source),&progress,NULL)==QAPLET_OK && qaplet_run(r,&drive,NULL)==QAPLET_OK && drive.reason==QAPLET_DRIVE_IDLE && drive.phase==QAPLET_PHASE_DONE);
  qaplet_process_snapshot ps=QAPLET_PROCESS_SNAPSHOT_INIT;CHECK(qaplet_get_process_snapshot(r,&ps,NULL)==QAPLET_OK && ps.processes==1 && !ps.referenced);
  CHECK(qaplet_eval(r,"p.ref();42",10,&progress,NULL)==QAPLET_OK && qaplet_get_process_snapshot(r,&ps,NULL)==QAPLET_OK && ps.referenced==1);
  CHECK(qaplet_run_once(r,0,&drive,NULL)==QAPLET_OK && drive.reason==QAPLET_DRIVE_WAIT_TIMEOUT);
  source="p.unref();p=null;for(let i=0;i<100000;++i){let a={};a.self=a;}42";
  CHECK(qaplet_eval(r,source,strlen(source),&progress,NULL)==QAPLET_OK && qaplet_run(r,&drive,NULL)==QAPLET_OK && drive.reason==QAPLET_DRIVE_IDLE);
  CHECK(qaplet_get_process_snapshot(r,&ps,NULL)==QAPLET_OK && ps.processes==1 && !ps.referenced);
  qaplet_keepalive token=QAPLET_KEEPALIVE_INIT;CHECK(qaplet_keepalive_create(r,&token,NULL)==QAPLET_OK);
  source="input.write(new Uint8Array([1])).promise.then(async()=>{input.finish();out.close();err.close();if((await exited).exitCode!==37)throw Error('GC cancelled');await closed;return 42})";
  CHECK(qaplet_eval(r,source,strlen(source),&progress,NULL)==QAPLET_OK);double until=milliseconds()+5000;
  do{CHECK(qaplet_run_once(r,1000,&drive,NULL)==QAPLET_OK);if(drive.phase==QAPLET_PHASE_DONE)break;}while(milliseconds()<until);
  qaplet_result result=QAPLET_RESULT_INIT;CHECK(drive.phase==QAPLET_PHASE_DONE && qaplet_get_result(r,&result,NULL)==QAPLET_OK && result.type==QAPLET_VALUE_DOUBLE && result.number==42);
  CHECK(qaplet_keepalive_release(r,&token,NULL)==QAPLET_OK && qaplet_run(r,&drive,NULL)==QAPLET_OK && drive.reason==QAPLET_DRIVE_IDLE);
  CHECK(!finish(r));puts("M2 process JS liveness: unref natural idle; ref restores keepalive; discarded Process survives bounded cyclic GC pressure; explicit owner drive observes natural exit and closure: pass");return 0;
}
static int js_limits(void) {
  const char *source="Promise.all([import('qaplet:process'),import('qaplet:runtime'),import('qaplet:work')]).then(async ([{spawn},{argv},{submit}])=>{"
    "const p=spawn(argv[0],{argv:[...argv]});const w=submit('quota',new Uint8Array([65,66,67])),a=p.stdout.read(4);"
    "try{p.stdin.write(new Uint8Array([1]));throw Error('count')}catch(e){if(e.code!==24)throw e}"
    "try{p.stdout.read(1);throw Error('busy')}catch(e){if(e.code!==4)throw e}a.cancel();p.cancel();"
    "if((await w.promise)[0]!==87)throw Error('work');const errors=await Promise.all([a.promise.catch(e=>e),p.exited.catch(e=>e)]);if(errors.some(e=>e.code!==28))throw Error('cancel');"
    "p.stdin.close();p.stdout.close();p.stderr.close();await p.closed;return 42})";
  CHECK(!js_closed_case_limits("--hang",source,1));puts("M2 process JS limits: Work/IO share two operations/eight reserved bytes; atomic count/data refusal; BUSY priority; cancelled reservations retained through owner handoff then released: pass");return 0;
}
static int js_terminal(void) {
  for(unsigned failed=0;failed<6;++failed){qaplet_process_options o;prepare(&o,"--hang");qaplet_config b=QAPLET_CONFIG_INIT;b.argv=o.argv;b.argc=o.argc;
    if(failed==1 || failed==3){b.limits.pump_jobs=1;b.limits.execution_dispatches=64;}if(failed==2)b.limits.engine_heap_bytes=1024*1024;
    qaplet_m2_config m=QAPLET_M2_CONFIG_INIT;qaplet_runtime *r=NULL;CHECK(qaplet_create_m2(&b,&m,&r,NULL)==QAPLET_OK);
    unsigned marks=0;qaplet_host_export item=QAPLET_HOST_EXPORT_INIT;item.name="mark";item.name_length=4;item.kind=QAPLET_HOST_FUNCTION;item.function=cleanup_mark;item.userdata=&marks;
    qaplet_host_module module=QAPLET_HOST_MODULE_INIT;module.name="host:process-cleanup";module.name_length=20;module.exports=&item;module.export_count=1;CHECK(qaplet_register_host_module(r,&module,NULL)==QAPLET_OK);
    const char *setup="Promise.all([import('qaplet:process'),import('qaplet:runtime'),import('host:process-cleanup')]).then(async ([{spawn},{argv},h])=>{"
      "globalThis.p=spawn(argv[0],{argv:[...argv]});p.exited.then(()=>h.mark(),()=>h.mark()).finally(()=>h.mark());await p.stdout.read(1).promise;"
      "globalThis.h=h;globalThis.t=p.stdout.read(1);t.promise.then(()=>h.mark(),()=>h.mark()).finally(()=>h.mark());return 42})";
    qaplet_progress progress=QAPLET_PROGRESS_INIT;qaplet_drive_result drive=QAPLET_DRIVE_RESULT_INIT;
    CHECK(qaplet_eval(r,setup,strlen(setup),&progress,NULL)==QAPLET_OK);
    double until=milliseconds()+5000;do{CHECK(qaplet_run_once(r,1000,&drive,NULL)==QAPLET_OK);qaplet_result result=QAPLET_RESULT_INIT;if(drive.phase==QAPLET_PHASE_DONE && qaplet_get_result(r,&result,NULL)==QAPLET_OK && result.type==QAPLET_VALUE_DOUBLE && result.number==42)break;}while(milliseconds()<until);
    CHECK(drive.phase==QAPLET_PHASE_DONE && !marks);
    qaplet_code code;
    if(!failed){CHECK(qaplet_request_stop(r,NULL)==QAPLET_OK);code=qaplet_run_once(r,0,&drive,NULL);CHECK(code==QAPLET_EXECUTION_STOPPED);}
    else {const char *bad=failed==1 ? "const u=p.stderr.read(1);u.promise.catch(()=>{function again(){Promise.resolve().then(again)}again()});u.cancel();new Promise(()=>{})" : failed==3 ? "const u=p.stderr.read(1);u.promise.catch(()=>{function again(){Promise.resolve().then(again)}again()});u.cancel();42" : failed>=4 ? "const u=p.stderr.read(1);u.promise.catch(()=>{throw Error('ordinary callback')});u.cancel();42" : "try{globalThis.hog=[];for(;;)hog.push(new Uint8Array(65536))}catch(e){};42";
      code=qaplet_eval(r,bad,strlen(bad),&progress,NULL);if(failed==5 && !code)code=qaplet_run(r,&drive,NULL);else for(unsigned i=0;i<80 && !code;++i)code=qaplet_pump(r,&progress,NULL);CHECK(code==(failed==2 ? QAPLET_MEMORY_LIMIT : failed>=4 ? QAPLET_JS_EXCEPTION : QAPLET_DISPATCH_LIMIT));}
    qaplet_snapshot snapshot=QAPLET_SNAPSHOT_INIT;CHECK(qaplet_get_snapshot(r,&snapshot,NULL)==QAPLET_OK && snapshot.termination_reason==(failed>=4 ? QAPLET_OK : code) && snapshot.lifecycle==(failed>=4 ? QAPLET_OPEN : failed ? QAPLET_FAILED : QAPLET_STOPPED));
    if(failed==1 || failed==3)CHECK(snapshot.execution_dispatches==64);
    qaplet_process_snapshot ps=QAPLET_PROCESS_SNAPSHOT_INIT;CHECK(qaplet_get_process_snapshot(r,&ps,NULL)==QAPLET_OK && ps.referenced==(failed>=4 ? 1 : 0) && !marks);
    if(failed>=4){qaplet_result result=QAPLET_RESULT_INIT;CHECK(qaplet_eval(r,"42",2,&progress,NULL)==QAPLET_OK && qaplet_get_result(r,&result,NULL)==QAPLET_OK && result.type==QAPLET_VALUE_DOUBLE && result.number==42);}
    CHECK(qaplet_close(r,NULL)==QAPLET_OK && qaplet_close(r,NULL)==QAPLET_OK && !marks);qaplet_m2_snapshot closed=QAPLET_M2_SNAPSHOT_INIT;CHECK(qaplet_get_m2_snapshot(r,&closed,NULL)==QAPLET_OK && closed.lifecycle==QAPLET_CLOSED && !closed.engine_current_bytes);
    CHECK(!finish(r) && !marks);
  }
  puts("M2 process JS terminal: owner stop/dispatch/caught allocation failure cancels child and IO before close; ordinary callback error recovers; no then/catch/finally after termination; CLOSED engine released; actual quiesce/destroy: pass");return 0;
}
static int js_lifetime(void) {
  const char *source="Promise.all([import('qaplet:process'),import('qaplet:runtime')]).then(async ([{spawn},{argv}])=>{"
    "const p=spawn(argv[0],{argv:[...argv]});let seen=false;p.exited.then(()=>{seen=true});"
    "const out=await p.stdout.read(1).promise,err=await p.stderr.read(1).promise;"
    "if(!out.eof||!err.eof||seen||!p.hasRef())throw Error('EOF before exit');p.stdin.finish();p.stdin.finish();"
    "if((await p.exited).exitCode!==37)throw Error('exit');await p.closed;"
    "const args=[...argv];args[args.indexOf('--eof-before-exit')]='--cancel-output';const q=spawn(args[0],{argv:args});"
    "const first=await q.stdout.read(1).promise;if(first.bytes[0]!==82)throw Error('ready');q.cancel();q.cancel();"
    "const cancelled=await q.exited.then(()=>{throw Error('cancel resolved')},e=>{if(e.code!==28||e.reaped!==true)throw Error('cancel facts');return e});"
    "if(!q.hasRef())throw Error('unread ref');q.stdin.finish();let all=[];for(;;){const x=await q.stdout.read(8).promise;all.push(...x.bytes);if(x.eof)break}"
    "if(all.join(',')!=='0,1,255,84,10')throw Error('remaining output');q.stderr.close();q.stderr.close();await q.closed;return 42})";
  CHECK(!js_closed_case("--eof-before-exit",source));puts("M2 process JS lifetime: EOF before exit; cancel before actual reap; native facts; unread binary output after exit; ref through managed closure; repeated close/cancel: pass");return 0;
}
static int js_backpressure(void) {
  const char *source="Promise.all([import('qaplet:process'),import('qaplet:runtime')]).then(async ([{spawn},{argv}])=>{"
    "const p=spawn(argv[0],{argv:[...argv]});let exited=false;p.exited.then(()=>{exited=true});p.stdin.finish();"
    "const ready=await p.stderr.read(1).promise;if(ready.bytes[0]!==82||exited)throw Error('producer gate');"
    "let total=0;for(;;){const x=await p.stdout.read(65536).promise;for(const b of x.bytes){if(b!==total%256)throw Error('order');++total}if(x.eof)break}"
    "if(total!==1048576)throw Error('bounded output');let errors=[];for(;;){const x=await p.stderr.read(2).promise;errors.push(...x.bytes);if(x.eof)break}"
    "if(errors.join(',')!=='68'||(await p.exited).exitCode!==37)throw Error('stderr/exit');await p.closed;return 42})";
  CHECK(!js_closed_case("--flood",source));puts("M2 process JS backpressure: controlled producer; no implicit collection; independent stderr; ordered 1MiB binary pull; bounded chunks and EOF: pass");return 0;
}
static int js_partial(void) {
  const char *source="Promise.all([import('qaplet:process'),import('qaplet:runtime')]).then(async ([{spawn},{argv}])=>{"
    "const p=spawn(argv[0],{argv:[...argv]});if((await p.stdout.read(1).promise).bytes[0]!==82)throw Error('ready');"
    "const w=p.stdin.write(new Uint8Array(1048576).fill(90));const e=await w.promise.then(()=>{throw Error('missing partial error')},e=>e);"
    "if(e.code!==30||!e.errorDomain||!e.error||e.bytesWritten<4096||e.bytesWritten>=1048576)throw Error('partial bytes '+e.bytesWritten);"
    "await p.stdin.closed;for(const endpoint of [p.stdout,p.stderr])for(;;){if((await endpoint.read(16).promise).eof)break}"
    "if((await p.exited).exitCode!==37)throw Error('consumption');await p.closed;try{p.cancel();throw Error('completion cancel')}catch(e){if(e.code!==29)throw e}return 42})";
  CHECK(!js_closed_case("--partial-input",source));puts("M2 process JS partial write: controlled 4096-byte consumer; stable native error and actual accepted byte count; completion-first cancel; ordinary error cleanup: pass");return 0;
}
static int js_inputs(void) {
  qaplet_process_options o;prepare(&o,"--args");arguments[o.argc++]=(qaplet_string){native_cwd,strlen(native_cwd)};
  CHECK(!setenv("QAPLET_SNAPSHOT","frozen",1) && !unsetenv("QAPLET_AFTER_CREATE"));
  qaplet_config b=QAPLET_CONFIG_INIT;b.argv=arguments;b.argc=o.argc;qaplet_m2_config m=QAPLET_M2_CONFIG_INIT;qaplet_runtime *r=NULL;
  CHECK(qaplet_create_m2(&b,&m,&r,NULL)==QAPLET_OK);CHECK(!setenv("QAPLET_SNAPSHOT","changed",1) && !setenv("QAPLET_AFTER_CREATE","later",1));
  const char *source="Promise.all([import('qaplet:process'),import('qaplet:runtime')]).then(async ([{spawn},{argv}])=>{"
    "const cwd=argv[argv.length-1],args=argv.slice(0,-1),ignore=['ignore','ignore','ignore'];"
    "function rejects(f,c){try{f()}catch(e){if(e.code===c)return;throw e}throw Error('missing refusal '+c)}"
    "rejects(()=>spawn(1),1);rejects(()=>spawn(String.fromCharCode(0xd800)),12);rejects(()=>spawn('a'+String.fromCharCode(0)),1);"
    "rejects(()=>spawn(args[0],{argv:[1]}),1);rejects(()=>spawn(args[0],{stdio:['ignore','ignore','ignore','ignore']}),1);"
    "try{spawn('qaplet-missing-explicit-path',{env:[],stdio:ignore});throw Error('missing OS error')}catch(e){if(e.code!==27||!e.errorDomain||!e.error)throw e}"
    "const p=spawn(args[0],{argv:args,cwd,env:{QAPLET_ONLY:'完整 值',QAPLET_CWD:cwd},stdio:ignore});"
    "args.fill('changed');if((await p.exited).exitCode!==37)throw Error('snapshot');await p.closed;"
    "const saved=argv.slice(0,-1);saved[saved.indexOf('--args')]='--snapshot';const q=spawn(saved[0],{argv:saved,stdio:ignore});"
    "if((await q.exited).exitCode!==37)throw Error('factory env');await q.closed;const inherited=saved.map(x=>x==='--snapshot'?'--empty':x);"
    "const h=spawn(inherited[0],{argv:inherited,env:{},stdio:['inherit','inherit','inherit']});if(h.stdin!==null||h.stdout!==null||h.stderr!==null)throw Error('inherit endpoints');"
    "if((await h.exited).exitCode!==37)throw Error('inherit exit');await h.closed;return 42})";
  char message[1024];qaplet_diagnostic d=QAPLET_DIAGNOSTIC_INIT;d.message=message;d.capacity=sizeof(message);qaplet_progress p=QAPLET_PROGRESS_INIT;
  qaplet_code code=qaplet_eval(r,source,strlen(source),&p,&d);qaplet_drive_result drive=QAPLET_DRIVE_RESULT_INIT;if(!code)code=qaplet_run(r,&drive,&d);
  if(code)fprintf(stderr,"JS inputs code=%d diagnostic=%s\n",code,message);qaplet_result result=QAPLET_RESULT_INIT;int passed=code==QAPLET_OK && drive.phase==QAPLET_PHASE_DONE && qaplet_get_result(r,&result,NULL)==QAPLET_OK && result.type==QAPLET_VALUE_DOUBLE && result.number==42;
  int cleanup=finish(r);CHECK(!cleanup);CHECK(!unsetenv("QAPLET_SNAPSHOT") && !unsetenv("QAPLET_AFTER_CREATE"));CHECK(passed);
  puts("M2 process JS inputs: strict path/options/UTF-8; copied Unicode argv and cwd; complete environment mapping and factory snapshot; ordinary rejection cleanup: pass");return 0;
}
static int js_bytes(int after) {
  qaplet_process_options o;prepare(&o,"--pipe");qaplet_config b=QAPLET_CONFIG_INIT;b.argv=o.argv;b.argc=o.argc;
  arguments[b.argc++]=(qaplet_string){after ? "after" : "before",after ? 5 : 6};
  qaplet_m2_config m=QAPLET_M2_CONFIG_INIT;qaplet_runtime *r=NULL;CHECK(qaplet_create_m2(&b,&m,&r,NULL)==QAPLET_OK);
  const char *source="Promise.all([import('qaplet:process'),import('qaplet:runtime')]).then(async ([{spawn},{argv}])=>{"
    "const p=spawn(argv[0],{argv:[...argv]});let a=p.stdout.read(64),b=p.stderr.read(64);"
    "try{p.stdout.read(1);throw Error('missing BUSY')}catch(e){if(e.code!==4)throw e}"
    "const input=new Uint8Array([73,0,255,78,10]),w=p.stdin.write(input);input.fill(0);const after=argv[argv.length-1]==='after';if(!after)p.stdin.finish();"
    "if((await w.promise).bytesWritten!==5)throw Error('write');if(after)p.stdin.finish();"
    "async function drain(e,first){let all=[];for(;;){const x=await first.promise;all.push(...x.bytes);if(x.eof)return all;first=e.read(64)}}"
    "const [out,err]=await Promise.all([drain(p.stdout,a),drain(p.stderr,b)]);"
    "if(out.join(',')!=='73,0,255,78,10'||err.join(',')!=='69,0,254,82')throw Error('binary');"
    "if((await p.exited).exitCode!==37)throw Error('exit');await p.closed;"
    "try{w.cancel();throw Error('completed cancel')}catch(e){if(e.code!==29)throw e}return 42}).catch(e=>{throw Error(JSON.stringify({code:e.code,native:e.error,domain:e.errorDomain,stack:e.stack}))})";
  char message[1024];qaplet_diagnostic d=QAPLET_DIAGNOSTIC_INIT;d.message=message;d.capacity=sizeof(message);
  qaplet_progress p=QAPLET_PROGRESS_INIT;qaplet_code code=qaplet_eval(r,source,strlen(source),&p,&d);qaplet_drive_result drive=QAPLET_DRIVE_RESULT_INIT;
  if(!code)code=qaplet_run(r,&drive,&d);if(code)fprintf(stderr,"JS bytes code=%d diagnostic=%s\n",code,message);
  qaplet_result result=QAPLET_RESULT_INIT;int passed=code==QAPLET_OK && drive.phase==QAPLET_PHASE_DONE && qaplet_get_result(r,&result,NULL)==QAPLET_OK && result.type==QAPLET_VALUE_DOUBLE && result.number==42;
  if(!passed){int cleanup=finish(r);CHECK(!cleanup);CHECK(passed);}
  qaplet_io_snapshot io=QAPLET_IO_SNAPSHOT_INIT;CHECK(qaplet_get_io_snapshot(r,&io,NULL)==QAPLET_OK && !io.pending_handoffs && !io.shared_operations && !io.shared_data_bytes);
  qaplet_process_snapshot ps=QAPLET_PROCESS_SNAPSHOT_INIT;CHECK(qaplet_get_process_snapshot(r,&ps,NULL)==QAPLET_OK && !ps.referenced && !ps.retained_results);
  CHECK(!finish(r));puts("M2 process JS bytes: default three pipes; copied binary write; concurrent stdout/stderr pull; BUSY; EOF; separate exit and close; automatic result retirement: pass");return 0;
}
static int child(int argc,char **argv) {
  if (argc<4) return 98;
  const char *mode=argv[2];
  if(!strcmp(mode,"--eof-before-exit")){CHECK(!close(1) && !close(2));closefrom(3);char byte;while(read(0,&byte,1)>0){}return 37;}
  if(!strcmp(mode,"--cancel-output")){const unsigned char data[]={'R',0,1,255,'T','\n'};CHECK(write(1,data,sizeof(data))==sizeof(data));char byte;while(read(0,&byte,1)>0){}return 37;}
  if(!strcmp(mode,"--flood")){
    unsigned char data[4096];for(size_t i=0;i<sizeof(data);++i)data[i]=i%256;
    CHECK(write(2,"R",1)==1);
    for(unsigned block=0;block<256;++block){size_t offset=0;while(offset<sizeof(data)){ssize_t n=write(1,data+offset,sizeof(data)-offset);if(n<=0)return 98;offset+=n;}}
    return write(2,"D",1)==1 ? 37 : 98;
  }
  if(!strcmp(mode,"--closed-input")){CHECK(!close(0) && write(1,"R",1)==1);return 37;}
  if(!strcmp(mode,"--partial-input")){
    unsigned char data[4096];size_t total=0;
    /* Bound this actual Linux producer/consumer fixture below the request.
     * A full write into a 64K kernel pipe is also valid OS acceptance, so it
     * cannot prove the partial-error contract without this explicit capacity. */
    /* Linux UAPI F_LINUX_SPECIFIC_BASE(1024)+7; the pinned portable header
     * omits this Linux-only fixture control. Use one actual kernel page so
     * this fixture also works on a 64K kernel. Its public IO block override
     * is 1MiB, greater than that capacity. Never use this command on another OS. */
    if(IsLinux()){long page=sysconf(_SC_PAGESIZE);CHECK(page>0 && fcntl(0,1031,page)==page);}
    CHECK(write(1,"R",1)==1);
    while(total<sizeof(data)){ssize_t n=read(0,data+total,sizeof(data)-total);if(n<=0)return 98;total+=n;}
    for(size_t i=0;i<sizeof(data);++i)if(data[i]!=0x5a)return 98;
    return close(0) ? 98 : 37;
  }
  if (!strcmp(mode,"--inherit") || !strcmp(mode,"--pipe")) {
    if(!strcmp(mode,"--inherit")) {
    const char *cursor=argv[3];unsigned count=0;
    do {
      char *end;long fd=strtol(cursor,&end,10);
      if (end==cursor || fd<3 || fd>INT_MAX || (*end && *end!=',')) return 98;
      errno=0;int flags=fcntl(fd,F_GETFD),cached_error=errno;char probe;
      errno=0;int observed=read(fd,&probe,1),native_error=errno;
      if (flags!=-1 || cached_error!=EBADF || observed!=-1 || native_error!=EBADF) {
        fprintf(stderr,"inherited descriptor: fd=%ld flags=%d errno=%d read=%d errno=%d\n",fd,flags,cached_error,observed,native_error);return 98;
      }
      ++count;cursor=*end ? end+1 : end;
    } while (*cursor);
    if (count!=3) return 98;
    }
    const unsigned char expected[]={'I',0,255,'N','\n'},errors[]={'E',0,254,'R'};
    unsigned char data[16]; ssize_t n=read(0,data,sizeof(data));
    if (n!=sizeof(expected) || memcmp(data,expected,sizeof(expected)) || read(0,data,sizeof(data))!=0) return 98;
    return write(1,expected,sizeof(expected))==sizeof(expected) && write(2,errors,sizeof(errors))==sizeof(errors) ? 37 : 98;
  }
  if (!strcmp(mode,"--hang")) { CHECK(write(1,"R",1)==1); char byte; while (read(0,&byte,1)>0) {} return 37; }
  if (!strcmp(mode,"--empty")) { extern char **environ; return environ[0] ? 98 : 37; }
  if (!strcmp(mode,"--snapshot")) return getenv("QAPLET_SNAPSHOT") && !strcmp(getenv("QAPLET_SNAPSHOT"),"frozen") && !getenv("QAPLET_AFTER_CREATE") ? 37 : 98;
  if (argc!=10) return 98;
  for (int i=0;i<7;++i) if (strcmp(argv[i+3],special[i])) return 98;
  extern char **environ; size_t n=0; while(environ[n]) ++n;
  char actual[PATH_MAX]; char byte;
  return n==2 && getenv("QAPLET_ONLY") && !strcmp(getenv("QAPLET_ONLY"),"完整 值") && getenv("QAPLET_CWD") && getcwd(actual,sizeof(actual)) && !strcmp(actual,getenv("QAPLET_CWD")) && read(0,&byte,1)==0 ? 37 : 98;
}
int sdk_process(int argc,char **argv) {
  if (argc>1 && !strcmp(argv[1],"--m2-process-child")) return child(argc,argv);
  CHECK(argc==4); self=argv[2]; launcher=argv[3];
  char original[PATH_MAX]; CHECK(getcwd(original,sizeof(original)));
  const char *temporary=getenv("TMPDIR"); if (!temporary) temporary=getenv("TEMP"); if (!temporary) temporary="/tmp";
  snprintf(root,sizeof(root),"%s/qaplet-process-参数-XXXXXX",temporary); if (!mkdtemp(root)) { fprintf(stderr,"fixture root=%s windows=%d\n",root,IsWindows()); return 1; } CHECK(realpath(root,cwd));
  CHECK(!chdir(cwd)); CHECK(getcwd(cwd,sizeof(cwd)));
  if (IsWindows()) { char16_t wide[PATH_MAX]; extern uint32_t GetCurrentDirectory(uint32_t,char16_t *); CHECK(GetCurrentDirectory(PATH_MAX,wide)); tprecode16to8(native_cwd,sizeof(native_cwd),wide); }
  else strcpy(native_cwd,cwd);
  CHECK(!chdir(original));
  snprintf(executable,sizeof(executable),"%s/子程序 空格%s",cwd,IsWindows() ? ".exe" : "");
  char source[PATH_MAX];
  if (IsWindows()) { strcpy(source,self); char *slash=strrchr(source,'/'); if (!slash) slash=strrchr(source,'\\'); CHECK(slash); strcpy(slash+1,"sdk/process-child.exe"); }
  else strcpy(source,launcher);
  CHECK(!copy_file(source,executable));
#ifdef SDK_JS_START_FAULT
  if (!strcmp(argv[1],"--m2-process-js-start-fault")) {int result=js_start_fault();CHECK(!unlink(executable));CHECK(!rmdir(cwd));return result;}
#endif
  if (!strcmp(argv[1],"--m2-process-js-cycles")) {int result=js_cycles();CHECK(!unlink(executable));CHECK(!rmdir(cwd));return result;}
  if (!strcmp(argv[1],"--m2-process-js-liveness")) {int result=js_liveness();CHECK(!unlink(executable));CHECK(!rmdir(cwd));return result;}
  if (!strcmp(argv[1],"--m2-process-js-io-cancel")) {int result=js_io_cancel(0);CHECK(!unlink(executable));CHECK(!rmdir(cwd));return result;}
  if (!strcmp(argv[1],"--m2-process-js-limits")) {int result=js_limits();CHECK(!unlink(executable));CHECK(!rmdir(cwd));return result;}
  if (!strcmp(argv[1],"--m2-process-js-terminal")) {int result=js_terminal();CHECK(!unlink(executable));CHECK(!rmdir(cwd));return result;}
  if (!strcmp(argv[1],"--m2-process-js-lifetime")) {int result=js_lifetime();CHECK(!unlink(executable));CHECK(!rmdir(cwd));return result;}
  if (!strcmp(argv[1],"--m2-process-js-backpressure")) {int result=js_backpressure();CHECK(!unlink(executable));CHECK(!rmdir(cwd));return result;}
  if (!strcmp(argv[1],"--m2-process-js-partial")) {int result=js_partial();CHECK(!unlink(executable));CHECK(!rmdir(cwd));return result;}
  if (!strcmp(argv[1],"--m2-process-js-finish-after")) {int result=js_bytes(1);CHECK(!unlink(executable));CHECK(!rmdir(cwd));return result;}
  if (!strcmp(argv[1],"--m2-process-js-inputs")) {int result=js_inputs();CHECK(!unlink(executable));CHECK(!rmdir(cwd));return result;}
  if (!strcmp(argv[1],"--m2-process-js-issue")) {int result=js_issue();CHECK(!unlink(executable));CHECK(!rmdir(cwd));return result;}
  if (!strcmp(argv[1],"--m2-process-js-bytes")) {
    int result=js_bytes(0); CHECK(!unlink(executable)); CHECK(!rmdir(cwd)); return result;
  }
  if (!strcmp(argv[1],"--m2-process-js-basic")) {
    int result=js_basic(0); CHECK(!unlink(executable)); CHECK(!rmdir(cwd)); return result;
  }
  if (!strcmp(argv[1],"--m2-process-pipe-lifetime")) {
    int result=pipe_lifetime(); CHECK(!unlink(executable)); CHECK(!rmdir(cwd)); return result;
  }
  if (!strcmp(argv[1],"--m2-process-pipe-backpressure")) {
    int result=pipe_backpressure(); CHECK(!unlink(executable)); CHECK(!rmdir(cwd)); return result;
  }
  if (!strcmp(argv[1],"--m2-process-pipe-shared-limits")) {
    int result=pipe_shared_limits(); CHECK(!unlink(executable)); CHECK(!rmdir(cwd)); return result;
  }
  if (!strcmp(argv[1],"--m2-process-pipe-control")) {
    int result=pipe_control(); CHECK(!unlink(executable)); CHECK(!rmdir(cwd)); return result;
  }
  if (!strcmp(argv[1],"--m2-process-pipe-write-failure")) {
    int result=pipe_write_failure(); CHECK(!unlink(executable)); CHECK(!rmdir(cwd)); return result;
  }
  if (!strcmp(argv[1],"--m2-process-pipe-bytes")) {
    int result=pipe_bytes(); CHECK(!unlink(executable)); CHECK(!rmdir(cwd)); return result;
  }
  if (!strcmp(argv[1],"--m2-process-pipe-defaults")) {
    int result=pipe_defaults(); CHECK(!unlink(executable)); CHECK(!rmdir(cwd)); return result;
  }
  int result=!strcmp(argv[1],"--m2-process-terminal") ? terminal_cleanup() : !strcmp(argv[1],"--m2-process-cancel") ? cancellation() : !strcmp(argv[1],"--m2-process-query") ? query() : !strcmp(argv[1],"--m2-process-inputs") ? inputs() : !strcmp(argv[1],"--m2-process-errors") ? errors() : !strcmp(argv[1],"--m2-process-inherit") ? inherit() : !strcmp(argv[1],"--m2-process-limits") ? limits() : !strcmp(argv[1],"--m2-process-ledger") ? ledger() : !strcmp(argv[1],"--m2-process-scheduling") ? scheduling() : !strcmp(argv[1],"--m2-process-liveness") ? liveness() : !strcmp(argv[1],"--m2-process-identity") ? identity() : cleanup();
  CHECK(!unlink(executable)); CHECK(!rmdir(cwd)); return result;
}
#ifdef SDK_STANDALONE
int main(int argc,char **argv) { return sdk_process(argc,argv); }
#endif
