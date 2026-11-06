/* SPDX-License-Identifier: MIT */
/* Actual native thread lifetime gates, outside the product. */
#define _COSMO_SOURCE 1
#include <errno.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "libc/dce.h"
#include "libc/nt/runtime.h"
#include "libc/nt/process.h"
#include "libc/nt/events.h"
#include "libc/nt/errors.h"
#include "libc/nt/synchronization.h"
extern int __real_pthread_create(pthread_t *,const pthread_attr_t *,void *(*)(void *),void *);
extern int __real_pthread_tryjoin_np(pthread_t,void **);
extern uint32_t __real_WaitForSingleObject(int64_t,uint32_t);
extern bool32 __real_DuplicateHandle(int64_t,int64_t,int64_t,int64_t *,uint32_t,bool32,uint32_t);
extern bool32 __real_CloseHandle(int64_t);
typedef struct {void *(*entry)(void *);void *argument;unsigned number;} launch;
static unsigned sequence;
static int mode;
static int64_t gate;
static _Atomic int64_t relay_handle;
static pthread_t first_thread;
static int relay_joined;
static __thread unsigned ordinal;
static pthread_mutex_t log_mutex=PTHREAD_MUTEX_INITIALIZER;
static void record_value(const char *operation,unsigned value){
  int saved=errno;uint32_t error=GetLastError();
  pthread_mutex_lock(&log_mutex);FILE *f=fopen("relay-retirement.ndjson","ab");
  if(f){fprintf(f,"{\"operation\":\"%s\",\"value\":%u}\n",operation,value);fclose(f);}pthread_mutex_unlock(&log_mutex);
  errno=saved;SetLastError(error);
}
static void record(const char *operation){record_value(operation,0);}
__attribute__((constructor)) static void setup(void){
  if(!IsWindows())return;const char *value=getenv("QAPLET_RELAY_RETIRE_FAULT");
  mode=value ? atoi(value) : 0;if(mode)gate=CreateEvent(NULL,1,0,NULL);
}
static void *held_return(void *opaque){
  launch copy=*(launch *)opaque;free(opaque);ordinal=copy.number;
  void *result=copy.entry(copy.argument);
  if(copy.number==1){record("relay-return-held");__real_WaitForSingleObject(gate,0xffffffff);record("relay-return-released");}
  else{record("output-return-held");struct timespec delay={0,100000000};while(nanosleep(&delay,&delay) && errno==EINTR){}record("output-return-released");}
  return result;
}
int __wrap_pthread_create(pthread_t *thread,const pthread_attr_t *attr,void *(*entry)(void *),void *argument){
  if(!mode)return __real_pthread_create(thread,attr,entry,argument);
  launch *owned=malloc(sizeof(*owned));if(!owned)return ENOMEM;
  unsigned n=__atomic_add_fetch(&sequence,1,__ATOMIC_SEQ_CST);*owned=(launch){entry,argument,n};
  int error=__real_pthread_create(thread,attr,held_return,owned);if(error)free(owned);else if(n==1)first_thread=*thread;return error;
}
int __wrap_pthread_tryjoin_np(pthread_t thread,void **result){
  int error=__real_pthread_tryjoin_np(thread,result);
  if(mode && !relay_joined && pthread_equal(thread,first_thread)){
    if(error==EBUSY){record("relay-join-busy");if(mode==1)SetEvent(gate);}
    else if(!error){relay_joined=1;record("relay-joined");}
  }return error;
}
bool32 __wrap_DuplicateHandle(int64_t process,int64_t source,int64_t target,int64_t *out,uint32_t access,bool32 inherit,uint32_t options){
  bool32 ok=__real_DuplicateHandle(process,source,target,out,access,inherit,options);
  if(ok && mode && ordinal==1 && source==-2){atomic_store(&relay_handle,*out);record("relay-native-handle");}return ok;
}
uint32_t __wrap_WaitForSingleObject(int64_t handle,uint32_t timeout){
  if(mode && handle==atomic_load(&relay_handle)){
    record_value(timeout ? "relay-native-bounded-wait" : "relay-native-zero-check",timeout);
    if(mode==1)SetEvent(gate);
  }return __real_WaitForSingleObject(handle,timeout);
}
bool32 __wrap_CloseHandle(int64_t handle){
  bool32 ok=__real_CloseHandle(handle);
  if(ok && mode && handle==atomic_load(&relay_handle)){
    atomic_store(&relay_handle,0);record("relay-native-handle-closed");
  }return ok;
}
