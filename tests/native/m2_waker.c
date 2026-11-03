/* SPDX-License-Identifier: MIT */
/* Real OS feasibility probes; this is not a Qaplet Runtime implementation. */
#define _COSMO_SOURCE 1
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/utsname.h>
#include <time.h>
#include <unistd.h>

static int failures, joined, live_helpers, cleanup_ok = 1;
static const char *boolean(int value) { return value ? "true" : "false"; }
static void result(const char *id, bool pass, const char *actual) {
  printf("{\"case_id\":\"%s\",\"status\":\"%s\",\"actual\":%s}\n", id, pass ? "pass" : "fail", actual);
  fflush(stdout);
  failures += !pass;
}
static double monotonic_ms(void) {
  struct timespec ts;
  if (clock_gettime(CLOCK_MONOTONIC, &ts)) return -1;
  return ts.tv_sec * 1000.0 + ts.tv_nsec / 1000000.0;
}
static int closed(int *fd) {
  if (*fd < 0) return 1;
  int old = *fd;
  *fd = -1;
  int value = close(old);
  errno = 0;
  int checked = fcntl(old, F_GETFD);
  int ok = value == 0 && checked == -1 && errno == EBADF;
  cleanup_ok &= ok;
  return ok;
}
static int wait_fd(int fd, short requested, short *observed, int timeout) {
  struct pollfd p = {fd, requested, 0};
  int value = poll(&p, 1, timeout);
  *observed = p.revents;
  return value;
}
static int nonblock(int fd) {
  int flags = fcntl(fd, F_GETFL);
  return flags >= 0 && fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0;
}
/* Use loopback TCP because Windows poll has a distinct socket route. */
static int pair(int fd[2], int *connection, int *connection_error, int *socket_error) {
  int listener = -1;
  short revents = 0;
  struct sockaddr_in address = {0};
  socklen_t length = sizeof(address);
  fd[0] = fd[1] = -1;
  listener = socket(AF_INET, SOCK_STREAM, 0);
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  if (listener < 0 || bind(listener, (void *)&address, length) ||
      getsockname(listener, (void *)&address, &length) || listen(listener, 1)) goto fail;
  fd[1] = socket(AF_INET, SOCK_STREAM, 0);
  if (fd[1] < 0 || !nonblock(fd[1])) goto fail;
  errno = 0;
  *connection = connect(fd[1], (void *)&address, length);
  *connection_error = errno;
  if (*connection && *connection_error != EINPROGRESS && *connection_error != EWOULDBLOCK) goto fail;
  if (wait_fd(fd[1], POLLOUT, &revents, 2000) != 1) goto fail;
  length = sizeof(*socket_error);
  if (getsockopt(fd[1], SOL_SOCKET, SO_ERROR, socket_error, &length) || *socket_error) goto fail;
  if (wait_fd(listener, POLLIN, &revents, 2000) != 1) goto fail;
  fd[0] = accept(listener, 0, 0);
  if (fd[0] < 0 || !nonblock(fd[0])) goto fail;
  return closed(&listener);
fail:
  closed(&listener);
  closed(&fd[0]);
  closed(&fd[1]);
  return 0;
}

struct waker {
  pthread_mutex_t mutex;
  pthread_cond_t condition;
  int fd[2], gate, done, mode, accepted, published, pending, signal_bytes;
  int closing, cancelled, send_result, control_error;
};
static int notify_locked(struct waker *w) {
  if (w->closing) return 0;
  ++w->published;  /* Commit before the fallible signal syscall. */
  if (!w->pending) {
    errno = 0;
    w->send_result = send(w->fd[1], "W", 1, 0);
    if (w->send_result == 1) {
      ++w->signal_bytes;
      w->pending = 1;
    } else {
      w->control_error = errno;
    }
  }
  return 1;
}
static void *publish(void *argument) {
  struct waker *w = argument;
  pthread_mutex_lock(&w->mutex);
  while (!w->gate) pthread_cond_wait(&w->condition, &w->mutex);
  if (w->mode == 2 || w->mode == 4) {
    /* An explicit timed gate, not a scheduling sleep or periodic polling. */
    struct timespec deadline;
    clock_gettime(CLOCK_REALTIME, &deadline);
    deadline.tv_nsec += 100000000;
    if (deadline.tv_nsec >= 1000000000) { ++deadline.tv_sec; deadline.tv_nsec -= 1000000000; }
    int rc = 0;
    while (rc == 0) rc = pthread_cond_timedwait(&w->condition, &w->mutex, &deadline);
    if (rc != ETIMEDOUT) w->control_error = rc;
  }
  if (w->mode == 4) w->cancelled = 1;
  int count = w->mode == 3 ? 1000 : 1;
  for (int i = 0; i < count; ++i) w->accepted += notify_locked(w);
  w->done = 1;
  pthread_cond_broadcast(&w->condition);
  pthread_mutex_unlock(&w->mutex);
  return 0;
}
static int drain_locked(struct waker *w) {
  char buffer[128];
  int bytes = 0, value;
  while ((value = recv(w->fd[0], buffer, sizeof(buffer), 0)) > 0) bytes += value;
  w->pending = 0;
  return bytes;
}
static int init_waker(struct waker *w) {
  memset(w, 0, sizeof(*w));
  int connect_result = 0, connect_error = 0, socket_error = 0;
  w->fd[0] = w->fd[1] = -1;
  if (pthread_mutex_init(&w->mutex, 0)) return 0;
  if (pthread_cond_init(&w->condition, 0)) { pthread_mutex_destroy(&w->mutex); return 0; }
  if (!pair(w->fd, &connect_result, &connect_error, &socket_error)) {
    pthread_cond_destroy(&w->condition);
    pthread_mutex_destroy(&w->mutex);
    return 0;
  }
  return 1;
}
static int close_waker(struct waker *w) {
  pthread_mutex_lock(&w->mutex);
  w->closing = 1;
  int ok = closed(&w->fd[0]) & closed(&w->fd[1]);
  pthread_mutex_unlock(&w->mutex);
  return ok;
}
static void wakers(void) {
  const char *ids[] = {"waker.before-check", "waker.prepared", "waker.blocked", "waker.coalesced",
                       "waker.cancel", "waker.close-before-notify", "waker.notify-before-close"};
  for (int mode = 0; mode < 7; ++mode) {
    struct waker w;
    char json[1024];
    if (!init_waker(&w)) { result(ids[mode], false, "{\"setup\":false}"); continue; }
    w.mode = mode;
    pthread_t thread;
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    int attributes = pthread_attr_setstacksize(&attr, 256 * 1024);
    int created = attributes ? attributes : pthread_create(&thread, &attr, publish, &w);
    pthread_attr_destroy(&attr);
    if (created) {
      snprintf(json, sizeof(json), "{\"pthread_create\":%d}", created);
      result(ids[mode], false, json);
      close_waker(&w);
      pthread_cond_destroy(&w.condition);
      pthread_mutex_destroy(&w.mutex);
      continue;
    }
    ++live_helpers;
    if (mode == 5) close_waker(&w);  /* Close-before-notify ordered by mutex. */
    pthread_mutex_lock(&w.mutex);
    int empty = w.published;
    w.gate = 1;
    pthread_cond_broadcast(&w.condition);
    if (mode != 2 && mode != 4) while (!w.done) pthread_cond_wait(&w.condition, &w.mutex);
    pthread_mutex_unlock(&w.mutex);
    short events = 0;
    int polled = -2;
    double elapsed = 0;
    struct rusage before = {0}, after = {0};
    int usage_before = 0, usage_after = 0;
    if (mode == 1 || mode == 2 || mode == 4) {
      usage_before = getrusage(RUSAGE_SELF, &before);
      double start = monotonic_ms();
      polled = wait_fd(w.fd[0], POLLIN, &events, -1);
      elapsed = monotonic_ms() - start;
      usage_after = getrusage(RUSAGE_SELF, &after);
    }
    short wait_events = events;
    int join_result = pthread_join(thread, 0);
    if (!join_result) { ++joined; --live_helpers; }
    pthread_mutex_lock(&w.mutex);
    int observed = w.published;
    int drained = w.fd[0] >= 0 ? drain_locked(&w) : 0;
    int extra = w.fd[0] >= 0 ? wait_fd(w.fd[0], POLLIN, &events, 0) : -2;
    pthread_mutex_unlock(&w.mutex);
    int released = close_waker(&w);
    snprintf(json, sizeof(json), "{\"empty_check\":%d,\"accepted\":%d,\"observed\":%d,\"poll\":%d,"
        "\"raw_revents\":%d,\"signal_bytes\":%d,\"drained_bytes\":%d,\"extra_poll\":%d,\"joined\":%s,"
        "\"closed\":%s,\"cancel_seen\":%s,\"elapsed_ms\":%.3f,\"gate_deadline_ms\":100,\"poll_calls\":%d,"
        "\"timeout_ms\":-1,\"helper_stack_bytes\":262144,\"control_errno\":%d,\"usage_measured\":%s,"
        "\"process_cpu_us\":%ld,\"maxrss_raw\":%ld,\"usage_source\":\"getrusage(RUSAGE_SELF); maxrss units are OS-specific\"}",
        empty, w.accepted, observed, polled, wait_events, w.signal_bytes, drained, extra, boolean(!join_result),
        boolean(released), boolean(w.cancelled), elapsed, polled != -2, w.control_error,
        boolean(!usage_before && !usage_after),
        (after.ru_utime.tv_sec - before.ru_utime.tv_sec + after.ru_stime.tv_sec - before.ru_stime.tv_sec) * 1000000L +
        after.ru_utime.tv_usec - before.ru_utime.tv_usec + after.ru_stime.tv_usec - before.ru_stime.tv_usec, after.ru_maxrss);
    int wanted = mode == 3 ? 1000 : mode == 5 ? 0 : 1;
    int ok = !join_result && released && !w.control_error && w.accepted == wanted && observed == wanted;
    if (mode == 1 || mode == 2 || mode == 4) ok &= polled == 1;
    if (mode == 2) ok &= elapsed >= 80;
    if (mode == 3) ok &= drained == 1 && w.signal_bytes == 1 && extra == 0;
    if (mode == 4) ok &= w.cancelled;
    result(ids[mode], ok, json);
    pthread_cond_destroy(&w.condition);
    pthread_mutex_destroy(&w.mutex);
  }
  struct waker redundant;
  if (init_waker(&redundant)) {
    short events = 0;
    int sent = send(redundant.fd[1], "W", 1, 0);
    int polled = wait_fd(redundant.fd[0], POLLIN, &events, -1);
    pthread_mutex_lock(&redundant.mutex);
    int observed = redundant.published;
    int drained = drain_locked(&redundant);
    pthread_mutex_unlock(&redundant.mutex);
    int extra = wait_fd(redundant.fd[0], POLLIN, &events, 0);
    int released = close_waker(&redundant);
    char json[256];
    snprintf(json, sizeof(json), "{\"send\":%d,\"poll\":%d,\"observed\":%d,\"drained_bytes\":%d,\"extra_poll\":%d,\"closed\":%s}",
        sent, polled, observed, drained, extra, boolean(released));
    result("waker.redundant", sent == 1 && polled == 1 && observed == 0 && drained == 1 && extra == 0 && released, json);
    pthread_cond_destroy(&redundant.condition);
    pthread_mutex_destroy(&redundant.mutex);
  } else result("waker.redundant", false, "{\"setup\":false}");
  struct waker fault;
  if (init_waker(&fault)) {
    closed(&fault.fd[1]);  /* Controlled real EBADF after acceptance. */
    pthread_mutex_lock(&fault.mutex);
    int accepted = notify_locked(&fault);
    pthread_mutex_unlock(&fault.mutex);
    char json[512];
    snprintf(json, sizeof(json), "{\"accepted\":%d,\"observed\":%d,\"send\":%d,\"errno\":%d,"
        "\"control_failed\":%s,\"fault_fixture\":\"closed-notify-descriptor\"}",
        accepted, fault.published, fault.send_result, fault.control_error, boolean(fault.control_error != 0));
    result("waker.control-failure", accepted == 1 && fault.published == 1 && fault.send_result == -1 && fault.control_error == EBADF, json);
    close_waker(&fault);
    pthread_cond_destroy(&fault.condition);
    pthread_mutex_destroy(&fault.mutex);
  } else result("waker.control-failure", false, "{\"setup\":false}");
  char json[256];
  snprintf(json, sizeof(json), "{\"all_descriptors_closed\":%s,\"threads_joined\":%d,\"live_helpers\":%d}", boolean(cleanup_ok), joined, live_helpers);
  result("waker.cleanup", cleanup_ok && joined == 7 && live_helpers == 0, json);
}
static void sockets(void) {
  int fd[2], connect_value = 0, connect_error = 0, socket_error = 0;
  int setup = pair(fd, &connect_value, &connect_error, &socket_error);
  char json[1024], byte = 0;
  snprintf(json, sizeof(json), "{\"nonblocking\":%s,\"connect\":%d,\"connect_errno\":%d,\"socket_error\":%d,\"connected\":%s}",
      boolean(setup), connect_value, connect_error, socket_error, boolean(setup));
  result("socket.connect", setup, json);
  if (!setup) return;
  short events = 0;
  int empty = wait_fd(fd[0], POLLIN, &events, 0);
  int sent = send(fd[1], "S", 1, 0);
  int polled = wait_fd(fd[0], POLLIN, &events, 2000);
  int received = polled == 1 ? recv(fd[0], &byte, 1, 0) : -2;
  snprintf(json, sizeof(json), "{\"empty_poll\":%d,\"send\":%d,\"poll\":%d,\"raw_revents\":%d,\"readable\":%s,\"read\":%d,\"byte\":%d}",
      empty, sent, polled, events, boolean(events & POLLIN), received, byte);
  result("socket.readiness", empty == 0 && sent == 1 && polled == 1 && (events & POLLIN) && received == 1 && byte == 'S', json);
  int shut = shutdown(fd[1], SHUT_WR);
  polled = wait_fd(fd[0], POLLIN, &events, 2000);
  int eof = polled == 1 ? recv(fd[0], &byte, 1, 0) : -2;
  int reverse = send(fd[0], "R", 1, 0);
  int ready = wait_fd(fd[1], POLLIN, &events, 2000);
  int readback = ready == 1 ? recv(fd[1], &byte, 1, 0) : -2;
  snprintf(json, sizeof(json), "{\"shutdown\":%d,\"poll\":%d,\"eof\":%d,\"reverse_send\":%d,\"reverse_read\":%d,\"byte\":%d,\"raw_revents\":%d}",
      shut, polled, eof, reverse, readback, byte, events);
  result("socket.halfclose", !shut && polled == 1 && eof == 0 && reverse == 1 && readback == 1 && byte == 'R', json);
  int peer_closed = closed(&fd[0]);
  polled = wait_fd(fd[1], POLLIN, &events, 2000);
  eof = polled == 1 ? recv(fd[1], &byte, 1, 0) : -2;
  snprintf(json, sizeof(json), "{\"peer_closed\":%s,\"poll\":%d,\"eof\":%d,\"raw_revents\":%d,\"hup\":%s,\"error\":%s}",
      boolean(peer_closed), polled, eof, events, boolean(events & POLLHUP), boolean(events & POLLERR));
  result("socket.eof", peer_closed && polled == 1 && eof == 0, json);
  int retired = fd[1];
  closed(&fd[1]);
  errno = 0;
  polled = wait_fd(retired, POLLIN, &events, 0);
  int bad_errno = errno;
  int invalid = (polled == 1 && (events & POLLNVAL)) || (polled == -1 && bad_errno == EBADF);
  snprintf(json, sizeof(json), "{\"poll\":%d,\"raw_revents\":%d,\"errno\":%d,\"invalid_observed\":%s}", polled, events, bad_errno, boolean(invalid));
  result("socket.invalid", invalid, json);
  int replacement[2];
  setup = pair(replacement, &connect_value, &connect_error, &socket_error);
  int newfd = -1;
  if (setup) {
    if (replacement[0] == retired) newfd = replacement[0];
    else if (replacement[1] == retired) { int temp = replacement[0]; replacement[0] = replacement[1]; replacement[1] = temp; newfd = retired; }
    else { newfd = dup2(replacement[0], retired); closed(&replacement[0]); replacement[0] = newfd; }
  }
  sent = setup ? send(replacement[1], "N", 1, 0) : -2;
  polled = setup ? wait_fd(newfd, POLLIN, &events, 2000) : -2;
  received = polled == 1 ? recv(newfd, &byte, 1, 0) : -2;
  snprintf(json, sizeof(json), "{\"retired_descriptor\":%d,\"replacement_descriptor\":%d,\"same_descriptor\":%s,\"read\":%d,\"byte\":%d}",
      retired, newfd, boolean(newfd == retired), received, byte);
  result("socket.reuse", setup && newfd == retired && sent == 1 && received == 1 && byte == 'N', json);
  closed(&replacement[0]); closed(&replacement[1]);

  int reserve = socket(AF_INET, SOCK_STREAM, 0), client = -1;
  struct sockaddr_in address = {0};
  address.sin_family = AF_INET; address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  socklen_t length = sizeof(address);
  setup = reserve >= 0 && !bind(reserve, (void *)&address, length) && !getsockname(reserve, (void *)&address, &length);
  client = setup ? socket(AF_INET, SOCK_STREAM, 0) : -1;
  setup = setup && client >= 0 && nonblock(client);
  errno = 0;
  connect_value = setup ? connect(client, (void *)&address, length) : -2;
  connect_error = errno;
  polled = setup ? wait_fd(client, POLLOUT, &events, 2000) : -2;
  socket_error = 0; length = sizeof(socket_error);
  int got = setup ? getsockopt(client, SOL_SOCKET, SO_ERROR, &socket_error, &length) : -2;
  int selected = -2, writable = 0, exceptional = 0;
  if (setup && polled == 0 && !got && !socket_error) {
    /* A measured WSAPoll limitation must not become an invented capability. */
    fd_set writes, errors;
    FD_ZERO(&writes); FD_ZERO(&errors);
    FD_SET(client, &writes); FD_SET(client, &errors);
    struct timeval deadline = {5, 0};
    selected = select(client + 1, 0, &writes, &errors, &deadline);
    writable = selected > 0 && FD_ISSET(client, &writes);
    exceptional = selected > 0 && FD_ISSET(client, &errors);
    length = sizeof(socket_error);
    got = getsockopt(client, SOL_SOCKET, SO_ERROR, &socket_error, &length);
  }
  int failed_connect = setup && connect_value == -1 &&
      ((connect_error != EINPROGRESS && connect_error != EWOULDBLOCK) || ((polled == 1 || selected > 0) && !got && socket_error != 0));
  snprintf(json, sizeof(json), "{\"connect\":%d,\"connect_errno\":%d,\"poll\":%d,\"poll_timeout_ms\":2000,\"raw_revents\":%d,\"error\":%s,\"hup\":%s,\"select\":%d,\"select_writable\":%s,\"select_exceptional\":%s,\"socket_error\":%d,\"connection_failed\":%s}",
      connect_value, connect_error, polled, events, boolean(events & POLLERR), boolean(events & POLLHUP), selected, boolean(writable), boolean(exceptional), socket_error, boolean(failed_connect));
  result("socket.connect-error", failed_connect, json);
  closed(&client); closed(&reserve);
  snprintf(json, sizeof(json), "{\"all_descriptors_closed\":%s}", boolean(cleanup_ok));
  result("socket.cleanup", cleanup_ok, json);
}
int main(int argc, char **argv) {
  if (argc == 2 && !strcmp(argv[1], "--hang")) for (;;) pause();
  if (argc != 2 || strcmp(argv[1], "--suite")) return 64;
  signal(SIGPIPE, SIG_IGN);
  struct utsname u;
  uname(&u);
  const char *arch =
#if defined(__aarch64__)
      "aarch64";
#else
      "x86_64";
#endif
  char json[1024];
  snprintf(json, sizeof(json), "{\"probe\":\"m2-waker\",\"os\":\"%s\",\"arch\":\"%s\",\"page_size\":%ld,\"helper_limit\":1,"
      "\"symbolic_flags\":{\"POLLIN\":%d,\"POLLOUT\":%d,\"POLLERR\":%d,\"POLLHUP\":%d,\"POLLNVAL\":%d}}",
      u.sysname, arch, sysconf(_SC_PAGESIZE), POLLIN, POLLOUT, POLLERR, POLLHUP, POLLNVAL);
  result("facts", true, json);
  wakers(); sockets();
  return failures ? 1 : 0;
}
