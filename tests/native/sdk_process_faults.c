/* SPDX-License-Identifier: MIT */
/* Controlled system boundary, compiled outside the source tree against the SDK. */
#define _COSMO_SOURCE 1
#include "qaplet.h"
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <sched.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>
#include "libc/dce.h"
#define CHECK(c) do { if (!(c)) { fprintf(stderr,"process system boundary line %d: %s errno=%d\n",__LINE__,#c,errno); return 1; } } while (0)
extern int sys_waitid(int,int,void *,int,void *);
extern int __real_pthread_create(pthread_t *,const pthread_attr_t *,void *(*)(void *),void *);
extern void *__real_calloc(size_t,size_t);
extern int __real_fork(void);
static int fault;
static qaplet_runtime *fixture_runtime;
extern size_t __real_mkntcmdline(void *,char *const *,size_t);
size_t __wrap_mkntcmdline(void *command,char *const *argv,size_t capacity) {
  if (fault==5) qaplet_request_stop(fixture_runtime,NULL);
  return __real_mkntcmdline(command,argv,capacity);
}
static pthread_key_t key;
static pthread_mutex_t mutex=PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t condition=PTHREAD_COND_INITIALIZER;
static int destructor_entered,destructor_released;
static void *(*entry)(void *);
static void *argument;
static void destructor(void *unused) {
  (void)unused;
  pthread_mutex_lock(&mutex); destructor_entered=1; pthread_cond_broadcast(&condition);
  while (!destructor_released) pthread_cond_wait(&condition,&mutex);
  pthread_mutex_unlock(&mutex);
}
static void *trampoline(void *unused) {
  (void)unused;
  pthread_setspecific(key,(void *)1);
  return entry(argument);
}
int __wrap_pthread_create(pthread_t *thread,const pthread_attr_t *attributes,void *(*function)(void *),void *data) {
  if (fault==1) return EAGAIN;
  if (fault==2) { entry=function; argument=data; return __real_pthread_create(thread,attributes,trampoline,NULL); }
  return __real_pthread_create(thread,attributes,function,data);
}
void *__wrap_calloc(size_t count,size_t bytes) { return fault==4 ? NULL : __real_calloc(count,bytes); }
int __wrap_fork(void) {
  if (fault==3) { errno=EAGAIN; return -1; }
  if (fault==5) qaplet_request_stop(fixture_runtime,NULL);
  return __real_fork();
}
static int fixture(const char *mode,const char *child) {
  qaplet_config b=QAPLET_CONFIG_INIT; qaplet_m2_config m=QAPLET_M2_CONFIG_INIT; m.limits.processes=1; m.limits.helper_threads=1;
  qaplet_runtime *r=NULL; CHECK(qaplet_create_m2(&b,&m,&r,NULL)==QAPLET_OK);
  fixture_runtime=r;
  qaplet_progress progress=QAPLET_PROGRESS_INIT; CHECK(qaplet_eval(r,"42",2,&progress,NULL)==QAPLET_OK);
  qaplet_process_options o=QAPLET_PROCESS_OPTIONS_INIT;
  o.path=(qaplet_string){child,strlen(child)}; o.flags=QAPLET_PROCESS_REPLACE_ENV;
  qaplet_string args[]={ {child,strlen(child)},{"--empty",7} };
  o.argv=args; o.argc=IsWindows() ? 2 : 1;
  for (int i=0;i<3;++i) o.stdio[i]=QAPLET_PROCESS_IGNORE;
  qaplet_process p=QAPLET_PROCESS_INIT; qaplet_process_status s=QAPLET_PROCESS_STATUS_INIT;
  if (!strcmp(mode,"--physical")) { CHECK(!pthread_key_create(&key,destructor)); fault=2; }
  else fault=!strcmp(mode,"--thread-fail") ? 1 : !strcmp(mode,"--fork-fail") ? 3 : !strcmp(mode,"--stop-create") ? 5 : 4;
  int selected=fault;
  qaplet_code code=qaplet_process_spawn(r,&o,&p,&s,NULL); fault=0;
  qaplet_drive_result drive=QAPLET_DRIVE_RESULT_INIT;
  if (selected==2) {
    CHECK(code==QAPLET_OK);
    CHECK(qaplet_run(r,&drive,NULL)==QAPLET_OK);
    pthread_mutex_lock(&mutex); while (!destructor_entered) pthread_cond_wait(&condition,&mutex); pthread_mutex_unlock(&mutex);
    CHECK(qaplet_process_get_status(r,&p,&s,NULL)==QAPLET_OK && s.reaped && s.helper_active && s.native_exit_code==(IsWindows() ? 37 : 0));
    CHECK(qaplet_process_release(r,&p,NULL)==QAPLET_OK);
    CHECK(qaplet_process_get_status(r,&p,&s,NULL)==QAPLET_INVALID_HANDLE);
    qaplet_process extra=QAPLET_PROCESS_INIT;
    CHECK(qaplet_process_spawn(r,&o,&extra,&s,NULL)==QAPLET_RESOURCE_LIMIT && !extra.runtime_id);
    CHECK(qaplet_request_stop(r,NULL)==QAPLET_OK && qaplet_run_once(r,0,&drive,NULL)==QAPLET_EXECUTION_STOPPED);
  } else if (selected==5) {
    CHECK(code==QAPLET_BAD_STATE && !p.runtime_id);
    CHECK(qaplet_run_once(r,0,&drive,NULL)==QAPLET_EXECUTION_STOPPED);
  } else {
    CHECK(code==(selected==4 ? QAPLET_MEMORY_LIMIT : QAPLET_PROCESS_ERROR) && !p.runtime_id);
    if (selected!=4) { CHECK(s.error_domain==QAPLET_ERROR_DOMAIN_ERRNO && s.error==EAGAIN); qaplet_result result=QAPLET_RESULT_INIT; CHECK(qaplet_get_result(r,&result,NULL)==QAPLET_OK && result.number==42); }
    qaplet_snapshot snap=QAPLET_SNAPSHOT_INIT; CHECK(qaplet_get_snapshot(r,&snap,NULL)==QAPLET_OK && snap.lifecycle==(selected==4 ? QAPLET_FAILED : QAPLET_OPEN));
  }
  CHECK(qaplet_close(r,NULL)==QAPLET_OK);
  if (selected==2) {
    CHECK(qaplet_quiesce(r,0,&drive,NULL)==QAPLET_OK && drive.reason==QAPLET_DRIVE_WAIT_TIMEOUT);
    CHECK(qaplet_quiesce(r,25,&drive,NULL)==QAPLET_OK && drive.reason==QAPLET_DRIVE_WAIT_TIMEOUT);
    CHECK(qaplet_destroy(r,NULL)==QAPLET_BUSY);
    qaplet_process_snapshot snapshot=QAPLET_PROCESS_SNAPSHOT_INIT;
    CHECK(qaplet_get_process_snapshot(r,&snapshot,NULL)==QAPLET_OK && snapshot.helper_threads==1 && snapshot.helper_stack_bytes==1048576 && snapshot.processes==1);
    pthread_mutex_lock(&mutex); destructor_released=1; pthread_cond_broadcast(&condition); pthread_mutex_unlock(&mutex);
  }
  CHECK(qaplet_quiesce(r,5000,&drive,NULL)==QAPLET_OK && drive.reason==QAPLET_DRIVE_QUIESCENT);
  qaplet_m2_snapshot snapshot=QAPLET_M2_SNAPSHOT_INIT; CHECK(qaplet_get_m2_snapshot(r,&snapshot,NULL)==QAPLET_OK && !snapshot.native_handles && !snapshot.native_uncertain_handles && !snapshot.reactor_registrations);
  CHECK(qaplet_destroy(r,NULL)==QAPLET_OK);
  if (selected==2) CHECK(!pthread_key_delete(key));
  puts("M2 process system boundary: controlled failure/physical exit; retained capacity; safe close/quiesce/destroy: pass"); return 0;
}
static _Atomic int entered,returned;
static int observed,observed_error,wait_pid;
static void handler(int signal) { (void)signal; }
static void *interrupted(void *unused) {
  (void)unused; siginfo_t info;
  atomic_store(&entered,1); errno=0;
  observed=sys_waitid(1,wait_pid,&info,WEXITED|WNOWAIT,NULL); observed_error=errno;
  atomic_store(&returned,1); return NULL;
}
static int wait_probe(void) {
  CHECK(!IsWindows());
  siginfo_t info; errno=0;
  CHECK(sys_waitid(-1,1,&info,WEXITED|WNOWAIT,NULL)==-1 && errno==EINVAL);
  int invalid_errno=errno,gate[2]; CHECK(!pipe2(gate,O_CLOEXEC));
  pid_t pid=fork(); CHECK(pid>=0);
  if (!pid) { close(gate[1]); char byte; read(gate[0],&byte,1); _exit(37); }
  close(gate[0]); wait_pid=pid;
  struct sigaction action={0},old; action.sa_handler=handler; sigemptyset(&action.sa_mask); CHECK(!sigaction(SIGUSR1,&action,&old));
  pthread_t thread; CHECK(!pthread_create(&thread,NULL,interrupted,NULL));
  while (!atomic_load(&entered)) sched_yield();
  while (!atomic_load(&returned)) { int error=pthread_kill(thread,SIGUSR1); CHECK(!error || (error==ESRCH && atomic_load(&returned))); sched_yield(); }
  CHECK(!pthread_join(thread,NULL)); CHECK(observed==-1 && observed_error==EINTR); CHECK(!sigaction(SIGUSR1,&old,NULL));
  close(gate[1]);
  CHECK(!sys_waitid(1,pid,&info,WEXITED|WNOWAIT,NULL));
  CHECK(!sys_waitid(1,pid,&info,WEXITED|WNOWAIT|WNOHANG,NULL));
  CHECK(!kill(pid,SIGKILL)); /* exit won; WNOWAIT still owns the zombie PID. */
  int status=0; CHECK(waitpid(pid,&status,0)==pid && WIFEXITED(status) && WEXITSTATUS(status)==37);
  errno=0; CHECK(sys_waitid(1,pid,&info,WEXITED|WNOWAIT,NULL)==-1 && errno==ECHILD); int child_errno=errno;
  CHECK(!pipe2(gate,O_CLOEXEC)); pid=fork(); CHECK(pid>=0);
  if (!pid) { close(gate[1]); char byte; read(gate[0],&byte,1); _exit(99); }
  close(gate[0]); CHECK(!kill(pid,SIGKILL)); CHECK(!sys_waitid(1,pid,&info,WEXITED|WNOWAIT,NULL));
  CHECK(waitpid(pid,&status,0)==pid && WIFSIGNALED(status) && WTERMSIG(status)==SIGKILL); close(gate[1]);
  printf("{\"invalid_errno\":%d,\"interrupted_errno\":%d,\"reaped_errno\":%d,\"exit_first\":37,\"kill_first_signal\":%d,\"waitnowait_twice\":true,\"WEXITED\":%d,\"WNOWAIT\":%d,\"opaque_siginfo_bytes\":%zu}\n",invalid_errno,observed_error,child_errno,SIGKILL,WEXITED,WNOWAIT,sizeof(info));
  puts("M2 process waitid: actual pinned thunk; WNOWAIT; EINTR/EINVAL/ECHILD; exit/kill orders; real reap: pass"); return 0;
}
int main(int argc,char **argv) {
  if (argc==2 && !strcmp(argv[1],"--waitid")) return wait_probe();
  return argc==3 ? fixture(argv[1],argv[2]) : 2;
}
