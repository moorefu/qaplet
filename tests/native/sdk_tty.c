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
#include <signal.h>
#include <poll.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include "libc/dce.h"
#include "libc/nt/console.h"
#include "libc/nt/runtime.h"
#include "libc/nt/synchronization.h"
extern int64_t CreateFile(const char16_t *,uint32_t,uint32_t,void *,uint32_t,uint32_t,int64_t);
extern bool32 DuplicateHandle(int64_t,int64_t,int64_t,int64_t *,uint32_t,bool32,uint32_t);
#define CHECK(c) do{if(!(c)){fprintf(stderr,"TTY public contract line %d: %s (errno %d)\n",__LINE__,#c,errno);return 1;}}while(0)
static int record_mode(const char *stage,int fd,int64_t handle){
  FILE *file=fopen("tty-native.ndjson","ab");if(!file)return 0;
  int ok=1;
  if(IsWindows()){uint32_t mode=0;ok=GetConsoleMode(handle,&mode);fprintf(file,"{\"stage\":\"%s\",\"console_mode\":%u,\"ok\":%s}\n",stage,mode,ok ? "true" : "false");}
  else{struct termios mode={0};ok=!tcgetattr(fd,&mode);int flags=fcntl(fd,F_GETFL);fprintf(file,"{\"stage\":\"%s\",\"iflag\":%llu,\"oflag\":%llu,\"cflag\":%llu,\"lflag\":%llu,\"ispeed\":%u,\"ospeed\":%u,\"flags\":%d,\"cc\":[",stage,(unsigned long long)mode.c_iflag,(unsigned long long)mode.c_oflag,(unsigned long long)mode.c_cflag,(unsigned long long)mode.c_lflag,(unsigned)cfgetispeed(&mode),(unsigned)cfgetospeed(&mode),flags);for(unsigned i=0;i<sizeof(mode.c_cc);++i)fprintf(file,"%s%u",i ? "," : "",(unsigned char)mode.c_cc[i]);fprintf(file,"],\"ok\":%s}\n",ok && flags>=0 ? "true" : "false");}
  return !fclose(file) && ok;
}
static int same(const struct termios *a,const struct termios *b){return a->c_iflag==b->c_iflag && a->c_oflag==b->c_oflag && a->c_cflag==b->c_cflag && a->c_lflag==b->c_lflag && !memcmp(a->c_cc,b->c_cc,sizeof(a->c_cc)) && cfgetispeed(a)==cfgetispeed(b) && cfgetospeed(a)==cfgetospeed(b);}
static int finish_tty(qaplet_runtime *r){
  CHECK(qaplet_close(r,NULL)==QAPLET_OK);qaplet_drive_result drive=QAPLET_DRIVE_RESULT_INIT;
  CHECK(qaplet_quiesce(r,5000,&drive,NULL)==QAPLET_OK && drive.reason==QAPLET_DRIVE_QUIESCENT);
  qaplet_m2_snapshot native=QAPLET_M2_SNAPSHOT_INIT;qaplet_io_snapshot io=QAPLET_IO_SNAPSHOT_INIT;
  CHECK(qaplet_get_m2_snapshot(r,&native,NULL)==QAPLET_OK && !native.native_handles && !native.native_uncertain_handles && !native.reactor_registrations && qaplet_get_io_snapshot(r,&io,NULL)==QAPLET_OK && !io.endpoints && !io.operations && !io.data_bytes && !io.helper_threads && qaplet_destroy(r,NULL)==QAPLET_OK);return 0;
}
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
  CHECK(record_mode("before",slave,console));
  CHECK(qaplet_tty_acquire(r,0,&options,&endpoint,&status,NULL)==QAPLET_OK && status.active);
  CHECK(qaplet_tty_acquire(r,1,&options,&alias,&status,NULL)==QAPLET_BUSY && !alias.generation);
  qaplet_runtime *other=NULL;CHECK(qaplet_create_m2(&b,&m,&other,NULL)==QAPLET_OK);
  CHECK(qaplet_tty_acquire(other,1,&options,&alias,&status,NULL)==QAPLET_BUSY && !alias.generation);
  if(IsWindows()){CHECK(GetConsoleMode(console,&measured) && measured!=mode);}
  else CHECK(!tcgetattr(slave,&modified) && !same(&before,&modified) && (fcntl(slave,F_GETFL)&O_NONBLOCK));
  CHECK(record_mode("modified",slave,console));
  CHECK(qaplet_tty_release(r,&endpoint,&status,NULL)==QAPLET_OK && !status.active && status.restored);
  CHECK(qaplet_tty_release(r,&endpoint,&status,NULL)==QAPLET_OK && status.restored);
  if(IsWindows()){CHECK(GetConsoleMode(console,&measured) && measured==mode);}
  else CHECK(!tcgetattr(slave,&after) && same(&before,&after) && !(fcntl(slave,F_GETFL)&O_NONBLOCK));
  CHECK(record_mode("restored",slave,console));
  CHECK(qaplet_stdio_detach(r,0,NULL)==QAPLET_OK);
  CHECK(qaplet_tty_acquire(other,0,&options,&alias,&status,NULL)==QAPLET_OK);
  CHECK(qaplet_tty_release(other,&alias,&status,NULL)==QAPLET_OK && status.restored);
  CHECK(!finish_tty(other) && !finish_tty(r));
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
  CHECK(!finish_tty(r));
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
  CHECK(!finish_tty(r));
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
  const char *chunks[]={"\xe4\xb8\xad","\xf0\x9f\x99\x82"};
  for(unsigned i=0;i<2;++i){qaplet_tty_write_status written=QAPLET_TTY_WRITE_STATUS_INIT;size_t n=strlen(chunks[i]);CHECK(qaplet_io_write(r,&e,chunks[i],n,&op,NULL)==QAPLET_OK && qaplet_run(r,&drive,NULL)==QAPLET_OK && qaplet_io_copy_result(r,&op,&result,NULL)==QAPLET_OK && result.code==QAPLET_OK && result.length==n && qaplet_get_tty_write_status(r,&op,&written,NULL)==QAPLET_OK && written.native_utf16_units==i+1 && !written.partial_scalar && qaplet_io_release(r,&op,NULL)==QAPLET_OK);}
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
    int master=-1,slave=-1,saved=-1,saved_output=-1;int64_t original=0,console=0,original_output=0,output=0;struct termios before,after;uint32_t mode=0,output_mode=0,checked=0;
    if(IsWindows()){original=GetStdHandle(-10);console=CreateFile(u"CONIN$",0xc0000000u,3,NULL,3,0,0);CHECK(console!=-1 && GetConsoleMode(console,&mode) && SetStdHandle(-10,console));}
    else{saved=dup(0);CHECK(saved>=0 && !openpty(&master,&slave,NULL,NULL,NULL) && dup2(slave,0)==0 && !tcgetattr(slave,&before));}
    if(IsWindows()){original_output=GetStdHandle(-11);output=CreateFile(u"CONOUT$",0xc0000000u,3,NULL,3,0,0);CHECK(output!=-1 && GetConsoleMode(output,&output_mode) && SetStdHandle(-11,output));}
    else{saved_output=dup(1);CHECK(saved_output>=0 && dup2(slave,1)==1);}
    qaplet_config b=QAPLET_CONFIG_INIT;qaplet_m2_config m=QAPLET_M2_CONFIG_INIT;qaplet_runtime *r=NULL;CHECK(qaplet_create_m2(&b,&m,&r,NULL)==QAPLET_OK);
    qaplet_tty_options options=QAPLET_TTY_OPTIONS_INIT;options.flags=QAPLET_TTY_RAW;qaplet_tty_status status=QAPLET_TTY_STATUS_INIT;qaplet_io_endpoint e=QAPLET_IO_ENDPOINT_INIT;
    CHECK(qaplet_tty_acquire(r,0,&options,&e,&status,NULL)==QAPLET_OK);
    qaplet_tty_options vt=QAPLET_TTY_OPTIONS_INIT;vt.flags=QAPLET_TTY_VT;qaplet_io_endpoint joined=QAPLET_IO_ENDPOINT_INIT;CHECK(qaplet_tty_join(r,&e,1,&vt,&joined,&status,NULL)==QAPLET_OK);
    qaplet_io_operation op=QAPLET_IO_OPERATION_INIT;qaplet_progress p=QAPLET_PROGRESS_INIT;CHECK(qaplet_io_read(r,&e,16,&op,NULL)==QAPLET_OK && qaplet_pump(r,&p,NULL)==QAPLET_OK);
    qaplet_drive_result drive=QAPLET_DRIVE_RESULT_INIT;
    if(!path){
      CHECK(qaplet_request_stop(r,NULL)==QAPLET_OK);
      if(IsWindows())CHECK(GetConsoleMode(console,&checked) && checked!=mode);else CHECK(!tcgetattr(slave,&after) && !same(&before,&after));
      CHECK(qaplet_run(r,&drive,NULL)==QAPLET_EXECUTION_STOPPED);
      struct timespec start,now;CHECK(!clock_gettime(CLOCK_MONOTONIC,&start));
      do{CHECK(qaplet_get_tty_status(r,&e,&status,NULL)==QAPLET_OK);if(status.restored)break;CHECK(qaplet_run_once(r,0,&drive,NULL)==QAPLET_EXECUTION_STOPPED);sched_yield();CHECK(!clock_gettime(CLOCK_MONOTONIC,&now));CHECK(now.tv_sec-start.tv_sec<5);}while(1);
      CHECK(status.restored && !status.active);
      if(IsWindows())CHECK(GetConsoleMode(console,&checked) && checked==mode && GetConsoleMode(output,&checked) && checked==output_mode);else CHECK(!tcgetattr(slave,&after) && same(&before,&after));
    }
    CHECK(qaplet_close(r,NULL)==QAPLET_OK && qaplet_close(r,NULL)==QAPLET_OK);
    CHECK(qaplet_quiesce(r,5000,&drive,NULL)==QAPLET_OK && drive.reason==QAPLET_DRIVE_QUIESCENT);
    qaplet_m2_snapshot final=QAPLET_M2_SNAPSHOT_INIT;CHECK(qaplet_get_m2_snapshot(r,&final,NULL)==QAPLET_OK && !final.native_handles && !final.reactor_registrations && !final.native_uncertain_handles && qaplet_destroy(r,NULL)==QAPLET_OK);
    if(IsWindows())CHECK(GetConsoleMode(console,&checked) && checked==mode && GetConsoleMode(output,&checked) && checked==output_mode && SetStdHandle(-10,original) && SetStdHandle(-11,original_output) && CloseHandle(console) && CloseHandle(output));
    else CHECK(!tcgetattr(slave,&after) && same(&before,&after) && !(fcntl(slave,F_GETFL)&O_NONBLOCK) && dup2(saved,0)==0 && dup2(saved_output,1)==1 && !close(saved) && !close(saved_output) && !close(slave) && !close(master));
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
typedef struct {qaplet_runtime *runtime;qaplet_code code;} wrong_tty;
static void *wrong_tty_owner(void *opaque){wrong_tty *p=opaque;qaplet_tty_support support=QAPLET_TTY_SUPPORT_INIT;p->code=qaplet_tty_inspect(p->runtime,0,&support,NULL);return NULL;}
static int validation(void){
  qaplet_config b=QAPLET_CONFIG_INIT;qaplet_m2_config m=QAPLET_M2_CONFIG_INIT;qaplet_runtime *r=NULL;CHECK(qaplet_create_m2(&b,&m,&r,NULL)==QAPLET_OK);
  qaplet_abi abi=QAPLET_ABI_INIT;CHECK(qaplet_query_abi(&abi)==QAPLET_OK && (abi.capabilities&QAPLET_CAP_TTY));
  qaplet_tty_options options=QAPLET_TTY_OPTIONS_INIT;qaplet_tty_status status=QAPLET_TTY_STATUS_INIT;qaplet_io_endpoint e=QAPLET_IO_ENDPOINT_INIT;
  options.flags=0x80000000u;CHECK(qaplet_tty_acquire(r,0,&options,&e,&status,NULL)==QAPLET_INVALID_ARGUMENT && !e.generation);options.flags=0;options.reserved=1;
  CHECK(qaplet_tty_acquire(r,0,&options,&e,&status,NULL)==QAPLET_INVALID_ARGUMENT && !e.generation);options.reserved=0;options.struct_size=8;
  CHECK(qaplet_tty_acquire(r,0,&options,&e,&status,NULL)==QAPLET_ABI_MISMATCH && !e.generation);options=(qaplet_tty_options)QAPLET_TTY_OPTIONS_INIT;
  struct {qaplet_tty_options prefix;uint64_t unknown;} extended={QAPLET_TTY_OPTIONS_INIT,1};extended.prefix.struct_size=sizeof(extended);
  CHECK(qaplet_tty_acquire(r,0,&extended.prefix,&e,&status,NULL)==QAPLET_ABI_MISMATCH && !e.generation);
  qaplet_tty_support support=QAPLET_TTY_SUPPORT_INIT;CHECK(qaplet_tty_inspect(r,3,&support,NULL)==QAPLET_INVALID_ARGUMENT);
  CHECK(qaplet_get_tty_status(r,&e,&status,NULL)==QAPLET_INVALID_HANDLE && qaplet_tty_notify_resize(r,&e,NULL)==QAPLET_INVALID_HANDLE);
  wrong_tty p={r,QAPLET_OK};pthread_t t;CHECK(!pthread_create(&t,NULL,wrong_tty_owner,&p) && !pthread_join(t,NULL) && p.code==QAPLET_WRONG_THREAD);
  qaplet_io_snapshot io=QAPLET_IO_SNAPSHOT_INIT;CHECK(qaplet_get_io_snapshot(r,&io,NULL)==QAPLET_OK && !io.endpoints && !io.operations && !io.data_bytes && qaplet_close(r,NULL)==QAPLET_OK && qaplet_destroy(r,NULL)==QAPLET_OK);
  CHECK(qaplet_create(&b,&r,NULL)==QAPLET_OK && qaplet_tty_inspect(r,0,&support,NULL)==QAPLET_UNSUPPORTED && qaplet_close(r,NULL)==QAPLET_OK && qaplet_destroy(r,NULL)==QAPLET_OK);
  puts("M2 TTY validation: strict version/size/unknown flags/reserved/tail; wrong owner and invalid identity; no accepted resource; old factory remains unsupported: pass");return 0;
}
static int resize_event_fd=-1;
static void resize_signal_handler(int signal){(void)signal;int saved=errno;char event='W';write(resize_event_fd,&event,1);errno=saved;}
typedef struct {qaplet_runtime *runtime;qaplet_io_endpoint endpoint;int fd;qaplet_code code;int signalled;} resize_bridge;
static void *publish_native_resize(void *opaque){resize_bridge *bridge=opaque;struct pollfd fd={bridge->fd,POLLIN,0};char event=0;bridge->signalled=poll(&fd,1,2000)==1 && read(bridge->fd,&event,1)==1 && event=='W';bridge->code=bridge->signalled ? qaplet_tty_notify_resize(bridge->runtime,&bridge->endpoint,NULL) : QAPLET_IO_ERROR;return NULL;}
static int resize_signal(void){
  CHECK(!IsWindows());int master,slave,events[2],ack[2],saved=dup(0);CHECK(saved>=0 && !openpty(&master,&slave,NULL,NULL,NULL) && !pipe(events) && !pipe(ack));
  struct sigaction before,after;CHECK(!sigaction(SIGWINCH,NULL,&before));
  pid_t child=fork();CHECK(child>=0);
  if(!child){
    close(events[0]);close(ack[1]);close(master);resize_event_fd=events[1];struct sigaction action={0};action.sa_handler=resize_signal_handler;sigemptyset(&action.sa_mask);
    if(setsid()<0 || ioctl(slave,TIOCSCTTY,0) || tcsetpgrp(slave,getpgrp()) || sigaction(SIGWINCH,&action,NULL))_exit(3);
    char ready='R';if(write(events[1],&ready,1)!=1)_exit(4);char done;ssize_t n;do{n=read(ack[0],&done,1);}while(n<0 && errno==EINTR);_exit(n==1 && done=='A' ? 0 : 5);
  }
  CHECK(!close(events[1]) && !close(ack[0]));char ready;CHECK(read(events[0],&ready,1)==1 && ready=='R' && dup2(slave,0)==0);
  qaplet_config b=QAPLET_CONFIG_INIT;qaplet_m2_config m=QAPLET_M2_CONFIG_INIT;qaplet_runtime *r=NULL;CHECK(qaplet_create_m2(&b,&m,&r,NULL)==QAPLET_OK);
  qaplet_tty_options options=QAPLET_TTY_OPTIONS_INIT;qaplet_tty_status status=QAPLET_TTY_STATUS_INIT;qaplet_io_endpoint e=QAPLET_IO_ENDPOINT_INIT;CHECK(qaplet_tty_acquire(r,0,&options,&e,&status,NULL)==QAPLET_OK && qaplet_tty_watch_resize(r,&e,1,NULL)==QAPLET_OK);
  resize_bridge bridge={r,e,events[0],QAPLET_BAD_STATE,0};pthread_t producer;CHECK(!pthread_create(&producer,NULL,publish_native_resize,&bridge));
  struct winsize size={.ws_col=90,.ws_row=31};CHECK(!ioctl(slave,TIOCSWINSZ,&size));size.ws_col=100;size.ws_row=33;CHECK(!ioctl(slave,TIOCSWINSZ,&size));
  CHECK(!pthread_join(producer,NULL) && bridge.signalled && bridge.code==QAPLET_OK);
  qaplet_progress p=QAPLET_PROGRESS_INIT;CHECK(qaplet_pump(r,&p,NULL)==QAPLET_OK && qaplet_get_tty_status(r,&e,&status,NULL)==QAPLET_OK && status.columns==100 && status.rows==33 && status.revision==1);
  CHECK(qaplet_tty_watch_resize(r,&e,0,NULL)==QAPLET_OK && qaplet_tty_release(r,&e,&status,NULL)==QAPLET_OK && qaplet_close(r,NULL)==QAPLET_OK && qaplet_destroy(r,NULL)==QAPLET_OK);
  CHECK(!sigaction(SIGWINCH,NULL,&after) && before.sa_handler==after.sa_handler && before.sa_flags==after.sa_flags);
  char done='A';CHECK(write(ack[1],&done,1)==1);int child_status;CHECK(waitpid(child,&child_status,0)==child && WIFEXITED(child_status) && WEXITSTATUS(child_status)==0);
  CHECK(dup2(saved,0)==0 && !close(saved) && !close(events[0]) && !close(ack[1]) && !close(slave) && !close(master));
  puts("M2 TTY SIGWINCH: actual foreground PTY resize reaches Host signal self-pipe; native producer publishes across threads; owner queries final100x33 while child lives; SDK signal disposition unchanged; joined/reaped/closed: pass");return 0;
}
static int bytes(void){
  CHECK(!IsWindows());int master,slave,saved[2]={dup(0),dup(1)};CHECK(saved[0]>=0 && saved[1]>=0 && !openpty(&master,&slave,NULL,NULL,NULL) && dup2(slave,0)==0);
  struct termios before,after;CHECK(!tcgetattr(slave,&before));int flags=fcntl(slave,F_GETFL);
  qaplet_config b=QAPLET_CONFIG_INIT;qaplet_m2_config m=QAPLET_M2_CONFIG_INIT;m.limits.operations=1;m.limits.operation_data_bytes=16;m.limits.io_block_bytes=16;m.limits.pump_io_bytes=3;qaplet_runtime *r=NULL;CHECK(qaplet_create_m2(&b,&m,&r,NULL)==QAPLET_OK);
  qaplet_tty_options options=QAPLET_TTY_OPTIONS_INIT;options.flags=QAPLET_TTY_RAW;qaplet_tty_status status=QAPLET_TTY_STATUS_INIT;qaplet_io_endpoint e=QAPLET_IO_ENDPOINT_INIT;CHECK(qaplet_tty_acquire(r,0,&options,&e,&status,NULL)==QAPLET_OK);
  unsigned char payload[256],actual[256];for(unsigned i=0;i<256;++i)payload[i]=i;CHECK(write(master,payload,256)==256);size_t offset=0;qaplet_drive_result drive=QAPLET_DRIVE_RESULT_INIT;
  while(offset<256){
    qaplet_io_operation op=QAPLET_IO_OPERATION_INIT,refused=QAPLET_IO_OPERATION_INIT;CHECK(qaplet_io_read(r,&e,16,&op,NULL)==QAPLET_OK && qaplet_run(r,&drive,NULL)==QAPLET_OK);
    qaplet_io_snapshot ledger=QAPLET_IO_SNAPSHOT_INIT;CHECK(qaplet_get_io_snapshot(r,&ledger,NULL)==QAPLET_OK && ledger.shared_operations==1 && ledger.shared_data_bytes==16);
    CHECK(qaplet_io_read(r,&e,1,&refused,NULL)==QAPLET_RESOURCE_LIMIT && !refused.generation);
    qaplet_io_result result=QAPLET_IO_RESULT_INIT;result.buffer=actual+offset;result.capacity=256-offset;CHECK(qaplet_io_copy_result(r,&op,&result,NULL)==QAPLET_OK && result.code==QAPLET_OK && result.length>0 && !result.eof);offset+=result.length;CHECK(qaplet_io_release(r,&op,NULL)==QAPLET_OK);
  }
  CHECK(!memcmp(payload,actual,256) && qaplet_tty_release(r,&e,&status,NULL)==QAPLET_OK && status.restored && qaplet_stdio_detach(r,0,NULL)==QAPLET_OK);
  CHECK(dup2(slave,1)==1 && qaplet_tty_acquire(r,1,&options,&e,&status,NULL)==QAPLET_OK);
  for(offset=0;offset<256;offset+=16){qaplet_io_operation op=QAPLET_IO_OPERATION_INIT;CHECK(qaplet_io_write(r,&e,payload+offset,16,&op,NULL)==QAPLET_OK && qaplet_run(r,&drive,NULL)==QAPLET_OK);qaplet_io_result result=QAPLET_IO_RESULT_INIT;CHECK(qaplet_io_copy_result(r,&op,&result,NULL)==QAPLET_OK && result.code==QAPLET_OK && result.length==16 && qaplet_io_release(r,&op,NULL)==QAPLET_OK);}
  CHECK(read(master,actual,256)==256 && !memcmp(payload,actual,256) && qaplet_tty_release(r,&e,&status,NULL)==QAPLET_OK && status.restored);
  CHECK(qaplet_close(r,NULL)==QAPLET_OK && qaplet_destroy(r,NULL)==QAPLET_OK && !tcgetattr(slave,&after) && same(&before,&after) && fcntl(slave,F_GETFL)==flags);
  CHECK(dup2(saved[0],0)==0 && dup2(saved[1],1)==1 && !close(saved[0]) && !close(saved[1]) && !close(slave) && !close(master));
  puts("M2 TTY POSIX bytes: exact all256/NUL/invalid UTF8 raw input and output; three-byte native budget; retained results occupy shared count/data; rejection atomic; full mode/flags restored: pass");return 0;
}
static int identities(void){
  int master=-1,slave=-1,saved=-1;int64_t original=0,console=0,source=0;struct termios before,after;uint32_t mode=0,checked=0;
  if(IsWindows()){original=GetStdHandle(-10);console=CreateFile(u"CONIN$",0xc0000000u,3,NULL,3,0,0);CHECK(console!=-1 && DuplicateHandle(GetCurrentProcess(),console,GetCurrentProcess(),&source,0,0,2) && GetConsoleMode(console,&mode) && SetStdHandle(-10,console));}
  else{saved=dup(0);CHECK(saved>=0 && !openpty(&master,&slave,NULL,NULL,NULL) && dup2(slave,0)==0 && !tcgetattr(slave,&before));}
  qaplet_config b=QAPLET_CONFIG_INIT;qaplet_m2_config m=QAPLET_M2_CONFIG_INIT;m.limits.native_handles=3;m.limits.reactor_registrations=1;qaplet_runtime *r=NULL;CHECK(qaplet_create_m2(&b,&m,&r,NULL)==QAPLET_OK);
  qaplet_tty_options options=QAPLET_TTY_OPTIONS_INIT;options.flags=QAPLET_TTY_RAW;qaplet_tty_status status=QAPLET_TTY_STATUS_INIT;qaplet_io_endpoint e=QAPLET_IO_ENDPOINT_INIT;
  CHECK(qaplet_tty_acquire(r,0,&options,&e,&status,NULL)==QAPLET_RESOURCE_LIMIT && !e.generation && qaplet_close(r,NULL)==QAPLET_OK && qaplet_destroy(r,NULL)==QAPLET_OK);
  m.limits.reactor_registrations=2;m.limits.native_handles=IsWindows() ? 4 : 3;m.limits.operations=1;m.limits.operation_data_bytes=4;m.limits.io_block_bytes=4;CHECK(qaplet_create_m2(&b,&m,&r,NULL)==QAPLET_OK);
  qaplet_runtime *other=NULL;CHECK(qaplet_create_m2(&b,&m,&other,NULL)==QAPLET_OK);
  unsigned char open_before[256]={0};if(!IsWindows())for(int fd=3;fd<256;++fd)open_before[fd]=fcntl(fd,F_GETFD)>=0;
  CHECK(qaplet_tty_acquire(r,0,&options,&e,&status,NULL)==QAPLET_OK && qaplet_get_tty_status(other,&e,&status,NULL)==QAPLET_INVALID_HANDLE && qaplet_tty_notify_resize(other,&e,NULL)==QAPLET_INVALID_HANDLE);
  int reused_fd=-1;if(!IsWindows())for(int fd=3;fd<256;++fd)if(!open_before[fd] && fcntl(fd,F_GETFD)>=0){CHECK(reused_fd==-1);reused_fd=fd;}
  qaplet_io_operation op=QAPLET_IO_OPERATION_INIT;
  if(IsWindows()){CHECK(qaplet_io_read(r,&e,4,&op,NULL)==QAPLET_RESOURCE_LIMIT && !op.generation);qaplet_io_snapshot io=QAPLET_IO_SNAPSHOT_INIT;CHECK(qaplet_get_io_snapshot(r,&io,NULL)==QAPLET_OK && !io.operations && !io.data_bytes && io.helper_threads==1);}
  else{CHECK(write(master,"abcd",4)==4 && qaplet_io_read(r,&e,4,&op,NULL)==QAPLET_OK);qaplet_drive_result drive=QAPLET_DRIVE_RESULT_INIT;CHECK(qaplet_run(r,&drive,NULL)==QAPLET_OK);qaplet_io_operation refused=QAPLET_IO_OPERATION_INIT;CHECK(qaplet_io_read(r,&e,1,&refused,NULL)==QAPLET_RESOURCE_LIMIT && !refused.generation && qaplet_io_release(r,&op,NULL)==QAPLET_OK);}
  CHECK(qaplet_tty_release(r,&e,&status,NULL)==QAPLET_OK && status.restored && qaplet_stdio_detach(r,0,NULL)==QAPLET_OK);
  if(IsWindows()){int64_t old=console;CHECK(CloseHandle(console) && DuplicateHandle(GetCurrentProcess(),source,GetCurrentProcess(),&console,0,0,2) && console==old && SetStdHandle(-10,console));}
  else CHECK(reused_fd>=0 && fcntl(reused_fd,F_GETFD)<0 && errno==EBADF);
  qaplet_io_endpoint next=QAPLET_IO_ENDPOINT_INIT;CHECK(qaplet_tty_acquire(r,0,&options,&next,&status,NULL)==QAPLET_OK && next.slot==e.slot && next.generation!=e.generation);
  if(!IsWindows())CHECK(fcntl(reused_fd,F_GETFD)>=0);
  CHECK(qaplet_get_tty_status(r,&e,&status,NULL)==QAPLET_INVALID_HANDLE && qaplet_tty_notify_resize(r,&e,NULL)==QAPLET_INVALID_HANDLE);
  if(IsWindows())CHECK(qaplet_tty_watch_resize(r,&next,1,NULL)==QAPLET_UNSUPPORTED && qaplet_tty_get_size(r,&next,&status,NULL)==QAPLET_UNSUPPORTED);
  else CHECK(qaplet_tty_watch_resize(r,&next,1,NULL)==QAPLET_OK && qaplet_tty_notify_resize(r,&next,NULL)==QAPLET_OK && qaplet_tty_watch_resize(r,&next,0,NULL)==QAPLET_OK);
  CHECK(qaplet_tty_release(r,&next,&status,NULL)==QAPLET_OK && !finish_tty(r) && !finish_tty(other));
  if(IsWindows())CHECK(GetConsoleMode(console,&checked) && checked==mode && SetStdHandle(-10,original) && CloseHandle(console) && CloseHandle(source));
  else CHECK(!tcgetattr(slave,&after) && same(&before,&after) && dup2(saved,0)==0 && !close(saved) && !close(slave) && !close(master));
  puts("M2 TTY identity/limits: finite handle/registration and retained operation/data refusal; no mode side effect on rejection; cross Runtime token rejection; actual native number reuse plus new generation rejects stale notification; full cleanup: pass");return 0;
}
static int join_js(void){
  int master=-1,slave=-1,saved[2]={-1,-1};int64_t input=0,output=0,original[2]={0};struct termios before,after;uint32_t im=0,om=0,checked=0;
  if(IsWindows()){
    input=CreateFile(u"CONIN$",0xc0000000u,3,NULL,3,0,0);output=CreateFile(u"CONOUT$",0xc0000000u,3,NULL,3,0,0);CHECK(input!=-1 && output!=-1 && GetConsoleMode(input,&im) && GetConsoleMode(output,&om) && FlushConsoleInputBuffer(input));
    for(unsigned i=0;i<2;++i)original[i]=GetStdHandle(-10-i);CHECK(SetStdHandle(-10,input) && SetStdHandle(-11,output));
  }else{CHECK(!openpty(&master,&slave,NULL,NULL,NULL) && !tcgetattr(slave,&before));for(unsigned i=0;i<2;++i){saved[i]=dup(i);CHECK(saved[i]>=0 && dup2(slave,i)==i);}}
  CHECK(record_mode("input.before",0,input) && record_mode("output.before",1,output));
  qaplet_config b=QAPLET_CONFIG_INIT;qaplet_m2_config m=QAPLET_M2_CONFIG_INIT;m.limits.pump_io_bytes=1;qaplet_runtime *r=NULL;CHECK(qaplet_create_m2(&b,&m,&r,NULL)==QAPLET_OK);
  const char *source="import('qaplet:stdio').then(m=>{const input=m.acquireTerminal(0,{raw:true}),output=input.joinTerminal(1,{vt:true});if(input.getSize().columns!==80)throw Error('joined size');input.unref();return new Promise(resolve=>input.watchResize(async s=>{if(s.columns!==100||s.rows!==33)throw Error('latest');input.ref();input.unwatchResize();await output.write(new Uint8Array([65,228,184,173,240,159,153,130,90])).promise;let bytes=[];while(bytes.length<5){let next=await input.read(5-bytes.length).promise;bytes.push(...next.bytes)}if(bytes.join(',')!=='65,228,184,173,90')throw Error('input');input.releaseTerminal();await Promise.all([input.closed,output.closed]);resolve(42)}))})";
  if(!IsWindows()){struct winsize size={.ws_col=80,.ws_row=24};CHECK(!ioctl(slave,TIOCSWINSZ,&size));}
  qaplet_progress p=QAPLET_PROGRESS_INIT;qaplet_drive_result drive=QAPLET_DRIVE_RESULT_INIT;CHECK(qaplet_eval(r,source,strlen(source),&p,NULL)==QAPLET_OK && qaplet_run(r,&drive,NULL)==QAPLET_OK && drive.reason==QAPLET_DRIVE_UNSETTLED_ENTRY);
  CHECK(record_mode("input.modified",0,input) && record_mode("output.modified",1,output));
  qaplet_io_endpoint e=QAPLET_IO_ENDPOINT_INIT;qaplet_stdio_info info=QAPLET_STDIO_INFO_INIT;CHECK(qaplet_stdio_get(r,0,&e,&info,NULL)==QAPLET_OK);
  if(IsWindows()){
    FILE *gate=fopen("resize.ready","wb");CHECK(gate && !fclose(gate));int final=0;
    while(!final){CHECK(WaitForSingleObject(input,2000)==0);struct NtInputRecord event;uint32_t got;CHECK(ReadConsoleInput(input,&event,1,&got) && got==1);if(event.EventType==kNtWindowBufferSizeEvent)final=event.Event.WindowBufferSizeEvent.dwSize.X==100 && event.Event.WindowBufferSizeEvent.dwSize.Y==33;}
  }else{struct winsize size={.ws_col=100,.ws_row=33};CHECK(!ioctl(slave,TIOCSWINSZ,&size));}
  CHECK(qaplet_tty_notify_resize(r,&e,NULL)==QAPLET_OK && qaplet_pump(r,&p,NULL)==QAPLET_OK);
  if(IsWindows()){FILE *gate=fopen("text.ready","wb");CHECK(gate && !fclose(gate));}
  else{const unsigned char text[]={65,228,184,173,90};CHECK(write(master,text,5)==5);}
  qaplet_result result=QAPLET_RESULT_INIT;CHECK(qaplet_run(r,&drive,NULL)==QAPLET_OK && drive.phase==QAPLET_PHASE_DONE && qaplet_get_result(r,&result,NULL)==QAPLET_OK && result.type==QAPLET_VALUE_DOUBLE && result.number==42);
  CHECK(record_mode("input.restored",0,input) && record_mode("output.restored",1,output));
  CHECK(qaplet_close(r,NULL)==QAPLET_OK && qaplet_quiesce(r,5000,&drive,NULL)==QAPLET_OK && drive.reason==QAPLET_DRIVE_QUIESCENT && qaplet_destroy(r,NULL)==QAPLET_OK);
  if(IsWindows()){CHECK(GetConsoleMode(input,&checked) && checked==im && GetConsoleMode(output,&checked) && checked==om && SetStdHandle(-10,original[0]) && SetStdHandle(-11,original[1]) && CloseHandle(input) && CloseHandle(output));}
  else{unsigned char actual[10];const unsigned char expected[]={65,228,184,173,240,159,153,130,90};CHECK(read(master,actual,10)==sizeof(expected) && !memcmp(actual,expected,sizeof(expected)) && !tcgetattr(slave,&after) && same(&before,&after) && !(fcntl(slave,F_GETFL)&O_NONBLOCK));for(unsigned i=0;i<2;++i)CHECK(dup2(saved[i],i)==i && !close(saved[i]));CHECK(!close(slave) && !close(master));}
  puts("M2 TTY joined JS: one exclusive lease owns explicit input/output; input size uses joined output; real resize latest, BMP input and supplementary output; group release joins all IO then restores full shared modes/flags: pass");return 0;
}
static int join_c(void){
  int master=-1,slave=-1,output_fd=-1,output_flags=0,saved[2]={-1,-1};int64_t input=0,output=0,original[2]={0};struct termios before,after;uint32_t im=0,om=0,checked;
  if(IsWindows()){input=CreateFile(u"CONIN$",0xc0000000u,3,NULL,3,0,0);output=CreateFile(u"CONOUT$",0xc0000000u,3,NULL,3,0,0);CHECK(input!=-1 && output!=-1 && GetConsoleMode(input,&im) && GetConsoleMode(output,&om));for(unsigned i=0;i<2;++i)original[i]=GetStdHandle(-10-i);CHECK(SetStdHandle(-10,input) && SetStdHandle(-11,output));}
  else{CHECK(!openpty(&master,&slave,NULL,NULL,NULL) && !tcgetattr(slave,&before));output_fd=open(ptsname(master),O_RDWR|O_NOCTTY);CHECK(output_fd>=0);output_flags=fcntl(output_fd,F_GETFL)|O_APPEND;CHECK(!fcntl(output_fd,F_SETFL,output_flags));for(unsigned i=0;i<2;++i){saved[i]=dup(i);CHECK(saved[i]>=0 && dup2(i ? output_fd : slave,i)==i);}}
  qaplet_config b=QAPLET_CONFIG_INIT;qaplet_m2_config m=QAPLET_M2_CONFIG_INIT;qaplet_runtime *r=NULL,*other=NULL;CHECK(qaplet_create_m2(&b,&m,&r,NULL)==QAPLET_OK && qaplet_create_m2(&b,&m,&other,NULL)==QAPLET_OK);
  qaplet_tty_options raw=QAPLET_TTY_OPTIONS_INIT,vt=QAPLET_TTY_OPTIONS_INIT;raw.flags=QAPLET_TTY_RAW;vt.flags=QAPLET_TTY_VT;qaplet_tty_status status=QAPLET_TTY_STATUS_INIT;qaplet_io_endpoint in=QAPLET_IO_ENDPOINT_INIT,out=QAPLET_IO_ENDPOINT_INIT,refused=QAPLET_IO_ENDPOINT_INIT;
  CHECK(qaplet_tty_acquire(r,0,&raw,&in,&status,NULL)==QAPLET_OK && qaplet_tty_join(other,&in,1,&vt,&refused,&status,NULL)==QAPLET_INVALID_HANDLE && !refused.generation);
  if(!IsWindows()){
    int other_master,other_slave;CHECK(!openpty(&other_master,&other_slave,NULL,NULL,NULL) && dup2(other_slave,1)==1);
    CHECK(qaplet_tty_join(r,&in,1,&vt,&refused,&status,NULL)==QAPLET_UNSUPPORTED && !refused.generation && dup2(output_fd,1)==1 && !close(other_slave) && !close(other_master));
  }
  CHECK(qaplet_tty_join(r,&in,1,&vt,&out,&status,NULL)==QAPLET_OK && status.active);
  CHECK(qaplet_tty_acquire(other,1,&vt,&refused,&status,NULL)==QAPLET_BUSY && !refused.generation);
  CHECK(qaplet_tty_get_size(r,&in,&status,NULL)==QAPLET_OK);
  CHECK(qaplet_io_close(r,&in,NULL)==QAPLET_OK && qaplet_get_tty_status(r,&in,&status,NULL)==QAPLET_OK && status.active && !status.restored);
  qaplet_io_endpoint_status endpoint_status=QAPLET_IO_ENDPOINT_STATUS_INIT;CHECK(qaplet_get_io_endpoint_status(r,&in,&endpoint_status,NULL)==QAPLET_OK && endpoint_status.ended && endpoint_status.physical_pending);
  if(IsWindows())CHECK(GetConsoleMode(input,&checked) && checked!=im);else CHECK(!tcgetattr(slave,&after) && !same(&before,&after));
  CHECK(qaplet_tty_release(r,&out,&status,NULL)==QAPLET_OK && status.restored && qaplet_get_tty_status(r,&in,&status,NULL)==QAPLET_OK && status.restored && !status.active);
  CHECK(!finish_tty(r) && !finish_tty(other));
  if(IsWindows())CHECK(GetConsoleMode(input,&checked) && checked==im && GetConsoleMode(output,&checked) && checked==om && SetStdHandle(-10,original[0]) && SetStdHandle(-11,original[1]) && CloseHandle(input) && CloseHandle(output));
  else{CHECK(!tcgetattr(slave,&after) && same(&before,&after) && !(fcntl(slave,F_GETFL)&O_NONBLOCK) && fcntl(output_fd,F_GETFL)==output_flags);for(unsigned i=0;i<2;++i)CHECK(dup2(saved[i],i)==i && !close(saved[i]));CHECK(!close(slave) && !close(master) && !close(output_fd));}
  puts("M2 TTY joined C: explicit same-terminal group; cross Runtime/wrong terminal rejected; second independent lease conflicts; closing one direction retains shared state and closed barrier; group release restores once in reverse order: pass");return 0;
}
static int helper_limit(void){
  CHECK(IsWindows());int64_t saved=GetStdHandle(-11),console=CreateFile(u"CONOUT$",0xc0000000u,3,NULL,3,0,0);uint32_t mode,after;CHECK(console!=-1 && GetConsoleMode(console,&mode) && SetStdHandle(-11,console));
  qaplet_config b=QAPLET_CONFIG_INIT;qaplet_m2_config m=QAPLET_M2_CONFIG_INIT;m.limits.helper_threads=1;qaplet_runtime *r=NULL,*other=NULL;CHECK(qaplet_create_m2(&b,&m,&r,NULL)==QAPLET_OK && qaplet_create_m2(&b,&m,&other,NULL)==QAPLET_OK);
  qaplet_tty_options options=QAPLET_TTY_OPTIONS_INIT;options.flags=QAPLET_TTY_VT;qaplet_io_endpoint e=QAPLET_IO_ENDPOINT_INIT,refused=QAPLET_IO_ENDPOINT_INIT;qaplet_tty_status status=QAPLET_TTY_STATUS_INIT;CHECK(qaplet_tty_acquire(r,1,&options,&e,&status,NULL)==QAPLET_OK);
  qaplet_io_snapshot io=QAPLET_IO_SNAPSHOT_INIT;CHECK(qaplet_tty_acquire(other,1,&options,&refused,&status,NULL)==QAPLET_BUSY && !refused.generation && qaplet_get_io_snapshot(other,&io,NULL)==QAPLET_OK && !io.endpoints && !io.helper_threads && !io.operations);
  qaplet_io_operation op=QAPLET_IO_OPERATION_INIT;CHECK(qaplet_io_write(r,&e,"A",1,&op,NULL)==QAPLET_RESOURCE_LIMIT && !op.generation && qaplet_get_io_snapshot(r,&io,NULL)==QAPLET_OK && io.helper_threads==1 && io.helper_stack_bytes==1048576 && !io.operations && !io.data_bytes && !io.accepted);
  CHECK(qaplet_tty_release(r,&e,&status,NULL)==QAPLET_OK && status.restored && !finish_tty(r) && !finish_tty(other));
  CHECK(GetConsoleMode(console,&after) && after==mode && SetStdHandle(-11,saved) && CloseHandle(console));puts("M2 TTY helper limit: Console join control consumes one existing helper and requested stack; physical IO refuses atomically at quota1; conflicting Runtime starts no control helper; full native cleanup: pass");return 0;
}
int sdk_tty(int argc,char **argv){(void)argc;if(!strcmp(argv[1],"--m2-tty-helper-limit"))return helper_limit();if(!strcmp(argv[1],"--m2-tty-join-c"))return join_c();if(!strcmp(argv[1],"--m2-tty-join-js"))return join_js();if(!strcmp(argv[1],"--m2-tty-identities"))return identities();if(!strcmp(argv[1],"--m2-tty-bytes"))return bytes();if(!strcmp(argv[1],"--m2-tty-validation"))return validation();if(!strcmp(argv[1],"--m2-tty-resize-signal"))return resize_signal();if(!strcmp(argv[1],"--m2-tty-lease"))return lease();if(!strcmp(argv[1],"--m2-tty-resize"))return IsWindows() ? console_resize(0) : resize();if(!strcmp(argv[1],"--m2-tty-js"))return IsWindows() ? console_resize(1) : js();if(!strcmp(argv[1],"--m2-tty-output"))return output();if(!strcmp(argv[1],"--m2-tty-input"))return input();if(!strcmp(argv[1],"--m2-tty-stop-close"))return stop_close();return 2;}
#ifdef QAPLET_TTY_STANDALONE
int main(int argc,char **argv){return sdk_tty(argc,argv);}
#endif
