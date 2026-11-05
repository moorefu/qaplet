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
int sdk_stdio(int argc,char **argv) {
  if(argc>1 && !strcmp(argv[1],"--classify"))return classify();
  if(argc>1 && !strcmp(argv[1],"--file"))return file_bytes();
  if(argc>1 && !strcmp(argv[1],"--pipe"))return pipe_bytes();
  if(argc>1 && !strcmp(argv[1],"--output-limits"))return output_limits();
  qaplet_abi abi=QAPLET_ABI_INIT;
  CHECK(qaplet_query_abi(&abi)==QAPLET_OK);
  CHECK(abi.capabilities & (1u<<24));
  puts("M2 stdio capability: explicit opt-in available: pass");return 0;
}
#ifdef SDK_STANDALONE
int main(int argc,char **argv){return sdk_stdio(argc,argv);}
#endif
