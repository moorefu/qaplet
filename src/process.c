/* SPDX-License-Identifier: MIT */
#define _COSMO_SOURCE 1
#include "runtime_internal.h"
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <signal.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>
#include "libc/dce.h"
#include "libc/nt/accounting.h"
#include "libc/nt/process.h"
#include "libc/nt/runtime.h"
#include "libc/nt/struct/startupinfoex.h"
#include "libc/nt/synchronization.h"
#include "libc/proc/ntspawn.h"
#include "libc/str/str.h"
#include "libc/sysv/pib.h"
extern bool32 DuplicateHandle(int64_t,int64_t,int64_t,int64_t *,uint32_t,bool32,uint32_t);
extern int __mkntpath(const char *,char16_t *);
extern char **environ;
/* The pinned archive exports sys_waitid but no public wrapper. Use its OS
 * dispatch thunk and mapped wait options; the native siginfo payload is opaque
 * and never decoded. Linux and Darwin both define P_PID=1. */
extern int sys_waitid(int,int,void *,int,void *);
/* Pinned x64/ARM64 thunks and readdir use Darwin getdirentries(196), whose
 * native entries have a 32-bit inode and name/reclen at offsets 8/4. */
extern long sys_getdirentries(int,void *,size_t,long *);

typedef struct {
  qaplet_runtime *runtime;
  uint64_t generation;
  pthread_t thread;
  int active, accepted, ready, helper, finished, join_retired, released, terminating, identity_uncertain;
  pid_t pid;
  int64_t handle;
  qaplet_process_status status;
} process_slot;
struct qaplet_process_pool {
  process_slot *slots;
  size_t slot_bytes, environment_size, environment_data_bytes;
  char **environment;
  size_t environment_count;
  int closing;
};
static void peak(uint64_t value,uint64_t *p) { if (value>*p) *p=value; }
static void increment(uint64_t *p) { if (*p!=UINT64_MAX) ++*p; }
static void free_locked(qaplet_runtime *r,void *p,size_t n) {
  if (p) { free(p); r->snapshot.native_current_bytes-=n; }
}
static int fits_locked(qaplet_runtime *r,size_t bytes) {
  return bytes<=r->snapshot.limits.native_storage_bytes-r->snapshot.native_current_bytes;
}
static void cleanup_domain_locked(qaplet_runtime *r,int error,uint32_t domain) {
  if (!r->cleanup_reason) { r->cleanup_reason=QAPLET_PROCESS_ERROR; r->cleanup_error=error ? error : EIO; r->process_cleanup_error_domain=domain; }
}
static void cleanup_locked(qaplet_runtime *r,int error) { cleanup_domain_locked(r,error,QAPLET_ERROR_DOMAIN_ERRNO); }
static void acquired(qaplet_runtime *r) {
  pthread_mutex_lock(&r->mutex); ++r->native_handles; peak(r->native_handles,&r->native_handles_peak); pthread_mutex_unlock(&r->mutex);
}
static void closed_fd(qaplet_runtime *r,int *fd) {
  if (*fd<0) return;
  int old=*fd; *fd=-1;
  int error=close(old) ? errno : 0;
  pthread_mutex_lock(&r->mutex); --r->native_handles;
  if (error) { ++r->native_uncertain_handles; cleanup_locked(r,error); }
  pthread_mutex_unlock(&r->mutex);
}
static int private_fd(qaplet_runtime *r,int *fd) {
  if (*fd>=3) return 0;
  pthread_mutex_lock(&r->mutex);
  int full=r->native_handles>=r->m2_limits.native_handles;
  pthread_mutex_unlock(&r->mutex);
  int copy=full ? -1 : fcntl(*fd,F_DUPFD_CLOEXEC,3),error=full ? EMFILE : copy<0 ? errno : 0;
  if (copy>=0) acquired(r);
  closed_fd(r,fd); *fd=copy;
  return error;
}
static void closed_handle_locked(qaplet_runtime *r,int64_t *handle) {
  if (!*handle) return;
  int64_t old=*handle; *handle=0; --r->native_handles;
  if (!CloseHandle(old)) { ++r->native_uncertain_handles; cleanup_domain_locked(r,GetLastError(),QAPLET_ERROR_DOMAIN_WIN32); }
}
static qaplet_code text(qaplet_string s,int nonempty) {
  if ((!s.data && s.length) || (nonempty && !s.length) || s.length==SIZE_MAX) return QAPLET_INVALID_ARGUMENT;
  if (!qaplet_utf8_valid(s.data,s.length)) return QAPLET_INVALID_UTF8;
  if (s.length && memchr(s.data,0,s.length)) return QAPLET_INVALID_ARGUMENT;
  return QAPLET_OK;
}
static int absolute(qaplet_string s) {
  return s.length && (s.data[0]=='/' || (IsWindows() && s.length>2 && s.data[1]==':' && (s.data[2]=='/' || s.data[2]=='\\')));
}
static int add_size(size_t *size,size_t n,size_t limit) {
  if (n>limit || *size>limit-n) return 0;
  *size+=n; return 1;
}
static char *copy_string(char **cursor,qaplet_string s) {
  char *start=*cursor;
  if (s.length) memcpy(start,s.data,s.length);
  start[s.length]=0; *cursor+=s.length+1; return start;
}
static char *copy_path(char **cursor,const char *base,qaplet_string value) {
  char *start=*cursor;
  if (!absolute(value)) { size_t n=strlen(base); memcpy(*cursor,base,n); *cursor+=n; *(*cursor)++='/'; }
  copy_string(cursor,value); return start;
}
qaplet_code qaplet_process_init(qaplet_runtime *r) {
  if (!r->m2_enabled) return QAPLET_OK;
  size_t count=0,bytes=0;
  while (environ[count]) {
    size_t n=strlen(environ[count]);
    qaplet_code code=text((qaplet_string){environ[count],n},1);
    if (code) return code;
    if (!add_size(&bytes,n+1,r->m2_limits.process_data_bytes)) return QAPLET_RESOURCE_LIMIT;
    ++count;
  }
  size_t environment_data_bytes=bytes;
  if (count>SIZE_MAX/sizeof(char *)-1 || !add_size(&bytes,(count+1)*sizeof(char *),SIZE_MAX-sizeof(qaplet_process_pool))) return QAPLET_RESOURCE_LIMIT;
  pthread_mutex_lock(&r->mutex); int fits=fits_locked(r,sizeof(qaplet_process_pool)+bytes); pthread_mutex_unlock(&r->mutex);
  if (!fits) return QAPLET_RESOURCE_LIMIT;
  qaplet_process_pool *pool=qaplet_native_alloc(r,sizeof(*pool));
  if (!pool) return QAPLET_MEMORY_LIMIT;
  memset(pool,0,sizeof(*pool)); r->process_pool=pool;
  pool->environment_size=bytes; pool->environment_count=count;
  pool->environment_data_bytes=environment_data_bytes;
  pool->environment=qaplet_native_alloc(r,bytes);
  if (!pool->environment) return QAPLET_MEMORY_LIMIT;
  char *cursor=(char *)(pool->environment+count+1);
  for (size_t i=0;i<count;++i) pool->environment[i]=copy_string(&cursor,(qaplet_string){environ[i],strlen(environ[i])});
  pool->environment[count]=NULL; r->process_snapshot.environment_bytes=bytes;
  return QAPLET_OK;
}
static void *wait_child(void *opaque) {
  process_slot *slot=opaque; qaplet_runtime *r=slot->runtime;
  pthread_mutex_lock(&r->mutex);
  while (!slot->ready) pthread_cond_wait(&r->native_condition,&r->mutex);
  pid_t pid=slot->pid; int64_t handle=slot->handle;
  pthread_mutex_unlock(&r->mutex);
  int error=0,status=0; uint32_t exit=0; int waited=0;
  if (handle) {
    uint32_t result=WaitForSingleObject(handle,0xffffffffu);
    if (result==0 && GetExitCodeProcess(handle,&exit)) waited=1;
    else error=GetLastError();
    pthread_mutex_lock(&r->mutex);
    if (waited) { slot->status.native_exit_code=exit; closed_handle_locked(r,&slot->handle); }
  } else if (pid>0) {
    /* Observe without reaping first. The unreaped PID cannot be reused while
     * owner termination and the final waitpid are serialized by the mutex. */
    siginfo_t info;
    int result;
    do { result=sys_waitid(1,pid,&info,WEXITED|WNOWAIT,NULL); } while (result<0 && errno==EINTR);
    error=result<0 ? (errno ? errno : EIO) : 0;
    pthread_mutex_lock(&r->mutex);
    if (!error) {
      pid_t got; do { got=waitpid(pid,&status,0); } while (got<0 && errno==EINTR);
      if (got==pid) {
        waited=1; slot->status.raw_wait_status=status;
        slot->status.native_exit_code=WIFEXITED(status) ? WEXITSTATUS(status) : 0;
        slot->status.native_signal=WIFSIGNALED(status) ? WTERMSIG(status) : 0;
      } else error=errno;
    }
  } else pthread_mutex_lock(&r->mutex);
  if (pid>0 || handle) {
    slot->status.exited=slot->status.reaped=waited;
    if (error) { if (slot->status.code!=QAPLET_CANCELLED) slot->status.code=QAPLET_PROCESS_ERROR; slot->status.error_domain=handle ? QAPLET_ERROR_DOMAIN_WIN32 : QAPLET_ERROR_DOMAIN_ERRNO; slot->status.error=error; }
    if (error && !handle) {
      /* Another Host wait/SIGCHLD policy may have removed this child. The
       * numeric PID can already name an unrelated process. Never signal it
       * again or turn absence of ownership into successful native cleanup. */
      slot->identity_uncertain=1; slot->pid=-1; cleanup_locked(r,error);
    }
  }
  slot->finished=1;
  qaplet_wake_locked(r);
  qaplet_notification notification=qaplet_notify_prepare_locked(r);
  pthread_cond_broadcast(&r->native_condition);
  pthread_mutex_unlock(&r->mutex);
  qaplet_notify_dispatch(r,notification);
  return NULL;
}
static process_slot *lookup_locked(qaplet_runtime *r,const qaplet_process *token) {
  if (!r->process_pool || !r->process_pool->slots || token->runtime_id!=r->runtime_id || token->slot>=r->m2_limits.processes) return NULL;
  process_slot *s=&r->process_pool->slots[token->slot];
  return s->active && s->accepted && !s->released && s->generation==token->generation ? s : NULL;
}
static void retire_locked(qaplet_runtime *r,process_slot *s) {
  s->active=0; --r->process_snapshot.processes;
  if (s->accepted && !s->released) --r->process_snapshot.retained_results;
}
void qaplet_process_collect(qaplet_runtime *r) {
  if (!r->process_pool) return;
  pthread_mutex_lock(&r->mutex);
  qaplet_process_pool *pool=r->process_pool;
  if (pool->slots) for (uint64_t i=0;i<r->m2_limits.processes;++i) {
    process_slot *s=&pool->slots[i];
    if (!s->active || !s->finished) continue;
    if (s->helper && !s->join_retired) {
      s->join_retired=1;
      pthread_mutex_unlock(&r->mutex);
      int error=pthread_tryjoin_np(s->thread,NULL);
      pthread_mutex_lock(&r->mutex);
      if (error==EBUSY) s->join_retired=0;
      else if (error) cleanup_locked(r,error);
      else {
        s->helper=0; s->status.helper_active=0; --r->native_retained; --r->native_handles; --r->reactor_registrations;
        --r->process_snapshot.helper_threads;
        r->process_snapshot.helper_stack_bytes-=r->m2_limits.helper_stack_bytes;
        --r->helper_threads; r->helper_stack_bytes-=r->m2_limits.helper_stack_bytes;
      }
    }
    if (!s->identity_uncertain && !s->helper && (!s->accepted || s->released || pool->closing) && (s->pid<=0 && !s->handle || s->status.reaped)) retire_locked(r,s);
  }
  if (pool->closing && !r->process_snapshot.processes && !r->process_snapshot.helper_threads) {
    free_locked(r,pool->environment,pool->environment_size);
    free_locked(r,pool->slots,pool->slot_bytes);
    if (pool->slots) --r->native_retained;
    free_locked(r,pool,sizeof(*pool)); r->process_pool=NULL;
    r->process_snapshot.environment_bytes=0;
  }
  if (r->loop_closed && !r->native_retained && !r->notify_inflight && !r->native_handles && !r->native_uncertain_handles) r->native_state=QAPLET_NATIVE_QUIESCENT;
  pthread_mutex_unlock(&r->mutex);
}
unsigned qaplet_process_handoff(qaplet_runtime *r,uint64_t maximum) {
  unsigned count=0;
  pthread_mutex_lock(&r->mutex);
  qaplet_process_pool *pool=r->process_pool;
  if (pool && pool->slots) for (uint64_t i=0;i<r->m2_limits.processes && count<maximum;++i) {
    process_slot *s=&pool->slots[i];
    if (!s->active || !s->accepted || s->released || !s->finished || s->status.delivered) continue;
    /* The active entry owns its existing ledger. Native-only C completion
     * creates no JS execution and cannot replace the last completed result. */
    if (r->execution_active && qaplet_execution_dispatch_locked(r)) break;
    s->status.delivered=1; ++count; increment(&r->process_snapshot.completed);
    if (s->status.referenced) { s->status.referenced=0; --r->process_snapshot.referenced; }
  }
  pthread_mutex_unlock(&r->mutex);
  return count;
}
int qaplet_process_pending_locked(qaplet_runtime *r) {
  if (!r->process_pool || !r->process_pool->slots) return 0;
  for (uint64_t i=0;i<r->m2_limits.processes;++i) {
    process_slot *s=&r->process_pool->slots[i];
    if (s->active && s->accepted && !s->released && s->finished && !s->status.delivered) return 1;
  }
  return 0;
}
int qaplet_process_referenced_locked(qaplet_runtime *r) { return r->process_snapshot.referenced!=0; }
static void terminate_locked(qaplet_runtime *r,process_slot *s) {
  if (!s->ready || s->status.reaped || s->terminating || s->identity_uncertain || (s->pid<=0 && !s->handle)) return;
  s->terminating=1;
  int error=0; uint32_t domain=s->handle ? QAPLET_ERROR_DOMAIN_WIN32 : QAPLET_ERROR_DOMAIN_ERRNO;
  if (s->handle) {
    if (!TerminateProcess(s->handle,0xD00D)) error=GetLastError();
  } else if (kill(s->pid,SIGKILL) && errno!=ESRCH) error=errno;
  if (error) {
    s->status.error_domain=domain; s->status.error=error;
    cleanup_domain_locked(r,error,domain);
  }
}
static qaplet_code cancel_locked(qaplet_runtime *r,process_slot *s) {
  if (s->status.code==QAPLET_CANCELLED) return QAPLET_OK;
  if (s->finished) return QAPLET_ALREADY_COMPLETED;
  s->status.code=QAPLET_CANCELLED;
  terminate_locked(r,s);
  return QAPLET_OK;
}
qaplet_code qaplet_process_cancel(qaplet_runtime *r,const qaplet_process *token,qaplet_diagnostic *d) {
  qaplet_code code=qaplet_check_diagnostic(d);
  if (!code) code=qaplet_check_structure(token,sizeof(*token));
  if (!code && !r) code=QAPLET_INVALID_ARGUMENT;
  if (code) return qaplet_report(d,code);
  qaplet_notification notification={0};
  pthread_mutex_lock(&r->mutex);
  process_slot *s=lookup_locked(r,token);
  if (!r->m2_enabled) code=QAPLET_UNSUPPORTED;
  else if (!s) code=QAPLET_INVALID_HANDLE;
  else {
    code=cancel_locked(r,s);
    if (!code) {
      /* Accepted state stays accepted if its subsequent wake records a fault. */
      qaplet_wake_locked(r); notification=qaplet_notify_prepare_locked(r);
      pthread_cond_broadcast(&r->native_condition);
    }
  }
  pthread_mutex_unlock(&r->mutex);
  qaplet_notify_dispatch(r,notification);
  return qaplet_report(d,code);
}
void qaplet_process_close(qaplet_runtime *r) {
  if (r->process_pool) {
    pthread_mutex_lock(&r->mutex); r->process_pool->closing=1;
    if (r->process_pool->slots) for (uint64_t i=0;i<r->m2_limits.processes;++i) {
      process_slot *s=&r->process_pool->slots[i];
      if (s->active && s->accepted && !s->released) { s->released=1; --r->process_snapshot.retained_results; }
    }
    pthread_mutex_unlock(&r->mutex);
  }
  qaplet_process_shutdown(r);
}
void qaplet_process_shutdown(qaplet_runtime *r) {
  if (!r->process_pool) return;
  pthread_mutex_lock(&r->mutex);
  qaplet_process_pool *pool=r->process_pool;

  if (pool->slots) for (uint64_t i=0;i<r->m2_limits.processes;++i) {
    process_slot *s=&pool->slots[i];
    if (!s->active) continue;
    if (s->status.referenced) { s->status.referenced=0; --r->process_snapshot.referenced; }
    cancel_locked(r,s);
  }
  pthread_cond_broadcast(&r->native_condition);
  pthread_mutex_unlock(&r->mutex);
  qaplet_process_collect(r);
}
static int close_child_directory(int directory,int acknowledgement) {
  unsigned char buffer[512];long base=0;
  for (;;) {
    long length;
    do { length=sys_getdirentries(directory,buffer,sizeof(buffer),&base); } while (length<0 && errno==EINTR);
    if (length<0) return errno;
    if (!length) return close(directory) ? errno : 0;
    if (length>sizeof(buffer)) return EIO;
    for (size_t offset=0;offset<(size_t)length;) {
      if ((size_t)length-offset<8) return EIO;
      uint16_t record;memcpy(&record,buffer+offset+4,sizeof(record));
      unsigned name_length=buffer[offset+7];
      if (record<9 || record>(size_t)length-offset || name_length>=record-8 || buffer[offset+8+name_length]) return EIO;
      const unsigned char *name=buffer+offset+8;
      if (name_length && name[0]>='0' && name[0]<='9') {
        unsigned fd=0;
        for (unsigned i=0;i<name_length;++i) {
          if (name[i]<'0' || name[i]>'9' || fd>(INT_MAX-(name[i]-'0'))/10u) return EIO;
          fd=fd*10+name[i]-'0';
        }
        /* This is the child's actual FD table after fork: no parent snapshot
         * race, resource-limit bound, allocation, or inherited metadata. Keep
         * only authorized stdio and the CLOEXEC exec-error acknowledgement. */
        if (fd>2 && fd!=directory && fd!=acknowledgement && close(fd) && errno!=EBADF) return errno;
      }
      offset+=record;
    }
  }
}
static int spawn_posix(qaplet_runtime *r,process_slot *s,const char *path,char **argv,char **env,const char *cwd,const qaplet_process_options *o) {
  struct rlimit limits={0};
  if (!IsXnu() && getrlimit(RLIMIT_NOFILE,&limits)) return errno;
  int directory=-1,io[2]={-1,-1},nulls[3]={-1,-1,-1},error=0;
  if (IsXnu()) {
    directory=open("/dev/fd",O_RDONLY|O_DIRECTORY|O_CLOEXEC);
    if (directory<0) return errno;
    acquired(r);error=private_fd(r,&directory);
    if (error) return error;
  }
  if (pipe2(io,O_CLOEXEC)) { error=errno;closed_fd(r,&directory);return error; }
  acquired(r); acquired(r);
  error=private_fd(r,&io[0]);
  if (!error) error=private_fd(r,&io[1]);
  for (int i=0;i<3 && !error;++i) if (o->stdio[i]==QAPLET_PROCESS_IGNORE) {
    nulls[i]=open("/dev/null",(i ? O_WRONLY : O_RDONLY)|O_CLOEXEC);
    if (nulls[i]<0) error=errno; else { acquired(r); error=private_fd(r,&nulls[i]); }
  }
  if (!error) {
    s->pid=fork();
    if (!s->pid) {
      int fail=0;
      if (chdir(cwd)) fail=errno;
      for (int i=0;i<3 && !fail;++i) if (nulls[i]>=0 && dup2(nulls[i],i)<0) fail=errno;
      int closed=0;
      if (IsXnu()) closed=close_child_directory(directory,io[1]);
      else {
        if (io[1]>3 && close_range(3,io[1]-1,0)) for (int fd=3;fd<io[1];++fd) close(fd);
        if (close_range(io[1]+1,UINT_MAX,0)) {
          if (limits.rlim_max>INT_MAX) closed=EOVERFLOW;
          else for (int fd=io[1]+1;fd<(int)limits.rlim_max;++fd) close(fd);
        }
      }
      if (!fail) fail=closed;
      if (!fail) { execve(path,argv,env); fail=errno; }
      ssize_t n; do { n=write(io[1],&fail,sizeof(fail)); } while (n<0 && errno==EINTR);
      _exit(127);
    }
    if (s->pid<0) error=errno;
  }
  closed_fd(r,&directory);
  for (int i=0;i<3;++i) closed_fd(r,&nulls[i]);
  closed_fd(r,&io[1]);
  if (!error) {
    int child_error=0; ssize_t got;
    do { got=read(io[0],&child_error,sizeof(child_error)); } while (got<0 && errno==EINTR);
    if (got) error=got==sizeof(child_error) ? child_error : got<0 ? errno : EIO;
  }
  closed_fd(r,&io[0]); return error;
}
static int spawn_windows(qaplet_runtime *r,process_slot *s,const char *path,char **argv,char **env,const char *cwd,const qaplet_process_options *o,qaplet_process_status *failure) {
  size_t envchars=2;
  for (size_t i=0;env[i];++i) envchars+=strlen(env[i])+1;
  size_t chars=envchars+32768+2*PATH_MAX;
  if (chars>SIZE_MAX/2) return 8;
  size_t storage=chars*2;
  pthread_mutex_lock(&r->mutex); int fits=fits_locked(r,storage); pthread_mutex_unlock(&r->mutex);
  if (!fits) { failure->code=QAPLET_RESOURCE_LIMIT; return 8; }
  char16_t *wide=qaplet_native_alloc(r,storage);
  if (!wide) { failure->code=QAPLET_MEMORY_LIMIT; return 8; }
  char16_t *app=wide,*wcwd=app+PATH_MAX,*command=wcwd+PATH_MAX,*block=command+32768;
  int error=0; int64_t handles[3]={0}; int nulls[3]={-1,-1,-1};
  struct NtStartupInfoEx startup={0}; struct NtProcessInformation process={0};
  size_t attrsize=0;
  InitializeProcThreadAttributeList(NULL,1,0,&attrsize);
  pthread_mutex_lock(&r->mutex); fits=fits_locked(r,attrsize); pthread_mutex_unlock(&r->mutex);
  startup.lpAttributeList=fits ? qaplet_native_alloc(r,attrsize) : NULL;
  int initialized=0;
  if (!startup.lpAttributeList) { failure->code=fits ? QAPLET_MEMORY_LIMIT : QAPLET_RESOURCE_LIMIT; error=8; goto done; }
  if (__mkntpath(path,app)<0 || __mkntpath(cwd,wcwd)<0) { failure->error_domain=QAPLET_ERROR_DOMAIN_ERRNO; failure->error=error=errno; goto done; }
  if (mkntcmdline(command,argv,32768)>=32768) { failure->code=QAPLET_VALUE_TOO_LARGE; error=87; goto done; }
  size_t cursor=0;
  for (size_t i=0;env[i];++i) { tprecode8to16(block+cursor,envchars-cursor,env[i]); while (block[cursor]) ++cursor; ++cursor; }
  block[cursor]=0; if (!cursor) block[1]=0;
  for (int i=0;i<3;++i) {
    int fd=i;
    if (o->stdio[i]==QAPLET_PROCESS_IGNORE) {
      fd=nulls[i]=open("/dev/null",(i ? O_WRONLY : O_RDONLY)|O_CLOEXEC);
      if (fd<0) { error=GetLastError(); goto done; } acquired(r);
    }
    if (fd>=__get_pib()->fds.n || !DuplicateHandle(-1,__get_pib()->fds.p[fd].handle,-1,&handles[i],0,true,2)) { error=GetLastError(); goto done; }
    acquired(r); startup.StartupInfo.stdiofds[i]=handles[i];
  }
  startup.StartupInfo.cb=sizeof(startup); startup.StartupInfo.dwFlags=0x100;
  initialized=InitializeProcThreadAttributeList(startup.lpAttributeList,1,0,&attrsize);
  if (!initialized || !UpdateProcThreadAttribute(startup.lpAttributeList,0,kNtProcThreadAttributeHandleList,handles,sizeof(handles),NULL,NULL) ||
      !CreateProcess(app,command,NULL,NULL,true,0x80000|0x400|0x08000000,block,wcwd,&startup.StartupInfo,&process)) { error=GetLastError(); goto done; }
  acquired(r); acquired(r); s->handle=process.hProcess; s->pid=process.dwProcessId;
  pthread_mutex_lock(&r->mutex); closed_handle_locked(r,&process.hThread); pthread_mutex_unlock(&r->mutex);
 done:
  if (initialized) DeleteProcThreadAttributeList(startup.lpAttributeList);
  qaplet_native_free(r,startup.lpAttributeList,attrsize);
  for (int i=0;i<3;++i) { pthread_mutex_lock(&r->mutex); closed_handle_locked(r,&handles[i]); pthread_mutex_unlock(&r->mutex); closed_fd(r,&nulls[i]); }
  qaplet_native_free(r,wide,storage); return error;
}
qaplet_code qaplet_process_spawn(qaplet_runtime *r,const qaplet_process_options *o,qaplet_process *out,qaplet_process_status *status,qaplet_diagnostic *diagnostic) {
  qaplet_code code=qaplet_check_diagnostic(diagnostic);
  if (!code) code=qaplet_check_structure(o,sizeof(*o));
  if (!code) code=qaplet_check_structure(out,sizeof(*out));
  if (!code) code=qaplet_check_structure(status,sizeof(*status));
  if (!code) code=qaplet_owner(r);
  if (code) return qaplet_report(diagnostic,code);
  qaplet_process_status value=QAPLET_PROCESS_STATUS_INIT;
  if (!r->m2_enabled) code=QAPLET_UNSUPPORTED;
  else if (qaplet_get_lifecycle(r)!=QAPLET_OPEN) code=QAPLET_BAD_STATE;
  else if (o->flags&~(uint64_t)QAPLET_PROCESS_REPLACE_ENV || o->reserved || (!o->argv && o->argc) || (!o->env && o->env_count) || (!(o->flags&QAPLET_PROCESS_REPLACE_ENV) && (o->env || o->env_count))) code=QAPLET_INVALID_ARGUMENT;
  for (int i=0;i<3 && !code;++i) if (o->stdio[i]>QAPLET_PROCESS_IGNORE) code=QAPLET_INVALID_ARGUMENT;
  if (!code) code=text(o->path,1);
  if (!code) code=text(o->cwd,0);
  size_t bytes=0,argc=o->argc ? o->argc : 1,envc=(o->flags&QAPLET_PROCESS_REPLACE_ENV) ? o->env_count : 0;
  if (!code && (argc>r->m2_limits.process_arguments || envc>SIZE_MAX/sizeof(char *)-1)) code=QAPLET_RESOURCE_LIMIT;
  if (!code && (!add_size(&bytes,o->path.length+1,r->m2_limits.process_data_bytes) ||
      !add_size(&bytes,(o->cwd.length ? o->cwd.length : strlen(r->initial_cwd))+1,r->m2_limits.process_data_bytes))) code=QAPLET_RESOURCE_LIMIT;
  if (!code && !absolute(o->path) && !add_size(&bytes,strlen(r->initial_cwd)+1,r->m2_limits.process_data_bytes)) code=QAPLET_RESOURCE_LIMIT;
  if (!code && o->cwd.length && !absolute(o->cwd) && !add_size(&bytes,strlen(r->initial_cwd)+1,r->m2_limits.process_data_bytes)) code=QAPLET_RESOURCE_LIMIT;
  for (size_t i=0;i<o->argc && !code;++i) {
    code=text(o->argv[i],0);
    if (!code && !add_size(&bytes,o->argv[i].length+1,r->m2_limits.process_data_bytes)) code=QAPLET_RESOURCE_LIMIT;
  }
  if (!code && !o->argc && !add_size(&bytes,o->path.length+1,r->m2_limits.process_data_bytes)) code=QAPLET_RESOURCE_LIMIT;
  for (size_t i=0;i<envc && !code;++i) {
    qaplet_string e=o->env[i]; code=text(e,1);
    const char *equals=code ? NULL : memchr(e.data,'=',e.length);
    if (!code && (!equals || equals==e.data)) code=QAPLET_INVALID_ARGUMENT;
    if (!code && !add_size(&bytes,e.length+1,r->m2_limits.process_data_bytes)) code=QAPLET_RESOURCE_LIMIT;
    if (!code) for (size_t j=0;j<i;++j) {
      size_t key=equals-e.data;
      if (o->env[j].length>key && o->env[j].data[key]=='=' && !(IsWindows() ? strncasecmp(e.data,o->env[j].data,key) : memcmp(e.data,o->env[j].data,key))) { code=QAPLET_INVALID_ARGUMENT; break; }
    }
  }
  if (!code && !(o->flags&QAPLET_PROCESS_REPLACE_ENV)) {
    /* Reusing the immutable factory copy avoids duplicate storage; its encoded
     * bytes still belong to this spawn's combined argv/env/path input limit. */
    size_t encoded=bytes;
    if (!add_size(&encoded,r->process_pool->environment_data_bytes,r->m2_limits.process_data_bytes)) code=QAPLET_RESOURCE_LIMIT;
  }
  for (int i=0;i<3 && !code;++i) if (o->stdio[i]==QAPLET_PROCESS_PIPE) code=QAPLET_UNSUPPORTED;
  for (int i=0;i<3 && !code;++i) if (o->stdio[i]==QAPLET_PROCESS_INHERIT && fcntl(i,F_GETFD)<0) {
    code=QAPLET_PROCESS_ERROR; value.error_domain=QAPLET_ERROR_DOMAIN_ERRNO; value.error=errno;
  }
  if (code) goto rejected;
  qaplet_process_collect(r);
  qaplet_process_pool *pool=r->process_pool;
  if (r->m2_limits.processes>SIZE_MAX/sizeof(process_slot) || argc>SIZE_MAX/sizeof(char *)-1 ||
      !add_size(&bytes,(argc+envc+2)*sizeof(char *),SIZE_MAX)) { code=QAPLET_RESOURCE_LIMIT; goto rejected; }
  size_t metadata=pool->slots ? 0 : r->m2_limits.processes*sizeof(process_slot);
  size_t scratch=0;
  if (IsWindows()) {
    size_t envchars=2,attributes=0;
    if (o->flags&QAPLET_PROCESS_REPLACE_ENV) { for (size_t i=0;i<envc;++i) envchars+=o->env[i].length+1; }
    else for (size_t i=0;i<pool->environment_count;++i) envchars+=strlen(pool->environment[i])+1;
    InitializeProcThreadAttributeList(NULL,1,0,&attributes);
    if (envchars>(SIZE_MAX-attributes)/2-32768-2*PATH_MAX) { code=QAPLET_RESOURCE_LIMIT; goto rejected; }
    scratch=(envchars+32768+2*PATH_MAX)*2+attributes;
  }
  size_t handles=IsWindows() ? 6 : 3+!!IsXnu();
  for (int i=0;i<3;++i) handles+=o->stdio[i]==QAPLET_PROCESS_IGNORE;
  if (!IsWindows()) for (int i=0;i<3;++i) if (fcntl(i,F_GETFD)<0) { ++handles; break; }
  pthread_mutex_lock(&r->mutex);
  if (r->process_snapshot.processes>=r->m2_limits.processes || r->helper_threads>=r->m2_limits.helper_threads ||
      handles>r->m2_limits.native_handles-r->native_handles || r->reactor_registrations>=r->m2_limits.reactor_registrations || metadata>SIZE_MAX-bytes || scratch>SIZE_MAX-metadata-bytes || !fits_locked(r,metadata+bytes+scratch)) code=QAPLET_RESOURCE_LIMIT;
  pthread_mutex_unlock(&r->mutex);
  if (code) goto rejected;
  if (!pool->slots) {
    process_slot *slots=qaplet_native_alloc(r,metadata); if (!slots) { code=QAPLET_MEMORY_LIMIT; goto rejected; }
    memset(slots,0,metadata);
    pthread_mutex_lock(&r->mutex);
    pool->slots=slots; pool->slot_bytes=metadata; ++r->native_retained;
    pthread_mutex_unlock(&r->mutex);
  }
  char *data=qaplet_native_alloc(r,bytes);
  if (!data) { code=QAPLET_MEMORY_LIMIT; goto rejected; }
  char **argv=(char **)data,**env=argv+argc+1;
  char *cursor=(char *)(env+envc+1),*path=copy_path(&cursor,r->initial_cwd,o->path);
  char *cwd=o->cwd.length ? copy_path(&cursor,r->initial_cwd,o->cwd) : copy_string(&cursor,(qaplet_string){r->initial_cwd,strlen(r->initial_cwd)});
  for (size_t i=0;i<argc;++i) argv[i]=copy_string(&cursor,o->argc ? o->argv[i] : o->path);
  argv[argc]=NULL;
  for (size_t i=0;i<envc;++i) env[i]=copy_string(&cursor,o->env[i]); env[envc]=NULL;
  if (!(o->flags&QAPLET_PROCESS_REPLACE_ENV)) env=pool->environment;
  qaplet_driver_set(r,1);
  pthread_mutex_lock(&r->mutex);
  process_slot *slot=NULL; uint64_t index=0;
  if (r->snapshot.lifecycle!=QAPLET_OPEN) code=QAPLET_BAD_STATE;
  else for (;index<r->m2_limits.processes;++index) if (!pool->slots[index].active && pool->slots[index].generation!=UINT64_MAX) { slot=&pool->slots[index]; break; }
  if (!code && !slot) code=QAPLET_RESOURCE_LIMIT;
  if (code) { pthread_mutex_unlock(&r->mutex); qaplet_native_free(r,data,bytes); qaplet_driver_set(r,0); goto rejected; }
  uint64_t generation=slot->generation+1;
  memset(slot,0,sizeof(*slot)); slot->generation=generation; slot->runtime=r; slot->pid=-1; slot->active=1; slot->status=(qaplet_process_status)QAPLET_PROCESS_STATUS_INIT;
  ++r->process_snapshot.processes; peak(r->process_snapshot.processes,&r->process_snapshot.processes_peak);
  pthread_attr_t attributes; int error=pthread_attr_init(&attributes);
  if (!error) { error=pthread_attr_setstacksize(&attributes,r->m2_limits.helper_stack_bytes); if (!error) error=pthread_create(&slot->thread,&attributes,wait_child,slot); pthread_attr_destroy(&attributes); }
  if (error) { slot->active=0; --r->process_snapshot.processes; value.error_domain=QAPLET_ERROR_DOMAIN_ERRNO; value.error=error; code=QAPLET_PROCESS_ERROR; }
  else {
    slot->helper=1; slot->status.helper_active=1; ++r->native_retained; ++r->native_handles; ++r->reactor_registrations;
    peak(r->native_handles,&r->native_handles_peak); peak(r->reactor_registrations,&r->reactor_registrations_peak); ++r->process_snapshot.helper_threads;
    r->process_snapshot.helper_stack_bytes+=r->m2_limits.helper_stack_bytes;
    ++r->helper_threads; r->helper_stack_bytes+=r->m2_limits.helper_stack_bytes;
    peak(r->process_snapshot.helper_threads,&r->process_snapshot.helper_threads_peak); peak(r->process_snapshot.helper_stack_bytes,&r->process_snapshot.helper_stack_bytes_peak);
  }
  pthread_mutex_unlock(&r->mutex);
  if (!error) {
    error=IsWindows() ? spawn_windows(r,slot,path,argv,env,cwd,o,&value) : spawn_posix(r,slot,path,argv,env,cwd,o);
    pthread_mutex_lock(&r->mutex);
    slot->ready=1;
    if (error) {
      code=value.code ? value.code : QAPLET_PROCESS_ERROR;
      if (!value.code && !value.error_domain) { value.error_domain=IsWindows() ? QAPLET_ERROR_DOMAIN_WIN32 : QAPLET_ERROR_DOMAIN_ERRNO; value.error=error; }
      value.helper_active=1;
      slot->released=1;
      terminate_locked(r,slot);
    } else if (r->snapshot.lifecycle!=QAPLET_OPEN) {
      /* Stop won before spawn acceptance. The newly created direct child is
       * rollback responsibility, never an accepted user process or valid token. */
      code=QAPLET_BAD_STATE; value.helper_active=1; slot->released=1;
    } else {
      slot->accepted=1; slot->status.referenced=1; ++r->process_snapshot.referenced; ++r->process_snapshot.retained_results; increment(&r->process_snapshot.accepted);
      out->runtime_id=r->runtime_id; out->slot=index; out->generation=generation; value=slot->status;
    }
    pthread_cond_broadcast(&r->native_condition);
    pthread_mutex_unlock(&r->mutex);
  }
  qaplet_native_free(r,data,bytes);
  if (qaplet_get_lifecycle(r)!=QAPLET_OPEN) qaplet_process_shutdown(r);
  qaplet_driver_set(r,0);
 rejected:
  value.code=code; memcpy((char *)status+8,(char *)&value+8,sizeof(value)-8);
  return qaplet_report(diagnostic,code);
}
qaplet_code qaplet_process_get_status(qaplet_runtime *r,const qaplet_process *token,qaplet_process_status *out,qaplet_diagnostic *d) {
  qaplet_code code=qaplet_check_diagnostic(d);
  if (!code) code=qaplet_check_structure(token,sizeof(*token));
  if (!code) code=qaplet_check_structure(out,sizeof(*out));
  if (!code) code=qaplet_owner(r);
  if (!code && !r->m2_enabled) code=QAPLET_UNSUPPORTED;
  if (!code && qaplet_get_lifecycle(r)==QAPLET_CLOSED) code=QAPLET_BAD_STATE;
  if (code) return qaplet_report(d,code);
  qaplet_process_collect(r); pthread_mutex_lock(&r->mutex);
  process_slot *s=lookup_locked(r,token);
  if (!s) code=QAPLET_INVALID_HANDLE; else memcpy((char *)out+8,(char *)&s->status+8,sizeof(*out)-8);
  pthread_mutex_unlock(&r->mutex); return qaplet_report(d,code);
}
qaplet_code qaplet_get_process_status(qaplet_runtime *r,const qaplet_process *token,qaplet_process_status *out,qaplet_diagnostic *d) {
  qaplet_code code=qaplet_check_diagnostic(d);
  if (!code) code=qaplet_check_structure(token,sizeof(*token));
  if (!code) code=qaplet_check_structure(out,sizeof(*out));
  if (!code && !r) code=QAPLET_INVALID_ARGUMENT;
  if (code) return qaplet_report(d,code);
  pthread_mutex_lock(&r->mutex);
  if (!r->m2_enabled) code=QAPLET_UNSUPPORTED;
  else {
    process_slot *s=lookup_locked(r,token);
    if (!s) code=QAPLET_INVALID_HANDLE;
    else memcpy((char *)out+8,(char *)&s->status+8,sizeof(*out)-8);
  }
  pthread_mutex_unlock(&r->mutex); return qaplet_report(d,code);
}
qaplet_code qaplet_process_set_ref(qaplet_runtime *r,const qaplet_process *token,uint32_t ref,qaplet_diagnostic *d) {
  qaplet_code code=qaplet_check_diagnostic(d);
  if (!code) code=qaplet_check_structure(token,sizeof(*token));
  if (!code && ref>1) code=QAPLET_INVALID_ARGUMENT;
  if (!code) code=qaplet_owner(r);
  if (code) return qaplet_report(d,code);
  pthread_mutex_lock(&r->mutex); process_slot *s=lookup_locked(r,token);
  if (!s) code=QAPLET_INVALID_HANDLE;
  else if (r->snapshot.lifecycle!=QAPLET_OPEN) code=QAPLET_BAD_STATE;
  else if (!s->status.delivered && s->status.referenced!=ref) { if (ref) ++r->process_snapshot.referenced; else --r->process_snapshot.referenced; s->status.referenced=ref; }
  pthread_mutex_unlock(&r->mutex); return qaplet_report(d,code);
}
qaplet_code qaplet_process_release(qaplet_runtime *r,const qaplet_process *token,qaplet_diagnostic *d) {
  qaplet_code code=qaplet_check_diagnostic(d);
  if (!code) code=qaplet_check_structure(token,sizeof(*token));
  if (!code) code=qaplet_owner(r);
  if (code) return qaplet_report(d,code);
  qaplet_process_collect(r); pthread_mutex_lock(&r->mutex); process_slot *s=lookup_locked(r,token);
  if (!s) code=QAPLET_INVALID_HANDLE;
  else if (!s->status.reaped) code=QAPLET_BUSY;
  else {
    s->released=1; --r->process_snapshot.retained_results;
    if (s->status.referenced) { s->status.referenced=0; --r->process_snapshot.referenced; }
  }
  pthread_mutex_unlock(&r->mutex); qaplet_process_collect(r); return qaplet_report(d,code);
}
qaplet_code qaplet_get_process_snapshot(qaplet_runtime *r,qaplet_process_snapshot *out,qaplet_diagnostic *d) {
  qaplet_code code=qaplet_check_diagnostic(d);
  if (!code) code=qaplet_check_structure(out,sizeof(*out));
  if (!code && !r) code=QAPLET_INVALID_ARGUMENT;
  if (code) return qaplet_report(d,code);
  pthread_mutex_lock(&r->mutex);
  if (!r->m2_enabled) code=QAPLET_UNSUPPORTED;
  else memcpy((char *)out+8,(char *)&r->process_snapshot+8,sizeof(*out)-8);
  pthread_mutex_unlock(&r->mutex); return qaplet_report(d,code);
}
