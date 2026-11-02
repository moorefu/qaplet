/* SPDX-License-Identifier: MIT */
/* Budget behavior is observed only through the distributed public SDK. */
#include "qaplet.h"
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <time.h>

static int deadline(void);
static int dispatch(void);
static int options(void);
static int host_wait(void);
static int interruption(void);
static int defaults_and_reuse(void);

#define CHECK(condition) do { if (!(condition)) { \
  fprintf(stderr, "public budget contract failed at line %d: %s\n", __LINE__, #condition); return 1; \
} } while (0)

int sdk_budgets(void) {
  qaplet_config config = QAPLET_CONFIG_INIT;
  qaplet_runtime *runtime = NULL;
  qaplet_snapshot snapshot = QAPLET_SNAPSHOT_INIT;
  CHECK(qaplet_create(&config, &runtime, NULL) == QAPLET_OK);
  CHECK(qaplet_get_snapshot(runtime, &snapshot, NULL) == QAPLET_OK);
  CHECK(snapshot.limits.execution_timeout_ms == 30000);
  CHECK(snapshot.limits.execution_dispatches == 100000);
  CHECK(qaplet_close(runtime, NULL) == QAPLET_OK);
  CHECK(qaplet_destroy(runtime, NULL) == QAPLET_OK);
  CHECK(!deadline());
  CHECK(!dispatch());
  CHECK(!options());
  CHECK(!host_wait());
  CHECK(!interruption());
  CHECK(!defaults_and_reuse());
  puts("budgets: defaults; deadlines/host wait; cumulative jobs; tightening; first cause; close: pass");
  return 0;
}

static int failed(qaplet_runtime *runtime, qaplet_code reason) {
  qaplet_snapshot snapshot = QAPLET_SNAPSHOT_INIT;
  qaplet_progress progress = QAPLET_PROGRESS_INIT;
  qaplet_result result = QAPLET_RESULT_INIT;
  qaplet_diagnostic diagnostic = QAPLET_DIAGNOSTIC_INIT;
  char message[64];
  diagnostic.message = message;
  diagnostic.capacity = sizeof(message);
  CHECK(qaplet_get_snapshot(runtime, &snapshot, NULL) == QAPLET_OK);
  CHECK(snapshot.lifecycle == QAPLET_FAILED && snapshot.phase == QAPLET_PHASE_DONE);
  CHECK(snapshot.termination_reason == reason);
  CHECK(qaplet_get_result(runtime, &result, &diagnostic) == reason && result.code == reason && diagnostic.code == reason);
  CHECK(!strcmp(message, reason == QAPLET_DEADLINE_EXCEEDED ? "DEADLINE_EXCEEDED" :
                reason == QAPLET_DISPATCH_LIMIT ? "DISPATCH_LIMIT" : "MEMORY_LIMIT"));
  CHECK(qaplet_eval(runtime, "42", 2, &progress, NULL) == QAPLET_BAD_STATE);
  CHECK(qaplet_pump(runtime, &progress, NULL) == QAPLET_BAD_STATE);
  CHECK(qaplet_close(runtime, NULL) == QAPLET_OK);
  CHECK(qaplet_close(runtime, NULL) == QAPLET_OK);
  CHECK(qaplet_get_snapshot(runtime, &snapshot, NULL) == QAPLET_OK);
  CHECK(snapshot.lifecycle == QAPLET_CLOSED && snapshot.termination_reason == reason &&
        snapshot.engine_current_bytes == 0);
  CHECK(qaplet_destroy(runtime, NULL) == QAPLET_OK);
  return 0;
}

static int deadline(void) {
  qaplet_config config = QAPLET_CONFIG_INIT;
  config.limits.execution_timeout_ms = 10;
  qaplet_runtime *runtime = NULL;
  qaplet_progress progress = QAPLET_PROGRESS_INIT;
  CHECK(qaplet_create(&config, &runtime, NULL) == QAPLET_OK);
  /* Finite even without the product interrupt, so red is a returned mismatch. */
  const char source[] = "var end=Date.now()+100; while(Date.now()<end){}; 42";
  CHECK(qaplet_eval(runtime, source, sizeof(source) - 1, &progress, NULL) == QAPLET_DEADLINE_EXCEEDED);
  CHECK(!failed(runtime, QAPLET_DEADLINE_EXCEEDED));
  return 0;
}

static int dispatch(void) {
  qaplet_config config = QAPLET_CONFIG_INIT;
  config.limits.execution_dispatches = 300;
  qaplet_runtime *runtime = NULL;
  qaplet_progress progress = QAPLET_PROGRESS_INIT;
  CHECK(qaplet_create(&config, &runtime, NULL) == QAPLET_OK);
  const char source[] = "var chain=Promise.resolve(0); for(let i=0;i<700;++i)chain=chain.then(n=>n+1); chain";
  CHECK(qaplet_eval(runtime, source, sizeof(source) - 1, &progress, NULL) == QAPLET_OK &&
        progress.phase == QAPLET_PHASE_PENDING);
  CHECK(qaplet_pump(runtime, &progress, NULL) == QAPLET_OK &&
        progress.phase == QAPLET_PHASE_PENDING && progress.progressed);
  qaplet_snapshot snapshot = QAPLET_SNAPSHOT_INIT;
  CHECK(qaplet_get_snapshot(runtime, &snapshot, NULL) == QAPLET_OK && snapshot.execution_dispatches == 256);
  CHECK(qaplet_pump(runtime, &progress, NULL) == QAPLET_DISPATCH_LIMIT);
  CHECK(qaplet_get_snapshot(runtime, &snapshot, NULL) == QAPLET_OK && snapshot.execution_dispatches == 300);
  CHECK(!failed(runtime, QAPLET_DISPATCH_LIMIT));
  return 0;
}

static int options(void) {
  qaplet_config config = QAPLET_CONFIG_INIT;
  config.limits.execution_timeout_ms = 1000;
  config.limits.execution_dispatches = 10;
  qaplet_runtime *runtime = NULL;
  qaplet_progress progress = QAPLET_PROGRESS_INIT;
  qaplet_result result = QAPLET_RESULT_INIT;
  qaplet_execution_options options = QAPLET_EXECUTION_OPTIONS_INIT;
  CHECK(qaplet_create(&config, &runtime, NULL) == QAPLET_OK);
  CHECK(qaplet_eval(runtime, "42", 2, &progress, NULL) == QAPLET_OK);
  options.timeout_ms = 1001;
  CHECK(qaplet_eval_with_options(runtime, "0", 1, &options, &progress, NULL) == QAPLET_INVALID_ARGUMENT);
  options.timeout_ms = 0;
  options.max_dispatches = 11;
  CHECK(qaplet_eval_with_options(runtime, "0", 1, &options, &progress, NULL) == QAPLET_INVALID_ARGUMENT);
  options.max_dispatches = 0;
  options.flags = 1;
  CHECK(qaplet_eval_with_options(runtime, "0", 1, &options, &progress, NULL) == QAPLET_INVALID_ARGUMENT);
  options.flags = 0;
  options.reserved[3] = 1;
  CHECK(qaplet_eval_with_options(runtime, "0", 1, &options, &progress, NULL) == QAPLET_INVALID_ARGUMENT);
  CHECK(qaplet_get_result(runtime, &result, NULL) == QAPLET_OK && result.number == 42);
  options = (qaplet_execution_options)QAPLET_EXECUTION_OPTIONS_INIT;
  options.abi_version = 2;
  CHECK(qaplet_eval_with_options(runtime, "0", 1, &options, &progress, NULL) == QAPLET_ABI_MISMATCH);
  options.abi_version = QAPLET_ABI_VERSION;
  options.struct_size = 8;
  CHECK(qaplet_eval_with_options(runtime, "0", 1, &options, &progress, NULL) == QAPLET_ABI_MISMATCH);
  options = (qaplet_execution_options)QAPLET_EXECUTION_OPTIONS_INIT;
  options.timeout_ms = UINT64_MAX;
  CHECK(qaplet_eval_with_options(runtime, "0", 1, &options, &progress, NULL) == QAPLET_INVALID_ARGUMENT);
  struct { qaplet_execution_options options; uint64_t tail; } extended = {QAPLET_EXECUTION_OPTIONS_INIT, 0};
  extended.options.struct_size = sizeof(extended);
  CHECK(qaplet_eval_with_options(runtime, "43", 2, &extended.options, &progress, NULL) == QAPLET_OK);
  extended.tail = 1;
  CHECK(qaplet_eval_with_options(runtime, "0", 1, &extended.options, &progress, NULL) == QAPLET_ABI_MISMATCH);
  CHECK(qaplet_get_result(runtime, &result, NULL) == QAPLET_OK && result.number == 43);
  options = (qaplet_execution_options)QAPLET_EXECUTION_OPTIONS_INIT;
  options.max_dispatches = 2;
  const char source[] = "var chain=Promise.resolve(0); for(let i=0;i<3;++i)chain=chain.then(n=>n+1); chain";
  CHECK(qaplet_eval_with_options(runtime, source, sizeof(source) - 1, &options, &progress, NULL) == QAPLET_OK);
  CHECK(qaplet_pump(runtime, &progress, NULL) == QAPLET_DISPATCH_LIMIT);
  qaplet_snapshot snapshot = QAPLET_SNAPSHOT_INIT;
  CHECK(qaplet_get_snapshot(runtime, &snapshot, NULL) == QAPLET_OK && snapshot.execution_dispatches == 2);
  CHECK(!failed(runtime, QAPLET_DISPATCH_LIMIT));
  CHECK(qaplet_create(&config, &runtime, NULL) == QAPLET_OK);
  options = (qaplet_execution_options)QAPLET_EXECUTION_OPTIONS_INIT;
  options.timeout_ms = 10;
  CHECK(qaplet_eval_with_options(runtime, "for(;;){}", 9, &options, &progress, NULL) == QAPLET_DEADLINE_EXCEEDED);
  CHECK(!failed(runtime, QAPLET_DEADLINE_EXCEEDED));
  config.limits.execution_timeout_ms = UINT64_MAX;
  CHECK(qaplet_create(&config, &runtime, NULL) == QAPLET_INVALID_ARGUMENT && !runtime);
  return 0;
}

static int host_wait(void) {
  qaplet_config config = QAPLET_CONFIG_INIT;
  config.limits.execution_timeout_ms = 200;
  qaplet_runtime *runtime = NULL;
  qaplet_progress progress = QAPLET_PROGRESS_INIT;
  CHECK(qaplet_create(&config, &runtime, NULL) == QAPLET_OK);
  const char source[] = "new Promise(()=>{})";
  CHECK(qaplet_eval(runtime, source, sizeof(source) - 1, &progress, NULL) == QAPLET_OK &&
        progress.phase == QAPLET_PHASE_PENDING);
  CHECK(qaplet_pump(runtime, &progress, NULL) == QAPLET_OK &&
        progress.phase == QAPLET_PHASE_PENDING && !progress.progressed);
  /* Owner has explicitly observed PENDING. Wait at least 250ms with the OS
   * monotonic clock; this handoff deliberately exceeds the same deadline. */
  struct timespec before, now;
  CHECK(clock_gettime(CLOCK_MONOTONIC, &before) == 0);
  do {
    struct timespec pause = {0, 10000000};
    CHECK(nanosleep(&pause, NULL) == 0 || errno == EINTR);
    CHECK(clock_gettime(CLOCK_MONOTONIC, &now) == 0);
  } while ((now.tv_sec - before.tv_sec) * 1000000000LL + now.tv_nsec - before.tv_nsec < 250000000LL);
  CHECK(qaplet_pump(runtime, &progress, NULL) == QAPLET_DEADLINE_EXCEEDED &&
        progress.phase == QAPLET_PHASE_DONE && !progress.progressed);
  CHECK(!failed(runtime, QAPLET_DEADLINE_EXCEEDED));
  return 0;
}

static int interruption(void) {
  qaplet_config config = QAPLET_CONFIG_INIT;
  config.limits.execution_timeout_ms = 10;
  qaplet_runtime *runtime = NULL;
  qaplet_progress progress = QAPLET_PROGRESS_INIT;
  CHECK(qaplet_create(&config, &runtime, NULL) == QAPLET_OK);
  /* There is no self-terminating branch. Supervisor expiry is a failed test. */
  CHECK(qaplet_eval(runtime, "for(;;){}", 9, &progress, NULL) == QAPLET_DEADLINE_EXCEEDED);
  CHECK(!failed(runtime, QAPLET_DEADLINE_EXCEEDED));
  CHECK(qaplet_create(&config, &runtime, NULL) == QAPLET_OK);
  const char async[] = "Promise.resolve().then(async()=>{try{for(;;){}}catch(e){return 42}}).catch(()=>42)";
  CHECK(qaplet_eval(runtime, async, sizeof(async) - 1, &progress, NULL) == QAPLET_OK);
  CHECK(qaplet_pump(runtime, &progress, NULL) == QAPLET_DEADLINE_EXCEEDED);
  CHECK(!failed(runtime, QAPLET_DEADLINE_EXCEEDED));
  config.limits.execution_timeout_ms = 2000;
  config.limits.engine_heap_bytes = 1048576;
  CHECK(qaplet_create(&config, &runtime, NULL) == QAPLET_OK);
  const char memory[] = "try{let data=[];for(;;)data.push('x'.repeat(16384))}catch(e){};for(;;){}";
  CHECK(qaplet_eval(runtime, memory, sizeof(memory) - 1, &progress, NULL) == QAPLET_MEMORY_LIMIT);
  CHECK(!failed(runtime, QAPLET_MEMORY_LIMIT));
  config.limits.engine_heap_bytes = 0;
  config.limits.execution_dispatches = 2;
  CHECK(qaplet_create(&config, &runtime, NULL) == QAPLET_OK);
  const char ordinary[] = "function spin(){Promise.resolve().then(spin)}spin();throw new Error('ordinary')";
  CHECK(qaplet_eval(runtime, ordinary, sizeof(ordinary) - 1, &progress, NULL) == QAPLET_OK);
  CHECK(qaplet_pump(runtime, &progress, NULL) == QAPLET_DISPATCH_LIMIT);
  CHECK(!failed(runtime, QAPLET_DISPATCH_LIMIT));
  return 0;
}

static int defaults_and_reuse(void) {
  qaplet_config config = QAPLET_CONFIG_INIT;
  qaplet_runtime *runtime = NULL;
  qaplet_progress progress = QAPLET_PROGRESS_INIT;
  qaplet_snapshot snapshot = QAPLET_SNAPSHOT_INIT;
  CHECK(qaplet_create(&config, &runtime, NULL) == QAPLET_OK);
  qaplet_abi abi = QAPLET_ABI_INIT;
  CHECK(qaplet_query_abi(&abi) == QAPLET_OK && (abi.capabilities & QAPLET_CAP_EXECUTION_BUDGETS));
  const char infinite[] = "function spin(){Promise.resolve().then(spin)}spin();0";
  CHECK(qaplet_eval(runtime, infinite, sizeof(infinite) - 1, &progress, NULL) == QAPLET_OK);
  unsigned pumps = 0;
  qaplet_code code;
  do {
    CHECK(pumps++ < 400);
    code = qaplet_pump(runtime, &progress, NULL);
  } while (progress.phase == QAPLET_PHASE_PENDING);
  CHECK(code == QAPLET_DISPATCH_LIMIT);
  CHECK(qaplet_get_snapshot(runtime, &snapshot, NULL) == QAPLET_OK && snapshot.execution_dispatches == 100000);
  CHECK(!failed(runtime, QAPLET_DISPATCH_LIMIT));
  config.limits.execution_dispatches = 3;
  CHECK(qaplet_create(&config, &runtime, NULL) == QAPLET_OK);
  qaplet_execution_options options = QAPLET_EXECUTION_OPTIONS_INIT;
  for (unsigned i = 0; i < 2; ++i) {
    const char source[] = "var chain=Promise.resolve(0);for(let i=0;i<3;++i)chain=chain.then(n=>n+1);chain";
    CHECK(qaplet_eval_with_options(runtime, source, sizeof(source) - 1, &options, &progress, NULL) == QAPLET_OK);
    CHECK(qaplet_pump(runtime, &progress, NULL) == QAPLET_OK && progress.phase == QAPLET_PHASE_DONE);
    CHECK(qaplet_get_snapshot(runtime, &snapshot, NULL) == QAPLET_OK && snapshot.execution_dispatches == 3);
    qaplet_result result = QAPLET_RESULT_INIT;
    CHECK(qaplet_get_result(runtime, &result, NULL) == QAPLET_OK && result.number == 3);
  }
  CHECK(qaplet_close(runtime, NULL) == QAPLET_OK);
  CHECK(qaplet_destroy(runtime, NULL) == QAPLET_OK);
  return 0;
}
