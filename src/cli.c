/* SPDX-License-Identifier: MIT */
#define _COSMO_SOURCE 1
#include "qaplet.h"
#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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
  char message[1024]; diagnostic.message=message; diagnostic.capacity=sizeof(message);
  qaplet_code code=qaplet_create(&config,&runtime,&diagnostic);
  free(arguments);
  if (!code) code=is_run ? qaplet_run_entry(runtime,input,strlen(input),&progress,&diagnostic) :
                          qaplet_eval(runtime,input,strlen(input),&progress,&diagnostic);
  int unsettled=0;
  while (!code && progress.phase==QAPLET_PHASE_PENDING) {
    code=qaplet_pump(runtime,&progress,&diagnostic);
    if (!code && progress.phase==QAPLET_PHASE_PENDING && !progress.progressed) { unsettled=1; break; }
  }
  if (!code && !unsettled && !is_run) code=qaplet_get_result(runtime,&result,&diagnostic);
  if (code==QAPLET_BUFFER_TOO_SMALL) {
    result.buffer=malloc(result.length); result.capacity=result.length;
    if (!result.buffer) { code=QAPLET_MEMORY_LIMIT; strcpy(message,"MEMORY_LIMIT"); }
    else code=qaplet_get_result(runtime,&result,&diagnostic);
  }
  if (!code && !unsettled && !is_run) print_result(&result);
  free(result.buffer);
  if (code) fprintf(stderr,"qaplet: code=%u %s\n",(unsigned)code,message);
  if (unsettled) fputs("qaplet: UNSETTLED_ENTRY\n",stderr);
  if (runtime) { qaplet_close(runtime,NULL); qaplet_destroy(runtime,NULL); }
  return finish(code || unsettled ? 1 : 0);
}
