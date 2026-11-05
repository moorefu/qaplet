/* SPDX-License-Identifier: MIT */
/* Actual public SDK plus separate controlled system inputs. No private headers. */
#define _COSMO_SOURCE 1
#include "qaplet.h"
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <sched.h>
#include <signal.h>
#include <stdbool.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include "libc/dce.h"
#include "libc/nt/runtime.h"
#include "libc/nt/events.h"
#include "libc/nt/synchronization.h"
#include "libc/nt/struct/overlapped.h"
extern bool32 ReadFile(int64_t,void *,uint32_t,uint32_t *,struct NtOverlapped *);
extern bool32 WriteFile(int64_t,const void *,uint32_t,uint32_t *,struct NtOverlapped *);
extern bool32 GetOverlappedResult(int64_t,struct NtOverlapped *,uint32_t *,bool32);
extern void SetLastError(uint32_t);
extern ssize_t __real_read(int,void *,size_t);
extern ssize_t __real_write(int,const void *,size_t);
extern int __real_close(int);
extern bool32 __real_ReadFile(int64_t,void *,uint32_t,uint32_t *,struct NtOverlapped *);
extern bool32 __real_WriteFile(int64_t,const void *,uint32_t,uint32_t *,struct NtOverlapped *);
extern bool32 __real_CloseHandle(int64_t);
extern bool32 __real_GetExitCodeProcess(int64_t,uint32_t *);
extern uint32_t __real_WaitForSingleObject(int64_t,uint32_t);
extern void *__real_calloc(size_t,size_t);
extern int __real_pthread_create(pthread_t *,const pthread_attr_t *,void *(*)(void *),void *);
#define CHECK(c) do{if(!(c)){fprintf(stderr,"I/O system boundary line %d: %s errno=%d\n",__LINE__,#c,errno);return 1;}}while(0)
static double milliseconds(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return t.tv_sec*1000.0+t.tv_nsec/1000000.0;}
static _Atomic int fail_alloc,write_stage,watch_read,fail_thread,fail_send,fail_wait;
static _Atomic int64_t wait_event;
static int64_t host_event;
static void notify_host(void *unused){(void)unused;SetEvent(host_event);}
extern ssize_t __real_send(int,const void *,size_t,int);
ssize_t __wrap_send(int fd,const void *bytes,size_t n,int flags){if(atomic_exchange(&fail_send,0)){errno=EIO;return -1;}return __real_send(fd,bytes,n,flags);}
static int64_t observed_native=-1,previous_native=-2;static int native_closed,native_reused;static size_t real_prefix;
static int recycle_native,external_pipe[2]={-1,-1};
static int64_t recycled_events[8192],replacement_event;static unsigned recycled_count;
void *__wrap_calloc(size_t n,size_t size){return atomic_load(&fail_alloc) ? NULL : __real_calloc(n,size);}
ssize_t __wrap_read(int fd,void *data,size_t n){if(atomic_load(&watch_read)){observed_native=fd;if(fd==previous_native && native_closed)native_reused=1;}return __real_read(fd,data,n);}
bool32 __wrap_ReadFile(int64_t h,void *data,uint32_t n,uint32_t *got,struct NtOverlapped *o){if(atomic_load(&watch_read)){observed_native=h;if(h==previous_native && native_closed)native_reused=1;}int ok=__real_ReadFile(h,data,n,got,o);if(!ok && GetLastError()==997 && o && atomic_load(&fail_wait))atomic_store(&wait_event,o->hEvent);return ok;}
uint32_t __wrap_WaitForSingleObject(int64_t h,uint32_t ms){if(h==atomic_load(&wait_event) && atomic_exchange(&fail_wait,0)){SetLastError(6);return 0xffffffffu;}return __real_WaitForSingleObject(h,ms);}
int __wrap_close(int fd){
  int result=__real_close(fd);
  if(!result && fd==previous_native){native_closed=1;
    if(recycle_native){recycle_native=0;
      if(!pipe2(external_pipe,O_CLOEXEC)){
        if(external_pipe[1]==fd){int copy=fcntl(external_pipe[1],F_DUPFD_CLOEXEC,3);if(copy>=0){__real_close(external_pipe[1]);external_pipe[1]=copy;}}
        if(external_pipe[0]!=fd && fcntl(fd,F_GETFD)<0 && errno==EBADF && dup2(external_pipe[0],fd)==fd){__real_close(external_pipe[0]);external_pipe[0]=fd;}
        if(external_pipe[0]==fd && fcntl(fd,F_SETFL,O_NONBLOCK)==0 && __real_write(external_pipe[1],"Z",1)==1)native_reused=1;
      }
    }
  }return result;
}
bool32 __wrap_CloseHandle(int64_t h){
  int result=__real_CloseHandle(h);
  if(result && h==previous_native){native_closed=1;
    if(recycle_native){recycle_native=0;
      /* The old number is reused by the real OS before the old helper can
       * allocate TLS control handles. Retain the replacement through join. */
      while(recycled_count<8192 && !replacement_event){int64_t event=CreateEvent(NULL,true,false,NULL);if(!event)break;recycled_events[recycled_count++]=event;if(event==h){replacement_event=event;native_reused=1;}}
    }
  }return result;
}
ssize_t __wrap_write(int fd,const void *data,size_t n){
  int stage=atomic_load(&write_stage);
  if(stage && n && ((const unsigned char *)data)[0]==0x5a){
    if(stage==1){ssize_t got=__real_write(fd,data,n>1024 ? 1024 : n);if(got>0){real_prefix+=got;atomic_store(&write_stage,2);}return got;}
    errno=EPIPE;return -1;
  }
  return __real_write(fd,data,n);
}
bool32 __wrap_WriteFile(int64_t h,const void *data,uint32_t n,uint32_t *got,struct NtOverlapped *o){
  int stage=atomic_load(&write_stage);
  if(stage && n && ((const unsigned char *)data)[0]==0x5a){
    if(stage==1){int ok=__real_WriteFile(h,data,n>1024 ? 1024 : n,got,o);if(!ok && GetLastError()==997)ok=GetOverlappedResult(h,o,got,true);if(ok){real_prefix+=*got;atomic_store(&write_stage,2);}return ok;}
    *got=0;SetLastError(109);return false;
  }
  return __real_WriteFile(h,data,n,got,o);
}
static pthread_key_t key;static pthread_mutex_t gate_mutex=PTHREAD_MUTEX_INITIALIZER;static pthread_cond_t gate_condition=PTHREAD_COND_INITIALIZER;
static int gate_entered,gate_released;static _Atomic int gate_next;
static _Atomic int hold_exit;static pthread_t owner_thread;
static void *(*gated_entry)(void *);static void *gated_argument;
static void destructor(void *unused){(void)unused;pthread_mutex_lock(&gate_mutex);gate_entered=1;pthread_cond_broadcast(&gate_condition);while(!gate_released)pthread_cond_wait(&gate_condition,&gate_mutex);pthread_mutex_unlock(&gate_mutex);}
static void *trampoline(void *unused){(void)unused;pthread_setspecific(key,(void *)1);return gated_entry(gated_argument);}
bool32 __wrap_GetExitCodeProcess(int64_t handle,uint32_t *code){
  int ok=__real_GetExitCodeProcess(handle,code);
  if(ok && *code!=259 && atomic_load(&hold_exit) && !pthread_equal(owner_thread,pthread_self())){
    pthread_mutex_lock(&gate_mutex);gate_entered=1;pthread_cond_broadcast(&gate_condition);while(!gate_released)pthread_cond_wait(&gate_condition,&gate_mutex);pthread_mutex_unlock(&gate_mutex);
  }
  return ok;
}
int __wrap_pthread_create(pthread_t *thread,const pthread_attr_t *attr,void *(*entry)(void *),void *argument){
  if(atomic_load(&fail_thread))return EAGAIN;
  if(atomic_exchange(&gate_next,0)){gated_entry=entry;gated_argument=argument;return __real_pthread_create(thread,attr,trampoline,NULL);}
  return __real_pthread_create(thread,attr,entry,argument);
}
static int finish(qaplet_runtime *r){
  int gated=atomic_load(&hold_exit);
  if(gated){pthread_mutex_lock(&gate_mutex);while(!gate_entered)pthread_cond_wait(&gate_condition,&gate_mutex);pthread_mutex_unlock(&gate_mutex);}
  qaplet_drive_result drive=QAPLET_DRIVE_RESULT_INIT;qaplet_code closed=qaplet_close(r,NULL);
  if(gated){atomic_store(&hold_exit,0);pthread_mutex_lock(&gate_mutex);gate_released=1;pthread_cond_broadcast(&gate_condition);pthread_mutex_unlock(&gate_mutex);}
  qaplet_code quiet=qaplet_quiesce(r,5000,&drive,NULL);
  if(closed || quiet || drive.reason!=QAPLET_DRIVE_QUIESCENT){qaplet_m2_diagnostic d=QAPLET_M2_DIAGNOSTIC_INIT;qaplet_get_m2_diagnostic(r,&d,NULL);qaplet_io_snapshot io=QAPLET_IO_SNAPSHOT_INIT;qaplet_get_io_snapshot(r,&io,NULL);fprintf(stderr,"cleanup close=%u quiet=%u reason=%u cleanup=%u domain=%u native=%d handles=%llu io_ops=%llu io_helpers=%llu io_ends=%llu\n",closed,quiet,drive.reason,d.cleanup_reason,d.cleanup_error_domain,d.cleanup_error,(unsigned long long)d.native_uncertain_handles,(unsigned long long)io.operations,(unsigned long long)io.helper_threads,(unsigned long long)io.endpoints);}
  CHECK(closed==QAPLET_OK && quiet==QAPLET_OK && drive.reason==QAPLET_DRIVE_QUIESCENT);qaplet_io_snapshot io=QAPLET_IO_SNAPSHOT_INIT;CHECK(qaplet_get_io_snapshot(r,&io,NULL)==QAPLET_OK && !io.operations && !io.data_bytes && !io.helper_threads && !io.endpoints && !io.shared_operations && !io.shared_data_bytes);CHECK(qaplet_destroy(r,NULL)==QAPLET_OK);return 0;
}
static int spawn(qaplet_runtime *r,qaplet_process *p,qaplet_io_endpoint ep[3],const char *self,const char *loader,const char *pe){
  qaplet_process_options options=QAPLET_PROCESS_OPTIONS_INIT;const char *program=IsWindows() ? pe : loader;options.path=(qaplet_string){program,strlen(program)};
  qaplet_string args[3]={{program,strlen(program)},{IsWindows() ? "--hang" : self,IsWindows() ? 6 : strlen(self)},{"--io-child",10}};options.argv=args;options.argc=IsWindows() ? 2 : 3;
  qaplet_process_status status=QAPLET_PROCESS_STATUS_INIT;qaplet_code accepted;double until=milliseconds()+5000;
  do{accepted=qaplet_process_spawn(r,&options,p,&status,NULL);if(accepted!=QAPLET_RESOURCE_LIMIT)break;sched_yield();}while(milliseconds()<until);
  CHECK(accepted==QAPLET_OK);
  for(int i=0;i<3;++i){ep[i]=(qaplet_io_endpoint)QAPLET_IO_ENDPOINT_INIT;CHECK(qaplet_process_get_stdio(r,p,i,&ep[i],NULL)==QAPLET_OK);}return 0;
}
static int result(qaplet_runtime *r,qaplet_io_operation *op,qaplet_io_result *out){
  double until=milliseconds()+5000;qaplet_io_status status=QAPLET_IO_STATUS_INIT;
  do{CHECK(qaplet_get_io_status(r,op,&status,NULL)==QAPLET_OK);if(status.state!=QAPLET_IO_PENDING && !status.physical_pending && !status.pending_handoff)break;qaplet_drive_result d=QAPLET_DRIVE_RESULT_INIT;CHECK(qaplet_run_once(r,1000,&d,NULL)==QAPLET_OK);}while(milliseconds()<until);
  CHECK(status.state!=QAPLET_IO_PENDING && !status.physical_pending && !status.pending_handoff && qaplet_io_copy_result(r,op,out,NULL)==QAPLET_OK);return 0;
}
static int ready(qaplet_runtime *r,const qaplet_io_endpoint *ep){
  qaplet_io_operation op=QAPLET_IO_OPERATION_INIT;CHECK(qaplet_io_read(r,ep,1,&op,NULL)==QAPLET_OK);unsigned char byte=0;qaplet_io_result value=QAPLET_IO_RESULT_INIT;value.buffer=&byte;value.capacity=1;
  atomic_store(&watch_read,1);CHECK(!result(r,&op,&value));atomic_store(&watch_read,0);CHECK(value.length==1 && byte=='R' && qaplet_io_release(r,&op,NULL)==QAPLET_OK);
  double until=milliseconds()+5000;qaplet_io_snapshot io=QAPLET_IO_SNAPSHOT_INIT;
  do{qaplet_drive_result drive=QAPLET_DRIVE_RESULT_INIT;CHECK(qaplet_run_once(r,0,&drive,NULL)==QAPLET_OK && qaplet_get_io_snapshot(r,&io,NULL)==QAPLET_OK);if(!io.operations && !io.helper_threads)break;sched_yield();}while(milliseconds()<until);
  CHECK(!io.operations && !io.helper_threads);return 0;
}
static int run_case(const char *mode,const char *self,const char *loader,const char *pe){
  owner_thread=pthread_self();if(IsWindows() && !strcmp(mode,"--partial"))atomic_store(&hold_exit,1);
  qaplet_config base=QAPLET_CONFIG_INIT;qaplet_m2_config config=QAPLET_M2_CONFIG_INIT;config.limits.processes=1;
  if(!strcmp(mode,"--physical"))config.limits.operations=1;
  qaplet_runtime *r=NULL;CHECK(qaplet_create_m2(&base,&config,&r,NULL)==QAPLET_OK);
  if(!strcmp(mode,"--wait")){host_event=CreateEvent(NULL,true,false,NULL);CHECK(host_event);qaplet_host_notify hook=QAPLET_HOST_NOTIFY_INIT;hook.function=notify_host;CHECK(qaplet_set_host_notify(r,&hook,NULL)==QAPLET_OK);}
  qaplet_process p=QAPLET_PROCESS_INIT;qaplet_io_endpoint ep[3];CHECK(!spawn(r,&p,ep,self,loader,pe) && !ready(r,&ep[1]));
  if(!strcmp(mode,"--partial")){
    unsigned char input[8192];memset(input,0x5a,sizeof(input));qaplet_io_operation op=QAPLET_IO_OPERATION_INIT;atomic_store(&write_stage,1);
    CHECK(qaplet_io_write(r,&ep[0],input,sizeof(input),&op,NULL)==QAPLET_OK);memset(input,0,sizeof(input));qaplet_io_result value=QAPLET_IO_RESULT_INIT;CHECK(!result(r,&op,&value));atomic_store(&write_stage,0);
    CHECK(real_prefix==1024 && value.length==1024 && value.state==QAPLET_IO_FAILED && value.code==QAPLET_IO_ERROR && value.error==(IsWindows() ? 109 : EPIPE) && value.error_domain==(IsWindows() ? QAPLET_ERROR_DOMAIN_WIN32 : QAPLET_ERROR_DOMAIN_ERRNO));
    qaplet_snapshot s=QAPLET_SNAPSHOT_INIT;CHECK(qaplet_get_snapshot(r,&s,NULL)==QAPLET_OK && s.lifecycle==QAPLET_OPEN && !s.termination_reason);CHECK(qaplet_io_release(r,&op,NULL)==QAPLET_OK && !finish(r));
    puts("controlled I/O partial: real 1024-byte prefix then system write error; exact copied result and native domain; ordinary failure; native cleanup: pass");return 0;
  }
  if(!strcmp(mode,"--allocation")){
    qaplet_io_operation op=QAPLET_IO_OPERATION_INIT;atomic_store(&fail_alloc,1);qaplet_code accepted=qaplet_io_write(r,&ep[0],"X",1,&op,NULL);atomic_store(&fail_alloc,0);CHECK(accepted==QAPLET_MEMORY_LIMIT && !op.runtime_id);
    qaplet_snapshot s=QAPLET_SNAPSHOT_INIT;CHECK(qaplet_get_snapshot(r,&s,NULL)==QAPLET_OK && s.lifecycle==QAPLET_FAILED && s.termination_reason==QAPLET_MEMORY_LIMIT);
    qaplet_process_status process=QAPLET_PROCESS_STATUS_INIT;double until=milliseconds()+5000;
    do{CHECK(qaplet_get_process_status(r,&p,&process,NULL)==QAPLET_OK);if(process.reaped)break;sched_yield();}while(milliseconds()<until);
    CHECK(process.reaped && !finish(r));puts("controlled I/O allocation: actual SDK refusal is permanent; owner starts child termination without another pump/close; first cause and physical cleanup: pass");return 0;
  }
  if(!strcmp(mode,"--wake")){
    qaplet_drive_result drive=QAPLET_DRIVE_RESULT_INIT;CHECK(qaplet_run_once(r,0,&drive,NULL)==QAPLET_OK);
    qaplet_io_operation op=QAPLET_IO_OPERATION_INIT;atomic_store(&fail_send,1);CHECK(qaplet_io_read(r,&ep[1],1,&op,NULL)==QAPLET_OK && op.runtime_id && !atomic_load(&fail_send));
    qaplet_snapshot state=QAPLET_SNAPSHOT_INIT;CHECK(qaplet_get_snapshot(r,&state,NULL)==QAPLET_OK && state.lifecycle==QAPLET_FAILED && state.termination_reason==QAPLET_CONTROL_FAILURE);
    CHECK(qaplet_io_cancel(r,&op,NULL)==QAPLET_OK && qaplet_io_release(r,&op,NULL)==QAPLET_OK);
    qaplet_progress progress=QAPLET_PROGRESS_INIT;CHECK(qaplet_pump(r,&progress,NULL)==QAPLET_CONTROL_FAILURE);
    qaplet_m2_diagnostic diagnostic=QAPLET_M2_DIAGNOSTIC_INIT;CHECK(qaplet_get_m2_diagnostic(r,&diagnostic,NULL)==QAPLET_OK && diagnostic.control_error==EIO && diagnostic.primary_reason==QAPLET_CONTROL_FAILURE);
    CHECK(!finish(r));puts("controlled I/O wake: accepted token survives actual wake failure; cancellation/release remain accepted; independent first cause and owner cleanup: pass");return 0;
  }
  if(!strcmp(mode,"--start")){
    CHECK(IsWindows());qaplet_io_operation op=QAPLET_IO_OPERATION_INIT;atomic_store(&fail_thread,1);qaplet_code accepted=qaplet_io_read(r,&ep[1],1,&op,NULL);atomic_store(&fail_thread,0);
    qaplet_io_snapshot io=QAPLET_IO_SNAPSHOT_INIT;CHECK(accepted==QAPLET_IO_ERROR && !op.runtime_id && qaplet_get_io_snapshot(r,&io,NULL)==QAPLET_OK && !io.shared_operations && !io.shared_data_bytes && io.start_error_domain==QAPLET_ERROR_DOMAIN_ERRNO && io.start_error==EAGAIN);
    CHECK(qaplet_io_read(r,&ep[1],1,&op,NULL)==QAPLET_OK && qaplet_io_cancel(r,&op,NULL)==QAPLET_OK && qaplet_io_release(r,&op,NULL)==QAPLET_OK && !finish(r));
    puts("controlled I/O start: real helper start refusal preserves native errno; no accepted token/count/bytes; ordinary failure retries and cleans up: pass");return 0;
  }
  if(!strcmp(mode,"--wait")){
    CHECK(IsWindows());qaplet_io_operation op=QAPLET_IO_OPERATION_INIT;atomic_store(&fail_wait,1);CHECK(qaplet_io_read(r,&ep[1],1,&op,NULL)==QAPLET_OK);
    qaplet_progress progress=QAPLET_PROGRESS_INIT;CHECK(qaplet_pump(r,&progress,NULL)==QAPLET_OK);
    double until=milliseconds()+5000;qaplet_io_status status=QAPLET_IO_STATUS_INIT;
    do{
      CHECK(ResetEvent(host_event) && qaplet_get_io_status(r,&op,&status,NULL)==QAPLET_OK);
      if(status.state==QAPLET_IO_FAILED && !status.physical_pending)break;
      CHECK(WaitForSingleObject(host_event,5000)==0);
    }while(milliseconds()<until);
    CHECK(status.state==QAPLET_IO_FAILED && !status.physical_pending);
    qaplet_io_result value=QAPLET_IO_RESULT_INIT;CHECK(!result(r,&op,&value) && !atomic_load(&fail_wait));
    CHECK(value.state==QAPLET_IO_FAILED && value.code==QAPLET_IO_ERROR && value.error_domain==QAPLET_ERROR_DOMAIN_WIN32 && value.error==6 && !value.length);
    qaplet_snapshot state=QAPLET_SNAPSHOT_INIT;CHECK(qaplet_get_snapshot(r,&state,NULL)==QAPLET_OK && state.lifecycle==QAPLET_OPEN && !state.termination_reason);
    CHECK(qaplet_io_release(r,&op,NULL)==QAPLET_OK && !finish(r) && CloseHandle(host_event));puts("controlled I/O wait: event wait failure ends only the operation; native Win32 error preserved; pending kernel I/O cancelled and proven complete before event/buffer retirement; cleanup: pass");return 0;
  }
  if(!strcmp(mode,"--physical")){
    CHECK(IsWindows() && !pthread_key_create(&key,destructor));atomic_store(&gate_next,1);qaplet_io_operation op=QAPLET_IO_OPERATION_INIT;CHECK(qaplet_io_read(r,&ep[1],1,&op,NULL)==QAPLET_OK);
    qaplet_progress progress=QAPLET_PROGRESS_INIT;CHECK(qaplet_pump(r,&progress,NULL)==QAPLET_OK && qaplet_io_cancel(r,&op,NULL)==QAPLET_OK);
    pthread_mutex_lock(&gate_mutex);while(!gate_entered)pthread_cond_wait(&gate_condition,&gate_mutex);pthread_mutex_unlock(&gate_mutex);
    qaplet_io_status status=QAPLET_IO_STATUS_INIT;CHECK(qaplet_get_io_status(r,&op,&status,NULL)==QAPLET_OK && status.state==QAPLET_IO_CANCELLED && !status.physical_pending && status.helper_active);
    CHECK(qaplet_io_release(r,&op,NULL)==QAPLET_OK && qaplet_get_io_status(r,&op,&status,NULL)==QAPLET_INVALID_HANDLE);
    CHECK(qaplet_close(r,NULL)==QAPLET_OK);qaplet_io_snapshot io=QAPLET_IO_SNAPSHOT_INIT;CHECK(qaplet_get_io_snapshot(r,&io,NULL)==QAPLET_OK && io.operations==1 && io.data_bytes==1 && io.shared_operations==1 && io.shared_data_bytes==1 && io.helper_threads==1);
    qaplet_drive_result drive=QAPLET_DRIVE_RESULT_INIT;CHECK(qaplet_quiesce(r,0,&drive,NULL)==QAPLET_OK && drive.reason==QAPLET_DRIVE_WAIT_TIMEOUT && qaplet_quiesce(r,10,&drive,NULL)==QAPLET_OK && drive.reason==QAPLET_DRIVE_WAIT_TIMEOUT && qaplet_destroy(r,NULL)==QAPLET_BUSY);
    pthread_mutex_lock(&gate_mutex);gate_released=1;pthread_cond_broadcast(&gate_condition);pthread_mutex_unlock(&gate_mutex);CHECK(qaplet_wake(r,NULL)==QAPLET_OK && !finish(r));pthread_key_delete(key);
    puts("controlled I/O physical: cancelled syscall exits before gated helper TLS destruction; released token invalid; bytes/count retained CLOSED; finite retry and BUSY destroy; real join retires storage: pass");return 0;
  }
  CHECK(!strcmp(mode,"--reuse"));qaplet_io_endpoint stale_endpoint=ep[1];qaplet_io_operation stale=QAPLET_IO_OPERATION_INIT;
  {
    previous_native=observed_native;native_closed=0;recycle_native=1;
    CHECK(qaplet_io_read(r,&ep[1],1,&stale,NULL)==QAPLET_OK);qaplet_progress progress=QAPLET_PROGRESS_INIT;CHECK(qaplet_pump(r,&progress,NULL)==QAPLET_OK && qaplet_io_cancel(r,&stale,NULL)==QAPLET_OK && qaplet_io_release(r,&stale,NULL)==QAPLET_OK);
    for(int i=0;i<3;++i)CHECK(qaplet_io_close(r,&ep[i],NULL)==QAPLET_OK);
    qaplet_drive_result drive=QAPLET_DRIVE_RESULT_INIT;CHECK(qaplet_run(r,&drive,NULL)==QAPLET_OK && drive.reason==QAPLET_DRIVE_IDLE && qaplet_process_release(r,&p,NULL)==QAPLET_OK);
    p=(qaplet_process)QAPLET_PROCESS_INIT;CHECK(!spawn(r,&p,ep,self,loader,pe));
    qaplet_io_operation rejected=QAPLET_IO_OPERATION_INIT;qaplet_io_status status=QAPLET_IO_STATUS_INIT;CHECK(qaplet_io_read(r,&stale_endpoint,1,&rejected,NULL)==QAPLET_INVALID_HANDLE && qaplet_get_io_status(r,&stale,&status,NULL)==QAPLET_INVALID_HANDLE);
    CHECK(!ready(r,&ep[1]));stale_endpoint=ep[1];
  }
  if(!native_reused)fprintf(stderr,"native reuse not obtained: retired=%lld closed=%d event_attempts=%u\n",(long long)previous_native,native_closed,recycled_count);
  CHECK(native_reused && !finish(r));
  if(IsWindows()){CHECK(replacement_event==previous_native && WaitForSingleObject(replacement_event,0)==258 && SetEvent(replacement_event) && WaitForSingleObject(replacement_event,0)==0);while(recycled_count)CHECK(CloseHandle(recycled_events[--recycled_count]));}
  else{char byte=0;CHECK(__real_read(external_pipe[0],&byte,1)==1 && byte=='Z' && !__real_close(external_pipe[0]) && !__real_close(external_pipe[1]));}
  puts("controlled I/O reuse: real closed native descriptor/HANDLE reused; pending cancel/close and stale endpoint/operation tokens never target replacement; exact new child byte and cleanup: pass");return 0;
}
int main(int argc,char **argv){
  if(argc==2 && !strcmp(argv[1],"--io-child")){if(write(1,"R",1)!=1)return 98;unsigned char data[4096];while(read(0,data,sizeof(data))>0){}return 37;}
  CHECK(argc==5);return run_case(argv[1],argv[2],argv[3],argv[4]);
}
