/* SPDX-License-Identifier: MIT */
/* Controlled SYSTEM-boundary fixture. Link supplied SDK with --wrap; never
 * includes private headers or injects by SDK allocation/call ordinal. */
#include "qaplet.h"
#include <pthread.h>
#include <time.h>
#include <errno.h>
#include <stdio.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <netinet/in.h>
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "controlled M2 contract line %d: %s\n", __LINE__, #c); return 1; } } while (0)
static int failure, mutexes, attributes, conditions, observe, observed, invalid_reentry;
static qaplet_runtime *watched;
static int inject_poll, live_sockets;
static int creation_listener = -1, listener_close_attempts;
static qaplet_keepalive held;
static int sockets[32];
static pthread_mutex_t notify_gate_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t notify_gate_condition = PTHREAD_COND_INITIALIZER;
static int notify_entered, notify_continue, notify_fds[2], notify_mode;
static _Thread_local int stall_notify;
int __real_socket(int, int, int);
int __real_accept(int, struct sockaddr *, socklen_t *);
int __real_close(int);
int __real_poll(struct pollfd *, nfds_t, int);
ssize_t __real_send(int, const void *, size_t, int);
ssize_t __real_recv(int, void *, size_t, int);
static int remember(int fd) {
  if (fd >= 0) { for (int i = 0; i < 32; ++i) if (!sockets[i]) { sockets[i] = fd + 1; ++live_sockets; break; } }
  return fd;
}
int __wrap_socket(int family, int type, int protocol) {
  if (failure == 10) { errno = EMFILE; return -1; }
  return remember(__real_socket(family, type, protocol));
}
int __wrap_accept(int fd, struct sockaddr *address, socklen_t *length) {
  if (failure >= 14 && failure <= 16) creation_listener = fd;
  if (failure == 11 || failure == 15) { errno = EMFILE; return -1; }
  return remember(__real_accept(fd, address, length));
}
int __wrap_close(int fd) {
  if (failure >= 14 && failure <= 16 && fd == creation_listener) {
    ++listener_close_attempts;
    if (failure == 16) {
      /* The wrapper alone knows the original identity was released. The
       * replacement is a fixture-owned distinct resource at the same number. */
      if (__real_close(fd)) return -1;
      int replacement = __real_socket(AF_INET, SOCK_STREAM, 0);
      if (replacement != fd) {
        if (replacement < 0 || dup2(replacement, fd) != fd) invalid_reentry = 1;
        if (replacement >= 0) __real_close(replacement);
      }
    }
    errno = failure == 14 ? EIO : failure == 15 ? EACCES : EINTR;
    return -1;
  }
  if (failure == 12) for (int i = 0; i < 32; ++i) if (sockets[i] == fd + 1) { errno = EIO; return -1; }
  if (failure == 13) for (int i = 0; i < 32; ++i) if (sockets[i] == fd + 1) {
    /* Actually release the old socket, then create a distinct resource using
     * the same numeric fd before reporting the old close's error. */
    if (__real_close(fd)) return -1;
    int replacement = __real_socket(AF_INET, SOCK_STREAM, 0);
    if (replacement != fd) {
      if (replacement < 0 || dup2(replacement, fd) != fd) invalid_reentry = 1;
      if (replacement >= 0) __real_close(replacement);
    }
    errno = EINTR;
    return -1;
  }
  int rc = __real_close(fd);
  if (!rc) for (int i = 0; i < 32; ++i) if (sockets[i] == fd + 1) { sockets[i] = 0; --live_sockets; break; }
  return rc;
}
ssize_t __wrap_send(int fd, const void *data, size_t size, int flags) {
  if (stall_notify && size == 1 && *(const char *)data == 'N') {
    /* Pause at the Host's notification syscall, not inside SDK or Host code.
     * Normal Host hooks have no gate and never block; this recipe is distinct. */
    pthread_mutex_lock(&notify_gate_mutex);
    notify_entered = 1;
    pthread_cond_broadcast(&notify_gate_condition);
    while (!notify_continue) pthread_cond_wait(&notify_gate_condition, &notify_gate_mutex);
    pthread_mutex_unlock(&notify_gate_mutex);
  }
  if (failure == 5) { errno = EIO; return -1; }
  return __real_send(fd, data, size, flags);
}
ssize_t __wrap_recv(int fd, void *data, size_t size, int flags) {
  if (failure == 7) { errno = EIO; return -1; }
  return __real_recv(fd, data, size, flags);
}
int __wrap_poll(struct pollfd *fds, nfds_t count, int timeout) {
  if (failure == 6) { errno = EIO; return -1; }
  if (inject_poll && watched) {
    inject_poll = 0;
    qaplet_drive_result drive = QAPLET_DRIVE_RESULT_INIT;
    qaplet_progress progress = QAPLET_PROGRESS_INIT;
    qaplet_keepalive refused = QAPLET_KEEPALIVE_INIT;
    qaplet_m2_snapshot snapshot = QAPLET_M2_SNAPSHOT_INIT;
    /* System-boundary interposition chooses the exact post-check/pre-sleep
     * order, but all SDK observations still use its public contract. */
    if (qaplet_run_once(watched, 0, &drive, NULL) != QAPLET_BUSY ||
        qaplet_run(watched, &drive, NULL) != QAPLET_BUSY ||
        qaplet_pump(watched, &progress, NULL) != QAPLET_BUSY ||
        qaplet_eval(watched, "1", 1, &progress, NULL) != QAPLET_BUSY ||
        qaplet_keepalive_create(watched, &refused, NULL) != QAPLET_BUSY ||
        qaplet_close(watched, NULL) != QAPLET_BUSY || qaplet_destroy(watched, NULL) != QAPLET_BUSY ||
        qaplet_get_m2_snapshot(watched, &snapshot, NULL) != QAPLET_OK || !snapshot.driving ||
        qaplet_post_event(watched, 42, 7, "x", 1, NULL) != QAPLET_OK ||
        qaplet_keepalive_release(watched, &held, NULL) != QAPLET_OK) invalid_reentry = 1;
  }
  return __real_poll(fds, count, timeout);
}
int __real_pthread_mutex_init(pthread_mutex_t *, const pthread_mutexattr_t *);
int __real_pthread_mutex_destroy(pthread_mutex_t *);
int __real_pthread_condattr_init(pthread_condattr_t *);
int __real_pthread_condattr_destroy(pthread_condattr_t *);
int __real_pthread_condattr_setclock(pthread_condattr_t *, clockid_t);
int __real_pthread_cond_init(pthread_cond_t *, const pthread_condattr_t *);
int __real_pthread_cond_destroy(pthread_cond_t *);
int __real_clock_gettime(clockid_t, struct timespec *);
int __wrap_pthread_mutex_init(pthread_mutex_t *mutex, const pthread_mutexattr_t *attr) {
  if (failure == 1) return ENOMEM;
  int error = __real_pthread_mutex_init(mutex, attr); if (!error) ++mutexes; return error;
}
int __wrap_pthread_mutex_destroy(pthread_mutex_t *mutex) {
  int error = __real_pthread_mutex_destroy(mutex); if (!error) --mutexes; return error;
}
int __wrap_pthread_condattr_init(pthread_condattr_t *attr) {
  if (failure == 2) return ENOMEM;
  int error = __real_pthread_condattr_init(attr); if (!error) ++attributes; return error;
}
int __wrap_pthread_condattr_destroy(pthread_condattr_t *attr) {
  int error = __real_pthread_condattr_destroy(attr); if (!error) --attributes; return error;
}
int __wrap_pthread_condattr_setclock(pthread_condattr_t *attr, clockid_t clock) {
  return failure == 3 ? EINVAL : __real_pthread_condattr_setclock(attr, clock);
}
int __wrap_pthread_cond_init(pthread_cond_t *condition, const pthread_condattr_t *attr) {
  if (failure == 4) return ENOMEM;
  int error = __real_pthread_cond_init(condition, attr); if (!error) ++conditions; return error;
}
int __wrap_pthread_cond_destroy(pthread_cond_t *condition) {
  int error = __real_pthread_cond_destroy(condition); if (!error) --conditions; return error;
}
int __wrap_clock_gettime(clockid_t clock, struct timespec *now) {
  if (failure == 8) { errno = EIO; return -1; }
  if (failure == 9) { now->tv_sec = 0; now->tv_nsec = 0; return 0; }
  if (observe && watched) {
    int mode = observe; observe = 0;
    qaplet_drive_result result = QAPLET_DRIVE_RESULT_INIT;
    qaplet_progress progress = QAPLET_PROGRESS_INIT;
    (void)mode;
    if (qaplet_quiesce(watched, 0, &result, NULL) != QAPLET_BUSY ||
        qaplet_run_once(watched, 0, &result, NULL) != QAPLET_BUSY ||
        qaplet_pump(watched, &progress, NULL) != QAPLET_BUSY ||
        qaplet_close(watched, NULL) != QAPLET_BUSY)
      invalid_reentry = 1;
    ++observed;
  }
  return __real_clock_gettime(clock, now);
}
static void ignored_notify(void *unused) { (void)unused; }
static int setup(void) {
  qaplet_config base = QAPLET_CONFIG_INIT;
  qaplet_m2_config config = QAPLET_M2_CONFIG_INIT;
  qaplet_progress progress = QAPLET_PROGRESS_INIT;
  CHECK(qaplet_create_m2(&base, &config, &watched, NULL) == QAPLET_OK);
  qaplet_host_notify hook = QAPLET_HOST_NOTIFY_INIT;
  hook.function = ignored_notify;
  CHECK(qaplet_set_host_notify(watched, &hook, NULL) == QAPLET_OK);
  const char source[] = "globalThis.seen=0;import('qaplet:events').then(m=>m.onEvent(e=>{seen++;}))";
  CHECK(qaplet_eval(watched, source, sizeof(source)-1, &progress, NULL) == QAPLET_OK);
  while (progress.phase == QAPLET_PHASE_PENDING) CHECK(qaplet_pump(watched, &progress, NULL) == QAPLET_OK);
  CHECK(qaplet_keepalive_create(watched, &held, NULL) == QAPLET_OK);
  return 0;
}
static int dispose(void) {
  failure = 0;
  qaplet_drive_result drive = QAPLET_DRIVE_RESULT_INIT;
  CHECK(qaplet_close(watched, NULL) == QAPLET_OK);
  CHECK(qaplet_quiesce(watched, 10, &drive, NULL) == QAPLET_OK && drive.reason == QAPLET_DRIVE_QUIESCENT);
  CHECK(qaplet_destroy(watched, NULL) == QAPLET_OK);
  watched = NULL;
  CHECK(!live_sockets && !mutexes && !attributes && !conditions);
  return 0;
}
static int creation_diagnostics(void) {
  qaplet_config base = QAPLET_CONFIG_INIT;
  qaplet_m2_config config = QAPLET_M2_CONFIG_INIT;
  qaplet_runtime *runtime = (qaplet_runtime *)1;
  qaplet_m2_diagnostic detail = QAPLET_M2_DIAGNOSTIC_INIT;
  qaplet_diagnostic text = QAPLET_DIAGNOSTIC_INIT;
  for (int stage = 1; stage <= 4; ++stage) {
    failure = stage;
    CHECK(qaplet_create_m2_with_diagnostic(&base, &config, &runtime, &detail, NULL) == QAPLET_MEMORY_LIMIT && !runtime);
    CHECK(detail.primary_reason == QAPLET_MEMORY_LIMIT && detail.primary_error_domain == QAPLET_ERROR_DOMAIN_ERRNO && detail.primary_error == (stage == 3 ? EINVAL : ENOMEM));
    CHECK(detail.creation_rollback == QAPLET_ROLLBACK_COMPLETE && !detail.cleanup_reason && !detail.cleanup_error_domain && !detail.cleanup_error && !detail.native_uncertain_handles);
    CHECK(!live_sockets && !mutexes && !attributes && !conditions);
  }
  /* accept refuses before publication; every acquired socket can close. */
  failure = 11;
  CHECK(qaplet_create_m2_with_diagnostic(&base, &config, &runtime, &detail, &text) == QAPLET_CONTROL_FAILURE && !runtime);
  CHECK(detail.primary_reason == QAPLET_CONTROL_FAILURE && detail.primary_error_domain == QAPLET_ERROR_DOMAIN_ERRNO && detail.primary_error == EMFILE);
  CHECK(detail.creation_rollback == QAPLET_ROLLBACK_COMPLETE && detail.native_state == QAPLET_NATIVE_QUIESCENT && !detail.cleanup_reason && !detail.native_uncertain_handles);
  CHECK(text.code == QAPLET_CONTROL_FAILURE && text.length > 0 && (text.flags & QAPLET_DIAGNOSTIC_TRUNCATED));
  CHECK(!live_sockets && !mutexes && !attributes && !conditions);
  /* The listener's close is explicitly refused, with the original socket
   * still alive. Failed publication must expose the uncompleted rollback. */
  for (int kind = 14; kind <= 16; ++kind) {
    failure = kind; listener_close_attempts = 0;
    int primary = kind == 14 ? EIO : kind == 15 ? EMFILE : EINTR;
    int cleanup = kind == 14 ? EIO : kind == 15 ? EACCES : EINTR;
    CHECK(qaplet_create_m2_with_diagnostic(&base, &config, &runtime, &detail, NULL) == QAPLET_CONTROL_FAILURE && !runtime);
    CHECK(detail.primary_reason == QAPLET_CONTROL_FAILURE && detail.primary_error_domain == QAPLET_ERROR_DOMAIN_ERRNO && detail.primary_error == primary);
    CHECK(detail.cleanup_reason == QAPLET_CONTROL_FAILURE && detail.cleanup_error_domain == QAPLET_ERROR_DOMAIN_ERRNO && detail.cleanup_error == cleanup);
    CHECK(detail.creation_rollback == QAPLET_ROLLBACK_INCOMPLETE && detail.native_state == QAPLET_NATIVE_CLEANING && detail.native_uncertain_handles == 1);
    CHECK(live_sockets == 1 && listener_close_attempts == 1 && !mutexes && !attributes && !conditions && !invalid_reentry);
    failure = 0;
    /* This fixture knows the refused original or replacement identity. Its
     * own cleanup cannot change the SDK's INCOMPLETE result into COMPLETE. */
    CHECK(__wrap_close(creation_listener) == 0);
    CHECK(!live_sockets && detail.creation_rollback == QAPLET_ROLLBACK_INCOMPLETE);
  }
  failure = 0;
  return 0;
}
static int wait_faults(void) {
  qaplet_drive_result drive = QAPLET_DRIVE_RESULT_INIT;
  qaplet_m2_snapshot snapshot = QAPLET_M2_SNAPSHOT_INIT;
  qaplet_m2_diagnostic detail = QAPLET_M2_DIAGNOSTIC_INIT;
  CHECK(!setup());
  inject_poll = 1;
  CHECK(qaplet_run(watched, &drive, NULL) == QAPLET_OK && drive.progressed && drive.reason == QAPLET_DRIVE_IDLE && !invalid_reentry);
  CHECK(!dispose());
  for (int kind = 5; kind <= 9; ++kind) {
    CHECK(!setup());
    failure = kind;
    if (kind == 5) {
      CHECK(qaplet_post_event(watched, 1, 0, "x", 1, NULL) == QAPLET_OK);
      CHECK(qaplet_get_m2_snapshot(watched, &snapshot, NULL) == QAPLET_OK && snapshot.control_error_domain == 1 && snapshot.control_error == EIO);
    }
    qaplet_code driven = qaplet_run_once(watched, 40, &drive, NULL);
    if (driven != QAPLET_CONTROL_FAILURE || drive.reason != QAPLET_DRIVE_ERROR)
      fprintf(stderr, "system fault %d: code=%d reason=%d\n", kind, driven, drive.reason);
    CHECK(driven == QAPLET_CONTROL_FAILURE && drive.reason == QAPLET_DRIVE_ERROR);
    failure = 0;
    CHECK(qaplet_get_m2_snapshot(watched, &snapshot, NULL) == QAPLET_OK && snapshot.lifecycle == QAPLET_FAILED && snapshot.termination_reason == QAPLET_CONTROL_FAILURE && !snapshot.native_handles);
    CHECK(qaplet_request_stop(watched, NULL) == QAPLET_OK);
    CHECK(!dispose());
  }
  CHECK(!setup()); failure = 5;
  CHECK(qaplet_keepalive_release(watched, &held, NULL) == QAPLET_OK);
  CHECK(qaplet_get_m2_snapshot(watched, &snapshot, NULL) == QAPLET_OK && snapshot.termination_reason == QAPLET_CONTROL_FAILURE && !snapshot.keepalive_tokens);
  CHECK(!dispose());
  CHECK(!setup()); failure = 5;
  CHECK(qaplet_request_stop(watched, NULL) == QAPLET_OK);
  CHECK(qaplet_get_m2_snapshot(watched, &snapshot, NULL) == QAPLET_OK && snapshot.termination_reason == QAPLET_EXECUTION_STOPPED && snapshot.control_error == EIO);
  failure = 0;
  CHECK(qaplet_run(watched, &drive, NULL) == QAPLET_EXECUTION_STOPPED);
  CHECK(!dispose());
  for (int stage = 10; stage <= 11; ++stage) {
    qaplet_config base = QAPLET_CONFIG_INIT;
    qaplet_m2_config config = QAPLET_M2_CONFIG_INIT;
    failure = stage;
    CHECK(qaplet_create_m2(&base, &config, &watched, NULL) == QAPLET_CONTROL_FAILURE && !watched);
    CHECK(!live_sockets && !mutexes && !attributes && !conditions);
    failure = 0;
  }
  CHECK(!setup());
  CHECK(qaplet_close(watched, NULL) == QAPLET_OK);
  failure = 8;
  CHECK(qaplet_quiesce(watched, 10, &drive, NULL) == QAPLET_CONTROL_FAILURE);
  failure = 0;
  CHECK(qaplet_get_m2_snapshot(watched, &snapshot, NULL) == QAPLET_OK && snapshot.lifecycle == QAPLET_CLOSED && snapshot.cleanup_reason == QAPLET_CONTROL_FAILURE);
  CHECK(qaplet_get_m2_diagnostic(watched, &detail, NULL) == QAPLET_OK && !detail.primary_reason && !detail.primary_error && !detail.control_error);
  CHECK(detail.cleanup_reason == QAPLET_CONTROL_FAILURE && detail.cleanup_error_domain == QAPLET_ERROR_DOMAIN_ERRNO && detail.cleanup_error == EIO);
  CHECK(qaplet_destroy(watched, NULL) == QAPLET_OK);
  watched = NULL;
  CHECK(!live_sockets && !mutexes && !attributes && !conditions);
  for (int kind = 12; kind <= 13; ++kind) {
    CHECK(!setup());
    failure = 5;
    CHECK((kind == 12 ? qaplet_wake(watched, NULL) : qaplet_request_stop(watched, NULL)) == QAPLET_OK);
    failure = kind;
    CHECK(qaplet_close(watched, NULL) == QAPLET_CONTROL_FAILURE);
    CHECK(qaplet_get_m2_diagnostic(watched, &detail, NULL) == QAPLET_OK);
    CHECK(detail.primary_reason == (kind == 12 ? QAPLET_CONTROL_FAILURE : QAPLET_EXECUTION_STOPPED));
    CHECK(detail.primary_error == (kind == 12 ? EIO : 0) && detail.primary_error_domain == (kind == 12 ? QAPLET_ERROR_DOMAIN_ERRNO : QAPLET_ERROR_DOMAIN_NONE));
    CHECK(detail.control_error_domain == QAPLET_ERROR_DOMAIN_ERRNO && detail.control_error == EIO);
    CHECK(detail.cleanup_reason == QAPLET_CONTROL_FAILURE && detail.cleanup_error_domain == QAPLET_ERROR_DOMAIN_ERRNO && detail.cleanup_error == (kind == 12 ? EIO : EINTR));
    CHECK(detail.native_uncertain_handles == 2 && detail.creation_rollback == QAPLET_ROLLBACK_NONE && detail.native_state == QAPLET_NATIVE_CLEANING);
    CHECK(qaplet_get_m2_snapshot(watched, &snapshot, NULL) == QAPLET_OK && snapshot.lifecycle == QAPLET_CLOSED && snapshot.native_state == QAPLET_NATIVE_CLEANING && !snapshot.native_handles && snapshot.native_uncertain_handles == 2 && !snapshot.keepalive_tokens);
    CHECK(qaplet_quiesce(watched, 0, &drive, NULL) == QAPLET_CONTROL_FAILURE && drive.reason == QAPLET_DRIVE_WAIT_TIMEOUT);
    CHECK(qaplet_destroy(watched, NULL) == QAPLET_BUSY);
    failure = 0;
    CHECK(qaplet_quiesce(watched, 10, &drive, NULL) == QAPLET_CONTROL_FAILURE && drive.reason == QAPLET_DRIVE_WAIT_TIMEOUT);
    CHECK(qaplet_destroy(watched, NULL) == QAPLET_BUSY && !invalid_reentry);
    failure = 8;
    CHECK(qaplet_quiesce(watched, 10, &drive, NULL) == QAPLET_CONTROL_FAILURE);
    failure = 0;
    CHECK(qaplet_get_m2_diagnostic(watched, &detail, NULL) == QAPLET_OK && detail.primary_reason == (kind == 12 ? QAPLET_CONTROL_FAILURE : QAPLET_EXECUTION_STOPPED) && detail.cleanup_error == (kind == 12 ? EIO : EINTR));
    /* Fixture owns its replacements and tears them down itself. SDK cleanup
     * remains unproven; retained CLOSED controls intentionally live to process
     * exit. This negative case never claims native quiescence or destroy. */
    for (int i = 0; i < 32; ++i) if (sockets[i]) {
      CHECK(fcntl(sockets[i]-1, F_GETFD) >= 0);
      CHECK(__wrap_close(sockets[i]-1) == 0);
    }
    watched = NULL;
    CHECK(!live_sockets && !attributes && mutexes == kind-11 && conditions == kind-11);
  }
  return 0;
}
static void controlled_notify(void *userdata) {
  int fd = *(int *)userdata;
  if (send(fd, "N", 1, MSG_NOSIGNAL) != 1) invalid_reentry = 1;
}
static void *overlapping_notification(void *unused) {
  (void)unused;
  stall_notify = 1;
  qaplet_code code = notify_mode ? qaplet_wake(watched, NULL) : qaplet_post_event(watched, 1, 1, "x", 1, NULL);
  if (code != QAPLET_OK) invalid_reentry = 1;
  return NULL;
}
static int notify_channel(void) {
  struct sockaddr_in address = {0}; socklen_t length = sizeof(address);
  address.sin_family = AF_INET; address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  int listener = socket(AF_INET, SOCK_STREAM, 0);
  CHECK(listener >= 0 && !bind(listener, (void *)&address, length));
  CHECK(!getsockname(listener, (void *)&address, &length) && !listen(listener, 1));
  notify_fds[1] = socket(AF_INET, SOCK_STREAM, 0);
  CHECK(notify_fds[1] >= 0 && !connect(notify_fds[1], (void *)&address, length));
  notify_fds[0] = accept(listener, NULL, NULL);
  CHECK(notify_fds[0] >= 0 && !close(listener));
  int flags = fcntl(notify_fds[1], F_GETFL);
  CHECK(flags >= 0 && !fcntl(notify_fds[1], F_SETFL, flags | O_NONBLOCK));
  return 0;
}
static int notification_overlap(void) {
  for (notify_mode = 0; notify_mode < 2; ++notify_mode) {
    qaplet_config base = QAPLET_CONFIG_INIT;
    qaplet_m2_config config = QAPLET_M2_CONFIG_INIT;
    qaplet_host_notify hook = QAPLET_HOST_NOTIFY_INIT;
    qaplet_notify_snapshot notifications = QAPLET_NOTIFY_SNAPSHOT_INIT;
    qaplet_m2_snapshot snapshot = QAPLET_M2_SNAPSHOT_INIT;
    qaplet_snapshot events = QAPLET_SNAPSHOT_INIT;
    qaplet_progress progress = QAPLET_PROGRESS_INIT;
    qaplet_drive_result drive = QAPLET_DRIVE_RESULT_INIT;
    CHECK(!notify_channel());
    CHECK(qaplet_create_m2(&base, &config, &watched, NULL) == QAPLET_OK);
    hook.function = controlled_notify; hook.userdata = &notify_fds[1];
    CHECK(qaplet_set_host_notify(watched, &hook, NULL) == QAPLET_OK);
    const char source[] = "import('qaplet:events').then(m=>m.onEvent(()=>{throw Error('must not run')}))";
    CHECK(qaplet_eval(watched, source, sizeof(source)-1, &progress, NULL) == QAPLET_OK);
    while (progress.phase == QAPLET_PHASE_PENDING) CHECK(qaplet_pump(watched, &progress, NULL) == QAPLET_OK);
    if (notify_mode) CHECK(qaplet_close(watched, NULL) == QAPLET_OK);
    notify_entered = notify_continue = 0;
    pthread_t producer;
    CHECK(!pthread_create(&producer, NULL, overlapping_notification, NULL));
    pthread_mutex_lock(&notify_gate_mutex);
    while (!notify_entered) pthread_cond_wait(&notify_gate_condition, &notify_gate_mutex);
    pthread_mutex_unlock(&notify_gate_mutex);
    CHECK(qaplet_get_notify_snapshot(watched, &notifications, NULL) == QAPLET_OK && notifications.inflight == 1);
    CHECK(qaplet_close(watched, NULL) == QAPLET_OK);
    CHECK(qaplet_get_m2_snapshot(watched, &snapshot, NULL) == QAPLET_OK && snapshot.lifecycle == QAPLET_CLOSED && snapshot.native_state == QAPLET_NATIVE_CLEANING && !snapshot.engine_current_bytes && !snapshot.native_handles && !snapshot.native_uncertain_handles);
    CHECK(qaplet_quiesce(watched, 0, &drive, NULL) == QAPLET_OK && drive.reason == QAPLET_DRIVE_WAIT_TIMEOUT);
    CHECK(qaplet_quiesce(watched, 1, &drive, NULL) == QAPLET_OK && drive.reason == QAPLET_DRIVE_WAIT_TIMEOUT);
    CHECK(qaplet_destroy(watched, NULL) == QAPLET_BUSY);
    CHECK(qaplet_get_snapshot(watched, &events, NULL) == QAPLET_OK && events.events_consumed == 0 && events.events_dropped == (notify_mode ? 0 : 1));
    pthread_mutex_lock(&notify_gate_mutex);
    notify_continue = 1; pthread_cond_broadcast(&notify_gate_condition);
    pthread_mutex_unlock(&notify_gate_mutex);
    CHECK(!pthread_join(producer, NULL) && !invalid_reentry);
    CHECK(qaplet_get_notify_snapshot(watched, &notifications, NULL) == QAPLET_OK && !notifications.inflight && notifications.inflight_peak >= 1);
    CHECK(qaplet_quiesce(watched, 100, &drive, NULL) == QAPLET_OK && drive.reason == QAPLET_DRIVE_QUIESCENT);
    CHECK(qaplet_destroy(watched, NULL) == QAPLET_OK);
    watched = NULL;
    CHECK(!close(notify_fds[0]) && !close(notify_fds[1]));
    CHECK(!live_sockets && !mutexes && !conditions && !attributes);
  }
  puts("controlled HostNotify: accepted event/close and CLOSED wake overlap; JS released; inflight retained; finite quiesce retry; destroy BUSY; producer joined; drained cleanup: pass");
  return 0;
}
static void forbidden_notify_reentry(void *userdata) {
  qaplet_runtime *runtime = userdata;
  qaplet_abi abi = QAPLET_ABI_INIT;
  qaplet_progress progress = QAPLET_PROGRESS_INIT;
  qaplet_m2_snapshot snapshot = QAPLET_M2_SNAPSHOT_INIT;
  qaplet_notify_snapshot notifications = QAPLET_NOTIFY_SNAPSHOT_INIT;
  qaplet_drive_result drive = QAPLET_DRIVE_RESULT_INIT;
  if (qaplet_query_abi(&abi) != QAPLET_BUSY ||
      qaplet_get_m2_snapshot(runtime, &snapshot, NULL) != QAPLET_BUSY ||
      qaplet_get_notify_snapshot(runtime, &notifications, NULL) != QAPLET_BUSY ||
      qaplet_eval(runtime, "throw Error()", 13, &progress, NULL) != QAPLET_BUSY ||
      qaplet_pump(runtime, &progress, NULL) != QAPLET_BUSY ||
      qaplet_run_once(runtime, 0, &drive, NULL) != QAPLET_BUSY ||
      qaplet_run(runtime, &drive, NULL) != QAPLET_BUSY ||
      qaplet_post_event(runtime, 0, 0, NULL, 0, NULL) != QAPLET_BUSY ||
      qaplet_request_stop(runtime, NULL) != QAPLET_BUSY || qaplet_wake(runtime, NULL) != QAPLET_BUSY ||
      qaplet_close(runtime, NULL) != QAPLET_BUSY || qaplet_quiesce(runtime, 0, &drive, NULL) != QAPLET_BUSY ||
      qaplet_destroy(runtime, NULL) != QAPLET_BUSY) invalid_reentry = 1;
}
static int notification_reentry(void) {
  qaplet_config base = QAPLET_CONFIG_INIT;
  qaplet_m2_config config = QAPLET_M2_CONFIG_INIT;
  qaplet_host_notify hook = QAPLET_HOST_NOTIFY_INIT;
  qaplet_m2_snapshot snapshot = QAPLET_M2_SNAPSHOT_INIT;
  CHECK(qaplet_create_m2(&base, &config, &watched, NULL) == QAPLET_OK);
  hook.function = forbidden_notify_reentry; hook.userdata = watched;
  CHECK(qaplet_set_host_notify(watched, &hook, NULL) == QAPLET_OK);
  /* Deliberately invalid Host code tests refusal, not permitted hook behavior. */
  CHECK(qaplet_wake(watched, NULL) == QAPLET_OK && !invalid_reentry);
  CHECK(qaplet_get_m2_snapshot(watched, &snapshot, NULL) == QAPLET_OK && snapshot.lifecycle == QAPLET_OPEN);
  CHECK(!dispose() && !invalid_reentry);
  puts("controlled HostNotify reentry: deliberately invalid hook receives BUSY for SDK queries/control/drives/close; no JS or stop mutation: pass");
  return 0;
}
int main(void) {
  qaplet_config base = QAPLET_CONFIG_INIT;
  qaplet_m2_config config = QAPLET_M2_CONFIG_INIT;
  CHECK(!creation_diagnostics());
  for (int stage = 1; stage <= 4; ++stage) {
    qaplet_runtime *runtime = NULL;
    failure = stage;
    CHECK(qaplet_create_m2(&base, &config, &runtime, NULL) == QAPLET_MEMORY_LIMIT && !runtime);
    CHECK(!mutexes && !attributes && !conditions);
    failure = 0;
    CHECK(qaplet_create_m2(&base, &config, &runtime, NULL) == QAPLET_OK);
    CHECK(qaplet_close(runtime, NULL) == QAPLET_OK && qaplet_destroy(runtime, NULL) == QAPLET_OK);
    CHECK(!mutexes && !attributes && !conditions);
  }
  CHECK(qaplet_create_m2(&base, &config, &watched, NULL) == QAPLET_OK);
  qaplet_progress progress = QAPLET_PROGRESS_INIT;
  qaplet_drive_result result = QAPLET_DRIVE_RESULT_INIT;
  const char source[] = "new Promise(()=>{})";
  CHECK(qaplet_eval(watched, source, sizeof(source)-1, &progress, NULL) == QAPLET_OK);
  observe = 1;
  CHECK(qaplet_run_once(watched, 0, &result, NULL) == QAPLET_OK && observed == 1 && !invalid_reentry);
  CHECK(qaplet_close(watched, NULL) == QAPLET_OK);
  observe = 2;
  CHECK(qaplet_quiesce(watched, 10, &result, NULL) == QAPLET_OK && observed == 2 && !invalid_reentry);
  CHECK(qaplet_destroy(watched, NULL) == QAPLET_OK);
  CHECK(!mutexes && !attributes && !conditions);
  watched = NULL;
  CHECK(!notification_overlap());
  CHECK(!notification_reentry());
  CHECK(!wait_faults());
  puts("controlled M2: four system initialization refusals; resources balanced; PENDING/CLOSED whole-drive guard: pass");
  puts("controlled M2 wait: pre-sleep handoff; accepted send failure; poll/recv/clock faults; first cause; socket rollback; uncertain close retained; reused fd untouched: pass");
  puts("controlled M2 diagnostics: original setup/cleanup errors; incomplete unpublished rollback; fd reuse; independent published first cause: pass");
  return 0;
}
