/* SPDX-License-Identifier: MIT */
/* M0 native facts only: no Reactor, Node binding, Runtime or SDK. */
#define _COSMO_SOURCE 1
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <spawn.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/utsname.h>
#include <sys/wait.h>
#include <netinet/in.h>
#include <pty.h>
#include <termios.h>
#include <unistd.h>
#include "libc/dce.h"
#include "libc/nt/console.h"
#include "libc/nt/createfile.h"
#include "libc/nt/enum/accessmask.h"
#include "libc/nt/enum/creationdisposition.h"
#include "libc/nt/enum/filesharemode.h"
#include "libc/nt/runtime.h"

static int failures;
static int unavailable;
static FILE *evidence;
extern char **environ;
static void state(const char *id, const char *status, const char *actual) {
  fprintf(evidence, "{\"case_id\":\"%s\",\"status\":\"%s\",\"actual\":%s}\n", id, status, actual);
  fflush(evidence);
  failures += !strcmp(status, "fail");
  unavailable += !strcmp(status, "blocked");
}
static void result(const char *id, bool ok, const char *actual) {
  state(id, ok ? "pass" : "fail", actual);
}
static int ready(int fd, short *events, int timeout) {
  struct pollfd p = {fd, POLLIN, 0};
  int r = poll(&p, 1, timeout);
  *events = p.revents;
  return r;
}
static int closed(int fd) {
  if (fd < 0) return 0;
  int r = close(fd);
  errno = 0;
  int invalid = fcntl(fd, F_GETFD);
  return r == 0 && invalid == -1 && errno == EBADF;
}
static void sockets(void) {
  int listener = -1, client = -1, peer = -1;
  char json[512], byte = 0;
  struct sockaddr_in address = {0};
  socklen_t length = sizeof(address);
  short events = 0;
  listener = socket(AF_INET, SOCK_STREAM, 0);
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  bool setup = listener >= 0 && !bind(listener, (void *)&address, length) &&
      !getsockname(listener, (void *)&address, &length) && !listen(listener, 1);
  if (setup) {
    client = socket(AF_INET, SOCK_STREAM, 0);
    setup = client >= 0 && !connect(client, (void *)&address, length) && ready(listener, &events, 1000) == 1;
  }
  if (setup) peer = accept(listener, 0, 0);
  setup = setup && peer >= 0;
  if (!setup) {
    snprintf(json, sizeof(json), "{\"setup\":false,\"errno\":%d}", errno);
    result("socket.readiness", false, json);
    goto cleanup;
  }
  int empty = ready(peer, &events, 50);
  int sent = send(client, "S", 1, 0);
  int readable = ready(peer, &events, 1000);
  int received = readable == 1 ? recv(peer, &byte, 1, 0) : -2;
  snprintf(json, sizeof(json), "{\"empty_poll\":%d,\"send\":%d,\"ready_poll\":%d,\"revents\":%d,\"read\":%d,\"byte\":%d}", empty, sent, readable, events, received, byte);
  result("socket.readiness", empty == 0 && sent == 1 && readable == 1 && (events & POLLIN) && received == 1 && byte == 'S', json);
  int shut = shutdown(client, SHUT_WR);
  int eof_ready = ready(peer, &events, 1000);
  int eof = eof_ready == 1 ? recv(peer, &byte, 1, 0) : -2;
  int reply = send(peer, "R", 1, 0);
  int reply_ready = ready(client, &events, 1000);
  int reply_read = reply_ready == 1 ? recv(client, &byte, 1, 0) : -2;
  snprintf(json, sizeof(json), "{\"shutdown\":%d,\"eof_poll\":%d,\"eof_read\":%d,\"reverse_send\":%d,\"reverse_read\":%d}", shut, eof_ready, eof, reply, reply_read);
  result("socket.halfclose", !shut && eof_ready == 1 && eof == 0 && reply == 1 && reply_read == 1 && byte == 'R', json);
  errno = 0;
  int invalid = recv(-1, &byte, 1, 0), error = errno;
  snprintf(json, sizeof(json), "{\"read\":%d,\"errno\":%d,\"expected_errno\":%d}", invalid, error, EBADF);
  result("socket.invalid", invalid == -1 && error == EBADF, json);
cleanup:
  int a = closed(peer), b = closed(client), c = closed(listener);
  snprintf(json, sizeof(json), "{\"peer_closed\":%d,\"client_closed\":%d,\"listener_closed\":%d}", a, b, c);
  result("socket.cleanup", a && b && c, json);
}
static void pipes(void) {
  int fd[2] = {-1, -1}, broken[2] = {-1, -1};
  char json[384], byte = 0;
  short events = 0;
  if (pipe(fd)) {
    snprintf(json, sizeof(json), "{\"pipe\":-1,\"errno\":%d}", errno);
    result("pipe.readiness", false, json);
    return;
  }
  int empty = ready(fd[0], &events, 50);
  int wrote = write(fd[1], "P", 1);
  int readable = ready(fd[0], &events, 1000);
  int read_value = readable == 1 ? read(fd[0], &byte, 1) : -2;
  snprintf(json, sizeof(json), "{\"empty_poll\":%d,\"write\":%d,\"ready_poll\":%d,\"revents\":%d,\"read\":%d}", empty, wrote, readable, events, read_value);
  result("pipe.readiness", empty == 0 && wrote == 1 && readable == 1 && (events & POLLIN) && read_value == 1 && byte == 'P', json);
  int writer_closed = closed(fd[1]);
  int eof_ready = ready(fd[0], &events, 1000);
  int eof = eof_ready == 1 ? read(fd[0], &byte, 1) : -2;
  snprintf(json, sizeof(json), "{\"writer_closed\":%d,\"ready_poll\":%d,\"revents\":%d,\"read\":%d}", writer_closed, eof_ready, events, eof);
  result("pipe.eof", writer_closed && eof_ready == 1 && eof == 0, json);
  int reader_closed = closed(fd[0]);
  int made = pipe(broken);
  int broken_reader_closed = !made ? closed(broken[0]) : 0;
  errno = 0;
  int bad_write = !made ? write(broken[1], "X", 1) : -2, error = errno;
  snprintf(json, sizeof(json), "{\"write\":%d,\"errno\":%d,\"expected_errno\":%d,\"reader_closed\":%d,\"sigpipe_ignored\":true}", bad_write, error, EPIPE, broken_reader_closed);
  result("pipe.broken-writer", !made && broken_reader_closed && bad_write == -1 && error == EPIPE, json);
  int broken_writer_closed = !made ? closed(broken[1]) : 0;
  snprintf(json, sizeof(json), "{\"reader_closed\":%d,\"writer_closed\":%d,\"broken_reader_closed\":%d,\"broken_writer_closed\":%d}", reader_closed, writer_closed, broken_reader_closed, broken_writer_closed);
  result("pipe.cleanup", reader_closed && writer_closed && broken_reader_closed && broken_writer_closed, json);
}
static void terminal_fd(int fd) {
  char json[384];
  struct termios saved = {0}, changed = {0}, measured = {0}, restored = {0};
  int detected = isatty(fd), got = tcgetattr(fd, &saved);
  changed = saved;
  changed.c_lflag ^= ECHO;
  int set = got == 0 ? tcsetattr(fd, TCSANOW, &changed) : -2;
  int readback = set == 0 ? tcgetattr(fd, &measured) : -2;
  bool modified = readback == 0 && (measured.c_lflag & ECHO) == (changed.c_lflag & ECHO);
  /* Restore before emitting/returning even when the mutation/readback fails. */
  int reset = got == 0 ? tcsetattr(fd, TCSANOW, &saved) : -2;
  int checked = reset == 0 ? tcgetattr(fd, &restored) : -2;
  bool same = checked == 0 && saved.c_iflag == restored.c_iflag && saved.c_oflag == restored.c_oflag &&
      saved.c_cflag == restored.c_cflag && saved.c_lflag == restored.c_lflag &&
      !memcmp(saved.c_cc, restored.c_cc, sizeof(saved.c_cc));
  snprintf(json, sizeof(json), "{\"isatty\":%d,\"getattr\":%d,\"setattr\":%d,\"modified\":%s,\"errno\":%d}", detected, got, set, modified ? "true" : "false", errno);
  result("tty.controlled", detected == 1 && !got && !set && modified, json);
  snprintf(json, sizeof(json), "{\"reset\":%d,\"getattr\":%d,\"restored\":%s}", reset, checked, same ? "true" : "false");
  result("tty.restore", same, json);
}
static void terminals(bool controlled) {
  char json[384];
  if (IsWindows() && controlled) {
    uint32_t saved = 0, measured = 0, restored = 0;
    uint32_t standard_mode = 0;
    int standard_console = GetConsoleMode(GetStdHandle((uint32_t)-10), &standard_mode);
    int64_t handle = CreateFile(u"CONIN$", kNtGenericRead | kNtGenericWrite,
                               kNtFileShareRead | kNtFileShareWrite, 0, kNtOpenExisting, 0, 0);
    int got = GetConsoleMode(handle, &saved);
    int changed = got ? SetConsoleMode(handle, saved ^ 4u) : 0;
    int readback = changed ? GetConsoleMode(handle, &measured) : 0;
    int reset = got ? SetConsoleMode(handle, saved) : 0;
    int checked = reset ? GetConsoleMode(handle, &restored) : 0;
    snprintf(json, sizeof(json), "{\"isatty\":%d,\"standard_handle_console\":%d,\"owned_CONIN_handle\":%s,\"GetConsoleMode\":%d,\"SetConsoleMode\":%d,\"before_mode\":%u,\"modified_mode\":%u,\"winerror\":%u}", isatty(0), standard_console, handle != -1 ? "true" : "false", got, changed, saved, measured, GetLastError());
    result("tty.controlled", got && changed && readback && measured == (saved ^ 4u), json);
    snprintf(json, sizeof(json), "{\"reset\":%d,\"restored\":%s,\"restored_mode\":%u}", reset, checked && saved == restored ? "true" : "false", restored);
    result("tty.restore", checked && saved == restored, json);
    int released = handle != -1 ? CloseHandle(handle) : 0;
    int invalid = GetConsoleMode(handle, &measured);
    unsigned close_error = GetLastError();
    snprintf(json, sizeof(json), "{\"CloseHandle\":%d,\"after_close_GetConsoleMode\":%d,\"winerror\":%u}", released, invalid, close_error);
    result("tty.console-handle-closed", released && !invalid && close_error == 6, json);
    return;
  }
  if (controlled) {
    terminal_fd(0);
    return;
  }
  int redirected[2] = {-1, -1};
  int made = pipe(redirected);
  errno = 0;
  int detected = isatty(redirected[0]), error = errno;
  struct termios attrs;
  errno = 0;
  int got = tcgetattr(redirected[0], &attrs), attr_error = errno;
  int closed_reader = closed(redirected[0]), closed_writer = closed(redirected[1]);
  snprintf(json, sizeof(json), "{\"isatty\":%d,\"errno\":%d,\"getattr\":%d,\"getattr_errno\":%d,\"reader_closed\":%d,\"writer_closed\":%d}", detected, error, got, attr_error, closed_reader, closed_writer);
  result("tty.redirected", !made && detected == 0 && got == -1 && attr_error == ENOTTY && closed_reader && closed_writer, json);
  if (!IsWindows()) {
    int master = -1, slave = -1;
    master = posix_openpt(O_RDWR | O_NOCTTY);
    int opened = master >= 0 && !grantpt(master) && !unlockpt(master) ? 0 : -1;
    const char *name = opened == 0 ? ptsname(master) : 0;
    if (name) slave = open(name, O_RDWR | O_NOCTTY);
    opened = opened == 0 && slave >= 0 ? 0 : -1;
    if (!opened) terminal_fd(slave);
    else {
      int error = errno;
      snprintf(json, sizeof(json), "{\"fixture_open\":%d,\"errno\":%d,\"reason\":\"private PTY facility unavailable; mode operations unmeasured\"}", opened, error);
      const char *status = error == ENOENT || error == ENODEV || error == ENOSYS || error == EACCES ? "blocked" : "fail";
      state("tty.controlled", status, json);
      state("tty.restore", "blocked", json);
    }
    int a = slave < 0 || closed(slave), b = master < 0 || closed(master);
    snprintf(json, sizeof(json), "{\"slave_closed_or_absent\":%d,\"master_closed_or_absent\":%d}", a, b);
    result("tty.cleanup", a && b, json);
  }
}
static pid_t wait_bounded(pid_t pid, int *status, int iterations) {
  for (int i = 0; i < iterations; ++i) {
    pid_t r = waitpid(pid, status, WNOHANG);
    if (r == -1 && errno == EINTR) continue;
    if (r) return r;
    usleep(10000);
  }
  return 0;
}
static int child_spawn(const char *self, const char *launcher, const char *mode, pid_t *pid) {
  char *args[] = {(char *)(launcher ? launcher : self), launcher ? (char *)self : (char *)mode,
      launcher ? (char *)mode : (char *)"参数 空格", launcher ? (char *)"参数 空格" : (char *)"quote\"slash\\",
      launcher ? (char *)"quote\"slash\\" : (char *)"literal&|;$()", launcher ? (char *)"literal&|;$()" : 0, 0};
  return posix_spawn(pid, args[0], 0, 0, args, environ);
}
static void processes(const char *self, const char *launcher) {
  char json[512];
  pid_t pid = -1;
  int spawned = child_spawn(self, launcher, "--child", &pid), status = -1;
  pid_t waited = !spawned ? wait_bounded(pid, &status, 200) : -1;
  int killed = 0;
  if (!spawned && waited != pid) {
    killed = kill(pid, SIGKILL) == 0;
    waited = wait_bounded(pid, &status, 100);
  }
  bool passed = !spawned && waited == pid && WIFEXITED(status) && WEXITSTATUS(status) == 37 && !killed;
  snprintf(json, sizeof(json), "{\"posix_spawn\":%d,\"pid\":%d,\"waitpid\":%d,\"raw_wait_status\":%d,\"exit_status\":%d,\"argv_verified\":%s,\"explicit_launcher\":%s,\"shell\":false}", spawned, pid, waited, status, WIFEXITED(status) ? WEXITSTATUS(status) : -1, passed ? "true" : "false", launcher ? "true" : "false");
  result("process.spawn-wait", passed, json);
  char *absent_args[] = {(char *)"qaplet-this-process-does-not-exist-08", 0};
  pid_t absent_pid = -1;
  int absent = posix_spawn(&absent_pid, absent_args[0], 0, 0, absent_args, environ);
  bool no_child = absent == ENOENT;
  /* Some libc implementations discover exec failure in the child. Reap that
     actual result as well; never leave an unexpected child behind. */
  int absent_status = -1;
  pid_t absent_wait = -1;
  if (!absent) {
    absent_wait = wait_bounded(absent_pid, &absent_status, 100);
    if (absent_wait != absent_pid) {
      kill(absent_pid, SIGKILL);
      absent_wait = wait_bounded(absent_pid, &absent_status, 100);
    }
  }
  snprintf(json, sizeof(json), "{\"posix_spawn\":%d,\"expected_errno\":%d,\"pid\":%d,\"waitpid\":%d,\"raw_wait_status\":%d}", absent, ENOENT, absent_pid, absent_wait, absent_status);
  result("process.missing", no_child, json);
  pid = -1;
  spawned = child_spawn(self, launcher, "--hang", &pid);
  status = -1;
  waited = !spawned ? wait_bounded(pid, &status, 15) : -1;
  bool was_running = waited == 0;
  killed = !spawned && was_running ? kill(pid, SIGKILL) == 0 : 0;
  if (!spawned && was_running) waited = wait_bounded(pid, &status, 100);
  errno = 0;
  pid_t after_wait = !spawned && waited == pid ? waitpid(pid, &status, WNOHANG) : -2;
  int after_error = errno;
  bool reaped = waited == pid && after_wait == -1 && after_error == ECHILD;
  snprintf(json, sizeof(json), "{\"posix_spawn\":%d,\"pid\":%d,\"observed_running\":%s,\"killed\":%s,\"waitpid\":%d,\"raw_wait_status\":%d,\"reaped\":%s,\"post_wait_errno\":%d,\"deadline_ms\":150}", spawned, pid, was_running ? "true" : "false", killed ? "true" : "false", waited, status, reaped ? "true" : "false", after_error);
  result("process.timeout-cleanup", !spawned && was_running && killed && reaped, json);
}
static _Thread_local int thread_local_value = 17;
static int thread_initial;
static void *thread_body(void *unused) {
  thread_initial = thread_local_value;
  thread_local_value = 99;
  return (void *)(intptr_t)thread_local_value;
}
static void threads(void) {
  char json[384];
  pthread_t thread;
  thread_local_value = 7;
  void *value = 0;
  int created = pthread_create(&thread, 0, thread_body, 0);
  int joined = !created ? pthread_join(thread, &value) : -1;
  snprintf(json, sizeof(json), "{\"pthread_create\":%d,\"pthread_join\":%d,\"thread_initial\":%d,\"thread_final\":%ld,\"parent_tls\":%d,\"joined\":%s}", created, joined, thread_initial, (long)(intptr_t)value, thread_local_value, !created && !joined ? "true" : "false");
  result("pthread.tls", !created && !joined && thread_initial == 17 && (intptr_t)value == 99 && thread_local_value == 7, json);
}
static void zip_resources(void) {
  static const char expected[] = "qaplet M0 I/O ZIP\n";
  char buffer[64] = {0}, json[384];
  errno = 0;
  int fd = open("/zip/io-baseline.txt", O_RDONLY), error = errno;
  int length = fd >= 0 ? read(fd, buffer, sizeof(buffer)) : -2;
  int eof = fd >= 0 ? read(fd, buffer + 32, sizeof(buffer) - 32) : -2;
  snprintf(json, sizeof(json), "{\"open\":%d,\"errno\":%d,\"read\":%d,\"eof_read\":%d,\"content_matches\":%s}", fd, error, length, eof, length == sizeof(expected) - 1 && !memcmp(buffer, expected, sizeof(expected) - 1) ? "true" : "false");
  result("zipos.read-eof", fd >= 0 && length == sizeof(expected) - 1 && !memcmp(buffer, expected, sizeof(expected) - 1) && eof == 0, json);
  int released = fd >= 0 ? closed(fd) : 0;
  errno = 0;
  int writable = open("/zip/io-baseline.txt", O_WRONLY), write_error = errno;
  if (writable >= 0) close(writable);
  snprintf(json, sizeof(json), "{\"write_open\":%d,\"errno\":%d,\"expected_errno\":%d}", writable, write_error, EROFS);
  result("zipos.readonly", writable == -1 && write_error == EROFS, json);
  snprintf(json, sizeof(json), "{\"reader_closed\":%d,\"unexpected_writer_closed\":%s}", released, writable < 0 ? "true" : "false");
  result("zipos.cleanup", released && writable < 0, json);
}
int main(int argc, char **argv) {
  evidence = stdout;
  signal(SIGPIPE, SIG_IGN);
  if (argc >= 2 && !strcmp(argv[1], "--child"))
    return argc == 5 && !strcmp(argv[2], "参数 空格") && !strcmp(argv[3], "quote\"slash\\") &&
        !strcmp(argv[4], "literal&|;$()") ? 37 : 98;
  if (argc >= 2 && !strcmp(argv[1], "--hang")) for (;;) usleep(100000);
  if (argc >= 2 && !strcmp(argv[1], "--tty")) {
    if (argc == 4 && !strcmp(argv[2], "--result")) {
      evidence = fopen(argv[3], "w");
      if (!evidence) return 3;
    }
    terminals(true);
    if (evidence != stdout && fclose(evidence)) return 4;
    return failures ? 1 : unavailable ? 2 : 0;
  }
  struct utsname host;
  uname(&host);
#ifdef __aarch64__
  const char *arch = "aarch64";
#else
  const char *arch = "x86_64";
#endif
  printf("{\"case_id\":\"facts\",\"status\":\"pass\",\"actual\":{\"probe\":\"native-io\",\"os\":\"%s\",\"arch\":\"%s\",\"page_size\":%ld}}\n", host.sysname, arch, sysconf(_SC_PAGESIZE));
  sockets();
  pipes();
  terminals(false);
  processes(argv[0], argc == 4 && !strcmp(argv[2], "--launcher") ? argv[3] : 0);
  threads();
  zip_resources();
  return failures ? 1 : unavailable ? 2 : 0;
}
