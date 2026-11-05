/* SPDX-License-Identifier: MIT */
/* Public SDK plus real native terminal boundary; no private SDK headers. */
#define _COSMO_SOURCE 1
#include "qaplet.h"
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <pthread.h>
#include <time.h>
#include <termios.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include "libc/dce.h"
#include "libc/nt/console.h"
#include "libc/nt/runtime.h"
#include "libc/nt/synchronization.h"
extern int64_t CreateFile(const char16_t *,uint32_t,uint32_t,void *,uint32_t,uint32_t,int64_t);
#define CHECK(c) do{if(!(c)){fprintf(stderr,"TTY public contract line %d: %s (errno %d)\n",__LINE__,#c,errno);return 1;}}while(0)
static int same(const struct termios *a,const struct termios *b){return a->c_iflag==b->c_iflag && a->c_oflag==b->c_oflag && a->c_cflag==b->c_cflag && a->c_lflag==b->c_lflag && !memcmp(a->c_cc,b->c_cc,sizeof(a->c_cc)) && cfgetispeed(a)==cfgetispeed(b) && cfgetospeed(a)==cfgetospeed(b);}
static int lease(void){
  qaplet_config b=QAPLET_CONFIG_INIT;qaplet_m2_config m=QAPLET_M2_CONFIG_INIT;qaplet_runtime *r=NULL;
  int master=-1,slave=-1,saved[2]={-1,-1};int64_t console=0,saved_handle[2]={0};
  struct termios before={0},modified={0},after={0};uint32_t mode=0,measured=0;
  if(IsWindows()){
    console=CreateFile(u"CONIN$",0xc0000000u,3,NULL,3,0,0);CHECK(console!=-1 && GetConsoleMode(console,&mode));
    for(int i=0;i<2;++i){saved_handle[i]=GetStdHandle(-10-i);CHECK(SetStdHandle(-10-i,console));}
  }else{
    CHECK(!openpty(&master,&slave,NULL,NULL,NULL));CHECK(!tcgetattr(slave,&before));
    for(int i=0;i<2;++i){saved[i]=dup(i);CHECK(saved[i]>=0 && dup2(slave,i)==i);}
  }
  CHECK(qaplet_create_m2(&b,&m,&r,NULL)==QAPLET_OK);
  qaplet_tty_support support=QAPLET_TTY_SUPPORT_INIT;CHECK(qaplet_tty_inspect(r,0,&support,NULL)==QAPLET_OK && support.lease && support.raw && support.kind==(IsWindows() ? QAPLET_STDIO_CONSOLE : QAPLET_STDIO_TTY));
  CHECK(IsWindows() ? support.utf8_text && !support.binary && !support.resize && !support.supplementary_input && !support.raw_invalid_utf8_input : support.binary && support.resize && support.raw_invalid_utf8_input);
  qaplet_tty_options options=QAPLET_TTY_OPTIONS_INIT;options.flags=QAPLET_TTY_RAW;
  qaplet_io_endpoint endpoint=QAPLET_IO_ENDPOINT_INIT,alias=QAPLET_IO_ENDPOINT_INIT;
  qaplet_tty_status status=QAPLET_TTY_STATUS_INIT;
  CHECK(qaplet_tty_acquire(r,0,&options,&endpoint,&status,NULL)==QAPLET_OK && status.active);
  CHECK(qaplet_tty_acquire(r,1,&options,&alias,&status,NULL)==QAPLET_BUSY && !alias.generation);
  qaplet_runtime *other=NULL;CHECK(qaplet_create_m2(&b,&m,&other,NULL)==QAPLET_OK);
  CHECK(qaplet_tty_acquire(other,1,&options,&alias,&status,NULL)==QAPLET_BUSY && !alias.generation);
  if(IsWindows()){CHECK(GetConsoleMode(console,&measured) && measured!=mode);}
  else CHECK(!tcgetattr(slave,&modified) && !same(&before,&modified) && (fcntl(slave,F_GETFL)&O_NONBLOCK));
  CHECK(qaplet_tty_release(r,&endpoint,&status,NULL)==QAPLET_OK && !status.active && status.restored);
  CHECK(qaplet_tty_release(r,&endpoint,&status,NULL)==QAPLET_OK && status.restored);
  if(IsWindows()){CHECK(GetConsoleMode(console,&measured) && measured==mode);}
  else CHECK(!tcgetattr(slave,&after) && same(&before,&after) && !(fcntl(slave,F_GETFL)&O_NONBLOCK));
  CHECK(qaplet_stdio_detach(r,0,NULL)==QAPLET_OK);
  CHECK(qaplet_tty_acquire(other,0,&options,&alias,&status,NULL)==QAPLET_OK);
  CHECK(qaplet_tty_release(other,&alias,&status,NULL)==QAPLET_OK && status.restored);
  CHECK(qaplet_close(other,NULL)==QAPLET_OK && qaplet_destroy(other,NULL)==QAPLET_OK);
  CHECK(qaplet_close(r,NULL)==QAPLET_OK && qaplet_destroy(r,NULL)==QAPLET_OK);
  if(IsWindows()){for(int i=0;i<2;++i)CHECK(SetStdHandle(-10-i,saved_handle[i]));CHECK(CloseHandle(console));}
  else{for(int i=0;i<2;++i)CHECK(dup2(saved[i],i)==i && !close(saved[i]));CHECK(!close(slave) && !close(master));}
  puts("M2 TTY lease: explicit terminal acquisition; native alias conflict; full mode and shared flag readback; idempotent release; Host originals survive and native resources retire: pass");return 0;
}
static int resize(void){
  CHECK(!IsWindows());int master=-1,slave=-1,saved=dup(0);CHECK(saved>=0 && !openpty(&master,&slave,NULL,NULL,NULL) && dup2(slave,0)==0);
  struct winsize size={.ws_col=80,.ws_row=24};CHECK(!ioctl(slave,TIOCSWINSZ,&size));
  qaplet_config b=QAPLET_CONFIG_INIT;qaplet_m2_config m=QAPLET_M2_CONFIG_INIT;qaplet_runtime *r=NULL;
  CHECK(qaplet_create_m2(&b,&m,&r,NULL)==QAPLET_OK);
  qaplet_tty_options options=QAPLET_TTY_OPTIONS_INIT;qaplet_tty_status status=QAPLET_TTY_STATUS_INIT;qaplet_io_endpoint e=QAPLET_IO_ENDPOINT_INIT;
  CHECK(qaplet_tty_acquire(r,0,&options,&e,&status,NULL)==QAPLET_OK);
  CHECK(qaplet_tty_get_size(r,&e,&status,NULL)==QAPLET_OK && status.columns==80 && status.rows==24);
  CHECK(qaplet_tty_watch_resize(r,&e,1,NULL)==QAPLET_OK);
  qaplet_m2_snapshot before=QAPLET_M2_SNAPSHOT_INIT,after=QAPLET_M2_SNAPSHOT_INIT;
  CHECK(qaplet_get_m2_snapshot(r,&before,NULL)==QAPLET_OK);
  for(unsigned i=0;i<10000;++i){size.ws_col=i==9999 ? 100 : 90;size.ws_row=i==9999 ? 33 : 31;CHECK(!ioctl(slave,TIOCSWINSZ,&size));CHECK(qaplet_tty_notify_resize(r,&e,NULL)==QAPLET_OK);}
  CHECK(qaplet_get_m2_snapshot(r,&after,NULL)==QAPLET_OK && after.native_current_bytes==before.native_current_bytes);
  qaplet_progress p=QAPLET_PROGRESS_INIT;CHECK(qaplet_pump(r,&p,NULL)==QAPLET_OK && p.progressed);
  CHECK(qaplet_get_tty_status(r,&e,&status,NULL)==QAPLET_OK && status.columns==100 && status.rows==33 && status.revision==1);
  CHECK(qaplet_tty_watch_resize(r,&e,0,NULL)==QAPLET_OK);
  CHECK(qaplet_tty_notify_resize(r,&e,NULL)==QAPLET_BAD_STATE);
  CHECK(qaplet_tty_release(r,&e,&status,NULL)==QAPLET_OK && status.restored);
  CHECK(qaplet_close(r,NULL)==QAPLET_OK && qaplet_destroy(r,NULL)==QAPLET_OK);
  CHECK(dup2(saved,0)==0 && !close(saved) && !close(slave) && !close(master));
  puts("M2 TTY resize: real kernel sizes; 10000 Host notifications coalesce to latest query; bounded storage and single revision; unwatch rejects late producer; cleanup: pass");return 0;
}
static int js(void){
  CHECK(!IsWindows());int master=-1,slave=-1,saved=dup(0);CHECK(saved>=0 && !openpty(&master,&slave,NULL,NULL,NULL) && dup2(slave,0)==0);
  struct termios before,after;CHECK(!tcgetattr(slave,&before));struct winsize size={.ws_col=80,.ws_row=24};CHECK(!ioctl(slave,TIOCSWINSZ,&size));
  qaplet_config b=QAPLET_CONFIG_INIT;qaplet_m2_config m=QAPLET_M2_CONFIG_INIT;qaplet_runtime *r=NULL;
  CHECK(qaplet_create_m2(&b,&m,&r,NULL)==QAPLET_OK);
  const char *source="import('qaplet:stdio').then(m=>{if(m.stdin!==null)throw Error('implicit');const e=m.acquireTerminal(0,{raw:true});if(e.getSize().columns!==80)throw Error('size');"
    "e.unref();if(e.hasRef())throw Error('ref');return new Promise(resolve=>{e.watchResize(size=>{if(size.columns!==100||size.rows!==33)throw Error('latest');e.unwatchResize();e.close();e.closed.then(()=>resolve(42))});globalThis.terminal=null})})";
  qaplet_progress p=QAPLET_PROGRESS_INIT;qaplet_drive_result drive=QAPLET_DRIVE_RESULT_INIT;
  CHECK(qaplet_eval(r,source,strlen(source),&p,NULL)==QAPLET_OK && qaplet_run(r,&drive,NULL)==QAPLET_OK && drive.reason==QAPLET_DRIVE_UNSETTLED_ENTRY);
  qaplet_io_endpoint e=QAPLET_IO_ENDPOINT_INIT;qaplet_stdio_info info=QAPLET_STDIO_INFO_INIT;
  CHECK(qaplet_stdio_get(r,0,&e,&info,NULL)==QAPLET_OK);
  size.ws_col=100;size.ws_row=33;CHECK(!ioctl(slave,TIOCSWINSZ,&size));CHECK(qaplet_tty_notify_resize(r,&e,NULL)==QAPLET_OK);
  CHECK(qaplet_run(r,&drive,NULL)==QAPLET_OK && drive.reason==QAPLET_DRIVE_UNSETTLED_ENTRY);
  CHECK(qaplet_pump(r,&p,NULL)==QAPLET_OK);
  CHECK(qaplet_run(r,&drive,NULL)==QAPLET_OK && drive.phase==QAPLET_PHASE_DONE);
  qaplet_result result=QAPLET_RESULT_INIT;CHECK(qaplet_get_result(r,&result,NULL)==QAPLET_OK && result.type==QAPLET_VALUE_DOUBLE && result.number==42);
  CHECK(!tcgetattr(slave,&after) && same(&before,&after));
  CHECK(qaplet_close(r,NULL)==QAPLET_OK && qaplet_destroy(r,NULL)==QAPLET_OK);
  CHECK(dup2(saved,0)==0 && !close(saved) && !close(slave) && !close(master));
  puts("M2 TTY JS: explicit stdio terminal lease; size query; unref is not cancel; latest resize via explicit pump; callback and closed Promise jobs; full mode restoration: pass");return 0;
}
static int output(void){
  CHECK(IsWindows());int64_t original=GetStdHandle(-11),console=CreateFile(u"CONOUT$",0xc0000000u,3,NULL,3,0,0);uint32_t mode,restored;
  CHECK(console!=-1 && GetConsoleMode(console,&mode) && SetStdHandle(-11,console));
  qaplet_config b=QAPLET_CONFIG_INIT;qaplet_m2_config m=QAPLET_M2_CONFIG_INIT;m.limits.pump_io_bytes=1;qaplet_runtime *r=NULL;
  CHECK(qaplet_create_m2(&b,&m,&r,NULL)==QAPLET_OK);
  qaplet_tty_options options=QAPLET_TTY_OPTIONS_INIT;options.flags=QAPLET_TTY_VT;qaplet_tty_status status=QAPLET_TTY_STATUS_INIT;qaplet_io_endpoint e=QAPLET_IO_ENDPOINT_INIT;
  CHECK(qaplet_tty_acquire(r,1,&options,&e,&status,NULL)==QAPLET_OK);
  const unsigned char invalid[][5]={{0xc0,0xaf},{0xed,0xa0,0x80},{0xf4,0x90,0x80,0x80},{0xe4,0xb8},{0x80}};const size_t lengths[]={2,3,4,2,1};
  for(unsigned i=0;i<5;++i){qaplet_io_operation refused=QAPLET_IO_OPERATION_INIT;CHECK(qaplet_io_write(r,&e,invalid[i],lengths[i],&refused,NULL)==QAPLET_INVALID_UTF8 && !refused.generation);}
  const char text[]="A\xe4\xb8\xad\xf0\x9f\x99\x82Z";qaplet_io_operation op=QAPLET_IO_OPERATION_INIT;
  CHECK(qaplet_io_write(r,&e,text,sizeof(text)-1,&op,NULL)==QAPLET_OK);
  qaplet_drive_result drive=QAPLET_DRIVE_RESULT_INIT;CHECK(qaplet_run(r,&drive,NULL)==QAPLET_OK);
  qaplet_io_result result=QAPLET_IO_RESULT_INIT;CHECK(qaplet_io_copy_result(r,&op,&result,NULL)==QAPLET_OK && result.code==QAPLET_OK && result.length==sizeof(text)-1);
  CHECK(qaplet_io_release(r,&op,NULL)==QAPLET_OK);
  struct NtConsoleScreenBufferInfo position;CHECK(GetConsoleScreenBufferInfo(console,&position));
  const char vt[]="\033[31mX\033[0m";CHECK(qaplet_io_write(r,&e,vt,sizeof(vt)-1,&op,NULL)==QAPLET_OK && qaplet_run(r,&drive,NULL)==QAPLET_OK);
  CHECK(qaplet_io_copy_result(r,&op,&result,NULL)==QAPLET_OK && result.code==QAPLET_OK && result.length==sizeof(vt)-1 && qaplet_io_release(r,&op,NULL)==QAPLET_OK);
  struct NtCharInfo cell={0};struct NtSmallRect region={position.dwCursorPosition.X,position.dwCursorPosition.Y,position.dwCursorPosition.X,position.dwCursorPosition.Y};
  CHECK(ReadConsoleOutput(console,&cell,(struct NtCoord){1,1},(struct NtCoord){0,0},&region) && cell.Char.UnicodeChar==88 && (cell.Attributes&7)==4);
  CHECK(qaplet_tty_release(r,&e,&status,NULL)==QAPLET_OK && status.restored);
  CHECK(qaplet_close(r,NULL)==QAPLET_OK && qaplet_quiesce(r,5000,&drive,NULL)==QAPLET_OK && qaplet_destroy(r,NULL)==QAPLET_OK);
  CHECK(GetConsoleMode(console,&restored) && restored==mode && SetStdHandle(-11,original) && CloseHandle(console));
  puts("M2 TTY Console output: strict UTF8 rejects overlong/surrogate/range/truncated/continuation before acceptance; one-byte native chunks preserve BMP and supplementary output; SGR31/reset rendered; exact written count and full mode cleanup: pass");return 0;
}
static int input(void){
  CHECK(IsWindows());int64_t original=GetStdHandle(-10),console=CreateFile(u"CONIN$",0xc0000000u,3,NULL,3,0,0);uint32_t mode,restored;
  CHECK(console!=-1 && GetConsoleMode(console,&mode) && SetStdHandle(-10,console) && FlushConsoleInputBuffer(console));
  qaplet_config b=QAPLET_CONFIG_INIT;qaplet_m2_config m=QAPLET_M2_CONFIG_INIT;m.limits.pump_io_bytes=1;qaplet_runtime *r=NULL;CHECK(qaplet_create_m2(&b,&m,&r,NULL)==QAPLET_OK);
  qaplet_tty_options options=QAPLET_TTY_OPTIONS_INIT;options.flags=QAPLET_TTY_RAW;qaplet_tty_status status=QAPLET_TTY_STATUS_INIT;qaplet_io_endpoint e=QAPLET_IO_ENDPOINT_INIT;
  CHECK(qaplet_tty_acquire(r,0,&options,&e,&status,NULL)==QAPLET_OK);
  qaplet_io_operation op=QAPLET_IO_OPERATION_INIT;qaplet_progress progress=QAPLET_PROGRESS_INIT;qaplet_drive_result drive=QAPLET_DRIVE_RESULT_INIT;
  CHECK(qaplet_io_read(r,&e,1,&op,NULL)==QAPLET_OK && qaplet_pump(r,&progress,NULL)==QAPLET_OK);
  CHECK(qaplet_io_cancel(r,&op,NULL)==QAPLET_OK && qaplet_run(r,&drive,NULL)==QAPLET_OK);
  qaplet_io_result result=QAPLET_IO_RESULT_INIT;unsigned char actual[5]={0};result.buffer=actual;result.capacity=sizeof(actual);
  for(unsigned i=0;i<1000 && qaplet_io_copy_result(r,&op,&result,NULL)==QAPLET_BUSY;++i)CHECK(qaplet_run_once(r,1,&drive,NULL)==QAPLET_OK);
  CHECK(qaplet_io_copy_result(r,&op,&result,NULL)==QAPLET_OK && result.code==QAPLET_CANCELLED && !result.length && !result.eof && qaplet_io_release(r,&op,NULL)==QAPLET_OK);
  FILE *gate=fopen("text.ready","wb");CHECK(gate && !fclose(gate));
  for(unsigned i=0;i<sizeof(actual);++i){
    CHECK(qaplet_io_read(r,&e,1,&op,NULL)==QAPLET_OK && qaplet_run(r,&drive,NULL)==QAPLET_OK);
    result.buffer=&actual[i];result.capacity=1;CHECK(qaplet_io_copy_result(r,&op,&result,NULL)==QAPLET_OK && result.code==QAPLET_OK && result.length==1 && !result.eof && qaplet_io_release(r,&op,NULL)==QAPLET_OK);
  }
  const unsigned char expected[]={0x41,0xe4,0xb8,0xad,0x5a};CHECK(!memcmp(actual,expected,sizeof(actual)));
  qaplet_code code=qaplet_tty_release(r,&e,&status,NULL);CHECK(code==QAPLET_OK || code==QAPLET_BUSY);
  CHECK(qaplet_close(r,NULL)==QAPLET_OK && qaplet_quiesce(r,5000,&drive,NULL)==QAPLET_OK && drive.reason==QAPLET_DRIVE_QUIESCENT);
  qaplet_m2_snapshot final=QAPLET_M2_SNAPSHOT_INIT;CHECK(qaplet_get_m2_snapshot(r,&final,NULL)==QAPLET_OK && !final.native_handles && !final.reactor_registrations && !final.native_uncertain_handles && qaplet_destroy(r,NULL)==QAPLET_OK);
  CHECK(GetConsoleMode(console,&restored) && restored==mode && SetStdHandle(-10,original) && CloseHandle(console));
  puts("M2 TTY Console input: bytewise ConPTY UTF8 transport to BMP key events to exact one-byte SDK reads; cancellation wake is not EOF; owner restores modes after helper join; native resources zero: pass");return 0;
}
static int stop_close(void){
  for(unsigned path=0;path<2;++path){
    int master=-1,slave=-1,saved=-1;int64_t original=0,console=0;struct termios before,after;uint32_t mode=0,checked=0;
    if(IsWindows()){original=GetStdHandle(-10);console=CreateFile(u"CONIN$",0xc0000000u,3,NULL,3,0,0);CHECK(console!=-1 && GetConsoleMode(console,&mode) && SetStdHandle(-10,console));}
    else{saved=dup(0);CHECK(saved>=0 && !openpty(&master,&slave,NULL,NULL,NULL) && dup2(slave,0)==0 && !tcgetattr(slave,&before));}
    qaplet_config b=QAPLET_CONFIG_INIT;qaplet_m2_config m=QAPLET_M2_CONFIG_INIT;qaplet_runtime *r=NULL;CHECK(qaplet_create_m2(&b,&m,&r,NULL)==QAPLET_OK);
    qaplet_tty_options options=QAPLET_TTY_OPTIONS_INIT;options.flags=QAPLET_TTY_RAW;qaplet_tty_status status=QAPLET_TTY_STATUS_INIT;qaplet_io_endpoint e=QAPLET_IO_ENDPOINT_INIT;
    CHECK(qaplet_tty_acquire(r,0,&options,&e,&status,NULL)==QAPLET_OK);
    qaplet_io_operation op=QAPLET_IO_OPERATION_INIT;qaplet_progress p=QAPLET_PROGRESS_INIT;CHECK(qaplet_io_read(r,&e,16,&op,NULL)==QAPLET_OK && qaplet_pump(r,&p,NULL)==QAPLET_OK);
    qaplet_drive_result drive=QAPLET_DRIVE_RESULT_INIT;
    if(!path){
      CHECK(qaplet_request_stop(r,NULL)==QAPLET_OK);
      if(IsWindows())CHECK(GetConsoleMode(console,&checked) && checked!=mode);else CHECK(!tcgetattr(slave,&after) && !same(&before,&after));
      CHECK(qaplet_run(r,&drive,NULL)==QAPLET_EXECUTION_STOPPED);
      struct timespec start,now;CHECK(!clock_gettime(CLOCK_MONOTONIC,&start));
      do{CHECK(qaplet_get_tty_status(r,&e,&status,NULL)==QAPLET_OK);if(status.restored)break;CHECK(qaplet_run_once(r,0,&drive,NULL)==QAPLET_EXECUTION_STOPPED);sched_yield();CHECK(!clock_gettime(CLOCK_MONOTONIC,&now));CHECK(now.tv_sec-start.tv_sec<5);}while(1);
      CHECK(status.restored && !status.active);
      if(IsWindows())CHECK(GetConsoleMode(console,&checked) && checked==mode);else CHECK(!tcgetattr(slave,&after) && same(&before,&after));
    }
    CHECK(qaplet_close(r,NULL)==QAPLET_OK && qaplet_close(r,NULL)==QAPLET_OK);
    CHECK(qaplet_quiesce(r,5000,&drive,NULL)==QAPLET_OK && drive.reason==QAPLET_DRIVE_QUIESCENT);
    qaplet_m2_snapshot final=QAPLET_M2_SNAPSHOT_INIT;CHECK(qaplet_get_m2_snapshot(r,&final,NULL)==QAPLET_OK && !final.native_handles && !final.reactor_registrations && !final.native_uncertain_handles && qaplet_destroy(r,NULL)==QAPLET_OK);
    if(IsWindows())CHECK(GetConsoleMode(console,&checked) && checked==mode && SetStdHandle(-10,original) && CloseHandle(console));
    else CHECK(!tcgetattr(slave,&after) && same(&before,&after) && !(fcntl(slave,F_GETFL)&O_NONBLOCK) && dup2(saved,0)==0 && !close(saved) && !close(slave) && !close(master));
  }
  puts("M2 TTY stop/close: pending native input; stop publication leaves modes for owner; owner confirms cancellation; repeated close and native quiesce join before full restoration; Host originals survive and native resources zero: pass");return 0;
}
static int console_resize(int javascript){
  CHECK(IsWindows());int64_t input=CreateFile(u"CONIN$",0xc0000000u,3,NULL,3,0,0),output=CreateFile(u"CONOUT$",0xc0000000u,3,NULL,3,0,0),saved=GetStdHandle(-11);uint32_t im,om,checked;
  CHECK(input!=-1 && output!=-1 && GetConsoleMode(input,&im) && GetConsoleMode(output,&om) && SetConsoleMode(input,(im&~6u)|8u) && FlushConsoleInputBuffer(input) && SetStdHandle(-11,output));
  qaplet_config b=QAPLET_CONFIG_INIT;qaplet_m2_config m=QAPLET_M2_CONFIG_INIT;qaplet_runtime *r=NULL;CHECK(qaplet_create_m2(&b,&m,&r,NULL)==QAPLET_OK);
  qaplet_io_endpoint e=QAPLET_IO_ENDPOINT_INIT;qaplet_tty_status status=QAPLET_TTY_STATUS_INIT;qaplet_progress p=QAPLET_PROGRESS_INIT;qaplet_drive_result drive=QAPLET_DRIVE_RESULT_INIT;
  if(javascript){
    const char *source="import('qaplet:stdio').then(m=>{const e=m.acquireTerminal(1,{vt:true});if(e.getSize().columns!==80)throw Error('initial');e.unref();return new Promise(resolve=>e.watchResize(s=>{if(s.columns!==100||s.rows!==33)throw Error('latest');e.unwatchResize();e.close();e.closed.then(()=>resolve(42))}))})";
    CHECK(qaplet_eval(r,source,strlen(source),&p,NULL)==QAPLET_OK && qaplet_run(r,&drive,NULL)==QAPLET_OK && drive.reason==QAPLET_DRIVE_UNSETTLED_ENTRY);
    qaplet_stdio_info info=QAPLET_STDIO_INFO_INIT;CHECK(qaplet_stdio_get(r,1,&e,&info,NULL)==QAPLET_OK);
  }else{
    qaplet_tty_options options=QAPLET_TTY_OPTIONS_INIT;options.flags=QAPLET_TTY_VT;CHECK(qaplet_tty_acquire(r,1,&options,&e,&status,NULL)==QAPLET_OK);
    CHECK(qaplet_tty_get_size(r,&e,&status,NULL)==QAPLET_OK && status.columns==80 && status.rows==24 && qaplet_tty_watch_resize(r,&e,1,NULL)==QAPLET_OK);
  }
  FILE *gate=fopen("resize.ready","wb");CHECK(gate && !fclose(gate));int final=0;unsigned notifications=0;
  while(!final){
    CHECK(WaitForSingleObject(input,2000)==0);struct NtInputRecord record;uint32_t got;
    CHECK(ReadConsoleInput(input,&record,1,&got) && got==1);
    if(record.EventType==kNtWindowBufferSizeEvent){++notifications;final=record.Event.WindowBufferSizeEvent.dwSize.X==100 && record.Event.WindowBufferSizeEvent.dwSize.Y==33;CHECK(qaplet_tty_notify_resize(r,&e,NULL)==QAPLET_OK);}
  }
  CHECK(notifications>0 && qaplet_pump(r,&p,NULL)==QAPLET_OK);
  if(javascript){qaplet_result result=QAPLET_RESULT_INIT;CHECK(qaplet_run(r,&drive,NULL)==QAPLET_OK && drive.phase==QAPLET_PHASE_DONE && qaplet_get_result(r,&result,NULL)==QAPLET_OK && result.type==QAPLET_VALUE_DOUBLE && result.number==42);}
  else {CHECK(qaplet_get_tty_status(r,&e,&status,NULL)==QAPLET_OK && status.columns==100 && status.rows==33 && status.revision==1 && qaplet_tty_watch_resize(r,&e,0,NULL)==QAPLET_OK && qaplet_tty_release(r,&e,&status,NULL)==QAPLET_OK && status.restored);}
  CHECK(qaplet_close(r,NULL)==QAPLET_OK && qaplet_quiesce(r,5000,&drive,NULL)==QAPLET_OK && drive.reason==QAPLET_DRIVE_QUIESCENT && qaplet_destroy(r,NULL)==QAPLET_OK);
  CHECK(GetConsoleMode(output,&checked) && checked==om && SetConsoleMode(input,im) && GetConsoleMode(input,&checked) && checked==im && SetStdHandle(-11,saved) && CloseHandle(input) && CloseHandle(output));
  puts(javascript ? "M2 TTY Console JS resize: explicit lease; real final window event precedes owner size query; latest callback and closed Promise jobs; unref explicit pump; full mode restoration and cleanup: pass" : "M2 TTY Console resize: real final window event precedes owner size query; coalesced latest100x33 and single revision; full mode restoration and cleanup: pass");return 0;
}
int sdk_tty(int argc,char **argv){(void)argc;if(!strcmp(argv[1],"--m2-tty-lease"))return lease();if(!strcmp(argv[1],"--m2-tty-resize"))return IsWindows() ? console_resize(0) : resize();if(!strcmp(argv[1],"--m2-tty-js"))return IsWindows() ? console_resize(1) : js();if(!strcmp(argv[1],"--m2-tty-output"))return output();if(!strcmp(argv[1],"--m2-tty-input"))return input();if(!strcmp(argv[1],"--m2-tty-stop-close"))return stop_close();return 2;}
#ifdef QAPLET_TTY_STANDALONE
int main(int argc,char **argv){return sdk_tty(argc,argv);}
#endif
