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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "libc/dce.h"
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

static void print_result(const qaplet_result *result) {
  switch (result->type) {
    case QAPLET_VALUE_UNDEFINED: fputs("undefined",stdout); break;
    case QAPLET_VALUE_NULL: fputs("null",stdout); break;
    case QAPLET_VALUE_BOOL: fputs(result->boolean ? "true" : "false",stdout); break;
    case QAPLET_VALUE_DOUBLE:
      if (isnan(result->number)) fputs("NaN",stdout);
      else if (isinf(result->number)) fputs(signbit(result->number) ? "-Infinity" : "Infinity",stdout);
      else printf("%.17g",result->number);
      break;
    case QAPLET_VALUE_INT64: printf("%" PRId64,result->signed_integer); break;
    case QAPLET_VALUE_UINT64: printf("%" PRIu64,result->unsigned_integer); break;
    case QAPLET_VALUE_STRING:
      if (result->length) fwrite(result->buffer,1,result->length,stdout);
      break;
    case QAPLET_VALUE_BYTES:
      fputs("0x",stdout);
      for (size_t i=0;i<result->length;++i) printf("%02x",((unsigned char *)result->buffer)[i]);
      break;
  }
  putchar('\n');
}

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
  /* SDK byte writes have retired before the Host's buffered result/diagnostics
   * use these same routes. Blocking flags and complete terminal modes have
   * been restored; a large eval result must not silently truncate on EAGAIN. */
  if (!code && !unsettled && !cleanup && !restoration && !is_run) print_result(&result);
  free(result.buffer);
  if (execution_code) fprintf(stderr,"qaplet: code=%u %s\n",(unsigned)execution_code,message);
  if (unsettled) fputs("qaplet: UNSETTLED_ENTRY\n",stderr);
  if(cleanup)fprintf(stderr,"qaplet: native cleanup incomplete code=%u\n",(unsigned)cleanup);
  if(restoration)fprintf(stderr,"qaplet: standard stream restoration incomplete code=%u\n",(unsigned)restoration);
  if(cleanup || restoration)code=cleanup ? cleanup : restoration;
  return finish(code || unsettled ? 1 : 0);
}
