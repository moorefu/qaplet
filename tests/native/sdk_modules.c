/* SPDX-License-Identifier: MIT */
/* This consumer uses only the supplied public SDK and real source files. */
#include "qaplet.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

int sdk_module_policy(const char *mode);

#define CHECK(condition) do { if (!(condition)) { \
  fprintf(stderr, "public module contract failed at line %d: %s\n", __LINE__, #condition); return 1; \
} } while (0)

static int scalar(qaplet_runtime *runtime, const char *source, double expected) {
  qaplet_progress progress = QAPLET_PROGRESS_INIT;
  qaplet_result result = QAPLET_RESULT_INIT;
  CHECK(qaplet_eval(runtime, source, strlen(source), &progress, NULL) == QAPLET_OK);
  CHECK(progress.phase == QAPLET_PHASE_DONE);
  CHECK(qaplet_get_result(runtime, &result, NULL) == QAPLET_OK);
  CHECK(result.type == QAPLET_VALUE_DOUBLE && result.number == expected);
  return 0;
}

static int settle(qaplet_runtime *runtime, qaplet_progress *progress) {
  unsigned calls = 0;
  while (progress->phase == QAPLET_PHASE_PENDING && calls++ < 16)
    CHECK(qaplet_pump(runtime, progress, NULL) == QAPLET_OK && progress->progressed);
  CHECK(progress->phase == QAPLET_PHASE_DONE);
  return 0;
}

static int run(qaplet_runtime *runtime, const char *directory, const char *name) {
  qaplet_progress progress = QAPLET_PROGRESS_INIT;
  char path[4096];
  CHECK(snprintf(path, sizeof(path), "%s/%s", directory, name) < sizeof(path));
  CHECK(qaplet_run_entry(runtime, path, strlen(path), &progress, NULL) == QAPLET_OK);
  CHECK(!settle(runtime, &progress));
  qaplet_result result = QAPLET_RESULT_INIT;
  CHECK(qaplet_get_result(runtime, &result, NULL) == QAPLET_OK && result.type == QAPLET_VALUE_UNDEFINED);
  return 0;
}

static int release(qaplet_runtime *runtime) {
  CHECK(qaplet_close(runtime, NULL) == QAPLET_OK);
  qaplet_snapshot snapshot = QAPLET_SNAPSHOT_INIT;
  CHECK(qaplet_get_snapshot(runtime, &snapshot, NULL) == QAPLET_OK && !snapshot.engine_current_bytes &&
        !snapshot.module_count && !snapshot.source_cache_bytes);
  CHECK(qaplet_destroy(runtime, NULL) == QAPLET_OK);
  return 0;
}

static int option_refusal(qaplet_runtime *runtime, const char *path,
                          const qaplet_execution_options *options, qaplet_code expected) {
  qaplet_progress progress = QAPLET_PROGRESS_INIT;
  const char base[] = "file:///qaplet-options.js";
  CHECK(qaplet_run_entry_with_options(runtime, path, strlen(path), options, &progress, NULL) == expected);
  CHECK(qaplet_eval_with_base_and_options(runtime, "globalThis.refused=1", 20, base, sizeof(base)-1,
        options, &progress, NULL) == expected);
  return 0;
}

static int option_inputs(const char *directory) {
  qaplet_config config = QAPLET_CONFIG_INIT;
  config.limits.execution_timeout_ms = config.limits.execution_dispatches = 1000;
  qaplet_runtime *runtime = NULL;
  qaplet_progress progress = QAPLET_PROGRESS_INIT;
  qaplet_result result = QAPLET_RESULT_INIT;
  qaplet_snapshot before = QAPLET_SNAPSHOT_INIT, after = QAPLET_SNAPSHOT_INIT;
  char path[4096];
  CHECK(snprintf(path, sizeof(path), "%s/entry.mjs", directory) < sizeof(path));
  CHECK(qaplet_create(&config, &runtime, NULL) == QAPLET_OK);
  CHECK(!scalar(runtime, "42", 42));
  CHECK(qaplet_get_snapshot(runtime, &before, NULL) == QAPLET_OK);
  for (unsigned variant = 0; variant < 7; ++variant) {
    qaplet_execution_options options = QAPLET_EXECUTION_OPTIONS_INIT;
    if (variant == 0) options.max_dispatches = 1001;
    if (variant == 1) options.timeout_ms = 1001;
    if (variant == 2) options.flags = 1;
    if (variant == 3) options.reserved[3] = 1;
    if (variant == 4) options.timeout_ms = UINT64_MAX;
    if (variant == 5) options.abi_version = 2;
    if (variant == 6) options.struct_size = 8;
    CHECK(!option_refusal(runtime, path, &options, variant < 5 ? QAPLET_INVALID_ARGUMENT : QAPLET_ABI_MISMATCH));
    CHECK(qaplet_get_result(runtime, &result, NULL) == QAPLET_OK && result.number == 42);
    CHECK(qaplet_get_snapshot(runtime, &after, NULL) == QAPLET_OK && after.lifecycle == QAPLET_OPEN &&
          after.module_count == before.module_count && after.source_cache_bytes == before.source_cache_bytes &&
          after.native_current_bytes == before.native_current_bytes && !after.execution_dispatches);
  }
  struct { qaplet_execution_options options; uint64_t tail; } extended = {QAPLET_EXECUTION_OPTIONS_INIT, 1};
  extended.options.struct_size = sizeof(extended);
  CHECK(!option_refusal(runtime, path, &extended.options, QAPLET_ABI_MISMATCH));
  CHECK(!scalar(runtime, "typeof refused==='undefined' && typeof entryRuns==='undefined' ? 42 : 0", 42));
  extended.tail = 0;
  CHECK(qaplet_run_entry_with_options(runtime, path, strlen(path), &extended.options, &progress, NULL) == QAPLET_OK);
  CHECK(!settle(runtime, &progress));
  CHECK(qaplet_run_entry_with_options(runtime, path, strlen(path), NULL, &progress, NULL) == QAPLET_OK && !settle(runtime, &progress));
  CHECK(!scalar(runtime, "entryRuns", 1));
  CHECK(qaplet_eval(runtime, "entryUrl", 8, &progress, NULL) == QAPLET_OK);
  char base[4096]; result = (qaplet_result)QAPLET_RESULT_INIT;
  result.buffer = base; result.capacity = sizeof(base);
  CHECK(qaplet_get_result(runtime, &result, NULL) == QAPLET_OK);
  size_t base_length = result.length;
  CHECK(qaplet_eval_with_base_and_options(runtime, "entryRuns+41", 12, base, result.length,
        &extended.options, &progress, NULL) == QAPLET_OK && progress.phase == QAPLET_PHASE_DONE);
  result = (qaplet_result)QAPLET_RESULT_INIT;
  CHECK(qaplet_get_result(runtime, &result, NULL) == QAPLET_OK && result.number == 42);
  const char pending[] = "new Promise(()=>{})";
  CHECK(qaplet_eval_with_base_and_options(runtime, pending, sizeof(pending)-1, base, base_length,
        NULL, &progress, NULL) == QAPLET_OK && progress.phase == QAPLET_PHASE_PENDING);
  CHECK(!option_refusal(runtime, path, NULL, QAPLET_BUSY));
  extended.options.flags = 1;
  CHECK(!option_refusal(runtime, path, &extended.options, QAPLET_INVALID_ARGUMENT));
  CHECK(qaplet_pump(runtime, &progress, NULL) == QAPLET_OK && progress.phase == QAPLET_PHASE_PENDING && !progress.progressed);
  CHECK(qaplet_request_stop(runtime, NULL) == QAPLET_OK);
  CHECK(!option_refusal(runtime, path, NULL, QAPLET_BAD_STATE));
  CHECK(!option_refusal(runtime, path, &extended.options, QAPLET_INVALID_ARGUMENT));
  CHECK(qaplet_close(runtime, NULL) == QAPLET_OK);
  CHECK(!option_refusal(runtime, path, &extended.options, QAPLET_INVALID_ARGUMENT));
  CHECK(qaplet_destroy(runtime, NULL) == QAPLET_OK);
  return 0;
}

int sdk_modules(const char *directory, const char *mode) {
  if (!strcmp(mode, "--module-nul") || !strcmp(mode, "--module-cache") ||
      !strcmp(mode, "--module-policy") || !strcmp(mode, "--module-aliases") ||
      !strcmp(mode, "--module-symlinks")) return sdk_module_policy(mode);
  qaplet_config config = QAPLET_CONFIG_INIT;
  qaplet_runtime *runtime = NULL;
  qaplet_progress progress = QAPLET_PROGRESS_INIT;
  qaplet_result result = QAPLET_RESULT_INIT;
  qaplet_snapshot snapshot = QAPLET_SNAPSHOT_INIT;
  qaplet_abi abi = QAPLET_ABI_INIT;
  CHECK(qaplet_query_abi(&abi) == QAPLET_OK && (abi.capabilities & QAPLET_CAP_FILE_MODULES));
  if (!strcmp(mode, "--module-budgets")) {
    CHECK(!option_inputs(directory));
    char path[4096];
    config.limits.execution_dispatches = 1000;
    qaplet_execution_options options = QAPLET_EXECUTION_OPTIONS_INIT;
    options.max_dispatches = 300;
    CHECK(qaplet_create(&config, &runtime, NULL) == QAPLET_OK);
    CHECK(snprintf(path, sizeof(path), "%s/job-budget.mjs", directory) < sizeof(path));
    CHECK(qaplet_run_entry_with_options(runtime, path, strlen(path), &options, &progress, NULL) == QAPLET_OK && progress.phase == QAPLET_PHASE_PENDING);
    CHECK(qaplet_pump(runtime, &progress, NULL) == QAPLET_OK && progress.phase == QAPLET_PHASE_PENDING);
    CHECK(qaplet_get_snapshot(runtime, &snapshot, NULL) == QAPLET_OK && snapshot.execution_dispatches == 256);
    CHECK(qaplet_pump(runtime, &progress, NULL) == QAPLET_DISPATCH_LIMIT && progress.phase == QAPLET_PHASE_DONE);
    CHECK(qaplet_get_snapshot(runtime, &snapshot, NULL) == QAPLET_OK && snapshot.execution_dispatches == 300 &&
          snapshot.lifecycle == QAPLET_FAILED && snapshot.termination_reason == QAPLET_DISPATCH_LIMIT);
    CHECK(qaplet_get_result(runtime, &result, NULL) == QAPLET_DISPATCH_LIMIT);
    CHECK(qaplet_run_entry(runtime, path, strlen(path), &progress, NULL) == QAPLET_BAD_STATE);
    CHECK(!release(runtime));
    config = (qaplet_config)QAPLET_CONFIG_INIT;
    config.limits.execution_timeout_ms = 1000;
    CHECK(qaplet_create(&config, &runtime, NULL) == QAPLET_OK);
    CHECK(snprintf(path, sizeof(path), "%s/pending.mjs", directory) < sizeof(path));
    options = (qaplet_execution_options)QAPLET_EXECUTION_OPTIONS_INIT;
    options.timeout_ms = 100;
    CHECK(qaplet_run_entry_with_options(runtime, path, strlen(path), &options, &progress, NULL) == QAPLET_OK && progress.phase == QAPLET_PHASE_PENDING);
    usleep(150000);
    CHECK(qaplet_pump(runtime, &progress, NULL) == QAPLET_DEADLINE_EXCEEDED && progress.phase == QAPLET_PHASE_DONE && !progress.progressed);
    CHECK(qaplet_get_result(runtime, &result, NULL) == QAPLET_DEADLINE_EXCEEDED);
    CHECK(!release(runtime));
    config = (qaplet_config)QAPLET_CONFIG_INIT;
    config.limits.execution_dispatches = 1000;
    CHECK(qaplet_create(&config, &runtime, NULL) == QAPLET_OK);
    CHECK(!run(runtime, directory, "entry.mjs"));
    CHECK(qaplet_eval(runtime, "entryUrl", 8, &progress, NULL) == QAPLET_OK);
    char base[4096];
    result = (qaplet_result)QAPLET_RESULT_INIT;
    result.buffer = base; result.capacity = sizeof(base);
    CHECK(qaplet_get_result(runtime, &result, NULL) == QAPLET_OK && result.type == QAPLET_VALUE_STRING);
    size_t base_length = result.length;
    const char import_jobs[] = "import('./job-budget.mjs')";
    options = (qaplet_execution_options)QAPLET_EXECUTION_OPTIONS_INIT;
    options.max_dispatches = 300;
    CHECK(qaplet_eval_with_base_and_options(runtime, import_jobs, sizeof(import_jobs)-1, base, result.length,
          &options, &progress, NULL) == QAPLET_OK && progress.phase == QAPLET_PHASE_PENDING);
    CHECK(qaplet_pump(runtime, &progress, NULL) == QAPLET_OK && progress.phase == QAPLET_PHASE_PENDING);
    CHECK(qaplet_get_snapshot(runtime, &snapshot, NULL) == QAPLET_OK && snapshot.execution_dispatches == 256);
    CHECK(qaplet_pump(runtime, &progress, NULL) == QAPLET_DISPATCH_LIMIT && progress.phase == QAPLET_PHASE_DONE);
    CHECK(qaplet_get_snapshot(runtime, &snapshot, NULL) == QAPLET_OK && snapshot.execution_dispatches == 300 &&
          snapshot.lifecycle == QAPLET_FAILED && snapshot.termination_reason == QAPLET_DISPATCH_LIMIT);
    CHECK(!release(runtime));
    config.limits.execution_timeout_ms = 1000;
    CHECK(qaplet_create(&config, &runtime, NULL) == QAPLET_OK);
    options = (qaplet_execution_options)QAPLET_EXECUTION_OPTIONS_INIT;
    options.timeout_ms = 100;
    const char pending_script[] = "new Promise(()=>{})";
    CHECK(qaplet_eval_with_base_and_options(runtime, pending_script, sizeof(pending_script)-1, base, base_length,
          &options, &progress, NULL) == QAPLET_OK && progress.phase == QAPLET_PHASE_PENDING);
    CHECK(qaplet_pump(runtime, &progress, NULL) == QAPLET_OK && !progress.progressed);
    usleep(150000);
    CHECK(qaplet_pump(runtime, &progress, NULL) == QAPLET_DEADLINE_EXCEEDED && progress.phase == QAPLET_PHASE_DONE && !progress.progressed);
    CHECK(qaplet_get_snapshot(runtime, &snapshot, NULL) == QAPLET_OK && snapshot.lifecycle == QAPLET_FAILED &&
          snapshot.termination_reason == QAPLET_DEADLINE_EXCEEDED && !snapshot.execution_dispatches);
    CHECK(!release(runtime));
    config = (qaplet_config)QAPLET_CONFIG_INIT;
    config.limits.execution_timeout_ms = 20;
    CHECK(qaplet_create(&config, &runtime, NULL) == QAPLET_OK);
    CHECK(snprintf(path, sizeof(path), "%s/loop.mjs", directory) < sizeof(path));
    CHECK(qaplet_run_entry(runtime, path, strlen(path), &progress, NULL) == QAPLET_DEADLINE_EXCEEDED && progress.phase == QAPLET_PHASE_DONE);
    CHECK(qaplet_get_snapshot(runtime, &snapshot, NULL) == QAPLET_OK && snapshot.lifecycle == QAPLET_FAILED &&
          snapshot.termination_reason == QAPLET_DEADLINE_EXCEEDED);
    CHECK(!release(runtime));
    config = (qaplet_config)QAPLET_CONFIG_INIT;
    CHECK(qaplet_create(&config, &runtime, NULL) == QAPLET_OK);
    CHECK(qaplet_get_snapshot(runtime, &snapshot, NULL) == QAPLET_OK);
    uint64_t baseline = snapshot.native_current_bytes;
    CHECK(!release(runtime));
    CHECK(snprintf(path, sizeof(path), "%s/entry.mjs", directory) < sizeof(path));
    config.limits.native_storage_bytes = baseline + strlen(path) + 1;
    CHECK(qaplet_create(&config, &runtime, NULL) == QAPLET_OK);
    CHECK(qaplet_run_entry(runtime, path, strlen(path), &progress, NULL) == QAPLET_MEMORY_LIMIT);
    CHECK(qaplet_get_snapshot(runtime, &snapshot, NULL) == QAPLET_OK && snapshot.lifecycle == QAPLET_FAILED &&
          snapshot.termination_reason == QAPLET_MEMORY_LIMIT);
    CHECK(!release(runtime));
    puts("module budgets: cumulative TLA jobs; host-wait deadline; module loop interrupted; permanent failure: pass");
    return 0;
  }
  if (!strcmp(mode, "--module-identity")) {
    char saved[4096];
    CHECK(getcwd(saved, sizeof(saved)) && !chdir(directory));
    CHECK(qaplet_create(&config, &runtime, NULL) == QAPLET_OK);
    CHECK(!chdir(saved));
    CHECK(qaplet_run_entry(runtime, "entry.mjs", 9, &progress, NULL) == QAPLET_OK && !settle(runtime, &progress));
    CHECK(!scalar(runtime, "entryMain === true && dependencyMain === false ? 42 : 0", 42));
    CHECK(!scalar(runtime, "entryUrl.startsWith('file:///') && dependencyUrl.endsWith('/dependency.js') ? 42 : 0", 42));
    CHECK(!run(runtime, directory, "./entry.mjs"));
    CHECK(!run(runtime, directory, "dependency.js"));
    CHECK(!scalar(runtime, "entryRuns === 1 && dependencyRuns === 1 && dependencyMain === false ? 42 : 0", 42));
    CHECK(qaplet_get_snapshot(runtime, &snapshot, NULL) == QAPLET_OK && snapshot.module_count == 4);
    struct stat entry_stat, dependency_stat;
    char file[4096];
    CHECK(snprintf(file, sizeof(file), "%s/entry.mjs", directory) < sizeof(file) && !stat(file, &entry_stat));
    CHECK(snprintf(file, sizeof(file), "%s/dependency.js", directory) < sizeof(file) && !stat(file, &dependency_stat));
    CHECK(snapshot.source_cache_bytes == (uint64_t)entry_stat.st_size + (uint64_t)dependency_stat.st_size);
    uint64_t bytes = snapshot.source_cache_bytes;
    CHECK(!run(runtime, directory, "entry.mjs"));
    CHECK(qaplet_get_snapshot(runtime, &snapshot, NULL) == QAPLET_OK && snapshot.source_cache_bytes == bytes && snapshot.module_count == 4);
    char base[4096];
    CHECK(qaplet_eval(runtime, "entryUrl", 8, &progress, NULL) == QAPLET_OK);
    result.buffer = base; result.capacity = sizeof(base);
    CHECK(qaplet_get_result(runtime, &result, NULL) == QAPLET_OK && result.type == QAPLET_VALUE_STRING);
    CHECK(qaplet_run_entry(runtime, base, result.length, &progress, NULL) == QAPLET_OK && !settle(runtime, &progress));
    const char dynamic[] = "import('./sub/dynamic.mjs').then(module=>module.increment)";
    CHECK(qaplet_eval_with_base(runtime, dynamic, sizeof(dynamic)-1, base, result.length, &progress, NULL) == QAPLET_OK);
    CHECK(!settle(runtime, &progress));
    CHECK(qaplet_get_result(runtime, &result, NULL) == QAPLET_OK && result.number == 2);
    CHECK(!scalar(runtime, "dynamicMain === false && dynamicRuns === 1 ? 42 : 0", 42));
    CHECK(qaplet_eval(runtime, dynamic, sizeof(dynamic)-1, &progress, NULL) == QAPLET_OK);
    CHECK(progress.phase == QAPLET_PHASE_PENDING);
    CHECK(qaplet_pump(runtime, &progress, NULL) == QAPLET_MODULE_RESOLVE && progress.phase == QAPLET_PHASE_DONE);
    CHECK(!scalar(runtime, "dynamicRuns", 1));
    CHECK(!release(runtime));
    puts("module identity: fixed cwd; first metadata; canonical reuse; explicit Script base: pass");
    return 0;
  }
  if (!strcmp(mode, "--module-limits")) {
    config.limits.source_bytes = 16;
    CHECK(qaplet_create(&config, &runtime, NULL) == QAPLET_OK);
    CHECK(!scalar(runtime, "42", 42));
    char path[4096];
    CHECK(snprintf(path, sizeof(path), "%s/entry.mjs", directory) < sizeof(path));
    CHECK(qaplet_run_entry(runtime, path, strlen(path), &progress, NULL) == QAPLET_SOURCE_TOO_LARGE);
    CHECK(qaplet_get_result(runtime, &result, NULL) == QAPLET_OK && result.number == 42);
    CHECK(qaplet_get_snapshot(runtime, &snapshot, NULL) == QAPLET_OK && snapshot.lifecycle == QAPLET_OPEN && !snapshot.source_cache_bytes);
    CHECK(!release(runtime));
    config = (qaplet_config)QAPLET_CONFIG_INIT;
    config.limits.module_count = 3;
    CHECK(qaplet_create(&config, &runtime, NULL) == QAPLET_OK);
    CHECK(qaplet_run_entry(runtime, path, strlen(path), &progress, NULL) == QAPLET_MODULE_LIMIT);
    CHECK(progress.phase == QAPLET_PHASE_DONE && progress.code == QAPLET_MODULE_LIMIT);
    CHECK(qaplet_get_snapshot(runtime, &snapshot, NULL) == QAPLET_OK && snapshot.module_count == 3 && snapshot.lifecycle == QAPLET_OPEN);
    CHECK(!scalar(runtime, "42", 42));
    CHECK(!release(runtime));
    config = (qaplet_config)QAPLET_CONFIG_INIT;
    config.limits.source_cache_bytes = 1;
    CHECK(qaplet_create(&config, &runtime, NULL) == QAPLET_OK);
    CHECK(qaplet_run_entry(runtime, path, strlen(path), &progress, NULL) == QAPLET_SOURCE_TOO_LARGE);
    CHECK(qaplet_get_snapshot(runtime, &snapshot, NULL) == QAPLET_OK && !snapshot.source_cache_bytes && snapshot.lifecycle == QAPLET_OPEN);
    CHECK(!release(runtime));
    puts("module limits: source precheck preserves result; count/cache bounded; close releases: pass");
    return 0;
  }
  CHECK(qaplet_create(&config, &runtime, NULL) == QAPLET_OK);
  if (!strcmp(mode, "--module-async")) {
    CHECK(!run(runtime, directory, "tla.mjs"));
    CHECK(!scalar(runtime, "tlaAnswer", 44));
    CHECK(!run(runtime, directory, "cycle-a.mjs"));
    CHECK(!scalar(runtime, "cycleAnswer", 42));
    CHECK(!run(runtime, directory, "namespace-then.mjs"));
    CHECK(!scalar(runtime, "namespaceRuns === 1 && typeof namespaceThenCalled === 'undefined' ? 42 : 0", 42));
    CHECK(!release(runtime));
    puts("module async: static/dynamic imports; TLA; cycle; namespace then untouched: pass");
    return 0;
  }
  if (!strcmp(mode, "--module-pending")) {
    char path[4096];
    CHECK(snprintf(path, sizeof(path), "%s/pending.mjs", directory) < sizeof(path));
    CHECK(qaplet_run_entry(runtime, path, strlen(path), &progress, NULL) == QAPLET_OK && progress.phase == QAPLET_PHASE_PENDING);
    CHECK(qaplet_pump(runtime, &progress, NULL) == QAPLET_OK && progress.phase == QAPLET_PHASE_PENDING && !progress.progressed);
    CHECK(qaplet_run_entry(runtime, path, strlen(path), &progress, NULL) == QAPLET_BUSY);
    CHECK(qaplet_eval(runtime, "42", 2, &progress, NULL) == QAPLET_BUSY);
    CHECK(qaplet_get_result(runtime, &result, NULL) == QAPLET_BAD_STATE);
    CHECK(!release(runtime));
    puts("module pending: unresolved TLA; no progress; BUSY; pending close: pass");
    return 0;
  }
  CHECK(!run(runtime, directory, "entry.mjs"));
  CHECK(!scalar(runtime, "moduleAnswer", 42));
  CHECK(!release(runtime));
  puts("modules: real static graph; completed result: pass");
  return 0;
}
