/* SPDX-License-Identifier: MIT */
/* Supplied public SDK boundary, with no private engine headers. */
#include "qaplet.h"
#include <stdio.h>
#include <string.h>
#include <pthread.h>
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "timer contract line %d: %s\n", __LINE__, #c); return 1; } } while (0)
typedef struct { pthread_t owner; unsigned calls; int valid, self_cancel, reserved; } observed;
static void delivered(qaplet_runtime *runtime, const qaplet_timer *timer, void *opaque) {
  (void)runtime; (void)timer;
  observed *state = opaque;
  ++state->calls; state->valid = pthread_equal(state->owner, pthread_self());
  qaplet_progress progress = QAPLET_PROGRESS_INIT;
  state->valid &= qaplet_eval(runtime, "1", 1, &progress, NULL) == QAPLET_BUSY &&
    qaplet_pump(runtime, &progress, NULL) == QAPLET_BUSY && qaplet_close(runtime, NULL) == QAPLET_BUSY &&
    qaplet_destroy(runtime, NULL) == QAPLET_BUSY;
  if (state->self_cancel) {
    qaplet_timer refused = QAPLET_TIMER_INIT;
    qaplet_timer_snapshot snapshot = QAPLET_TIMER_SNAPSHOT_INIT;
    state->reserved = qaplet_timer_cancel(runtime, timer, NULL) == QAPLET_OK &&
      qaplet_timer_create(runtime, 0, delivered, state, &refused, NULL) == QAPLET_RESOURCE_LIMIT &&
      qaplet_get_timer_snapshot(runtime, &snapshot, NULL) == QAPLET_OK && snapshot.current == 1;
  }
}
typedef struct { qaplet_runtime *runtime; qaplet_timer timer; int valid; } thread_check;
static void *wrong_thread(void *opaque) {
  thread_check *state = opaque;
  qaplet_timer output = QAPLET_TIMER_INIT;
  qaplet_timer_snapshot snapshot = QAPLET_TIMER_SNAPSHOT_INIT;
  state->valid = qaplet_timer_create(state->runtime, 0, delivered, NULL, &output, NULL) == QAPLET_WRONG_THREAD &&
    qaplet_timer_cancel(state->runtime, &state->timer, NULL) == QAPLET_WRONG_THREAD &&
    qaplet_timer_ref(state->runtime, &state->timer, 0, NULL) == QAPLET_WRONG_THREAD &&
    qaplet_get_timer_snapshot(state->runtime, &snapshot, NULL) == QAPLET_OK;
  return NULL;
}
static int finish(qaplet_runtime *runtime) {
  qaplet_drive_result drive = QAPLET_DRIVE_RESULT_INIT;
  qaplet_m2_snapshot snapshot = QAPLET_M2_SNAPSHOT_INIT;
  CHECK(qaplet_close(runtime, NULL) == QAPLET_OK);
  CHECK(qaplet_quiesce(runtime, 0, &drive, NULL) == QAPLET_OK && drive.reason == QAPLET_DRIVE_QUIESCENT);
  CHECK(qaplet_get_m2_snapshot(runtime, &snapshot, NULL) == QAPLET_OK);
  CHECK(!snapshot.engine_current_bytes && !snapshot.native_handles && !snapshot.native_uncertain_handles);
  qaplet_timer_snapshot timers = QAPLET_TIMER_SNAPSHOT_INIT;
  CHECK(qaplet_get_timer_snapshot(runtime, &timers, NULL) == QAPLET_OK && !timers.current && !timers.reserved_bytes);
  CHECK(qaplet_destroy(runtime, NULL) == QAPLET_OK);
  return 0;
}
int sdk_timers(const char *mode, const char *modules) {
  (void)modules;
  qaplet_config base = QAPLET_CONFIG_INIT;
  qaplet_m2_config config = QAPLET_M2_CONFIG_INIT;
  qaplet_runtime *runtime = NULL;
  qaplet_timer timer = QAPLET_TIMER_INIT;
  qaplet_drive_result drive = QAPLET_DRIVE_RESULT_INIT;
  observed state = {pthread_self(), 0, 0};
  if (!strcmp(mode, "--m2-timers-limits")) config.limits.timers = 1;
  if (!strcmp(mode, "--m2-timers-jobs")) base.limits.pump_jobs = 1;
  if (!strcmp(mode, "--m2-timers-deadline")) base.limits.execution_timeout_ms = 10;
  if (!strcmp(mode, "--m2-timers-budget")) { base.limits.execution_dispatches = 20; base.limits.pump_jobs = 1; }
  CHECK(qaplet_create_m2(&base, &config, &runtime, NULL) == QAPLET_OK);
  if (!strcmp(mode, "--m2-timers-liveness")) {
    qaplet_progress progress = QAPLET_PROGRESS_INIT;
    qaplet_result result = QAPLET_RESULT_INIT;
    CHECK(qaplet_eval(runtime, "42", 2, &progress, NULL) == QAPLET_OK);
    CHECK(qaplet_timer_create(runtime, 0, delivered, &state, &timer, NULL) == QAPLET_OK);
    CHECK(qaplet_timer_ref(runtime, &timer, 0, NULL) == QAPLET_OK);
    CHECK(qaplet_run(runtime, &drive, NULL) == QAPLET_OK && drive.reason == QAPLET_DRIVE_IDLE && !state.calls);
    CHECK(qaplet_get_result(runtime, &result, NULL) == QAPLET_OK && result.number == 42);
    CHECK(qaplet_pump(runtime, &progress, NULL) == QAPLET_OK && progress.progressed && state.calls == 1);
    CHECK(qaplet_timer_create(runtime, 5000, delivered, &state, &timer, NULL) == QAPLET_OK);
    CHECK(qaplet_timer_ref(runtime, &timer, 0, NULL) == QAPLET_OK);
    CHECK(qaplet_run_once(runtime, 10000, &drive, NULL) == QAPLET_OK && drive.reason == QAPLET_DRIVE_IDLE && state.calls == 1);
    CHECK(qaplet_timer_cancel(runtime, &timer, NULL) == QAPLET_OK);
    CHECK(qaplet_timer_create(runtime, 0, delivered, &state, &timer, NULL) == QAPLET_OK);
    CHECK(qaplet_timer_ref(runtime, &timer, 0, NULL) == QAPLET_OK);
    CHECK(qaplet_run_once(runtime, 0, &drive, NULL) == QAPLET_OK && drive.progressed && state.calls == 2);
    CHECK(qaplet_timer_create(runtime, 15, delivered, &state, &timer, NULL) == QAPLET_OK);
    CHECK(qaplet_run_once(runtime, 1, &drive, NULL) == QAPLET_OK && drive.reason == QAPLET_DRIVE_WAIT_TIMEOUT && state.calls == 2);
    CHECK(qaplet_run_once(runtime, 1000, &drive, NULL) == QAPLET_OK && drive.reason == QAPLET_DRIVE_PROGRESS && state.calls == 3);
    CHECK(qaplet_timer_create(runtime, 5000, delivered, &state, &timer, NULL) == QAPLET_OK);
    CHECK(qaplet_request_stop(runtime, NULL) == QAPLET_OK);
    CHECK(qaplet_run(runtime, &drive, NULL) == QAPLET_EXECUTION_STOPPED && state.calls == 3);
    qaplet_timer_snapshot timers = QAPLET_TIMER_SNAPSHOT_INIT;
    CHECK(qaplet_get_timer_snapshot(runtime, &timers, NULL) == QAPLET_OK && !timers.current && !timers.reserved_bytes);
    CHECK(!finish(runtime));
    puts("M2 timer liveness: DONE ref wait; unref run/pump/run_once; closest wait; stop retirement: pass");
    return 0;
  }
  if (!strcmp(mode, "--m2-timers-limits")) {
    qaplet_timer refused = {7, 8, 9};
    qaplet_timer_snapshot timers = QAPLET_TIMER_SNAPSHOT_INIT;
    CHECK(qaplet_timer_create(runtime, UINT64_MAX, delivered, &state, &refused, NULL) == QAPLET_INVALID_ARGUMENT && refused.runtime_id == 7);
    CHECK(qaplet_timer_create(runtime, UINT64_C(9007199254740991), delivered, &state, &refused, NULL) == QAPLET_INVALID_ARGUMENT);
    CHECK(qaplet_timer_create(runtime, 0, delivered, &state, &timer, NULL) == QAPLET_OK);
    CHECK(qaplet_timer_create(runtime, 0, delivered, &state, &refused, NULL) == QAPLET_RESOURCE_LIMIT && refused.runtime_id == 7);
    CHECK(qaplet_timer_cancel(runtime, &timer, NULL) == QAPLET_OK);
    CHECK(qaplet_timer_create(runtime, 0, delivered, &state, &timer, NULL) == QAPLET_OK);
    state.self_cancel = 1;
    CHECK(qaplet_run(runtime, &drive, NULL) == QAPLET_OK && state.calls == 1 && state.reserved);
    state.self_cancel = 0;
    CHECK(qaplet_get_timer_snapshot(runtime, &timers, NULL) == QAPLET_OK && !timers.current && timers.peak == 1);
    CHECK(!finish(runtime));
    config.limits.timers = 2147483647;
    qaplet_m2_diagnostic detail = QAPLET_M2_DIAGNOSTIC_INIT;
    CHECK(qaplet_create_m2_with_diagnostic(&base, &config, &runtime, &detail, NULL) == QAPLET_RESOURCE_LIMIT && !runtime && detail.creation_rollback == QAPLET_ROLLBACK_COMPLETE);
    puts("M2 timer limits: count reuse; strict C delay; metadata total-storage precheck; rollback: pass");
    return 0;
  }
  if (!strcmp(mode, "--m2-timers-strict")) {
    qaplet_progress progress = QAPLET_PROGRESS_INIT;
    qaplet_result result = QAPLET_RESULT_INIT;
    const char *source = "import('qaplet:timers').then(({setTimeout,clearTimeout})=>{let hooks=0,n=0;for(const d of [NaN,Infinity,-1,0.5,'1',null,undefined,1n,Symbol(),9007199254740992,9007199254740991,{valueOf(){++hooks;return 0}}]){try{setTimeout(()=>{},d)}catch(e){if(e.code!==1)throw e;++n}}let t=setTimeout(()=>{},5000);if(!t.hasRef())throw Error();t.unref();if(t.hasRef())throw Error();t.ref();clearTimeout(t);try{t.cancel()}catch(e){if(e.code!==25)throw e;++n}return hooks===0&&n===13})";
    CHECK(qaplet_eval(runtime, source, strlen(source), &progress, NULL) == QAPLET_OK);
    CHECK(qaplet_run(runtime, &drive, NULL) == QAPLET_OK);
    CHECK(qaplet_get_result(runtime, &result, NULL) == QAPLET_OK && result.type == QAPLET_VALUE_BOOL && result.boolean == 1);
    CHECK(!finish(runtime));
    puts("M2 timer strict JS: finite safe integer only; no coercion hooks; deadline overflow; handle controls: pass");
    return 0;
  }
  if (!strcmp(mode, "--m2-timers-jobs")) {
    qaplet_progress progress = QAPLET_PROGRESS_INIT;
    qaplet_result result = QAPLET_RESULT_INIT;
    const char *source = "import('qaplet:timers').then(({setTimeout})=>new Promise(resolve=>{let s='';setTimeout(()=>{s+='a';Promise.resolve().then(()=>s+='1').then(()=>s+='2')},0);setTimeout(()=>{if(s!=='a12')throw Error('checkpoint');resolve(42)},0)}))";
    CHECK(qaplet_eval(runtime, source, strlen(source), &progress, NULL) == QAPLET_OK);
    CHECK(qaplet_run(runtime, &drive, NULL) == QAPLET_OK);
    CHECK(qaplet_get_result(runtime, &result, NULL) == QAPLET_OK && result.number == 42);
    source = "import('qaplet:timers').then(({setTimeout})=>{globalThis.afterTimer=0;setTimeout(()=>{Promise.resolve().then(()=>afterTimer=1);throw Error('timer')},0);setTimeout(()=>{if(afterTimer!==1)throw Error('jobs lost');afterTimer=2},0)})";
    CHECK(qaplet_eval(runtime, source, strlen(source), &progress, NULL) == QAPLET_OK);
    CHECK(qaplet_run(runtime, &drive, NULL) == QAPLET_JS_EXCEPTION && drive.phase == QAPLET_PHASE_DONE);
    CHECK(qaplet_run(runtime, &drive, NULL) == QAPLET_OK);
    CHECK(qaplet_eval(runtime, "afterTimer", 10, &progress, NULL) == QAPLET_OK);
    CHECK(qaplet_get_result(runtime, &result, NULL) == QAPLET_OK && result.number == 2);
    CHECK(!finish(runtime));
    puts("M2 timer jobs: complete cross-pump checkpoints; ordinary error settles jobs; subsequent drive: pass");
    return 0;
  }
  if (!strcmp(mode, "--m2-timers-deadline")) {
    qaplet_progress progress = QAPLET_PROGRESS_INIT;
    qaplet_timer_snapshot timers = QAPLET_TIMER_SNAPSHOT_INIT;
    const char *source = "import('qaplet:timers').then(({setTimeout})=>new Promise(resolve=>setTimeout(resolve,5000)))";
    CHECK(qaplet_eval(runtime, source, strlen(source), &progress, NULL) == QAPLET_OK);
    CHECK(qaplet_run(runtime, &drive, NULL) == QAPLET_DEADLINE_EXCEEDED && drive.reason == QAPLET_DRIVE_ERROR);
    CHECK(qaplet_get_timer_snapshot(runtime, &timers, NULL) == QAPLET_OK && !timers.current && !timers.reserved_bytes);
    CHECK(!finish(runtime));
    puts("M2 timer deadline: active entry absolute deadline bounds later timer; terminal retirement: pass");
    return 0;
  }
  if (!strcmp(mode, "--m2-timers-budget")) {
    qaplet_progress progress = QAPLET_PROGRESS_INIT;
    qaplet_snapshot snapshot = QAPLET_SNAPSHOT_INIT;
    const char *source = "import('qaplet:timers').then(({setTimeout})=>new Promise(()=>{function again(){setTimeout(again,0)}again()}))";
    CHECK(qaplet_eval(runtime, source, strlen(source), &progress, NULL) == QAPLET_OK);
    qaplet_code code = QAPLET_OK;
    for (unsigned i = 0; i < 30 && !code; ++i) code = qaplet_run_once(runtime, 0, &drive, NULL);
    CHECK(code == QAPLET_DISPATCH_LIMIT);
    CHECK(qaplet_get_snapshot(runtime, &snapshot, NULL) == QAPLET_OK && snapshot.execution_dispatches == 20 && snapshot.lifecycle == QAPLET_FAILED);
    CHECK(!finish(runtime));
    puts("M2 timer budget: active entry dispatch ledger accumulates across zero-wait drives; terminal cleanup: pass");
    return 0;
  }
  if (!strcmp(mode, "--m2-timers-tla")) {
    char path[4096];
    CHECK(modules && snprintf(path, sizeof(path), "%s/timer-tla.mjs", modules) > 0);
    qaplet_progress progress = QAPLET_PROGRESS_INIT;
    qaplet_result result = QAPLET_RESULT_INIT;
    CHECK(qaplet_run_entry(runtime, path, strlen(path), &progress, NULL) == QAPLET_OK && progress.phase == QAPLET_PHASE_PENDING);
    CHECK(qaplet_run(runtime, &drive, NULL) == QAPLET_OK && drive.phase == QAPLET_PHASE_DONE);
    CHECK(qaplet_eval(runtime, "timerTla", 8, &progress, NULL) == QAPLET_OK);
    CHECK(qaplet_get_result(runtime, &result, NULL) == QAPLET_OK && result.number == 42);
    CHECK(!finish(runtime));
    puts("M2 timer TLA: real ESM entry waits and resumes; cleanup: pass");
    return 0;
  }
  if (!strcmp(mode, "--m2-timers-identity")) {
    qaplet_timer_snapshot timers = QAPLET_TIMER_SNAPSHOT_INIT;
    qaplet_runtime *other = NULL;
    struct { uint32_t struct_size, abi_version; uint64_t canary; } short_diagnostic = {8, 1, UINT64_C(0x123456789abcdef0)};
    CHECK(qaplet_timer_cancel(runtime, &timer, (qaplet_diagnostic *)&short_diagnostic) == QAPLET_ABI_MISMATCH && short_diagnostic.canary == UINT64_C(0x123456789abcdef0));
    CHECK(qaplet_create_m2(&base, &config, &other, NULL) == QAPLET_OK);
    CHECK(qaplet_timer_create(runtime, 1000, delivered, &state, &timer, NULL) == QAPLET_OK);
    thread_check check = {runtime, timer, 0}; pthread_t worker;
    CHECK(!pthread_create(&worker, NULL, wrong_thread, &check) && !pthread_join(worker, NULL) && check.valid);
    CHECK(qaplet_get_timer_snapshot(runtime, (qaplet_timer_snapshot *)&short_diagnostic, NULL) == QAPLET_ABI_MISMATCH && short_diagnostic.canary == UINT64_C(0x123456789abcdef0));
    CHECK(qaplet_get_timer_snapshot(runtime, &timers, NULL) == QAPLET_OK && timers.current == 1 && timers.referenced == 1 && !timers.ready && timers.next_timeout_ms > 0 && timers.next_timeout_ms <= 1000);
    CHECK(qaplet_timer_cancel(other, &timer, NULL) == QAPLET_INVALID_HANDLE);
    qaplet_timer stale = timer;
    CHECK(qaplet_timer_cancel(runtime, &timer, NULL) == QAPLET_OK);
    CHECK(qaplet_timer_create(runtime, 0, delivered, &state, &timer, NULL) == QAPLET_OK);
    CHECK(qaplet_timer_cancel(runtime, &stale, NULL) == QAPLET_INVALID_HANDLE);
    CHECK(qaplet_timer_ref(runtime, &stale, 0, NULL) == QAPLET_INVALID_HANDLE);
    CHECK(qaplet_get_timer_snapshot(runtime, &timers, NULL) == QAPLET_OK && timers.current == 1 && timers.ready == 1);
    CHECK(qaplet_run(runtime, &drive, NULL) == QAPLET_OK && state.calls == 1 && state.valid);
    CHECK(qaplet_timer_cancel(runtime, &timer, NULL) == QAPLET_INVALID_HANDLE);
    CHECK(qaplet_get_timer_snapshot(runtime, &timers, NULL) == QAPLET_OK && !timers.current && timers.peak == 1 && timers.next_timeout_ms == UINT64_MAX);
    CHECK(!finish(runtime) && !finish(other));
    puts("M2 timer identity: Runtime/generation isolation; stale completion; ready/ref/deadline POD; cleanup: pass");
    return 0;
  }
  if (!strcmp(mode, "--m2-timers-js")) {
    qaplet_progress progress = QAPLET_PROGRESS_INIT;
    qaplet_result result = QAPLET_RESULT_INIT;
    const char *source = "import('qaplet:timers').then(({setTimeout})=>new Promise(resolve=>{globalThis.trace='registered';setTimeout(()=>{if(trace!=='registered')throw Error('sync');resolve(42)},5)}))";
    CHECK(qaplet_eval(runtime, source, strlen(source), &progress, NULL) == QAPLET_OK);
    CHECK(qaplet_run(runtime, &drive, NULL) == QAPLET_OK && drive.phase == QAPLET_PHASE_DONE && drive.reason == QAPLET_DRIVE_IDLE);
    CHECK(qaplet_get_result(runtime, &result, NULL) == QAPLET_OK && result.type == QAPLET_VALUE_DOUBLE && result.number == 42);
    CHECK(!finish(runtime));
    puts("M2 timer JS: registered callback advances entry Promise; cleanup: pass");
    return 0;
  }
  CHECK(!strcmp(mode, "--m2-timers"));
  CHECK(qaplet_timer_create(runtime, 0, delivered, &state, &timer, NULL) == QAPLET_OK && !state.calls);
  CHECK(qaplet_run_once(runtime, 0, &drive, NULL) == QAPLET_OK && drive.progressed && state.calls == 1 && state.valid);
  CHECK(qaplet_run(runtime, &drive, NULL) == QAPLET_OK && !drive.progressed && state.calls == 1);
  CHECK(!finish(runtime));
  puts("M2 timers: zero-delay asynchronous owner callback; exactly once; cleanup: pass");
  return 0;
}
#ifdef SDK_STANDALONE
int main(int argc, char **argv) { return sdk_timers(argc > 1 ? argv[1] : "--m2-timers", argc > 2 ? argv[2] : NULL); }
#endif
