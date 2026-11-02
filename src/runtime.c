/* SPDX-License-Identifier: MIT */
#include "runtime_internal.h"
#include <stddef.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

/* Own allocation headers keep accounting independent of libc usable-size APIs. */
union qaplet_allocation {
  struct { size_t size; qaplet_allocation *previous, *next; } record;
  max_align_t alignment;
};
typedef qaplet_allocation allocation;

static void memory_failure(qaplet_runtime *runtime) {
  qaplet_record_termination_locked(runtime, QAPLET_MEMORY_LIMIT);
}

static void *engine_malloc(JSMallocState *state, size_t size) {
  qaplet_runtime *runtime = state->opaque;
  if (!size) return NULL;
  pthread_mutex_lock(&runtime->mutex);
  uint64_t current = runtime->snapshot.engine_current_bytes;
  if (size > SIZE_MAX - sizeof(allocation) ||
      size + sizeof(allocation) > runtime->snapshot.limits.engine_heap_bytes - current) {
    memory_failure(runtime);
    pthread_mutex_unlock(&runtime->mutex);
    return NULL;
  }
  allocation *block = malloc(sizeof(*block) + size);
  if (!block) {
    memory_failure(runtime);
    pthread_mutex_unlock(&runtime->mutex);
    return NULL;
  }
  block->record.size = size;
  block->record.previous = NULL;
  block->record.next = runtime->engine_allocations;
  if (runtime->engine_allocations) runtime->engine_allocations->record.previous = block;
  runtime->engine_allocations = block;
  runtime->snapshot.engine_current_bytes += sizeof(*block) + size;
  if (runtime->snapshot.engine_current_bytes > runtime->snapshot.engine_peak_bytes)
    runtime->snapshot.engine_peak_bytes = runtime->snapshot.engine_current_bytes;
  state->malloc_count++;
  state->malloc_size += sizeof(*block) + size;
  pthread_mutex_unlock(&runtime->mutex);
  return block + 1;
}

static void engine_free(JSMallocState *state, void *pointer) {
  if (!pointer) return;
  qaplet_runtime *runtime = state->opaque;
  allocation *block = (allocation *)pointer - 1;
  pthread_mutex_lock(&runtime->mutex);
  if (block->record.previous) block->record.previous->record.next = block->record.next;
  else runtime->engine_allocations = block->record.next;
  if (block->record.next) block->record.next->record.previous = block->record.previous;
  runtime->snapshot.engine_current_bytes -= sizeof(*block) + block->record.size;
  state->malloc_count--;
  state->malloc_size -= sizeof(*block) + block->record.size;
  pthread_mutex_unlock(&runtime->mutex);
  free(block);
}

static void *engine_realloc(JSMallocState *state, void *pointer, size_t size) {
  if (!pointer) return engine_malloc(state, size);
  if (!size) { engine_free(state, pointer); return NULL; }
  allocation *block = (allocation *)pointer - 1;
  if (size <= block->record.size) return pointer;
  qaplet_runtime *runtime = state->opaque;
  pthread_mutex_lock(&runtime->mutex);
  size_t growth = size - block->record.size;
  if (size > SIZE_MAX - sizeof(*block) ||
      growth > runtime->snapshot.limits.engine_heap_bytes - runtime->snapshot.engine_current_bytes) {
    memory_failure(runtime);
    pthread_mutex_unlock(&runtime->mutex);
    return NULL;
  }
  allocation *grown = realloc(block, sizeof(*block) + size);
  if (!grown) {
    memory_failure(runtime);
    pthread_mutex_unlock(&runtime->mutex);
    return NULL;
  }
  grown->record.size = size;
  if (grown->record.previous) grown->record.previous->record.next = grown;
  else runtime->engine_allocations = grown;
  if (grown->record.next) grown->record.next->record.previous = grown;
  runtime->snapshot.engine_current_bytes += growth;
  if (runtime->snapshot.engine_current_bytes > runtime->snapshot.engine_peak_bytes)
    runtime->snapshot.engine_peak_bytes = runtime->snapshot.engine_current_bytes;
  state->malloc_size += growth;
  pthread_mutex_unlock(&runtime->mutex);
  return grown + 1;
}

static size_t engine_usable_size(const void *pointer) {
  return pointer ? ((const allocation *)pointer - 1)->record.size : 0;
}

static void discard_unpublished_engine(qaplet_runtime *runtime) {
  /* No user JS, Host modules or external native resources exist during the
   * factory. An allocation refusal can leave a partial engine object graph
   * which is unsafe to traverse (including a freed Context GC header). Reclaim
   * its backing allocations directly. Published instances always use the
   * normal Context/Runtime finalization path in close. */
  allocation *block = runtime->engine_allocations;
  while (block) {
    allocation *next = block->record.next;
    free(block);
    block = next;
  }
  runtime->engine_allocations = NULL;
}

qaplet_code qaplet_owner(qaplet_runtime *runtime) {
  if (!runtime) return QAPLET_INVALID_ARGUMENT;
  if (!pthread_equal(runtime->owner, pthread_self())) return QAPLET_WRONG_THREAD;
  if (runtime->snapshot.phase == QAPLET_PHASE_RUNNING) return QAPLET_BUSY;
  return QAPLET_OK;
}

qaplet_lifecycle qaplet_get_lifecycle(qaplet_runtime *runtime) {
  pthread_mutex_lock(&runtime->mutex);
  qaplet_lifecycle lifecycle = runtime->snapshot.lifecycle;
  pthread_mutex_unlock(&runtime->mutex);
  return lifecycle;
}

qaplet_code qaplet_query_abi(qaplet_abi *output) {
  qaplet_code code = qaplet_check_structure(output, sizeof(*output));
  if (!code) output->capabilities = QAPLET_CAP_LIFECYCLE | QAPLET_CAP_SYNC_SCRIPT |
      QAPLET_CAP_SCALAR_RESULT | QAPLET_CAP_OWNER_THREAD | QAPLET_CAP_POD_SNAPSHOT |
      QAPLET_CAP_RESOURCE_BASE | QAPLET_CAP_COPIED_VALUES | QAPLET_CAP_PROMISE_JOBS |
      QAPLET_CAP_FILE_MODULES | QAPLET_CAP_EXECUTION_BUDGETS | QAPLET_CAP_MODULE_POLICY | QAPLET_CAP_HOST_MODULES |
      QAPLET_CAP_POD_EVENTS | QAPLET_CAP_RUNTIME_SNAPSHOT | QAPLET_CAP_RESOURCE_LIMITS | QAPLET_CAP_COOPERATIVE_STOP;
  return code;
}

qaplet_code qaplet_create(const qaplet_config *config, qaplet_runtime **output,
                         qaplet_diagnostic *diagnostic) {
  if (output) *output = NULL;
  qaplet_code code = qaplet_check_diagnostic(diagnostic);
  if (code) return code;
  code = qaplet_check_structure(config, sizeof(*config));
  if (!code) code = qaplet_check_structure(&config->limits, sizeof(config->limits));
  if (code) return qaplet_report(diagnostic, code);
  if (!output || config->flags || config->reserved[0] || config->reserved[1] ||
      config->reserved[2] || config->reserved[3]) return qaplet_report(diagnostic, QAPLET_INVALID_ARGUMENT);
  code = qaplet_check_runtime_config(config);
  if (code) return qaplet_report(diagnostic, code);
  qaplet_limits limits = config->limits;
  if (!limits.engine_heap_bytes) limits.engine_heap_bytes = 64u * 1024 * 1024;
  if (!limits.engine_stack_bytes) limits.engine_stack_bytes = 1024u * 1024;
  if (!limits.native_storage_bytes) limits.native_storage_bytes = 16u * 1024 * 1024;
  if (!limits.source_bytes) limits.source_bytes = 4u * 1024 * 1024;
  if (!limits.module_count) limits.module_count = 256;
  if (limits.module_count < 2) return qaplet_report(diagnostic, QAPLET_INVALID_ARGUMENT);
  if (!limits.source_cache_bytes) limits.source_cache_bytes = 32u * 1024 * 1024;
  if (!limits.execution_timeout_ms) limits.execution_timeout_ms = 30000;
  if (!limits.execution_dispatches) limits.execution_dispatches = 100000;
  if (!limits.event_queue_slots) limits.event_queue_slots = 256;
  if (!limits.event_payload_bytes) limits.event_payload_bytes = 1024;
  if (!limits.bridge_value_bytes) limits.bridge_value_bytes = QAPLET_VALUE_BYTES_MAX;
  if (!limits.host_arguments) limits.host_arguments = QAPLET_HOST_ARGUMENTS_MAX;
  if (!limits.pump_jobs) limits.pump_jobs = 256;
  if (!limits.pump_events) limits.pump_events = 32;
  code = qaplet_events_check_limits(&limits);
  if (code) return qaplet_report(diagnostic, code);
  if (limits.execution_timeout_ms > UINT64_MAX / 1000000)
    return qaplet_report(diagnostic, QAPLET_INVALID_ARGUMENT);
  if (limits.engine_heap_bytes > SIZE_MAX || limits.engine_stack_bytes > PTRDIFF_MAX ||
      limits.native_storage_bytes > SIZE_MAX || limits.source_bytes >= SIZE_MAX ||
      limits.module_count > SIZE_MAX || limits.source_cache_bytes >= SIZE_MAX ||
      limits.bridge_value_bytes >= SIZE_MAX || limits.host_arguments > INT_MAX ||
      limits.host_arguments > SIZE_MAX / (sizeof(qaplet_result) + sizeof(qaplet_value) + sizeof(void *)))
    return qaplet_report(diagnostic, QAPLET_INVALID_ARGUMENT);
  if (limits.native_storage_bytes < sizeof(qaplet_runtime))
    return qaplet_report(diagnostic, QAPLET_MEMORY_LIMIT);
  qaplet_runtime *runtime = calloc(1, sizeof(*runtime));
  if (!runtime) return qaplet_report(diagnostic, QAPLET_MEMORY_LIMIT);
  runtime->owner = pthread_self();
  if (pthread_mutex_init(&runtime->mutex, NULL)) {
    free(runtime);
    return qaplet_report(diagnostic, QAPLET_MEMORY_LIMIT);
  }
  runtime->snapshot = (qaplet_snapshot)QAPLET_SNAPSHOT_INIT;
  runtime->snapshot.limits = limits;
  runtime->snapshot.native_current_bytes = runtime->snapshot.native_peak_bytes = sizeof(*runtime);
  runtime->result = (qaplet_result)QAPLET_RESULT_INIT;
  const JSMallocFunctions allocator = {engine_malloc, engine_free, engine_realloc, engine_usable_size};
  runtime->engine = JS_NewRuntime2(&allocator, runtime);
  if (runtime->engine) {
    JS_SetMaxStackSize(runtime->engine, (size_t)limits.engine_stack_bytes);
    runtime->context = JS_NewContext(runtime->engine);
  }
  if (!runtime->context || runtime->snapshot.lifecycle == QAPLET_FAILED || qaplet_values_init(runtime)) {
    discard_unpublished_engine(runtime);
    pthread_mutex_destroy(&runtime->mutex);
    free(runtime);
    return qaplet_report(diagnostic, QAPLET_MEMORY_LIMIT);
  }
  qaplet_execution_init(runtime);
  runtime->event_handler = JS_UNDEFINED;
  JS_SetContextOpaque(runtime->context, runtime);
  code = qaplet_modules_init(runtime, config);
  if (!code) code = qaplet_events_init(runtime);
  if (!code) code = qaplet_execution_check(runtime);
  if (code) {
    qaplet_events_dispose(runtime);
    qaplet_modules_dispose(runtime);
    qaplet_execution_dispose(runtime);
    qaplet_runtime_snapshot_dispose(runtime);
    discard_unpublished_engine(runtime);
    pthread_mutex_destroy(&runtime->mutex);
    free(runtime);
    return qaplet_report(diagnostic, code);
  }
  *output = runtime;
  return qaplet_report(diagnostic, QAPLET_OK);
}

qaplet_code qaplet_eval(qaplet_runtime *runtime, const char *source, size_t length,
                       qaplet_progress *progress, qaplet_diagnostic *diagnostic) {
  return qaplet_eval_filename(runtime, source, length, "<eval>", NULL, progress, diagnostic);
}

qaplet_code qaplet_eval_with_options(qaplet_runtime *runtime, const char *source, size_t length,
                                    const qaplet_execution_options *options,
                                    qaplet_progress *progress, qaplet_diagnostic *diagnostic) {
  return qaplet_eval_filename(runtime, source, length, "<eval>", options, progress, diagnostic);
}

qaplet_code qaplet_eval_filename(qaplet_runtime *runtime, const char *source, size_t length,
                               const char *filename, const qaplet_execution_options *options,
                               qaplet_progress *progress, qaplet_diagnostic *diagnostic) {
  qaplet_code code = qaplet_check_diagnostic(diagnostic);
  if (code) return code;
  code = qaplet_check_structure(progress, sizeof(*progress));
  if (!code) code = qaplet_check_execution_options(NULL, options);
  if (!code && !source && length) code = QAPLET_INVALID_ARGUMENT;
  if (!code) code = qaplet_owner(runtime);
  if (!code && qaplet_get_lifecycle(runtime) != QAPLET_OPEN) code = QAPLET_BAD_STATE;
  if (!code && runtime->snapshot.phase == QAPLET_PHASE_PENDING) code = QAPLET_BUSY;
  if (!code) code = qaplet_check_execution_options(runtime, options);
  if (!code && length > runtime->snapshot.limits.source_bytes) code = QAPLET_SOURCE_TOO_LARGE;
  if (!code && !qaplet_utf8_valid(source, length)) code = QAPLET_INVALID_UTF8;
  if (code) return qaplet_report(diagnostic, code);
  /* A source copy is native SDK storage, separate from the engine allocator. */
  char *copy = qaplet_native_alloc(runtime, length + 1);
  if (!copy) return qaplet_report(diagnostic, QAPLET_MEMORY_LIMIT);
  code = qaplet_execution_begin(runtime, options);
  if (code) {
    qaplet_native_free(runtime, copy, length + 1);
    return qaplet_report(diagnostic, code);
  }
  runtime->module_execution = 0;
  if (length) memcpy(copy, source, length);
  copy[length] = 0;
  JSValue value = JS_Eval(runtime->context, copy, length, filename, JS_EVAL_TYPE_GLOBAL);
  code = qaplet_execution_check(runtime);
  if (JS_IsException(value)) {
    JSValue exception = JS_GetException(runtime->context);
    if (!code) qaplet_capture_exception(runtime, exception);
    if (!code) code = qaplet_module_exception_code(runtime, exception);
    JS_FreeValue(runtime->context, exception);
  } else if (!code && (int)JS_PromiseState(runtime->context, value) >= 0) {
    runtime->entry_promise = JS_DupValue(runtime->context, value);
  } else if (!code) code = qaplet_store_scalar(runtime, value);
  JS_FreeValue(runtime->context, value);
  qaplet_native_free(runtime, copy, length + 1);
  pthread_mutex_lock(&runtime->mutex);
  runtime->execution_code = code;
  pthread_mutex_unlock(&runtime->mutex);
  code = qaplet_execution_checkpoint(runtime, progress, 1);
  return qaplet_report_result(runtime, diagnostic, code);
}

qaplet_code qaplet_get_snapshot(qaplet_runtime *runtime, qaplet_snapshot *output,
                               qaplet_diagnostic *diagnostic) {
  qaplet_code code = qaplet_check_diagnostic(diagnostic);
  if (code) return code;
  code = qaplet_check_structure(output, sizeof(*output));
  if (!code && !runtime) code = QAPLET_INVALID_ARGUMENT;
  if (!code) {
    pthread_mutex_lock(&runtime->mutex);
    memcpy((char *)output + 8, (char *)&runtime->snapshot + 8, sizeof(*output) - 8);
    pthread_mutex_unlock(&runtime->mutex);
  }
  return qaplet_report(diagnostic, code);
}

qaplet_code qaplet_close(qaplet_runtime *runtime, qaplet_diagnostic *diagnostic) {
  qaplet_code code = qaplet_check_diagnostic(diagnostic);
  if (code) return code;
  code = qaplet_owner(runtime);
  if (code) return qaplet_report(diagnostic, code);
  if (qaplet_get_lifecycle(runtime) != QAPLET_CLOSED) {
    qaplet_events_dispose(runtime);
    qaplet_execution_dispose(runtime);
    qaplet_modules_dispose(runtime);
    qaplet_values_dispose(runtime);
    JS_FreeContext(runtime->context);
    JS_FreeRuntime(runtime->engine);
    /* Function descriptors/code/userdata remain reachable through engine close. */
    qaplet_host_dispose(runtime);
    runtime->context = NULL;
    runtime->engine = NULL;
    runtime->has_result = 0;
    pthread_mutex_lock(&runtime->mutex);
    runtime->snapshot.lifecycle = QAPLET_CLOSED;
    runtime->snapshot.phase = QAPLET_PHASE_IDLE;
    pthread_mutex_unlock(&runtime->mutex);
  }
  return qaplet_report(diagnostic, QAPLET_OK);
}

qaplet_code qaplet_destroy(qaplet_runtime *runtime, qaplet_diagnostic *diagnostic) {
  qaplet_code code = qaplet_check_diagnostic(diagnostic);
  if (code) return code;
  code = qaplet_owner(runtime);
  if (!code && runtime->snapshot.lifecycle != QAPLET_CLOSED) code = QAPLET_BAD_STATE;
  if (!code) {
    qaplet_runtime_snapshot_dispose(runtime);
    pthread_mutex_destroy(&runtime->mutex);
    free(runtime);
  }
  return qaplet_report(diagnostic, code);
}
