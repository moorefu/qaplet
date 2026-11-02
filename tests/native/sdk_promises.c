/* SPDX-License-Identifier: MIT */
/* Promise behavior is observed only through the distributed public SDK. */
#include "qaplet.h"
#include <pthread.h>
#include <stdio.h>
#include <string.h>

#define CHECK(condition) do { if (!(condition)) { \
  fprintf(stderr, "public Promise contract failed at line %d: %s\n", __LINE__, #condition); return 1; \
} } while (0)

static int scalar(qaplet_runtime *runtime, const char *source, double expected) {
  qaplet_progress progress = QAPLET_PROGRESS_INIT;
  qaplet_abi abi = QAPLET_ABI_INIT;
  CHECK(qaplet_query_abi(&abi) == QAPLET_OK && (abi.capabilities & QAPLET_CAP_PROMISE_JOBS));
  qaplet_result result = QAPLET_RESULT_INIT;
  CHECK(qaplet_eval(runtime, source, strlen(source), &progress, NULL) == QAPLET_OK);
  CHECK(progress.phase == QAPLET_PHASE_DONE);
  CHECK(qaplet_get_result(runtime, &result, NULL) == QAPLET_OK);
  CHECK(result.type == QAPLET_VALUE_DOUBLE && result.number == expected);
  return 0;
}

typedef struct { qaplet_runtime *runtime; int passed; } worker_state;
static void *foreign_pump(void *opaque) {
  worker_state *state = opaque;
  qaplet_progress progress = QAPLET_PROGRESS_INIT;
  state->passed = qaplet_pump(state->runtime, &progress, NULL) == QAPLET_WRONG_THREAD;
  return NULL;
}

int sdk_promises(void) {
  qaplet_config config = QAPLET_CONFIG_INIT;
  qaplet_runtime *runtime = NULL;
  qaplet_progress progress = QAPLET_PROGRESS_INIT;
  CHECK(qaplet_create(&config, &runtime, NULL) == QAPLET_OK);
  CHECK(qaplet_pump(runtime, &progress, NULL) == QAPLET_OK && !progress.progressed && progress.phase == QAPLET_PHASE_IDLE);
  const char source[] = "Promise.resolve(41).then(value => value + 1)";
  CHECK(qaplet_eval(runtime, source, sizeof(source) - 1, &progress, NULL) == QAPLET_OK);
  CHECK(progress.phase == QAPLET_PHASE_PENDING && progress.progressed);
  CHECK(qaplet_pump(runtime, &progress, NULL) == QAPLET_OK);
  CHECK(progress.phase == QAPLET_PHASE_DONE && progress.progressed);
  qaplet_result result = QAPLET_RESULT_INIT;
  CHECK(qaplet_get_result(runtime, &result, NULL) == QAPLET_OK && result.number == 42);
  const char chain[] = "var chain = Promise.resolve(0); for (let i=0; i<600; ++i) chain=chain.then(n=>n+1); chain";
  CHECK(qaplet_eval(runtime, chain, sizeof(chain) - 1, &progress, NULL) == QAPLET_OK);
  CHECK(progress.phase == QAPLET_PHASE_PENDING);
  CHECK(qaplet_eval(runtime, "0", 1, &progress, NULL) == QAPLET_BUSY);
  CHECK(qaplet_get_result(runtime, &result, NULL) == QAPLET_BAD_STATE);
  CHECK(qaplet_pump(runtime, &progress, NULL) == QAPLET_OK && progress.progressed && progress.phase == QAPLET_PHASE_PENDING);
  CHECK(qaplet_pump(runtime, &progress, NULL) == QAPLET_OK && progress.progressed && progress.phase == QAPLET_PHASE_PENDING);
  CHECK(qaplet_pump(runtime, &progress, NULL) == QAPLET_OK && progress.progressed && progress.phase == QAPLET_PHASE_DONE);
  CHECK(qaplet_get_result(runtime, &result, NULL) == QAPLET_OK && result.number == 600);
  const char orphan[] = "Promise.reject(new Error('orphan rejection')); 42";
  CHECK(qaplet_eval(runtime, orphan, sizeof(orphan) - 1, &progress, NULL) == QAPLET_JS_EXCEPTION);
  CHECK(progress.phase == QAPLET_PHASE_DONE && progress.code == QAPLET_JS_EXCEPTION);
  CHECK(qaplet_get_result(runtime, &result, NULL) == QAPLET_JS_EXCEPTION);
  CHECK(qaplet_pump(runtime, &progress, NULL) == QAPLET_OK && !progress.progressed);
  CHECK(qaplet_eval(runtime, "42", 2, &progress, NULL) == QAPLET_OK);
  const char handled[] = "var held=Promise.reject(new Error('handled late')); var delayed=Promise.resolve(); "
      "for(let i=0;i<300;++i) delayed=delayed.then(()=>{}); delayed.then(()=>held.catch(()=>{})); 42";
  CHECK(qaplet_eval(runtime, handled, sizeof(handled) - 1, &progress, NULL) == QAPLET_OK && progress.phase == QAPLET_PHASE_PENDING);
  CHECK(qaplet_pump(runtime, &progress, NULL) == QAPLET_OK && progress.phase == QAPLET_PHASE_PENDING);
  CHECK(qaplet_pump(runtime, &progress, NULL) == QAPLET_OK && progress.phase == QAPLET_PHASE_DONE);
  CHECK(qaplet_get_result(runtime, &result, NULL) == QAPLET_OK);
  CHECK(result.number == 42);
  /* Rejected entry is reported once; a later call sees only its own outcome. */
  const char rejected_entry[] = "Promise.resolve().then(()=>{throw new Error('entry rejection')})";
  CHECK(qaplet_eval(runtime, rejected_entry, sizeof(rejected_entry) - 1, &progress, NULL) == QAPLET_OK);
  CHECK(qaplet_pump(runtime, &progress, NULL) == QAPLET_JS_EXCEPTION && progress.phase == QAPLET_PHASE_DONE);
  CHECK(qaplet_pump(runtime, &progress, NULL) == QAPLET_OK && !progress.progressed);
  CHECK(qaplet_get_result(runtime, &result, NULL) == QAPLET_JS_EXCEPTION);
  CHECK(!scalar(runtime, "42", 42));

  /* Ordinary failures preserve mutations and settle all runnable jobs. */
  const char synchronous[] = "globalThis.effect=0; Promise.resolve().then(()=>{effect=42}); throw new Error('sync failure')";
  CHECK(qaplet_eval(runtime, synchronous, sizeof(synchronous) - 1, &progress, NULL) == QAPLET_OK && progress.phase == QAPLET_PHASE_PENDING);
  CHECK(qaplet_pump(runtime, &progress, NULL) == QAPLET_JS_EXCEPTION && progress.phase == QAPLET_PHASE_DONE && progress.progressed);
  CHECK(!scalar(runtime, "effect", 42));
  const char job_failure[] = "Promise.resolve().then(()=>{effect=43; throw new Error('job failure')}); "
      "Promise.resolve().then(()=>{effect+=1}); 0";
  CHECK(qaplet_eval(runtime, job_failure, sizeof(job_failure) - 1, &progress, NULL) == QAPLET_OK);
  CHECK(qaplet_pump(runtime, &progress, NULL) == QAPLET_JS_EXCEPTION && progress.phase == QAPLET_PHASE_DONE);
  CHECK(!scalar(runtime, "effect", 44));
  const char pending_failure[] = "Promise.reject(new Error('unhandled')); new Promise(()=>{})";
  CHECK(qaplet_eval(runtime, pending_failure, sizeof(pending_failure) - 1, &progress, NULL) == QAPLET_JS_EXCEPTION);
  CHECK(progress.phase == QAPLET_PHASE_DONE);
  CHECK(!scalar(runtime, "effect", 44));

  /* Neither arbitrary thenables nor Promise proxies may run getters. */
  const char thenable[] = "globalThis.getters=0; ({get then(){++getters; throw new Error('must not run')}})";
  CHECK(qaplet_eval(runtime, thenable, sizeof(thenable) - 1, &progress, NULL) == QAPLET_VALUE_TYPE_UNSUPPORTED);
  CHECK(!scalar(runtime, "getters", 0));
  const char proxy[] = "new Proxy(Promise.resolve(42), {get(){++getters; throw new Error('must not run')}})";
  CHECK(qaplet_eval(runtime, proxy, sizeof(proxy) - 1, &progress, NULL) == QAPLET_VALUE_TYPE_UNSUPPORTED);
  CHECK(!scalar(runtime, "getters", 0));
  /* Polling a Promise must not consult constructor / Symbol.species either. */
  const char species[] = "var genuine=Promise.resolve(42); Object.defineProperty(genuine,'constructor',"
      "{get(){++getters; throw new Error('must not run')}}); genuine";
  CHECK(qaplet_eval(runtime, species, sizeof(species) - 1, &progress, NULL) == QAPLET_OK && progress.phase == QAPLET_PHASE_DONE);
  CHECK(qaplet_get_result(runtime, &result, NULL) == QAPLET_OK && result.number == 42);
  CHECK(!scalar(runtime, "getters", 0));

  /* Finishing exactly the quota still completes this checkpoint. */
  const char exact[] = "var exact=Promise.resolve(0); for(let i=0;i<256;++i) exact=exact.then(n=>n+1); exact";
  CHECK(qaplet_eval(runtime, exact, sizeof(exact) - 1, &progress, NULL) == QAPLET_OK && progress.phase == QAPLET_PHASE_PENDING);
  CHECK(qaplet_pump(runtime, &progress, NULL) == QAPLET_OK && progress.phase == QAPLET_PHASE_DONE);
  CHECK(qaplet_get_result(runtime, &result, NULL) == QAPLET_OK && result.number == 256);

  const char pending[] = "new Promise(()=>{})";
  CHECK(qaplet_eval(runtime, pending, sizeof(pending) - 1, &progress, NULL) == QAPLET_OK && progress.phase == QAPLET_PHASE_PENDING);
  CHECK(qaplet_pump(runtime, &progress, NULL) == QAPLET_OK && progress.phase == QAPLET_PHASE_PENDING && !progress.progressed);
  CHECK(qaplet_pump(runtime, &progress, NULL) == QAPLET_OK && progress.phase == QAPLET_PHASE_PENDING && !progress.progressed);
  CHECK(qaplet_eval(runtime, "42", 2, &progress, NULL) == QAPLET_BUSY);
  pthread_t thread;
  worker_state worker = {runtime, 0};
  CHECK(pthread_create(&thread, NULL, foreign_pump, &worker) == 0);
  CHECK(pthread_join(thread, NULL) == 0 && worker.passed);
  CHECK(qaplet_close(runtime, NULL) == QAPLET_OK);
  qaplet_snapshot snapshot = QAPLET_SNAPSHOT_INIT;
  CHECK(qaplet_get_snapshot(runtime, &snapshot, NULL) == QAPLET_OK);
  CHECK(snapshot.lifecycle == QAPLET_CLOSED && snapshot.engine_current_bytes == 0);
  CHECK(qaplet_pump(runtime, &progress, NULL) == QAPLET_BAD_STATE);
  CHECK(qaplet_destroy(runtime, NULL) == QAPLET_OK);
  /* Outstanding jobs/rejections are released rather than executed on close. */
  CHECK(qaplet_create(&config, &runtime, NULL) == QAPLET_OK);
  CHECK(qaplet_get_snapshot(runtime, &snapshot, NULL) == QAPLET_OK);
  uint64_t native_baseline = snapshot.native_current_bytes;
  const char outstanding[] = "Promise.reject(new Error('discard on close')); var outstanding=Promise.resolve(); "
      "for(let i=0;i<600;++i) outstanding=outstanding.then(()=>{}); outstanding";
  CHECK(qaplet_eval(runtime, outstanding, sizeof(outstanding) - 1, &progress, NULL) == QAPLET_OK);
  CHECK(qaplet_close(runtime, NULL) == QAPLET_OK);
  CHECK(qaplet_get_snapshot(runtime, &snapshot, NULL) == QAPLET_OK && snapshot.engine_current_bytes == 0 &&
        snapshot.native_current_bytes == native_baseline);
  CHECK(qaplet_destroy(runtime, NULL) == QAPLET_OK);
  puts("promises: explicit pump; bounded jobs; checkpoints; failure reuse; pending close: pass");
  return 0;
}
