/* SPDX-License-Identifier: MIT */
/* Included by the supplied C Host; only qaplet.h is used for product access. */
#include <math.h>
#include <stdlib.h>
static int integer_values(void) {
  qaplet_config config = QAPLET_CONFIG_INIT;
  qaplet_runtime *runtime = NULL;
  qaplet_progress progress = QAPLET_PROGRESS_INIT;
  qaplet_result result = QAPLET_RESULT_INIT;
  CHECK(qaplet_create(&config, &runtime, NULL) == QAPLET_OK);
  const struct { const char *source; qaplet_value_type type; int64_t signed_value; uint64_t unsigned_value; } cases[] = {
    {"-9223372036854775808n", QAPLET_VALUE_INT64, INT64_MIN, 0},
    {"9223372036854775807n", QAPLET_VALUE_INT64, INT64_MAX, 0},
    {"9007199254740993n", QAPLET_VALUE_INT64, INT64_C(9007199254740993), 0},
    {"9223372036854775808n", QAPLET_VALUE_UINT64, 0, UINT64_C(9223372036854775808)},
    {"18446744073709551615n", QAPLET_VALUE_UINT64, 0, UINT64_MAX},
    {"0n", QAPLET_VALUE_INT64, 0, 0},
  };
  for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
    CHECK(qaplet_eval(runtime, cases[i].source, strlen(cases[i].source), &progress, NULL) == QAPLET_OK);
    CHECK(qaplet_get_result(runtime, &result, NULL) == QAPLET_OK && result.type == cases[i].type);
    CHECK(result.type == QAPLET_VALUE_INT64 ? result.signed_integer == cases[i].signed_value :
          result.unsigned_integer == cases[i].unsigned_value);
  }
  const char *rejected[] = {"-9223372036854775809n", "18446744073709551616n", "1n << 200n", "-(1n << 200n)"};
  for (size_t i = 0; i < sizeof(rejected) / sizeof(rejected[0]); ++i) {
    CHECK(qaplet_eval(runtime, rejected[i], strlen(rejected[i]), &progress, NULL) == QAPLET_VALUE_TYPE_UNSUPPORTED);
    CHECK(qaplet_get_result(runtime, &result, NULL) == QAPLET_VALUE_TYPE_UNSUPPORTED);
  }
  const struct { const char *source; qaplet_value_type type; unsigned boolean; } scalars[] = {
    {"undefined",QAPLET_VALUE_UNDEFINED,0}, {"null",QAPLET_VALUE_NULL,0},
    {"true",QAPLET_VALUE_BOOL,1}, {"false",QAPLET_VALUE_BOOL,0},
  };
  for (size_t i = 0; i < sizeof(scalars)/sizeof(scalars[0]); ++i) {
    CHECK(qaplet_eval(runtime,scalars[i].source,strlen(scalars[i].source),&progress,NULL) == QAPLET_OK);
    CHECK(qaplet_get_result(runtime,&result,NULL) == QAPLET_OK && result.type == scalars[i].type && result.boolean == scalars[i].boolean);
  }
  const char *numbers[] = {"NaN", "Infinity", "-Infinity", "-0", "1.25"};
  for (size_t i = 0; i < sizeof(numbers)/sizeof(numbers[0]); ++i) {
    CHECK(qaplet_eval(runtime,numbers[i],strlen(numbers[i]),&progress,NULL) == QAPLET_OK);
    CHECK(qaplet_get_result(runtime,&result,NULL) == QAPLET_OK && result.type == QAPLET_VALUE_DOUBLE);
    CHECK(i == 0 ? isnan(result.number) : i == 1 ? isinf(result.number) && result.number > 0 :
          i == 2 ? isinf(result.number) && result.number < 0 : i == 3 ? result.number == 0 && signbit(result.number) : result.number == 1.25);
  }
  CHECK(qaplet_close(runtime, NULL) == QAPLET_OK && qaplet_destroy(runtime, NULL) == QAPLET_OK);
  puts("integers: exact signed/unsigned 64-bit boundaries; oversized BigInt rejected");
  return 0;
}

static int string_values(void) {
  qaplet_config config = QAPLET_CONFIG_INIT;
  qaplet_runtime *runtime = NULL;
  qaplet_progress progress = QAPLET_PROGRESS_INIT;
  qaplet_result result = QAPLET_RESULT_INIT;
  CHECK(qaplet_create(&config, &runtime, NULL) == QAPLET_OK);
  const char source[] = "globalThis.copies = 1; 'A\\0\\u00e9\\u{1f600}'";
  const unsigned char expected[] = {'A', 0, 0xc3, 0xa9, 0xf0, 0x9f, 0x98, 0x80};
  unsigned char output[sizeof(expected) + 2];
  memset(output, 0x55, sizeof(output));
  CHECK(qaplet_eval(runtime, source, sizeof(source) - 1, &progress, NULL) == QAPLET_OK);
  CHECK(qaplet_get_result(runtime, &result, NULL) == QAPLET_BUFFER_TOO_SMALL);
  CHECK(result.type == QAPLET_VALUE_STRING && result.length == sizeof(expected));
  result.buffer = output;
  result.capacity = sizeof(expected) - 1;
  CHECK(qaplet_get_result(runtime, &result, NULL) == QAPLET_BUFFER_TOO_SMALL && output[0] == 0x55);
  result.capacity = sizeof(expected);
  CHECK(qaplet_get_result(runtime, &result, NULL) == QAPLET_OK);
  CHECK(result.buffer == output && result.capacity == sizeof(expected) && result.length == sizeof(expected));
  CHECK(!memcmp(output, expected, sizeof(expected)) && output[sizeof(expected)] == 0x55);
  output[0] = 0;
  CHECK(qaplet_get_result(runtime, &result, NULL) == QAPLET_OK && output[0] == 'A');
  CHECK(qaplet_eval(runtime, NULL, 1, &progress, NULL) == QAPLET_INVALID_ARGUMENT);
  CHECK(qaplet_get_result(runtime, &result, NULL) == QAPLET_OK && !memcmp(output, expected, sizeof(expected)));
  const unsigned char invalid[][4] = {{0xc0,0xaf,0,0},{0xed,0xa0,0x80,0},{0xf4,0x90,0x80,0x80},{0xe2,0x82,0,0},{0x80,0,0,0}};
  const size_t lengths[] = {2,3,4,2,1};
  for (size_t i = 0; i < sizeof(lengths)/sizeof(lengths[0]); ++i) {
    CHECK(qaplet_eval(runtime, (const char *)invalid[i], lengths[i], &progress, NULL) == QAPLET_INVALID_UTF8);
    CHECK(qaplet_get_result(runtime, &result, NULL) == QAPLET_OK && !memcmp(output, expected, sizeof(expected)));
  }
  const char *surrogates[] = {"'\\ud800'", "'\\udc00'", "'a\\ud800b'"};
  for (size_t i = 0; i < sizeof(surrogates)/sizeof(surrogates[0]); ++i) {
    CHECK(qaplet_eval(runtime, surrogates[i], strlen(surrogates[i]), &progress, NULL) == QAPLET_INVALID_UTF8);
    CHECK(qaplet_get_result(runtime, &result, NULL) == QAPLET_INVALID_UTF8);
  }
  const char oversized[] = "'x'.repeat(1048577)";
  CHECK(qaplet_eval(runtime, oversized, sizeof(oversized)-1, &progress, NULL) == QAPLET_VALUE_TOO_LARGE);
  const char encoded_oversized[] = "'\\u0800'.repeat(349526)";
  CHECK(qaplet_eval(runtime, encoded_oversized, sizeof(encoded_oversized)-1, &progress, NULL) == QAPLET_VALUE_TOO_LARGE);
  const char empty[] = "''";
  CHECK(qaplet_eval(runtime, empty, sizeof(empty)-1, &progress, NULL) == QAPLET_OK);
  result.buffer = NULL; result.capacity = 0;
  CHECK(qaplet_get_result(runtime, &result, NULL) == QAPLET_OK && result.length == 0);
  const char count[] = "copies";
  CHECK(qaplet_eval(runtime, count, sizeof(count)-1, &progress, NULL) == QAPLET_OK);
  CHECK(qaplet_get_result(runtime, &result, NULL) == QAPLET_OK && result.number == 1);
  CHECK(qaplet_close(runtime, NULL) == QAPLET_OK && qaplet_destroy(runtime, NULL) == QAPLET_OK);
  puts("strings: strict UTF-8 and NUL; copied retry; accepted/rejected result lifetime; byte limit");
  return 0;
}

static int byte_values(void) {
  qaplet_config config = QAPLET_CONFIG_INIT;
  qaplet_runtime *runtime = NULL;
  qaplet_progress progress = QAPLET_PROGRESS_INIT;
  qaplet_result result = QAPLET_RESULT_INIT;
  unsigned char output[8];
  result.buffer = output; result.capacity = sizeof(output);
  CHECK(qaplet_create(&config, &runtime, NULL) == QAPLET_OK);
  const struct { const char *source; size_t length; unsigned char expected[4]; } cases[] = {
    {"new Uint8Array([0,127,128,255])", 4, {0,127,128,255}},
    {"new Uint8Array([9,1,2,3,9]).subarray(1,4)", 3, {1,2,3}},
    {"new (class extends Uint8Array {})([1,2,3])", 3, {1,2,3}},
    {"new Uint8Array(0)", 0, {0}},
    {"globalThis.rab = new ArrayBuffer(2,{maxByteLength:8}); globalThis.view=new Uint8Array(rab); rab.resize(4); view.set([1,2,3,4]); view", 4, {1,2,3,4}},
    {"rab.resize(1); view", 1, {1}},
    {"globalThis.guarded=new Uint8Array([1,2]); Object.defineProperty(guarded,'byteLength',{get(){throw 1}}); guarded", 2, {1,2}},
    {"Uint8Array=function(){throw 2}; Object.defineProperty(Object.getPrototypeOf(Object.getPrototypeOf(guarded)), 'byteLength',{get(){throw 3}}); guarded", 2, {1,2}},
  };
  for (size_t i = 0; i < sizeof(cases)/sizeof(cases[0]); ++i) {
    memset(output, 0x55, sizeof(output));
    CHECK(qaplet_eval(runtime, cases[i].source, strlen(cases[i].source), &progress, NULL) == QAPLET_OK);
    CHECK(qaplet_get_result(runtime, &result, NULL) == QAPLET_OK);
    CHECK(result.type == QAPLET_VALUE_BYTES && result.length == cases[i].length);
    CHECK(!memcmp(output, cases[i].expected, cases[i].length) && output[cases[i].length] == 0x55);
    if (cases[i].length) {
      output[0] = 99;
      result.capacity = cases[i].length - 1;
      CHECK(qaplet_get_result(runtime, &result, NULL) == QAPLET_BUFFER_TOO_SMALL && output[0] == 99);
      result.capacity = sizeof(output);
      CHECK(qaplet_get_result(runtime, &result, NULL) == QAPLET_OK && output[0] == cases[i].expected[0]);
    }
  }
  CHECK(qaplet_close(runtime, NULL) == QAPLET_OK && qaplet_destroy(runtime, NULL) == QAPLET_OK);
  CHECK(qaplet_create(&config, &runtime, NULL) == QAPLET_OK);
  const char *rejected[] = {
    "new Int8Array([1])", "new Uint8ClampedArray([1])", "new Uint16Array([1])", "new DataView(new ArrayBuffer(1))",
    "new ArrayBuffer(1)", "new Uint8Array(new SharedArrayBuffer(1))",
    "globalThis.detached=new ArrayBuffer(1); globalThis.old=new Uint8Array(detached); detached.transfer(); old",
    "globalThis.shrunk=new ArrayBuffer(4,{maxByteLength:8}); globalThis.fixed=new Uint8Array(shrunk,2,2); shrunk.resize(1); fixed",
    "globalThis.traps=0; new Proxy(new Uint8Array([1]),{get(){traps++;throw 1},getPrototypeOf(){traps++;throw 2}})",
    "({get then(){traps++;throw 1},toString(){traps++;throw 2}})", "(function(){})", "Symbol('x')", "new Number(1)"
  };
  for (size_t i = 0; i < sizeof(rejected)/sizeof(rejected[0]); ++i) {
    CHECK(qaplet_eval(runtime, rejected[i], strlen(rejected[i]), &progress, NULL) == QAPLET_VALUE_TYPE_UNSUPPORTED);
    CHECK(qaplet_get_result(runtime, &result, NULL) == QAPLET_VALUE_TYPE_UNSUPPORTED);
  }
  const char traps[] = "traps";
  CHECK(qaplet_eval(runtime, traps, sizeof(traps)-1, &progress, NULL) == QAPLET_OK);
  CHECK(qaplet_get_result(runtime, &result, NULL) == QAPLET_OK && result.number == 0);
  const char oversized[] = "new Uint8Array(1048577)";
  CHECK(qaplet_eval(runtime, oversized, sizeof(oversized)-1, &progress, NULL) == QAPLET_VALUE_TOO_LARGE);
  CHECK(qaplet_close(runtime, NULL) == QAPLET_OK && qaplet_destroy(runtime, NULL) == QAPLET_OK);
  puts("bytes: copied Uint8Array views; resize/detach/brand/shared boundaries; no user coercion");
  return 0;
}

typedef struct { qaplet_runtime *runtime; int passed; } diagnostic_worker_state;
static void *diagnostic_worker(void *opaque) {
  diagnostic_worker_state *state = opaque;
  char message[32];
  qaplet_diagnostic diagnostic = QAPLET_DIAGNOSTIC_INIT;
  qaplet_progress progress = QAPLET_PROGRESS_INIT;
  diagnostic.message = message; diagnostic.capacity = sizeof(message);
  state->passed = 1;
  for (unsigned i = 0; i < 100; ++i) {
    if (qaplet_eval(state->runtime, "1", 1, &progress, &diagnostic) != QAPLET_WRONG_THREAD ||
        diagnostic.code != QAPLET_WRONG_THREAD || strcmp(message, "WRONG_THREAD")) state->passed = 0;
  }
  return NULL;
}

static int safe_diagnostics(void) {
  qaplet_config config = QAPLET_CONFIG_INIT;
  qaplet_runtime *runtime = NULL;
  qaplet_progress progress = QAPLET_PROGRESS_INIT;
  qaplet_result result = QAPLET_RESULT_INIT;
  char message[1100], tiny[4] = {1,1,1,1};
  qaplet_diagnostic diagnostic = QAPLET_DIAGNOSTIC_INIT;
  diagnostic.message = message; diagnostic.capacity = sizeof(message);
  CHECK(qaplet_create(&config, &runtime, NULL) == QAPLET_OK);
  const char safe[] = "globalThis.diagTraps=0; throw new Error('safe message')";
  CHECK(qaplet_eval(runtime, safe, sizeof(safe)-1, &progress, &diagnostic) == QAPLET_JS_EXCEPTION);
  CHECK(diagnostic.code == QAPLET_JS_EXCEPTION && !strcmp(message,"safe message") && diagnostic.length == 12 && !diagnostic.flags);
  CHECK(qaplet_get_result(runtime, &result, &diagnostic) == QAPLET_JS_EXCEPTION && !strcmp(message,"safe message"));
  diagnostic.message = tiny; diagnostic.capacity = sizeof(tiny);
  CHECK(qaplet_get_result(runtime, &result, &diagnostic) == QAPLET_JS_EXCEPTION);
  CHECK(diagnostic.code == QAPLET_JS_EXCEPTION && diagnostic.length == 12 && diagnostic.flags == QAPLET_DIAGNOSTIC_TRUNCATED);
  CHECK(!memcmp(tiny,"saf\0",4));
  diagnostic.message = NULL; diagnostic.capacity = 0;
  CHECK(qaplet_get_result(runtime, &result, &diagnostic) == QAPLET_JS_EXCEPTION && diagnostic.code == QAPLET_JS_EXCEPTION && diagnostic.length == 12 && diagnostic.flags);
  diagnostic.message = tiny; diagnostic.capacity = 1; tiny[0] = 1;
  CHECK(qaplet_get_result(runtime, &result, &diagnostic) == QAPLET_JS_EXCEPTION && tiny[0] == 0);
  const char unicode[] = "throw '\\u00e9\\u{1f600}'";
  diagnostic.capacity = 2;
  CHECK(qaplet_eval(runtime, unicode, sizeof(unicode)-1, &progress, &diagnostic) == QAPLET_JS_EXCEPTION);
  CHECK(tiny[0] == 0 && diagnostic.length == 6 && diagnostic.flags == QAPLET_DIAGNOSTIC_TRUNCATED);
  diagnostic.message = message; diagnostic.capacity = sizeof(message);
  const char *unsafe[] = {
    "throw {get message(){diagTraps++;return 'bad'},toString(){diagTraps++;return 'bad'}}",
    "throw new Proxy(new Error('bad'),{get(){diagTraps++;throw 1},getOwnPropertyDescriptor(){diagTraps++;throw 2},getPrototypeOf(){diagTraps++;throw 3}})",
    "globalThis.e=new Error(); Object.defineProperty(e,'message',{get(){diagTraps++;return 'bad'}}); throw e",
    "e=new Error(); e.message={toString(){diagTraps++;return 'bad'}}; throw e",
    "throw {get name(){diagTraps++;throw 1},get stack(){diagTraps++;throw 2}}"
  };
  for (size_t i = 0; i < sizeof(unsafe)/sizeof(unsafe[0]); ++i) {
    CHECK(qaplet_eval(runtime, unsafe[i], strlen(unsafe[i]), &progress, &diagnostic) == QAPLET_JS_EXCEPTION);
    CHECK(diagnostic.code == QAPLET_JS_EXCEPTION && !strcmp(message,"JS_EXCEPTION"));
  }
  const char primitive[] = "throw 'plain\\0text'";
  CHECK(qaplet_eval(runtime, primitive, sizeof(primitive)-1, &progress, &diagnostic) == QAPLET_JS_EXCEPTION);
  CHECK(!strcmp(message,"plain\\0text"));
  const char malformed[] = "throw '\\ud800'";
  CHECK(qaplet_eval(runtime, malformed, sizeof(malformed)-1, &progress, &diagnostic) == QAPLET_JS_EXCEPTION);
  CHECK(!strcmp(message,"JS_EXCEPTION"));
  const char large[] = "throw 'x'.repeat(2000)";
  CHECK(qaplet_eval(runtime, large, sizeof(large)-1, &progress, &diagnostic) == QAPLET_JS_EXCEPTION);
  CHECK(diagnostic.flags == QAPLET_DIAGNOSTIC_TRUNCATED && diagnostic.length > 0 && diagnostic.length < 1100);
  CHECK(message[diagnostic.length] == 0);
  pthread_t thread;
  diagnostic_worker_state state = {runtime,0};
  CHECK(pthread_create(&thread,NULL,diagnostic_worker,&state) == 0);
  const char repeated[] = "throw new Error('safe message')";
  for (unsigned i = 0; i < 100; ++i) {
    CHECK(qaplet_eval(runtime, repeated, sizeof(repeated)-1, &progress, &diagnostic) == QAPLET_JS_EXCEPTION);
    CHECK(diagnostic.code == QAPLET_JS_EXCEPTION && !strcmp(message,"safe message"));
  }
  CHECK(pthread_join(thread,NULL) == 0 && state.passed);
  const char traps[] = "diagTraps";
  CHECK(qaplet_eval(runtime, traps, sizeof(traps)-1, &progress, NULL) == QAPLET_OK);
  CHECK(qaplet_get_result(runtime, &result, NULL) == QAPLET_OK && result.number == 0);
  CHECK(qaplet_close(runtime, NULL) == QAPLET_OK && qaplet_destroy(runtime, NULL) == QAPLET_OK);
  puts("diagnostics: safe text; getter/Proxy/toString untouched; bounded independent caller buffers");
  return 0;
}

static int copied_value_limits(void) {
  qaplet_config config = QAPLET_CONFIG_INIT;
  qaplet_runtime *runtime = NULL;
  qaplet_progress progress = QAPLET_PROGRESS_INIT;
  qaplet_result result = QAPLET_RESULT_INIT;
  qaplet_snapshot snapshot = QAPLET_SNAPSHOT_INIT;
  qaplet_abi abi = QAPLET_ABI_INIT;
  CHECK(qaplet_query_abi(&abi) == QAPLET_OK && (abi.capabilities & QAPLET_CAP_COPIED_VALUES));
  CHECK(qaplet_create(&config,&runtime,NULL) == QAPLET_OK);
  CHECK(qaplet_close(runtime,NULL) == QAPLET_OK);
  CHECK(qaplet_get_snapshot(runtime,&snapshot,NULL) == QAPLET_OK);
  uint64_t closed_base = snapshot.native_current_bytes;
  CHECK(qaplet_destroy(runtime,NULL) == QAPLET_OK);
  CHECK(qaplet_create(&config,&runtime,NULL) == QAPLET_OK);
  CHECK(qaplet_get_snapshot(runtime,&snapshot,NULL) == QAPLET_OK);
  uint64_t base = snapshot.native_current_bytes;
  unsigned char *output = malloc(QAPLET_VALUE_BYTES_MAX);
  CHECK(output);
  result.buffer=output; result.capacity=QAPLET_VALUE_BYTES_MAX;
  const char maximum_string[] = "'x'.repeat(1048576)";
  CHECK(qaplet_eval(runtime,maximum_string,sizeof(maximum_string)-1,&progress,NULL) == QAPLET_OK);
  CHECK(qaplet_get_result(runtime,&result,NULL) == QAPLET_OK && result.type == QAPLET_VALUE_STRING && result.length == QAPLET_VALUE_BYTES_MAX);
  CHECK(output[0] == 'x' && output[QAPLET_VALUE_BYTES_MAX-1] == 'x');
  CHECK(qaplet_get_snapshot(runtime,&snapshot,NULL) == QAPLET_OK && snapshot.native_current_bytes == base + QAPLET_VALUE_BYTES_MAX);
  const char maximum_bytes[] = "new Uint8Array(1048576).fill(255)";
  CHECK(qaplet_eval(runtime,maximum_bytes,sizeof(maximum_bytes)-1,&progress,NULL) == QAPLET_OK);
  CHECK(output[0] == 'x' && output[QAPLET_VALUE_BYTES_MAX-1] == 'x');
  CHECK(qaplet_get_result(runtime,&result,NULL) == QAPLET_OK && result.type == QAPLET_VALUE_BYTES && result.length == QAPLET_VALUE_BYTES_MAX);
  CHECK(output[0] == 255 && output[QAPLET_VALUE_BYTES_MAX-1] == 255);
  result.buffer=NULL; result.capacity=1;
  CHECK(qaplet_get_result(runtime,&result,NULL) == QAPLET_INVALID_ARGUMENT);
  result.buffer=output; result.capacity=QAPLET_VALUE_BYTES_MAX;
  CHECK(qaplet_get_result(runtime,&result,NULL) == QAPLET_OK);
  CHECK(qaplet_close(runtime,NULL) == QAPLET_OK);
  CHECK(qaplet_get_snapshot(runtime,&snapshot,NULL) == QAPLET_OK && snapshot.native_current_bytes == closed_base && snapshot.engine_current_bytes == 0);
  CHECK(output[0] == 255 && output[QAPLET_VALUE_BYTES_MAX-1] == 255);
  CHECK(qaplet_get_result(runtime,&result,NULL) == QAPLET_BAD_STATE);
  CHECK(qaplet_destroy(runtime,NULL) == QAPLET_OK);
  free(output);
  config.limits.native_storage_bytes=base + 512;
  CHECK(qaplet_create(&config,&runtime,NULL) == QAPLET_OK);
  const char storage_pressure[] = "'x'.repeat(1024)";
  CHECK(qaplet_eval(runtime,storage_pressure,sizeof(storage_pressure)-1,&progress,NULL) == QAPLET_MEMORY_LIMIT);
  CHECK(qaplet_get_snapshot(runtime,&snapshot,NULL) == QAPLET_OK && snapshot.lifecycle == QAPLET_FAILED && snapshot.termination_reason == QAPLET_MEMORY_LIMIT);
  CHECK(qaplet_eval(runtime,"1",1,&progress,NULL) == QAPLET_BAD_STATE);
  CHECK(qaplet_close(runtime,NULL) == QAPLET_OK && qaplet_destroy(runtime,NULL) == QAPLET_OK);
  puts("value limits: exact 1 MiB strings/bytes; independent copies; metered permanent allocation failure");
  return 0;
}

static int promise_values(void) {
  qaplet_config config = QAPLET_CONFIG_INIT;
  qaplet_runtime *runtime = NULL;
  qaplet_progress progress = QAPLET_PROGRESS_INIT;
  qaplet_result result = QAPLET_RESULT_INIT;
  qaplet_diagnostic diagnostic = QAPLET_DIAGNOSTIC_INIT;
  char message[64];
  unsigned char output[8];
  diagnostic.message=message; diagnostic.capacity=sizeof(message);
  result.buffer=output; result.capacity=sizeof(output);
  CHECK(qaplet_create(&config,&runtime,NULL) == QAPLET_OK);
  const char integer[] = "Promise.resolve(9007199254740993n)";
  CHECK(qaplet_eval(runtime,integer,sizeof(integer)-1,&progress,NULL) == QAPLET_OK && progress.phase == QAPLET_PHASE_DONE);
  CHECK(qaplet_get_result(runtime,&result,NULL) == QAPLET_OK && result.type == QAPLET_VALUE_INT64 && result.signed_integer == INT64_C(9007199254740993));
  const char string[] = "Promise.resolve().then(()=>'A\\0\\u00e9')";
  CHECK(qaplet_eval(runtime,string,sizeof(string)-1,&progress,NULL) == QAPLET_OK && progress.phase == QAPLET_PHASE_PENDING);
  CHECK(qaplet_pump(runtime,&progress,NULL) == QAPLET_OK && progress.phase == QAPLET_PHASE_DONE);
  result.capacity=0;
  CHECK(qaplet_get_result(runtime,&result,NULL) == QAPLET_BUFFER_TOO_SMALL && result.length == 4);
  result.capacity=sizeof(output);
  CHECK(qaplet_get_result(runtime,&result,NULL) == QAPLET_OK && result.type == QAPLET_VALUE_STRING && !memcmp(output,"A\0\xc3\xa9",4));
  const char bytes[] = "Promise.resolve().then(()=>new Uint8Array([9,8]))";
  CHECK(qaplet_eval(runtime,bytes,sizeof(bytes)-1,&progress,NULL) == QAPLET_OK && progress.phase == QAPLET_PHASE_PENDING);
  CHECK(qaplet_pump(runtime,&progress,NULL) == QAPLET_OK && progress.phase == QAPLET_PHASE_DONE);
  CHECK(qaplet_get_result(runtime,&result,NULL) == QAPLET_OK && result.type == QAPLET_VALUE_BYTES && result.length == 2 && output[0] == 9 && output[1] == 8);
  const char entry[] = "Promise.resolve().then(()=>{throw new Error('entry safe')})";
  CHECK(qaplet_eval(runtime,entry,sizeof(entry)-1,&progress,&diagnostic) == QAPLET_OK);
  CHECK(qaplet_pump(runtime,&progress,&diagnostic) == QAPLET_JS_EXCEPTION && !strcmp(message,"entry safe"));
  CHECK(qaplet_get_result(runtime,&result,&diagnostic) == QAPLET_JS_EXCEPTION && !strcmp(message,"entry safe"));
  const char orphan[] = "Promise.reject('orphan safe'); 0";
  CHECK(qaplet_eval(runtime,orphan,sizeof(orphan)-1,&progress,&diagnostic) == QAPLET_JS_EXCEPTION && !strcmp(message,"orphan safe"));
  const char unsafe[] = "globalThis.promiseTraps=0; Promise.resolve().then(()=>{throw new Proxy(new Error('unsafe'),{get(){promiseTraps++;throw 1},getOwnPropertyDescriptor(){promiseTraps++;throw 2}})})";
  CHECK(qaplet_eval(runtime,unsafe,sizeof(unsafe)-1,&progress,&diagnostic) == QAPLET_OK);
  CHECK(qaplet_pump(runtime,&progress,&diagnostic) == QAPLET_JS_EXCEPTION && !strcmp(message,"JS_EXCEPTION"));
  const char first[] = "Promise.resolve().then(()=>{throw new Error('later failure')}); throw {get message(){promiseTraps++;throw 1}}";
  CHECK(qaplet_eval(runtime,first,sizeof(first)-1,&progress,&diagnostic) == QAPLET_OK && progress.phase == QAPLET_PHASE_PENDING);
  CHECK(qaplet_pump(runtime,&progress,&diagnostic) == QAPLET_JS_EXCEPTION && !strcmp(message,"JS_EXCEPTION"));
  const char handled[] = "var safeHeld=Promise.reject(new Error('handled')); Promise.resolve().then(()=>safeHeld.catch(()=>{})); Promise.resolve().then(()=>promiseTraps)";
  CHECK(qaplet_eval(runtime,handled,sizeof(handled)-1,&progress,&diagnostic) == QAPLET_OK);
  CHECK(qaplet_pump(runtime,&progress,&diagnostic) == QAPLET_OK && !strcmp(message,"OK"));
  CHECK(qaplet_get_result(runtime,&result,&diagnostic) == QAPLET_OK && result.number == 0 && !strcmp(message,"OK"));
  CHECK(qaplet_close(runtime,NULL) == QAPLET_OK && qaplet_destroy(runtime,NULL) == QAPLET_OK);
  puts("Promise values: exact/copied results; safe rejection diagnostics; first-error and handled checkpoints");
  return 0;
}
