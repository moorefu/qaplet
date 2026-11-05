/* SPDX-License-Identifier: MIT */
#define _COSMO_SOURCE 1
#include "qaplet.h"
/* Product-private drive boundary; this is not an Embed SDK API. It uses run's
 * automatic liveness rules and returns before the first independent delivery
 * can replace the completed entry result that the CLI must copy. */
qaplet_code qaplet_drive_entry(qaplet_runtime *, qaplet_drive_result *, qaplet_diagnostic *);
#include <inttypes.h>
#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "libc/dce.h"
#include "libc/nt/console.h"
#include "libc/nt/runtime.h"

static int finish(int status) {
  /* The supplied Cosmopolitan exit path encodes POSIX wait status on PE.
   * CLI status is a raw OS contract. Complete SDK cleanup and stdio first. */
  fflush(NULL);
  if (IsWindows()) ExitProcess(status);
  return status;
}

static int invalid_arguments(void) {
  fputs("qaplet: INVALID_ARGUMENT\n",stderr);
  return finish(2);
}

static int parse_budget(const char *text, uint64_t *output) {
  if (!*text) return 0;
  uint64_t value=0;
  for (;*text;++text) {
    if (*text<'0' || *text>'9' || value>(UINT64_MAX-(unsigned)(*text-'0'))/10) return 0;
    value=value*10+(unsigned)(*text-'0');
  }
  *output=value;
  return 1;
}

typedef struct {
  int changed[3], flags[3];
  qaplet_io_endpoint pipes[3];
} cli_stdio;

/* The CLI is the Host of these inherited routes. It authorizes temporary
 * O_NONBLOCK on POSIX pipes, including the shared open file description; no
 * other participant may change flags or perform concurrent IO during this
 * ownership interval. The SDK itself continues to be a non-mutating borrower.
 * Unsupported/invalid routes remain absent from the first JS stdio snapshot.
 * In particular a synchronous Windows pipe is never treated as overlapped. */
static qaplet_code connect_stdio(qaplet_runtime *runtime,cli_stdio *host,qaplet_diagnostic *diagnostic) {
  qaplet_io_endpoint terminals[3];unsigned count=0;
  for(uint32_t stream=0;stream<3;++stream){
    qaplet_stdio_info info=QAPLET_STDIO_INFO_INIT;
    qaplet_io_endpoint endpoint=QAPLET_IO_ENDPOINT_INIT;
    qaplet_code code=qaplet_stdio_inspect(runtime,stream,&info,diagnostic);
    if(code)return code;
    if(info.kind==QAPLET_STDIO_TTY || info.kind==QAPLET_STDIO_CONSOLE){
      qaplet_tty_options options=QAPLET_TTY_OPTIONS_INIT;
      qaplet_tty_status status=QAPLET_TTY_STATUS_INIT;
      /* Join only the actual selected route. An unrelated terminal gets its
       * own explicit lease; no alternate device is opened. Zero flags preserve
       * terminal modes. JS may release this group, await closed, then acquire
       * raw/VT facades; exports are deliberately the first-import snapshot. */
      code=QAPLET_UNSUPPORTED;
      for(unsigned i=0;i<count && code==QAPLET_UNSUPPORTED;++i)
        code=qaplet_tty_join(runtime,&terminals[i],stream,&options,&endpoint,&status,diagnostic);
      if(code==QAPLET_UNSUPPORTED){
        code=qaplet_tty_acquire(runtime,stream,&options,&endpoint,&status,diagnostic);
        if(!code)terminals[count++]=endpoint;
      }
      if(code==QAPLET_UNSUPPORTED)continue;
      if(code)return code;
    }else{
      if(!IsWindows() && info.kind==QAPLET_STDIO_PIPE && !info.supported){
        int flags=fcntl(stream,F_GETFL);
        if(flags<0)return QAPLET_IO_ERROR;
        host->flags[stream]=flags;
        if(fcntl(stream,F_SETFL,flags|O_NONBLOCK)<0)return QAPLET_IO_ERROR;
        host->changed[stream]=1;info.supported=1;
      }
      if(!info.supported)continue;
      code=qaplet_stdio_attach(runtime,stream,&endpoint,&info,diagnostic);
      if(code)return code;
      host->pipes[stream]=endpoint;
    }
  }
  return QAPLET_OK;
}

static qaplet_code restore_stdio(qaplet_runtime *runtime,cli_stdio *host,int quiescent) {
  qaplet_code result=QAPLET_OK;
  for(int stream=2;stream>=0;--stream)if(host->changed[stream]){
    int retired=quiescent || !host->pipes[stream].generation;
    if(!retired){
      qaplet_io_endpoint_status status=QAPLET_IO_ENDPOINT_STATUS_INIT;
      qaplet_code code=qaplet_get_io_endpoint_status(runtime,&host->pipes[stream],&status,NULL);
      retired=code==QAPLET_INVALID_HANDLE || (!code && status.ended && !status.physical_pending);
    }
    /* An unrelated retained file/helper must not prevent restoring an already
     * retired pipe. Conversely a live operation keeps its flags until exit. */
    if(!retired || fcntl(stream,F_SETFL,host->flags[stream])<0)result=QAPLET_IO_ERROR;
    else host->changed[stream]=0;
  }
  return result;
}

static qaplet_code render_result(const qaplet_result *result,char **out,size_t *length) {
  size_t capacity=result->type==QAPLET_VALUE_BYTES ? result->length*2+3 :
                  result->type==QAPLET_VALUE_STRING ? result->length+1 : 128;
  char *text=malloc(capacity);if(!text)return QAPLET_MEMORY_LIMIT;size_t n=0;
  switch (result->type) {
    case QAPLET_VALUE_UNDEFINED: n=sprintf(text,"undefined");break;
    case QAPLET_VALUE_NULL: n=sprintf(text,"null");break;
    case QAPLET_VALUE_BOOL: n=sprintf(text,"%s",result->boolean ? "true" : "false");break;
    case QAPLET_VALUE_DOUBLE:
      if (isnan(result->number)) n=sprintf(text,"NaN");
      else if (isinf(result->number)) n=sprintf(text,"%s",signbit(result->number) ? "-Infinity" : "Infinity");
      else n=sprintf(text,"%.17g",result->number);
      break;
    case QAPLET_VALUE_INT64: n=sprintf(text,"%" PRId64,result->signed_integer);break;
    case QAPLET_VALUE_UINT64: n=sprintf(text,"%" PRIu64,result->unsigned_integer);break;
    case QAPLET_VALUE_STRING:
      n=result->length;if(n)memcpy(text,result->buffer,n);
      break;
    case QAPLET_VALUE_BYTES:
      text[n++]='0';text[n++]='x';
      for(size_t i=0;i<result->length;++i){unsigned byte=((unsigned char *)result->buffer)[i];text[n++]="0123456789abcdef"[byte>>4];text[n++]="0123456789abcdef"[byte&15];}
      break;
  }
  text[n++]='\n';*out=text;*length=n;return QAPLET_OK;
}

/* A one-shot Host write after native retirement, not another loop/SDK backend.
 * Win32 CRT offsets can be cached separately from the shared HANDLE position
 * advanced by managed IO. Use the actual selected route for final output too.
 * Console gets explicit UTF-16, independent of the console output code page;
 * its UTF-8 text boundary is the same strict scalar subset as the SDK adapter. */
static qaplet_code write_output(unsigned stream,const char *data,size_t size) {
  if(IsWindows()){
    int64_t handle=GetStdHandle(-10-(int)stream);uint32_t mode=0;
    if(!handle || handle==-1)return QAPLET_IO_ERROR;
    int console=GetConsoleMode(handle,&mode);
    while(size){
      uint32_t written=0;
      if(!console){
        uint32_t chunk=size>65536 ? 65536 : size;
        if(!WriteFile(handle,data,chunk,&written,NULL) || !written || written>chunk)return QAPLET_IO_ERROR;
        data+=written;size-=written;
      }else{
        char16_t wide[1024];uint32_t count=0;size_t used=0;
        while(used<size && count<1022){
          unsigned first=(unsigned char)data[used++],scalar=first,following=0,minimum=0;
          if(first>=128){if(first>=194 && first<=223){scalar=first&31;following=1;minimum=128;}
            else if(first<=239 && first>=224){scalar=first&15;following=2;minimum=2048;}
            else if(first<=244 && first>=240){scalar=first&7;following=3;minimum=65536;}
            else return QAPLET_INVALID_UTF8;}
          if(following>size-used)return QAPLET_INVALID_UTF8;
          for(unsigned i=0;i<following;++i){unsigned next=(unsigned char)data[used++];if((next&192)!=128)return QAPLET_INVALID_UTF8;scalar=(scalar<<6)|(next&63);}
          if(scalar<minimum || scalar>0x10ffff || (scalar>=0xd800 && scalar<=0xdfff))return QAPLET_INVALID_UTF8;
          if(scalar>65535){scalar-=65536;wide[count++]=0xd800+(scalar>>10);wide[count++]=0xdc00+(scalar&1023);}else wide[count++]=scalar;
        }
        uint32_t at=0;
        while(at<count){if(!WriteConsole(handle,wide+at,count-at,&written,NULL) || !written || written>count-at)return QAPLET_IO_ERROR;at+=written;}
        data+=used;size-=used;
      }
    }
    return QAPLET_OK;
  }
  /* Match the SDK byte writer's thread-local SIGPIPE discipline. Never change
   * the Host's process disposition; consume only a signal produced by this
   * failed output and preserve one that was already pending. */
  sigset_t blocked,previous,pending;sigemptyset(&blocked);sigaddset(&blocked,SIGPIPE);
  if(pthread_sigmask(SIG_BLOCK,&blocked,&previous))return QAPLET_IO_ERROR;
  int was_pending=!sigpending(&pending) && sigismember(&pending,SIGPIPE);
  int failed=0;
  while(size){ssize_t n=write(stream,data,size);if(n<0 && errno==EINTR)continue;if(n<=0){failed=1;break;}data+=n;size-=n;}
  if(!was_pending && !sigpending(&pending) && sigismember(&pending,SIGPIPE)){
    struct timespec zero={0};while(sigtimedwait(&blocked,NULL,&zero)<0 && errno==EINTR){}
  }
  if(pthread_sigmask(SIG_SETMASK,&previous,NULL))failed=1;
  return failed ? QAPLET_IO_ERROR : QAPLET_OK;
}

static qaplet_code output_result(const qaplet_result *result) {
  char *data=NULL;size_t size=0;qaplet_code code=render_result(result,&data,&size);
  if(!code)code=write_output(1,data,size);free(data);return code;
}
static void report_error(const char *message) {write_output(2,message,strlen(message));}

int main(int argc, char **argv) {
  qaplet_config config = QAPLET_CONFIG_INIT;
  int position=1, seen_timeout=0, seen_dispatches=0;
  while (position<argc && (!strcmp(argv[position],"--timeout-ms") || !strcmp(argv[position],"--max-dispatches"))) {
    int timeout=!strcmp(argv[position],"--timeout-ms");
    uint64_t value;
    if (position+1>=argc || !parse_budget(argv[position+1],&value) ||
        (timeout && (seen_timeout++ || value>UINT64_MAX/1000000)) || (!timeout && seen_dispatches++))
      return invalid_arguments();
    if (timeout) config.limits.execution_timeout_ms=value;
    else config.limits.execution_dispatches=value;
    position+=2;
  }
  if (argc-position==1 && !strcmp(argv[position],"--help")) {
    fputs("Usage: qaplet [--timeout-ms N] [--max-dispatches N] run <file> [-- args...]\n"
          "       qaplet [--timeout-ms N] [--max-dispatches N] eval --source <text>\n"
          "       qaplet --help | --version\n"
          "run uses ESM; argv contains only args after --. eval uses Script.\n"
          "eval prints scalar values, raw UTF-8 strings, or 0x-prefixed hex bytes, then a newline.\n"
          "Zero budgets use finite SDK defaults. Exit: 0 success, 1 execution, 2 arguments.\n",stdout);
    return finish(0);
  }
  if (argc-position==1 && !strcmp(argv[position],"--version")) {
    printf("qaplet 0.1.0 (Embed SDK ABI %u)\n",QAPLET_ABI_VERSION);
    return finish(0);
  }
  int is_run=position<argc && !strcmp(argv[position],"run");
  const char *input=NULL;
  qaplet_string *arguments=NULL;
  if (is_run) {
    if (argc-position<2 || (argc-position>2 && strcmp(argv[position+2],"--"))) return invalid_arguments();
    input=argv[position+1];
    if (!*input) return invalid_arguments();
    if (argc-position>3) {
      config.argc=(size_t)(argc-position-3);
      arguments=calloc(config.argc,sizeof(*arguments));
      if (!arguments) { fputs("qaplet: code=11 MEMORY_LIMIT\n",stderr); return finish(1); }
      for (size_t i=0;i<config.argc;++i) arguments[i]=(qaplet_string){argv[position+3+i],strlen(argv[position+3+i])};
      config.argv=arguments;
    }
  } else {
    if (argc-position!=3 || strcmp(argv[position],"eval") || strcmp(argv[position+1],"--source")) return invalid_arguments();
    input=argv[position+2];
  }
  qaplet_runtime *runtime = NULL;
  qaplet_progress progress = QAPLET_PROGRESS_INIT;
  qaplet_result result = QAPLET_RESULT_INIT;
  qaplet_diagnostic diagnostic = QAPLET_DIAGNOSTIC_INIT;
  qaplet_m2_config m2 = QAPLET_M2_CONFIG_INIT;
  qaplet_drive_result drive = QAPLET_DRIVE_RESULT_INIT;
  cli_stdio host={0};
  char message[1024]; diagnostic.message=message; diagnostic.capacity=sizeof(message);
  qaplet_code code=qaplet_create_m2(&config,&m2,&runtime,&diagnostic);
  free(arguments);
  if(!code)code=connect_stdio(runtime,&host,&diagnostic);
  if (!code) code=is_run ? qaplet_run_entry(runtime,input,strlen(input),&progress,&diagnostic) :
                          qaplet_eval(runtime,input,strlen(input),&progress,&diagnostic);
  int unsettled=0;
  while (!code && progress.phase==QAPLET_PHASE_PENDING) {
    code=qaplet_drive_entry(runtime,&drive,&diagnostic);
    progress.phase=drive.phase;
    if (!code && drive.reason==QAPLET_DRIVE_UNSETTLED_ENTRY) { unsettled=1; break; }
  }
  if (!code && !unsettled && !is_run) code=qaplet_get_result(runtime,&result,&diagnostic);
  if (code==QAPLET_BUFFER_TOO_SMALL) {
    result.buffer=malloc(result.length); result.capacity=result.length;
    if (!result.buffer) { code=QAPLET_MEMORY_LIMIT; strcpy(message,"MEMORY_LIMIT"); }
    else code=qaplet_get_result(runtime,&result,&diagnostic);
  }
  /* Retain the copied entry result before later independent deliveries can
   * replace the SDK's most recent result. */
  if (!code && !unsettled) {
    code=qaplet_run(runtime,&drive,&diagnostic);
    if (!code && drive.reason==QAPLET_DRIVE_UNSETTLED_ENTRY) unsettled=1;
  }
  qaplet_code execution_code=code,cleanup=QAPLET_OK,restoration=QAPLET_OK;
  if (runtime) {
    cleanup=qaplet_close(runtime,NULL);
    if (!cleanup) cleanup=qaplet_quiesce(runtime,5000,&drive,NULL);
    if (!cleanup && drive.reason!=QAPLET_DRIVE_QUIESCENT) cleanup=QAPLET_BUSY;
    restoration=restore_stdio(runtime,&host,!cleanup);
    if (!cleanup) cleanup=qaplet_destroy(runtime,NULL);
  }
  /* SDK byte writes have retired before the Host's final result/diagnostics
   * use these same routes. Blocking flags and complete terminal modes have
   * been restored; a large eval result must not silently truncate on EAGAIN. */
  if (!code && !unsettled && !cleanup && !restoration && !is_run){
    code=output_result(&result);
    if(code){char text[128];snprintf(text,sizeof(text),"qaplet: code=%u %s (entry result output)\n",code,code==QAPLET_MEMORY_LIMIT ? "MEMORY_LIMIT" : code==QAPLET_INVALID_UTF8 ? "INVALID_UTF8" : "IO_ERROR");report_error(text);}
  }
  free(result.buffer);
  if (execution_code){char text[1200];snprintf(text,sizeof(text),"qaplet: code=%u %s\n",(unsigned)execution_code,message);report_error(text);}
  if (unsettled) report_error("qaplet: UNSETTLED_ENTRY\n");
  if(cleanup){char text[128];snprintf(text,sizeof(text),"qaplet: native cleanup incomplete code=%u\n",(unsigned)cleanup);report_error(text);}
  if(restoration){char text[128];snprintf(text,sizeof(text),"qaplet: standard stream restoration incomplete code=%u\n",(unsigned)restoration);report_error(text);}
  if(cleanup || restoration)code=cleanup ? cleanup : restoration;
  return finish(code || unsettled ? 1 : 0);
}
