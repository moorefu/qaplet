/* Controlled OS hint delay after an entry has captured its real native target.
 * Runtime is destroyed while that entry is held; replacement resources and a
 * post-destroy delivery check whether native targets/code remain safe. */
#define _COSMO_SOURCE 1
#include "qaplet.h"
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include "libc/dce.h"
#include "libc/nt/console.h"
#include "libc/nt/dll.h"
#include "libc/nt/events.h"
#include "libc/nt/runtime.h"
#include "libc/nt/synchronization.h"
extern ssize_t __real_write(int,const void *,size_t);
extern qaplet_code __real_qaplet_destroy(qaplet_runtime *,qaplet_diagnostic *);
extern void *__real_GetProcAddress(int64_t,const char *);
static int enabled,log_fd=-1,gate[2],target_fd=-1;
static pthread_t producer;static __thread int fixture_producer;
static struct stat original_target;
static unsigned entered,released,late_write,destroyed,future_writes;
#if defined(__x86_64__)
static int64_t native_log,held_event,release_event,completed_event,first_handle;
static uint32_t held_thread;
static unsigned native_calls;
static bool32 (__msabi *set_event)(int64_t);
static uint32_t (__msabi *wait_event)(int64_t,uint32_t),(__msabi *thread_id)(void);
static bool32 (__msabi *native_write)(int64_t,const void *,uint32_t,uint32_t *,void *);
static void foreign_log(const char *text,uint32_t size){uint32_t n=0;native_write(native_log,text,size,&n,NULL);}
static bool32 __msabi delayed_event(int64_t handle){
  unsigned call=__atomic_add_fetch(&native_calls,1,__ATOMIC_SEQ_CST);uint32_t id=thread_id();
  if(call==1){
    first_handle=handle;held_thread=id;
    static const char held[]="{\"operation\":\"late-hint-held\"}\n";foreign_log(held,sizeof(held)-1);
    __atomic_store_n(&entered,1,__ATOMIC_SEQ_CST);set_event(held_event);wait_event(release_event,0xffffffff);
    __atomic_store_n(&released,1,__ATOMIC_SEQ_CST);
  }
  bool32 result=set_event(handle);
  if(__atomic_load_n(&released,__ATOMIC_SEQ_CST) && id==held_thread && handle!=first_handle){
    static const char completed[]="{\"operation\":\"late-hint-completed\"}\n";foreign_log(completed,sizeof(completed)-1);set_event(completed_event);
  }
  return result;
}
#endif
static void log_line(const char *text){__real_write(log_fd,text,strlen(text));}
static void *unix_producer(void *unused){
  (void)unused;struct timespec pause={0,1000000};
  for(unsigned i=0;i<20000 && access("late-arm",F_OK);++i)nanosleep(&pause,NULL);
  fixture_producer=1;pthread_kill(pthread_self(),SIGINT);return NULL;
}
__attribute__((constructor)) static void setup(void){
  enabled=getenv("QAPLET_SIGNAL_LATE")!=NULL;if(!enabled)return;
  log_fd=open("signal-late.ndjson",O_WRONLY|O_CREAT|O_APPEND,0600);
  if(IsWindows()){
#if defined(__x86_64__)
    int64_t kernel=LoadLibraryA("kernel32.dll");
    set_event=(void *)__real_GetProcAddress(kernel,"SetEvent");wait_event=(void *)__real_GetProcAddress(kernel,"WaitForSingleObject");
    thread_id=(void *)__real_GetProcAddress(kernel,"GetCurrentThreadId");native_write=(void *)__real_GetProcAddress(kernel,"WriteFile");
    held_event=CreateEvent(NULL,1,0,NULL);release_event=CreateEvent(NULL,1,0,NULL);completed_event=CreateEvent(NULL,1,0,NULL);
    extern int64_t CreateFile(const char16_t *,uint32_t,uint32_t,void *,uint32_t,uint32_t,int64_t);
    native_log=CreateFile(u"signal-late.ndjson",0x40000000,3,NULL,3,0x80,0);
#endif
  }else{pipe(gate);pthread_create(&producer,NULL,unix_producer,NULL);}
}
void *__wrap_GetProcAddress(int64_t library,const char *name){
#if defined(__x86_64__)
  if(enabled && IsWindows() && !strcmp(name,"SetEvent"))return (void *)delayed_event;
#endif
  return __real_GetProcAddress(library,name);
}
ssize_t __wrap_write(int fd,const void *data,size_t size){
  if(enabled && !IsWindows() && fixture_producer && size==1 && *(const unsigned char *)data==1 && !__atomic_load_n(&entered,__ATOMIC_SEQ_CST)){
    target_fd=fd;fstat(fd,&original_target);__atomic_store_n(&entered,1,__ATOMIC_SEQ_CST);log_line("{\"operation\":\"late-hint-held\"}\n");
    char b;while(read(gate[0],&b,1)<0 && errno==EINTR){}
    ssize_t result=__real_write(fd,data,size);__atomic_store_n(&late_write,result==1,__ATOMIC_SEQ_CST);
    log_line("{\"operation\":\"late-hint-completed\"}\n");return result;
  }
  if(enabled && !IsWindows() && __atomic_load_n(&destroyed,__ATOMIC_SEQ_CST) && fd==target_fd && size==1)__atomic_add_fetch(&future_writes,1,__ATOMIC_SEQ_CST);
  return __real_write(fd,data,size);
}
qaplet_code __wrap_qaplet_destroy(qaplet_runtime *runtime,qaplet_diagnostic *diagnostic){
  qaplet_code result=__real_qaplet_destroy(runtime,diagnostic);if(!enabled || result)return result;
  if(!__atomic_load_n(&entered,__ATOMIC_SEQ_CST)){log_line("{\"operation\":\"late-entry-gate-missing\"}\n");return result;}
  int same=0,reused=0,finished=0,untouched=1,future=0;
  if(IsWindows()){
#if defined(__x86_64__)
    int64_t replacements[16];for(unsigned i=0;i<16;++i){replacements[i]=CreateEvent(NULL,1,0,NULL);reused|=replacements[i]==first_handle;}
    same=first_handle && WaitForSingleObject(first_handle,0)!=0xffffffff;
    set_event(release_event);finished=wait_event(completed_event,1000)==0;
    unsigned before=__atomic_load_n(&native_calls,__ATOMIC_SEQ_CST);
    GenerateConsoleCtrlEvent(0,0);struct timespec pause={0,1000000};
    for(unsigned i=0;i<1000;++i){if(__atomic_load_n(&native_calls,__ATOMIC_SEQ_CST)>=before+2){future=1;break;}nanosleep(&pause,NULL);}
    for(unsigned i=0;i<16;++i){untouched&=WaitForSingleObject(replacements[i],0)==258;CloseHandle(replacements[i]);}
#endif
  }else{
    int replacements[16][2];for(unsigned i=0;i<16;++i){pipe(replacements[i]);fcntl(replacements[i][0],F_SETFL,O_NONBLOCK);reused|=replacements[i][0]==target_fd || replacements[i][1]==target_fd;}
    struct stat after;same=!fstat(target_fd,&after) && after.st_dev==original_target.st_dev && after.st_ino==original_target.st_ino;
    __real_write(gate[1],"G",1);struct timespec deadline;clock_gettime(CLOCK_REALTIME,&deadline);deadline.tv_sec+=1;
    finished=!pthread_timedjoin_np(producer,NULL,&deadline) && __atomic_load_n(&late_write,__ATOMIC_SEQ_CST);
    __atomic_store_n(&destroyed,1,__ATOMIC_SEQ_CST);raise(SIGINT);future=__atomic_load_n(&future_writes,__ATOMIC_SEQ_CST)>0;
    for(unsigned i=0;i<16;++i){char b;untouched&=read(replacements[i][0],&b,1)<0 && errno==EAGAIN;close(replacements[i][0]);close(replacements[i][1]);}
  }
  char text[400];snprintf(text,sizeof(text),"{\"operation\":\"post-destroy-late-entry\",\"destroy_code\":%u,\"original_target_identity_preserved\":%s,\"replacement_identity_reused\":%s,\"late_entry_completed\":%s,\"replacement_objects_untouched\":%s,\"future_entry_delivered\":%s}\n",result,same ? "true":"false",reused ? "true":"false",finished ? "true":"false",untouched ? "true":"false",future ? "true":"false");
  if(IsWindows()){
#if defined(__x86_64__)
    foreign_log(text,strlen(text));
#endif
  }else log_line(text);
  return result;
}
