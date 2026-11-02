/* SPDX-License-Identifier: MIT */
/* This Host deliberately includes only the distributed public SDK header. */
#include "qaplet.h"
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

int sdk_promises(void);
int sdk_modules(const char *, const char *);
int sdk_budgets(void);
int sdk_events(const char *);
int sdk_events_entry(const char *);
int sdk_stop(const char *);
int sdk_host_modules(const char *);
int sdk_resources(const char *);

#define CHECK(condition) do { if (!(condition)) { \
  fprintf(stderr, "public SDK contract failed at line %d: %s\n", __LINE__, #condition); return 1; \
} } while (0)

#include "host-values.c"

static int scalar(qaplet_runtime *runtime, const char *source, double expected) {
  qaplet_progress progress = QAPLET_PROGRESS_INIT;
  qaplet_result result = QAPLET_RESULT_INIT;
  CHECK(qaplet_eval(runtime, source, strlen(source), &progress, NULL) == QAPLET_OK);
  CHECK(progress.phase == QAPLET_PHASE_DONE && progress.progressed && progress.code == QAPLET_OK);
  CHECK(qaplet_get_result(runtime, &result, NULL) == QAPLET_OK);
  CHECK(result.type == QAPLET_VALUE_DOUBLE && result.number == expected);
  return 0;
}

static int release(qaplet_runtime *runtime) {
  CHECK(qaplet_close(runtime, NULL) == QAPLET_OK);
  CHECK(qaplet_destroy(runtime, NULL) == QAPLET_OK);
  return 0;
}

static int state(void) {
  qaplet_config config = QAPLET_CONFIG_INIT;
  qaplet_runtime *runtime = NULL;
  qaplet_progress progress = QAPLET_PROGRESS_INIT;
  qaplet_result result = QAPLET_RESULT_INIT;
  qaplet_snapshot snapshot = QAPLET_SNAPSHOT_INIT;
  CHECK(qaplet_create(&config, &runtime, NULL) == QAPLET_OK);
  CHECK(!scalar(runtime, "globalThis.counter = 40; counter", 40));
  CHECK(!scalar(runtime, "counter += 2", 42));
  const char failure[] = "counter = 43; throw new Error('expected')";
  CHECK(qaplet_eval(runtime, failure, sizeof(failure) - 1, &progress, NULL) == QAPLET_JS_EXCEPTION);
  CHECK(qaplet_get_result(runtime, &result, NULL) == QAPLET_JS_EXCEPTION && result.code == QAPLET_JS_EXCEPTION);
  CHECK(qaplet_get_snapshot(runtime, &snapshot, NULL) == QAPLET_OK);
  CHECK(snapshot.lifecycle == QAPLET_OPEN && snapshot.phase == QAPLET_PHASE_DONE);
  CHECK(!scalar(runtime, "counter", 43));
  CHECK(!release(runtime));
  CHECK(qaplet_create(&config, &runtime, NULL) == QAPLET_OK);
  const char fresh[] = "typeof counter === 'undefined'";
  CHECK(qaplet_eval(runtime, fresh, sizeof(fresh) - 1, &progress, NULL) == QAPLET_OK);
  CHECK(qaplet_get_result(runtime, &result, NULL) == QAPLET_OK);
  CHECK(result.type == QAPLET_VALUE_BOOL && result.boolean == 1);
  CHECK(!release(runtime));
  puts("state: 40 -> 42 -> exception -> 43; fresh: true");
  return 0;
}

static int lifetime(void) {
  qaplet_config config = QAPLET_CONFIG_INIT;
  qaplet_runtime *runtime = NULL;
  qaplet_snapshot snapshot = QAPLET_SNAPSHOT_INIT;
  qaplet_progress progress = QAPLET_PROGRESS_INIT;
  qaplet_result result = QAPLET_RESULT_INIT;
  CHECK(qaplet_create(&config, &runtime, NULL) == QAPLET_OK);
  CHECK(qaplet_get_snapshot(runtime, &snapshot, NULL) == QAPLET_OK);
  CHECK(snapshot.lifecycle == QAPLET_OPEN && snapshot.phase == QAPLET_PHASE_IDLE);
  CHECK(snapshot.limits.engine_heap_bytes == 67108864 && snapshot.limits.engine_stack_bytes == 1048576);
  CHECK(snapshot.limits.native_storage_bytes == 16777216 && snapshot.limits.source_bytes == 4194304);
  CHECK(snapshot.limits.module_count == 256 && snapshot.limits.source_cache_bytes == 33554432);
  CHECK(snapshot.engine_current_bytes > 0 && snapshot.engine_current_bytes <= snapshot.engine_peak_bytes);
  CHECK(snapshot.native_current_bytes > 0 && snapshot.native_current_bytes <= snapshot.native_peak_bytes);
  CHECK(qaplet_destroy(runtime, NULL) == QAPLET_BAD_STATE);
  CHECK(!scalar(runtime, "42", 42));
  CHECK(qaplet_close(runtime, NULL) == QAPLET_OK);
  CHECK(qaplet_close(runtime, NULL) == QAPLET_OK);
  CHECK(qaplet_get_snapshot(runtime, &snapshot, NULL) == QAPLET_OK);
  CHECK(snapshot.lifecycle == QAPLET_CLOSED && snapshot.engine_current_bytes == 0);
  CHECK(qaplet_get_result(runtime, &result, NULL) == QAPLET_BAD_STATE);
  CHECK(qaplet_eval(runtime, "1", 1, &progress, NULL) == QAPLET_BAD_STATE);
  CHECK(qaplet_destroy(runtime, NULL) == QAPLET_OK);
  for (unsigned i = 0; i < 1000; ++i) {
    CHECK(qaplet_create(&config, &runtime, NULL) == QAPLET_OK);
    CHECK(!release(runtime));
  }
  puts("lifetime: closed twice; destroy ordering: pass; cycles: 1000");
  return 0;
}

typedef struct { qaplet_runtime *runtime; int passed; } worker_state;
static void *worker(void *opaque) {
  worker_state *state = opaque;
  qaplet_progress progress = QAPLET_PROGRESS_INIT;
  qaplet_result result = QAPLET_RESULT_INIT;
  qaplet_snapshot snapshot = QAPLET_SNAPSHOT_INIT;
  qaplet_abi abi = QAPLET_ABI_INIT;
  const char source[] = "globalThis.foreign = 1";
  state->passed = qaplet_eval(state->runtime, source, sizeof(source) - 1, &progress, NULL) == QAPLET_WRONG_THREAD &&
      qaplet_get_result(state->runtime, &result, NULL) == QAPLET_WRONG_THREAD &&
      qaplet_close(state->runtime, NULL) == QAPLET_WRONG_THREAD &&
      qaplet_destroy(state->runtime, NULL) == QAPLET_WRONG_THREAD &&
      qaplet_get_snapshot(state->runtime, &snapshot, NULL) == QAPLET_OK && snapshot.lifecycle == QAPLET_OPEN &&
      qaplet_query_abi(&abi) == QAPLET_OK;
  return NULL;
}

static int owner(void) {
  qaplet_config config = QAPLET_CONFIG_INIT;
  qaplet_runtime *runtime = NULL;
  pthread_t thread;
  CHECK(qaplet_create(&config, &runtime, NULL) == QAPLET_OK);
  worker_state state = {runtime, 0};
  CHECK(pthread_create(&thread, NULL, worker, &state) == 0);
  CHECK(pthread_join(thread, NULL) == 0 && state.passed);
  CHECK(!scalar(runtime, "typeof foreign === 'undefined' ? 42 : 0", 42));
  CHECK(!release(runtime));
  puts("owner: foreign operations rejected; snapshot allowed; JS unchanged");
  return 0;
}

static int creation(void) {
  for (unsigned budget = 1024; budget <= 131072; budget += 1024) {
    qaplet_config config = QAPLET_CONFIG_INIT;
    qaplet_runtime *runtime = NULL;
    config.limits.engine_heap_bytes = budget;
    qaplet_code code = qaplet_create(&config, &runtime, NULL);
    if (code == QAPLET_OK) {
      qaplet_snapshot snapshot = QAPLET_SNAPSHOT_INIT;
      CHECK(runtime && qaplet_get_snapshot(runtime, &snapshot, NULL) == QAPLET_OK);
      CHECK(snapshot.lifecycle == QAPLET_OPEN && snapshot.termination_reason == QAPLET_OK);
      CHECK(!release(runtime));
    } else {
      CHECK(code == QAPLET_MEMORY_LIMIT && runtime == NULL);
      qaplet_config recovery = QAPLET_CONFIG_INIT;
      qaplet_snapshot snapshot = QAPLET_SNAPSHOT_INIT;
      CHECK(qaplet_create(&recovery, &runtime, NULL) == QAPLET_OK);
      CHECK(!scalar(runtime, "6 * 7", 42));
      CHECK(qaplet_close(runtime, NULL) == QAPLET_OK);
      CHECK(qaplet_get_snapshot(runtime, &snapshot, NULL) == QAPLET_OK);
      CHECK(snapshot.lifecycle == QAPLET_CLOSED && snapshot.engine_current_bytes == 0);
      CHECK(qaplet_destroy(runtime, NULL) == QAPLET_OK);
    }
  }
  qaplet_config config = QAPLET_CONFIG_INIT;
  qaplet_runtime *runtime = NULL;
  config.limits.native_storage_bytes = 1;
  CHECK(qaplet_create(&config, &runtime, NULL) == QAPLET_MEMORY_LIMIT && runtime == NULL);
  config.limits.native_storage_bytes = 0;
  CHECK(qaplet_create(&config, &runtime, NULL) == QAPLET_OK);
  CHECK(!scalar(runtime, "6 * 7", 42));
  CHECK(!release(runtime));
  puts("creation: 128 bounded heap configurations; null failures; repeated recovery: pass");
  return 0;
}

static int resources(void) {
  qaplet_config config = QAPLET_CONFIG_INIT;
  qaplet_runtime *runtime = NULL;
  qaplet_progress progress = QAPLET_PROGRESS_INIT;
  qaplet_result result = QAPLET_RESULT_INIT;
  qaplet_snapshot snapshot = QAPLET_SNAPSHOT_INIT;
  config.limits.source_bytes = 16;
  CHECK(qaplet_create(&config, &runtime, NULL) == QAPLET_OK);
  CHECK(!scalar(runtime, "40+2", 42));
  CHECK(qaplet_eval(runtime, "12345678901234567", 17, &progress, NULL) == QAPLET_SOURCE_TOO_LARGE);
  CHECK(qaplet_get_result(runtime, &result, NULL) == QAPLET_OK && result.number == 42);
  CHECK(!scalar(runtime, "41+1", 42));
  CHECK(!release(runtime));
  config.limits.source_bytes = 0;
  config.limits.engine_heap_bytes = 1048576;
  CHECK(qaplet_create(&config, &runtime, NULL) == QAPLET_OK);
  const char pressure[] = "globalThis.a=[]; for(let i=0;i<100000;i++) a.push('x'.repeat(128)); 0";
  CHECK(qaplet_eval(runtime, pressure, sizeof(pressure) - 1, &progress, NULL) == QAPLET_MEMORY_LIMIT);
  CHECK(qaplet_get_snapshot(runtime, &snapshot, NULL) == QAPLET_OK);
  CHECK(snapshot.lifecycle == QAPLET_FAILED && snapshot.termination_reason == QAPLET_MEMORY_LIMIT);
  CHECK(snapshot.engine_peak_bytes <= 1048576);
  CHECK(qaplet_eval(runtime, "42", 2, &progress, NULL) == QAPLET_BAD_STATE);
  CHECK(qaplet_close(runtime, NULL) == QAPLET_OK);
  CHECK(qaplet_get_snapshot(runtime, &snapshot, NULL) == QAPLET_OK);
  CHECK(snapshot.lifecycle == QAPLET_CLOSED && snapshot.engine_current_bytes == 0 &&
        snapshot.termination_reason == QAPLET_MEMORY_LIMIT);
  CHECK(qaplet_destroy(runtime, NULL) == QAPLET_OK);
  config.limits.engine_heap_bytes = 0;
  CHECK(qaplet_create(&config, &runtime, NULL) == QAPLET_OK);
  const char recursion[] = "(function recurse(){ return recurse(); })()";
  CHECK(qaplet_eval(runtime, recursion, sizeof(recursion) - 1, &progress, NULL) == QAPLET_JS_EXCEPTION);
  CHECK(!scalar(runtime, "42", 42));
  CHECK(!release(runtime));
  puts("resources: source precheck; heap failure permanent; stack exception reusable");
  return 0;
}

static int abi_contract(void) {
  qaplet_config config = QAPLET_CONFIG_INIT;
  qaplet_runtime *runtime = NULL;
  qaplet_abi abi = QAPLET_ABI_INIT;
  uint64_t required = QAPLET_CAP_LIFECYCLE | QAPLET_CAP_SYNC_SCRIPT | QAPLET_CAP_SCALAR_RESULT |
      QAPLET_CAP_OWNER_THREAD | QAPLET_CAP_POD_SNAPSHOT | QAPLET_CAP_RESOURCE_BASE;
  CHECK(qaplet_query_abi(&abi) == QAPLET_OK && abi.abi_version == 1 && (abi.capabilities & required) == required);
  config.abi_version = 2;
  CHECK(qaplet_create(&config, &runtime, NULL) == QAPLET_ABI_MISMATCH && !runtime);
  config.abi_version = 1;
  config.flags = 1;
  CHECK(qaplet_create(&config, &runtime, NULL) == QAPLET_INVALID_ARGUMENT && !runtime);
  config.flags = 0;
  config.reserved[0] = 1;
  CHECK(qaplet_create(&config, &runtime, NULL) == QAPLET_INVALID_ARGUMENT && !runtime);
  struct { qaplet_config config; uint64_t tail; } extended = {QAPLET_CONFIG_INIT, 0};
  extended.config.struct_size = sizeof(extended);
  CHECK(qaplet_create(&extended.config, &runtime, NULL) == QAPLET_OK);
  CHECK(!release(runtime));
  runtime = NULL;
  extended.tail = 1;
  CHECK(qaplet_create(&extended.config, &runtime, NULL) == QAPLET_ABI_MISMATCH && !runtime);
  config = (qaplet_config)QAPLET_CONFIG_INIT;
  CHECK(qaplet_create(&config, &runtime, NULL) == QAPLET_OK);
  char message[4] = {1, 1, 1, 1};
  qaplet_diagnostic diagnostic = QAPLET_DIAGNOSTIC_INIT;
  diagnostic.message = message;
  diagnostic.capacity = sizeof(message);
  qaplet_progress progress = QAPLET_PROGRESS_INIT;
  CHECK(qaplet_eval(runtime, "(", 1, &progress, &diagnostic) == QAPLET_JS_EXCEPTION);
  CHECK(diagnostic.code == QAPLET_JS_EXCEPTION && diagnostic.length > 3 &&
        diagnostic.flags == QAPLET_DIAGNOSTIC_TRUNCATED && message[3] == 0);
  qaplet_result result = QAPLET_RESULT_INIT;
  result.struct_size = 8;
  CHECK(qaplet_get_result(runtime, &result, NULL) == QAPLET_ABI_MISMATCH);
  CHECK(!release(runtime));
  puts("ABI: version/flags/tails/short structures; bounded diagnostics: pass");
  return 0;
}

int main(int argc, char **argv) {
  if (argc == 3 && !strcmp(argv[1], "--events-entry")) return sdk_events_entry(argv[2]);
  if (argc == 3 && !strcmp(argv[1], "--host-modules")) return sdk_host_modules(argv[2]);
  if (argc == 3 && (!strcmp(argv[1], "--modules") || !strncmp(argv[1], "--module-", 9))) return sdk_modules(argv[2], argv[1]);
  if (argc == 1) {
    qaplet_config config = QAPLET_CONFIG_INIT;
    qaplet_runtime *runtime = NULL;
    CHECK(qaplet_create(&config, &runtime, NULL) == QAPLET_OK);
    CHECK(!scalar(runtime, "6 * 7", 42));
    CHECK(!release(runtime));
    puts("scalar: 42; closed: true");
    return 0;
  }
  if (argc != 2) return 2;
  if (!strcmp(argv[1], "--state")) return state();
  if (!strcmp(argv[1], "--lifetime")) return lifetime();
  if (!strcmp(argv[1], "--owner")) return owner();
  if (!strcmp(argv[1], "--creation")) return creation();
  if (!strcmp(argv[1], "--resources")) return resources();
  if (!strncmp(argv[1], "--resource-", 11)) return sdk_resources(argv[1]);
  if (!strcmp(argv[1], "--abi")) return abi_contract();
  if (!strcmp(argv[1], "--integers")) return integer_values();
  if (!strcmp(argv[1], "--strings")) return string_values();
  if (!strcmp(argv[1], "--bytes")) return byte_values();
  if (!strcmp(argv[1], "--diagnostics")) return safe_diagnostics();
  if (!strcmp(argv[1], "--value-limits")) return copied_value_limits();
  if (!strcmp(argv[1], "--promise-values")) return promise_values();
  if (!strcmp(argv[1], "--promises")) return sdk_promises();
  if (!strcmp(argv[1], "--budgets")) return sdk_budgets();
  if (!strncmp(argv[1], "--stop", 6)) return sdk_stop(argv[1]);
  if (!strncmp(argv[1], "--events", 8)) return sdk_events(argv[1]);
  if (!strcmp(argv[1], "--facts")) {
#if defined(__aarch64__)
    const char *arch = "aarch64";
#else
    const char *arch = "x86_64";
#endif
    printf("{\"probe\":\"qaplet-host\",\"arch\":\"%s\",\"page_size\":%ld}\n", arch, sysconf(_SC_PAGESIZE));
    return 0;
  }
  return 2;
}
