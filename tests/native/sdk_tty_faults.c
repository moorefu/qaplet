/* SPDX-License-Identifier: MIT */
/* Explicit system-boundary faults; only the supplied public SDK is consumed. */
#define _COSMO_SOURCE 1
#include <stdatomic.h>
#include <stdbool.h>
#include <sched.h>
#ifdef QAPLET_TTY_STANDALONE
#undef QAPLET_TTY_STANDALONE
#endif
#include "host-tty.c"
extern int __real_tcsetattr(int,int,const struct termios *);
extern bool32 __real_SetConsoleMode(int64_t,uint32_t);
extern bool32 __real_WriteConsole(int64_t,const void *,uint32_t,uint32_t *,void *);
extern void SetLastError(uint32_t);
extern int __real_poll(struct pollfd *,nfds_t,int);
extern ssize_t __real_read(int,void *,size_t);
extern int __real_pthread_create(pthread_t *,const pthread_attr_t *,void *(*)(void *),void *);
static _Atomic int fail_restore,partial_kind,partial_seen;
static pthread_mutex_t fault_mutex=PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t fault_condition=PTHREAD_COND_INITIALIZER;
static int native_entered,native_release;
static uint32_t real_partial_written;
static pthread_key_t exit_key;
static _Atomic int exit_gate,stage_entered,arm_wait;
static int wait_entered,wait_result=-1,producer_abort;
static _Atomic int gate_created_thread,fail_created_thread;
static _Atomic int fail_terminal_read;
ssize_t __wrap_read(int fd,void *data,size_t n){return __real_read(atomic_exchange(&fail_terminal_read,0) ? -1 : fd,data,n);}
typedef struct {void *(*entry)(void *);void *argument;} native_start;
static void *native_trampoline(void *opaque){native_start start=*(native_start *)opaque;free(opaque);pthread_setspecific(exit_key,(void *)1);return start.entry(start.argument);}
int __wrap_pthread_create(pthread_t *thread,const pthread_attr_t *attr,void *(*entry)(void *),void *argument){
  if(atomic_load(&fail_created_thread))return EAGAIN;
  if(!atomic_load(&gate_created_thread))return __real_pthread_create(thread,attr,entry,argument);
  native_start *start=malloc(sizeof(*start));if(!start)return ENOMEM;*start=(native_start){entry,argument};int error=__real_pthread_create(thread,attr,native_trampoline,start);if(error)free(start);return error;
}
static void exit_destructor(void *unused){(void)unused;pthread_mutex_lock(&fault_mutex);native_entered=1;pthread_cond_broadcast(&fault_condition);while(!native_release)pthread_cond_wait(&fault_condition,&fault_mutex);pthread_mutex_unlock(&fault_mutex);}
int __wrap_poll(struct pollfd *fds,nfds_t n,int timeout){
  int observe=atomic_load(&arm_wait);
  if(observe){pthread_mutex_lock(&fault_mutex);wait_entered=1;pthread_cond_broadcast(&fault_condition);pthread_mutex_unlock(&fault_mutex);}
  int result=__real_poll(fds,n,timeout);if(observe)wait_result=result;return result;
}
int __wrap_tcsetattr(int fd,int action,const struct termios *mode){if(atomic_load(&fail_restore)){errno=EIO;return -1;}return __real_tcsetattr(fd,action,mode);}
bool32 __wrap_SetConsoleMode(int64_t handle,uint32_t mode){if(atomic_load(&fail_restore)){SetLastError(5);return false;}return __real_SetConsoleMode(handle,mode);}
bool32 __wrap_WriteConsole(int64_t handle,const void *text,uint32_t n,uint32_t *written,void *reserved){
  if(atomic_load(&exit_gate))pthread_setspecific(exit_key,(void *)1);
  int kind=atomic_load(&partial_kind);
  if(kind && n==2 && !atomic_exchange(&partial_seen,1)){
    bool32 ok=__real_WriteConsole(handle,text,1,written,reserved);real_partial_written=*written;
    if(!ok || *written!=1)return ok;
    if(kind==2){SetLastError(5);return false;}
    if(kind>=3){pthread_mutex_lock(&fault_mutex);native_entered=1;pthread_cond_broadcast(&fault_condition);while(!native_release)pthread_cond_wait(&fault_condition,&fault_mutex);pthread_mutex_unlock(&fault_mutex);}
    return true;
  }
  return __real_WriteConsole(handle,text,n,written,reserved);
}
static int restore_failure(void){
  int master=-1,slave=-1,saved=-1,saved_output=-1;int64_t original=0,console=0,original_output=0,output=0;struct termios before,after;uint32_t mode=0,output_mode=0,checked=0;
  if(IsWindows()){original=GetStdHandle(-10);console=CreateFile(u"CONIN$",0xc0000000u,3,NULL,3,0,0);CHECK(console!=-1 && GetConsoleMode(console,&mode) && SetStdHandle(-10,console));}
  else{saved=dup(0);CHECK(saved>=0 && !private_pty(&master,&slave) && dup2(slave,0)==0 && !tcgetattr(slave,&before));}
  if(IsWindows()){output=CreateFile(u"CONOUT$",0xc0000000u,3,NULL,3,0,0);original_output=GetStdHandle(-11);CHECK(output!=-1 && GetConsoleMode(output,&output_mode) && SetStdHandle(-11,output));}
  else{saved_output=dup(1);CHECK(saved_output>=0 && dup2(slave,1)==1);}
  qaplet_config b=QAPLET_CONFIG_INIT;qaplet_m2_config m=QAPLET_M2_CONFIG_INIT;qaplet_runtime *r=NULL,*other=NULL;CHECK(qaplet_create_m2(&b,&m,&r,NULL)==QAPLET_OK && qaplet_create_m2(&b,&m,&other,NULL)==QAPLET_OK);
  qaplet_tty_options options=QAPLET_TTY_OPTIONS_INIT;options.flags=QAPLET_TTY_RAW;qaplet_tty_status status=QAPLET_TTY_STATUS_INIT;qaplet_io_endpoint e=QAPLET_IO_ENDPOINT_INIT,alias=QAPLET_IO_ENDPOINT_INIT;
  CHECK(qaplet_tty_acquire(r,0,&options,&e,&status,NULL)==QAPLET_OK);
  qaplet_tty_options vt=QAPLET_TTY_OPTIONS_INIT;vt.flags=QAPLET_TTY_VT;qaplet_io_endpoint joined=QAPLET_IO_ENDPOINT_INIT;CHECK(qaplet_tty_join(r,&e,1,&vt,&joined,&status,NULL)==QAPLET_OK);
  atomic_store(&fail_restore,1);CHECK(qaplet_tty_release(r,&e,&status,NULL)==QAPLET_IO_ERROR && status.active && !status.restored && status.error==(IsWindows() ? 5 : EIO));
  CHECK(qaplet_tty_acquire(other,0,&options,&alias,&status,NULL)==QAPLET_BUSY && !alias.generation);
  CHECK(qaplet_close(r,NULL)==QAPLET_IO_ERROR);
  qaplet_drive_result drive=QAPLET_DRIVE_RESULT_INIT;CHECK(qaplet_quiesce(r,0,&drive,NULL)==QAPLET_IO_ERROR && drive.reason==QAPLET_DRIVE_WAIT_TIMEOUT && qaplet_destroy(r,NULL)==QAPLET_BUSY);
  qaplet_m2_snapshot retained=QAPLET_M2_SNAPSHOT_INIT;CHECK(qaplet_get_m2_snapshot(r,&retained,NULL)==QAPLET_OK && retained.native_handles>0 && retained.reactor_registrations>0);
  atomic_store(&fail_restore,0);struct timespec retry_start,retry_now;CHECK(!clock_gettime(CLOCK_MONOTONIC,&retry_start));
  do{CHECK(qaplet_quiesce(r,0,&drive,NULL)==QAPLET_IO_ERROR);if(drive.reason==QAPLET_DRIVE_QUIESCENT)break;sched_yield();CHECK(!clock_gettime(CLOCK_MONOTONIC,&retry_now) && retry_now.tv_sec-retry_start.tv_sec<5);}while(1);
  CHECK(qaplet_get_m2_snapshot(r,&retained,NULL)==QAPLET_OK && !retained.native_handles && !retained.reactor_registrations && !retained.native_uncertain_handles && qaplet_destroy(r,NULL)==QAPLET_OK);
  CHECK(qaplet_tty_acquire(other,0,&options,&alias,&status,NULL)==QAPLET_OK && qaplet_tty_release(other,&alias,&status,NULL)==QAPLET_OK && status.restored && !finish_tty(other));
  if(IsWindows())CHECK(GetConsoleMode(console,&checked) && checked==mode && GetConsoleMode(output,&checked) && checked==output_mode && SetStdHandle(-10,original) && SetStdHandle(-11,original_output) && CloseHandle(console) && CloseHandle(output));
  else CHECK(!tcgetattr(slave,&after) && same(&before,&after) && !(fcntl(slave,F_GETFL)&O_NONBLOCK) && dup2(saved,0)==0 && dup2(saved_output,1)==1 && !close(saved) && !close(saved_output) && !close(slave) && !close(master));
  puts("controlled TTY restore: syscall failure is IO_ERROR; lease and duplicate remain charged and exclude aliases across CLOSED; quiesce pending and destroy BUSY; actual retry restores all state before retirement; original error retained: pass");return 0;
}
static int partial(int kind){
  CHECK(IsWindows());int64_t original=GetStdHandle(-11),console=CreateFile(u"CONOUT$",0xc0000000u,3,NULL,3,0,0);uint32_t mode,checked;
  int64_t input=0,original_input=0;uint32_t input_mode=0;
  if(kind==4){input=CreateFile(u"CONIN$",0xc0000000u,3,NULL,3,0,0);original_input=GetStdHandle(-10);CHECK(input!=-1 && GetConsoleMode(input,&input_mode) && SetStdHandle(-10,input));}
  CHECK(console!=-1 && GetConsoleMode(console,&mode) && SetStdHandle(-11,console));
  qaplet_config b=QAPLET_CONFIG_INIT;qaplet_m2_config m=QAPLET_M2_CONFIG_INIT;m.limits.operations=kind==4 ? 2 : 1;qaplet_runtime *r=NULL;CHECK(qaplet_create_m2(&b,&m,&r,NULL)==QAPLET_OK);
  qaplet_tty_options options=QAPLET_TTY_OPTIONS_INIT;options.flags=QAPLET_TTY_VT;qaplet_tty_status status=QAPLET_TTY_STATUS_INIT;qaplet_io_endpoint e=QAPLET_IO_ENDPOINT_INIT;
  CHECK(qaplet_tty_acquire(r,1,&options,&e,&status,NULL)==QAPLET_OK);atomic_store(&partial_kind,kind);
  qaplet_io_endpoint joined=QAPLET_IO_ENDPOINT_INIT;qaplet_io_operation input_op=QAPLET_IO_OPERATION_INIT;
  if(kind==4){qaplet_tty_options raw=QAPLET_TTY_OPTIONS_INIT;raw.flags=QAPLET_TTY_RAW;CHECK(qaplet_tty_join(r,&e,0,&raw,&joined,&status,NULL)==QAPLET_OK && qaplet_io_read(r,&joined,8,&input_op,NULL)==QAPLET_OK);}
  const char text[]="A\xf0\x9f\x99\x82Z";qaplet_io_operation op=QAPLET_IO_OPERATION_INIT;CHECK(qaplet_io_write(r,&e,text,sizeof(text)-1,&op,NULL)==QAPLET_OK);
  qaplet_drive_result drive=QAPLET_DRIVE_RESULT_INIT;
  if(kind>=3){
    qaplet_progress p=QAPLET_PROGRESS_INIT;CHECK(qaplet_pump(r,&p,NULL)==QAPLET_OK);
    pthread_mutex_lock(&fault_mutex);while(!native_entered)pthread_cond_wait(&fault_condition,&fault_mutex);pthread_mutex_unlock(&fault_mutex);
    CHECK(qaplet_io_cancel(r,&op,NULL)==QAPLET_OK);
    qaplet_io_result pending=QAPLET_IO_RESULT_INIT;CHECK(qaplet_io_copy_result(r,&op,&pending,NULL)==QAPLET_BUSY);
    qaplet_io_operation refused=QAPLET_IO_OPERATION_INIT;CHECK(qaplet_io_write(r,&e,"x",1,&refused,NULL)==QAPLET_BUSY && !refused.generation);
    qaplet_io_snapshot retained=QAPLET_IO_SNAPSHOT_INIT;CHECK(qaplet_get_io_snapshot(r,&retained,NULL)==QAPLET_OK && retained.shared_operations==(kind==4 ? 2 : 1) && retained.shared_data_bytes==sizeof(text)-1+(kind==4 ? 8 : 0) && retained.helper_threads==(kind==4 ? 3 : 2));
    if(kind==4)CHECK(qaplet_close(r,NULL)==QAPLET_OK && qaplet_quiesce(r,0,&drive,NULL)==QAPLET_OK && drive.reason==QAPLET_DRIVE_WAIT_TIMEOUT && qaplet_destroy(r,NULL)==QAPLET_BUSY);
    pthread_mutex_lock(&fault_mutex);native_release=1;pthread_cond_broadcast(&fault_condition);pthread_mutex_unlock(&fault_mutex);
    if(kind==4)CHECK(qaplet_quiesce(r,5000,&drive,NULL)==QAPLET_OK && drive.reason==QAPLET_DRIVE_QUIESCENT);
  }
  if(kind!=4){
    CHECK(qaplet_run(r,&drive,NULL)==QAPLET_OK);
    qaplet_io_result result=QAPLET_IO_RESULT_INIT;qaplet_tty_write_status text_status=QAPLET_TTY_WRITE_STATUS_INIT;
    struct timespec start,now;CHECK(!clock_gettime(CLOCK_MONOTONIC,&start));
    while(qaplet_io_copy_result(r,&op,&result,NULL)==QAPLET_BUSY){CHECK(qaplet_run_once(r,0,&drive,NULL)==QAPLET_OK);sched_yield();CHECK(!clock_gettime(CLOCK_MONOTONIC,&now) && now.tv_sec-start.tv_sec<5);}
    CHECK(qaplet_io_copy_result(r,&op,&result,NULL)==QAPLET_OK && qaplet_get_tty_write_status(r,&op,&text_status,NULL)==QAPLET_OK);
    CHECK(real_partial_written==1 && text_status.native_utf16_units==(kind==1 ? 4 : 2) && text_status.partial_scalar==(kind!=1));
    CHECK(result.length==(kind==1 ? 6 : 1) && text_status.complete_utf8_bytes==result.length && result.code==(kind==1 ? QAPLET_OK : kind==3 ? QAPLET_CANCELLED : QAPLET_IO_ERROR));
    if(kind==2)CHECK(result.error_domain==QAPLET_ERROR_DOMAIN_WIN32 && result.error==5);
    CHECK(qaplet_io_release(r,&op,NULL)==QAPLET_OK && qaplet_close(r,NULL)==QAPLET_OK && qaplet_quiesce(r,5000,&drive,NULL)==QAPLET_OK && drive.reason==QAPLET_DRIVE_QUIESCENT);
  }
  CHECK(real_partial_written==1 && qaplet_destroy(r,NULL)==QAPLET_OK);
  CHECK(GetConsoleMode(console,&checked) && checked==mode && SetStdHandle(-11,original) && CloseHandle(console));
  if(kind==4)CHECK(GetConsoleMode(input,&checked) && checked==input_mode && SetStdHandle(-10,original_input) && CloseHandle(input));
  puts(kind==1 ? "controlled TTY UTF16 partial: real high surrogate acceptance then successful remaining unit; exact complete UTF8 prefix6 and native units4; cleanup: pass" : kind==2 ? "controlled TTY UTF16 error: real high surrogate acceptance then error5; UTF8 complete prefix1/native units2/partial scalar visible; no retry-offset claim; restored and cleaned: pass" : kind==3 ? "controlled TTY UTF16 cancel: physical BUSY retains count/data/helper; real high surrogate side effect settles as CANCELLED prefix1/native units2/partial scalar; restored and cleaned: pass" : "controlled TTY joined cancel-close: pending input plus real output high surrogate side effect held across CLOSED; both buffers/counts/helpers and group lease retained; destroy BUSY; all native exit/join precedes full restoration: pass");return 0;
}
static int js_partial(void){
  CHECK(IsWindows());int64_t original=GetStdHandle(-11),console=CreateFile(u"CONOUT$",0xc0000000u,3,NULL,3,0,0),original_input=GetStdHandle(-10),input=CreateFile(u"CONIN$",0xc0000000u,3,NULL,3,0,0);uint32_t mode,checked,input_mode;CHECK(console!=-1 && input!=-1 && GetConsoleMode(input,&input_mode) && GetConsoleMode(console,&mode) && SetStdHandle(-11,console) && SetStdHandle(-10,input));
  qaplet_config b=QAPLET_CONFIG_INIT;qaplet_m2_config m=QAPLET_M2_CONFIG_INIT;qaplet_runtime *r=NULL;CHECK(qaplet_create_m2(&b,&m,&r,NULL)==QAPLET_OK);atomic_store(&partial_kind,2);
  const char *source="import('qaplet:stdio').then(async m=>{const input=m.acquireTerminal(0,{raw:true}),e=input.joinTerminal(1,{vt:true});const pending=input.read(8).promise.catch(x=>{if(x.code!==28)throw x});try{await e.write(new Uint8Array([65,240,159,153,130,90])).promise;throw Error('success')}catch(x){if(x.code!==30||x.errorDomain!==2||x.error!==5||x.bytesWritten!==1||x.nativeUtf16Units!==2||x.partialScalar!==true)throw x}await Promise.all([e.closed,input.closed,pending]);return 42})";
  qaplet_progress p=QAPLET_PROGRESS_INIT;qaplet_drive_result drive=QAPLET_DRIVE_RESULT_INIT;qaplet_result result=QAPLET_RESULT_INIT;CHECK(qaplet_eval(r,source,strlen(source),&p,NULL)==QAPLET_OK && qaplet_run(r,&drive,NULL)==QAPLET_OK && drive.phase==QAPLET_PHASE_DONE && qaplet_get_result(r,&result,NULL)==QAPLET_OK && result.type==QAPLET_VALUE_DOUBLE && result.number==42 && real_partial_written==1);
  qaplet_code close_code=qaplet_close(r,NULL),wait_code=qaplet_quiesce(r,5000,&drive,NULL);
  if(close_code || wait_code || drive.reason!=QAPLET_DRIVE_QUIESCENT){qaplet_m2_snapshot native=QAPLET_M2_SNAPSHOT_INIT;qaplet_io_snapshot io=QAPLET_IO_SNAPSHOT_INIT;qaplet_m2_diagnostic diagnostic=QAPLET_M2_DIAGNOSTIC_INIT;qaplet_get_m2_snapshot(r,&native,NULL);qaplet_get_io_snapshot(r,&io,NULL);qaplet_get_m2_diagnostic(r,&diagnostic,NULL);fprintf(stderr,"TTY close detail: close=%d wait=%d reason=%d native=%d handles=%llu uncertain=%llu endpoints=%llu ops=%llu helpers=%llu cleanup=%d/%u/%d\n",close_code,wait_code,drive.reason,native.native_state,(unsigned long long)native.native_handles,(unsigned long long)native.native_uncertain_handles,(unsigned long long)io.endpoints,(unsigned long long)io.operations,(unsigned long long)io.helper_threads,diagnostic.cleanup_reason,diagnostic.cleanup_error_domain,diagnostic.cleanup_error);}
  CHECK(close_code==QAPLET_OK && wait_code==QAPLET_OK && drive.reason==QAPLET_DRIVE_QUIESCENT && qaplet_destroy(r,NULL)==QAPLET_OK);
  CHECK(GetConsoleMode(console,&checked) && checked==mode && GetConsoleMode(input,&checked) && checked==input_mode && SetStdHandle(-11,original) && SetStdHandle(-10,original_input) && CloseHandle(console) && CloseHandle(input));puts("controlled TTY joined JS UTF16 error: real partial scalar reports prefix1/native units2/partialScalar/error5; group error cancels pending input; both closed Promises wait for all helpers and full restoration: pass");return 0;
}
static qaplet_code stage(qaplet_runtime *r,uint64_t call,size_t argc,const qaplet_value *argv,void *opaque){(void)r;(void)call;(void)argc;(void)argv;(void)opaque;atomic_store(&stage_entered,1);return QAPLET_OK;}
static void *release_exit_after_wait(void *unused){(void)unused;pthread_mutex_lock(&fault_mutex);while(!wait_entered && !producer_abort)pthread_cond_wait(&fault_condition,&fault_mutex);native_release=1;pthread_cond_broadcast(&fault_condition);pthread_mutex_unlock(&fault_mutex);return NULL;}
static int exit_wake(void){
  CHECK(IsWindows() && !pthread_key_create(&exit_key,exit_destructor));int64_t saved=GetStdHandle(-11),console=CreateFile(u"CONOUT$",0xc0000000u,3,NULL,3,0,0);uint32_t before,after;CHECK(console!=-1 && GetConsoleMode(console,&before) && SetStdHandle(-11,console));
  qaplet_config b=QAPLET_CONFIG_INIT;qaplet_m2_config m=QAPLET_M2_CONFIG_INIT;qaplet_runtime *r=NULL;CHECK(qaplet_create_m2(&b,&m,&r,NULL)==QAPLET_OK);
  qaplet_host_export export=QAPLET_HOST_EXPORT_INIT;export.name="stage";export.name_length=5;export.kind=QAPLET_HOST_FUNCTION;export.function=stage;qaplet_host_module host=QAPLET_HOST_MODULE_INIT;host.name="host:tty-exit";host.name_length=strlen(host.name);host.exports=&export;host.export_count=1;CHECK(qaplet_register_host_module(r,&host,NULL)==QAPLET_OK);atomic_store(&exit_gate,1);
  const char *source="Promise.all([import('qaplet:stdio'),import('host:tty-exit')]).then(async([m,h])=>{const e=m.acquireTerminal(1,{vt:true});await e.write(new Uint8Array([65])).promise;h.stage();await e.releaseTerminal();return 42})";
  qaplet_progress p=QAPLET_PROGRESS_INIT;qaplet_drive_result drive=QAPLET_DRIVE_RESULT_INIT;CHECK(qaplet_eval(r,source,strlen(source),&p,NULL)==QAPLET_OK);
  struct timespec start,now;CHECK(!clock_gettime(CLOCK_MONOTONIC,&start));
  for(;;){CHECK(qaplet_run_once(r,0,&drive,NULL)==QAPLET_OK);pthread_mutex_lock(&fault_mutex);int gated=native_entered;pthread_mutex_unlock(&fault_mutex);if(gated && atomic_load(&stage_entered))break;CHECK(!clock_gettime(CLOCK_MONOTONIC,&now) && now.tv_sec-start.tv_sec<5);sched_yield();}
  qaplet_io_snapshot held=QAPLET_IO_SNAPSHOT_INIT;CHECK(qaplet_get_io_snapshot(r,&held,NULL)==QAPLET_OK && held.operations==1 && held.helper_threads>0);
  pthread_t producer;atomic_store(&arm_wait,1);CHECK(!pthread_create(&producer,NULL,release_exit_after_wait,NULL));qaplet_code code=qaplet_run_once(r,2000,&drive,NULL);atomic_store(&arm_wait,0);
  pthread_mutex_lock(&fault_mutex);producer_abort=1;native_release=1;pthread_cond_broadcast(&fault_condition);pthread_mutex_unlock(&fault_mutex);CHECK(!pthread_join(producer,NULL));
  int automatic=code==QAPLET_OK && drive.phase==QAPLET_PHASE_DONE && wait_entered && wait_result>0;
  qaplet_result result=QAPLET_RESULT_INIT;if(automatic)automatic=qaplet_get_result(r,&result,NULL)==QAPLET_OK && result.type==QAPLET_VALUE_DOUBLE && result.number==42;
  CHECK(qaplet_close(r,NULL)==QAPLET_OK && qaplet_quiesce(r,5000,&drive,NULL)==QAPLET_OK && drive.reason==QAPLET_DRIVE_QUIESCENT && qaplet_destroy(r,NULL)==QAPLET_OK && !pthread_key_delete(exit_key));
  CHECK(GetConsoleMode(console,&after) && after==before && SetStdHandle(-11,saved) && CloseHandle(console));
  if(!automatic){fprintf(stderr,"TTY actual helper exit wake: code=%d native_wait_entered=%d poll_result=%d; cleanup completed\n",code,wait_entered,wait_result);return 1;}
  puts("controlled TTY actual exit wake: real Console worker TLS destructor holds physical join after IO completion; closed keeps ref; actual OS wait then native exit/join reliably wakes owner; no Host wake/polling; restored and cleaned: pass");return 0;
}
static int control_lifetime(int fail_start){
  CHECK(IsWindows());int64_t saved=GetStdHandle(-11),console=CreateFile(u"CONOUT$",0xc0000000u,3,NULL,3,0,0);uint32_t before,after;CHECK(console!=-1 && GetConsoleMode(console,&before) && SetStdHandle(-11,console));
  qaplet_config b=QAPLET_CONFIG_INIT;qaplet_m2_config m=QAPLET_M2_CONFIG_INIT;qaplet_runtime *r=NULL;CHECK(qaplet_create_m2(&b,&m,&r,NULL)==QAPLET_OK);
  qaplet_tty_options options=QAPLET_TTY_OPTIONS_INIT;options.flags=QAPLET_TTY_VT;qaplet_io_endpoint e=QAPLET_IO_ENDPOINT_INIT;qaplet_tty_status status=QAPLET_TTY_STATUS_INIT;
  if(fail_start){
    atomic_store(&fail_created_thread,1);qaplet_code code=qaplet_tty_acquire(r,1,&options,&e,&status,NULL);atomic_store(&fail_created_thread,0);
    qaplet_io_snapshot io=QAPLET_IO_SNAPSHOT_INIT;qaplet_snapshot state=QAPLET_SNAPSHOT_INIT;CHECK(code==QAPLET_IO_ERROR && !e.generation && status.error_domain==QAPLET_ERROR_DOMAIN_ERRNO && status.error==EAGAIN && qaplet_get_io_snapshot(r,&io,NULL)==QAPLET_OK && !io.endpoints && !io.operations && !io.helper_threads && qaplet_get_snapshot(r,&state,NULL)==QAPLET_OK && state.lifecycle==QAPLET_OPEN && GetConsoleMode(console,&after) && after==before);
    CHECK(qaplet_tty_acquire(r,1,&options,&e,&status,NULL)==QAPLET_OK && qaplet_tty_release(r,&e,&status,NULL)==QAPLET_OK && !finish_tty(r));
  }else{
    CHECK(!pthread_key_create(&exit_key,exit_destructor));atomic_store(&gate_created_thread,1);CHECK(qaplet_tty_acquire(r,1,&options,&e,&status,NULL)==QAPLET_OK);atomic_store(&gate_created_thread,0);
    CHECK(qaplet_tty_release(r,&e,&status,NULL)==QAPLET_OK && status.restored && qaplet_close(r,NULL)==QAPLET_OK);
    pthread_mutex_lock(&fault_mutex);while(!native_entered)pthread_cond_wait(&fault_condition,&fault_mutex);pthread_mutex_unlock(&fault_mutex);
    qaplet_drive_result drive=QAPLET_DRIVE_RESULT_INIT;qaplet_m2_snapshot native=QAPLET_M2_SNAPSHOT_INIT;qaplet_io_snapshot io=QAPLET_IO_SNAPSHOT_INIT;
    CHECK(qaplet_quiesce(r,0,&drive,NULL)==QAPLET_OK && drive.reason==QAPLET_DRIVE_WAIT_TIMEOUT && qaplet_destroy(r,NULL)==QAPLET_BUSY && qaplet_get_m2_snapshot(r,&native,NULL)==QAPLET_OK && !native.engine_current_bytes && native.native_handles==1 && qaplet_get_io_snapshot(r,&io,NULL)==QAPLET_OK && io.helper_threads==1 && io.helper_stack_bytes==1048576);
    pthread_mutex_lock(&fault_mutex);native_release=1;pthread_cond_broadcast(&fault_condition);pthread_mutex_unlock(&fault_mutex);
    CHECK(qaplet_quiesce(r,5000,&drive,NULL)==QAPLET_OK && drive.reason==QAPLET_DRIVE_QUIESCENT && qaplet_destroy(r,NULL)==QAPLET_OK && !pthread_key_delete(exit_key));
  }
  CHECK(GetConsoleMode(console,&after) && after==before && SetStdHandle(-11,saved) && CloseHandle(console));
  puts(fail_start ? "controlled TTY control startup: pthread EAGAIN refuses lease before mode mutation; original errno and OPEN retained; no accepted endpoint/helper; retry succeeds and all native resources retire: pass" : "controlled TTY control exit: real control-thread TLS destructor retains one helper/handle/requested stack after JS close; zero quiesce pending and destroy BUSY; actual exit/join retires control resource: pass");return 0;
}
static int io_error(void){
  CHECK(!IsWindows());int master,slave,saved[2]={dup(0),dup(1)};struct termios before,after;CHECK(saved[0]>=0 && saved[1]>=0 && !private_pty(&master,&slave) && !tcgetattr(slave,&before) && dup2(slave,0)==0 && dup2(slave,1)==1);
  qaplet_config b=QAPLET_CONFIG_INIT;qaplet_m2_config m=QAPLET_M2_CONFIG_INIT;m.limits.operations=2;qaplet_runtime *r=NULL;CHECK(qaplet_create_m2(&b,&m,&r,NULL)==QAPLET_OK);
  qaplet_tty_options raw=QAPLET_TTY_OPTIONS_INIT,vt=QAPLET_TTY_OPTIONS_INIT;raw.flags=QAPLET_TTY_RAW;vt.flags=QAPLET_TTY_VT;qaplet_tty_status status=QAPLET_TTY_STATUS_INIT;qaplet_io_endpoint input=QAPLET_IO_ENDPOINT_INIT,output=QAPLET_IO_ENDPOINT_INIT;CHECK(qaplet_tty_acquire(r,0,&raw,&input,&status,NULL)==QAPLET_OK && qaplet_tty_join(r,&input,1,&vt,&output,&status,NULL)==QAPLET_OK);
  qaplet_io_operation in=QAPLET_IO_OPERATION_INIT,out=QAPLET_IO_OPERATION_INIT;CHECK(qaplet_io_read(r,&input,4,&in,NULL)==QAPLET_OK && qaplet_io_write(r,&output,"data",4,&out,NULL)==QAPLET_OK);atomic_store(&fail_terminal_read,1);
  qaplet_drive_result drive=QAPLET_DRIVE_RESULT_INIT;CHECK(qaplet_run(r,&drive,NULL)==QAPLET_OK);qaplet_io_result result=QAPLET_IO_RESULT_INIT;
  CHECK(qaplet_io_copy_result(r,&in,&result,NULL)==QAPLET_OK && result.code==QAPLET_IO_ERROR && result.error_domain==QAPLET_ERROR_DOMAIN_ERRNO && result.error==EBADF && !result.length);
  CHECK(qaplet_io_copy_result(r,&out,&result,NULL)==QAPLET_OK && result.code==QAPLET_CANCELLED && !result.length && qaplet_get_tty_status(r,&input,&status,NULL)==QAPLET_OK && status.restored && !status.active && !tcgetattr(slave,&after) && same(&before,&after) && !(fcntl(slave,F_GETFL)&O_NONBLOCK));
  qaplet_snapshot state=QAPLET_SNAPSHOT_INIT;CHECK(qaplet_get_snapshot(r,&state,NULL)==QAPLET_OK && state.lifecycle==QAPLET_OPEN && !state.termination_reason && qaplet_io_release(r,&in,NULL)==QAPLET_OK && qaplet_io_release(r,&out,NULL)==QAPLET_OK && !finish_tty(r));
  CHECK(dup2(saved[0],0)==0 && dup2(saved[1],1)==1 && !close(saved[0]) && !close(saved[1]) && !close(slave) && !close(master));puts("controlled TTY POSIX IO error: real EBADF preserves errno; group cancels unissued output without bytes; full mode/flags restore while Runtime OPEN; retained results released and native cleanup complete: pass");return 0;
}
int main(int argc,char **argv){if(argc!=2)return 2;if(!strcmp(argv[1],"--io-error"))return io_error();if(!strcmp(argv[1],"--control-start"))return control_lifetime(1);if(!strcmp(argv[1],"--control-exit"))return control_lifetime(0);if(!strcmp(argv[1],"--exit-wake"))return exit_wake();if(!strcmp(argv[1],"--restore"))return restore_failure();if(!strcmp(argv[1],"--partial"))return partial(1);if(!strcmp(argv[1],"--partial-error"))return partial(2);if(!strcmp(argv[1],"--partial-cancel"))return partial(3);if(!strcmp(argv[1],"--partial-close"))return partial(4);if(!strcmp(argv[1],"--js-partial"))return js_partial();return 2;}
