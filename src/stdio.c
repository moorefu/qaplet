/* SPDX-License-Identifier: MIT */
#define _COSMO_SOURCE 1
#include "io_internal.h"
#include "js_io.h"
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "libc/dce.h"
#include "libc/nt/console.h"
#include "libc/nt/ipc.h"
#include "libc/nt/runtime.h"
#include "libc/nt/nt/file.h"
#include "libc/nt/struct/filemodeinformation.h"
extern uint32_t GetFileType(int64_t);

/* Read-only native classification. No duplicate, flags, mode or offset change.
 * FILE_MODE_INFORMATION is queried on the real standard HANDLE, never inferred
 * from a private Console/pipe fixture. Unknown/query failures stay unsupported. */
void qaplet_stdio_classify(uint32_t stream,qaplet_stdio_info *info,int *fd,int64_t *handle,uint32_t *quota) {
  *info=(qaplet_stdio_info)QAPLET_STDIO_INFO_INIT;*fd=-1;*handle=0;*quota=0;
  if(IsWindows()){
    *handle=GetStdHandle(-10-(int)stream);
    if(!*handle || *handle==-1){info->error_domain=QAPLET_ERROR_DOMAIN_WIN32;info->error=6;return;}
    uint32_t type=GetFileType(*handle),mode=0;
    if(!type){info->error_domain=QAPLET_ERROR_DOMAIN_WIN32;info->error=GetLastError();return;}
    if(type==2 && GetConsoleMode(*handle,&mode)){info->kind=QAPLET_STDIO_CONSOLE;return;}
    info->kind=type==1 ? QAPLET_STDIO_FILE : type==3 ? QAPLET_STDIO_PIPE : QAPLET_STDIO_OTHER;
    if(type!=1 && type!=3)return;
    struct NtIoStatusBlock status={0};struct NtFileModeInformation file_mode={0};
    int32_t code=NtQueryInformationFile(*handle,&status,&file_mode,sizeof(file_mode),kNtFileModeInformation);
    if(code<0){info->error_domain=QAPLET_ERROR_DOMAIN_WIN32;info->error=RtlNtStatusToDosError(code);return;}
    int synchronous=(file_mode.Mode&0x30)!=0;
    if(type==1)info->supported=synchronous;
    else if(!synchronous){
      /* Only overlapped byte pipes use the completion backend. A write must
       * also have a known quota so one pending IRP never hides a partial chunk. */
      uint32_t flags=0;
      if(!GetNamedPipeInfo(*handle,&flags,quota,NULL,NULL)){info->error_domain=QAPLET_ERROR_DOMAIN_WIN32;info->error=GetLastError();return;}
      info->supported=!(flags&4) && (!stream || *quota);
    }
  }else{
    *fd=(int)stream;struct stat status;
    if(fstat(*fd,&status)){info->error_domain=QAPLET_ERROR_DOMAIN_ERRNO;info->error=errno;return;}
    if(S_ISFIFO(status.st_mode)){
      info->kind=QAPLET_STDIO_PIPE;int flags=fcntl(*fd,F_GETFL);
      if(flags<0){info->error_domain=QAPLET_ERROR_DOMAIN_ERRNO;info->error=errno;return;}
      info->supported=(flags&O_NONBLOCK)!=0;
    }else if(S_ISREG(status.st_mode)){info->kind=QAPLET_STDIO_FILE;info->supported=1;}
    else if(S_ISCHR(status.st_mode) && isatty(*fd))info->kind=QAPLET_STDIO_TTY;
    else info->kind=QAPLET_STDIO_OTHER;
  }
}
qaplet_code qaplet_stdio_inspect(qaplet_runtime *r,uint32_t stream,qaplet_stdio_info *out,qaplet_diagnostic *d) {
  qaplet_code code=qaplet_check_diagnostic(d);if(!code)code=qaplet_check_structure(out,sizeof(*out));
  if(!code && (stream>2 || out->reserved))code=QAPLET_INVALID_ARGUMENT;if(!code)code=qaplet_owner(r);if(code)return qaplet_report(d,code);
  if(!r->m2_enabled)return qaplet_report(d,QAPLET_UNSUPPORTED);
  if(qaplet_get_lifecycle(r)!=QAPLET_OPEN)return qaplet_report(d,QAPLET_BAD_STATE);
  qaplet_stdio_info info;int fd;int64_t handle;uint32_t quota;
  qaplet_stdio_classify(stream,&info,&fd,&handle,&quota);
  pthread_mutex_lock(&r->mutex);info.attached=qaplet_io_standard_locked(r,stream,NULL,NULL);pthread_mutex_unlock(&r->mutex);
  memcpy((char *)out+8,(char *)&info+8,sizeof(info)-8);return qaplet_report(d,QAPLET_OK);
}
static int initialize_stdio(JSContext *context,JSModuleDef *module) {
  qaplet_runtime *r=JS_GetContextOpaque(context);
  static const char *names[]={"stdin","stdout","stderr"};
  static const char *kinds[]={"invalid","pipe","file","tty","console","other"};
  for(uint32_t stream=0;stream<3;++stream){
    qaplet_io_endpoint endpoint=QAPLET_IO_ENDPOINT_INIT;qaplet_stdio_info info=QAPLET_STDIO_INFO_INIT;
    r->bridge_active=1;qaplet_code code=qaplet_stdio_get(r,stream,&endpoint,&info,NULL);r->bridge_active=0;
    JSValue value;
    if(code==QAPLET_UNSUPPORTED)value=JS_NULL;
    else if(code){JS_ThrowInternalError(context,"standard stream unavailable");return -1;}
    else{
      value=qaplet_js_io_endpoint(r,&endpoint,stream!=0);
      if(!JS_IsException(value) && JS_DefinePropertyValueStr(context,value,"kind",JS_NewString(context,kinds[info.kind]),JS_PROP_ENUMERABLE)<0){JS_FreeValue(context,value);value=JS_EXCEPTION;}
    }
    if(JS_IsException(value))return -1;
    if(JS_SetModuleExport(context,module,names[stream],value)<0)return -1;
  }
  return 0;
}
JSModuleDef *qaplet_stdio_load(qaplet_runtime *r) {
  JSModuleDef *module=JS_NewCModule(r->context,"qaplet:stdio",initialize_stdio);
  if(module && (JS_AddModuleExport(r->context,module,"stdin")<0 || JS_AddModuleExport(r->context,module,"stdout")<0 || JS_AddModuleExport(r->context,module,"stderr")<0))return NULL;
  return module;
}
