/* SPDX-License-Identifier: MIT */
#include "runtime_internal.h"
#include "io_internal.h"
#include <limits.h>
#include <stddef.h>
#include <string.h>
#include <time.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <stdatomic.h>
#include <sys/socket.h>
#include <unistd.h>

static _Atomic uint64_t identities;
_Thread_local unsigned qaplet_in_notify;
static void increment(uint64_t *value) { if (*value != UINT64_MAX) ++*value; }

qaplet_code qaplet_set_host_notify(qaplet_runtime *runtime, const qaplet_host_notify *hook,
                                  qaplet_diagnostic *diagnostic) {
  qaplet_code code = qaplet_check_diagnostic(diagnostic);
  if (!code) code = qaplet_check_structure(hook, sizeof(*hook));
  if (!code && (!hook->function || hook->flags || hook->reserved[0] || hook->reserved[1])) code = QAPLET_INVALID_ARGUMENT;
  if (!code) code = qaplet_owner(runtime);
  if (code) return qaplet_report(diagnostic, code);
  pthread_mutex_lock(&runtime->mutex);
  if (!runtime->m2_enabled) code = QAPLET_UNSUPPORTED;
  else if (runtime->snapshot.lifecycle != QAPLET_OPEN || runtime->loop_closed ||
           runtime->host_registration_locked || runtime->notify_function) code = QAPLET_BAD_STATE;
  else { runtime->notify_function = hook->function; runtime->notify_userdata = hook->userdata; }
  pthread_mutex_unlock(&runtime->mutex);
  return qaplet_report(diagnostic, code);
}

qaplet_notification qaplet_notify_prepare_locked(qaplet_runtime *runtime) {
  qaplet_notification lease = {0};
  if (runtime->notify_function) {
    /* No per-notification allocation: one checked finite counter and each
     * caller's borrowed stack lease. OS threads exhaust before UINT64_MAX. */
    if (runtime->notify_inflight == UINT64_MAX) qaplet_control_failure_locked(runtime, EOVERFLOW);
    else {
      ++runtime->notify_inflight;
      if (runtime->notify_inflight > runtime->notify_inflight_peak) runtime->notify_inflight_peak = runtime->notify_inflight;
      if (runtime->loop_closed) runtime->native_state = QAPLET_NATIVE_CLEANING;
      lease.function = runtime->notify_function; lease.userdata = runtime->notify_userdata;
    }
  }
  return lease;
}
void qaplet_notify_dispatch(qaplet_runtime *runtime, qaplet_notification lease) {
  if (!lease.function) return;
  ++qaplet_in_notify;
  lease.function(lease.userdata);
  --qaplet_in_notify;
  pthread_mutex_lock(&runtime->mutex);
  --runtime->notify_inflight;
  if (runtime->loop_closed && !runtime->notify_inflight && !runtime->native_retained &&
      !runtime->native_handles && !runtime->native_uncertain_handles)
    runtime->native_state = QAPLET_NATIVE_QUIESCENT;
  pthread_cond_broadcast(&runtime->native_condition);
  pthread_mutex_unlock(&runtime->mutex);
}

qaplet_code qaplet_get_notify_snapshot(qaplet_runtime *runtime, qaplet_notify_snapshot *output,
                                      qaplet_diagnostic *diagnostic) {
  qaplet_code code = qaplet_check_diagnostic(diagnostic);
  if (!code) code = qaplet_check_structure(output, sizeof(*output));
  if (!code && !runtime) code = QAPLET_INVALID_ARGUMENT;
  if (code) return qaplet_report(diagnostic, code);
  qaplet_notify_snapshot value = QAPLET_NOTIFY_SNAPSHOT_INIT;
  pthread_mutex_lock(&runtime->mutex);
  if (!runtime->m2_enabled) code = QAPLET_UNSUPPORTED;
  else {
    value.installed = runtime->notify_function != NULL;
    value.inflight = runtime->notify_inflight;
    value.inflight_peak = runtime->notify_inflight_peak;
  }
  pthread_mutex_unlock(&runtime->mutex);
  if (!code) memcpy((char *)output + 8, (char *)&value + 8, sizeof(value) - 8);
  return qaplet_report(diagnostic, code);
}

void qaplet_control_failure_locked(qaplet_runtime *runtime, int error) {
  if (!runtime->control_error) runtime->control_error = error ? error : EIO;
  qaplet_record_termination_locked(runtime, QAPLET_CONTROL_FAILURE);
}

static void cleanup_failure_locked(qaplet_runtime *runtime, int error) {
  if (!runtime->cleanup_reason) {
    runtime->cleanup_reason = QAPLET_CONTROL_FAILURE;
    runtime->cleanup_error = error ? error : EIO;
  }
}

static void clock_failure_locked(qaplet_runtime *runtime, int error) {
  if (runtime->snapshot.lifecycle == QAPLET_CLOSED) cleanup_failure_locked(runtime, error);
  else qaplet_control_failure_locked(runtime, error);
}

int qaplet_clock_locked(qaplet_runtime *runtime, uint64_t *output) {
  struct timespec now;
  if (clock_gettime(CLOCK_MONOTONIC, &now) || now.tv_sec < 0 || now.tv_nsec < 0 ||
      now.tv_nsec >= 1000000000 || (uint64_t)now.tv_sec > (UINT64_MAX - now.tv_nsec) / 1000000000) {
    clock_failure_locked(runtime, errno ? errno : EIO);
    return 0;
  }
  *output = (uint64_t)now.tv_sec * 1000000000 + now.tv_nsec;
  if (*output < runtime->clock_last_ns) {
    clock_failure_locked(runtime, EIO);
    return 0;
  }
  runtime->clock_last_ns = *output;
  return 1;
}

static int socket_flags(int fd) {
  int flags = fcntl(fd, F_GETFL);
  return flags >= 0 && !fcntl(fd, F_SETFL, flags | O_NONBLOCK) && !fcntl(fd, F_SETFD, FD_CLOEXEC);
}
static int socket_ready(int fd, short events) {
  struct pollfd p = {fd, events, 0};
  return poll(&p, 1, 2000) == 1 && (p.revents & events);
}
static void close_socket(qaplet_runtime *, int *);
static int private_socket(qaplet_runtime *runtime, int fd) {
  if (fd >= 3) return fd;
  int error = 0, copy = -1;
  if (runtime->native_handles >= runtime->m2_limits.native_handles) error = EMFILE;
  else {
    copy = fcntl(fd, F_DUPFD_CLOEXEC, 3);
    if (copy < 0) error = errno;
    else {
      ++runtime->native_handles;
      if (runtime->native_handles > runtime->native_handles_peak) runtime->native_handles_peak = runtime->native_handles;
    }
  }
  close_socket(runtime, &fd);
  if (runtime->cleanup_reason || error) {
    close_socket(runtime, &copy);
    errno = error ? error : EIO;
    return -1;
  }
  return copy;
}
static int new_socket(qaplet_runtime *runtime) {
  int fd = socket(AF_INET, SOCK_STREAM, 0);
  if (fd >= 0) {
    ++runtime->native_handles;
    if (runtime->native_handles > runtime->native_handles_peak) runtime->native_handles_peak = runtime->native_handles;
  }
  return fd < 0 ? fd : private_socket(runtime, fd);
}
static void close_socket(qaplet_runtime *runtime, int *fd) {
  if (*fd < 0) return;
  /* Retire identity before the one close attempt. Neither errno nor a later
   * F_GETFD can prove a still-valid number refers to our original socket. */
  int owned = *fd;
  *fd = -1;
  --runtime->native_handles;
  int error = close(owned) ? errno : 0;
  if (error) {
    ++runtime->native_uncertain_handles;
    cleanup_failure_locked(runtime, error);
  }
}

qaplet_code qaplet_loop_init(qaplet_runtime *runtime) {
  runtime->wake_fd[0] = runtime->wake_fd[1] = -1;
  if (!runtime->m2_enabled) return QAPLET_OK;
  if (runtime->m2_limits.native_handles < 3 || runtime->m2_limits.reactor_registrations < 1 ||
      runtime->m2_limits.keepalive_tokens > SIZE_MAX / sizeof(*runtime->keepalives)) return QAPLET_RESOURCE_LIMIT;
  uint64_t id = atomic_load(&identities);
  do { if (id == UINT64_MAX) return QAPLET_RESOURCE_LIMIT; }
  while (!atomic_compare_exchange_weak(&identities, &id, id + 1));
  runtime->runtime_id = id + 1;
  runtime->keepalive_storage_size = runtime->m2_limits.keepalive_tokens * sizeof(*runtime->keepalives);
  runtime->keepalives = qaplet_native_alloc(runtime, runtime->keepalive_storage_size);
  if (!runtime->keepalives) return QAPLET_MEMORY_LIMIT;
  memset(runtime->keepalives, 0, runtime->keepalive_storage_size);
  int listener = -1, error = 0;
  struct sockaddr_in address = {0};
  socklen_t length = sizeof(address);
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  listener = new_socket(runtime);
  if (listener < 0 || !socket_flags(listener) || bind(listener, (void *)&address, length) ||
      getsockname(listener, (void *)&address, &length) || listen(listener, 1)) goto failed;
  runtime->wake_fd[1] = new_socket(runtime);
  if (runtime->wake_fd[1] < 0 || !socket_flags(runtime->wake_fd[1])) goto failed;
  if (connect(runtime->wake_fd[1], (void *)&address, length) && errno != EINPROGRESS && errno != EWOULDBLOCK) goto failed;
  if (!socket_ready(runtime->wake_fd[1], POLLOUT)) goto failed;
  length = sizeof(error);
  if (getsockopt(runtime->wake_fd[1], SOL_SOCKET, SO_ERROR, &error, &length) || error ||
      !socket_ready(listener, POLLIN)) goto failed;
  runtime->wake_fd[0] = accept(listener, NULL, NULL);
  if (runtime->wake_fd[0] < 0) goto failed;
  ++runtime->native_handles;
  if (runtime->native_handles > runtime->native_handles_peak) runtime->native_handles_peak = runtime->native_handles;
  runtime->wake_fd[0] = private_socket(runtime, runtime->wake_fd[0]);
  if (runtime->wake_fd[0] < 0) goto failed;
  if (!socket_flags(runtime->wake_fd[0])) goto failed;
  close_socket(runtime, &listener);
  if (runtime->cleanup_reason) goto failed;
  runtime->reactor_registrations = runtime->reactor_registrations_peak = 1;
  return QAPLET_OK;
failed:
  runtime->control_error = error ? error : errno ? errno : EIO;
  close_socket(runtime, &listener);
  qaplet_loop_shutdown(runtime);
  return QAPLET_CONTROL_FAILURE;
}

void qaplet_wake_locked(qaplet_runtime *runtime) {
  if (!runtime->m2_enabled || runtime->loop_closed) return;
  increment(&runtime->wake_requests);
  if (runtime->wake_pending) return;
  ssize_t sent;
  do { sent = send(runtime->wake_fd[1], "W", 1, MSG_NOSIGNAL); } while (sent < 0 && errno == EINTR);
  if (sent == 1 || (sent < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))) {
    runtime->wake_pending = 1;
    if (sent == 1) increment(&runtime->wake_signals);
  } else {
    qaplet_control_failure_locked(runtime, sent < 0 ? errno : EIO);
    /* A broken notification channel must also release an already sleeping
     * owner. Peer EOF/HUP is a control wake, never a business completion. */
    shutdown(runtime->wake_fd[1], SHUT_RDWR);
  }
}

void qaplet_loop_shutdown(qaplet_runtime *runtime) {
  if (!runtime->m2_enabled) return;
  qaplet_work_shutdown(runtime);
  qaplet_timers_dispose(runtime);
  pthread_mutex_lock(&runtime->mutex);
  qaplet_notification notification = {0};
  if (!runtime->loop_closed) notification = qaplet_notify_prepare_locked(runtime);
  int was_closed = runtime->loop_closed;
  runtime->loop_closed = 1;
  runtime->keepalive_tokens = 0;
  if (runtime->keepalives)
    for (uint64_t i = 0; i < runtime->m2_limits.keepalive_tokens; ++i) runtime->keepalives[i].active = 0;
  close_socket(runtime, &runtime->wake_fd[0]);
  close_socket(runtime, &runtime->wake_fd[1]);
  if (!was_closed && runtime->reactor_registrations) --runtime->reactor_registrations;
  runtime->native_state = runtime->native_retained || runtime->notify_inflight || runtime->native_handles || runtime->native_uncertain_handles ? QAPLET_NATIVE_CLEANING : QAPLET_NATIVE_QUIESCENT;
  pthread_mutex_unlock(&runtime->mutex);
  /* Owner-only retirement; releases under mutex have finished before this. */
  qaplet_native_free(runtime, runtime->keepalives, runtime->keepalive_storage_size);
  runtime->keepalives = NULL;
  qaplet_notify_dispatch(runtime, notification);
}

qaplet_code qaplet_keepalive_create(qaplet_runtime *runtime, qaplet_keepalive *output,
                                   qaplet_diagnostic *diagnostic) {
  qaplet_code code = qaplet_check_diagnostic(diagnostic);
  if (code) return code;
  if (!output) code = QAPLET_INVALID_ARGUMENT;
  if (!code) code = qaplet_owner(runtime);
  if (code) return qaplet_report(diagnostic, code);
  pthread_mutex_lock(&runtime->mutex);
  if (!runtime->m2_enabled) code = QAPLET_UNSUPPORTED;
  else if (runtime->snapshot.lifecycle != QAPLET_OPEN || runtime->loop_closed) code = QAPLET_BAD_STATE;
  else {
    code = QAPLET_RESOURCE_LIMIT;
    for (uint64_t i = 0; i < runtime->m2_limits.keepalive_tokens; ++i) {
      qaplet_keepalive_slot *slot = &runtime->keepalives[i];
      if (slot->active || slot->generation == UINT64_MAX) continue;
      slot->active = 1; ++slot->generation;
      *output = (qaplet_keepalive){runtime->runtime_id, i, slot->generation};
      if (++runtime->keepalive_tokens > runtime->keepalive_tokens_peak) runtime->keepalive_tokens_peak = runtime->keepalive_tokens;
      code = QAPLET_OK;
      break;
    }
  }
  pthread_mutex_unlock(&runtime->mutex);
  return qaplet_report(diagnostic, code);
}
qaplet_code qaplet_keepalive_release(qaplet_runtime *runtime, const qaplet_keepalive *token,
                                    qaplet_diagnostic *diagnostic) {
  qaplet_code code = qaplet_check_diagnostic(diagnostic);
  if (code) return code;
  if (!runtime || !token) return qaplet_report(diagnostic, QAPLET_INVALID_ARGUMENT);
  qaplet_notification notification = {0};
  pthread_mutex_lock(&runtime->mutex);
  if (!runtime->m2_enabled) code = QAPLET_UNSUPPORTED;
  else if (runtime->loop_closed || token->runtime_id != runtime->runtime_id ||
           token->slot >= runtime->m2_limits.keepalive_tokens || !token->generation ||
           !runtime->keepalives[token->slot].active ||
           runtime->keepalives[token->slot].generation != token->generation) code = QAPLET_INVALID_HANDLE;
  else {
    runtime->keepalives[token->slot].active = 0;
    --runtime->keepalive_tokens;
    qaplet_wake_locked(runtime);
    notification = qaplet_notify_prepare_locked(runtime);
  }
  pthread_mutex_unlock(&runtime->mutex);
  qaplet_notify_dispatch(runtime, notification);
  return qaplet_report(diagnostic, code);
}
qaplet_code qaplet_wake(qaplet_runtime *runtime, qaplet_diagnostic *diagnostic) {
  qaplet_code code = qaplet_check_diagnostic(diagnostic);
  if (code) return code;
  if (!runtime) return qaplet_report(diagnostic, QAPLET_INVALID_ARGUMENT);
  qaplet_notification notification = {0};
  pthread_mutex_lock(&runtime->mutex);
  if (!runtime->m2_enabled) code = QAPLET_UNSUPPORTED;
  else { qaplet_wake_locked(runtime); notification = qaplet_notify_prepare_locked(runtime); }
  pthread_mutex_unlock(&runtime->mutex);
  qaplet_notify_dispatch(runtime, notification);
  return qaplet_report(diagnostic, code);
}

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
  return qaplet_create_internal(base, config, output, diagnostic, NULL);
}

qaplet_code qaplet_create_m2_with_diagnostic(const qaplet_config *base, const qaplet_m2_config *config,
                                          qaplet_runtime **output, qaplet_m2_diagnostic *detail,
                                          qaplet_diagnostic *diagnostic) {
  if (output) *output = NULL;
  qaplet_code code = qaplet_check_diagnostic(diagnostic);
  if (code) return code;
  code = qaplet_check_structure(detail, sizeof(*detail));
  if (code) return qaplet_report(diagnostic, code);
  qaplet_m2_diagnostic value = QAPLET_M2_DIAGNOSTIC_INIT;
  value.native_state = QAPLET_NATIVE_QUIESCENT;
  code = config ? qaplet_create_internal(base, config, output, diagnostic, &value) :
    qaplet_report(diagnostic, QAPLET_INVALID_ARGUMENT);
  value.primary_reason = code;
  value.creation_rollback = !code ? QAPLET_ROLLBACK_NONE :
    value.native_uncertain_handles ? QAPLET_ROLLBACK_INCOMPLETE : QAPLET_ROLLBACK_COMPLETE;
  if (!code) value.native_state = QAPLET_NATIVE_RUNNING;
  memcpy((char *)detail + 8, (char *)&value + 8, sizeof(value) - 8);
  return code;
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
  value.capabilities = runtime->m2_enabled ? abi.capabilities : abi.capabilities &
    ~((uint64_t)QAPLET_CAP_M2_RUNTIME | QAPLET_CAP_M2_WAIT | QAPLET_CAP_M2_TIMERS | QAPLET_CAP_M2_WORK | QAPLET_CAP_HOST_NOTIFY | QAPLET_CAP_PROCESS | QAPLET_CAP_PROCESS_IO | QAPLET_CAP_STDIO);
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
  value.native_handles = runtime->native_handles; value.native_handles_peak = runtime->native_handles_peak;
  value.reactor_registrations = runtime->reactor_registrations;
  value.reactor_registrations_peak = runtime->reactor_registrations_peak;
  value.keepalive_tokens = runtime->keepalive_tokens; value.keepalive_tokens_peak = runtime->keepalive_tokens_peak;
  value.wake_requests = runtime->wake_requests; value.wake_signals = runtime->wake_signals;
  value.wait_calls = runtime->wait_calls;
  value.control_error_domain = runtime->control_error ? 1 : 0; value.control_error = runtime->control_error;
  value.native_uncertain_handles = runtime->native_uncertain_handles;
  pthread_mutex_unlock(&runtime->mutex);
  memcpy((char *)output + 8, (char *)&value + 8, sizeof(value) - 8);
  return qaplet_report(diagnostic, QAPLET_OK);
}

qaplet_code qaplet_get_m2_diagnostic(qaplet_runtime *runtime, qaplet_m2_diagnostic *output,
                                    qaplet_diagnostic *diagnostic) {
  qaplet_code code = qaplet_check_diagnostic(diagnostic);
  if (code) return code;
  code = qaplet_check_structure(output, sizeof(*output));
  if (!code && !runtime) code = QAPLET_INVALID_ARGUMENT;
  if (code) return qaplet_report(diagnostic, code);
  qaplet_m2_diagnostic value = QAPLET_M2_DIAGNOSTIC_INIT;
  pthread_mutex_lock(&runtime->mutex);
  if (!runtime->m2_enabled) code = QAPLET_UNSUPPORTED;
  else {
    value.primary_reason = runtime->snapshot.termination_reason;
    value.primary_error_domain = runtime->primary_error ? QAPLET_ERROR_DOMAIN_ERRNO : QAPLET_ERROR_DOMAIN_NONE;
    value.primary_error = runtime->primary_error;
    value.control_error_domain = runtime->control_error ? QAPLET_ERROR_DOMAIN_ERRNO : QAPLET_ERROR_DOMAIN_NONE;
    value.control_error = runtime->control_error;
    value.cleanup_reason = runtime->cleanup_reason;
    value.cleanup_error_domain = runtime->cleanup_error ? (runtime->process_cleanup_error_domain ? runtime->process_cleanup_error_domain : QAPLET_ERROR_DOMAIN_ERRNO) : QAPLET_ERROR_DOMAIN_NONE;
    value.cleanup_error = runtime->cleanup_error;
    value.native_state = runtime->native_state;
    value.native_uncertain_handles = runtime->native_uncertain_handles;
  }
  pthread_mutex_unlock(&runtime->mutex);
  if (!code) memcpy((char *)output + 8, (char *)&value + 8, sizeof(value) - 8);
  return qaplet_report(diagnostic, code);
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

static qaplet_code drive(qaplet_runtime *runtime, uint64_t wait, int continuous, int entry_only,
                         qaplet_drive_result *output, qaplet_diagnostic *diagnostic) {
  qaplet_code code = drive_preflight(runtime, wait, output, diagnostic);
  if (code) return qaplet_report(diagnostic, code);
  qaplet_lifecycle state = qaplet_get_lifecycle(runtime);
  if (state == QAPLET_CLOSED)
    return qaplet_report(diagnostic, QAPLET_BAD_STATE);
  qaplet_driver_set(runtime, 1);
  qaplet_progress progress = QAPLET_PROGRESS_INIT;
  unsigned progressed = 0;
  uint64_t remaining = wait * 1000000;
  qaplet_drive_reason reason = QAPLET_DRIVE_IDLE;
  for (;;) {
    code = qaplet_pump_ready(runtime, &progress, diagnostic, entry_only ? 2 : continuous);
    progressed |= progress.progressed;
    if (code) { if(qaplet_get_lifecycle(runtime)!=QAPLET_OPEN)qaplet_process_shutdown(runtime); reason = code == QAPLET_EXECUTION_STOPPED ? QAPLET_DRIVE_STOPPED : QAPLET_DRIVE_ERROR; break; }
    if (entry_only && progress.phase == QAPLET_PHASE_DONE) { reason = QAPLET_DRIVE_PROGRESS; break; }
    if (!continuous && progress.progressed) { reason = QAPLET_DRIVE_PROGRESS; break; }
    if (JS_IsJobPending(runtime->engine) || qaplet_events_pending(runtime)) continue;
    pthread_mutex_lock(&runtime->mutex);
    /* Drain and clear the coalescing flag under the same acceptance lock used
     * by every publisher. Recheck authoritative state before unlocking to poll.
     * A publisher after this unlock leaves a byte for that poll. */
    if (!runtime->loop_closed) {
      char bytes[128]; ssize_t got;
      do { got = recv(runtime->wake_fd[0], bytes, sizeof(bytes), 0); }
      while (got > 0 || (got < 0 && errno == EINTR));
      if (!got || (got < 0 && errno != EAGAIN && errno != EWOULDBLOCK))
        qaplet_control_failure_locked(runtime, got < 0 ? errno : EIO);
      runtime->wake_pending = 0;
    }
    if (runtime->snapshot.termination_reason || runtime->snapshot.queue_depth) {
      pthread_mutex_unlock(&runtime->mutex); continue;
    }
    if (!runtime->keepalive_tokens && !qaplet_work_referenced_locked(runtime) && !qaplet_process_referenced_locked(runtime) && !qaplet_timers_ref_locked(runtime) && !qaplet_io_referenced_locked(runtime)) {
      reason = progress.phase == QAPLET_PHASE_PENDING ? QAPLET_DRIVE_UNSETTLED_ENTRY : QAPLET_DRIVE_IDLE;
      pthread_mutex_unlock(&runtime->mutex); break;
    }
    if (qaplet_work_ready_locked(runtime) || qaplet_process_pending_locked(runtime) || qaplet_io_ready_locked(runtime) || qaplet_io_issuable_locked(runtime)) { pthread_mutex_unlock(&runtime->mutex); continue; }
    if (!continuous && !remaining) {
      reason = QAPLET_DRIVE_WAIT_TIMEOUT;
      pthread_mutex_unlock(&runtime->mutex); break;
    }
    uint64_t before, duration = continuous ? UINT64_MAX : remaining;
    if (!qaplet_clock_locked(runtime, &before)) { pthread_mutex_unlock(&runtime->mutex); continue; }
    uint64_t timer_deadline;
    if (qaplet_timers_deadline_locked(runtime, &timer_deadline)) {
      if (timer_deadline <= before) { pthread_mutex_unlock(&runtime->mutex); continue; }
      if (duration > timer_deadline - before) duration = timer_deadline - before;
    }
    if (runtime->execution_active) {
      if (before >= runtime->execution_deadline_ns) { pthread_mutex_unlock(&runtime->mutex); continue; }
      uint64_t until_deadline = runtime->execution_deadline_ns - before;
      if (duration > until_deadline) duration = until_deadline;
    }
    int timeout = duration == UINT64_MAX ? -1 : duration / 1000000 >= INT_MAX ? INT_MAX :
      (int)(duration / 1000000 + (duration % 1000000 != 0));
    struct pollfd fd = {runtime->wake_fd[0], POLLIN, 0};
    nfds_t fd_count=0;struct pollfd *fds=qaplet_io_poll_locked(runtime,&fd_count);
    if(!fds){fds=&fd;fd_count=1;}
    increment(&runtime->wait_calls);
    pthread_mutex_unlock(&runtime->mutex);
    int ready = poll(fds, fd_count, timeout), error = errno;
    pthread_mutex_lock(&runtime->mutex);
    uint64_t after;
    if (qaplet_clock_locked(runtime, &after) && !continuous) {
      uint64_t elapsed = after - before;
      remaining = elapsed >= remaining ? 0 : remaining - elapsed;
    }
    if ((ready < 0 && error != EINTR) || (ready > 0 && (fds[0].revents & (POLLERR | POLLHUP | POLLNVAL))))
      qaplet_control_failure_locked(runtime, ready < 0 ? error : EIO);
    pthread_mutex_unlock(&runtime->mutex);
    /* Even at timeout, observe accepted work and the execution deadline before
     * reporting a normal wait timeout. No user execution budget is reset. */
  }
  output->phase = progress.phase;
  output->progressed = progressed;
  output->code = code;
  output->reason = reason;
  qaplet_driver_set(runtime, 0);
  return code;
}

qaplet_code qaplet_run_once(qaplet_runtime *runtime, uint64_t wait,
                           qaplet_drive_result *output, qaplet_diagnostic *diagnostic) {
  return drive(runtime, wait, 0, 0, output, diagnostic);
}
qaplet_code qaplet_run(qaplet_runtime *runtime, qaplet_drive_result *output, qaplet_diagnostic *diagnostic) {
  return drive(runtime, 0, 1, 0, output, diagnostic);
}
qaplet_code qaplet_drive_entry(qaplet_runtime *runtime, qaplet_drive_result *output, qaplet_diagnostic *diagnostic) {
  return drive(runtime, 0, 1, 1, output, diagnostic);
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
  qaplet_notification notification = qaplet_notify_prepare_locked(runtime);
  if (!runtime->native_retained && !runtime->notify_inflight && !runtime->native_handles && !runtime->native_uncertain_handles && runtime->loop_closed)
    runtime->native_state = QAPLET_NATIVE_QUIESCENT;
  pthread_cond_broadcast(&runtime->native_condition);
  pthread_mutex_unlock(&runtime->mutex);
  qaplet_notify_dispatch(runtime, notification);
}

qaplet_code qaplet_quiesce(qaplet_runtime *runtime, uint64_t wait,
                          qaplet_drive_result *output, qaplet_diagnostic *diagnostic) {
  qaplet_code code = drive_preflight(runtime, wait, output, diagnostic);
  if (!code && qaplet_get_lifecycle(runtime) != QAPLET_CLOSED) code = QAPLET_BAD_STATE;
  if (code) return qaplet_report(diagnostic, code);
  qaplet_driver_set(runtime, 1);
  qaplet_loop_shutdown(runtime);
  qaplet_process_close(runtime);
  struct timespec deadline;
  pthread_mutex_lock(&runtime->mutex);
  if (!code && wait) {
    uint64_t now;
    if (qaplet_clock_locked(runtime, &now)) {
      if (wait * 1000000 > UINT64_MAX - now) code = QAPLET_INVALID_ARGUMENT;
      else { now += wait * 1000000; deadline.tv_sec = now / 1000000000; deadline.tv_nsec = now % 1000000000; }
    }
  }
  if (code) { pthread_mutex_unlock(&runtime->mutex); qaplet_driver_set(runtime, 0); return qaplet_report(diagnostic, code); }
  pthread_mutex_unlock(&runtime->mutex);
  qaplet_process_collect(runtime);
  pthread_mutex_lock(&runtime->mutex);
  qaplet_work_reap_locked(runtime);
  int pending = runtime->native_state != QAPLET_NATIVE_QUIESCENT;
  while (pending && wait && !runtime->cleanup_reason) {
    int error = pthread_cond_timedwait(&runtime->native_condition, &runtime->mutex, &deadline);
    pthread_mutex_unlock(&runtime->mutex);
    qaplet_process_collect(runtime);
    pthread_mutex_lock(&runtime->mutex);
    qaplet_work_reap_locked(runtime);
    pending = runtime->native_state != QAPLET_NATIVE_QUIESCENT;
    if (error == ETIMEDOUT) break;
    if (error) {
      cleanup_failure_locked(runtime, error);
      break;
    }
    uint64_t observed;
    qaplet_clock_locked(runtime, &observed);
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
