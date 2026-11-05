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
static _Atomic int fail_restore,partial_kind,partial_seen;
static pthread_mutex_t fault_mutex=PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t fault_condition=PTHREAD_COND_INITIALIZER;
static int native_entered,native_release;
static uint32_t real_partial_written;
int __wrap_tcsetattr(int fd,int action,const struct termios *mode){if(atomic_load(&fail_restore)){errno=EIO;return -1;}return __real_tcsetattr(fd,action,mode);}
bool32 __wrap_SetConsoleMode(int64_t handle,uint32_t mode){if(atomic_load(&fail_restore)){SetLastError(5);return false;}return __real_SetConsoleMode(handle,mode);}
bool32 __wrap_WriteConsole(int64_t handle,const void *text,uint32_t n,uint32_t *written,void *reserved){
  int kind=atomic_load(&partial_kind);
  if(kind && n==2 && !atomic_exchange(&partial_seen,1)){
    bool32 ok=__real_WriteConsole(handle,text,1,written,reserved);real_partial_written=*written;
    if(!ok || *written!=1)return ok;
    if(kind==2){SetLastError(5);return false;}
    if(kind==3){pthread_mutex_lock(&fault_mutex);native_entered=1;pthread_cond_broadcast(&fault_condition);while(!native_release)pthread_cond_wait(&fault_condition,&fault_mutex);pthread_mutex_unlock(&fault_mutex);}
    return true;
  }
  return __real_WriteConsole(handle,text,n,written,reserved);
}
static int restore_failure(void){
  int master=-1,slave=-1,saved=-1;int64_t original=0,console=0;struct termios before,after;uint32_t mode=0,checked=0;
  if(IsWindows()){original=GetStdHandle(-10);console=CreateFile(u"CONIN$",0xc0000000u,3,NULL,3,0,0);CHECK(console!=-1 && GetConsoleMode(console,&mode) && SetStdHandle(-10,console));}
  else{saved=dup(0);CHECK(saved>=0 && !openpty(&master,&slave,NULL,NULL,NULL) && dup2(slave,0)==0 && !tcgetattr(slave,&before));}
  qaplet_config b=QAPLET_CONFIG_INIT;qaplet_m2_config m=QAPLET_M2_CONFIG_INIT;qaplet_runtime *r=NULL,*other=NULL;CHECK(qaplet_create_m2(&b,&m,&r,NULL)==QAPLET_OK && qaplet_create_m2(&b,&m,&other,NULL)==QAPLET_OK);
  qaplet_tty_options options=QAPLET_TTY_OPTIONS_INIT;options.flags=QAPLET_TTY_RAW;qaplet_tty_status status=QAPLET_TTY_STATUS_INIT;qaplet_io_endpoint e=QAPLET_IO_ENDPOINT_INIT,alias=QAPLET_IO_ENDPOINT_INIT;
  CHECK(qaplet_tty_acquire(r,0,&options,&e,&status,NULL)==QAPLET_OK);
  atomic_store(&fail_restore,1);CHECK(qaplet_tty_release(r,&e,&status,NULL)==QAPLET_IO_ERROR && status.active && !status.restored && status.error==(IsWindows() ? 5 : EIO));
  CHECK(qaplet_tty_acquire(other,0,&options,&alias,&status,NULL)==QAPLET_BUSY && !alias.generation);
  CHECK(qaplet_close(r,NULL)==QAPLET_IO_ERROR);
  qaplet_drive_result drive=QAPLET_DRIVE_RESULT_INIT;CHECK(qaplet_quiesce(r,0,&drive,NULL)==QAPLET_IO_ERROR && drive.reason==QAPLET_DRIVE_WAIT_TIMEOUT && qaplet_destroy(r,NULL)==QAPLET_BUSY);
  qaplet_m2_snapshot retained=QAPLET_M2_SNAPSHOT_INIT;CHECK(qaplet_get_m2_snapshot(r,&retained,NULL)==QAPLET_OK && retained.native_handles>0 && retained.reactor_registrations>0);
  atomic_store(&fail_restore,0);CHECK(qaplet_quiesce(r,5000,&drive,NULL)==QAPLET_IO_ERROR && drive.reason==QAPLET_DRIVE_QUIESCENT && qaplet_destroy(r,NULL)==QAPLET_OK);
  CHECK(qaplet_tty_acquire(other,0,&options,&alias,&status,NULL)==QAPLET_OK && qaplet_tty_release(other,&alias,&status,NULL)==QAPLET_OK && status.restored && qaplet_close(other,NULL)==QAPLET_OK && qaplet_destroy(other,NULL)==QAPLET_OK);
  if(IsWindows())CHECK(GetConsoleMode(console,&checked) && checked==mode && SetStdHandle(-10,original) && CloseHandle(console));
  else CHECK(!tcgetattr(slave,&after) && same(&before,&after) && dup2(saved,0)==0 && !close(saved) && !close(slave) && !close(master));
  puts("controlled TTY restore: syscall failure is IO_ERROR; lease and duplicate remain charged and exclude aliases across CLOSED; quiesce pending and destroy BUSY; actual retry restores all state before retirement; original error retained: pass");return 0;
}
static int partial(int kind){
  CHECK(IsWindows());int64_t original=GetStdHandle(-11),console=CreateFile(u"CONOUT$",0xc0000000u,3,NULL,3,0,0);uint32_t mode,checked;
  CHECK(console!=-1 && GetConsoleMode(console,&mode) && SetStdHandle(-11,console));
  qaplet_config b=QAPLET_CONFIG_INIT;qaplet_m2_config m=QAPLET_M2_CONFIG_INIT;m.limits.operations=1;qaplet_runtime *r=NULL;CHECK(qaplet_create_m2(&b,&m,&r,NULL)==QAPLET_OK);
  qaplet_tty_options options=QAPLET_TTY_OPTIONS_INIT;options.flags=QAPLET_TTY_VT;qaplet_tty_status status=QAPLET_TTY_STATUS_INIT;qaplet_io_endpoint e=QAPLET_IO_ENDPOINT_INIT;
  CHECK(qaplet_tty_acquire(r,1,&options,&e,&status,NULL)==QAPLET_OK);atomic_store(&partial_kind,kind);
  const char text[]="A\xf0\x9f\x99\x82Z";qaplet_io_operation op=QAPLET_IO_OPERATION_INIT;CHECK(qaplet_io_write(r,&e,text,sizeof(text)-1,&op,NULL)==QAPLET_OK);
  qaplet_drive_result drive=QAPLET_DRIVE_RESULT_INIT;
  if(kind==3){
    qaplet_progress p=QAPLET_PROGRESS_INIT;CHECK(qaplet_pump(r,&p,NULL)==QAPLET_OK);
    pthread_mutex_lock(&fault_mutex);while(!native_entered)pthread_cond_wait(&fault_condition,&fault_mutex);pthread_mutex_unlock(&fault_mutex);
    CHECK(qaplet_io_cancel(r,&op,NULL)==QAPLET_OK);
    qaplet_io_result pending=QAPLET_IO_RESULT_INIT;CHECK(qaplet_io_copy_result(r,&op,&pending,NULL)==QAPLET_BUSY);
    qaplet_io_operation refused=QAPLET_IO_OPERATION_INIT;CHECK(qaplet_io_write(r,&e,"x",1,&refused,NULL)==QAPLET_BUSY && !refused.generation);
    CHECK(qaplet_close(r,NULL)==QAPLET_OK && qaplet_quiesce(r,0,&drive,NULL)==QAPLET_OK && drive.reason==QAPLET_DRIVE_WAIT_TIMEOUT && qaplet_destroy(r,NULL)==QAPLET_BUSY);
    pthread_mutex_lock(&fault_mutex);native_release=1;pthread_cond_broadcast(&fault_condition);pthread_mutex_unlock(&fault_mutex);
    CHECK(qaplet_quiesce(r,5000,&drive,NULL)==QAPLET_OK && drive.reason==QAPLET_DRIVE_QUIESCENT);
  }else{
    CHECK(qaplet_run(r,&drive,NULL)==QAPLET_OK);
    qaplet_io_result result=QAPLET_IO_RESULT_INIT;qaplet_tty_write_status text_status=QAPLET_TTY_WRITE_STATUS_INIT;
    CHECK(qaplet_io_copy_result(r,&op,&result,NULL)==QAPLET_OK && qaplet_get_tty_write_status(r,&op,&text_status,NULL)==QAPLET_OK);
    CHECK(real_partial_written==1 && text_status.native_utf16_units==(kind==1 ? 4 : 2) && text_status.partial_scalar==(kind==2));
    CHECK(result.length==(kind==1 ? 6 : 1) && text_status.complete_utf8_bytes==result.length && result.code==(kind==1 ? QAPLET_OK : QAPLET_IO_ERROR));
    if(kind==2)CHECK(result.error_domain==QAPLET_ERROR_DOMAIN_WIN32 && result.error==5);
    CHECK(qaplet_io_release(r,&op,NULL)==QAPLET_OK && qaplet_close(r,NULL)==QAPLET_OK && qaplet_quiesce(r,5000,&drive,NULL)==QAPLET_OK && drive.reason==QAPLET_DRIVE_QUIESCENT);
  }
  CHECK(real_partial_written==1 && qaplet_destroy(r,NULL)==QAPLET_OK);
  CHECK(GetConsoleMode(console,&checked) && checked==mode && SetStdHandle(-11,original) && CloseHandle(console));
  puts(kind==1 ? "controlled TTY UTF16 partial: real high surrogate acceptance then successful remaining unit; exact complete UTF8 prefix6 and native units4; cleanup: pass" : kind==2 ? "controlled TTY UTF16 error: real high surrogate acceptance then error5; UTF8 complete prefix1/native units2/partial scalar visible; no retry-offset claim; restored and cleaned: pass" : "controlled TTY UTF16 cancel-close: native high surrogate side effect held across logical cancel and CLOSED; buffers/count/helper/lease retained; destroy BUSY; real exit/join restores and cleans: pass");return 0;
}
int main(int argc,char **argv){if(argc!=2)return 2;if(!strcmp(argv[1],"--restore"))return restore_failure();if(!strcmp(argv[1],"--partial"))return partial(1);if(!strcmp(argv[1],"--partial-error"))return partial(2);if(!strcmp(argv[1],"--partial-cancel"))return partial(3);return 2;}
