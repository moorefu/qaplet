/* SPDX-License-Identifier: MIT */
#include "runtime_internal.h"
#include <stdlib.h>
#include <time.h>

void qaplet_record_termination_locked(qaplet_runtime *runtime, qaplet_code code) {
  if (!runtime->snapshot.termination_reason) runtime->snapshot.termination_reason = code;
  runtime->snapshot.lifecycle = QAPLET_FAILED;
}

static int monotonic_ns(uint64_t *output) {
  struct timespec now;
  if (clock_gettime(CLOCK_MONOTONIC, &now) || now.tv_sec < 0 ||
      (uint64_t)now.tv_sec > (UINT64_MAX - (uint64_t)now.tv_nsec) / 1000000000)
    return 0;
  *output = (uint64_t)now.tv_sec * 1000000000 + (uint64_t)now.tv_nsec;
  return 1;
}

qaplet_code qaplet_check_execution_options(const qaplet_runtime *runtime,
                                          const qaplet_execution_options *options) {
  if (!options) return QAPLET_OK;
  qaplet_code code = qaplet_check_structure(options, sizeof(*options));
  if (code) return code;
  if (options->flags || options->reserved[0] || options->reserved[1] ||
      options->reserved[2] || options->reserved[3] || options->timeout_ms > UINT64_MAX / 1000000)
    return QAPLET_INVALID_ARGUMENT;
  if (runtime && (options->timeout_ms > runtime->snapshot.limits.execution_timeout_ms ||
                  options->max_dispatches > runtime->snapshot.limits.execution_dispatches))
    return QAPLET_INVALID_ARGUMENT;
  return QAPLET_OK;
}

qaplet_code qaplet_execution_begin(qaplet_runtime *runtime, const qaplet_execution_options *options) {
  qaplet_code code = qaplet_check_execution_options(runtime, options);
  if (code) return code;
  uint64_t timeout = options && options->timeout_ms ? options->timeout_ms : runtime->snapshot.limits.execution_timeout_ms;
  uint64_t now, duration = timeout * 1000000;
  if (!monotonic_ns(&now)) return QAPLET_BAD_STATE;
  if (duration > UINT64_MAX - now) return QAPLET_INVALID_ARGUMENT;
  pthread_mutex_lock(&runtime->mutex);
  runtime->execution_deadline_ns = now + duration;
  runtime->execution_dispatch_limit = options && options->max_dispatches ? options->max_dispatches :
      runtime->snapshot.limits.execution_dispatches;
  runtime->snapshot.execution_dispatches = 0;
  runtime->execution_active = 1;
  runtime->host_registration_locked = 1;
  runtime->snapshot.phase = QAPLET_PHASE_RUNNING;
  runtime->has_result = 0;
  runtime->execution_code = QAPLET_OK;
  pthread_mutex_unlock(&runtime->mutex);
  qaplet_values_reset(runtime);
  return QAPLET_OK;
}

qaplet_code qaplet_execution_check(qaplet_runtime *runtime) {
  pthread_mutex_lock(&runtime->mutex);
  uint64_t now;
  if (!runtime->snapshot.termination_reason && runtime->execution_active) {
    /* Clock failure cannot permit unbounded execution. It is an independent
     * control failure, never inferred from an engine exception. */
    if (!monotonic_ns(&now)) qaplet_record_termination_locked(runtime, QAPLET_BAD_STATE);
    else if (now >= runtime->execution_deadline_ns)
      qaplet_record_termination_locked(runtime, QAPLET_DEADLINE_EXCEEDED);
  }
  qaplet_code code = runtime->snapshot.termination_reason;
  pthread_mutex_unlock(&runtime->mutex);
  return code;
}

static int interrupt_handler(JSRuntime *engine, void *opaque) {
  (void)engine;
  return qaplet_execution_check(opaque) != QAPLET_OK;
}

/* Claim exactly one ready job/event before invoking it. Exact-quota completion
 * is allowed; only trying to schedule further work exhausts the budget. */
qaplet_code qaplet_execution_dispatch(qaplet_runtime *runtime) {
  qaplet_code code = qaplet_execution_check(runtime);
  if (code) return code;
  pthread_mutex_lock(&runtime->mutex);
  if (runtime->snapshot.execution_dispatches >= runtime->execution_dispatch_limit)
    qaplet_record_termination_locked(runtime, QAPLET_DISPATCH_LIMIT);
  else ++runtime->snapshot.execution_dispatches;
  code = runtime->snapshot.termination_reason;
  pthread_mutex_unlock(&runtime->mutex);
  return code;
}

struct qaplet_rejection {
  JSValue promise, reason;
  qaplet_rejection *next;
};

static void release_rejection(qaplet_runtime *runtime, qaplet_rejection *entry) {
  JS_FreeValue(runtime->context, entry->promise);
  JS_FreeValue(runtime->context, entry->reason);
  free(entry);
  pthread_mutex_lock(&runtime->mutex);
  runtime->snapshot.native_current_bytes -= sizeof(*entry);
  pthread_mutex_unlock(&runtime->mutex);
}

static void clear_rejections(qaplet_runtime *runtime) {
  while (runtime->rejections) {
    qaplet_rejection *entry = runtime->rejections;
    runtime->rejections = entry->next;
    release_rejection(runtime, entry);
  }
}

/* The engine borrows both values. This callback never calls application JS. */
static void rejection_tracker(JSContext *context, JSValueConst promise,
                              JSValueConst reason, JS_BOOL handled, void *opaque) {
  qaplet_runtime *runtime = opaque;
  qaplet_rejection **slot = &runtime->rejections;
  while (*slot && !JS_SameValue(context, (*slot)->promise, promise)) slot = &(*slot)->next;
  if (handled) {
    if (*slot) {
      qaplet_rejection *entry = *slot;
      *slot = entry->next;
      release_rejection(runtime, entry);
    }
    return;
  }
  if (*slot) return;
  pthread_mutex_lock(&runtime->mutex);
  qaplet_rejection *entry = NULL;
  if (sizeof(*entry) <= runtime->snapshot.limits.native_storage_bytes - runtime->snapshot.native_current_bytes)
    entry = malloc(sizeof(*entry));
  if (!entry) {
    qaplet_record_termination_locked(runtime, QAPLET_MEMORY_LIMIT);
    pthread_mutex_unlock(&runtime->mutex);
    return;
  }
  runtime->snapshot.native_current_bytes += sizeof(*entry);
  if (runtime->snapshot.native_current_bytes > runtime->snapshot.native_peak_bytes)
    runtime->snapshot.native_peak_bytes = runtime->snapshot.native_current_bytes;
  pthread_mutex_unlock(&runtime->mutex);
  entry->promise = JS_DupValue(context, promise);
  entry->reason = JS_DupValue(context, reason);
  entry->next = NULL;
  *slot = entry;
}

void qaplet_execution_init(qaplet_runtime *runtime) {
  runtime->entry_promise = JS_UNDEFINED;
  JS_SetHostPromiseRejectionTracker(runtime->engine, rejection_tracker, runtime);
  JS_SetInterruptHandler(runtime->engine, interrupt_handler, runtime);
}

void qaplet_execution_dispose(qaplet_runtime *runtime) {
  JS_SetHostPromiseRejectionTracker(runtime->engine, NULL, NULL);
  clear_rejections(runtime);
  JS_FreeValue(runtime->context, runtime->entry_promise);
  runtime->entry_promise = JS_UNDEFINED;
  runtime->execution_active = 0;
}

/* A checkpoint is complete only when all runnable jobs have settled. */
qaplet_code qaplet_execution_checkpoint(qaplet_runtime *runtime,
                                       qaplet_progress *progress, unsigned progressed) {
  int pending = 0;
  if (qaplet_execution_check(runtime)) goto finished;
  pending = JS_IsJobPending(runtime->engine);
  if (pending && runtime->snapshot.execution_dispatches >= runtime->execution_dispatch_limit) {
    pthread_mutex_lock(&runtime->mutex);
    qaplet_record_termination_locked(runtime, QAPLET_DISPATCH_LIMIT);
    pthread_mutex_unlock(&runtime->mutex);
    goto finished;
  }
  if (!pending && !JS_IsUndefined(runtime->entry_promise)) {
    JSPromiseStateEnum state = JS_PromiseState(runtime->context, runtime->entry_promise);
    if (state == JS_PROMISE_PENDING) pending = 1;
    else {
      JSValue value = JS_PromiseResult(runtime->context, runtime->entry_promise);
      if (!runtime->execution_code) {
        if (state == JS_PROMISE_REJECTED) {
          qaplet_capture_exception(runtime, value);
          runtime->execution_code = qaplet_module_exception_code(runtime, value);
        } else if (!runtime->module_execution) runtime->execution_code = qaplet_store_scalar(runtime, value);
      }
      JS_FreeValue(runtime->context, value);
    }
  }
  if (!JS_IsJobPending(runtime->engine)) {
    if (runtime->rejections && !runtime->execution_code) {
      qaplet_capture_exception(runtime, runtime->rejections->reason);
      runtime->execution_code = qaplet_module_exception_code(runtime, runtime->rejections->reason);
    }
    /* Includes the entry Promise's tracker record. It has already been
     * classified above, and must not resurface in a later execution. */
    clear_rejections(runtime);
    if (runtime->execution_code) pending = 0;
    if (!pending) {
      qaplet_modules_complete(runtime);
      JS_FreeValue(runtime->context, runtime->entry_promise);
      runtime->entry_promise = JS_UNDEFINED;
    }
  }
finished:
  qaplet_execution_check(runtime);
  pthread_mutex_lock(&runtime->mutex);
  if (runtime->snapshot.lifecycle == QAPLET_FAILED) {
    runtime->execution_code = runtime->snapshot.termination_reason;
    pending = 0;
  }
  runtime->snapshot.phase = pending ? QAPLET_PHASE_PENDING : QAPLET_PHASE_DONE;
  if (!pending) {
    runtime->execution_active = 0;
    runtime->result.code = runtime->execution_code;
    runtime->has_result = 1;
  }
  progress->phase = runtime->snapshot.phase;
  progress->progressed = progressed;
  progress->code = pending ? QAPLET_OK : runtime->execution_code;
  pthread_mutex_unlock(&runtime->mutex);
  return progress->code;
}

qaplet_code qaplet_pump(qaplet_runtime *runtime, qaplet_progress *progress,
                       qaplet_diagnostic *diagnostic) {
  qaplet_code code = qaplet_check_diagnostic(diagnostic);
  if (code) return code;
  code = qaplet_check_structure(progress, sizeof(*progress));
  if (!code) code = qaplet_owner(runtime);
  if (!code && runtime->snapshot.lifecycle != QAPLET_OPEN) code = QAPLET_BAD_STATE;
  if (code) return qaplet_report(diagnostic, code);
  runtime->host_registration_locked = 1;
  unsigned jobs = 0, events = 0, progressed = 0;
  for (;;) {
    if (runtime->execution_active) {
      pthread_mutex_lock(&runtime->mutex);
      runtime->snapshot.phase = QAPLET_PHASE_RUNNING;
      pthread_mutex_unlock(&runtime->mutex);
      while (jobs < 256 && JS_IsJobPending(runtime->engine) &&
             !qaplet_execution_dispatch(runtime)) {
        JSContext *context = NULL;
        int status = JS_ExecutePendingJob(runtime->engine, &context);
        if (!status) break;
        ++jobs;
        progressed = 1;
        if (status < 0) {
          JSValue exception = JS_GetException(context);
          if (!qaplet_execution_check(runtime) && !runtime->execution_code) {
            qaplet_capture_exception(runtime, exception);
            runtime->execution_code = qaplet_module_exception_code(runtime, exception);
          }
          JS_FreeValue(context, exception);
        }
      }
      code = qaplet_execution_checkpoint(runtime, progress, progressed);
      if (code || JS_IsJobPending(runtime->engine)) break;
    }
    if (events == 32 || !qaplet_events_pending(runtime)) {
      progress->phase = runtime->snapshot.phase;
      progress->progressed = progressed;
      progress->code = QAPLET_OK;
      break;
    }
    /* A pending entry owns its ledger. Otherwise one event plus its runnable
     * jobs owns a fresh independent ledger, which stays active across pumps. */
    if (!runtime->execution_active) {
      code = qaplet_execution_begin(runtime, NULL);
      if (code) break;
      runtime->module_execution = 0;
    } else {
      pthread_mutex_lock(&runtime->mutex);
      runtime->snapshot.phase = QAPLET_PHASE_RUNNING;
      pthread_mutex_unlock(&runtime->mutex);
    }
    unsigned claimed = 0;
    code = qaplet_events_deliver(runtime, &claimed);
    if (code && !runtime->execution_code) runtime->execution_code = code;
    events += claimed;
    if (claimed) progressed = 1;
  }
  return qaplet_report_result(runtime, diagnostic, code);
}
