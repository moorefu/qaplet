/* Evidence-only independent native arm. Built on Ubuntu, same APE both hosts. */
#define _COSMO_SOURCE 1
#include <signal.h>
#include <stdio.h>
#include <time.h>
#include <unistd.h>
#include "libc/dce.h"
#include "libc/nt/dll.h"
#include "libc/nt/events.h"
#include "libc/nt/synchronization.h"
int main(void){
  unsigned pid=getpid();
#if defined(__x86_64__)
  if(IsWindows()){
    int64_t kernel=LoadLibraryA("kernel32.dll");uint32_t (__msabi *native_pid)(void)=(void *)GetProcAddress(kernel,"GetCurrentProcessId");
    if(!native_pid)return 98;pid=native_pid();
  }
#endif
  if(!IsWindows()){signal(SIGINT,SIG_IGN);signal(SIGTERM,SIG_IGN);}
  FILE *f=fopen("strong-child.json","wb");if(!f)return 98;
  fprintf(f,"{\"native_pid\":%u,\"native_ppid_posix_only\":%d,\"arm_is_blocked_until_host_verified_identity\":true}\n",pid,IsWindows() ? -1:getppid());
  if(fclose(f))return 98;
  struct timespec interval={0,1000000};while(access("strong-child-arm",F_OK))nanosleep(&interval,NULL);
  if(write(1,"READY\n",6)!=6)return 98;
  if(IsWindows()){int64_t event=CreateEvent(NULL,1,0,NULL);if(!event)return 98;WaitForSingleObject(event,0xffffffff);}
  else for(;;)pause();
  return 99;
}
