/* SPDX-License-Identifier: MIT */
/* Public-header-only supplied SDK consumer. */
#include "qaplet.h"
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "Host module contract line %d: %s\n", __LINE__, #x); return 1; } } while (0)

typedef struct { unsigned calls, failures; uint64_t expired; } host_state;

static qaplet_code echo(qaplet_runtime *runtime, uint64_t call, size_t argc,
                        const qaplet_value *argv, void *opaque) {
  host_state *state = opaque;
  ++state->calls; state->expired = call;
  if (argc != 1) return QAPLET_HOST_ERROR;
  qaplet_code code = qaplet_host_set_result(runtime, call, argv, NULL);
  if (!code && qaplet_host_set_result(runtime, call, argv, NULL) != QAPLET_BAD_STATE) ++state->failures;
  return code;
}
static qaplet_code empty(qaplet_runtime *runtime, uint64_t call, size_t argc,
                         const qaplet_value *argv, void *opaque) {
  (void)runtime; (void)call; (void)argc; (void)argv;
  ++((host_state *)opaque)->calls;
  return QAPLET_OK;
}
static qaplet_code fail(qaplet_runtime *runtime, uint64_t call, size_t argc,
                        const qaplet_value *argv, void *opaque) {
  (void)runtime; (void)call; (void)argc; (void)argv;
  ++((host_state *)opaque)->calls;
  return QAPLET_HOST_ERROR;
}
static qaplet_code temporary(qaplet_runtime *runtime, uint64_t call, size_t argc,
                             const qaplet_value *argv, void *opaque) {
  (void)argc; (void)argv;
  host_state *state = opaque; ++state->calls;
  char text[] = {'c', 0, 'd'};
  qaplet_value value = QAPLET_VALUE_INIT;
  value.type = QAPLET_VALUE_STRING; value.data = text; value.length = sizeof(text);
  qaplet_code code = qaplet_host_set_result(runtime, call, &value, NULL);
  memset(text, 'x', sizeof(text));
  return code;
}
static qaplet_code reenter(qaplet_runtime *runtime, uint64_t call, size_t argc,
                           const qaplet_value *argv, void *opaque) {
  (void)argc; (void)argv;
  host_state *state = opaque; ++state->calls;
  qaplet_value stale = QAPLET_VALUE_INIT;
  if (state->expired && qaplet_host_set_result(runtime, state->expired, &stale, NULL) != QAPLET_BAD_STATE)
    ++state->failures;
  qaplet_progress progress = QAPLET_PROGRESS_INIT;
  qaplet_result result = QAPLET_RESULT_INIT;
  qaplet_snapshot snapshot = QAPLET_SNAPSHOT_INIT;
  qaplet_host_module module = QAPLET_HOST_MODULE_INIT;
  module.name = "host:later"; module.name_length = 10;
  qaplet_value value = QAPLET_VALUE_INIT; value.type = QAPLET_VALUE_BOOL; value.boolean = 1;
  qaplet_value invalid = QAPLET_VALUE_INIT;
  const char invalid_text[] = {(char)0xed, (char)0xa0, (char)0x80};
  invalid.type = QAPLET_VALUE_STRING; invalid.data = invalid_text; invalid.length = sizeof(invalid_text);
  if (qaplet_host_set_result(runtime, call, &invalid, NULL) != QAPLET_INVALID_UTF8) ++state->failures;
  if (qaplet_eval(runtime, "globalThis.reentered=1", 21, &progress, NULL) != QAPLET_BUSY ||
      qaplet_pump(runtime, &progress, NULL) != QAPLET_BUSY ||
      qaplet_run_entry(runtime, "file.js", 7, &progress, NULL) != QAPLET_BUSY ||
      qaplet_run_entry_with_options(runtime, "file.js", 7, NULL, &progress, NULL) != QAPLET_BUSY ||
      qaplet_eval_with_base_and_options(runtime, "0", 1, "file:///eval.js", 15, NULL, &progress, NULL) != QAPLET_BUSY ||
      qaplet_register_host_module(runtime, &module, NULL) != QAPLET_BUSY ||
      qaplet_get_result(runtime, &result, NULL) != QAPLET_BUSY ||
      qaplet_close(runtime, NULL) != QAPLET_BUSY || qaplet_destroy(runtime, NULL) != QAPLET_BUSY ||
      qaplet_get_snapshot(runtime, &snapshot, NULL) != QAPLET_OK ||
      snapshot.phase != QAPLET_PHASE_RUNNING) ++state->failures;
  return qaplet_host_set_result(runtime, call, &value, NULL);
}

static int finish(qaplet_runtime *runtime, qaplet_progress *progress, qaplet_code code) {
  if (code) { char text[1024]; qaplet_diagnostic diagnostic = QAPLET_DIAGNOSTIC_INIT;
    diagnostic.message = text; diagnostic.capacity = sizeof(text); qaplet_result result = QAPLET_RESULT_INIT;
    qaplet_get_result(runtime, &result, &diagnostic); fprintf(stderr, "execution code %d: %s\n", code, text); }
  CHECK(code == QAPLET_OK);
  unsigned pumps = 0;
  while (progress->phase == QAPLET_PHASE_PENDING && pumps++ < 20)
    CHECK(qaplet_pump(runtime, progress, NULL) == QAPLET_OK);
  CHECK(progress->phase == QAPLET_PHASE_DONE && progress->code == QAPLET_OK);
  return 0;
}
static int check_js(qaplet_runtime *runtime, const char *text) {
  qaplet_progress progress = QAPLET_PROGRESS_INIT;
  qaplet_result result = QAPLET_RESULT_INIT;
  CHECK(!finish(runtime, &progress, qaplet_eval(runtime, text, strlen(text), &progress, NULL)));
  CHECK(qaplet_get_result(runtime, &result, NULL) == QAPLET_OK);
  CHECK(result.type == QAPLET_VALUE_BOOL && result.boolean);
  return 0;
}
static int release(qaplet_runtime *runtime) {
  CHECK(qaplet_close(runtime, NULL) == QAPLET_OK);
  CHECK(qaplet_close(runtime, NULL) == QAPLET_OK);
  CHECK(qaplet_destroy(runtime, NULL) == QAPLET_OK);
  return 0;
}
static int install(qaplet_runtime *runtime, host_state *state) {
  char name[] = "host:bridge", export_name[] = "greeting", text[] = {'a', 0, 'b'};
  unsigned char bytes[] = {0, 255, 17};
  qaplet_host_export exports[8];
  for (size_t i = 0; i < 8; ++i) exports[i] = (qaplet_host_export)QAPLET_HOST_EXPORT_INIT;
  const char *names[] = {export_name, "bytes", "echo", "empty", "fail", "temporary", "reenter", "max"};
  for (size_t i = 0; i < 8; ++i) { exports[i].name = names[i]; exports[i].name_length = strlen(names[i]); }
  exports[0].value.type = QAPLET_VALUE_STRING; exports[0].value.data = text; exports[0].value.length = sizeof(text);
  exports[1].value.type = QAPLET_VALUE_BYTES; exports[1].value.data = bytes; exports[1].value.length = sizeof(bytes);
  qaplet_host_function functions[] = {echo, empty, fail, temporary, reenter};
  for (size_t i = 2; i < 7; ++i) { exports[i].kind = QAPLET_HOST_FUNCTION; exports[i].function = functions[i-2]; exports[i].userdata = state; }
  exports[7].value.type = QAPLET_VALUE_UINT64; exports[7].value.unsigned_integer = UINT64_MAX;
  qaplet_host_module module = QAPLET_HOST_MODULE_INIT;
  module.name = name; module.name_length = strlen(name); module.exports = exports; module.export_count = 8;
  CHECK(qaplet_register_host_module(runtime, &module, NULL) == QAPLET_OK);
  CHECK(qaplet_register_host_module(runtime, &module, NULL) == QAPLET_INVALID_ARGUMENT);
  memset(name, 'x', sizeof(name)); memset(export_name, 'x', sizeof(export_name));
  memset(text, 'x', sizeof(text)); memset(bytes, 42, sizeof(bytes)); memset(exports, 0, sizeof(exports));
  return 0;
}

typedef struct { qaplet_runtime *runtime; int failed; } foreign_state;
static void *foreign(void *opaque) {
  foreign_state *state = opaque;
  qaplet_host_module module = QAPLET_HOST_MODULE_INIT; module.name = "host:foreign"; module.name_length = 12;
  qaplet_value value = QAPLET_VALUE_INIT;
  state->failed = qaplet_register_host_module(state->runtime, &module, NULL) != QAPLET_WRONG_THREAD ||
      qaplet_host_set_result(state->runtime, 1, &value, NULL) != QAPLET_WRONG_THREAD;
  return NULL;
}
static int registration(void) {
  qaplet_config config = QAPLET_CONFIG_INIT; qaplet_runtime *runtime = NULL;
  CHECK(qaplet_create(&config, &runtime, NULL) == QAPLET_OK);
  foreign_state state = {runtime, 0}; pthread_t thread;
  CHECK(!pthread_create(&thread, NULL, foreign, &state)); CHECK(!pthread_join(thread, NULL)); CHECK(!state.failed);
  qaplet_host_export exports[2] = {QAPLET_HOST_EXPORT_INIT, QAPLET_HOST_EXPORT_INIT};
  exports[0].name = exports[1].name = "a"; exports[0].name_length = exports[1].name_length = 1;
  qaplet_host_module module = QAPLET_HOST_MODULE_INIT;
  module.name = "host:retry"; module.name_length = 10; module.exports = exports; module.export_count = 2;
  CHECK(qaplet_register_host_module(runtime, &module, NULL) == QAPLET_INVALID_ARGUMENT);
  exports[1].name = "b";
  const char invalid[] = {(char)0xc0, (char)0x80};
  exports[1].value.type = QAPLET_VALUE_STRING; exports[1].value.data = invalid; exports[1].value.length = 2;
  CHECK(qaplet_register_host_module(runtime, &module, NULL) == QAPLET_INVALID_UTF8);
  exports[1].value.type = QAPLET_VALUE_BYTES; exports[1].value.length = QAPLET_VALUE_BYTES_MAX + 1u;
  CHECK(qaplet_register_host_module(runtime, &module, NULL) == QAPLET_VALUE_TOO_LARGE);
  exports[1].value = (qaplet_value)QAPLET_VALUE_INIT;
  module.name = "qaplet:retry"; module.name_length = 12;
  CHECK(qaplet_register_host_module(runtime, &module, NULL) == QAPLET_INVALID_ARGUMENT);
  module.name = "host:a\0b"; module.name_length = 8;
  CHECK(qaplet_register_host_module(runtime, &module, NULL) == QAPLET_INVALID_ARGUMENT);
  module.name = "host:retry"; module.name_length = 10;
  qaplet_snapshot snapshot = QAPLET_SNAPSHOT_INIT;
  CHECK(qaplet_get_snapshot(runtime, &snapshot, NULL) == QAPLET_OK && snapshot.module_count == 2);
  CHECK(qaplet_register_host_module(runtime, &module, NULL) == QAPLET_OK);
  module.name = "host:中文"; module.name_length = strlen(module.name);
  CHECK(qaplet_register_host_module(runtime, &module, NULL) == QAPLET_OK);
  qaplet_progress progress = QAPLET_PROGRESS_INIT;
  CHECK(qaplet_pump(runtime, &progress, NULL) == QAPLET_OK);
  module.name = "host:late"; module.name_length = 9;
  CHECK(qaplet_register_host_module(runtime, &module, NULL) == QAPLET_BAD_STATE);
  CHECK(!release(runtime));
  config.limits.module_count = 3;
  CHECK(qaplet_create(&config, &runtime, NULL) == QAPLET_OK);
  CHECK(qaplet_register_host_module(runtime, &module, NULL) == QAPLET_OK);
  module.name = "host:other"; module.name_length = 10;
  CHECK(qaplet_register_host_module(runtime, &module, NULL) == QAPLET_MODULE_LIMIT);
  CHECK(qaplet_get_snapshot(runtime, &snapshot, NULL) == QAPLET_OK && snapshot.lifecycle == QAPLET_OPEN && snapshot.module_count == 3);
  return release(runtime);
}

static int allocation_failure(void) {
  qaplet_config config = QAPLET_CONFIG_INIT;
  qaplet_runtime *runtime = NULL; host_state state = {0};
  qaplet_progress progress = QAPLET_PROGRESS_INIT;
  qaplet_snapshot baseline = QAPLET_SNAPSHOT_INIT;
  CHECK(qaplet_create(&config, &runtime, NULL) == QAPLET_OK);
  CHECK(qaplet_get_snapshot(runtime, &baseline, NULL) == QAPLET_OK);
  CHECK(!release(runtime));
  config.limits.native_storage_bytes = baseline.native_current_bytes + 65536;
  CHECK(qaplet_create(&config, &runtime, NULL) == QAPLET_OK);
  CHECK(!install(runtime, &state));
  const char *load = "import('host:bridge').then(m=>{globalThis.h=m})";
  CHECK(!finish(runtime, &progress, qaplet_eval(runtime, load, strlen(load), &progress, NULL)));
  const char *source = "try{h.echo('x'.repeat(200000))}catch(e){globalThis.caught=e.code===11} true";
  CHECK(qaplet_eval(runtime, source, strlen(source), &progress, NULL) == QAPLET_MEMORY_LIMIT);
  CHECK(progress.phase == QAPLET_PHASE_DONE && progress.code == QAPLET_MEMORY_LIMIT && !state.calls);
  qaplet_snapshot snapshot = QAPLET_SNAPSHOT_INIT; qaplet_result result = QAPLET_RESULT_INIT;
  CHECK(qaplet_get_snapshot(runtime, &snapshot, NULL) == QAPLET_OK && snapshot.lifecycle == QAPLET_FAILED && snapshot.termination_reason == QAPLET_MEMORY_LIMIT);
  CHECK(qaplet_get_result(runtime, &result, NULL) == QAPLET_MEMORY_LIMIT && result.code == QAPLET_MEMORY_LIMIT);
  CHECK(qaplet_eval(runtime, "1", 1, &progress, NULL) == QAPLET_BAD_STATE);
  return release(runtime);
}

int sdk_host_modules(const char *root) {
  CHECK(!registration());
  CHECK(!allocation_failure());
  qaplet_abi abi = QAPLET_ABI_INIT;
  CHECK(qaplet_query_abi(&abi) == QAPLET_OK && (abi.capabilities & QAPLET_CAP_HOST_MODULES));
  qaplet_config config = QAPLET_CONFIG_INIT;
  qaplet_runtime *runtime = NULL;
  qaplet_progress progress = QAPLET_PROGRESS_INIT;
  host_state state = {0};
  CHECK(qaplet_create(&config, &runtime, NULL) == QAPLET_OK);
  CHECK(!install(runtime, &state));
  qaplet_snapshot snapshot = QAPLET_SNAPSHOT_INIT;
  CHECK(qaplet_get_snapshot(runtime, &snapshot, NULL) == QAPLET_OK && snapshot.module_count == 3);
  char path[4096]; CHECK(snprintf(path, sizeof(path), "%s/host-bridge.mjs", root) < sizeof(path));
  CHECK(!finish(runtime, &progress, qaplet_run_entry(runtime, path, strlen(path), &progress, NULL)));
  CHECK(!check_js(runtime, "h.greeting==='a\\0b' && h.bytes.join(',')==='0,255,17' && h.max===18446744073709551615n"));
  CHECK(!check_js(runtime, "h.echo(undefined)===undefined && h.echo(null)===null && h.echo(true)===true && h.echo(false)===false && h.echo(1.25)===1.25 && Object.is(h.echo(-0),-0) && Number.isNaN(h.echo(NaN)) && h.echo(Infinity)===Infinity"));
  CHECK(!check_js(runtime, "h.echo(-9223372036854775808n)===-9223372036854775808n && h.echo(9223372036854775807n)===9223372036854775807n && h.echo(18446744073709551615n)===18446744073709551615n && h.echo('x\\0😀')==='x\\0😀'"));
  CHECK(!check_js(runtime, "(()=>{let a=new Uint8Array([9,2,3,9]);let b=h.echo(a.subarray(1,3));a[1]=99;return b.join(',')==='2,3' && h.temporary()==='c\\0d' && h.empty()===undefined})()"));
  qaplet_value value = QAPLET_VALUE_INIT;
  CHECK(qaplet_host_set_result(runtime, state.expired, &value, NULL) == QAPLET_BAD_STATE);
  CHECK(qaplet_host_set_result(runtime, 0, &value, NULL) == QAPLET_BAD_STATE);
  CHECK(!check_js(runtime, "h.reenter()===true && !('reentered' in globalThis)"));
  CHECK(!check_js(runtime, "(()=>{let n=0;Object.defineProperty(Error.prototype,'code',{set(){n++},configurable:true});try{h.fail()}catch(e){return e instanceof Error && e.code===19 && n===0}return false})()"));
  unsigned before = state.calls;
  CHECK(!check_js(runtime, "(()=>{globalThis.hooks=0;let bad=[{},()=>{},Symbol(),new Number(2),Object(3n),new Proxy(new Uint8Array(1),{}),{toString(){hooks++;return 'x'}}];return bad.every(v=>{try{h.echo(v)}catch(e){return e.code===8}return false}) && hooks===0})()"));
  CHECK(state.calls == before);
  CHECK(!check_js(runtime, "(()=>{let bad=[-9223372036854775809n,18446744073709551616n,2n**128n+42n];return bad.every(v=>{try{h.echo(v)}catch(e){return e.code===8}return false})})()"));
  CHECK(!check_js(runtime, "(()=>{for(let s of ['\\ud800','\\udc00']){try{h.echo(s);return false}catch(e){if(e.code!==12)return false}}return true})()"));
  CHECK(state.calls == before);
  CHECK(!check_js(runtime, "(()=>{let b=new ArrayBuffer(8);let a=new Uint8Array(b);b.transfer();let shared=new Uint8Array(new SharedArrayBuffer(8));return [a,shared].every(v=>{try{h.echo(v)}catch(e){return e.code===8}return false})})()"));
  CHECK(state.calls == before);
  CHECK(!check_js(runtime, "(()=>{try{h.empty(...Array(33));return false}catch(e){if(e.code!==13)return false}return h.empty(...Array(32))===undefined})()"));
  CHECK(state.calls == before + 1);
  CHECK(!check_js(runtime, "(()=>{let s='x'.repeat(1048576);let a=new Uint8Array(1048576);a[1048575]=255;return h.echo(s)===s && h.echo(a)[1048575]===255})()"));
  before = state.calls;
  CHECK(!check_js(runtime, "(()=>{for(let v of ['x'.repeat(1048577),new Uint8Array(1048577)]){try{h.echo(v);return false}catch(e){if(e.code!==13)return false}}return true})()"));
  CHECK(state.calls == before);
  CHECK(!check_js(runtime, "h.echo(42)===42"));
  CHECK(qaplet_eval(runtime, "h.fail()", 8, &progress, NULL) == QAPLET_JS_EXCEPTION);
  CHECK(!check_js(runtime, "h.echo(43)===43"));
  CHECK(!state.failures && state.calls > 15);
  qaplet_host_module late = QAPLET_HOST_MODULE_INIT; late.name = "host:late"; late.name_length = 9;
  CHECK(qaplet_register_host_module(runtime, &late, NULL) == QAPLET_BAD_STATE);
  CHECK(!release(runtime));
  puts("host modules: copied registration; synchronous typed callback; catchable errors; guarded lifetime: pass");
  return 0;
}
