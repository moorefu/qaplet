/* SPDX-License-Identifier: MIT */
#include "runtime_internal.h"
#include <stdlib.h>

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
    if (!runtime->snapshot.termination_reason) runtime->snapshot.termination_reason = QAPLET_MEMORY_LIMIT;
    runtime->snapshot.lifecycle = QAPLET_FAILED;
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
}

void qaplet_execution_dispose(qaplet_runtime *runtime) {
  JS_SetHostPromiseRejectionTracker(runtime->engine, NULL, NULL);
  clear_rejections(runtime);
  JS_FreeValue(runtime->context, runtime->entry_promise);
  runtime->entry_promise = JS_UNDEFINED;
}

/* A checkpoint is complete only when all runnable jobs have settled. */
qaplet_code qaplet_execution_checkpoint(qaplet_runtime *runtime,
                                       qaplet_progress *progress, unsigned progressed) {
  int pending = JS_IsJobPending(runtime->engine);
  if (!pending && !JS_IsUndefined(runtime->entry_promise)) {
    JSPromiseStateEnum state = JS_PromiseState(runtime->context, runtime->entry_promise);
    if (state == JS_PROMISE_PENDING) pending = 1;
    else {
      JSValue value = JS_PromiseResult(runtime->context, runtime->entry_promise);
      if (!runtime->execution_code) {
        if (state == JS_PROMISE_REJECTED) {
          qaplet_capture_exception(runtime, value);
          runtime->execution_code = QAPLET_JS_EXCEPTION;
        } else runtime->execution_code = qaplet_store_scalar(runtime, value);
      }
      JS_FreeValue(runtime->context, value);
    }
  }
  if (!JS_IsJobPending(runtime->engine)) {
    if (runtime->rejections && !runtime->execution_code) {
      qaplet_capture_exception(runtime, runtime->rejections->reason);
      runtime->execution_code = QAPLET_JS_EXCEPTION;
    }
    /* Includes the entry Promise's tracker record. It has already been
     * classified above, and must not resurface in a later execution. */
    clear_rejections(runtime);
    if (runtime->execution_code) pending = 0;
    if (!pending) {
      JS_FreeValue(runtime->context, runtime->entry_promise);
      runtime->entry_promise = JS_UNDEFINED;
    }
  }
  pthread_mutex_lock(&runtime->mutex);
  if (runtime->snapshot.lifecycle == QAPLET_FAILED) {
    runtime->execution_code = runtime->snapshot.termination_reason;
    pending = 0;
  }
  runtime->snapshot.phase = pending ? QAPLET_PHASE_PENDING : QAPLET_PHASE_DONE;
  if (!pending) {
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
  if (runtime->snapshot.phase != QAPLET_PHASE_PENDING) {
    progress->phase = runtime->snapshot.phase;
    progress->progressed = 0;
    progress->code = QAPLET_OK;
    return qaplet_report(diagnostic, QAPLET_OK);
  }
  pthread_mutex_lock(&runtime->mutex);
  runtime->snapshot.phase = QAPLET_PHASE_RUNNING;
  pthread_mutex_unlock(&runtime->mutex);
  unsigned executed = 0;
  while (executed < 256 && JS_IsJobPending(runtime->engine) &&
         runtime->snapshot.lifecycle == QAPLET_OPEN) {
    JSContext *context = NULL;
    int status = JS_ExecutePendingJob(runtime->engine, &context);
    if (!status) break;
    ++executed;
    if (status < 0) {
      JSValue exception = JS_GetException(context);
      if (!runtime->execution_code) {
        qaplet_capture_exception(runtime, exception);
        runtime->execution_code = QAPLET_JS_EXCEPTION;
      }
      JS_FreeValue(context, exception);
    }
  }
  code = qaplet_execution_checkpoint(runtime, progress, executed != 0);
  return qaplet_report_result(runtime, diagnostic, code);
}
