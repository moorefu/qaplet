/* SPDX-License-Identifier: MIT */
/* Public supplied-SDK boundary: deliberately no private runtime headers. */
#include "qaplet.h"
#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <stdint.h>
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "M2 public contract line %d: %s\n", __LINE__, #c); return 1; } } while (0)
static int drive_contract(void) {
  qaplet_config base = QAPLET_CONFIG_INIT;
  base.limits.pump_jobs = 1;
  qaplet_m2_config config = QAPLET_M2_CONFIG_INIT;
  qaplet_runtime *runtime = NULL;
  qaplet_progress progress = QAPLET_PROGRESS_INIT;
  qaplet_drive_result drive = QAPLET_DRIVE_RESULT_INIT;
  qaplet_result result = QAPLET_RESULT_INIT;
  CHECK(qaplet_create_m2(&base, &config, &runtime, NULL) == QAPLET_OK);
  const char source[] = "Promise.resolve(40).then(x=>x+1).then(x=>x+1)";
  CHECK(qaplet_eval(runtime, source, sizeof(source)-1, &progress, NULL) == QAPLET_OK && progress.phase == QAPLET_PHASE_PENDING);
  CHECK(qaplet_run(runtime, &drive, NULL) == QAPLET_OK);
  CHECK(drive.reason == QAPLET_DRIVE_IDLE && drive.progressed && drive.phase == QAPLET_PHASE_DONE);
  CHECK(qaplet_get_result(runtime, &result, NULL) == QAPLET_OK && result.number == 42);
  CHECK(qaplet_pump(runtime, &progress, NULL) == QAPLET_OK && !progress.progressed);
  CHECK(qaplet_run_once(runtime, 30000, &drive, NULL) == QAPLET_OK && drive.reason == QAPLET_DRIVE_IDLE && !drive.progressed);
  CHECK(qaplet_get_result(runtime, &result, NULL) == QAPLET_OK && result.number == 42);
  const char pending[] = "new Promise(()=>{})";
  CHECK(qaplet_eval(runtime, pending, sizeof(pending)-1, &progress, NULL) == QAPLET_OK);
  CHECK(qaplet_pump(runtime, &progress, NULL) == QAPLET_OK && progress.phase == QAPLET_PHASE_PENDING && !progress.progressed);
  CHECK(qaplet_run_once(runtime, 0, &drive, NULL) == QAPLET_OK && drive.reason == QAPLET_DRIVE_UNSETTLED_ENTRY && drive.phase == QAPLET_PHASE_PENDING);
  CHECK(qaplet_run(runtime, &drive, NULL) == QAPLET_OK && drive.reason == QAPLET_DRIVE_UNSETTLED_ENTRY);
  CHECK(qaplet_close(runtime, NULL) == QAPLET_OK);
  CHECK(qaplet_quiesce(runtime, 10, &drive, NULL) == QAPLET_OK && drive.reason == QAPLET_DRIVE_QUIESCENT);
  CHECK(qaplet_destroy(runtime, NULL) == QAPLET_OK);
  puts("M2 drive: ready jobs; independent progress/reason; idle result retained; unsettled entry; no implicit close: pass");
  return 0;
}
static int finish(qaplet_runtime *runtime) {
  qaplet_drive_result result = QAPLET_DRIVE_RESULT_INIT;
  qaplet_m2_snapshot snapshot = QAPLET_M2_SNAPSHOT_INIT;
  CHECK(qaplet_close(runtime, NULL) == QAPLET_OK);
  CHECK(qaplet_close(runtime, NULL) == QAPLET_OK);
  CHECK(qaplet_quiesce(runtime, 0, &result, NULL) == QAPLET_OK && result.reason == QAPLET_DRIVE_QUIESCENT);
  CHECK(qaplet_quiesce(runtime, 100, &result, NULL) == QAPLET_OK && result.reason == QAPLET_DRIVE_QUIESCENT);
  CHECK(qaplet_get_m2_snapshot(runtime, &snapshot, NULL) == QAPLET_OK);
  CHECK(snapshot.lifecycle == QAPLET_CLOSED && snapshot.native_state == QAPLET_NATIVE_QUIESCENT && !snapshot.engine_current_bytes);
  CHECK(snapshot.native_current_bytes > 0 && !snapshot.cleanup_reason);
  CHECK(qaplet_destroy(runtime, NULL) == QAPLET_OK);
  return 0;
}
static int configuration(void) {
  qaplet_config base = QAPLET_CONFIG_INIT;
  qaplet_m2_config config = QAPLET_M2_CONFIG_INIT;
  qaplet_runtime *runtime = NULL;
  qaplet_abi abi = QAPLET_ABI_INIT;
  qaplet_m2_snapshot snapshot = QAPLET_M2_SNAPSHOT_INIT;
  qaplet_drive_result result = QAPLET_DRIVE_RESULT_INIT;
  CHECK(qaplet_query_abi(&abi) == QAPLET_OK && (abi.capabilities & QAPLET_CAP_M2_RUNTIME));
  CHECK(qaplet_create(&base, &runtime, NULL) == QAPLET_OK);
  CHECK(qaplet_get_m2_snapshot(runtime, &snapshot, NULL) == QAPLET_OK);
  CHECK(snapshot.capabilities == (abi.capabilities & ~((uint64_t)QAPLET_CAP_M2_RUNTIME | QAPLET_CAP_M2_WAIT)));
  CHECK(snapshot.m2_limits.worker_threads == 0 && snapshot.m2_limits.operations == 0);
  CHECK(qaplet_run_once(runtime, 0, &result, NULL) == QAPLET_UNSUPPORTED);
  CHECK(qaplet_quiesce(runtime, 0, &result, NULL) == QAPLET_UNSUPPORTED);
  CHECK(qaplet_close(runtime, NULL) == QAPLET_OK && qaplet_destroy(runtime, NULL) == QAPLET_OK);
  CHECK(qaplet_create_m2(&base, &config, &runtime, NULL) == QAPLET_OK);
  CHECK(qaplet_get_m2_snapshot(runtime, &snapshot, NULL) == QAPLET_OK && snapshot.capabilities == abi.capabilities);
  CHECK(snapshot.m2_limits.worker_threads == 2 && snapshot.m2_limits.helper_threads == 4);
  CHECK(snapshot.m2_limits.worker_stack_bytes == 1048576 && snapshot.m2_limits.helper_stack_bytes == 1048576);
  CHECK(snapshot.m2_limits.work_handlers == 64 && snapshot.m2_limits.handler_concurrency == 1);
  CHECK(snapshot.m2_limits.timers == 1024 && snapshot.m2_limits.reactor_registrations == 256 && snapshot.m2_limits.native_handles == 256);
  CHECK(snapshot.m2_limits.processes == 32 && snapshot.m2_limits.keepalive_tokens == 64 && snapshot.m2_limits.operations == 128);
  CHECK(snapshot.m2_limits.operation_data_bytes == 8388608 && snapshot.m2_limits.work_input_bytes == 1048576 && snapshot.m2_limits.work_output_bytes == 1048576);
  CHECK(snapshot.m2_limits.io_block_bytes == 65536 && snapshot.m2_limits.process_arguments == 256 && snapshot.m2_limits.process_data_bytes == 1048576 && snapshot.m2_limits.pump_io_bytes == 262144);
  CHECK(!finish(runtime));
  config.limits.worker_threads = 1; config.limits.helper_threads = 1;
  config.limits.operations = 3; config.limits.operation_data_bytes = 65536;
  CHECK(qaplet_create_m2(&base, &config, &runtime, NULL) == QAPLET_OK);
  config.limits.operations = 99;
  CHECK(qaplet_get_m2_snapshot(runtime, &snapshot, NULL) == QAPLET_OK && snapshot.m2_limits.operations == 3 && snapshot.m2_limits.worker_threads == 1);
  CHECK(!finish(runtime));
  config = (qaplet_m2_config)QAPLET_M2_CONFIG_INIT;
  struct { uint32_t struct_size, abi_version; uint64_t canary; } short_output = {8, 1, UINT64_C(0x123456789abcdef0)};
  CHECK(qaplet_create_m2(&base, &config, &runtime, NULL) == QAPLET_OK);
  CHECK(qaplet_get_m2_snapshot(runtime, (qaplet_m2_snapshot *)&short_output, NULL) == QAPLET_ABI_MISMATCH && short_output.canary == UINT64_C(0x123456789abcdef0));
  CHECK(qaplet_run_once(runtime, 0, (qaplet_drive_result *)&short_output, NULL) == QAPLET_ABI_MISMATCH && short_output.canary == UINT64_C(0x123456789abcdef0));
  struct { qaplet_m2_snapshot value; uint64_t tail; } extended_output = {QAPLET_M2_SNAPSHOT_INIT, 0};
  extended_output.value.struct_size = sizeof(extended_output);
  CHECK(qaplet_get_m2_snapshot(runtime, &extended_output.value, NULL) == QAPLET_OK && !extended_output.tail);
  extended_output.tail = 1;
  CHECK(qaplet_get_m2_snapshot(runtime, &extended_output.value, NULL) == QAPLET_ABI_MISMATCH);
  CHECK(qaplet_run_once(runtime, UINT64_MAX, &result, NULL) == QAPLET_INVALID_ARGUMENT);
  CHECK(!finish(runtime));
#define REJECT(expected) do { runtime = (qaplet_runtime *)(uintptr_t)1; CHECK(qaplet_create_m2(&base, &config, &runtime, NULL) == (expected) && !runtime); config = (qaplet_m2_config)QAPLET_M2_CONFIG_INIT; } while (0)
  config.struct_size = 8; REJECT(QAPLET_ABI_MISMATCH);
  config.abi_version = 2; REJECT(QAPLET_ABI_MISMATCH);
  config.limits.struct_size = 8; REJECT(QAPLET_ABI_MISMATCH);
  config.flags = 1; REJECT(QAPLET_INVALID_ARGUMENT);
  config.reserved[3] = 1; REJECT(QAPLET_INVALID_ARGUMENT);
  config.limits.worker_threads = UINT64_MAX; REJECT(QAPLET_INVALID_ARGUMENT);
  config.limits.worker_stack_bytes = UINT64_MAX; REJECT(QAPLET_INVALID_ARGUMENT);
  config.limits.worker_stack_bytes = 1; REJECT(QAPLET_INVALID_ARGUMENT);
  config.limits.handler_concurrency = 3; REJECT(QAPLET_INVALID_ARGUMENT);
  config.limits.operation_data_bytes = 16777217; REJECT(QAPLET_INVALID_ARGUMENT);
  config.limits.work_input_bytes = 1048577; REJECT(QAPLET_INVALID_ARGUMENT);
  config.limits.work_output_bytes = 1048577; REJECT(QAPLET_INVALID_ARGUMENT);
  config.limits.io_block_bytes = 1048577; REJECT(QAPLET_INVALID_ARGUMENT);
#undef REJECT
  struct { qaplet_m2_config value; uint64_t tail; } extended = {QAPLET_M2_CONFIG_INIT, 0};
  extended.value.struct_size = sizeof(extended);
  CHECK(qaplet_create_m2(&base, &extended.value, &runtime, NULL) == QAPLET_OK && !finish(runtime));
  extended.tail = 1;
  CHECK(qaplet_create_m2(&base, &extended.value, &runtime, NULL) == QAPLET_ABI_MISMATCH && !runtime);
  CHECK(qaplet_create_m2(&base, NULL, &runtime, NULL) == QAPLET_INVALID_ARGUMENT && !runtime);
  puts("M2 configuration: explicit capabilities; finite copied defaults; sizes/version/tails; invalid combinations: pass");
  return 0;
}

typedef struct { qaplet_runtime *runtime; int passed; } foreign_state;
static void *foreign(void *opaque) {
  foreign_state *state = opaque;
  qaplet_drive_result result = QAPLET_DRIVE_RESULT_INIT;
  qaplet_m2_snapshot snapshot = QAPLET_M2_SNAPSHOT_INIT;
  state->passed = qaplet_run_once(state->runtime, 0, &result, NULL) == QAPLET_WRONG_THREAD &&
    qaplet_run(state->runtime, &result, NULL) == QAPLET_WRONG_THREAD &&
    qaplet_quiesce(state->runtime, 0, &result, NULL) == QAPLET_WRONG_THREAD &&
    qaplet_close(state->runtime, NULL) == QAPLET_WRONG_THREAD &&
    qaplet_destroy(state->runtime, NULL) == QAPLET_WRONG_THREAD &&
    qaplet_get_m2_snapshot(state->runtime, &snapshot, NULL) == QAPLET_OK;
  return NULL;
}
static qaplet_code reentrant(qaplet_runtime *runtime, uint64_t call, size_t argc,
                            const qaplet_value *argv, void *userdata) {
  (void)argc; (void)argv;
  unsigned *called = userdata;
  qaplet_drive_result drive = QAPLET_DRIVE_RESULT_INIT;
  qaplet_progress progress = QAPLET_PROGRESS_INIT;
  qaplet_m2_snapshot snapshot = QAPLET_M2_SNAPSHOT_INIT;
  qaplet_result result = QAPLET_RESULT_INIT;
  qaplet_host_module module = QAPLET_HOST_MODULE_INIT;
  module.name = "host:other"; module.name_length = 10;
  if (qaplet_run(runtime, &drive, NULL) != QAPLET_BUSY ||
      qaplet_run_once(runtime, 0, &drive, NULL) != QAPLET_BUSY ||
      qaplet_quiesce(runtime, 0, &drive, NULL) != QAPLET_BUSY ||
      qaplet_pump(runtime, &progress, NULL) != QAPLET_BUSY ||
      qaplet_eval(runtime, "1", 1, &progress, NULL) != QAPLET_BUSY ||
      qaplet_register_host_module(runtime, &module, NULL) != QAPLET_BUSY ||
      qaplet_get_result(runtime, &result, NULL) != QAPLET_BUSY ||
      qaplet_close(runtime, NULL) != QAPLET_BUSY || qaplet_destroy(runtime, NULL) != QAPLET_BUSY ||
      qaplet_get_m2_snapshot(runtime, &snapshot, NULL) != QAPLET_OK || !snapshot.driving ||
      qaplet_post_event(runtime, 0, 0, NULL, 0, NULL) != QAPLET_NO_HANDLER)
    return QAPLET_HOST_ERROR;
  qaplet_value value = QAPLET_VALUE_INIT;
  value.type = QAPLET_VALUE_DOUBLE; value.number = 42;
  ++*called;
  return qaplet_host_set_result(runtime, call, &value, NULL);
}
static int owner_contract(void) {
  for (unsigned kind = 0; kind < 3; ++kind) {
    qaplet_config base = QAPLET_CONFIG_INIT;
    base.limits.pump_jobs = 1;
    qaplet_m2_config config = QAPLET_M2_CONFIG_INIT;
    qaplet_runtime *runtime = NULL;
    CHECK(qaplet_create_m2(&base, &config, &runtime, NULL) == QAPLET_OK);
    foreign_state state = {runtime, 0}; pthread_t thread;
    CHECK(!pthread_create(&thread, NULL, foreign, &state) && !pthread_join(thread, NULL) && state.passed);
    unsigned called = 0;
    qaplet_host_export item = QAPLET_HOST_EXPORT_INIT;
    item.name = "check"; item.name_length = 5; item.kind = QAPLET_HOST_FUNCTION; item.function = reentrant; item.userdata = &called;
    qaplet_host_module module = QAPLET_HOST_MODULE_INIT;
    module.name = "host:reentry"; module.name_length = 12; module.exports = &item; module.export_count = 1;
    CHECK(qaplet_register_host_module(runtime, &module, NULL) == QAPLET_OK);
    qaplet_progress progress = QAPLET_PROGRESS_INIT;
    qaplet_drive_result drive = QAPLET_DRIVE_RESULT_INIT;
    const char source[] = "import('host:reentry').then(m=>m.check())";
    CHECK(qaplet_eval(runtime, source, sizeof(source)-1, &progress, NULL) == QAPLET_OK && progress.phase == QAPLET_PHASE_PENDING);
    for (unsigned attempt = 0; attempt < 100 && progress.phase == QAPLET_PHASE_PENDING; ++attempt) {
      qaplet_code code = kind == 0 ? qaplet_pump(runtime, &progress, NULL) : kind == 1 ?
        qaplet_run_once(runtime, 0, &drive, NULL) : qaplet_run(runtime, &drive, NULL);
      CHECK(code == QAPLET_OK);
      if (kind) progress.phase = drive.phase;
    }
    qaplet_result result = QAPLET_RESULT_INIT;
    CHECK(called == 1 && qaplet_get_result(runtime, &result, NULL) == QAPLET_OK && result.number == 42);
    CHECK(qaplet_close(runtime, NULL) == QAPLET_OK);
    state.passed = 0;
    CHECK(!pthread_create(&thread, NULL, foreign, &state) && !pthread_join(thread, NULL) && state.passed);
    CHECK(qaplet_request_stop(runtime, NULL) == QAPLET_OK);
    CHECK(qaplet_quiesce(runtime, 1, &drive, NULL) == QAPLET_OK && qaplet_destroy(runtime, NULL) == QAPLET_OK);
  }
  puts("M2 owner: foreign calls refused; pump/run_once/run guard; safe POD/control/setter; CLOSED query: pass");
  return 0;
}
static int creation_contract(void) {
  unsigned heap_refusals = 0, native_refusals = 0, successes = 0;
  for (unsigned kind = 0; kind < 2; ++kind) {
    for (unsigned i = 0; i < 128; ++i) {
      qaplet_config base = QAPLET_CONFIG_INIT;
      qaplet_m2_config config = QAPLET_M2_CONFIG_INIT;
      qaplet_runtime *runtime = NULL;
      if (!kind) base.limits.engine_heap_bytes = 1 + i * 4096;
      else {
        base.limits.native_storage_bytes = 1 + i * 4096;
        config.limits.operation_data_bytes = 1; config.limits.process_data_bytes = 1; config.limits.io_block_bytes = 1;
      }
      qaplet_code code = qaplet_create_m2(&base, &config, &runtime, NULL);
      if (code) {
        CHECK(code == QAPLET_MEMORY_LIMIT && !runtime);
        if (kind) ++native_refusals; else ++heap_refusals;
      } else { ++successes; CHECK(!finish(runtime)); }
      base = (qaplet_config)QAPLET_CONFIG_INIT; config = (qaplet_m2_config)QAPLET_M2_CONFIG_INIT;
      CHECK(qaplet_create_m2(&base, &config, &runtime, NULL) == QAPLET_OK);
      qaplet_progress progress = QAPLET_PROGRESS_INIT;
      CHECK(qaplet_eval(runtime, "6*7", 3, &progress, NULL) == QAPLET_OK);
      CHECK(!finish(runtime));
    }
  }
  CHECK(heap_refusals && native_refusals && successes);
  puts("M2 creation: 256 public heap/native budgets; unpublished null failures; fresh recovery and cleanup: pass");
  return 0;
}

static int close_contract(void) {
  for (unsigned state = 0; state < 3; ++state) {
    qaplet_config base = QAPLET_CONFIG_INIT;
    if (state == 2) base.limits.execution_dispatches = 1;
    qaplet_m2_config config = QAPLET_M2_CONFIG_INIT;
    qaplet_runtime *runtime = NULL;
    qaplet_drive_result drive = QAPLET_DRIVE_RESULT_INIT;
    qaplet_progress progress = QAPLET_PROGRESS_INIT;
    qaplet_m2_snapshot snapshot = QAPLET_M2_SNAPSHOT_INIT;
    CHECK(qaplet_create_m2(&base, &config, &runtime, NULL) == QAPLET_OK);
    qaplet_code reason = state == 2 ? QAPLET_DISPATCH_LIMIT : QAPLET_EXECUTION_STOPPED;
    if (state < 2) {
      CHECK(qaplet_request_stop(runtime, NULL) == QAPLET_OK);
      if (state) CHECK(qaplet_run_once(runtime, 0, &drive, NULL) == reason && drive.reason == QAPLET_DRIVE_STOPPED && drive.code == reason);
    } else {
      const char source[] = "Promise.resolve().then(()=>1).then(()=>2)";
      CHECK(qaplet_eval(runtime, source, sizeof(source)-1, &progress, NULL) == QAPLET_OK);
      CHECK(qaplet_run(runtime, &drive, NULL) == reason && drive.reason == QAPLET_DRIVE_ERROR);
    }
    CHECK(qaplet_get_m2_snapshot(runtime, &snapshot, NULL) == QAPLET_OK && snapshot.termination_reason == reason);
    CHECK(snapshot.lifecycle == (state == 0 ? QAPLET_STOP_REQUESTED : state == 1 ? QAPLET_STOPPED : QAPLET_FAILED));
    CHECK(qaplet_close(runtime, NULL) == QAPLET_OK && qaplet_request_stop(runtime, NULL) == QAPLET_OK);
    CHECK(qaplet_quiesce(runtime, 10, &drive, NULL) == QAPLET_OK && drive.reason == QAPLET_DRIVE_QUIESCENT);
    CHECK(qaplet_get_m2_snapshot(runtime, &snapshot, NULL) == QAPLET_OK && snapshot.lifecycle == QAPLET_CLOSED && snapshot.termination_reason == reason);
    CHECK(!finish(runtime));
  }
  puts("M2 close: STOP_REQUESTED/STOPPED/FAILED; first cause retained; CLOSED never revived; quiesce retry: pass");
  return 0;
}

int sdk_m2_runtime(const char *mode) {
  if (!strcmp(mode, "--m2-close")) return close_contract();
  if (!strcmp(mode, "--m2-drive")) return drive_contract();
  if (!strcmp(mode, "--m2-configuration")) return configuration();
  if (!strcmp(mode, "--m2-owner")) return owner_contract();
  if (!strcmp(mode, "--m2-creation")) return creation_contract();
  qaplet_config base = QAPLET_CONFIG_INIT;
  qaplet_m2_config config = QAPLET_M2_CONFIG_INIT;
  qaplet_runtime *runtime = NULL;
  qaplet_m2_snapshot snapshot = QAPLET_M2_SNAPSHOT_INIT;
  qaplet_drive_result result = QAPLET_DRIVE_RESULT_INIT;
  CHECK(qaplet_create_m2(&base, &config, &runtime, NULL) == QAPLET_OK && runtime);
  CHECK(qaplet_get_m2_snapshot(runtime, &snapshot, NULL) == QAPLET_OK);
  CHECK(snapshot.capabilities & QAPLET_CAP_M2_RUNTIME);
  CHECK(snapshot.lifecycle == QAPLET_OPEN && snapshot.native_state == QAPLET_NATIVE_RUNNING);
  CHECK(qaplet_run_once(runtime, 0, &result, NULL) == QAPLET_OK);
  CHECK(result.reason == QAPLET_DRIVE_IDLE && !result.progressed && result.phase == QAPLET_PHASE_IDLE);
  CHECK(qaplet_destroy(runtime, NULL) == QAPLET_BAD_STATE);
  CHECK(qaplet_quiesce(runtime, 0, &result, NULL) == QAPLET_BAD_STATE);
  CHECK(qaplet_close(runtime, NULL) == QAPLET_OK);
  CHECK(qaplet_get_m2_snapshot(runtime, &snapshot, NULL) == QAPLET_OK);
  CHECK(snapshot.lifecycle == QAPLET_CLOSED && snapshot.native_state == QAPLET_NATIVE_QUIESCENT);
  CHECK(qaplet_quiesce(runtime, 0, &result, NULL) == QAPLET_OK && result.reason == QAPLET_DRIVE_QUIESCENT);
  CHECK(qaplet_destroy(runtime, NULL) == QAPLET_OK);
  puts("M2 runtime: explicit empty instance; idle drive; CLOSED/native quiescent; destroy: pass");
  return 0;
}

#ifdef SDK_STANDALONE
int main(int argc, char **argv) { return argc == 2 ? sdk_m2_runtime(argv[1]) : 2; }
#endif
