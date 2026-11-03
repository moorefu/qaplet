/* SPDX-License-Identifier: MIT */
#include "runtime_internal.h"
#include <stdlib.h>
#include <time.h>

void qaplet_record_termination_locked(qaplet_runtime *runtime, qaplet_code code) {
  if (runtime->snapshot.lifecycle == QAPLET_CLOSED || runtime->snapshot.termination_reason) return;
  runtime->snapshot.termination_reason = code;
  if (code == QAPLET_CONTROL_FAILURE) runtime->primary_error = runtime->control_error;
  runtime->snapshot.lifecycle = code == QAPLET_EXECUTION_STOPPED ? QAPLET_STOP_REQUESTED : QAPLET_FAILED;
}

qaplet_code qaplet_request_stop(qaplet_runtime *runtime, qaplet_diagnostic *diagnostic) {
  qaplet_code code = qaplet_check_diagnostic(diagnostic);
  if (code) return code;
  if (!runtime) return qaplet_report(diagnostic, QAPLET_INVALID_ARGUMENT);
  pthread_mutex_lock(&runtime->mutex);
  if (runtime->snapshot.lifecycle == QAPLET_OPEN)
    qaplet_record_termination_locked(runtime, QAPLET_EXECUTION_STOPPED);
  qaplet_wake_locked(runtime);
  pthread_mutex_unlock(&runtime->mutex);
  return qaplet_report(diagnostic, QAPLET_OK);
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

qaplet_code qaplet_execution_prepare(qaplet_runtime *runtime, const qaplet_execution_options *options,
                                     qaplet_execution_budget *budget) {
  qaplet_code code = qaplet_check_execution_options(runtime, options);
  if (code) return code;
  uint64_t timeout = options && options->timeout_ms ? options->timeout_ms : runtime->snapshot.limits.execution_timeout_ms;
  uint64_t now, duration = timeout * 1000000;
  if (runtime->m2_enabled) {
    pthread_mutex_lock(&runtime->mutex);
    int valid = qaplet_clock_locked(runtime, &now);
    pthread_mutex_unlock(&runtime->mutex);
    if (!valid) return QAPLET_CONTROL_FAILURE;
  } else if (!monotonic_ns(&now)) return QAPLET_BAD_STATE;
  if (duration > UINT64_MAX - now) return QAPLET_INVALID_ARGUMENT;
  budget->deadline_ns = now + duration;
  budget->dispatch_limit = options && options->max_dispatches ? options->max_dispatches :
      runtime->snapshot.limits.execution_dispatches;
  return QAPLET_OK;
}

qaplet_code qaplet_execution_begin(qaplet_runtime *runtime, const qaplet_execution_budget *budget) {
  pthread_mutex_lock(&runtime->mutex);
  if (runtime->snapshot.lifecycle != QAPLET_OPEN) {
    pthread_mutex_unlock(&runtime->mutex);
    return QAPLET_BAD_STATE;
  }
  runtime->execution_deadline_ns = budget->deadline_ns;
  runtime->execution_dispatch_limit = budget->dispatch_limit;
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

static qaplet_code execution_check_locked(qaplet_runtime *runtime) {
  uint64_t now;
  if (!runtime->snapshot.termination_reason && runtime->execution_active) {
    /* Clock failure cannot permit unbounded execution. It is an independent
     * control failure, never inferred from an engine exception. */
    if (runtime->m2_enabled ? !qaplet_clock_locked(runtime, &now) : !monotonic_ns(&now))
      qaplet_record_termination_locked(runtime, runtime->m2_enabled ? QAPLET_CONTROL_FAILURE : QAPLET_BAD_STATE);
    else if (now >= runtime->execution_deadline_ns)
      qaplet_record_termination_locked(runtime, QAPLET_DEADLINE_EXCEEDED);
  }
  return runtime->snapshot.termination_reason;
}

qaplet_code qaplet_execution_check(qaplet_runtime *runtime) {
  pthread_mutex_lock(&runtime->mutex);
  qaplet_code code = execution_check_locked(runtime);
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
  pthread_mutex_lock(&runtime->mutex);
  qaplet_code code = qaplet_execution_dispatch_locked(runtime);
  pthread_mutex_unlock(&runtime->mutex);
  return code;
}

qaplet_code qaplet_execution_dispatch_locked(qaplet_runtime *runtime) {
  qaplet_code code = execution_check_locked(runtime);
  if (code) return code;
  if (runtime->snapshot.execution_dispatches >= runtime->execution_dispatch_limit)
    qaplet_record_termination_locked(runtime, QAPLET_DISPATCH_LIMIT);
  else ++runtime->snapshot.execution_dispatches;
  code = runtime->snapshot.termination_reason;
  return code;
}

struct qaplet_rejection {
  JSValue promise, reason;
  qaplet_rejection *next;
};

static void release_rejection(qaplet_runtime *runtime, qaplet_rejection *entry) {
  JS_FreeValue(runtime->context, entry->promise);
  JS_FreeValue(runtime->context, entry->reason);
  qaplet_native_free(runtime, entry, sizeof(*entry));
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
  qaplet_rejection *entry = qaplet_native_alloc(runtime, sizeof(*entry));
  if (!entry) return;
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
  if (runtime->snapshot.termination_reason) {
    if (runtime->snapshot.lifecycle == QAPLET_STOP_REQUESTED)
      runtime->snapshot.lifecycle = QAPLET_STOPPED;
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
  if (runtime->m2_enabled && qaplet_get_lifecycle(runtime) != QAPLET_OPEN) qaplet_loop_shutdown(runtime);
  return progress->code;
}

qaplet_code qaplet_pump(qaplet_runtime *runtime, qaplet_progress *progress,
                       qaplet_diagnostic *diagnostic) {
  qaplet_code code = qaplet_check_diagnostic(diagnostic);
  if (code) return code;
  code = qaplet_check_structure(progress, sizeof(*progress));
  if (!code) code = qaplet_owner(runtime);
  qaplet_lifecycle lifecycle = !code ? qaplet_get_lifecycle(runtime) : QAPLET_CLOSED;
  if (!code && lifecycle != QAPLET_OPEN && lifecycle != QAPLET_STOP_REQUESTED &&
      !(runtime->m2_enabled && lifecycle != QAPLET_CLOSED)) code = QAPLET_BAD_STATE;
  if (code) return qaplet_report(diagnostic, code);
  qaplet_driver_set(runtime, 1);
  code = qaplet_pump_ready(runtime, progress, diagnostic);
  qaplet_driver_set(runtime, 0);
  return code;
}

qaplet_code qaplet_pump_ready(qaplet_runtime *runtime, qaplet_progress *progress,
                             qaplet_diagnostic *diagnostic) {
  qaplet_code code = QAPLET_OK;
  if (qaplet_execution_check(runtime)) {
    code = qaplet_execution_checkpoint(runtime, progress, 0);
    return qaplet_report_result(runtime, diagnostic, code);
  }
  runtime->host_registration_locked = 1;
  uint64_t jobs = 0, events = 0;
  unsigned progressed = 0;
  for (;;) {
    if (runtime->execution_active) {
      pthread_mutex_lock(&runtime->mutex);
      runtime->snapshot.phase = QAPLET_PHASE_RUNNING;
      pthread_mutex_unlock(&runtime->mutex);
      while (jobs < runtime->snapshot.limits.pump_jobs && JS_IsJobPending(runtime->engine) &&
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
    if (events == runtime->snapshot.limits.pump_events || !qaplet_events_pending(runtime)) {
      progress->phase = runtime->snapshot.phase;
      progress->progressed = progressed;
      progress->code = QAPLET_OK;
      break;
    }
    /* A pending entry owns its ledger. Otherwise one event plus its runnable
     * jobs owns a fresh independent ledger, which stays active across pumps. */
    if (!runtime->execution_active) {
      qaplet_execution_budget budget;
      code = qaplet_execution_prepare(runtime, NULL, &budget);
      if (!code) code = qaplet_execution_begin(runtime, &budget);
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
  /* Stop may win after the empty-queue check or before an idle event's begin.
   * Confirm it at this owner boundary without scheduling another job/event. */
  if (qaplet_execution_check(runtime))
    code = qaplet_execution_checkpoint(runtime, progress, progressed);
  return qaplet_report_result(runtime, diagnostic, code);
}
