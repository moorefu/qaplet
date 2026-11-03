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
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "controlled M2 contract line %d: %s\n", __LINE__, #c); return 1; } } while (0)
static int failure, mutexes, attributes, conditions, observe, observed, invalid_reentry;
static qaplet_runtime *watched;
static int inject_poll, live_sockets;
static qaplet_keepalive held;
static int sockets[32];
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
  if (failure == 11) { errno = EMFILE; return -1; }
  return remember(__real_accept(fd, address, length));
}
int __wrap_close(int fd) {
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
static int setup(void) {
  qaplet_config base = QAPLET_CONFIG_INIT;
  qaplet_m2_config config = QAPLET_M2_CONFIG_INIT;
  qaplet_progress progress = QAPLET_PROGRESS_INIT;
  CHECK(qaplet_create_m2(&base, &config, &watched, NULL) == QAPLET_OK);
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
static int wait_faults(void) {
  qaplet_drive_result drive = QAPLET_DRIVE_RESULT_INIT;
  qaplet_m2_snapshot snapshot = QAPLET_M2_SNAPSHOT_INIT;
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
  CHECK(qaplet_destroy(watched, NULL) == QAPLET_OK);
  watched = NULL;
  CHECK(!live_sockets && !mutexes && !attributes && !conditions);
  for (int kind = 12; kind <= 13; ++kind) {
    CHECK(!setup());
    failure = kind;
    CHECK(qaplet_close(watched, NULL) == QAPLET_CONTROL_FAILURE);
    CHECK(qaplet_get_m2_snapshot(watched, &snapshot, NULL) == QAPLET_OK && snapshot.lifecycle == QAPLET_CLOSED && snapshot.native_state == QAPLET_NATIVE_CLEANING && !snapshot.native_handles && snapshot.native_uncertain_handles == 2 && !snapshot.keepalive_tokens);
    CHECK(qaplet_quiesce(watched, 0, &drive, NULL) == QAPLET_CONTROL_FAILURE && drive.reason == QAPLET_DRIVE_WAIT_TIMEOUT);
    CHECK(qaplet_destroy(watched, NULL) == QAPLET_BUSY);
    failure = 0;
    CHECK(qaplet_quiesce(watched, 10, &drive, NULL) == QAPLET_CONTROL_FAILURE && drive.reason == QAPLET_DRIVE_WAIT_TIMEOUT);
    CHECK(qaplet_destroy(watched, NULL) == QAPLET_BUSY && !invalid_reentry);
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
int main(void) {
  qaplet_config base = QAPLET_CONFIG_INIT;
  qaplet_m2_config config = QAPLET_M2_CONFIG_INIT;
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
  CHECK(!wait_faults());
  puts("controlled M2: four system initialization refusals; resources balanced; PENDING/CLOSED whole-drive guard: pass");
  puts("controlled M2 wait: pre-sleep handoff; accepted send failure; poll/recv/clock faults; first cause; socket rollback; uncertain close retained; reused fd untouched: pass");
  return 0;
}
