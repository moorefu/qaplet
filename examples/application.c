/* SPDX-License-Identifier: MIT */
/* Compile this file using only the distributed public header and static SDK.
 * pthread, stdio and the native process exit adapter belong to the Host. */
#define _COSMO_SOURCE 1
#include "qaplet.h"
#include <inttypes.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/utsname.h>
#include <unistd.h>
#include "libc/dce.h"
#include "libc/nt/runtime.h"

static int finish(int status) {
  fflush(NULL);
  if (IsWindows()) ExitProcess(status);
  return status;
}

#define REQUIRE(test) do { if (!(test)) { \
  fprintf(stderr,"application contract failed at line %d: %s\n",__LINE__,#test); return 2; \
} } while (0)

typedef struct {
  qaplet_runtime *runtime;
  pthread_mutex_t mutex;
  pthread_cond_t changed;
  unsigned command, completed;
  qaplet_code code;
  int closed_calls;
  const char *mode;
  FILE *resource;
} host;

static qaplet_code record(qaplet_runtime *runtime, uint64_t call, size_t argc,
                           const qaplet_value *argv, void *opaque) {
  host *state=opaque;
  if (argc!=1 || argv[0].type!=QAPLET_VALUE_BYTES || argv[0].length!=3 ||
      memcmp(argv[0].data,"\0\xff\x11",3)) return QAPLET_HOST_ERROR;
  if (fwrite(argv[0].data,1,3,state->resource)!=3) return QAPLET_HOST_ERROR;
  qaplet_value result=QAPLET_VALUE_INIT;
  result.type=QAPLET_VALUE_STRING; result.data="copied"; result.length=6;
  return qaplet_host_set_result(runtime,call,&result,NULL);
}

static void *produce(void *opaque) {
  host *state=opaque;
  pthread_mutex_lock(&state->mutex);
  for (;;) {
    while (state->command==state->completed) pthread_cond_wait(&state->changed,&state->mutex);
    unsigned command=state->command;
    pthread_mutex_unlock(&state->mutex);
    if (command==1) {
      unsigned char payload[]={0,255,17};
      state->code=qaplet_post_event(state->runtime,7,UINT64_MAX,payload,sizeof(payload),NULL);
      memset(payload,99,sizeof(payload)); /* the queue must own its copy now */
      if (!strcmp(state->mode,"queue-full") && !state->code)
        state->code=qaplet_post_event(state->runtime,8,0,NULL,0,NULL);
    } else if (command==2) {
      state->code=qaplet_request_stop(state->runtime,NULL);
    } else {
      qaplet_snapshot snapshot=QAPLET_SNAPSHOT_INIT;
      state->closed_calls=qaplet_get_snapshot(state->runtime,&snapshot,NULL)==QAPLET_OK &&
          snapshot.lifecycle==QAPLET_CLOSED && !snapshot.engine_current_bytes &&
          qaplet_post_event(state->runtime,0,0,NULL,0,NULL)==QAPLET_BAD_STATE &&
          qaplet_request_stop(state->runtime,NULL)==QAPLET_OK;
    }
    pthread_mutex_lock(&state->mutex);
    state->completed=command;
    pthread_cond_broadcast(&state->changed);
    if (command==3) break;
  }
  pthread_mutex_unlock(&state->mutex);
  return NULL;
}

static void command(host *state, unsigned value) {
  pthread_mutex_lock(&state->mutex);
  state->command=value;
  pthread_cond_broadcast(&state->changed);
  while (state->completed!=value) pthread_cond_wait(&state->changed,&state->mutex);
  pthread_mutex_unlock(&state->mutex);
}

static int resource_open(char *path, size_t capacity, FILE **stream) {
  struct utsname facts;
  if (uname(&facts)) return 0;
  const char *root=!strcmp(facts.sysname,"Windows") ? getenv("TEMP") : "/tmp";
  if (!root || !*root) return 0;
  int length=snprintf(path,capacity,"%s/qaplet-application-XXXXXX",root);
  if (length<=0 || (size_t)length>=capacity) return 0;
  int descriptor=mkstemp(path);
  if (descriptor<0) return 0;
  *stream=fdopen(descriptor,"w+b");
  if (!*stream) { close(descriptor); unlink(path); }
  return *stream!=NULL;
}

static qaplet_code settle(qaplet_runtime *runtime, qaplet_progress *progress,
                            qaplet_diagnostic *diagnostic) {
  for (unsigned i=0; progress->phase==QAPLET_PHASE_PENDING && i<1000; ++i) {
    qaplet_code code=qaplet_pump(runtime,progress,diagnostic);
    if (code || !progress->progressed) return code;
  }
  return QAPLET_OK;
}

static int application(const char *entry, const char *mode) {
  host state={0}; state.mode=mode;
  char path[8192], message[256];
  REQUIRE(resource_open(path,sizeof(path),&state.resource));
  REQUIRE(!pthread_mutex_init(&state.mutex,NULL) && !pthread_cond_init(&state.changed,NULL));
  qaplet_config config=QAPLET_CONFIG_INIT;
  qaplet_string argument={mode,strlen(mode)}; config.argv=&argument; config.argc=1;
  if (!strcmp(mode,"deadline")) config.limits.execution_timeout_ms=20;
  if (!strcmp(mode,"queue-full")) config.limits.event_queue_slots=1;
  qaplet_diagnostic diagnostic=QAPLET_DIAGNOSTIC_INIT;
  diagnostic.message=message; diagnostic.capacity=sizeof(message);
  qaplet_code code=qaplet_create(&config,&state.runtime,&diagnostic);
  pthread_t thread; int started=0, delivered=0;
  if (!code) {
    qaplet_host_export exports[]={QAPLET_HOST_EXPORT_INIT,QAPLET_HOST_EXPORT_INIT};
    exports[0].name="answer"; exports[0].name_length=6;
    exports[0].kind=QAPLET_HOST_CONSTANT; exports[0].value.type=QAPLET_VALUE_DOUBLE;
    exports[0].value.number=40;
    exports[1].name="record"; exports[1].name_length=6;
    exports[1].kind=QAPLET_HOST_FUNCTION; exports[1].function=record; exports[1].userdata=&state;
    qaplet_host_module module=QAPLET_HOST_MODULE_INIT;
    module.name="host:application"; module.name_length=16; module.exports=exports; module.export_count=2;
    code=qaplet_register_host_module(state.runtime,&module,&diagnostic);
  }
  qaplet_progress progress=QAPLET_PROGRESS_INIT;
  if (!code) code=qaplet_run_entry(state.runtime,entry,strlen(entry),&progress,&diagnostic);
  if (!code) code=settle(state.runtime,&progress,&diagnostic);
  if (!code && progress.phase==QAPLET_PHASE_PENDING) {
    REQUIRE(!pthread_create(&thread,NULL,produce,&state)); started=1;
    command(&state,1); code=state.code;
    if (!code) code=settle(state.runtime,&progress,&diagnostic);
    if (!code && progress.phase!=QAPLET_PHASE_DONE) code=QAPLET_BAD_STATE;
    if (!code) {
      qaplet_result result=QAPLET_RESULT_INIT;
      code=qaplet_get_result(state.runtime,&result,&diagnostic);
      if (!code && result.type!=QAPLET_VALUE_UNDEFINED) code=QAPLET_BAD_STATE;
      const char source[]="globalThis.applicationResult";
      if (!code) code=qaplet_eval(state.runtime,source,sizeof(source)-1,&progress,&diagnostic);
      if (!code) code=qaplet_get_result(state.runtime,&result,&diagnostic);
      if (!code && (result.type!=QAPLET_VALUE_DOUBLE || result.number!=42)) code=QAPLET_BAD_STATE;
      delivered=!code;
    }
    if (!code) {
      command(&state,2); code=state.code;
      if (!code) {
        code=qaplet_pump(state.runtime,&progress,&diagnostic);
        qaplet_snapshot snapshot=QAPLET_SNAPSHOT_INIT;
        if (code==QAPLET_EXECUTION_STOPPED &&
            qaplet_get_snapshot(state.runtime,&snapshot,NULL)==QAPLET_OK &&
            snapshot.lifecycle==QAPLET_STOPPED) code=QAPLET_OK;
        else code=QAPLET_BAD_STATE;
      }
    }
  } else if (!code) code=QAPLET_BAD_STATE;
  /* Close leaves the synchronized control handle alive while the worker exits.
   * Host code/userdata/FILE stay valid through close. Never destroy before join. */
  if (state.runtime) REQUIRE(qaplet_close(state.runtime,NULL)==QAPLET_OK);
  if (started) {
    command(&state,3);
    REQUIRE(!pthread_join(thread,NULL) && state.closed_calls);
  }
  REQUIRE(!fflush(state.resource) && !fseek(state.resource,0,SEEK_SET));
  unsigned char contents[4]; size_t count=fread(contents,1,sizeof(contents),state.resource);
  REQUIRE(!ferror(state.resource));
  if (delivered) REQUIRE(count==3 && !memcmp(contents,"\0\xff\x11",3));
  REQUIRE(!fclose(state.resource) && !unlink(path));
  if (state.runtime) REQUIRE(qaplet_destroy(state.runtime,NULL)==QAPLET_OK);
  REQUIRE(!pthread_cond_destroy(&state.changed) && !pthread_mutex_destroy(&state.mutex));
  if (code) {
    fprintf(stderr,"application: code=%u; closed; joined; resource released; destroyed\n",(unsigned)code);
    return 1;
  }
  puts("application: result=42; worker copied; stopped; closed; joined; resource released; destroyed");
  return 0;
}

static int metadata(void) {
  qaplet_abi abi=QAPLET_ABI_INIT;
  qaplet_config config=QAPLET_CONFIG_INIT;
  qaplet_runtime *runtime=NULL;
  qaplet_snapshot snapshot=QAPLET_SNAPSHOT_INIT;
  REQUIRE(qaplet_query_abi(&abi)==QAPLET_OK && qaplet_create(&config,&runtime,NULL)==QAPLET_OK &&
      qaplet_get_snapshot(runtime,&snapshot,NULL)==QAPLET_OK);
  printf("{\"abi_version\":%u,\"capabilities\":%" PRIu64 ",\"defaults\":{",abi.abi_version,abi.capabilities);
  const qaplet_limits *limits=&snapshot.limits;
#define FIELD(name,comma) printf("\"" #name "\":%" PRIu64 comma,limits->name)
  FIELD(engine_heap_bytes,","); FIELD(engine_stack_bytes,","); FIELD(native_storage_bytes,",");
  FIELD(source_bytes,","); FIELD(module_count,","); FIELD(source_cache_bytes,",");
  FIELD(execution_timeout_ms,","); FIELD(execution_dispatches,","); FIELD(event_queue_slots,",");
  FIELD(event_payload_bytes,","); FIELD(bridge_value_bytes,","); FIELD(host_arguments,",");
  FIELD(pump_jobs,","); FIELD(pump_events,"");
#undef FIELD
  puts("}}");
  REQUIRE(qaplet_close(runtime,NULL)==QAPLET_OK && qaplet_destroy(runtime,NULL)==QAPLET_OK);
  return 0;
}

typedef struct { qaplet_runtime *runtime; int passed; } abi_thread;
static void *diagnose(void *opaque) {
  abi_thread *state=opaque;
  char text[32]; qaplet_diagnostic diagnostic=QAPLET_DIAGNOSTIC_INIT;
  diagnostic.message=text; diagnostic.capacity=sizeof(text);
  state->passed=qaplet_post_event(state->runtime,0,0,NULL,1,&diagnostic)==QAPLET_INVALID_ARGUMENT &&
      diagnostic.code==QAPLET_INVALID_ARGUMENT && !strcmp(text,"INVALID_ARGUMENT");
  return NULL;
}

static int abi_contract(void) {
  qaplet_runtime *runtime=NULL;
  struct { uint32_t struct_size,abi_version; uint64_t canary; } prefix={8,1,UINT64_C(0x123456789abcdef0)};
  REQUIRE(qaplet_create((const qaplet_config *)&prefix,&runtime,NULL)==QAPLET_ABI_MISMATCH && !runtime &&
      prefix.canary==UINT64_C(0x123456789abcdef0));
  struct { qaplet_abi value; uint64_t tail; } abi={QAPLET_ABI_INIT,0};
  abi.value.struct_size=sizeof(abi);
  REQUIRE(qaplet_query_abi(&abi.value)==QAPLET_OK && abi.value.capabilities==98303 && !abi.tail);
  abi.tail=1; REQUIRE(qaplet_query_abi(&abi.value)==QAPLET_ABI_MISMATCH);
  abi.tail=0; abi.value.abi_version=2; REQUIRE(qaplet_query_abi(&abi.value)==QAPLET_ABI_MISMATCH);
  qaplet_config config=QAPLET_CONFIG_INIT;
  config.abi_version=2; REQUIRE(qaplet_create(&config,&runtime,NULL)==QAPLET_ABI_MISMATCH && !runtime);
  config=(qaplet_config)QAPLET_CONFIG_INIT; config.limits.struct_size=8;
  REQUIRE(qaplet_create(&config,&runtime,NULL)==QAPLET_ABI_MISMATCH && !runtime);
  config=(qaplet_config)QAPLET_CONFIG_INIT; config.flags=UINT64_C(1)<<63;
  REQUIRE(qaplet_create(&config,&runtime,NULL)==QAPLET_INVALID_ARGUMENT && !runtime);
  config.flags=0; config.reserved[3]=1;
  REQUIRE(qaplet_create(&config,&runtime,NULL)==QAPLET_INVALID_ARGUMENT && !runtime);
  struct { qaplet_config value; uint64_t tail; } extended={QAPLET_CONFIG_INIT,0};
  extended.value.struct_size=sizeof(extended); extended.tail=1;
  REQUIRE(qaplet_create(&extended.value,&runtime,NULL)==QAPLET_ABI_MISMATCH && !runtime);
  extended.tail=0; REQUIRE(qaplet_create(&extended.value,&runtime,NULL)==QAPLET_OK && runtime);
  struct { qaplet_host_module value; uint64_t tail; } module={QAPLET_HOST_MODULE_INIT,0};
  module.value.struct_size=sizeof(module); module.value.name="host:abi"; module.value.name_length=8;
  module.value.flags=1; REQUIRE(qaplet_register_host_module(runtime,&module.value,NULL)==QAPLET_INVALID_ARGUMENT);
  module.value.flags=0; module.value.reserved[1]=1;
  REQUIRE(qaplet_register_host_module(runtime,&module.value,NULL)==QAPLET_INVALID_ARGUMENT);
  module.value.reserved[1]=0; module.tail=1;
  REQUIRE(qaplet_register_host_module(runtime,&module.value,NULL)==QAPLET_ABI_MISMATCH);
  module.tail=0;
  qaplet_host_export item=QAPLET_HOST_EXPORT_INIT;
  item.name="value"; item.name_length=5; item.kind=QAPLET_HOST_CONSTANT;
  module.value.exports=&item; module.value.export_count=1;
  item.reserved=1; REQUIRE(qaplet_register_host_module(runtime,&module.value,NULL)==QAPLET_INVALID_ARGUMENT);
  item.reserved=0; item.value.struct_size=8;
  REQUIRE(qaplet_register_host_module(runtime,&module.value,NULL)==QAPLET_ABI_MISMATCH);
  item.value=(qaplet_value)QAPLET_VALUE_INIT;
  REQUIRE(qaplet_register_host_module(runtime,&module.value,NULL)==QAPLET_OK);
  struct { qaplet_progress value; uint64_t tail; } progress={QAPLET_PROGRESS_INIT,0};
  progress.value.struct_size=sizeof(progress);
  struct { qaplet_execution_options value; uint64_t tail; } options={QAPLET_EXECUTION_OPTIONS_INIT,0};
  options.value.struct_size=sizeof(options); options.value.flags=1;
  REQUIRE(qaplet_eval_with_options(runtime,"42",2,&options.value,&progress.value,NULL)==QAPLET_INVALID_ARGUMENT);
  options.value.flags=0; options.value.reserved[3]=1;
  REQUIRE(qaplet_eval_with_options(runtime,"42",2,&options.value,&progress.value,NULL)==QAPLET_INVALID_ARGUMENT);
  options.value.reserved[3]=0; options.tail=1;
  REQUIRE(qaplet_eval_with_options(runtime,"42",2,&options.value,&progress.value,NULL)==QAPLET_ABI_MISMATCH);
  options.tail=0; options.value.struct_size=8;
  REQUIRE(qaplet_eval_with_options(runtime,"42",2,&options.value,&progress.value,NULL)==QAPLET_ABI_MISMATCH);
  options.value.struct_size=sizeof(options); progress.value.struct_size=8;
  REQUIRE(qaplet_eval(runtime,"globalThis.bad=1",16,&progress.value,NULL)==QAPLET_ABI_MISMATCH);
  progress.value.struct_size=sizeof(progress); progress.tail=1;
  REQUIRE(qaplet_eval(runtime,"globalThis.bad=1",16,&progress.value,NULL)==QAPLET_ABI_MISMATCH);
  progress.tail=0;
  const char untouched[]="typeof bad==='undefined'?42:0";
  REQUIRE(qaplet_eval_with_options(runtime,untouched,sizeof(untouched)-1,&options.value,&progress.value,NULL)==QAPLET_OK);
  struct { qaplet_result value; uint64_t tail; } result={QAPLET_RESULT_INIT,0};
  result.value.struct_size=8; REQUIRE(qaplet_get_result(runtime,&result.value,NULL)==QAPLET_ABI_MISMATCH);
  result.value.struct_size=sizeof(result); result.tail=1;
  REQUIRE(qaplet_get_result(runtime,&result.value,NULL)==QAPLET_ABI_MISMATCH);
  result.tail=0; REQUIRE(qaplet_get_result(runtime,&result.value,NULL)==QAPLET_OK && result.value.number==42 && !result.tail);
  struct { qaplet_snapshot value; uint64_t tail; } snapshot={QAPLET_SNAPSHOT_INIT,0};
  snapshot.value.struct_size=8; REQUIRE(qaplet_get_snapshot(runtime,&snapshot.value,NULL)==QAPLET_ABI_MISMATCH);
  snapshot.value.struct_size=sizeof(snapshot); snapshot.tail=1;
  REQUIRE(qaplet_get_snapshot(runtime,&snapshot.value,NULL)==QAPLET_ABI_MISMATCH);
  snapshot.tail=0; REQUIRE(qaplet_get_snapshot(runtime,&snapshot.value,NULL)==QAPLET_OK && !snapshot.tail);
  char owner_text[128], saved[128];
  struct { qaplet_diagnostic value; uint64_t tail; } diagnostic={QAPLET_DIAGNOSTIC_INIT,0};
  diagnostic.value.struct_size=8; REQUIRE(qaplet_close(runtime,&diagnostic.value)==QAPLET_ABI_MISMATCH);
  diagnostic.value.struct_size=sizeof(diagnostic); diagnostic.tail=1;
  REQUIRE(qaplet_close(runtime,&diagnostic.value)==QAPLET_ABI_MISMATCH);
  diagnostic.tail=0; diagnostic.value.message=owner_text; diagnostic.value.capacity=sizeof(owner_text);
  REQUIRE(qaplet_eval(runtime,"throw 'owner diagnostic'",24,&progress.value,&diagnostic.value)==QAPLET_JS_EXCEPTION);
  REQUIRE(diagnostic.value.code==QAPLET_JS_EXCEPTION && !strcmp(owner_text,"owner diagnostic"));
  memcpy(saved,owner_text,sizeof(saved));
  abi_thread state={runtime,0}; pthread_t thread;
  REQUIRE(!pthread_create(&thread,NULL,diagnose,&state) && !pthread_join(thread,NULL) && state.passed);
  REQUIRE(diagnostic.value.code==QAPLET_JS_EXCEPTION && !memcmp(saved,owner_text,sizeof(saved)));
  char short_text[4]={1,1,1,1}; diagnostic.value.message=short_text; diagnostic.value.capacity=sizeof(short_text);
  REQUIRE(qaplet_eval(runtime,"throw 'bounded diagnostic'",26,&progress.value,&diagnostic.value)==QAPLET_JS_EXCEPTION &&
      diagnostic.value.code==QAPLET_JS_EXCEPTION && diagnostic.value.length==18 &&
      diagnostic.value.flags==QAPLET_DIAGNOSTIC_TRUNCATED && !memcmp(short_text,"bou\0",4));
  diagnostic.value.message=NULL; diagnostic.value.capacity=0;
  REQUIRE(qaplet_eval(runtime,"throw 'zero'",12,&progress.value,&diagnostic.value)==QAPLET_JS_EXCEPTION &&
      diagnostic.value.code==QAPLET_JS_EXCEPTION && diagnostic.value.length==4 && diagnostic.value.flags==QAPLET_DIAGNOSTIC_TRUNCATED);
  REQUIRE(qaplet_close(runtime,NULL)==QAPLET_OK && qaplet_destroy(runtime,NULL)==QAPLET_OK);
  puts("package ABI: prefixes/short structures/zero tails/flags/reserved; independent worker diagnostics: pass");
  return 0;
}

int main(int argc, char **argv) {
  if (argc==2 && !strcmp(argv[1],"--abi")) return finish(abi_contract());
  if (argc==2 && !strcmp(argv[1],"--metadata")) return finish(metadata());
  if (argc==2) return finish(application(argv[1],"success"));
  if (argc==3 && (!strcmp(argv[1],"--reject") || !strcmp(argv[1],"--queue-full") || !strcmp(argv[1],"--deadline")))
    return finish(application(argv[2],argv[1]+2));
  fputs("Usage: qaplet-example [--reject|--queue-full|--deadline] entry.mjs | --metadata\n",stderr);
  return finish(2);
}
