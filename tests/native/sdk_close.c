/* SPDX-License-Identifier: MIT */
/* Close/destroy workloads consume only the distributed public SDK. */
#include "qaplet.h"
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(condition) do { if (!(condition)) { \
  fprintf(stderr, "public close contract failed at line %d: %s\n", __LINE__, #condition); return 1; \
} } while (0)

typedef struct { unsigned calls, forbidden, reentry; FILE *resource; } binding;

static qaplet_code observe(qaplet_runtime *runtime, uint64_t call, size_t argc,
                            const qaplet_value *argv, void *opaque) {
  (void)call; (void)argc; (void)argv;
  binding *host = opaque;
  ++host->calls;
  qaplet_snapshot snapshot = QAPLET_SNAPSHOT_INIT;
  if (qaplet_close(runtime, NULL) != QAPLET_BUSY || qaplet_destroy(runtime, NULL) != QAPLET_BUSY ||
      qaplet_get_snapshot(runtime, &snapshot, NULL) != QAPLET_OK || snapshot.phase != QAPLET_PHASE_RUNNING ||
      fputc('x', host->resource) == EOF) host->reentry = 1;
  return QAPLET_OK;
}

static qaplet_code forbidden(qaplet_runtime *runtime, uint64_t call, size_t argc,
                              const qaplet_value *argv, void *opaque) {
  (void)runtime; (void)call; (void)argc; (void)argv;
  ++((binding *)opaque)->forbidden;
  return QAPLET_OK;
}

static int register_binding(qaplet_runtime *runtime, binding *host) {
  host->resource = tmpfile();
  CHECK(host->resource);
  qaplet_host_export items[] = {QAPLET_HOST_EXPORT_INIT, QAPLET_HOST_EXPORT_INIT};
  items[0].name = "observe"; items[0].name_length = 7; items[0].kind = QAPLET_HOST_FUNCTION;
  items[0].function = observe; items[0].userdata = host;
  items[1].name = "forbidden"; items[1].name_length = 9; items[1].kind = QAPLET_HOST_FUNCTION;
  items[1].function = forbidden; items[1].userdata = host;
  qaplet_host_module module = QAPLET_HOST_MODULE_INIT;
  module.name = "host:close"; module.name_length = 10; module.exports = items; module.export_count = 2;
  CHECK(qaplet_register_host_module(runtime, &module, NULL) == QAPLET_OK);
  return 0;
}

static int settle(qaplet_runtime *runtime, const char *source) {
  qaplet_progress progress = QAPLET_PROGRESS_INIT;
  CHECK(qaplet_eval(runtime, source, strlen(source), &progress, NULL) == QAPLET_OK);
  for (unsigned i = 0; progress.phase == QAPLET_PHASE_PENDING && i < 1000; ++i)
    CHECK(qaplet_pump(runtime, &progress, NULL) == QAPLET_OK);
  CHECK(progress.phase == QAPLET_PHASE_DONE);
  return 0;
}

static int prepare(qaplet_runtime *runtime, binding *host, int with_pressure) {
  CHECK(!register_binding(runtime, host));
  if (with_pressure) CHECK(!settle(runtime, "import('host:pressure').then(m=>{globalThis.pressure=m.pressure})"));
  CHECK(!settle(runtime, "Promise.all([import('host:close'),import('qaplet:events')]).then(([h,e])=>{"
      "globalThis.h=h;h.observe();e.onEvent(()=>h.forbidden());"
      "globalThis.registry=new FinalizationRegistry(()=>h.forbidden());"
      "globalThis.target={};registry.register(target,h.observe);})"));
  CHECK(host->calls == 1 && !host->reentry && !host->forbidden);
  CHECK(qaplet_post_event(runtime, 1, 0, NULL, 0, NULL) == QAPLET_OK);
  CHECK(qaplet_post_event(runtime, 2, 0, NULL, 0, NULL) == QAPLET_OK);
  return 0;
}

static int verify_closed(qaplet_runtime *runtime, binding *host, qaplet_code reason) {
  qaplet_snapshot snapshot = QAPLET_SNAPSHOT_INIT;
  qaplet_progress progress = QAPLET_PROGRESS_INIT;
  qaplet_result result = QAPLET_RESULT_INIT;
  CHECK(qaplet_get_snapshot(runtime, &snapshot, NULL) == QAPLET_OK && snapshot.lifecycle == QAPLET_CLOSED &&
        snapshot.phase == QAPLET_PHASE_IDLE && snapshot.termination_reason == reason &&
        !snapshot.engine_current_bytes && !snapshot.queue_depth && !snapshot.module_count && !snapshot.source_cache_bytes &&
        snapshot.events_accepted == 2 && snapshot.events_dropped == 2 && !snapshot.events_consumed && snapshot.native_current_bytes);
  uint64_t native = snapshot.native_current_bytes, dropped = snapshot.events_dropped;
  CHECK(qaplet_get_result(runtime, &result, NULL) == QAPLET_BAD_STATE);
  CHECK(qaplet_eval(runtime, "h.forbidden()", 13, &progress, NULL) == QAPLET_BAD_STATE);
  CHECK(qaplet_pump(runtime, &progress, NULL) == QAPLET_BAD_STATE);
  CHECK(qaplet_post_event(runtime, 3, 0, NULL, 0, NULL) == QAPLET_BAD_STATE);
  CHECK(qaplet_post_event(runtime, 3, 0, NULL, 1, NULL) == QAPLET_INVALID_ARGUMENT);
  CHECK(qaplet_request_stop(runtime, NULL) == QAPLET_OK && qaplet_close(runtime, NULL) == QAPLET_OK);
  CHECK(qaplet_get_snapshot(runtime, &snapshot, NULL) == QAPLET_OK && snapshot.lifecycle == QAPLET_CLOSED &&
        snapshot.termination_reason == reason && snapshot.native_current_bytes == native && snapshot.events_dropped == dropped);
  CHECK(host->calls == 1 && !host->reentry && !host->forbidden);
  /* Host owns userdata/resources. They stay usable until close returns, then
   * Host closes them before final handle release. SDK never owns this FILE. */
  CHECK(!fflush(host->resource) && ftell(host->resource) == 1 && !fclose(host->resource));
  host->resource = NULL;
  return 0;
}

static int states(void) {
  for (unsigned mode = 0; mode < 6; ++mode) {
    qaplet_config config = QAPLET_CONFIG_INIT;
    config.limits.event_queue_slots = 2;
    config.limits.engine_heap_bytes = 1048576;
    qaplet_runtime *runtime = NULL;
    qaplet_snapshot snapshot = QAPLET_SNAPSHOT_INIT;
    qaplet_progress progress = QAPLET_PROGRESS_INIT;
    binding host = {0};
    CHECK(qaplet_create(&config, &runtime, NULL) == QAPLET_OK);
    CHECK(!prepare(runtime, &host, 0));
    const char *source = mode == 0 ? "42" : mode == 1 ? "throw new Error('ordinary')" :
        mode == 5 ? "try{let a=[];for(;;)a.push('x'.repeat(128))}catch(e){};42" :
        "Promise.resolve().then(()=>h.forbidden()).finally(()=>h.forbidden());"
        "new Promise(()=>{}).finally(()=>h.forbidden())";
    qaplet_code expected = mode == 1 ? QAPLET_JS_EXCEPTION : mode == 5 ? QAPLET_MEMORY_LIMIT : QAPLET_OK;
    CHECK(qaplet_eval(runtime, source, strlen(source), &progress, NULL) == expected);
    qaplet_code reason = mode >= 3 && mode <= 4 ? QAPLET_EXECUTION_STOPPED : mode == 5 ? QAPLET_MEMORY_LIMIT : QAPLET_OK;
    if (mode == 3 || mode == 4) CHECK(qaplet_request_stop(runtime, NULL) == QAPLET_OK);
    if (mode == 4) CHECK(qaplet_pump(runtime, &progress, NULL) == QAPLET_EXECUTION_STOPPED && !progress.progressed);
    CHECK(qaplet_get_snapshot(runtime, &snapshot, NULL) == QAPLET_OK);
    CHECK(snapshot.lifecycle == (mode == 3 ? QAPLET_STOP_REQUESTED : mode == 4 ? QAPLET_STOPPED :
          mode == 5 ? QAPLET_FAILED : QAPLET_OPEN));
    if (mode >= 2 && mode <= 3) CHECK(snapshot.phase == QAPLET_PHASE_PENDING);
    CHECK(qaplet_destroy(runtime, NULL) == QAPLET_BAD_STATE);
    qaplet_diagnostic invalid = QAPLET_DIAGNOSTIC_INIT; invalid.struct_size = 8;
    CHECK(qaplet_close(runtime, &invalid) == QAPLET_ABI_MISMATCH);
    CHECK(qaplet_get_snapshot(runtime, &snapshot, NULL) == QAPLET_OK && snapshot.queue_depth == 2);
    char text[32]; qaplet_diagnostic diagnostic = QAPLET_DIAGNOSTIC_INIT;
    diagnostic.message = text; diagnostic.capacity = sizeof(text);
    CHECK(qaplet_close(runtime, &diagnostic) == QAPLET_OK && diagnostic.code == QAPLET_OK && !strcmp(text, "OK"));
    CHECK(!verify_closed(runtime, &host, reason));
    CHECK(qaplet_destroy(runtime, NULL) == QAPLET_OK);
  }
  puts("close states: normal/error/PENDING/STOP_REQUESTED/STOPPED/FAILED; reentry; raw cleanup; retained handle: pass");
  return 0;
}

typedef struct {
  qaplet_runtime *runtime;
  pthread_mutex_t mutex;
  pthread_cond_t condition;
  int started, withdraw, failed, stop;
  unsigned during, after;
} producer;

static void *produce(void *opaque) {
  producer *worker = opaque;
  qaplet_snapshot snapshot = QAPLET_SNAPSHOT_INIT;
  char text[32]; qaplet_diagnostic diagnostic = QAPLET_DIAGNOSTIC_INIT;
  diagnostic.message = text; diagnostic.capacity = sizeof(text);
  qaplet_code code = qaplet_post_event(worker->runtime, 3, 0, NULL, 0, &diagnostic);
  if (code != QAPLET_QUEUE_FULL || diagnostic.code != code || strcmp(text, "QUEUE_FULL")) worker->failed = 1;
  pthread_mutex_lock(&worker->mutex);
  worker->started = 1;
  pthread_cond_broadcast(&worker->condition);
  pthread_mutex_unlock(&worker->mutex);
  for (;;) {
    if (qaplet_get_snapshot(worker->runtime, &snapshot, NULL) != QAPLET_OK) worker->failed = 1;
    int closed = snapshot.lifecycle == QAPLET_CLOSED;
    if (!closed && snapshot.events_dropped) ++worker->during;
    if (closed) {
      if (snapshot.engine_current_bytes || snapshot.queue_depth || snapshot.module_count || snapshot.source_cache_bytes)
        worker->failed = 1;
      ++worker->after;
    }
    if (worker->stop && qaplet_request_stop(worker->runtime, NULL) != QAPLET_OK) worker->failed = 1;
    code = qaplet_post_event(worker->runtime, 3, 0, NULL, 0, &diagnostic);
    if ((code != QAPLET_QUEUE_FULL && code != QAPLET_BAD_STATE) || diagnostic.code != code ||
        (code == QAPLET_BAD_STATE && strcmp(text, "BAD_STATE")) || (closed && code != QAPLET_BAD_STATE))
      worker->failed = 1;
    pthread_mutex_lock(&worker->mutex);
    int done = worker->withdraw && worker->after >= 100;
    pthread_mutex_unlock(&worker->mutex);
    if (done) break;
  }
  return NULL;
}

static int concurrent(void) {
  unsigned observed = 0;
  for (unsigned mode = 0; mode < 4; ++mode) {
    qaplet_config config = QAPLET_CONFIG_INIT;
    config.limits.event_queue_slots = 2;
    qaplet_runtime *runtime = NULL;
    binding host = {0};
    CHECK(qaplet_create(&config, &runtime, NULL) == QAPLET_OK && !prepare(runtime, &host, 0));
    /* Real retained JS objects lengthen native teardown without sleeps, hidden
     * engine hooks or assuming an internal allocation/finalizer count. */
    CHECK(!settle(runtime, "globalThis.heap=Array.from({length:20000},(_,i)=>({i,next:{i}}));42"));
    CHECK(qaplet_destroy(runtime, NULL) == QAPLET_BAD_STATE);
    producer worker = {.runtime = runtime, .stop = mode & 1};
    CHECK(!pthread_mutex_init(&worker.mutex, NULL) && !pthread_cond_init(&worker.condition, NULL));
    pthread_t thread;
    CHECK(!pthread_create(&thread, NULL, produce, &worker));
    pthread_mutex_lock(&worker.mutex);
    while (!worker.started) pthread_cond_wait(&worker.condition, &worker.mutex);
    pthread_mutex_unlock(&worker.mutex);
    CHECK(qaplet_close(runtime, NULL) == QAPLET_OK);
    pthread_mutex_lock(&worker.mutex);
    worker.withdraw = 1;
    pthread_mutex_unlock(&worker.mutex);
    /* A CLOSED handle and its synchronization remain alive while this join
     * drains calls; no pointer is used after destroy. */
    CHECK(!pthread_join(thread, NULL) && !worker.failed && worker.after >= 100);
    observed += worker.during;
    CHECK(!pthread_cond_destroy(&worker.condition) && !pthread_mutex_destroy(&worker.mutex));
    qaplet_snapshot snapshot = QAPLET_SNAPSHOT_INIT;
    CHECK(qaplet_get_snapshot(runtime, &snapshot, NULL) == QAPLET_OK);
    /* Stop may linearize before CLOSED or after it; only the former records a
     * first cause. Both public acceptance orders are valid in this race. */
    CHECK(snapshot.termination_reason == QAPLET_OK || (worker.stop && snapshot.termination_reason == QAPLET_EXECUTION_STOPPED));
    CHECK(!verify_closed(runtime, &host, snapshot.termination_reason));
    CHECK(qaplet_destroy(runtime, NULL) == QAPLET_OK);
  }
  CHECK(observed);
  puts("close concurrent: producer overlap observed; post/snapshot/stop stable; CLOSED calls drained before destroy: pass");
  return 0;
}

typedef struct {
  qaplet_runtime *runtime;
  pthread_mutex_t mutex;
  pthread_cond_t condition;
  int entered, released, allocation_first, failed;
  unsigned char *bytes;
} pressure_gate;

static qaplet_code pressure(qaplet_runtime *runtime, uint64_t call, size_t argc,
                             const qaplet_value *argv, void *opaque) {
  (void)argc; (void)argv;
  pressure_gate *gate = opaque;
  if (!gate->allocation_first) {
    pthread_mutex_lock(&gate->mutex);
    gate->entered = 1;
    pthread_cond_broadcast(&gate->condition);
    while (!gate->released) pthread_cond_wait(&gate->condition, &gate->mutex);
    pthread_mutex_unlock(&gate->mutex);
  }
  qaplet_value value = QAPLET_VALUE_INIT;
  value.type = QAPLET_VALUE_BYTES; value.data = gate->bytes; value.length = 1048576;
  if (qaplet_host_set_result(runtime, call, &value, NULL) != QAPLET_MEMORY_LIMIT) gate->failed = 1;
  qaplet_snapshot snapshot = QAPLET_SNAPSHOT_INIT;
  if (qaplet_get_snapshot(runtime, &snapshot, NULL) != QAPLET_OK ||
      snapshot.termination_reason != (gate->allocation_first ? QAPLET_MEMORY_LIMIT : QAPLET_EXECUTION_STOPPED))
    gate->failed = 1;
  if (gate->allocation_first) {
    pthread_mutex_lock(&gate->mutex);
    gate->entered = 1;
    pthread_cond_broadcast(&gate->condition);
    while (!gate->released) pthread_cond_wait(&gate->condition, &gate->mutex);
    pthread_mutex_unlock(&gate->mutex);
  }
  return QAPLET_OK;
}

static void *compete(void *opaque) {
  pressure_gate *gate = opaque;
  pthread_mutex_lock(&gate->mutex);
  while (!gate->entered) pthread_cond_wait(&gate->condition, &gate->mutex);
  pthread_mutex_unlock(&gate->mutex);
  qaplet_snapshot snapshot = QAPLET_SNAPSHOT_INIT;
  if (qaplet_request_stop(gate->runtime, NULL) != QAPLET_OK ||
      qaplet_get_snapshot(gate->runtime, &snapshot, NULL) != QAPLET_OK ||
      snapshot.termination_reason != (gate->allocation_first ? QAPLET_MEMORY_LIMIT : QAPLET_EXECUTION_STOPPED) ||
      qaplet_post_event(gate->runtime, 3, 0, NULL, 0, NULL) != QAPLET_BAD_STATE) gate->failed = 1;
  pthread_mutex_lock(&gate->mutex);
  gate->released = 1;
  pthread_cond_broadcast(&gate->condition);
  pthread_mutex_unlock(&gate->mutex);
  return NULL;
}

static int competition(void) {
  unsigned char *bytes = calloc(1, 1048576);
  CHECK(bytes);
  for (unsigned mode = 0; mode < 2; ++mode) {
    qaplet_config config = QAPLET_CONFIG_INIT;
    config.limits.engine_heap_bytes = 524288; config.limits.event_queue_slots = 2;
    qaplet_runtime *runtime = NULL;
    qaplet_progress progress = QAPLET_PROGRESS_INIT;
    binding host = {0};
    CHECK(qaplet_create(&config, &runtime, NULL) == QAPLET_OK);
    pressure_gate gate = {.runtime = runtime, .allocation_first = mode, .bytes = bytes};
    CHECK(!pthread_mutex_init(&gate.mutex, NULL) && !pthread_cond_init(&gate.condition, NULL));
    qaplet_host_export item = QAPLET_HOST_EXPORT_INIT;
    item.name = "pressure"; item.name_length = 8; item.kind = QAPLET_HOST_FUNCTION;
    item.function = pressure; item.userdata = &gate;
    qaplet_host_module module = QAPLET_HOST_MODULE_INIT;
    module.name = "host:pressure"; module.name_length = 13; module.exports = &item; module.export_count = 1;
    CHECK(qaplet_register_host_module(runtime, &module, NULL) == QAPLET_OK && !prepare(runtime, &host, 1));
    pthread_t thread;
    CHECK(!pthread_create(&thread, NULL, compete, &gate));
    qaplet_code expected = mode ? QAPLET_MEMORY_LIMIT : QAPLET_EXECUTION_STOPPED;
    char text[32]; qaplet_diagnostic diagnostic = QAPLET_DIAGNOSTIC_INIT;
    diagnostic.message = text; diagnostic.capacity = sizeof(text);
    CHECK(qaplet_eval(runtime, "pressure()", 10, &progress, &diagnostic) == expected && diagnostic.code == expected);
    CHECK(!pthread_join(thread, NULL) && !gate.failed);
    CHECK(progress.phase == QAPLET_PHASE_DONE && progress.code == expected);
    CHECK(qaplet_destroy(runtime, NULL) == QAPLET_BAD_STATE);
    CHECK(qaplet_close(runtime, &diagnostic) == QAPLET_OK && diagnostic.code == QAPLET_OK && !strcmp(text, "OK"));
    CHECK(!verify_closed(runtime, &host, expected));
    CHECK(qaplet_destroy(runtime, NULL) == QAPLET_OK);
    CHECK(!pthread_cond_destroy(&gate.condition) && !pthread_mutex_destroy(&gate.mutex));
  }
  free(bytes);
  puts("close competition: actual allocation refusal and worker stop in both recorded orders; first cause survives close: pass");
  return 0;
}

int sdk_close(const char *option) {
  if (!strcmp(option, "--close-states")) return states();
  if (!strcmp(option, "--close-concurrent")) return concurrent();
  if (!strcmp(option, "--close-competition")) return competition();
  return 2;
}
