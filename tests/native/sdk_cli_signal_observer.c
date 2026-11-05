/* Observes public SDK states and actual native direct-child reap, no fault.
 * No SDK private headers or layouts. CLI product control flow is unchanged. */
#define _COSMO_SOURCE 1
#include "qaplet.h"
#include <pthread.h>
#include <stdio.h>
#include <sys/wait.h>
#include <unistd.h>
#include "libc/dce.h"
#include "libc/nt/accounting.h"
#include "libc/nt/process.h"
#include "libc/nt/runtime.h"
#include "libc/nt/synchronization.h"
#include "libc/nt/struct/startupinfo.h"
static pthread_mutex_t log_mutex=PTHREAD_MUTEX_INITIALIZER;
static int64_t child_handle;static uint32_t child_pid;
static void record(const char *text){pthread_mutex_lock(&log_mutex);FILE *f=fopen("signal-native.ndjson","ab");if(f){fputs(text,f);fclose(f);}pthread_mutex_unlock(&log_mutex);}
extern __typeof__(CreateProcess) __real_CreateProcess;
bool32 __wrap_CreateProcess(const char16_t *app,char16_t *command,struct NtSecurityAttributes *process_attr,struct NtSecurityAttributes *thread_attr,bool32 inherit,uint32_t flags,void *environment,const char16_t *cwd,const struct NtStartupInfo *startup,struct NtProcessInformation *process){
  bool32 result=__real_CreateProcess(app,command,process_attr,thread_attr,inherit,flags,environment,cwd,startup,process);
  if(result){child_handle=process->hProcess;child_pid=process->dwProcessId;char text[200];snprintf(text,sizeof(text),"{\"operation\":\"native-child-created\",\"pid\":%u}\n",child_pid);record(text);}return result;
}
extern bool32 __real_TerminateProcess(int64_t,uint32_t);
bool32 __wrap_TerminateProcess(int64_t handle,uint32_t code){bool32 result=__real_TerminateProcess(handle,code);if(handle==child_handle && result)record("{\"operation\":\"native-force-accepted\"}\n");return result;}
extern bool32 __real_GetExitCodeProcess(int64_t,uint32_t *);
bool32 __wrap_GetExitCodeProcess(int64_t handle,uint32_t *code){bool32 result=__real_GetExitCodeProcess(handle,code);if(handle==child_handle && result && *code!=259){char text[200];snprintf(text,sizeof(text),"{\"operation\":\"native-child-reaped\",\"pid\":%u,\"exit_code\":%u}\n",child_pid,*code);record(text);}return result;}
extern pid_t __real_waitpid(pid_t,int *,int);
pid_t __wrap_waitpid(pid_t pid,int *status,int flags){pid_t result=__real_waitpid(pid,status,flags);if(result>0){char text[240];snprintf(text,sizeof(text),"{\"operation\":\"native-child-reaped\",\"pid\":%d,\"raw_wait_status\":%d,\"signal\":%u}\n",result,status ? *status:0,status && WIFSIGNALED(*status) ? WTERMSIG(*status):0);record(text);}return result;}
extern qaplet_code __real_qaplet_io_read(qaplet_runtime *,const qaplet_io_endpoint *,size_t,qaplet_io_operation *,qaplet_diagnostic *);
qaplet_code __wrap_qaplet_io_read(qaplet_runtime *r,const qaplet_io_endpoint *endpoint,size_t n,qaplet_io_operation *operation,qaplet_diagnostic *diagnostic){
  qaplet_code code=__real_qaplet_io_read(r,endpoint,n,operation,diagnostic);
  if(!code){qaplet_tty_status tty=QAPLET_TTY_STATUS_INIT;
    if(!qaplet_get_tty_status(r,endpoint,&tty,NULL) && tty.active){
      qaplet_process_snapshot process=QAPLET_PROCESS_SNAPSHOT_INIT;qaplet_resource_snapshot resources=QAPLET_RESOURCE_SNAPSHOT_INIT;
      qaplet_get_process_snapshot(r,&process,NULL);qaplet_get_resource_snapshot(r,&resources,NULL);
      char text[300];snprintf(text,sizeof(text),"{\"operation\":\"mixed-tty-read-accepted\",\"processes\":%llu,\"operations\":%llu,\"io_endpoints\":%llu}\n",(unsigned long long)process.processes,(unsigned long long)resources.operations,(unsigned long long)resources.io_endpoints);record(text);
      FILE *ready=fopen("mixed-ready","wb");if(ready){fputs("public TTY read accepted with live child and timer",ready);fclose(ready);}
    }
  }return code;
}
extern qaplet_code __real_qaplet_close(qaplet_runtime *,qaplet_diagnostic *);
qaplet_code __wrap_qaplet_close(qaplet_runtime *r,qaplet_diagnostic *diagnostic){qaplet_code code=__real_qaplet_close(r,diagnostic);qaplet_snapshot snap=QAPLET_SNAPSHOT_INIT;qaplet_get_snapshot(r,&snap,NULL);char text[180];snprintf(text,sizeof(text),"{\"operation\":\"owner-close\",\"code\":%u,\"termination_reason\":%u}\n",code,snap.termination_reason);record(text);return code;}
extern qaplet_code __real_qaplet_quiesce(qaplet_runtime *,uint64_t,qaplet_drive_result *,qaplet_diagnostic *);
qaplet_code __wrap_qaplet_quiesce(qaplet_runtime *r,uint64_t wait,qaplet_drive_result *drive,qaplet_diagnostic *diagnostic){qaplet_code code=__real_qaplet_quiesce(r,wait,drive,diagnostic);char text[180];snprintf(text,sizeof(text),"{\"operation\":\"native-quiesce\",\"code\":%u,\"reason\":%u,\"budget_ms\":%llu}\n",code,drive->reason,(unsigned long long)wait);record(text);return code;}
extern qaplet_code __real_qaplet_destroy(qaplet_runtime *,qaplet_diagnostic *);
qaplet_code __wrap_qaplet_destroy(qaplet_runtime *r,qaplet_diagnostic *diagnostic){qaplet_code code=__real_qaplet_destroy(r,diagnostic);char text[120];snprintf(text,sizeof(text),"{\"operation\":\"runtime-destroy\",\"code\":%u}\n",code);record(text);return code;}
