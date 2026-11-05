/* SPDX-License-Identifier: MIT */
/* Public SDK and real Host standard stream boundary. */
#define _COSMO_SOURCE 1
#include "qaplet.h"
#include <stdio.h>
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <stdlib.h>
#include <sched.h>
#include <time.h>
#include <termios.h>
#include "libc/nt/console.h"
#include <unistd.h>
#include "libc/dce.h"
#include "libc/nt/runtime.h"
#include "libc/nt/ipc.h"
extern int64_t CreateFile(const char16_t *,uint32_t,uint32_t,void *,uint32_t,uint32_t,int64_t);
extern int __mkntpath(const char *,char16_t *);
extern bool32 GetHandleInformation(int64_t,uint32_t *);
#define CHECK(c) do { if(!(c)){fprintf(stderr,"stdio contract line %d: %s\n",__LINE__,#c);return 1;} } while(0)
static int classify(void) {
  qaplet_config b=QAPLET_CONFIG_INIT;qaplet_m2_config m=QAPLET_M2_CONFIG_INIT;qaplet_runtime *r=NULL;
  CHECK(qaplet_create_m2(&b,&m,&r,NULL)==QAPLET_OK);
  qaplet_io_snapshot snapshot=QAPLET_IO_SNAPSHOT_INIT;
  CHECK(qaplet_get_io_snapshot(r,&snapshot,NULL)==QAPLET_OK && !snapshot.endpoints && !snapshot.operations);
  qaplet_stdio_info info=QAPLET_STDIO_INFO_INIT;
  if(IsWindows()){
    int64_t saved=GetStdHandle(-10),in=0,out=0;
    CHECK(CreatePipe(&in,&out,NULL,4096) && SetStdHandle(-10,in));
    CHECK(qaplet_stdio_inspect(r,0,&info,NULL)==QAPLET_OK && info.kind==QAPLET_STDIO_PIPE && !info.supported && !info.attached);
    CHECK(SetStdHandle(-10,0));
    CHECK(qaplet_stdio_inspect(r,0,&info,NULL)==QAPLET_OK && info.kind==QAPLET_STDIO_INVALID && !info.supported);
    CHECK(SetStdHandle(-10,saved) && CloseHandle(in) && CloseHandle(out));
  }else{
    int saved=dup(0),p[2];CHECK(saved>=0 && pipe(p)==0 && dup2(p[0],0)==0);
    CHECK(qaplet_stdio_inspect(r,0,&info,NULL)==QAPLET_OK && info.kind==QAPLET_STDIO_PIPE && !info.supported && !info.attached);
    FILE *f=tmpfile();CHECK(f && dup2(fileno(f),0)==0);
    CHECK(qaplet_stdio_inspect(r,0,&info,NULL)==QAPLET_OK && info.kind==QAPLET_STDIO_FILE && info.supported);
    CHECK(close(0)==0);
    CHECK(qaplet_stdio_inspect(r,0,&info,NULL)==QAPLET_OK && info.kind==QAPLET_STDIO_INVALID && !info.supported && info.error==EBADF);
    CHECK(dup2(saved,0)==0 && !close(saved) && !close(p[0]) && !close(p[1]) && !fclose(f));
  }
  CHECK(qaplet_get_io_snapshot(r,&snapshot,NULL)==QAPLET_OK && !snapshot.endpoints && !snapshot.operations);
  CHECK(qaplet_close(r,NULL)==QAPLET_OK && qaplet_destroy(r,NULL)==QAPLET_OK);
  puts("M2 stdio inspect: default untouched; real stdin pipe/file/invalid classification; separate attach support: pass");return 0;
}
static int file_bytes(void) {
  const unsigned char payload[]={0,1,127,128,255,10};unsigned char bytes[32]={0};
  char path[]="/tmp/qaplet-stdio-XXXXXX";int fd=mkstemp(path);CHECK(fd>=0 && write(fd,payload,sizeof(payload))==sizeof(payload) && lseek(fd,0,SEEK_SET)==0);
  int saved=-1;int64_t saved_handle=0,handle=0;
  if(IsWindows()){
    char16_t native[4096];CHECK(__mkntpath(path,native)>=0);
    handle=CreateFile(native,0xc0000000u,7,NULL,3,0x80,0);CHECK(handle!=-1);
    saved_handle=GetStdHandle(-10);CHECK(SetStdHandle(-10,handle));
  }else{saved=dup(0);CHECK(saved>=0 && dup2(fd,0)==0);}
  qaplet_config b=QAPLET_CONFIG_INIT;qaplet_m2_config m=QAPLET_M2_CONFIG_INIT;qaplet_runtime *r=NULL;
  CHECK(qaplet_create_m2(&b,&m,&r,NULL)==QAPLET_OK);
  qaplet_stdio_info info=QAPLET_STDIO_INFO_INIT;qaplet_io_endpoint endpoint=QAPLET_IO_ENDPOINT_INIT;
  CHECK(qaplet_stdio_get(r,0,&endpoint,&info,NULL)==QAPLET_UNSUPPORTED && !endpoint.generation);
  CHECK(qaplet_stdio_attach(r,0,&endpoint,&info,NULL)==QAPLET_OK && info.kind==QAPLET_STDIO_FILE && info.attached && info.supported);
  qaplet_io_endpoint again=QAPLET_IO_ENDPOINT_INIT;CHECK(qaplet_stdio_attach(r,0,&again,&info,NULL)==QAPLET_BUSY && !again.generation);
  qaplet_io_endpoint_status es=QAPLET_IO_ENDPOINT_STATUS_INIT;
  CHECK(qaplet_get_io_endpoint_status(r,&endpoint,&es,NULL)==QAPLET_OK && es.referenced && !es.writing && !es.ended);
  qaplet_drive_result drive=QAPLET_DRIVE_RESULT_INIT;
  CHECK(qaplet_run(r,&drive,NULL)==QAPLET_OK && drive.reason==QAPLET_DRIVE_IDLE);
  qaplet_io_operation op=QAPLET_IO_OPERATION_INIT,busy=QAPLET_IO_OPERATION_INIT;
  CHECK(qaplet_io_read(r,&endpoint,sizeof(bytes),&op,NULL)==QAPLET_OK);
  CHECK(qaplet_io_read(r,&endpoint,1,&busy,NULL)==QAPLET_BUSY && !busy.generation);
  CHECK(qaplet_run(r,&drive,NULL)==QAPLET_OK && drive.reason==QAPLET_DRIVE_IDLE);
  qaplet_io_result result=QAPLET_IO_RESULT_INIT;result.buffer=bytes;result.capacity=sizeof(bytes);
  CHECK(qaplet_io_copy_result(r,&op,&result,NULL)==QAPLET_OK && result.code==QAPLET_OK && result.length==sizeof(payload) && !result.eof && !memcmp(bytes,payload,sizeof(payload)));
  CHECK(qaplet_io_release(r,&op,NULL)==QAPLET_OK);
  CHECK(qaplet_io_read(r,&endpoint,1,&op,NULL)==QAPLET_OK && qaplet_run(r,&drive,NULL)==QAPLET_OK);
  CHECK(qaplet_io_copy_result(r,&op,&result,NULL)==QAPLET_OK && result.eof && !result.length);
  CHECK(qaplet_io_release(r,&op,NULL)==QAPLET_OK && qaplet_stdio_detach(r,0,NULL)==QAPLET_OK);
  CHECK(qaplet_get_io_endpoint_status(r,&endpoint,&es,NULL)==QAPLET_INVALID_HANDLE);
  CHECK(qaplet_close(r,NULL)==QAPLET_OK && qaplet_quiesce(r,5000,&drive,NULL)==QAPLET_OK && drive.reason==QAPLET_DRIVE_QUIESCENT);
  qaplet_m2_snapshot final=QAPLET_M2_SNAPSHOT_INIT;qaplet_io_snapshot ios=QAPLET_IO_SNAPSHOT_INIT;
  CHECK(qaplet_get_m2_snapshot(r,&final,NULL)==QAPLET_OK && !final.native_handles && !final.reactor_registrations && !final.native_uncertain_handles);
  CHECK(qaplet_get_io_snapshot(r,&ios,NULL)==QAPLET_OK && !ios.operations && !ios.data_bytes && !ios.helper_threads && !ios.endpoints);
  CHECK(qaplet_destroy(r,NULL)==QAPLET_OK);
  if(IsWindows()){uint32_t n=99;CHECK(ReadFile(handle,bytes,1,&n,NULL) && n==0);CHECK(SetStdHandle(-10,saved_handle) && CloseHandle(handle));}
  else{CHECK(fcntl(0,F_GETFD)>=0 && read(0,bytes,1)==0 && dup2(saved,0)==0 && !close(saved));}
  CHECK(!close(fd) && !unlink(path));
  puts("M2 stdio file: explicit borrowed stdin; binary short read/EOF/BUSY; idle versus operation ref; Host original survives; quiescent cleanup: pass");return 0;
}
static int pipe_bytes(void) {
  const unsigned char payload[]={0,1,127,128,255,10};unsigned char bytes[32]={0};
  qaplet_config b=QAPLET_CONFIG_INIT;qaplet_m2_config m=QAPLET_M2_CONFIG_INIT;qaplet_runtime *r=NULL;
  CHECK(qaplet_create_m2(&b,&m,&r,NULL)==QAPLET_OK);
  int saved=-1,p[2]={-1,-1},flags=-1;int64_t saved_handle=0,in=0,out=0;
  if(IsWindows()){
    saved_handle=GetStdHandle(-10);CHECK(CreatePipe(&in,&out,NULL,4096) && SetStdHandle(-10,in));
  }else{saved=dup(0);CHECK(saved>=0 && !pipe(p) && dup2(p[0],0)==0);flags=fcntl(0,F_GETFL);CHECK(flags>=0);}
  qaplet_stdio_info info=QAPLET_STDIO_INFO_INIT;qaplet_io_endpoint endpoint=QAPLET_IO_ENDPOINT_INIT;
  qaplet_m2_snapshot before=QAPLET_M2_SNAPSHOT_INIT,after=QAPLET_M2_SNAPSHOT_INIT;
  CHECK(qaplet_get_m2_snapshot(r,&before,NULL)==QAPLET_OK);
  CHECK(qaplet_stdio_attach(r,0,&endpoint,&info,NULL)==QAPLET_UNSUPPORTED && info.kind==QAPLET_STDIO_PIPE && !info.supported && !endpoint.generation);
  CHECK(qaplet_get_m2_snapshot(r,&after,NULL)==QAPLET_OK && before.native_current_bytes==after.native_current_bytes && before.native_handles==after.native_handles && before.reactor_registrations==after.reactor_registrations);
  if(IsWindows()){
    CHECK(SetStdHandle(-10,saved_handle) && CloseHandle(in) && CloseHandle(out));
    char text[128];char16_t name[128];snprintf(text,sizeof(text),"\\\\.\\pipe\\qaplet-stdio-%d",getpid());
    for(size_t i=0;i<=strlen(text);++i)name[i]=(unsigned char)text[i];
    in=CreateNamedPipe(name,1|0x40000000u,8,1,4096,4096,0,NULL);CHECK(in!=-1);
    out=CreateFile(name,0x40000000u,0,NULL,3,0,0);CHECK(out!=-1);
    struct NtOverlapped connected={0};CHECK(ConnectNamedPipe(in,&connected) || GetLastError()==535);
    CHECK(SetStdHandle(-10,in));
  }else{CHECK(fcntl(0,F_GETFL)==flags && fcntl(0,F_SETFL,flags|O_NONBLOCK)==0);flags=fcntl(0,F_GETFL);}
  CHECK(qaplet_stdio_attach(r,0,&endpoint,&info,NULL)==QAPLET_OK && info.kind==QAPLET_STDIO_PIPE && info.supported && info.attached);
  qaplet_io_operation op=QAPLET_IO_OPERATION_INIT,busy=QAPLET_IO_OPERATION_INIT;qaplet_drive_result drive=QAPLET_DRIVE_RESULT_INIT;
  CHECK(qaplet_io_read(r,&endpoint,sizeof(bytes),&op,NULL)==QAPLET_OK);
  CHECK(qaplet_io_read(r,&endpoint,1,&busy,NULL)==QAPLET_BUSY);
  CHECK(qaplet_io_cancel(r,&op,NULL)==QAPLET_OK);
  CHECK(qaplet_run(r,&drive,NULL)==QAPLET_OK && drive.reason==QAPLET_DRIVE_IDLE);
  qaplet_io_result result=QAPLET_IO_RESULT_INIT;result.buffer=bytes;result.capacity=sizeof(bytes);
  CHECK(qaplet_io_copy_result(r,&op,&result,NULL)==QAPLET_OK && result.code==QAPLET_CANCELLED && !result.length && !result.eof);
  CHECK(qaplet_io_release(r,&op,NULL)==QAPLET_OK);
  if(IsWindows()){uint32_t n=0;CHECK(WriteFile(out,payload,sizeof(payload),&n,NULL) && n==sizeof(payload));}
  else CHECK(write(p[1],payload,sizeof(payload))==sizeof(payload));
  CHECK(qaplet_io_read(r,&endpoint,sizeof(bytes),&op,NULL)==QAPLET_OK && qaplet_run(r,&drive,NULL)==QAPLET_OK);
  CHECK(qaplet_io_copy_result(r,&op,&result,NULL)==QAPLET_OK && result.code==QAPLET_OK && result.length==sizeof(payload) && !result.eof && !memcmp(bytes,payload,sizeof(payload)));
  CHECK(qaplet_io_release(r,&op,NULL)==QAPLET_OK);
  if(IsWindows()){CHECK(CloseHandle(out));out=0;}else{CHECK(close(p[1])==0);p[1]=-1;}
  CHECK(qaplet_io_read(r,&endpoint,1,&op,NULL)==QAPLET_OK && qaplet_run(r,&drive,NULL)==QAPLET_OK);
  CHECK(qaplet_io_copy_result(r,&op,&result,NULL)==QAPLET_OK && result.eof && !result.length);
  CHECK(qaplet_io_release(r,&op,NULL)==QAPLET_OK);
  CHECK(qaplet_close(r,NULL)==QAPLET_OK && qaplet_quiesce(r,5000,&drive,NULL)==QAPLET_OK && drive.reason==QAPLET_DRIVE_QUIESCENT && qaplet_destroy(r,NULL)==QAPLET_OK);
  if(IsWindows()){uint32_t native_flags=0;CHECK(GetHandleInformation(in,&native_flags) && SetStdHandle(-10,saved_handle) && CloseHandle(in));}
  else CHECK(fcntl(0,F_GETFL)==flags && dup2(saved,0)==0 && !close(saved) && !close(p[0]));
  puts("M2 stdio pipe: real stdin redirection; unsafe blocking borrow refused without side effects; safe binary/NUL/short read/EOF/cancel/BUSY; shared flags and Host handle preserved: pass");return 0;
}
typedef struct {char path[64];int fd,saved;int64_t handle,saved_handle;unsigned stream;} file_fixture;
static int install_file(file_fixture *f,unsigned stream){
  *f=(file_fixture){.fd=-1,.saved=-1,.stream=stream};strcpy(f->path,"/tmp/qaplet-stdio-XXXXXX");
  f->fd=mkstemp(f->path);CHECK(f->fd>=0);
  if(IsWindows()){
    char16_t native[4096];CHECK(__mkntpath(f->path,native)>=0);
    f->handle=CreateFile(native,0xc0000000u,7,NULL,3,0x80,0);CHECK(f->handle!=-1);
    f->saved_handle=GetStdHandle(-10-(int)stream);CHECK(SetStdHandle(-10-(int)stream,f->handle));
  }else{f->saved=dup(stream);CHECK(f->saved>=0 && dup2(f->fd,stream)==stream);}
  return 0;
}
static int restore_file(file_fixture *f){
  if(IsWindows())CHECK(SetStdHandle(-10-(int)f->stream,f->saved_handle) && CloseHandle(f->handle));
  else CHECK(dup2(f->saved,f->stream)==f->stream && !close(f->saved));
  CHECK(!close(f->fd) && !unlink(f->path));return 0;
}
static int output_limits(void){
  file_fixture f[3];for(unsigned i=0;i<3;++i)CHECK(!install_file(&f[i],i));
  qaplet_config b=QAPLET_CONFIG_INIT;qaplet_m2_config m=QAPLET_M2_CONFIG_INIT;qaplet_runtime *r=NULL;
  m.limits.operations=2;m.limits.operation_data_bytes=12;m.limits.io_block_bytes=6;m.limits.pump_io_bytes=3;
  m.limits.work_input_bytes=1;m.limits.work_output_bytes=1;
  CHECK(qaplet_create_m2(&b,&m,&r,NULL)==QAPLET_OK);
  qaplet_io_endpoint ep[3];qaplet_stdio_info info=QAPLET_STDIO_INFO_INIT;
  for(unsigned i=0;i<3;++i){ep[i]=(qaplet_io_endpoint)QAPLET_IO_ENDPOINT_INIT;CHECK(qaplet_stdio_attach(r,i,&ep[i],&info,NULL)==QAPLET_OK);}
  unsigned char a[]={0,1,127,128,255,10},z[]={9,8,0,7,255,6};
  qaplet_io_operation op[2]={QAPLET_IO_OPERATION_INIT,QAPLET_IO_OPERATION_INIT},reject=QAPLET_IO_OPERATION_INIT;
  CHECK(qaplet_io_write(r,&ep[1],a,6,&op[0],NULL)==QAPLET_OK && qaplet_io_write(r,&ep[2],z,6,&op[1],NULL)==QAPLET_OK);
  memset(a,0xa5,6);memset(z,0x5a,6);
  CHECK(qaplet_io_read(r,&ep[0],1,&reject,NULL)==QAPLET_RESOURCE_LIMIT && !reject.generation);
  qaplet_io_snapshot io=QAPLET_IO_SNAPSHOT_INIT;CHECK(qaplet_get_io_snapshot(r,&io,NULL)==QAPLET_OK && io.shared_operations==2 && io.shared_data_bytes==12 && io.helper_threads==2);
  qaplet_drive_result drive=QAPLET_DRIVE_RESULT_INIT;CHECK(qaplet_run(r,&drive,NULL)==QAPLET_OK && drive.reason==QAPLET_DRIVE_IDLE);
  for(unsigned i=0;i<2;++i){qaplet_io_result result=QAPLET_IO_RESULT_INIT;CHECK(qaplet_io_copy_result(r,&op[i],&result,NULL)==QAPLET_OK && result.code==QAPLET_OK && result.length==6);}
  CHECK(qaplet_get_io_snapshot(r,&io,NULL)==QAPLET_OK && io.shared_operations==2 && io.shared_data_bytes==12 && io.retained_results==2);
  CHECK(qaplet_io_read(r,&ep[0],1,&reject,NULL)==QAPLET_RESOURCE_LIMIT);
  CHECK(qaplet_io_release(r,&op[0],NULL)==QAPLET_OK && qaplet_io_release(r,&op[1],NULL)==QAPLET_OK);
  CHECK(qaplet_close(r,NULL)==QAPLET_OK && qaplet_quiesce(r,5000,&drive,NULL)==QAPLET_OK && drive.reason==QAPLET_DRIVE_QUIESCENT);
  CHECK(qaplet_get_io_snapshot(r,&io,NULL)==QAPLET_OK && !io.shared_operations && !io.shared_data_bytes && !io.helper_threads && !io.operations && !io.endpoints);
  CHECK(qaplet_destroy(r,NULL)==QAPLET_OK);
  const unsigned char expected[2][6]={{0,1,127,128,255,10},{9,8,0,7,255,6}};unsigned char actual[6];
  for(unsigned i=1;i<3;++i)CHECK(lseek(f[i].fd,0,SEEK_SET)==0 && read(f[i].fd,actual,6)==6 && !memcmp(actual,expected[i-1],6));
  for(unsigned i=0;i<3;++i)CHECK(!restore_file(&f[i]));
  puts("M2 stdio output limits: independent stdout/stderr copied binary writes; bounded 3-byte advances; shared count/data include retained results; refusal atomic; physical cleanup returns capacity: pass");return 0;
}
static int js_defaults(void){
  qaplet_config b=QAPLET_CONFIG_INIT;qaplet_m2_config m=QAPLET_M2_CONFIG_INIT;qaplet_runtime *r=NULL;
  CHECK(qaplet_create_m2(&b,&m,&r,NULL)==QAPLET_OK);
  const char *source="import('qaplet:stdio').then(m=>m.stdin===null&&m.stdout===null&&m.stderr===null)";
  qaplet_progress progress=QAPLET_PROGRESS_INIT;qaplet_code code=qaplet_eval(r,source,strlen(source),&progress,NULL);
  if(code)fprintf(stderr,"stdio JS default import code=%d phase=%u\n",code,progress.phase);
  CHECK(code==QAPLET_OK);
  qaplet_drive_result drive=QAPLET_DRIVE_RESULT_INIT;CHECK(qaplet_run(r,&drive,NULL)==QAPLET_OK && drive.phase==QAPLET_PHASE_DONE);
  qaplet_result result=QAPLET_RESULT_INIT;CHECK(qaplet_get_result(r,&result,NULL)==QAPLET_OK && result.type==QAPLET_VALUE_BOOL && result.boolean);
  CHECK(qaplet_close(r,NULL)==QAPLET_OK && qaplet_quiesce(r,5000,&drive,NULL)==QAPLET_OK && drive.reason==QAPLET_DRIVE_QUIESCENT && qaplet_destroy(r,NULL)==QAPLET_OK);
  puts("M2 stdio JS default: declared module; three null exports until explicit Host attachment; no implicit native takeover: pass");return 0;
}
static int js_bytes(void){
  file_fixture f[3];for(unsigned i=0;i<3;++i)CHECK(!install_file(&f[i],i));
  const unsigned char payload[]={0,1,127,128,255,10};CHECK(write(f[0].fd,payload,6)==6 && lseek(f[0].fd,0,SEEK_SET)==0);
  qaplet_config b=QAPLET_CONFIG_INIT;qaplet_m2_config m=QAPLET_M2_CONFIG_INIT;qaplet_runtime *r=NULL;b.limits.execution_timeout_ms=2000;CHECK(qaplet_create_m2(&b,&m,&r,NULL)==QAPLET_OK);
  for(unsigned i=0;i<3;++i){qaplet_io_endpoint ep=QAPLET_IO_ENDPOINT_INIT;qaplet_stdio_info info=QAPLET_STDIO_INFO_INIT;CHECK(qaplet_stdio_attach(r,i,&ep,&info,NULL)==QAPLET_OK);}
  const char *source="import('qaplet:stdio').then(async m=>{"
    "if(m.stdin.kind!=='file'||!m.stdin.hasRef())throw Error('kind/ref');"
    "let invalid=0;for(const n of [-1,0,NaN,Infinity,1.5,'3']){try{m.stdin.read(n)}catch(e){if(e.code===1)invalid++}}if(invalid!==6)throw Error('strict read');"
    "try{m.stdout.write('bytes');throw Error('accepted text')}catch(e){if(e.code!==8)throw e}"
    "let cancelled=m.stdin.read(32);cancelled.cancel();cancelled.cancel();"
    "await cancelled.promise.then(()=>{throw Error('cancel success')},e=>{if(e.code!==28||e.bytesWritten!==0)throw e});"
    "const task=m.stdin.read(32);let busy=false;try{m.stdin.read(1)}catch(e){busy=e.code===4}if(!busy)throw Error('busy');"
    "const first=await task.promise;if(first.eof||Array.from(first.bytes).join(',')!=='0,1,127,128,255,10')throw Error('bytes');"
    "let input=new Uint8Array([0,255,1]);const write=m.stdout.write(input);input.fill(4);"
    "if((await write.promise).bytesWritten!==3)throw Error('written');m.stdout.finish();await m.stdout.closed;"
    "const err=m.stderr.write(new Uint8Array([9,0,8]));m.stderr.finish();if((await err.promise).bytesWritten!==3)throw Error('stderr');await m.stderr.closed;"
    "const last=await m.stdin.read(32).promise;if(!last.eof||last.bytes.length)throw Error('eof');await m.stdin.closed;"
    "m.stdout.finish();m.stderr.close();await Promise.all([m.stdout.closed,m.stderr.closed]);return 6})";
  qaplet_progress progress=QAPLET_PROGRESS_INIT;qaplet_code code=qaplet_eval(r,source,strlen(source),&progress,NULL);
  CHECK(code==QAPLET_OK && progress.phase==QAPLET_PHASE_PENDING);
  qaplet_drive_result drive=QAPLET_DRIVE_RESULT_INIT;qaplet_diagnostic diagnostic=QAPLET_DIAGNOSTIC_INIT;char error[512];diagnostic.message=error;diagnostic.capacity=sizeof(error);
  code=qaplet_run(r,&drive,&diagnostic);if(code)fprintf(stderr,"stdio JS bytes code=%d domain? %.*s\n",code,(int)diagnostic.length,error);
  CHECK(code==QAPLET_OK && drive.phase==QAPLET_PHASE_DONE);
  qaplet_result result=QAPLET_RESULT_INIT;CHECK(qaplet_get_result(r,&result,NULL)==QAPLET_OK && result.type==QAPLET_VALUE_DOUBLE && result.number==6);
  CHECK(qaplet_close(r,NULL)==QAPLET_OK && qaplet_quiesce(r,5000,&drive,NULL)==QAPLET_OK && drive.reason==QAPLET_DRIVE_QUIESCENT && qaplet_destroy(r,NULL)==QAPLET_OK);
  unsigned char output[3];const unsigned char expected[2][3]={{0,255,1},{9,0,8}};
  for(unsigned i=1;i<3;++i)CHECK(lseek(f[i].fd,0,SEEK_SET)==0 && read(f[i].fd,output,3)==3 && !memcmp(output,expected[i-1],3));
  for(unsigned i=0;i<3;++i)CHECK(!restore_file(&f[i]));
  puts("M2 stdio JS bytes: explicit Host streams; strict input/BUSY/cancel; Promise byte/NUL/EOF and independent copied writes; closed promises; standalone IO keeps awaited entry alive without timers: pass");return 0;
}
static int terminal_kind(void){
  int saved=-1,master=-1,slave=-1;int64_t original=0,console=0;struct termios before={0},after={0};uint32_t mode=0,mode_after=0;
  if(IsWindows()){
    original=GetStdHandle(-10);
    /* ConPTY existence does not establish the APE startup's current stdin.
     * Install the owned Console fixture as the actual native standard handle,
     * then both the Host and product query GetStdHandle(-10), not CONIN$. */
    console=CreateFile(u"CONIN$",0xc0000000u,3,NULL,3,0,0);CHECK(console!=-1 && SetStdHandle(-10,console));
    CHECK(GetConsoleMode(GetStdHandle(-10),&mode));
  }
  else{master=posix_openpt(O_RDWR|O_NOCTTY);CHECK(master>=0 && !grantpt(master) && !unlockpt(master));slave=open(ptsname(master),O_RDWR|O_NOCTTY);saved=dup(0);CHECK(slave>=0 && saved>=0 && dup2(slave,0)==0 && !tcgetattr(0,&before));}
  qaplet_config b=QAPLET_CONFIG_INIT;qaplet_m2_config m=QAPLET_M2_CONFIG_INIT;qaplet_runtime *r=NULL;CHECK(qaplet_create_m2(&b,&m,&r,NULL)==QAPLET_OK);
  qaplet_stdio_info info=QAPLET_STDIO_INFO_INIT;qaplet_io_endpoint ep=QAPLET_IO_ENDPOINT_INIT;
  CHECK(qaplet_stdio_inspect(r,0,&info,NULL)==QAPLET_OK && info.kind==(IsWindows() ? QAPLET_STDIO_CONSOLE : QAPLET_STDIO_TTY) && !info.supported);
  CHECK(qaplet_stdio_attach(r,0,&ep,&info,NULL)==QAPLET_UNSUPPORTED && !ep.generation);
  CHECK(qaplet_close(r,NULL)==QAPLET_OK && qaplet_destroy(r,NULL)==QAPLET_OK);
  if(IsWindows())CHECK(GetConsoleMode(GetStdHandle(-10),&mode_after) && mode_after==mode && SetStdHandle(-10,original) && CloseHandle(console));
  else CHECK(!tcgetattr(0,&after) && !memcmp(&before,&after,sizeof(before)) && dup2(saved,0)==0 && !close(saved) && !close(slave) && !close(master));
  puts("M2 stdio terminal kind: real stdin TTY/Console classified; unsupported byte attachment refuses without changing terminal mode or closing original: pass");return 0;
}
static int pipe_backpressure(void){
  unsigned char input[65536],output[4096];for(size_t i=0;i<sizeof(input);++i)input[i]=(unsigned char)(i%251);
  size_t prefix=0;
  int saved=-1,p[2]={-1,-1},flags=-1;int64_t saved_handle=0,handle=0,peer=0;
  if(IsWindows()){
    saved_handle=GetStdHandle(-11);char name[128];char16_t native[128];snprintf(name,sizeof(name),"\\\\.\\pipe\\qaplet-stdio-write-%d",getpid());for(size_t i=0;i<=strlen(name);++i)native[i]=(unsigned char)name[i];
    handle=CreateNamedPipe(native,3|0x40000000u,8,1,4096,4096,0,NULL);CHECK(handle!=-1);
    peer=CreateFile(native,0x80000000u,0,NULL,3,0,0);CHECK(peer!=-1);struct NtOverlapped connected={0};CHECK(ConnectNamedPipe(handle,&connected)||GetLastError()==535);CHECK(SetStdHandle(-11,handle));
  }else{CHECK(!pipe(p));flags=fcntl(p[1],F_GETFL);CHECK(flags>=0 && !fcntl(p[1],F_SETFL,flags|O_NONBLOCK) && !fcntl(p[0],F_SETFL,O_NONBLOCK));saved=dup(1);CHECK(saved>=0 && dup2(p[1],1)==1);flags=fcntl(1,F_GETFL);
    memset(output,0xa5,sizeof(output));ssize_t n;while(prefix<16*1024*1024 && (n=write(p[1],output,sizeof(output)))>0)prefix+=n;
    CHECK(prefix>0 && prefix<16*1024*1024 && (errno==EAGAIN || errno==EWOULDBLOCK));
  }
  qaplet_config b=QAPLET_CONFIG_INIT;qaplet_m2_config m=QAPLET_M2_CONFIG_INIT;qaplet_runtime *r=NULL;CHECK(qaplet_create_m2(&b,&m,&r,NULL)==QAPLET_OK);
  qaplet_stdio_info info=QAPLET_STDIO_INFO_INIT;qaplet_io_endpoint ep=QAPLET_IO_ENDPOINT_INIT;CHECK(qaplet_stdio_attach(r,1,&ep,&info,NULL)==QAPLET_OK && info.kind==QAPLET_STDIO_PIPE);
  qaplet_io_operation op=QAPLET_IO_OPERATION_INIT,busy=QAPLET_IO_OPERATION_INIT;CHECK(qaplet_io_write(r,&ep,input,sizeof(input),&op,NULL)==QAPLET_OK);
  qaplet_io_status status=QAPLET_IO_STATUS_INIT;qaplet_progress progress=QAPLET_PROGRESS_INIT;
  for(unsigned i=0;i<4;++i)CHECK(qaplet_pump(r,&progress,NULL)==QAPLET_OK);
  CHECK(qaplet_get_io_status(r,&op,&status,NULL)==QAPLET_OK && status.state==QAPLET_IO_PENDING && status.length<sizeof(input));
  CHECK(qaplet_io_write(r,&ep,input,1,&busy,NULL)==QAPLET_BUSY);
  qaplet_io_snapshot io=QAPLET_IO_SNAPSHOT_INIT;CHECK(qaplet_get_io_snapshot(r,&io,NULL)==QAPLET_OK && io.operations==1 && io.data_bytes==sizeof(input));
  struct timespec clock;clock_gettime(CLOCK_MONOTONIC,&clock);double until=clock.tv_sec+clock.tv_nsec/1e9+5;size_t seen=0;
  while(seen<prefix+sizeof(input) || status.state==QAPLET_IO_PENDING || status.pending_handoff){
    CHECK(qaplet_pump(r,&progress,NULL)==QAPLET_OK && qaplet_get_io_status(r,&op,&status,NULL)==QAPLET_OK);
    ssize_t got=-1;
    if(IsWindows()){uint32_t available=0,n=0;CHECK(PeekNamedPipe(peer,NULL,0,NULL,&available,NULL));if(available){CHECK(ReadFile(peer,output,available>sizeof(output)?sizeof(output):available,&n,NULL));got=n;}}
    else got=read(p[0],output,sizeof(output));
    if(got>0){CHECK(seen+(size_t)got<=prefix+sizeof(input));for(ssize_t i=0;i<got;++i)CHECK(output[i]==(seen+(size_t)i<prefix ? 0xa5 : input[seen+i-prefix]));seen+=got;}
    clock_gettime(CLOCK_MONOTONIC,&clock);CHECK(clock.tv_sec+clock.tv_nsec/1e9<until);sched_yield();
  }
  qaplet_io_result value=QAPLET_IO_RESULT_INIT;CHECK(qaplet_io_copy_result(r,&op,&value,NULL)==QAPLET_OK && value.code==QAPLET_OK && value.length==sizeof(input) && seen==prefix+sizeof(input));
  CHECK(qaplet_io_release(r,&op,NULL)==QAPLET_OK && qaplet_io_finish(r,&ep,NULL)==QAPLET_OK);
  qaplet_drive_result drive=QAPLET_DRIVE_RESULT_INIT;CHECK(qaplet_close(r,NULL)==QAPLET_OK && qaplet_quiesce(r,5000,&drive,NULL)==QAPLET_OK && drive.reason==QAPLET_DRIVE_QUIESCENT && qaplet_destroy(r,NULL)==QAPLET_OK);
  if(IsWindows()){uint32_t original_flags=0;CHECK(GetHandleInformation(handle,&original_flags) && SetStdHandle(-11,saved_handle) && CloseHandle(handle) && CloseHandle(peer));}
  else CHECK(fcntl(1,F_GETFL)==flags && dup2(saved,1)==1 && !close(saved) && !close(p[0]) && !close(p[1]));
  puts("M2 stdio backpressure: real stdout pipe slow consumer; finite 64KiB reservation; pending/BUSY without hidden collection; exact ordered binary drain and written count; original handle/flags survive: pass");return 0;
}
int sdk_stdio(int argc,char **argv) {
  if(argc>1 && !strncmp(argv[1],"--m2-stdio-",11)){char mode[64];snprintf(mode,sizeof(mode),"--%s",argv[1]+11);char *adapted[]={argv[0],mode};return sdk_stdio(2,adapted);}
  if(argc>1 && !strcmp(argv[1],"--classify"))return classify();
  if(argc>1 && !strcmp(argv[1],"--file"))return file_bytes();
  if(argc>1 && !strcmp(argv[1],"--pipe"))return pipe_bytes();
  if(argc>1 && !strcmp(argv[1],"--output-limits"))return output_limits();
  if(argc>1 && !strcmp(argv[1],"--js-defaults"))return js_defaults();
  if(argc>1 && !strcmp(argv[1],"--js-bytes"))return js_bytes();
  if(argc>1 && !strcmp(argv[1],"--terminal"))return terminal_kind();
  if(argc>1 && !strcmp(argv[1],"--backpressure"))return pipe_backpressure();
  qaplet_abi abi=QAPLET_ABI_INIT;
  CHECK(qaplet_query_abi(&abi)==QAPLET_OK);
  CHECK(abi.capabilities & (1u<<24));
  puts("M2 stdio capability: explicit opt-in available: pass");return 0;
}
#ifdef SDK_STANDALONE
int main(int argc,char **argv){return sdk_stdio(argc,argv);}
#endif
