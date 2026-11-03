/* SPDX-License-Identifier: MIT */
/* Public supplied SDK contract; no private engine or Runtime layout. */
#include "qaplet.h"
#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <time.h>
#include <errno.h>
#include <sched.h>
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "wait contract line %d: %s\n", __LINE__, #c); return 1; } } while (0)
static int finish(qaplet_runtime *runtime) {
  qaplet_drive_result drive = QAPLET_DRIVE_RESULT_INIT;
  qaplet_m2_snapshot snapshot = QAPLET_M2_SNAPSHOT_INIT;
  CHECK(qaplet_close(runtime, NULL) == QAPLET_OK);
  CHECK(qaplet_quiesce(runtime, 100, &drive, NULL) == QAPLET_OK && drive.reason == QAPLET_DRIVE_QUIESCENT);
  CHECK(qaplet_get_m2_snapshot(runtime, &snapshot, NULL) == QAPLET_OK);
  CHECK(!snapshot.native_handles && !snapshot.native_uncertain_handles && !snapshot.reactor_registrations && !snapshot.keepalive_tokens && !snapshot.engine_current_bytes);
  CHECK(qaplet_destroy(runtime, NULL) == QAPLET_OK);
  return 0;
}
static int concurrent_contract(void), token_contract(void), deadline_contract(void), budget_contract(void), event_contract(void);
int sdk_wait(const char *mode) {
  if (!strcmp(mode, "--m2-wait-concurrent")) return concurrent_contract();
  if (!strcmp(mode, "--m2-wait-tokens")) return token_contract();
  if (!strcmp(mode, "--m2-wait-deadline")) return deadline_contract();
  if (!strcmp(mode, "--m2-wait-budget")) return budget_contract();
  if (!strcmp(mode, "--m2-wait-events")) return event_contract();
  qaplet_config base = QAPLET_CONFIG_INIT;
  qaplet_m2_config config = QAPLET_M2_CONFIG_INIT;
  qaplet_runtime *runtime = NULL;
  qaplet_keepalive token = QAPLET_KEEPALIVE_INIT;
  qaplet_drive_result drive = QAPLET_DRIVE_RESULT_INIT;
  CHECK(qaplet_create_m2(&base, &config, &runtime, NULL) == QAPLET_OK);
  CHECK(qaplet_keepalive_create(runtime, &token, NULL) == QAPLET_OK);
  CHECK(qaplet_run_once(runtime, 0, &drive, NULL) == QAPLET_OK && drive.reason == QAPLET_DRIVE_WAIT_TIMEOUT && !drive.progressed);
  CHECK(qaplet_run_once(runtime, 25, &drive, NULL) == QAPLET_OK && drive.reason == QAPLET_DRIVE_WAIT_TIMEOUT && !drive.progressed);
  CHECK(qaplet_keepalive_release(runtime, &token, NULL) == QAPLET_OK);
  CHECK(qaplet_run(runtime, &drive, NULL) == QAPLET_OK && drive.reason == QAPLET_DRIVE_IDLE);
  CHECK(!finish(runtime));
  puts("M2 wait: finite timeout; explicit keepalive; idle cleanup: pass");
  return 0;
}

#ifdef SDK_STANDALONE
int main(int argc, char **argv) { return argc == 2 ? sdk_wait(argv[1]) : 2; }
#endif

static double milliseconds(void) {
  struct timespec ts;
  if (clock_gettime(CLOCK_MONOTONIC, &ts)) return -1;
  return ts.tv_sec * 1000.0 + ts.tv_nsec / 1000000.0;
}
static int js(qaplet_runtime *runtime, const char *source) {
  qaplet_progress progress = QAPLET_PROGRESS_INIT;
  CHECK(qaplet_eval(runtime, source, strlen(source), &progress, NULL) == QAPLET_OK);
  for (unsigned i = 0; progress.phase == QAPLET_PHASE_PENDING && i < 100; ++i)
    CHECK(qaplet_pump(runtime, &progress, NULL) == QAPLET_OK);
  CHECK(progress.phase == QAPLET_PHASE_DONE);
  return 0;
}
typedef struct {
  qaplet_runtime *runtime;
  qaplet_keepalive token;
  pthread_mutex_t mutex;
  pthread_cond_t condition;
  int mode, gate, passed;
} producer;
static void *produce(void *opaque) {
  producer *p = opaque;
  pthread_mutex_lock(&p->mutex);
  while (!p->gate) pthread_cond_wait(&p->condition, &p->mutex);
  pthread_mutex_unlock(&p->mutex);
  /* Observe the public owner-drive state, then use an explicit timed release
   * gate. This tests a real blocked interval, never guessed scheduling sleeps. */
  qaplet_m2_snapshot snapshot = QAPLET_M2_SNAPSHOT_INIT;
  double until = milliseconds() + 5000;
  do {
    if (qaplet_get_m2_snapshot(p->runtime, &snapshot, NULL)) return NULL;
    if (snapshot.driving) break;
    sched_yield();
  } while (milliseconds() < until);
  if (!snapshot.driving) return NULL;
  if (p->mode == 3) {
    p->passed = 1;
    for (int i = 0; i < 12; ++i) {
      pthread_mutex_lock(&p->mutex);
      struct timespec gate;
      clock_gettime(CLOCK_REALTIME, &gate); gate.tv_nsec += 20000000;
      if (gate.tv_nsec >= 1000000000) { ++gate.tv_sec; gate.tv_nsec -= 1000000000; }
      int rc = 0;
      while (p->gate == 1 && !rc) rc = pthread_cond_timedwait(&p->condition, &p->mutex, &gate);
      int end = p->gate != 1;
      pthread_mutex_unlock(&p->mutex);
      if (end) break;
      if (rc != ETIMEDOUT || qaplet_wake(p->runtime, NULL) != QAPLET_OK) { p->passed = 0; break; }
    }
    p->passed &= qaplet_keepalive_release(p->runtime, &p->token, NULL) == QAPLET_OK;
    return NULL;
  }
  pthread_mutex_lock(&p->mutex);
  struct timespec deadline;
  clock_gettime(CLOCK_REALTIME, &deadline);
  deadline.tv_nsec += 60000000;
  if (deadline.tv_nsec >= 1000000000) { ++deadline.tv_sec; deadline.tv_nsec -= 1000000000; }
  int error = 0;
  while (!error) error = pthread_cond_timedwait(&p->condition, &p->mutex, &deadline);
  pthread_mutex_unlock(&p->mutex);
  if (error != ETIMEDOUT) return NULL;
  if (p->mode == 1) {
    unsigned char payload[] = {0, 255, 17};
    p->passed = qaplet_post_event(p->runtime, 7, 99, payload, sizeof(payload), NULL) == QAPLET_OK;
    memset(payload, 42, sizeof(payload));
  } else if (p->mode == 2) {
    p->passed = qaplet_request_stop(p->runtime, NULL) == QAPLET_OK;
    return NULL;
  } else p->passed = 1;
  p->passed &= qaplet_keepalive_release(p->runtime, &p->token, NULL) == QAPLET_OK;
  return NULL;
}
static int concurrent_contract(void) {
  for (int mode = 0; mode < 3; ++mode) {
    qaplet_config base = QAPLET_CONFIG_INIT;
    qaplet_m2_config config = QAPLET_M2_CONFIG_INIT;
    qaplet_runtime *runtime = NULL;
    qaplet_progress progress = QAPLET_PROGRESS_INIT;
    qaplet_drive_result drive = QAPLET_DRIVE_RESULT_INIT;
    CHECK(qaplet_create_m2(&base, &config, &runtime, NULL) == QAPLET_OK);
    CHECK(!js(runtime, "globalThis.received=[];import('qaplet:events').then(m=>m.onEvent(e=>received.push(e)))"));
    producer p = {.runtime=runtime, .mutex=PTHREAD_MUTEX_INITIALIZER,
                  .condition=PTHREAD_COND_INITIALIZER, .mode=mode};
    CHECK(qaplet_keepalive_create(runtime, &p.token, NULL) == QAPLET_OK);
    pthread_t thread;
    CHECK(!pthread_create(&thread, NULL, produce, &p));
    pthread_mutex_lock(&p.mutex); p.gate = 1; pthread_cond_broadcast(&p.condition); pthread_mutex_unlock(&p.mutex);
    double start = milliseconds();
    qaplet_code code = qaplet_run(runtime, &drive, NULL);
    double elapsed = milliseconds() - start;
    CHECK(!pthread_join(thread, NULL) && p.passed && elapsed >= 40 && elapsed < 5000);
    CHECK(!pthread_cond_destroy(&p.condition) && !pthread_mutex_destroy(&p.mutex));
    if (mode == 2) {
      qaplet_m2_snapshot snapshot = QAPLET_M2_SNAPSHOT_INIT;
      CHECK(code == QAPLET_EXECUTION_STOPPED && drive.reason == QAPLET_DRIVE_STOPPED);
      CHECK(qaplet_get_m2_snapshot(runtime, &snapshot, NULL) == QAPLET_OK && snapshot.lifecycle == QAPLET_STOPPED);
      CHECK(!snapshot.native_handles && !snapshot.keepalive_tokens);
    } else {
      CHECK(code == QAPLET_OK && drive.reason == QAPLET_DRIVE_IDLE && drive.progressed == (mode == 1));
      CHECK(!js(runtime, mode == 1 ? "received.length===1 && received[0].token===99n && received[0].type===7 && received[0].payload.join(',')==='0,255,17'" : "received.length===0"));
      qaplet_result result = QAPLET_RESULT_INIT;
      CHECK(qaplet_get_result(runtime, &result, NULL) == QAPLET_OK && result.boolean);
      CHECK(qaplet_pump(runtime, &progress, NULL) == QAPLET_OK && !progress.progressed);
    }
    CHECK(!finish(runtime));
  }
  puts("M2 concurrent wait: copied events; last token; owner stop; real blocked interval; joined cleanup: pass");
  return 0;
}
static int token_contract(void) {
  qaplet_config base = QAPLET_CONFIG_INIT;
  qaplet_m2_config config = QAPLET_M2_CONFIG_INIT;
  config.limits.keepalive_tokens = 1;
  qaplet_runtime *first = NULL, *second = NULL;
  qaplet_keepalive old = QAPLET_KEEPALIVE_INIT, current = QAPLET_KEEPALIVE_INIT, other = QAPLET_KEEPALIVE_INIT;
  qaplet_m2_snapshot snapshot = QAPLET_M2_SNAPSHOT_INIT;
  qaplet_drive_result drive = QAPLET_DRIVE_RESULT_INIT;
  CHECK(qaplet_create_m2(&base, &config, &first, NULL) == QAPLET_OK);
  CHECK(qaplet_create_m2(&base, &config, &second, NULL) == QAPLET_OK);
  CHECK(qaplet_keepalive_create(first, &old, NULL) == QAPLET_OK);
  CHECK(qaplet_keepalive_create(first, &current, NULL) == QAPLET_RESOURCE_LIMIT && !current.runtime_id);
  CHECK(qaplet_keepalive_create(second, &other, NULL) == QAPLET_OK);
  CHECK(qaplet_keepalive_release(second, &old, NULL) == QAPLET_INVALID_HANDLE);
  CHECK(qaplet_keepalive_release(first, &old, NULL) == QAPLET_OK);
  CHECK(qaplet_keepalive_create(first, &current, NULL) == QAPLET_OK);
  CHECK(qaplet_keepalive_release(first, &old, NULL) == QAPLET_INVALID_HANDLE);
  CHECK(qaplet_get_m2_snapshot(first, &snapshot, NULL) == QAPLET_OK && snapshot.keepalive_tokens == 1 && snapshot.keepalive_tokens_peak == 1);
  CHECK(snapshot.native_handles == 2 && snapshot.native_handles_peak == 3 && snapshot.reactor_registrations == 1);
  CHECK(qaplet_keepalive_release(first, &current, NULL) == QAPLET_OK);
  CHECK(qaplet_keepalive_release(first, &current, NULL) == QAPLET_INVALID_HANDLE);
  CHECK(!finish(first));
  CHECK(qaplet_run_once(second, 1, &drive, NULL) == QAPLET_OK && drive.reason == QAPLET_DRIVE_WAIT_TIMEOUT);
  CHECK(qaplet_close(second, NULL) == QAPLET_OK);
  CHECK(qaplet_keepalive_release(second, &other, NULL) == QAPLET_INVALID_HANDLE);
  CHECK(!finish(second));
  config.limits.native_handles = 2;
  CHECK(qaplet_create_m2(&base, &config, &first, NULL) == QAPLET_RESOURCE_LIMIT && !first);
  puts("M2 keepalive: finite capacity; Runtime/generation identity; stale/duplicate release; native handles; close invalidation: pass");
  return 0;
}
static int deadline_contract(void) {
  qaplet_config base = QAPLET_CONFIG_INIT;
  qaplet_m2_config config = QAPLET_M2_CONFIG_INIT;
  qaplet_runtime *runtime = NULL;
  qaplet_keepalive token = QAPLET_KEEPALIVE_INIT;
  qaplet_progress progress = QAPLET_PROGRESS_INIT;
  qaplet_drive_result drive = QAPLET_DRIVE_RESULT_INIT;
  qaplet_m2_snapshot snapshot = QAPLET_M2_SNAPSHOT_INIT;
  CHECK(qaplet_create_m2(&base, &config, &runtime, NULL) == QAPLET_OK);
  CHECK(qaplet_keepalive_create(runtime, &token, NULL) == QAPLET_OK);
  qaplet_execution_options options = QAPLET_EXECUTION_OPTIONS_INIT;
  options.timeout_ms = 90;
  const char source[] = "new Promise(()=>{})";
  CHECK(qaplet_eval_with_options(runtime, source, sizeof(source)-1, &options, &progress, NULL) == QAPLET_OK);
  CHECK(qaplet_run_once(runtime, 25, &drive, NULL) == QAPLET_OK && drive.phase == QAPLET_PHASE_PENDING && drive.reason == QAPLET_DRIVE_WAIT_TIMEOUT);
  CHECK(qaplet_run(runtime, &drive, NULL) == QAPLET_DEADLINE_EXCEEDED && drive.reason == QAPLET_DRIVE_ERROR);
  CHECK(qaplet_get_m2_snapshot(runtime, &snapshot, NULL) == QAPLET_OK && snapshot.lifecycle == QAPLET_FAILED && snapshot.termination_reason == QAPLET_DEADLINE_EXCEEDED);
  CHECK(!snapshot.native_handles && !snapshot.keepalive_tokens);
  CHECK(qaplet_request_stop(runtime, NULL) == QAPLET_OK);
  CHECK(!finish(runtime));
  puts("M2 wait deadline: pending entry keeps absolute execution ledger across waits; terminal cleanup starts before close: pass");
  return 0;
}

static int budget_contract(void) {
  qaplet_config base = QAPLET_CONFIG_INIT;
  qaplet_m2_config config = QAPLET_M2_CONFIG_INIT;
  qaplet_runtime *runtime = NULL;
  qaplet_drive_result drive = QAPLET_DRIVE_RESULT_INIT;
  qaplet_progress progress = QAPLET_PROGRESS_INIT;
  CHECK(qaplet_create_m2(&base, &config, &runtime, NULL) == QAPLET_OK);
  producer p = {.runtime=runtime, .mutex=PTHREAD_MUTEX_INITIALIZER,
                .condition=PTHREAD_COND_INITIALIZER, .mode=3, .gate=1};
  CHECK(qaplet_keepalive_create(runtime, &p.token, NULL) == QAPLET_OK);
  /* A Host keepalive must never make the M1 pump wait. */
  CHECK(qaplet_pump(runtime, &progress, NULL) == QAPLET_OK && !progress.progressed);
  pthread_t thread;
  CHECK(!pthread_create(&thread, NULL, produce, &p));
  double start = milliseconds();
  qaplet_code code = qaplet_run_once(runtime, 80, &drive, NULL);
  double elapsed = milliseconds() - start;
  pthread_mutex_lock(&p.mutex); p.gate=2; pthread_cond_broadcast(&p.condition); pthread_mutex_unlock(&p.mutex);
  CHECK(!pthread_join(thread, NULL) && p.passed);
  CHECK(code == QAPLET_OK && drive.reason == QAPLET_DRIVE_WAIT_TIMEOUT && !drive.progressed && elapsed >= 60);
  CHECK(!pthread_cond_destroy(&p.condition) && !pthread_mutex_destroy(&p.mutex));
  CHECK(qaplet_run(runtime, &drive, NULL) == QAPLET_OK && drive.reason == QAPLET_DRIVE_IDLE);
  CHECK(!finish(runtime));
  puts("M2 wait budget: redundant cross-thread wakes preserve cumulative timeout; hints are not work; pump never waits: pass");
  return 0;
}

static int event_contract(void) {
  qaplet_config base = QAPLET_CONFIG_INIT;
  base.limits.event_queue_slots = 2; base.limits.event_payload_bytes = 3;
  base.limits.pump_jobs = 1; base.limits.pump_events = 1;
  qaplet_m2_config config = QAPLET_M2_CONFIG_INIT;
  qaplet_runtime *runtime = NULL;
  qaplet_keepalive token = QAPLET_KEEPALIVE_INIT;
  qaplet_drive_result drive = QAPLET_DRIVE_RESULT_INIT;
  qaplet_progress progress = QAPLET_PROGRESS_INIT;
  qaplet_result result = QAPLET_RESULT_INIT;
  CHECK(qaplet_create_m2(&base, &config, &runtime, NULL) == QAPLET_OK);
  CHECK(qaplet_post_event(runtime, 0, 0, NULL, 0, NULL) == QAPLET_NO_HANDLER);
  unsigned char bytes[] = {0, 255, 17, 9};
  CHECK(qaplet_post_event(runtime, 0, 0, bytes, sizeof(bytes), NULL) == QAPLET_PAYLOAD_TOO_LARGE);
  CHECK(!js(runtime, "globalThis.received=[];import('qaplet:events').then(m=>m.onEvent(e=>{received.push(e);if(globalThis.resolveEntry)resolveEntry(e.type)}))"));
  CHECK(qaplet_post_event(runtime, 1, 0, bytes, 3, NULL) == QAPLET_OK);
  CHECK(qaplet_post_event(runtime, 2, 0, bytes, 3, NULL) == QAPLET_OK);
  memset(bytes, 42, sizeof(bytes));
  CHECK(qaplet_post_event(runtime, 3, 0, bytes, 3, NULL) == QAPLET_QUEUE_FULL);
  CHECK(qaplet_run_once(runtime, 10000, &drive, NULL) == QAPLET_OK && drive.progressed && drive.reason == QAPLET_DRIVE_PROGRESS);
  CHECK(qaplet_run(runtime, &drive, NULL) == QAPLET_OK && drive.reason == QAPLET_DRIVE_IDLE);
  CHECK(!js(runtime, "received.length===2 && received[0].type===1 && received[1].type===2 && received.every(e=>e.payload.join(',')==='0,255,17')"));
  CHECK(qaplet_get_result(runtime, &result, NULL) == QAPLET_OK && result.boolean);
  CHECK(qaplet_keepalive_create(runtime, &token, NULL) == QAPLET_OK);
  const char pending[] = "new Promise(r=>globalThis.resolveEntry=r).then(x=>x+1)";
  CHECK(qaplet_eval(runtime, pending, sizeof(pending)-1, &progress, NULL) == QAPLET_OK && progress.phase == QAPLET_PHASE_PENDING);
  CHECK(qaplet_post_event(runtime, 41, 0, NULL, 0, NULL) == QAPLET_OK);
  CHECK(qaplet_keepalive_release(runtime, &token, NULL) == QAPLET_OK);
  CHECK(qaplet_run(runtime, &drive, NULL) == QAPLET_OK && drive.phase == QAPLET_PHASE_DONE && drive.progressed);
  CHECK(qaplet_get_result(runtime, &result, NULL) == QAPLET_OK && result.number == 42);
  CHECK(qaplet_run_once(runtime, 10000, &drive, NULL) == QAPLET_OK && drive.reason == QAPLET_DRIVE_IDLE && !drive.progressed);
  CHECK(qaplet_get_result(runtime, &result, NULL) == QAPLET_OK && result.number == 42);
  /* Saturated business slots cannot prevent the independent stop commit. */
  CHECK(qaplet_keepalive_create(runtime, &token, NULL) == QAPLET_OK);
  CHECK(qaplet_post_event(runtime, 3, 0, NULL, 0, NULL) == QAPLET_OK);
  CHECK(qaplet_post_event(runtime, 4, 0, NULL, 0, NULL) == QAPLET_OK);
  CHECK(qaplet_post_event(runtime, 5, 0, NULL, 0, NULL) == QAPLET_QUEUE_FULL);
  CHECK(qaplet_request_stop(runtime, NULL) == QAPLET_OK);
  CHECK(qaplet_run(runtime, &drive, NULL) == QAPLET_EXECUTION_STOPPED && !drive.progressed);
  CHECK(!finish(runtime));
  puts("M2 waiting events: ready-first bounded drive; copied FIFO/rejections; last-token accepted work; complete jobs; idle result; full-queue stop: pass");
  return 0;
}
