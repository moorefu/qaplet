/* Controlled Win32 boundary observer. No SDK private header/layout, no logging
 * allocation/lock across native calls; preserve errno/GetLastError exactly. */
#define _COSMO_SOURCE 1
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdatomic.h>
#include "libc/dce.h"
#include "libc/nt/runtime.h"
#include "libc/nt/process.h"
#include "libc/nt/errors.h"
#include "libc/nt/synchronization.h"
#include "libc/nt/nt/file.h"
#include "libc/nt/struct/filemodeinformation.h"
#include "libc/nt/struct/overlapped.h"
extern uint32_t GetFileType(int64_t);
extern int64_t CreateFile(const char16_t *,uint32_t,uint32_t,void *,uint32_t,uint32_t,int64_t);
extern bool32 __real_WriteFile(int64_t,const void *,uint32_t,uint32_t *,struct NtOverlapped *);
extern bool32 __real_GetOverlappedResult(int64_t,struct NtOverlapped *,uint32_t *,bool32);
extern bool32 __real_CloseHandle(int64_t);
extern void __real_ExitProcess(uint32_t);
extern void *__real_malloc(size_t);
extern void __real_free(void *);
typedef struct {_Atomic uintptr_t pointer;_Atomic size_t size;} allocation;
static allocation allocations[512];static _Atomic unsigned cursor;
typedef struct {int64_t handle,event;struct NtOverlapped *operation;uintptr_t buffer,base;size_t allocation_size;uint32_t bytes;const char *kind;int pending;} operation;
static operation operations[64];static unsigned operation_count,freed_pending,closed_pending,reused_pending;
static int64_t log_handle;static int fault,fault_used;
static int async_pipe(int64_t handle){
  if(GetFileType(handle)!=3)return 0;
  struct NtIoStatusBlock status={0};struct NtFileModeInformation mode={0};
  return NtQueryInformationFile(handle,&status,&mode,sizeof(mode),kNtFileModeInformation)>=0 && !(mode.Mode&0x30);
}
static void log_line(const char *line){
  if(!log_handle)return;int saved_errno=errno;uint32_t saved_error=GetLastError(),n=0;
  __real_WriteFile(log_handle,line,strlen(line),&n,NULL);errno=saved_errno;SetLastError(saved_error);
}
__attribute__((constructor(101))) static void setup(void){
  if(!IsWindows())return;int saved_errno=errno;uint32_t saved_error=GetLastError();
  log_handle=CreateFile(u"host-output-native.ndjson",0x40000000u,7,NULL,2,0x80,0);
  if(log_handle==-1)log_handle=0;
  char16_t value[2]={0};fault=GetEnvironmentVariable(u"QAPLET_CLI_OUTPUT_OBSERVER_FAULT",value,2)>0 && value[0]=='1';
  errno=saved_errno;SetLastError(saved_error);
}
void *__wrap_malloc(size_t size){
  void *p=__real_malloc(size);unsigned i=atomic_fetch_add(&cursor,1)%512;
  atomic_store(&allocations[i].size,size);atomic_store(&allocations[i].pointer,(uintptr_t)p);return p;
}
void __wrap_free(void *p){
  if(IsWindows() && p){
    int saved_errno=errno;uint32_t saved_error=GetLastError();
    for(unsigned i=0;i<operation_count;++i)if(operations[i].pending && operations[i].base==(uintptr_t)p){++freed_pending;log_line("{\"operation\":\"free-while-pending\"}\n");}
    errno=saved_errno;SetLastError(saved_error);
  }
  for(unsigned i=0;i<512;++i)if(atomic_load(&allocations[i].pointer)==(uintptr_t)p)atomic_store(&allocations[i].pointer,0);
  __real_free(p);
}
bool32 __wrap_WriteFile(int64_t handle,const void *data,uint32_t size,uint32_t *written,struct NtOverlapped *overlapped){
  const char *kind=NULL;if(size>=16 && !memcmp(data,"XXXXXXXXXXXXXXXX",16))kind="value";
  else if(size>=7 && !memcmp(data,"qaplet:",7))kind="diagnostic";
  operation *op=NULL;int asynchronous=0;
  if(IsWindows() && kind && operation_count<64){
    int saved_errno=errno;uint32_t saved_error=GetLastError();asynchronous=async_pipe(handle);
    op=&operations[operation_count++];*op=(operation){.handle=handle,.event=overlapped ? overlapped->hEvent : 0,.operation=overlapped,.buffer=(uintptr_t)data,.bytes=size,.kind=kind};
    for(unsigned i=0;i<512;++i){uintptr_t base=atomic_load(&allocations[i].pointer);size_t n=atomic_load(&allocations[i].size);
      if(base && (uintptr_t)data>=base && (uintptr_t)data-base<=n && size<=n-((uintptr_t)data-base)){op->base=base;op->allocation_size=n;break;}}
    for(unsigned i=0;i+1<operation_count;++i)if(operations[i].pending && overlapped && (operations[i].operation==overlapped || operations[i].event==overlapped->hEvent))++reused_pending;
    errno=saved_errno;SetLastError(saved_error);
  }
  bool32 ok=__real_WriteFile(handle,data,size,written,overlapped);int saved_errno=errno;uint32_t error=IsWindows() ? GetLastError() : 0;
  if(op){op->pending=!ok && error==997;char line[512];snprintf(line,sizeof(line),"{\"operation\":\"WriteFile\",\"kind\":\"%s\",\"requested_bytes\":%u,\"asynchronous_pipe\":%d,\"overlapped_nonnull\":%d,\"event_nonnull\":%d,\"buffer_allocation_known\":%d,\"buffer_interior_offset\":%llu,\"ok\":%d,\"error\":%u,\"pending\":%d}\n",kind,size,asynchronous,overlapped!=NULL,op->event!=0,op->base!=0,(unsigned long long)(op->base ? op->buffer-op->base : 0),ok,error,op->pending);log_line(line);}
  errno=saved_errno;if(IsWindows())SetLastError(error);return ok;
}
bool32 __wrap_GetOverlappedResult(int64_t handle,struct NtOverlapped *overlapped,uint32_t *written,bool32 wait){
  operation *op=NULL;for(unsigned i=0;i<operation_count;++i)if(operations[i].handle==handle && operations[i].operation==overlapped && operations[i].pending){op=&operations[i];break;}
  if(op && fault && !fault_used && !strcmp(op->kind,"value")){
    fault_used=1;log_line("{\"operation\":\"GetOverlappedResult\",\"fault_injected\":true,\"error\":996,\"physical_completion_confirmed\":false}\n");SetLastError(996);return 0;
  }
  bool32 ok=__real_GetOverlappedResult(handle,overlapped,written,wait);int saved_errno=errno;uint32_t error=IsWindows() ? GetLastError() : 0;
  if(op){if(ok)op->pending=0;char line[320];snprintf(line,sizeof(line),"{\"operation\":\"GetOverlappedResult\",\"kind\":\"%s\",\"wait\":%d,\"ok\":%d,\"error\":%u,\"transferred\":%u,\"physical_completion_confirmed\":%s}\n",op->kind,wait,ok,error,written ? *written : 0,ok ? "true" : "false");log_line(line);}
  errno=saved_errno;if(IsWindows())SetLastError(error);return ok;
}
bool32 __wrap_CloseHandle(int64_t handle){
  if(IsWindows())for(unsigned i=0;i<operation_count;++i)if(operations[i].pending && operations[i].event==handle){++closed_pending;log_line("{\"operation\":\"CloseHandle-while-pending\"}\n");}
  return __real_CloseHandle(handle);
}
void __wrap_ExitProcess(uint32_t status){
  unsigned pending=0;for(unsigned i=0;i<operation_count;++i)pending+=operations[i].pending!=0;
  char line[320];snprintf(line,sizeof(line),"{\"operation\":\"ExitProcess\",\"status\":%u,\"unconfirmed_operations\":%u,\"buffer_freed_while_pending\":%u,\"event_closed_while_pending\":%u,\"identity_reused_while_pending\":%u,\"fault_injected\":%d}\n",status,pending,freed_pending,closed_pending,reused_pending,fault_used);log_line(line);
  if(log_handle)__real_CloseHandle(log_handle);__real_ExitProcess(status);__builtin_unreachable();
}
