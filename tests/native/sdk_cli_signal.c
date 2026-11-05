/* SPDX-License-Identifier: MIT */
/* Owned native Console launcher. The Python parent creates a private Console;
 * group zero is sent only inside that facility, never the user's Console. */
#define _COSMO_SOURCE 1
#include "qaplet.h"
#include <errno.h>
#include <stdio.h>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include "libc/dce.h"
#include "libc/nt/console.h"
#include "libc/nt/accounting.h"
#include "libc/nt/dll.h"
#include "libc/nt/events.h"
#include "libc/nt/ipc.h"
#include "libc/nt/process.h"
#include "libc/nt/runtime.h"
#include "libc/nt/synchronization.h"
#include "libc/nt/struct/startupinfo.h"
#include "libc/proc/ntspawn.h"
extern int64_t CreateFile(const char16_t *,uint32_t,uint32_t,void *,uint32_t,uint32_t,int64_t);
extern bool32 DuplicateHandle(int64_t,int64_t,int64_t,int64_t *,uint32_t,bool32,uint32_t);
#define CHECK(x) do{if(!(x)){fprintf(stderr,"signal fixture line %d: %s native=%u\n",__LINE__,#x,IsWindows() ? GetLastError() : errno);return 1;}}while(0)
#if defined(__x86_64__)
static unsigned accepted;
static bool32 __msabi accept_ctrl(uint32_t control){if(control)return 0;__atomic_fetch_add(&accepted,1,__ATOMIC_SEQ_CST);return 1;}
#endif
static qaplet_runtime *queue_runtime;
static int queue_pipe[2];static int64_t queue_event;
static unsigned queue_marks;static qaplet_code queue_stop_code;
static void queue_signal(int number){int saved=errno;(void)number;write(queue_pipe[1],"S",1);errno=saved;}
#if defined(__x86_64__)
static bool32 (__msabi *queue_set_event)(int64_t);
static bool32 __msabi queue_control(uint32_t number){if(number)return 0;queue_set_event(queue_event);return 1;}
#endif
static void *queue_relay(void *unused){
  (void)unused;if(IsWindows())WaitForSingleObject(queue_event,0xffffffff);else{char c;while(read(queue_pipe[0],&c,1)<0 && errno==EINTR){}}
  queue_stop_code=qaplet_request_stop(queue_runtime,NULL);return NULL;
}
static qaplet_code forbidden(qaplet_runtime *r,uint64_t call,size_t argc,const qaplet_value *args,void *opaque){
  (void)r;(void)call;(void)argc;(void)args;(void)opaque;++queue_marks;return QAPLET_OK;
}
static int queue_contract(void){
  qaplet_config config=QAPLET_CONFIG_INIT;qaplet_m2_config m2=QAPLET_M2_CONFIG_INIT;config.limits.event_queue_slots=2;
  struct sigaction before_int={0},before_term={0},after_int={0},after_term={0};int sdk_handlers_preserved=1;
  if(!IsWindows()){CHECK(!sigaction(SIGINT,NULL,&before_int) && !sigaction(SIGTERM,NULL,&before_term));}
  CHECK(!qaplet_create_m2(&config,&m2,&queue_runtime,NULL));
  if(!IsWindows()){
    CHECK(!sigaction(SIGINT,NULL,&after_int) && !sigaction(SIGTERM,NULL,&after_term));
    sdk_handlers_preserved=before_int.sa_handler==after_int.sa_handler && before_term.sa_handler==after_term.sa_handler;
    CHECK(sdk_handlers_preserved);
  }
  qaplet_host_export item=QAPLET_HOST_EXPORT_INIT;item.name="mark";item.name_length=4;item.kind=QAPLET_HOST_FUNCTION;item.function=forbidden;
  qaplet_host_module module=QAPLET_HOST_MODULE_INIT;module.name="host:signal";module.name_length=11;module.exports=&item;module.export_count=1;
  CHECK(!qaplet_register_host_module(queue_runtime,&module,NULL));qaplet_progress progress=QAPLET_PROGRESS_INIT;
  const char *source="Promise.all([import('qaplet:events'),import('host:signal')]).then(([e,h])=>{e.onEvent(()=>h.mark());globalThis.h=h})";
  CHECK(!qaplet_eval(queue_runtime,source,strlen(source),&progress,NULL));while(progress.phase==QAPLET_PHASE_PENDING)CHECK(!qaplet_pump(queue_runtime,&progress,NULL));
  CHECK(!qaplet_post_event(queue_runtime,1,0,NULL,0,NULL) && !qaplet_post_event(queue_runtime,2,0,NULL,0,NULL));
  CHECK(qaplet_post_event(queue_runtime,3,0,NULL,0,NULL)==QAPLET_QUEUE_FULL);
  source="Promise.resolve().then(()=>h.mark()).finally(()=>h.mark());new Promise(()=>{}).finally(()=>h.mark())";
  CHECK(!qaplet_eval(queue_runtime,source,strlen(source),&progress,NULL) && progress.phase==QAPLET_PHASE_PENDING);
  if(IsWindows()){
#if defined(__x86_64__)
    int64_t kernel=LoadLibraryA("kernel32.dll");queue_set_event=(void *)GetProcAddress(kernel,"SetEvent");
    bool32 (__msabi *set_ctrl)(bool32 (__msabi *)(uint32_t),bool32)=(void *)GetProcAddress(kernel,"SetConsoleCtrlHandler");
    queue_event=CreateEvent(NULL,1,0,NULL);CHECK(queue_set_event && set_ctrl && queue_event && set_ctrl(queue_control,1));
#endif
  }else{
    CHECK(!pipe(queue_pipe) && fcntl(queue_pipe[1],F_SETFL,O_NONBLOCK)>=0);
    struct sigaction action={0};action.sa_handler=queue_signal;sigemptyset(&action.sa_mask);
    CHECK(!sigaction(SIGINT,&action,NULL) && !sigaction(SIGTERM,&action,NULL));
  }
  pthread_t relay;CHECK(!pthread_create(&relay,NULL,queue_relay,NULL));CHECK(fputs("READY\n",stdout)>=0 && !fflush(stdout));
  CHECK(!pthread_join(relay,NULL) && !queue_stop_code);qaplet_drive_result drive=QAPLET_DRIVE_RESULT_INIT;
  CHECK(qaplet_run(queue_runtime,&drive,NULL)==QAPLET_EXECUTION_STOPPED && !queue_marks);
  qaplet_snapshot snap=QAPLET_SNAPSHOT_INIT;CHECK(!qaplet_get_snapshot(queue_runtime,&snap,NULL) && snap.termination_reason==QAPLET_EXECUTION_STOPPED && snap.queue_depth==2);
  CHECK(!qaplet_close(queue_runtime,NULL) && !qaplet_quiesce(queue_runtime,5000,&drive,NULL) && drive.reason==QAPLET_DRIVE_QUIESCENT);
  CHECK(!qaplet_get_snapshot(queue_runtime,&snap,NULL) && snap.events_dropped==2 && !queue_marks && !qaplet_destroy(queue_runtime,NULL));queue_runtime=NULL;
  FILE *record=fopen("queue-result.json","wb");CHECK(record);fprintf(record,"{\"queue_full\":true,\"owner_stopped\":true,\"user_marks\":%u,\"events_dropped\":%llu,\"sdk_unix_dispositions_preserved\":%s,\"sdk_destroyed\":true}\n",queue_marks,(unsigned long long)snap.events_dropped,sdk_handlers_preserved ? "true":"false");CHECK(!fclose(record));
  fputs("public C Host: EXECUTION_STOPPED; queue full; no remaining JS; physically quiescent/destroyed\n",stderr);fflush(NULL);
  if(IsWindows())ExitProcess(1);return 1;
}
static int console(int argc,char **argv){
  CHECK((argc==4 || argc==5) && IsWindows());int queue=argc==5 && !strcmp(argv[4],"queue"),mixed=argc==5 && !strcmp(argv[4],"mixed"),output=argc==5 && !queue && !mixed,asynchronous=output && !strcmp(argv[4],"async-output");
#if defined(__x86_64__)
  int64_t kernel=LoadLibraryA("kernel32.dll");CHECK(kernel);
  bool32 (__msabi *set_ctrl)(bool32 (__msabi *)(uint32_t),bool32)=(void *)GetProcAddress(kernel,"SetConsoleCtrlHandler");CHECK(set_ctrl);
  uint32_t (__msabi *console_processes)(uint32_t *,uint32_t)=(void *)GetProcAddress(kernel,"GetConsoleProcessList");CHECK(console_processes);
  uint32_t (__msabi *native_pid)(void)=(void *)GetProcAddress(kernel,"GetCurrentProcessId");CHECK(native_pid);
  CHECK(set_ctrl(NULL,0) && set_ctrl(accept_ctrl,1));
  uint32_t initial[16]={0},during[16]={0};uint32_t initial_n=console_processes(initial,16);
  fprintf(stderr,"private Console initial count=%u pid=%u libc_pid=%d first=%u\n",initial_n,native_pid(),getpid(),initial[0]);
  CHECK(initial_n==1 && initial[0]==native_pid());
  FILE *source=fopen(argv[3],"rb");CHECK(source);char text[30000];size_t bytes=fread(text,1,sizeof(text)-1,source);CHECK(!ferror(source) && !fclose(source));text[bytes]=0;
  int64_t fo=CreateFile(u"cli.stdout",0x40000000,3,NULL,2,0x80,0),fe=CreateFile(u"cli.stderr",0x40000000,3,NULL,2,0x80,0);CHECK(fo!=-1 && fe!=-1);
  int64_t fi=GetStdHandle(-10);uint32_t startup_modes[3]={0},before_modes[3]={0},modified_modes[3]={0},after_modes[3]={0};
  if(mixed){CHECK(CloseHandle(fo) && CloseHandle(fe));fi=CreateFile(u"CONIN$",0xc0000000,3,NULL,3,0,0);fo=CreateFile(u"CONOUT$",0xc0000000,3,NULL,3,0,0);fe=CreateFile(u"CONOUT$",0xc0000000,3,NULL,3,0,0);CHECK(fi!=-1 && fo!=-1 && fe!=-1);int64_t routes[]={fi,fo,fe};for(int i=0;i<3;++i)CHECK(GetConsoleMode(routes[i],&startup_modes[i]));}
  int64_t reader=0;uint32_t quota=0;
  if(output){
    CHECK(CloseHandle(fo));char ascii[100];char16_t pipe_name[100];snprintf(ascii,sizeof(ascii),"\\\\.\\pipe\\qaplet-signal-%u",native_pid());
    for(unsigned i=0;i<=strlen(ascii);++i)pipe_name[i]=(unsigned char)ascii[i];
    fo=CreateNamedPipe(pipe_name,3|(asynchronous ? 0x40000000 : 0),8,1,4096,4096,0,NULL);CHECK(fo!=-1);
    reader=CreateFile(pipe_name,0x80000000,0,NULL,3,0,0);CHECK(reader!=-1);
    CHECK(ConnectNamedPipe(fo,NULL) || GetLastError()==535);
    CHECK(GetNamedPipeInfo(fo,NULL,&quota,NULL,NULL) && quota);
  }
  int64_t handles[3];struct NtStartupInfo startup={.cb=sizeof(startup),.dwFlags=0x100};
  for(int i=0;i<3;++i){int64_t selected=i==1 ? fo : i==2 ? fe : fi;
    CHECK(DuplicateHandle(-1,selected,-1,&handles[i],0,1,2));startup.stdiofds[i]=handles[i];}
  if(mixed){
    char *warm[]={argv[2],"--help",NULL};char16_t warm_command[32768];struct NtProcessInformation warm_process={0};uint32_t warm_exit=99;
    CHECK(mkntcmdline(warm_command,warm,32768)<32768 && CreateProcess(NULL,warm_command,NULL,NULL,1,0,NULL,NULL,&startup,&warm_process));
    CHECK(CloseHandle(warm_process.hThread) && WaitForSingleObject(warm_process.hProcess,5000)==0 && GetExitCodeProcess(warm_process.hProcess,&warm_exit) && !warm_exit && CloseHandle(warm_process.hProcess));
    int64_t routes[]={fi,fo,fe};for(int i=0;i<3;++i)CHECK(GetConsoleMode(routes[i],&before_modes[i]));
  }
  char *args[]={argv[2],"--timeout-ms",getenv("QAPLET_SIGNAL_LATE") ? "1500" : "600000","--max-dispatches","100000000","eval","--source",text,NULL};char16_t command[32768];
  if(queue){args[1]="--queue-stop";args[2]=NULL;}
  CHECK(mkntcmdline(command,args,32768)<32768);struct NtProcessInformation process={0};
  CHECK(CreateProcess(NULL,command,NULL,NULL,1,0,NULL,NULL,&startup,&process));
  CHECK(CloseHandle(process.hThread));for(int i=0;i<3;++i)CHECK(CloseHandle(handles[i]));if(!mixed)CHECK(CloseHandle(fo) && CloseHandle(fe));
  int ready=0;struct timespec pause={0,1000000};
  uint32_t available=0;
  for(unsigned i=0;i<20000 && !ready;++i){
    if(mixed){uint32_t mode=0;CHECK(GetConsoleMode(fi,&mode));ready=access("mixed-ready",F_OK)==0 && !(mode&7);}
    else if(output){CHECK(PeekNamedPipe(reader,NULL,0,NULL,&available,NULL));ready=available>=quota;}
    else{FILE *f=fopen("cli.stdout","rb");if(f){char marker[7]={0};fread(marker,1,6,f);fclose(f);ready=!memcmp(marker,"READY\n",6);}}
    if(!ready)nanosleep(&pause,NULL);
  }
  CHECK(ready);
  if(mixed){int64_t routes[]={fi,fo,fe};for(int i=0;i<3;++i)CHECK(GetConsoleMode(routes[i],&modified_modes[i]));}
  uint32_t during_n=console_processes(during,16);int found=0;for(unsigned i=0;i<during_n && i<16;++i)found|=during[i]==process.dwProcessId;
  CHECK(found && during_n==2);
  struct timespec before,after;clock_gettime(CLOCK_MONOTONIC,&before);
  CHECK(GenerateConsoleCtrlEvent(0,0));
  unsigned repeats=0;int held=0;
  if(getenv("QAPLET_SIGNAL_THREAD_FAULT")){
    for(unsigned i=0;i<50;++i){
      FILE *f=fopen("signal-fault.ndjson","rb");if(f){char records[8192]={0};fread(records,1,sizeof(records)-1,f);fclose(f);held=strstr(records,"thread-exit-held")!=NULL;}
      if(WaitForSingleObject(process.hProcess,100)==0)break;
      if(held && repeats<5){CHECK(GenerateConsoleCtrlEvent(0,0));++repeats;}
      if(repeats==5)break;
    }
  }
  if(getenv("QAPLET_SIGNAL_LATE")){
    for(unsigned i=0;i<1000;++i){FILE *f=fopen("signal-late.ndjson","rb");if(f){char records[4096]={0};fread(records,1,sizeof(records)-1,f);fclose(f);held=strstr(records,"late-hint-held")!=NULL;}if(held)break;nanosleep(&pause,NULL);}
    CHECK(held && GenerateConsoleCtrlEvent(0,0));++repeats;
  }
  uint32_t waited=WaitForSingleObject(process.hProcess,12000),code=99;
  if(waited!=0){TerminateProcess(process.hProcess,99);WaitForSingleObject(process.hProcess,5000);}
  CHECK(GetExitCodeProcess(process.hProcess,&code) && CloseHandle(process.hProcess));clock_gettime(CLOCK_MONOTONIC,&after);
  if(mixed){
    int64_t routes[]={fi,fo,fe};for(int i=0;i<3;++i)CHECK(GetConsoleMode(routes[i],&after_modes[i]) && before_modes[i]==after_modes[i]);
    FILE *modes=fopen("mixed-modes.json","wb");CHECK(modes);fprintf(modes,"{\"facility_before_CRT\":[%u,%u,%u],\"actual_entry_baseline\":[%u,%u,%u],\"modified\":[%u,%u,%u],\"restored\":[%u,%u,%u],\"full_modes_restored\":true,\"pre_CRT_restore_claimed\":false}\n",startup_modes[0],startup_modes[1],startup_modes[2],before_modes[0],before_modes[1],before_modes[2],modified_modes[0],modified_modes[1],modified_modes[2],after_modes[0],after_modes[1],after_modes[2]);CHECK(!fclose(modes));for(int i=0;i<3;++i)CHECK(CloseHandle(routes[i]));
  }
  FILE *result=fopen("ctrl-result.json","wb");CHECK(result);
  fprintf(result,"{\"private_console\":true,\"initial_processes\":%u,\"shared_processes\":%u,\"sender_pid\":%u,\"target_pid\":%u,\"target_in_same_console\":true,\"event\":0,\"group\":0,\"sender_received\":%u,\"ready_accepted\":true,\"paused_reader_available\":%u,\"pipe_quota\":%u,\"asynchronous_pipe\":%s,\"native_thread_exit_held\":%s,\"repeated_after_held\":%u,\"exit_code\":%u,\"supervisor_timeout\":%s,\"elapsed_seconds\":%.9f}\n",initial_n,during_n,native_pid(),process.dwProcessId,__atomic_load_n(&accepted,__ATOMIC_SEQ_CST),available,quota,asynchronous ? "true" : "false",held ? "true" : "false",repeats,code,waited ? "true" : "false",(after.tv_sec-before.tv_sec)+(after.tv_nsec-before.tv_nsec)/1e9);
  if(reader)CHECK(CloseHandle(reader));
  CHECK(!fclose(result));return 0;
#else
  return 1;
#endif
}
int main(int argc,char **argv){
  if(argc>1 && !strcmp(argv[1],"--console"))return console(argc,argv);if(argc==2 && !strcmp(argv[1],"--queue-stop"))return queue_contract();
  if(argc==2 && !strcmp(argv[1],"--held-child")){
    unsigned pid=getpid();
#if defined(__x86_64__)
    if(IsWindows()){int64_t kernel=LoadLibraryA("kernel32.dll");uint32_t (__msabi *native_pid)(void)=(void *)GetProcAddress(kernel,"GetCurrentProcessId");pid=native_pid();}
#endif
    FILE *record=fopen("direct-child.json","wb");CHECK(record);fprintf(record,"{\"pid\":%u}\n",pid);CHECK(!fclose(record));CHECK(fwrite("R",1,1,stdout)==1 && !fflush(stdout));
    if(IsWindows()){int64_t event=CreateEvent(NULL,1,0,NULL);CHECK(event);WaitForSingleObject(event,0xffffffff);}else{signal(SIGINT,SIG_IGN);signal(SIGTERM,SIG_IGN);for(;;)pause();}
    return 99;
  }
  if(argc==2 && !strcmp(argv[1],"--forbidden")){FILE *f=fopen("forbidden-js","wb");if(f)fclose(f);if(IsWindows())ExitProcess(99);return 99;}
  return 2;
}
