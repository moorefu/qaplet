/* SPDX-License-Identifier: MIT */
/* Public SDK contrast and real CLI subprocess ownership, no private headers. */
#define _COSMO_SOURCE 1
#include "qaplet.h"
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>
#include "libc/dce.h"
#include "libc/nt/console.h"
#include "libc/nt/accounting.h"
#include "libc/nt/process.h"
#include "libc/nt/runtime.h"
#include "libc/nt/struct/startupinfo.h"
#include "libc/nt/synchronization.h"
#include "libc/proc/ntspawn.h"
extern int64_t CreateFile(const char16_t *,uint32_t,uint32_t,void *,uint32_t,uint32_t,int64_t);
extern bool32 DuplicateHandle(int64_t,int64_t,int64_t,int64_t *,uint32_t,bool32,uint32_t);
#define CHECK(x) do{if(!(x)){fprintf(stderr,"CLI public contract line %d: %s errno=%d\n",__LINE__,#x,errno);return 1;}}while(0)
static int dispose(qaplet_runtime *r){
  qaplet_drive_result d=QAPLET_DRIVE_RESULT_INIT;
  CHECK(!qaplet_close(r,NULL) && !qaplet_quiesce(r,5000,&d,NULL) && d.reason==QAPLET_DRIVE_QUIESCENT);
  qaplet_resource_snapshot s=QAPLET_RESOURCE_SNAPSHOT_INIT;
  CHECK(!qaplet_get_resource_snapshot(r,&s,NULL) && !s.operations && !s.io_endpoints && !s.helper_threads);
  CHECK(!qaplet_destroy(r,NULL));return 0;
}
static int results(void){
  qaplet_config b=QAPLET_CONFIG_INIT;qaplet_m2_config m=QAPLET_M2_CONFIG_INIT;qaplet_runtime *r=NULL;
  qaplet_progress p=QAPLET_PROGRESS_INIT;qaplet_drive_result d=QAPLET_DRIVE_RESULT_INIT;qaplet_result value=QAPLET_RESULT_INIT;
  CHECK(!qaplet_create_m2(&b,&m,&r,NULL));
  const char *s="import('qaplet:timers').then(t=>{globalThis.install=()=>t.setImmediate(()=>97);return 42})";
  CHECK(!qaplet_eval(r,s,strlen(s),&p,NULL));while(p.phase==QAPLET_PHASE_PENDING)CHECK(!qaplet_pump(r,&p,NULL));
  CHECK(!qaplet_get_result(r,&value,NULL) && value.number==42);
  CHECK(qaplet_eval(r,NULL,1,&p,NULL)==QAPLET_INVALID_ARGUMENT);
  CHECK(!qaplet_run(r,&d,NULL) && d.reason==QAPLET_DRIVE_IDLE);
  CHECK(!qaplet_get_result(r,&value,NULL) && value.number==42);
  s="install();42";CHECK(!qaplet_eval(r,s,strlen(s),&p,NULL));CHECK(!qaplet_run(r,&d,NULL));
  CHECK(!qaplet_get_result(r,&value,NULL) && value.type==QAPLET_VALUE_UNDEFINED);
  s="import('qaplet:timers').then(t=>{globalThis.jobs=0;t.setImmediate(()=>{Promise.resolve().then(()=>jobs=1);throw Error('background')});return 42})";
  CHECK(!qaplet_eval(r,s,strlen(s),&p,NULL));while(p.phase==QAPLET_PHASE_PENDING)CHECK(!qaplet_pump(r,&p,NULL));
  CHECK(qaplet_run(r,&d,NULL)==QAPLET_JS_EXCEPTION);
  qaplet_snapshot snap=QAPLET_SNAPSHOT_INIT;CHECK(!qaplet_get_snapshot(r,&snap,NULL) && snap.lifecycle==QAPLET_OPEN);
  CHECK(!qaplet_eval(r,"jobs+41",7,&p,NULL) && !qaplet_get_result(r,&value,NULL) && value.number==42);
  CHECK(!dispose(r));b.limits.execution_dispatches=100;
  CHECK(!qaplet_create_m2(&b,&m,&r,NULL));
  s="import('qaplet:timers').then(t=>{t.setImmediate(()=>{function again(){Promise.resolve().then(again)}again()});return 42})";
  CHECK(!qaplet_eval(r,s,strlen(s),&p,NULL));while(p.phase==QAPLET_PHASE_PENDING)CHECK(!qaplet_pump(r,&p,NULL));
  CHECK(qaplet_run(r,&d,NULL)==QAPLET_DISPATCH_LIMIT);
  CHECK(!qaplet_get_snapshot(r,&snap,NULL) && snap.lifecycle==QAPLET_FAILED && snap.termination_reason==QAPLET_DISPATCH_LIMIT);
  CHECK(qaplet_eval(r,"42",2,&p,NULL)==QAPLET_BAD_STATE && !dispose(r));
  puts("M2 CLI contrast: latest independent SDK result; rejected/idle preserved; background jobs settle; ordinary reuse; terminal first cause and safe cleanup: pass");return 0;
}
typedef struct {struct termios term;int flags;uint32_t mode;} native_mode;
static int record(const char *stage,native_mode modes[3]){
  FILE *f=fopen("cli-terminal.ndjson","ab");CHECK(f);
  for(int i=0;i<3;++i){
    if(IsWindows()){CHECK(GetConsoleMode(GetStdHandle(-10-i),&modes[i].mode));fprintf(f,"{\"stage\":\"%s\",\"stream\":%d,\"console_mode\":%u}\n",stage,i,modes[i].mode);}
    else{CHECK(!tcgetattr(i,&modes[i].term) && (modes[i].flags=fcntl(i,F_GETFL))>=0);struct termios *t=&modes[i].term;
      fprintf(f,"{\"stage\":\"%s\",\"stream\":%d,\"flags\":%d,\"iflag\":%u,\"oflag\":%u,\"cflag\":%u,\"lflag\":%u,\"ispeed\":%u,\"ospeed\":%u,\"cc\":[",stage,i,modes[i].flags,t->c_iflag,t->c_oflag,t->c_cflag,t->c_lflag,cfgetispeed(t),cfgetospeed(t));
      for(unsigned j=0;j<sizeof(t->c_cc);++j)fprintf(f,"%s%u",j ? "," : "",t->c_cc[j]);fputs("]}\n",f);
    }
  }CHECK(!fclose(f));return 0;
}
static int routing(void){
  qaplet_config b=QAPLET_CONFIG_INIT;qaplet_m2_config m=QAPLET_M2_CONFIG_INIT;m.limits.reactor_registrations=2;
  qaplet_runtime *r=NULL;CHECK(!qaplet_create_m2(&b,&m,&r,NULL));
  qaplet_tty_options o=QAPLET_TTY_OPTIONS_INIT;qaplet_tty_status s=QAPLET_TTY_STATUS_INIT;
  qaplet_io_endpoint old=QAPLET_IO_ENDPOINT_INIT,next=QAPLET_IO_ENDPOINT_INIT,route=QAPLET_IO_ENDPOINT_INIT;
  qaplet_stdio_info info=QAPLET_STDIO_INFO_INIT;CHECK(!qaplet_tty_acquire(r,0,&o,&old,&s,NULL));
  o.flags=QAPLET_TTY_RAW;CHECK(qaplet_tty_acquire(r,0,&o,&next,&s,NULL)==QAPLET_BUSY && !next.generation);
  CHECK(!qaplet_tty_release(r,&old,&s,NULL) && s.restored);
  CHECK(qaplet_tty_acquire(r,0,&o,&next,&s,NULL)==QAPLET_RESOURCE_LIMIT && !next.generation);
  CHECK(!qaplet_stdio_get(r,0,&route,&info,NULL) && route.generation==old.generation && route.slot==old.slot);
  CHECK(!qaplet_get_tty_status(r,&old,&s,NULL) && s.restored && !s.active);
  qaplet_resource_snapshot snapshot=QAPLET_RESOURCE_SNAPSHOT_INIT;
  CHECK(!qaplet_get_resource_snapshot(r,&snapshot,NULL) && snapshot.io_endpoints==1);
  CHECK(!qaplet_stdio_detach(r,0,NULL) && !qaplet_tty_acquire(r,0,&o,&next,&s,NULL) && next.generation!=old.generation);
  CHECK(!qaplet_tty_release(r,&next,&s,NULL) && s.restored && !dispose(r));return 0;
}
static int native_console_spawn(const char **args,unsigned n,struct NtProcessInformation *process){
  struct NtStartupInfo startup={.cb=sizeof(startup),.dwFlags=0x100};int64_t handles[3]={0};
  for(int i=0;i<3;++i){CHECK(DuplicateHandle(-1,GetStdHandle(-10-i),-1,&handles[i],0,1,2));startup.stdiofds[i]=handles[i];}
  char16_t command[32768];char *native_args[13];for(unsigned i=0;i<n;++i)native_args[i]=(char *)args[i];native_args[n]=NULL;
  CHECK(mkntcmdline(command,native_args,32768)<32768 && CreateProcess(NULL,command,NULL,NULL,1,0,NULL,NULL,&startup,process));
  CHECK(CloseHandle(process->hThread));for(int i=0;i<3;++i)CHECK(CloseHandle(handles[i]));return 0;
}
static int terminal(int argc,char **argv){
  /* cli, this supplied Host, loader or -, ESM fixture, normal/error/budget. */
  CHECK(argc==7);int saved[3]={-1,-1,-1},master=-1,slave=-1;
  int64_t original[3]={0},console[3]={0};uint32_t original_cp=0;
  int unicode=!strcmp(argv[6],"unicode");
  if(IsWindows()){
    /* Explicit private ConPTY fixture setup, never a product fallback. Windows
     * does not promise useful GetStdHandle values for this native launcher.
     * Install and inherit these real Console routes before starting the CLI. */
    console[0]=CreateFile(u"CONIN$",0xc0000000u,3,NULL,3,0,0);
    console[1]=CreateFile(u"CONOUT$",0xc0000000u,3,NULL,3,0,0);console[2]=console[1];
    CHECK(console[0]!=-1 && console[1]!=-1);
    for(int i=0;i<3;++i){original[i]=GetStdHandle(-10-i);CHECK(SetStdHandle(-10-i,console[i]));}
    original_cp=GetConsoleOutputCP();
    /* The pinned PE CRT enables WINDOW_INPUT and UTF-8 before main. Record
     * that distinct process-start effect, then freeze the actual entry baseline
     * with an explicit --help run of these same CLI bytes. This is not evidence
     * that CLI main/SDK restores the pre-CRT mode. */
    native_mode modes[3]={0};CHECK(!record("facility-before-CRT",modes));
    const char *warm[]={argv[2],"--help"};struct NtProcessInformation warmup={0};uint32_t exit_code=99;
    CHECK(!native_console_spawn(warm,2,&warmup) && WaitForSingleObject(warmup.hProcess,5000)==0 && GetExitCodeProcess(warmup.hProcess,&exit_code) && !exit_code && CloseHandle(warmup.hProcess));
    CHECK(!record("actual-entry-baseline",modes) && !rename("cli-terminal.ndjson","cli-startup.ndjson"));
  }else{
    master=posix_openpt(O_RDWR|O_NOCTTY|O_NONBLOCK);CHECK(master>=0 && !grantpt(master) && !unlockpt(master));
    slave=open(ptsname(master),O_RDWR|O_NOCTTY);CHECK(slave>=0);
    for(int i=0;i<3;++i){saved[i]=dup(i);CHECK(saved[i]>=0 && dup2(slave,i)==i);}CHECK(!close(slave));
  }
  native_mode before[3]={0},after[3]={0};CHECK(!routing() && !record("before",before));
  qaplet_config b=QAPLET_CONFIG_INIT;qaplet_m2_config m=QAPLET_M2_CONFIG_INIT;qaplet_runtime *r=NULL;
  qaplet_process_options o=QAPLET_PROCESS_OPTIONS_INIT;
  const char *args[12];unsigned n=0;int loader=strcmp(argv[4],"-")!=0;
  args[n++]=loader ? argv[4] : argv[2];if(loader)args[n++]=argv[2];
  if(!strcmp(argv[6],"budget")){args[n++]="--max-dispatches";args[n++]="600";}
  if(unicode){args[n++]="eval";args[n++]="--source";args[n++]="import('qaplet:stdio').then(async s=>{await s.stdin.releaseTerminal();await Promise.all([s.stdin.closed,s.stdout.closed,s.stderr.closed]);const input=s.acquireTerminal(0,{raw:true});input.joinTerminal(1,{vt:true});input.joinTerminal(2);let n=0;while(n<5)n+=(await input.read(5-n).promise).bytes.length;return 'A中🙂Z'})";}
  else{args[n++]="run";args[n++]=argv[5];args[n++]="--";args[n++]=argv[3];args[n++]=argv[4];args[n++]=argv[6];}
  qaplet_string strings[12];for(unsigned i=0;i<n;++i)strings[i]=(qaplet_string){args[i],strlen(args[i])};
  o.path=strings[0];o.argv=strings;o.argc=n;for(int i=0;i<3;++i)o.stdio[i]=QAPLET_PROCESS_INHERIT;
  qaplet_process child=QAPLET_PROCESS_INIT;qaplet_process_status status=QAPLET_PROCESS_STATUS_INIT;
  struct NtProcessInformation native={0};
  if(IsWindows()){
    CHECK(!native_console_spawn(args,n,&native));
  }else{CHECK(!qaplet_create_m2(&b,&m,&r,NULL) && !qaplet_process_spawn(r,&o,&child,&status,NULL));}
  qaplet_drive_result d=QAPLET_DRIVE_RESULT_INIT;int sent=0;
  do{
    if(IsWindows()){
      uint32_t waited=WaitForSingleObject(native.hProcess,10);CHECK(waited==0 || waited==258);
      if(!waited){CHECK(GetExitCodeProcess(native.hProcess,&status.native_exit_code));status.exited=status.reaped=1;CHECK(CloseHandle(native.hProcess));}
    }else{CHECK(!qaplet_run_once(r,10,&d,NULL) && !qaplet_process_get_status(r,&child,&status,NULL));}
    if(!sent){
      native_mode mode={0};int ready;
      if(IsWindows()){uint32_t output=0;CHECK(GetConsoleMode(console[0],&mode.mode) && GetConsoleMode(console[1],&output));ready=!(mode.mode&7) && (output&4);}
      else{CHECK(!tcgetattr(0,&mode.term));ready=!(mode.term.c_lflag&(ECHO|ICANON|ISIG)) && (fcntl(0,F_GETFL)&O_NONBLOCK);}
      if(ready){native_mode modified[3]={0};CHECK(!record("modified",modified));if(unicode && IsWindows())CHECK(SetConsoleOutputCP(437) && GetConsoleOutputCP()==437);FILE *f=fopen("text.ready","wb");CHECK(f && fputs("ready",f)>=0 && !fclose(f));
        if(!IsWindows()){const unsigned char text[]={65,228,184,173,90};CHECK(write(master,text,5)==5);}sent=1;}
    }
  }while(!status.exited || !status.reaped || status.helper_active);
  CHECK(!record("restored",after));
  for(int i=0;i<3;++i){
    if(IsWindows())CHECK(before[i].mode==after[i].mode);
    else CHECK(before[i].flags==after[i].flags && before[i].term.c_iflag==after[i].term.c_iflag && before[i].term.c_oflag==after[i].term.c_oflag && before[i].term.c_cflag==after[i].term.c_cflag && before[i].term.c_lflag==after[i].term.c_lflag && !memcmp(before[i].term.c_cc,after[i].term.c_cc,sizeof(before[i].term.c_cc)) && cfgetispeed(&before[i].term)==cfgetispeed(&after[i].term) && cfgetospeed(&before[i].term)==cfgetospeed(&after[i].term));
  }
  FILE *observed=fopen("cli-child-status.json","wb");CHECK(observed);
  fprintf(observed,"{\"exit_code\":%u,\"signal\":%u,\"exited\":%u,\"reaped\":%u,\"helper_active\":%u}\n",status.native_exit_code,status.native_signal,status.exited,status.reaped,status.helper_active);CHECK(!fclose(observed));
  if(IsWindows()){
    if(unicode){unsigned measured=GetConsoleOutputCP();CHECK(measured==437 && SetConsoleOutputCP(original_cp));FILE *f=fopen("cli-codepage.json","wb");CHECK(f);fprintf(f,"{\"before\":%u,\"requested\":437,\"after\":%u,\"restored\":%u}\n",original_cp,measured,GetConsoleOutputCP());CHECK(!fclose(f));}
    for(int i=0;i<3;++i)CHECK(SetStdHandle(-10-i,original[i]));CHECK(CloseHandle(console[0]) && CloseHandle(console[1]));
  }else{
    CHECK(!dispose(r));
    FILE *raw=fopen("cli-pty.stdout.log","wb");CHECK(raw);char data[4096];ssize_t got;
    while((got=read(master,data,sizeof(data)))>0)CHECK(fwrite(data,1,got,raw)==got);CHECK(!fclose(raw));
    for(int i=0;i<3;++i)CHECK(dup2(saved[i],i)==i && !close(saved[i]));CHECK(!close(master));
  }
  CHECK(status.native_exit_code==((!strcmp(argv[6],"normal") || unicode) ? 0 : 1) && !status.native_signal);
  FILE *f=fopen("cli-terminal-result.txt","wb");CHECK(f && fputs("CLI terminal: native modes/flags restored; actual child reaped; owner cleanup complete\n",f)>=0 && !fclose(f));
  return 0;
}
int sdk_cli(int argc,char **argv){
  if(argc==2 && !strcmp(argv[1],"--m2-cli-results"))return results();
  if(argc==2 && !strcmp(argv[1],"--m2-cli-emit")){
    unsigned char data[4096];for(unsigned i=0;i<sizeof(data);++i)data[i]=i%256;
    for(unsigned i=0;i<32;++i){CHECK(fwrite(data,1,sizeof(data),stdout)==sizeof(data));CHECK(fwrite(data,1,sizeof(data),stderr)==sizeof(data));CHECK(!fflush(stdout) && !fflush(stderr));}
    if(IsWindows())ExitProcess(37);return 37;
  }
  if(argc==2 && !strcmp(argv[1],"--m2-cli-mark")){
    native_mode modified[3]={0};CHECK(!record("modified",modified));
    if(IsWindows())CHECK(!(modified[0].mode&7) && (modified[1].mode&4));
    else CHECK(!(modified[0].term.c_lflag&(ECHO|ICANON|ISIG)) && (modified[0].flags&O_NONBLOCK));
    FILE *f=fopen("text.ready","wb");CHECK(f && fputs("ready",f)>=0 && !fclose(f));return 0;
  }
  if(argc>1 && !strcmp(argv[1],"--m2-cli-terminal"))return terminal(argc,argv);
  return 2;
}
#ifdef SDK_CLI_STANDALONE
int main(int argc,char **argv){int code=sdk_cli(argc,argv);fflush(NULL);if(IsWindows())ExitProcess(code);return code;}
#endif
