/* Controlled OS-thread lifetime and wait observer; product has no fault switch.
 * First created pthread is held after its actual function returns; subsequent
 * native thread returns are delayed three seconds. No private SDK layout. */
#define _COSMO_SOURCE 1
#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include "libc/dce.h"
#include "libc/nt/runtime.h"
#include "libc/nt/events.h"
#include "libc/nt/synchronization.h"
#include "libc/runtime/runtime.h"
typedef struct {void *(*entry)(void *);void *argument;unsigned number;} launch;
static unsigned sequence;
static pthread_mutex_t log_mutex=PTHREAD_MUTEX_INITIALIZER;
static int enabled;
static int hold_pipe[2];static int64_t hold_event;
static uint64_t ns(void){struct timespec n;clock_gettime(CLOCK_MONOTONIC,&n);return n.tv_sec*1000000000ull+n.tv_nsec;}
static void log_record(const char *kind,unsigned number,uint64_t duration){
  pthread_mutex_lock(&log_mutex);FILE *f=fopen("signal-fault.ndjson","ab");
  if(f){fprintf(f,"{\"operation\":\"%s\",\"thread\":%u,\"duration_ns\":%llu}\n",kind,number,(unsigned long long)duration);fclose(f);}pthread_mutex_unlock(&log_mutex);
}
__attribute__((constructor)) static void setup(void){
  enabled=getenv("QAPLET_SIGNAL_THREAD_FAULT")!=NULL;if(!enabled)return;
  ShowCrashReports();log_record("fault-setup",0,0);
  if(IsWindows())hold_event=CreateEvent(NULL,1,0,NULL);else pipe(hold_pipe);
}
static void *held_return(void *opaque){
  launch copy=*(launch *)opaque;free(opaque);log_record("thread-entry",copy.number,0);void *result=copy.entry(copy.argument);
  if(copy.number==1){
    log_record("thread-exit-held",copy.number,0);
    if(IsWindows())WaitForSingleObject(hold_event,0xffffffff);else{char b;while(read(hold_pipe[0],&b,1)<0 && errno==EINTR){}}
  }else{
    log_record("thread-exit-delay",copy.number,3000000000ull);struct timespec delay={3,0};while(nanosleep(&delay,&delay) && errno==EINTR){}
    log_record("thread-delay-released",copy.number,0);
  }
  return result;
}
extern int __real_pthread_create(pthread_t *,const pthread_attr_t *,void *(*)(void *),void *);
int __wrap_pthread_create(pthread_t *thread,const pthread_attr_t *attr,void *(*entry)(void *),void *argument){
  if(!enabled)return __real_pthread_create(thread,attr,entry,argument);
  launch *owned=malloc(sizeof(*owned));if(!owned)return ENOMEM;
  *owned=(launch){entry,argument,__atomic_add_fetch(&sequence,1,__ATOMIC_SEQ_CST)};
  int error=__real_pthread_create(thread,attr,held_return,owned);if(error)free(owned);return error;
}
extern int __real_pthread_cond_timedwait(pthread_cond_t *,pthread_mutex_t *,const struct timespec *);
int __wrap_pthread_cond_timedwait(pthread_cond_t *condition,pthread_mutex_t *mutex,const struct timespec *deadline){
  /* The supplied libc also uses its timedwait implementation for an untimed
   * condition wait with a null deadline. That is a runtime wait, not cleanup. */
  if(!deadline)return __real_pthread_cond_timedwait(condition,mutex,NULL);
  uint64_t before=ns();struct timespec shortened=*deadline;
  if(enabled && getenv("QAPLET_SIGNAL_SPLIT_WAIT")){
    uint64_t target=before+3200000000ull,current=(uint64_t)deadline->tv_sec*1000000000+deadline->tv_nsec;
    if(current>target){shortened.tv_sec=target/1000000000;shortened.tv_nsec=target%1000000000;log_record("system-short-timeout",0,3200000000ull);}
  }
  int error=__real_pthread_cond_timedwait(condition,mutex,&shortened);
  if(enabled)log_record("active-cond-wait",0,ns()-before);return error;
}
