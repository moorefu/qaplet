/* SPDX-License-Identifier: MIT */
/* Controlled native syscall boundary. The application uses only public SDK. */
#define _COSMO_SOURCE 1
#include <stdatomic.h>
#include <pthread.h>
#include <stdbool.h>
#ifdef SDK_STANDALONE
#undef SDK_STANDALONE
#endif
#include "host-stdio.c"
extern ssize_t __real_read(int,void *,size_t);
extern ssize_t __real_write(int,const void *,size_t);
extern bool32 __real_ReadFile(int64_t,void *,uint32_t,uint32_t *,struct NtOverlapped *);
extern bool32 __real_WriteFile(int64_t,const void *,uint32_t,uint32_t *,struct NtOverlapped *);
extern int __real_pthread_create(pthread_t *,const pthread_attr_t *,void *(*)(void *),void *);
extern void SetLastError(uint32_t);
extern void *__real_calloc(size_t,size_t);
static pthread_mutex_t gate=PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t condition=PTHREAD_COND_INITIALIZER;
static int entered,released;
static _Atomic int arm,partial,fail_start,fail_alloc;
void *__wrap_calloc(size_t n,size_t size){return atomic_load(&fail_alloc) ? NULL : __real_calloc(n,size);}
static void syscall_gate(void){
  if(!atomic_exchange(&arm,0))return;
  pthread_mutex_lock(&gate);entered=1;pthread_cond_broadcast(&condition);
  while(!released)pthread_cond_wait(&condition,&gate);pthread_mutex_unlock(&gate);
}
ssize_t __wrap_read(int fd,void *bytes,size_t n){syscall_gate();return __real_read(fd,bytes,n);}
bool32 __wrap_ReadFile(int64_t handle,void *bytes,uint32_t n,uint32_t *got,struct NtOverlapped *overlapped){syscall_gate();return __real_ReadFile(handle,bytes,n,got,overlapped);}
ssize_t __wrap_write(int fd,const void *bytes,size_t n){
  int stage=atomic_load(&partial);if(stage==1){atomic_store(&partial,2);return __real_write(fd,bytes,n>2 ? 2 : n);}
  if(stage==2){atomic_store(&partial,0);errno=EIO;return -1;}return __real_write(fd,bytes,n);
}
bool32 __wrap_WriteFile(int64_t handle,const void *bytes,uint32_t n,uint32_t *got,struct NtOverlapped *overlapped){
  int stage=atomic_load(&partial);if(stage==1){atomic_store(&partial,2);return __real_WriteFile(handle,bytes,n>2 ? 2 : n,got,overlapped);}
  if(stage==2){atomic_store(&partial,0);*got=0;SetLastError(5);return false;}return __real_WriteFile(handle,bytes,n,got,overlapped);
}
int __wrap_pthread_create(pthread_t *t,const pthread_attr_t *a,void *(*entry)(void *),void *arg){if(atomic_exchange(&fail_start,0))return EAGAIN;return __real_pthread_create(t,a,entry,arg);}
static double deadline(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return t.tv_sec+t.tv_nsec/1e9;}
static qaplet_code arm_js_start(qaplet_runtime *r,uint64_t call,size_t argc,const qaplet_value *args,void *opaque){
  (void)r;(void)call;(void)argc;(void)args;(void)opaque;atomic_store(&fail_start,1);return QAPLET_OK;
}
static int lifecycle(const char *mode){
  unsigned stream=!strcmp(mode,"--partial") ? 1 : 0;file_fixture f;CHECK(!install_file(&f,stream));
  const unsigned char payload[]={0,1,127,128,255,10};
  if(!stream){CHECK(__real_write(f.fd,payload,6)==6 && lseek(f.fd,0,SEEK_SET)==0);}
  qaplet_config b=QAPLET_CONFIG_INIT;qaplet_m2_config m=QAPLET_M2_CONFIG_INIT;m.limits.operations=1;
  qaplet_runtime *r=NULL;CHECK(qaplet_create_m2(&b,&m,&r,NULL)==QAPLET_OK);
  qaplet_stdio_info info=QAPLET_STDIO_INFO_INIT;qaplet_io_endpoint ep=QAPLET_IO_ENDPOINT_INIT;
  if(!strcmp(mode,"--allocation")){
    atomic_store(&fail_alloc,1);qaplet_code code=qaplet_stdio_attach(r,stream,&ep,&info,NULL);atomic_store(&fail_alloc,0);
    qaplet_snapshot snapshot=QAPLET_SNAPSHOT_INIT;qaplet_drive_result drive=QAPLET_DRIVE_RESULT_INIT;qaplet_io_snapshot io=QAPLET_IO_SNAPSHOT_INIT;
    CHECK(code==QAPLET_MEMORY_LIMIT && !ep.generation && qaplet_get_snapshot(r,&snapshot,NULL)==QAPLET_OK && snapshot.lifecycle==QAPLET_FAILED && snapshot.termination_reason==QAPLET_MEMORY_LIMIT);
    CHECK(qaplet_get_io_snapshot(r,&io,NULL)==QAPLET_OK && !io.endpoints && !io.operations && !io.data_bytes);
    CHECK(qaplet_close(r,NULL)==QAPLET_OK && qaplet_quiesce(r,5000,&drive,NULL)==QAPLET_OK && drive.reason==QAPLET_DRIVE_QUIESCENT && qaplet_destroy(r,NULL)==QAPLET_OK && !restore_file(&f));
    puts("controlled stdio allocation: actual SDK attach storage refusal permanently fails; no token/duplicate/operation; Host original survives and native cleanup completes: pass");return 0;
  }
  CHECK(qaplet_stdio_attach(r,stream,&ep,&info,NULL)==QAPLET_OK);
  if(!strcmp(mode,"--js-start")){
    qaplet_host_export exports[2]={QAPLET_HOST_EXPORT_INIT,QAPLET_HOST_EXPORT_INIT};
    exports[0].name="arm";exports[0].name_length=3;exports[0].kind=QAPLET_HOST_FUNCTION;exports[0].function=arm_js_start;
    exports[1].name="eagain";exports[1].name_length=6;exports[1].kind=QAPLET_HOST_CONSTANT;exports[1].value.type=QAPLET_VALUE_DOUBLE;exports[1].value.number=EAGAIN;
    qaplet_host_module host=QAPLET_HOST_MODULE_INIT;host.name="host:stdio-fault";host.name_length=strlen(host.name);host.exports=exports;host.export_count=2;CHECK(qaplet_register_host_module(r,&host,NULL)==QAPLET_OK);
    const char *source="Promise.all([import('qaplet:stdio'),import('host:stdio-fault')]).then(async([s,h])=>{h.arm();"
      "try{s.stdin.read(8);throw Error('start accepted')}catch(e){if(e.code!==30||e.errorDomain!==1||e.error!==h.eagain)throw Error(JSON.stringify([e.code,e.errorDomain,e.error]))}"
      "try{s.stdin.read(-1);throw Error('bad argument accepted')}catch(e){if(e.code!==1||e.errorDomain!==0||e.error!==0)throw Error('stale native error')}"
      "const value=await s.stdin.read(8).promise;return Array.from(value.bytes).join(',')==='0,1,127,128,255,10'?42:0})";
    qaplet_progress progress=QAPLET_PROGRESS_INIT;qaplet_drive_result drive=QAPLET_DRIVE_RESULT_INIT;qaplet_diagnostic diagnostic=QAPLET_DIAGNOSTIC_INIT;char message[512];diagnostic.message=message;diagnostic.capacity=sizeof(message);
    CHECK(qaplet_eval(r,source,strlen(source),&progress,NULL)==QAPLET_OK);qaplet_code code=qaplet_run(r,&drive,&diagnostic);
    qaplet_result result=QAPLET_RESULT_INIT;int success=code==QAPLET_OK && drive.phase==QAPLET_PHASE_DONE && qaplet_get_result(r,&result,NULL)==QAPLET_OK && result.type==QAPLET_VALUE_DOUBLE && result.number==42;
    if(!success)fprintf(stderr,"stdio JS start code=%d diagnostic=%.*s\n",code,(int)diagnostic.length,message);
    CHECK(qaplet_close(r,NULL)==QAPLET_OK && qaplet_quiesce(r,5000,&drive,NULL)==QAPLET_OK && drive.reason==QAPLET_DRIVE_QUIESCENT && qaplet_destroy(r,NULL)==QAPLET_OK && !restore_file(&f));
    if(success)puts("controlled stdio JS start: real helper EAGAIN retains code/native domain/value; later bad arguments carry no stale OS error; accepted retry returns copied bytes; cleanup: pass");return success ? 0 : 1;
  }
  qaplet_io_operation op=QAPLET_IO_OPERATION_INIT;unsigned char bytes[8]={0};qaplet_io_result value=QAPLET_IO_RESULT_INIT;value.buffer=bytes;value.capacity=8;
  qaplet_drive_result drive=QAPLET_DRIVE_RESULT_INIT;qaplet_io_snapshot io=QAPLET_IO_SNAPSHOT_INIT;
  if(!strcmp(mode,"--partial")){
    atomic_store(&partial,1);CHECK(qaplet_io_write(r,&ep,payload,6,&op,NULL)==QAPLET_OK && qaplet_run(r,&drive,NULL)==QAPLET_OK);
    CHECK(qaplet_io_copy_result(r,&op,&value,NULL)==QAPLET_OK && value.state==QAPLET_IO_FAILED && value.code==QAPLET_IO_ERROR && value.length==2 && value.error_domain==(IsWindows() ? QAPLET_ERROR_DOMAIN_WIN32 : QAPLET_ERROR_DOMAIN_ERRNO) && value.error==(IsWindows() ? 5 : EIO));
    qaplet_snapshot snapshot=QAPLET_SNAPSHOT_INIT;CHECK(qaplet_get_snapshot(r,&snapshot,NULL)==QAPLET_OK && snapshot.lifecycle==QAPLET_OPEN);
    CHECK(qaplet_io_release(r,&op,NULL)==QAPLET_OK);
  }else if(!strcmp(mode,"--start")){
    atomic_store(&fail_start,1);CHECK(qaplet_io_read(r,&ep,8,&op,NULL)==QAPLET_IO_ERROR && !op.generation);
    CHECK(qaplet_get_io_snapshot(r,&io,NULL)==QAPLET_OK && !io.operations && !io.data_bytes && !io.helper_threads && io.start_error_domain==QAPLET_ERROR_DOMAIN_ERRNO && io.start_error==EAGAIN);
    CHECK(qaplet_io_read(r,&ep,8,&op,NULL)==QAPLET_OK && qaplet_run(r,&drive,NULL)==QAPLET_OK);
    CHECK(qaplet_io_copy_result(r,&op,&value,NULL)==QAPLET_OK && value.code==QAPLET_OK && value.length==6 && !memcmp(bytes,payload,6));
    CHECK(qaplet_io_release(r,&op,NULL)==QAPLET_OK);
  }else{
    atomic_store(&arm,1);CHECK(qaplet_io_read(r,&ep,8,&op,NULL)==QAPLET_OK);
    qaplet_progress progress=QAPLET_PROGRESS_INIT;CHECK(qaplet_pump(r,&progress,NULL)==QAPLET_OK);
    pthread_mutex_lock(&gate);while(!entered)pthread_cond_wait(&condition,&gate);pthread_mutex_unlock(&gate);
    qaplet_io_status status=QAPLET_IO_STATUS_INIT;
    CHECK(qaplet_get_io_status(r,&op,&status,NULL)==QAPLET_OK && status.physical_pending && status.helper_active);
    if(!strcmp(mode,"--stop")){
      CHECK(qaplet_request_stop(r,NULL)==QAPLET_OK && qaplet_run(r,&drive,NULL)==QAPLET_EXECUTION_STOPPED && drive.reason==QAPLET_DRIVE_STOPPED);
      qaplet_snapshot stopped=QAPLET_SNAPSHOT_INIT;CHECK(qaplet_get_snapshot(r,&stopped,NULL)==QAPLET_OK && stopped.lifecycle==QAPLET_STOPPED && stopped.termination_reason==QAPLET_EXECUTION_STOPPED);
    }else CHECK(qaplet_io_cancel(r,&op,NULL)==QAPLET_OK && qaplet_run(r,&drive,NULL)==QAPLET_OK && drive.reason==QAPLET_DRIVE_IDLE);
    CHECK(qaplet_io_copy_result(r,&op,&value,NULL)==QAPLET_BUSY);
    CHECK(qaplet_get_io_snapshot(r,&io,NULL)==QAPLET_OK && io.shared_operations==1 && io.shared_data_bytes==8 && io.helper_threads==1);
    int closing=!strcmp(mode,"--close") || !strcmp(mode,"--stop");
    if(closing){
      CHECK(qaplet_io_release(r,&op,NULL)==QAPLET_OK && qaplet_get_io_status(r,&op,&status,NULL)==QAPLET_INVALID_HANDLE);
      CHECK(qaplet_close(r,NULL)==QAPLET_OK && qaplet_quiesce(r,0,&drive,NULL)==QAPLET_OK && drive.reason==QAPLET_DRIVE_WAIT_TIMEOUT);
      CHECK(qaplet_destroy(r,NULL)==QAPLET_BUSY);
      CHECK(qaplet_get_io_snapshot(r,&io,NULL)==QAPLET_OK && io.shared_operations==1 && io.shared_data_bytes==8 && io.helper_threads==1);
    }
    pthread_mutex_lock(&gate);released=1;pthread_cond_broadcast(&condition);pthread_mutex_unlock(&gate);
    if(!closing){
      double until=deadline()+5;
      do{CHECK(qaplet_run_once(r,0,&drive,NULL)==QAPLET_OK && qaplet_get_io_status(r,&op,&status,NULL)==QAPLET_OK);if(!status.physical_pending && !status.pending_handoff)break;sched_yield();}while(deadline()<until);
      CHECK(!status.physical_pending && !status.pending_handoff && qaplet_io_copy_result(r,&op,&value,NULL)==QAPLET_OK && value.code==QAPLET_CANCELLED && value.length==6 && !memcmp(bytes,payload,6));
      CHECK(qaplet_io_release(r,&op,NULL)==QAPLET_OK);
    }
  }
  CHECK(qaplet_close(r,NULL)==QAPLET_OK && qaplet_quiesce(r,5000,&drive,NULL)==QAPLET_OK && drive.reason==QAPLET_DRIVE_QUIESCENT);
  CHECK(qaplet_get_io_snapshot(r,&io,NULL)==QAPLET_OK && !io.shared_operations && !io.shared_data_bytes && !io.helper_threads && !io.endpoints);
  qaplet_m2_snapshot snapshot=QAPLET_M2_SNAPSHOT_INIT;CHECK(qaplet_get_m2_snapshot(r,&snapshot,NULL)==QAPLET_OK && !snapshot.native_handles && !snapshot.reactor_registrations && !snapshot.native_uncertain_handles);
  CHECK(qaplet_destroy(r,NULL)==QAPLET_OK);
  if(stream){CHECK(lseek(f.fd,0,SEEK_SET)==0 && read(f.fd,bytes,8)==2 && !memcmp(bytes,payload,2));}
  CHECK(!restore_file(&f));
  if(!strcmp(mode,"--partial"))puts("controlled stdio partial: real 2-byte prefix then native error; stable actual bytes and error domain; ordinary failure preserves OPEN; Host output preserved: pass");
  else if(!strcmp(mode,"--start"))puts("controlled stdio start: helper EAGAIN refuses atomically; no token/count/data/helper; stable errno; retry and real file cleanup: pass");
  else if(!strcmp(mode,"--close"))puts("controlled stdio close: cancelled native file call retains bytes/count/helper through CLOSED; zero quiesce times out; destroy BUSY; actual exit/join returns capacity and preserves Host handle: pass");
  else if(!strcmp(mode,"--stop"))puts("controlled stdio stop: owner confirms first stop and cancels in-flight file before close; physical data/helper retained through CLOSED/BUSY; actual native exit/join completes cleanup: pass");
  else puts("controlled stdio cancel: logical cancellation removes keepalive while syscall retains count/data/helper; copy BUSY until stable real bytes; single CANCELLED result then physical cleanup: pass");
  return 0;
}
int main(int argc,char **argv){return argc==2 ? lifecycle(argv[1]) : 2;}
