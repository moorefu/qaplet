/* SPDX-License-Identifier: MIT */
#include "runtime_internal.h"
#include <limits.h>
#include <stddef.h>
#include <string.h>
#include <time.h>
#include <errno.h>

qaplet_code qaplet_m2_configure(const qaplet_m2_config *config,
                               const qaplet_limits *base, qaplet_m2_limits *limits) {
  qaplet_code code = qaplet_check_structure(config, sizeof(*config));
  if (!code) code = qaplet_check_structure(&config->limits, sizeof(config->limits));
  if (code) return code;
  if (config->flags || config->reserved[0] || config->reserved[1] ||
      config->reserved[2] || config->reserved[3]) return QAPLET_INVALID_ARGUMENT;
  *limits = config->limits;
#define DEFAULT(field, value) if (!limits->field) limits->field = (value)
  DEFAULT(worker_threads, 2); DEFAULT(helper_threads, 4);
  DEFAULT(worker_stack_bytes, 1048576); DEFAULT(helper_stack_bytes, 1048576);
  DEFAULT(work_handlers, 64); DEFAULT(handler_concurrency, 1); DEFAULT(timers, 1024);
  DEFAULT(reactor_registrations, 256); DEFAULT(native_handles, 256);
  DEFAULT(processes, 32); DEFAULT(keepalive_tokens, 64); DEFAULT(operations, 128);
  DEFAULT(operation_data_bytes, 8388608); DEFAULT(work_input_bytes, base->bridge_value_bytes);
  DEFAULT(work_output_bytes, base->bridge_value_bytes);
  DEFAULT(io_block_bytes, base->bridge_value_bytes < 65536 ? base->bridge_value_bytes : 65536);
  DEFAULT(process_arguments, 256); DEFAULT(process_data_bytes, 1048576); DEFAULT(pump_io_bytes, 262144);
#undef DEFAULT
  /* Validate representations even for not-yet-enabled resource capacities.
   * No declared capacity grants a feature or starts a native resource. */
  const uint64_t counts[] = {limits->worker_threads, limits->helper_threads,
    limits->work_handlers, limits->handler_concurrency, limits->timers,
    limits->reactor_registrations, limits->native_handles, limits->processes,
    limits->keepalive_tokens, limits->operations, limits->process_arguments};
  for (size_t i = 0; i < sizeof(counts) / sizeof(*counts); ++i)
    if (counts[i] > INT_MAX) return QAPLET_INVALID_ARGUMENT;
  const uint64_t bytes[] = {limits->worker_stack_bytes, limits->helper_stack_bytes,
    limits->operation_data_bytes, limits->work_input_bytes, limits->work_output_bytes,
    limits->io_block_bytes, limits->process_data_bytes, limits->pump_io_bytes};
  for (size_t i = 0; i < sizeof(bytes) / sizeof(*bytes); ++i)
    if (bytes[i] > PTRDIFF_MAX) return QAPLET_INVALID_ARGUMENT;
  if (limits->worker_stack_bytes < PTHREAD_STACK_MIN || limits->helper_stack_bytes < PTHREAD_STACK_MIN ||
      limits->worker_threads > SIZE_MAX / limits->worker_stack_bytes ||
      limits->helper_threads > SIZE_MAX / limits->helper_stack_bytes ||
      limits->handler_concurrency > limits->worker_threads ||
      limits->operation_data_bytes > base->native_storage_bytes ||
      limits->work_input_bytes > base->bridge_value_bytes || limits->work_output_bytes > base->bridge_value_bytes ||
      limits->io_block_bytes > base->bridge_value_bytes || limits->io_block_bytes > limits->operation_data_bytes ||
      limits->process_data_bytes > base->native_storage_bytes)
    return QAPLET_INVALID_ARGUMENT;
  return QAPLET_OK;
}

qaplet_code qaplet_create_m2(const qaplet_config *base, const qaplet_m2_config *config,
                            qaplet_runtime **output, qaplet_diagnostic *diagnostic) {
  if (!config) {
    if (output) *output = NULL;
    qaplet_code code = qaplet_check_diagnostic(diagnostic);
    return code ? code : qaplet_report(diagnostic, QAPLET_INVALID_ARGUMENT);
  }
  return qaplet_create_internal(base, config, output, diagnostic);
}

void qaplet_driver_set(qaplet_runtime *runtime, int active) {
  pthread_mutex_lock(&runtime->mutex);
  runtime->driver_active = active;
  pthread_mutex_unlock(&runtime->mutex);
}

qaplet_code qaplet_get_m2_snapshot(qaplet_runtime *runtime, qaplet_m2_snapshot *output,
                                  qaplet_diagnostic *diagnostic) {
  qaplet_code code = qaplet_check_diagnostic(diagnostic);
  if (code) return code;
  code = qaplet_check_structure(output, sizeof(*output));
  if (!code && !runtime) code = QAPLET_INVALID_ARGUMENT;
  if (code) return qaplet_report(diagnostic, code);
  qaplet_m2_snapshot value = QAPLET_M2_SNAPSHOT_INIT;
  qaplet_abi abi = QAPLET_ABI_INIT;
  qaplet_query_abi(&abi);
  pthread_mutex_lock(&runtime->mutex);
  value.capabilities = runtime->m2_enabled ? abi.capabilities : abi.capabilities & ~((uint64_t)QAPLET_CAP_M2_RUNTIME);
  value.lifecycle = runtime->snapshot.lifecycle;
  value.phase = runtime->snapshot.phase;
  value.termination_reason = runtime->snapshot.termination_reason;
  value.cleanup_reason = runtime->cleanup_reason;
  value.native_state = runtime->native_state;
  value.driving = runtime->driver_active;
  value.limits = runtime->snapshot.limits;
  value.m2_limits = runtime->m2_limits;
  value.engine_current_bytes = runtime->snapshot.engine_current_bytes;
  value.engine_peak_bytes = runtime->snapshot.engine_peak_bytes;
  value.native_current_bytes = runtime->snapshot.native_current_bytes;
  value.native_peak_bytes = runtime->snapshot.native_peak_bytes;
  pthread_mutex_unlock(&runtime->mutex);
  memcpy((char *)output + 8, (char *)&value + 8, sizeof(value) - 8);
  return qaplet_report(diagnostic, QAPLET_OK);
}

static qaplet_code drive_preflight(qaplet_runtime *runtime, uint64_t wait,
                                   qaplet_drive_result *output, qaplet_diagnostic *diagnostic) {
  qaplet_code code = qaplet_check_diagnostic(diagnostic);
  if (code) return code;
  code = qaplet_check_structure(output, sizeof(*output));
  if (!code && wait > UINT64_MAX / 1000000) code = QAPLET_INVALID_ARGUMENT;
  if (!code) code = qaplet_owner(runtime);
  if (!code && !runtime->m2_enabled) code = QAPLET_UNSUPPORTED;
  return code;
}

static qaplet_code drive(qaplet_runtime *runtime, uint64_t wait, int continuous,
                         qaplet_drive_result *output, qaplet_diagnostic *diagnostic) {
  qaplet_code code = drive_preflight(runtime, wait, output, diagnostic);
  if (code) return qaplet_report(diagnostic, code);
  qaplet_lifecycle state = qaplet_get_lifecycle(runtime);
  if (state != QAPLET_OPEN && state != QAPLET_STOP_REQUESTED)
    return qaplet_report(diagnostic, QAPLET_BAD_STATE);
  qaplet_driver_set(runtime, 1);
  qaplet_progress progress = QAPLET_PROGRESS_INIT;
  unsigned progressed = 0;
  do {
    code = qaplet_pump_ready(runtime, &progress, diagnostic);
    progressed |= progress.progressed;
    /* Only accepted events and jobs exist at this capability level. A Promise
     * alone has no source of future progress and grants no permission to wait. */
  } while (continuous && !code && progress.progressed &&
           (JS_IsJobPending(runtime->engine) || qaplet_events_pending(runtime)));
  output->phase = progress.phase;
  output->progressed = progressed;
  output->code = code;
  output->reason = code == QAPLET_EXECUTION_STOPPED ? QAPLET_DRIVE_STOPPED : code ? QAPLET_DRIVE_ERROR :
    progress.phase == QAPLET_PHASE_PENDING && !JS_IsJobPending(runtime->engine) && !qaplet_events_pending(runtime) ?
      QAPLET_DRIVE_UNSETTLED_ENTRY : (progressed && !continuous) ? QAPLET_DRIVE_PROGRESS : QAPLET_DRIVE_IDLE;
  qaplet_driver_set(runtime, 0);
  return code;
}

qaplet_code qaplet_run_once(qaplet_runtime *runtime, uint64_t wait,
                           qaplet_drive_result *output, qaplet_diagnostic *diagnostic) {
  return drive(runtime, wait, 0, output, diagnostic);
}
qaplet_code qaplet_run(qaplet_runtime *runtime, qaplet_drive_result *output, qaplet_diagnostic *diagnostic) {
  return drive(runtime, 0, 1, output, diagnostic);
}
qaplet_code qaplet_native_retain(qaplet_runtime *runtime) {
  pthread_mutex_lock(&runtime->mutex);
  qaplet_code code = !runtime->m2_enabled ? QAPLET_UNSUPPORTED :
    runtime->snapshot.lifecycle != QAPLET_OPEN ? QAPLET_BAD_STATE :
    runtime->native_retained == UINT64_MAX ? QAPLET_BUSY : QAPLET_OK;
  if (!code) ++runtime->native_retained;
  pthread_mutex_unlock(&runtime->mutex);
  return code;
}
void qaplet_native_release(qaplet_runtime *runtime) {
  pthread_mutex_lock(&runtime->mutex);
  if (runtime->native_retained) --runtime->native_retained;
  if (!runtime->native_retained && runtime->snapshot.lifecycle == QAPLET_CLOSED)
    runtime->native_state = QAPLET_NATIVE_QUIESCENT;
  pthread_cond_broadcast(&runtime->native_condition);
  pthread_mutex_unlock(&runtime->mutex);
}

qaplet_code qaplet_quiesce(qaplet_runtime *runtime, uint64_t wait,
                          qaplet_drive_result *output, qaplet_diagnostic *diagnostic) {
  qaplet_code code = drive_preflight(runtime, wait, output, diagnostic);
  if (!code && qaplet_get_lifecycle(runtime) != QAPLET_CLOSED) code = QAPLET_BAD_STATE;
  if (code) return qaplet_report(diagnostic, code);
  qaplet_driver_set(runtime, 1);
  struct timespec deadline;
  if (!code && wait) {
    if (clock_gettime(CLOCK_MONOTONIC, &deadline) || deadline.tv_sec < 0) code = QAPLET_BAD_STATE;
    else {
      uint64_t seconds = wait / 1000;
      uint64_t nanoseconds = (uint64_t)deadline.tv_nsec + wait % 1000 * 1000000;
      seconds += nanoseconds / 1000000000;
      if (seconds > (uint64_t)INT64_MAX - (uint64_t)deadline.tv_sec) code = QAPLET_INVALID_ARGUMENT;
      else { deadline.tv_sec += seconds; deadline.tv_nsec = nanoseconds % 1000000000; }
    }
  }
  if (code) { qaplet_driver_set(runtime, 0); return qaplet_report(diagnostic, code); }
  pthread_mutex_lock(&runtime->mutex);
  int pending = runtime->native_state != QAPLET_NATIVE_QUIESCENT;
  while (pending && wait) {
    int error = pthread_cond_timedwait(&runtime->native_condition, &runtime->mutex, &deadline);
    pending = runtime->native_state != QAPLET_NATIVE_QUIESCENT;
    if (error == ETIMEDOUT) break;
    if (error) {
      if (!runtime->cleanup_reason) runtime->cleanup_reason = QAPLET_BAD_STATE;
      break;
    }
  }
  output->phase = runtime->snapshot.phase;
  output->progressed = 0;
  output->code = runtime->cleanup_reason;
  output->reason = pending ? QAPLET_DRIVE_WAIT_TIMEOUT : QAPLET_DRIVE_QUIESCENT;
  code = output->code;
  pthread_mutex_unlock(&runtime->mutex);
  qaplet_driver_set(runtime, 0);
  return qaplet_report(diagnostic, code);
}
