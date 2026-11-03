/* SPDX-License-Identifier: MIT */
/* Real OS probes only: no Runtime, reactor or product capability claims. */
#define _COSMO_SOURCE 1
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <sched.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/utsname.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>
#include "libc/dce.h"
#include "libc/nt/console.h"
#include "libc/nt/createfile.h"
#include "libc/nt/enum/accessmask.h"
#include "libc/nt/enum/creationdisposition.h"
#include "libc/nt/enum/filesharemode.h"
#include "libc/nt/ipc.h"
#include "libc/nt/runtime.h"
#include "libc/nt/thread.h"

/* Exact scalar-only declarations from the pinned libc/nt/files.h. Including
   that header also includes its broken, duplicated NtSidIdentifierAuthority;
   leave the locked toolchain untouched and exercise these wrappers on Windows. */
extern int32_t DuplicateHandle(int64_t, int64_t, int64_t, int64_t *, uint32_t, int32_t, uint32_t);
extern int32_t GetHandleInformation(int64_t, uint32_t *);

static FILE *evidence;
static int failures;
static const unsigned char payload[] = {0, 1, 0x7f, 0x80, 0xff, '\n'};
static const char *boolean(bool b) { return b ? "true" : "false"; }
static void result(const char *id, bool ok, const char *actual) {
  fprintf(evidence, "{\"case_id\":\"%s\",\"status\":\"%s\",\"actual\":%s}\n", id, ok ? "pass" : "fail", actual);
  fflush(evidence);
  failures += !ok;
}
static void operation_failure(const char *id, int error, const char *reason) {
  fprintf(evidence, "{\"case_id\":\"%s\",\"status\":\"fail\",\"actual\":{\"os_error\":%d},\"reason\":\"%s\"}\n", id, error, reason);
  fflush(evidence);
  ++failures;
}
static int release(int fd) {
  if (fd < 0) return 0;
  int r = close(fd);
  errno = 0;
  return !r && fcntl(fd, F_GETFD) == -1 && errno == EBADF;
}
static void hex(const unsigned char *bytes, size_t n, char *out) {
  for (size_t i = 0; i < n; ++i) sprintf(out + 2 * i, "%02x", bytes[i]);
  out[2 * n] = 0;
}
static void bytes_and_alias(void) {
  int fd[2] = {-1, -1}, alias = -1;
  unsigned char bytes[32] = {0};
  char json[512], encoded[65];
  int made = pipe(fd);
  ssize_t wrote = !made ? write(fd[1], payload, sizeof(payload)) : -1;
  struct pollfd p = {fd[0], POLLIN, 0};
  int ready = !made ? poll(&p, 1, 1000) : -1;
  ssize_t n = ready == 1 ? read(fd[0], bytes, sizeof(bytes)) : -1;
  hex(bytes, n > 0 ? n : 0, encoded);
  snprintf(json, sizeof(json), "{\"hex\":\"%s\",\"read\":%ld,\"write\":%ld,\"short_read\":%s,\"poll\":%d,\"revents\":%d}", encoded, (long)n, (long)wrote, boolean(n > 0 && n < sizeof(bytes)), ready, p.revents);
  result("pipe.bytes", !made && wrote == sizeof(payload) && n == sizeof(payload) && !memcmp(bytes, payload, sizeof(payload)) && ready == 1 && p.revents & POLLIN, json);
  alias = !made ? dup(fd[0]) : -1;
  int before = alias >= 0 ? fcntl(fd[0], F_GETFL) : -1;
  errno = 0;
  int set = before >= 0 ? fcntl(alias, F_SETFL, before | O_NONBLOCK) : -1, error = errno;
  int observed = set == 0 ? fcntl(fd[0], F_GETFL) : -1;
  int reset = before >= 0 ? fcntl(alias, F_SETFL, before) : -1;
  int after = reset == 0 ? fcntl(fd[0], F_GETFL) : -1;
  if (set == -1 && error) operation_failure("pipe.alias", error, "F_SETFL on a duplicate pipe failed; OS error retained");
  else {
    /* False is still an observed answer: platforms may maintain descriptor-local flags. */
    snprintf(json, sizeof(json), "{\"alias_observed\":true,\"shared_nonblocking\":%s,\"before\":%d,\"alias_set\":%d,\"borrowed_after\":%d,\"restored\":%s}", boolean(observed >= 0 && (observed & O_NONBLOCK)), before, set, observed, boolean(after == before));
    result("pipe.alias", alias >= 0 && set == 0 && observed >= 0 && after == before, json);
  }
  int writer_closed = release(fd[1]); fd[1] = -1;
  n = !made ? read(fd[0], bytes, 1) : -1;
  int broken[2] = {-1, -1};
  int made_broken = pipe(broken), closed_reader = !made_broken ? release(broken[0]) : 0;
  errno = 0;
  ssize_t bad = !made_broken ? write(broken[1], payload, 1) : -2; error = errno;
  snprintf(json, sizeof(json), "{\"eof\":%s,\"broken\":%s,\"read\":%ld,\"write\":%ld,\"errno\":%d,\"expected_errno\":%d}", boolean(writer_closed && n == 0), boolean(closed_reader && bad == -1 && error == EPIPE), (long)n, (long)bad, error, EPIPE);
  result("pipe.eof-broken", writer_closed && n == 0 && closed_reader && bad == -1 && error == EPIPE, json);
  int all = release(fd[0]);
  all &= release(alias);
  if (!made_broken) all = release(broken[1]) && all;
  result("pipe.cleanup", all, all ? "{\"all_closed\":true}" : "{\"all_closed\":false}");
}
static void windows_backpressure(void) {
  int64_t r = -1, w = -1;
  uint32_t mode = 1, n = 0, got = 0;
  bool made = CreatePipe(&r, &w, 0, 65536);
  int set = made && SetNamedPipeHandleState(w, &mode, 0, 0);
  unsigned error = set ? 0 : GetLastError();
  if (!set) {
    operation_failure("pipe.backpressure", error, "Win32 NOWAIT setup failed; OS error retained");
    if (r != -1) CloseHandle(r);
    if (w != -1) CloseHandle(w);
    return;
  }
  unsigned char input[16384], output[16384];
  memset(input, 0xa5, sizeof(input));
  size_t filled = 0, seen = 0;
  int wrote = 1;
  while (filled < 2 * 1024 * 1024) {
    n = 0;
    wrote = WriteFile(w, input, 4096, &n, 0);
    if (!wrote || !n) break;
    filled += n;
  }
  error = wrote ? 0 : GetLastError();
  bool blocked = filled > 0 && n == 0;
  int drained = filled >= 4096 && ReadFile(r, output, 4096, &got, 0) ? got : -1;
  bool same = drained == 4096;
  for (int i = 0; i < drained; ++i) same &= output[i] == 0xa5;
  memset(input, 0x5a, sizeof(input));
  n = 0;
  int partial_ok = blocked && drained > 0 && WriteFile(w, input, sizeof(input), &n, 0);
  unsigned partial_error = partial_ok ? 0 : GetLastError();
  bool short_write = partial_ok && n > 0 && n < sizeof(input);
  uint32_t partial = n;
  size_t remaining = filled - (drained > 0 ? drained : 0);
  bool cleaned = CloseHandle(w);
  int read_ok;
  while ((read_ok = ReadFile(r, output, sizeof(output), &got, 0)) && got) {
    for (unsigned i = 0; i < got; ++i, ++seen) same &= output[i] == (seen < remaining ? 0xa5 : 0x5a);
  }
  unsigned eof_error = read_ok ? 0 : GetLastError();
  same &= seen == remaining + partial && !read_ok && eof_error == 109;
  cleaned &= CloseHandle(r);
  char json[768];
  bool no_progress = partial_ok && partial == 0 && partial_error == 0;
  snprintf(json, sizeof(json), "{\"would_block\":%s,\"partial_write\":%s,\"raw_bytes_match\":%s,\"filled\":%zu,\"drained\":%d,\"write\":%u,\"write_success\":%s,\"requested\":%zu,\"os_error\":%u,\"full_error\":%u,\"eof_error\":%u,\"all_closed\":%s,\"route\":\"Win32 NOWAIT byte pipe\",\"capability_limit\":\"%s\"}", boolean(blocked), boolean(short_write), boolean(same), filled, drained, partial, boolean(partial_ok), sizeof(input), partial_error, error, eof_error, boolean(cleaned), no_progress ? "nowait-large-write-zero-progress; retain bytes and retry smaller chunks" : "");
  result("pipe.backpressure", blocked && (short_write || no_progress) && same && cleaned, json);
}
static void backpressure(void) {
  if (IsWindows()) { windows_backpressure(); return; }
  int fd[2] = {-1, -1};
  char json[512];
  if (pipe(fd)) { result("pipe.backpressure", false, "{\"setup\":false}"); return; }
  int flags = fcntl(fd[1], F_GETFL);
  errno = 0;
  int set = fcntl(fd[1], F_SETFL, flags | O_NONBLOCK), error = errno;
  if (set) {
    operation_failure("pipe.backpressure", error, "Nonblocking pipe setup failed; OS error retained");
    release(fd[0]); release(fd[1]); return;
  }
  /* A whole kernel page must be drained to free a pipe buffer slot on 64K
     Linux. A request above the macOS growth size still exercises a short write. */
  unsigned char input[131072], output[131072];
  long page_size = sysconf(_SC_PAGESIZE);
  size_t drain_size = page_size > 0 && page_size <= 65536 ? page_size : 4096;
  memset(input, 0xa5, sizeof(input));
  size_t filled = 0;
  ssize_t n = 0;
  while (filled < 2 * 1024 * 1024 && (n = write(fd[1], input, 4096)) > 0) filled += n;
  error = errno;
  bool blocked = n == -1 && (error == EAGAIN || error == EWOULDBLOCK);
  ssize_t drained = filled >= drain_size ? read(fd[0], output, drain_size) : -1;
  bool same = drained == drain_size;
  for (int i = 0; i < drained; ++i) same &= output[i] == 0xa5;
  memset(input, 0x5a, sizeof(input));
  ssize_t partial = blocked && drained > 0 ? write(fd[1], input, sizeof(input)) : -1;
  bool short_write = partial > 0 && partial < sizeof(input);
  size_t remaining = filled - (drained > 0 ? drained : 0), seen = 0;
  int closed_writer = release(fd[1]);
  while ((n = read(fd[0], output, sizeof(output))) > 0) {
    for (int i = 0; i < n; ++i, ++seen) same &= output[i] == (seen < remaining ? 0xa5 : 0x5a);
  }
  same &= n == 0 && seen == remaining + (partial > 0 ? partial : 0);
  int closed_reader = release(fd[0]);
  snprintf(json, sizeof(json), "{\"would_block\":%s,\"partial_write\":%s,\"raw_bytes_match\":%s,\"filled\":%zu,\"drained\":%ld,\"requested\":%zu,\"write\":%ld,\"errno\":%d,\"all_closed\":%s}", boolean(blocked), boolean(short_write), boolean(same), filled, (long)drained, sizeof(input), (long)partial, error, boolean(closed_writer && closed_reader));
  result("pipe.backpressure", blocked && short_write && same && closed_writer && closed_reader, json);
}

struct Wait {
  pthread_mutex_t mutex;
  pthread_cond_t condition;
  int fd, control, phase, action, syscall_result, syscall_error, poll_result, revents;
  uint32_t thread_id;
  int64_t handle;
  unsigned char byte;
  bool delivered, cancelled;
};
static void *waiter(void *opaque) {
  struct Wait *w = opaque;
  pthread_mutex_lock(&w->mutex);
  w->thread_id = IsWindows() ? GetCurrentThreadId() : 0;
  w->phase = 1;
  pthread_cond_broadcast(&w->condition);
  while (!w->action) pthread_cond_wait(&w->condition, &w->mutex);
  pthread_mutex_unlock(&w->mutex);
  if (IsWindows()) {
    uint32_t n = 0;
    int ok = ReadFile(w->handle, &w->byte, 1, &n, 0);
    w->syscall_result = ok ? n : -1;
    w->syscall_error = ok ? 0 : GetLastError();
  } else {
    struct pollfd p[2] = {{w->fd, POLLIN, 0}, {w->control, POLLIN, 0}};
    w->poll_result = poll(p, 2, 3000);
    w->revents = p[0].revents;
    if (w->poll_result > 0 && p[1].revents & POLLIN) {
      w->syscall_result = -1; w->syscall_error = ECANCELED;
    } else {
      w->syscall_result = w->poll_result > 0 ? read(w->fd, &w->byte, 1) : -2;
      w->syscall_error = errno;
    }
  }
  pthread_mutex_lock(&w->mutex);
  w->phase = 2;
  pthread_cond_broadcast(&w->condition);
  /* Publication to the owner is gated separately from physical syscall exit. */
  while (w->action != 2) pthread_cond_wait(&w->condition, &w->mutex);
  w->delivered = w->syscall_result >= 0 && !w->cancelled;
  w->phase = 3;
  pthread_mutex_unlock(&w->mutex);
  return 0;
}
static double seconds(void) {
  struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t);
  return t.tv_sec + t.tv_nsec / 1e9;
}
static void wait_probe(int mode) {
  int fd[2] = {-1, -1}, control[2] = {-1, -1};
  int64_t r = -1, wr = -1, thread_handle = 0;
  bool made = IsWindows() ? CreatePipe(&r, &wr, 0, 4096) : !pipe(fd) && !pipe(control);
  struct Wait w = {.mutex = PTHREAD_MUTEX_INITIALIZER, .condition = PTHREAD_COND_INITIALIZER,
                   .fd = fd[0], .control = control[0], .handle = r, .syscall_result = -99};
  pthread_t thread;
  int created = made ? pthread_create(&thread, 0, waiter, &w) : -1;
  int accepted = 0, cancel_error = 0, pending = 0;
  int closed_writer = 0;
  if (!created) {
    pthread_mutex_lock(&w.mutex);
    while (w.phase != 1) pthread_cond_wait(&w.condition, &w.mutex);
    /* For late readiness, publish cancellation and data before allowing poll. */
    if (!IsWindows() && mode == 2) {
      accepted = write(control[1], "C", 1) == 1;
      write(fd[1], "L", 1);
      w.cancelled = true;
    }
    w.action = 1;
    pthread_cond_broadcast(&w.condition);
    pthread_mutex_unlock(&w.mutex);
    if (IsWindows()) {
      thread_handle = OpenThread(0x1 | 0x40 | 0x800, false, w.thread_id);
      /* This is a controlled fixture handshake, not a runtime short-poll backend.
         Query actual pending I/O before cancellation; no sleep guesses. */
      double deadline = seconds() + 2;
      while (thread_handle && seconds() < deadline) {
        int32_t flag = 0;
        if (GetThreadIOPendingFlag(thread_handle, &flag) && flag) { pending = 1; break; }
        sched_yield();
      }
      if (mode == 0) {
        accepted = pending && CancelSynchronousIo(thread_handle);
        if (!accepted) cancel_error = GetLastError();
      } else if (mode == 2) {
        uint32_t n;
        WriteFile(wr, "L", 1, &n, 0);
      }
      if (mode == 1 || (mode == 0 && !accepted)) { closed_writer = CloseHandle(wr); wr = -1; }
    } else if (mode == 0) accepted = write(control[1], "C", 1) == 1;
    else if (mode == 1) { closed_writer = release(fd[1]); fd[1] = -1; }
    pthread_mutex_lock(&w.mutex);
    while (w.phase != 2) pthread_cond_wait(&w.condition, &w.mutex);
    /* Cancellation wins before the owner accepts a completed result. */
    bool late = mode == 2;
    if (late && IsWindows()) accepted = w.phase == 2 && !w.delivered;
    w.cancelled = mode != 1;
    w.action = 2;
    pthread_cond_broadcast(&w.condition);
    pthread_mutex_unlock(&w.mutex);
  }
  int joined = !created ? pthread_join(thread, 0) : -1;
  bool native_exited = !created && !joined && w.phase == 3;
  bool open = IsWindows() ? ({uint32_t flags; GetHandleInformation(r, &flags);}) : fcntl(fd[0], F_GETFD) >= 0;
  bool bytes_preserved = false;
  if (mode == 2) {
    if (IsWindows()) bytes_preserved = w.syscall_result == 1 && w.byte == 'L';
    else { unsigned char b = 0; bytes_preserved = read(fd[0], &b, 1) == 1 && b == 'L'; }
    /* Raw bytes remain in the OS/record; cancellation precedes owner delivery. */
  }
  bool cleaned = true;
  if (IsWindows()) {
    if (r != -1) cleaned &= CloseHandle(r);
    if (wr != -1) cleaned &= CloseHandle(wr);
    if (thread_handle) cleaned &= CloseHandle(thread_handle);
  } else {
    for (int i = 0; i < 2; ++i) {
      if (fd[i] >= 0) cleaned &= release(fd[i]);
      if (control[i] >= 0) cleaned &= release(control[i]);
    }
  }
  pthread_cond_destroy(&w.condition);
  pthread_mutex_destroy(&w.mutex);
  char json[768];
  snprintf(json, sizeof(json), "{\"accepted\":%s,\"native_exited\":%s,\"joined\":%s,\"borrowed_open\":%s,\"eof\":%s,\"cancel_first\":%s,\"delivered\":%s,\"bytes_preserved\":%s,\"syscall_result\":%d,\"os_error\":%d,\"cancel_error\":%d,\"pending_observed\":%d,\"poll\":%d,\"revents\":%d,\"all_closed\":%s,\"route\":\"%s\"}",
           boolean(accepted), boolean(native_exited), boolean(!joined), boolean(open),
           boolean(closed_writer && (IsWindows() ? w.syscall_result == -1 && w.syscall_error == 109 : w.syscall_result == 0)),
           boolean(mode == 2 && accepted), boolean(w.delivered), boolean(bytes_preserved), w.syscall_result, w.syscall_error, cancel_error, pending, w.poll_result, w.revents, boolean(cleaned), IsWindows() ? (mode == 2 ? "native-complete/logical-cancel/owner-gate" : "CancelSynchronousIo/ReadFile") : "poll/control-pipe");
  bool ok = native_exited && cleaned && open;
  if (mode == 0) ok &= accepted && (IsWindows() ? w.syscall_result == -1 && w.syscall_error == 995 : w.syscall_error == ECANCELED);
  else if (mode == 1) ok &= closed_writer && (IsWindows() ? w.syscall_error == 109 : w.syscall_result == 0);
  else ok &= accepted && !w.delivered && bytes_preserved;
  result(mode == 0 ? "pipe.cancel-wait" : mode == 1 ? "pipe.close-wait" : "pipe.late-completion", ok, json);
}
static void stdin_matrix(void) {
  int saved = dup(0), fd[2] = {-1, -1}, alias = -1;
  struct stat before = {0}, after = {0}, kind = {0};
  int original = fstat(0, &before);
  unsigned char bytes[32] = {0};
  char encoded[65], json[512];
  int made = pipe(fd), installed = !made ? dup2(fd[0], 0) : -1;
  if (installed >= 0) alias = dup(0);
  if (!made) write(fd[1], payload, sizeof(payload));
  ssize_t n = installed >= 0 ? read(0, bytes, sizeof(bytes)) : -1;
  hex(bytes, n > 0 ? n : 0, encoded);
  bool pipe_kind = !fstat(0, &kind) && S_ISFIFO(kind.st_mode);
  bool borrowed_open = fcntl(0, F_GETFD) >= 0, alias_open = alias >= 0 && fcntl(alias, F_GETFD) >= 0;
  snprintf(json, sizeof(json), "{\"kind\":\"%s\",\"hex\":\"%s\",\"borrowed_open\":%s,\"alias_open\":%s,\"owned_source_distinct\":%s}", pipe_kind ? "pipe" : "other", encoded, boolean(borrowed_open), boolean(alias_open), boolean(fd[0] != 0));
  result("stdin.pipe", pipe_kind && n == sizeof(payload) && !memcmp(bytes, payload, sizeof(payload)) && borrowed_open && alias_open, json);
  if (alias >= 0) release(alias);
  if (fd[0] >= 0) release(fd[0]);
  if (fd[1] >= 0) release(fd[1]);
  FILE *file = tmpfile();
  if (file) { fwrite(payload, 1, sizeof(payload), file); fflush(file); rewind(file); }
  installed = file ? dup2(fileno(file), 0) : -1;
  n = installed >= 0 ? read(0, bytes, sizeof(bytes)) : -1;
  hex(bytes, n > 0 ? n : 0, encoded);
  bool file_kind = !fstat(0, &kind) && S_ISREG(kind.st_mode);
  bool shared = file && lseek(fileno(file), 0, SEEK_CUR) == sizeof(payload);
  borrowed_open = fcntl(0, F_GETFD) >= 0;
  snprintf(json, sizeof(json), "{\"kind\":\"%s\",\"hex\":\"%s\",\"shared_offset\":%s,\"borrowed_open\":%s}", file_kind ? "file" : "other", encoded, boolean(shared), boolean(borrowed_open));
  result("stdin.file", file_kind && n == sizeof(payload) && !memcmp(bytes, payload, sizeof(payload)) && shared && borrowed_open, json);
  if (file) fclose(file);
  close(0);
  errno = 0; n = read(0, bytes, sizeof(bytes)); int error = errno;
  snprintf(json, sizeof(json), "{\"rejected\":%s,\"read\":%ld,\"errno\":%d,\"expected_errno\":%d}", boolean(n == -1 && error == EBADF), (long)n, error, EBADF);
  result("stdin.invalid", n == -1 && error == EBADF, json);
  int reset = saved >= 0 ? dup2(saved, 0) : -1;
  int after_stat = fstat(0, &after);
  /* Real Windows observations give different st_ino values for the restored
     duplicate. Keep both raw values; verify dup2 and device/type there. */
  bool restored = saved >= 0 ? reset == 0 && !after_stat && !original && before.st_dev == after.st_dev && (IsWindows() || before.st_ino == after.st_ino) && before.st_mode == after.st_mode : original == -1 && fcntl(0, F_GETFD) == -1;
  if (saved >= 0) restored &= release(saved);
  snprintf(json, sizeof(json), "{\"restored\":%s,\"saved\":%d,\"dup2\":%d,\"before_fstat\":%d,\"after_fstat\":%d,\"before_dev\":%llu,\"after_dev\":%llu,\"before_ino\":%llu,\"after_ino\":%llu,\"before_mode\":%u,\"after_mode\":%u}", boolean(restored), saved, reset, original, after_stat, (unsigned long long)before.st_dev, (unsigned long long)after.st_dev, (unsigned long long)before.st_ino, (unsigned long long)after.st_ino, before.st_mode, after.st_mode);
  result("stdin.restore", restored, json);
}
static void output_matrix(int target) {
  int saved = dup(target), fd[2] = {-1, -1};
  unsigned char bytes[32] = {0};
  char encoded[65], json[512];
  int made = pipe(fd), installed = !made && saved >= 0 ? dup2(fd[1], target) : -1;
  ssize_t wrote = installed >= 0 ? write(target, payload, sizeof(payload)) : -1;
  int reset = saved >= 0 ? dup2(saved, target) : -1;
  ssize_t n = wrote > 0 ? read(fd[0], bytes, sizeof(bytes)) : -1;
  hex(bytes, n > 0 ? n : 0, encoded);
  bool open = fcntl(target, F_GETFD) >= 0;
  bool cleaned = release(fd[0]);
  cleaned &= release(fd[1]);
  cleaned &= release(saved);
  snprintf(json, sizeof(json), "{\"hex\":\"%s\",\"read\":%ld,\"write\":%ld,\"borrowed_open\":%s,\"restored\":%s,\"all_closed\":%s}", encoded, (long)n, (long)wrote, boolean(open), boolean(reset == target), boolean(cleaned));
  result(target == 1 ? "stdout.pipe" : "stderr.pipe", wrote == sizeof(payload) && n == sizeof(payload) && !memcmp(bytes, payload, sizeof(payload)) && open && reset == target && cleaned, json);
}

static void terminal(void) {
  char json[512];
  if (IsWindows()) {
    int64_t saved_handle = GetStdHandle(kNtStdInputHandle);
    int64_t owned = CreateFile(u"CONIN$", kNtGenericRead | kNtGenericWrite, kNtFileShareRead | kNtFileShareWrite, 0, kNtOpenExisting, 0, 0), alias = -1;
    bool installed = owned != -1 && SetStdHandle(kNtStdInputHandle, owned);
    int64_t borrowed = GetStdHandle(kNtStdInputHandle);
    uint32_t before = 0, changed = 0, after = 0;
    bool got = installed && GetConsoleMode(borrowed, &before);
    bool duplicated = got && DuplicateHandle(-1, borrowed, -1, &alias, 0, false, 2);
    bool set = duplicated && SetConsoleMode(alias, before ^ 4u);
    bool shared = set && GetConsoleMode(borrowed, &changed) && changed == (before ^ 4u);
    bool reset = got && SetConsoleMode(borrowed, before) && GetConsoleMode(borrowed, &after) && after == before;
    snprintf(json, sizeof(json), "{\"isatty\":%s,\"borrowed_open\":%s,\"route\":\"GetStdHandle/Console\",\"os_error\":%u}", boolean(got), boolean(got), GetLastError());
    result("stdin.terminal", got, json);
    snprintf(json, sizeof(json), "{\"shared_mode\":%s,\"before\":%u,\"alias_changed\":%u}", boolean(shared), before, changed);
    result("terminal.alias", shared, json);
    bool standard_restored = SetStdHandle(kNtStdInputHandle, saved_handle) && GetStdHandle(kNtStdInputHandle) == saved_handle;
    snprintf(json, sizeof(json), "{\"restored\":%s,\"original_standard_restored\":%s}", boolean(reset && standard_restored), boolean(standard_restored));
    result("terminal.restore", reset && standard_restored, json);
    bool cleaned = owned != -1 && CloseHandle(owned);
    if (alias != -1) cleaned &= CloseHandle(alias); else cleaned = false;
    snprintf(json, sizeof(json), "{\"all_closed\":%s}", boolean(cleaned));
    result("terminal.cleanup", cleaned, json);
    return;
  }
  int master = posix_openpt(O_RDWR | O_NOCTTY), slave = -1, alias = -1, saved = dup(0);
  if (master >= 0 && !grantpt(master) && !unlockpt(master)) {
    const char *name = ptsname(master);
    if (name) slave = open(name, O_RDWR | O_NOCTTY);
  }
  bool installed = slave >= 0 && dup2(slave, 0) == 0;
  if (installed) alias = dup(0);
  struct termios before = {0}, changed = {0}, measured = {0}, after = {0};
  bool got = installed && !tcgetattr(0, &before);
  changed = before; changed.c_lflag ^= ECHO;
  bool shared = got && alias >= 0 && !tcsetattr(alias, TCSANOW, &changed) && !tcgetattr(0, &measured) && measured.c_lflag == changed.c_lflag;
  /* Restore before emitting any output, including error paths. */
  bool reset = got && !tcsetattr(0, TCSANOW, &before) && !tcgetattr(0, &after) && before.c_iflag == after.c_iflag && before.c_oflag == after.c_oflag && before.c_cflag == after.c_cflag && before.c_lflag == after.c_lflag && !memcmp(before.c_cc, after.c_cc, sizeof(before.c_cc));
  bool detected = installed && isatty(0) == 1, borrowed_open = fcntl(0, F_GETFD) >= 0;
  snprintf(json, sizeof(json), "{\"isatty\":%s,\"borrowed_open\":%s,\"route\":\"private PTY/dup2 stdin\",\"errno\":%d}", boolean(detected), boolean(borrowed_open), errno);
  result("stdin.terminal", detected && borrowed_open, json);
  snprintf(json, sizeof(json), "{\"shared_mode\":%s,\"before_lflag\":%u,\"alias_changed_lflag\":%u}", boolean(shared), before.c_lflag, measured.c_lflag);
  result("terminal.alias", shared, json);
  bool restored = saved >= 0 ? dup2(saved, 0) == 0 : close(0) == 0;
  snprintf(json, sizeof(json), "{\"restored\":%s,\"original_standard_restored\":%s}", boolean(reset && restored), boolean(restored));
  result("terminal.restore", reset && restored, json);
  bool cleaned = release(alias);
  cleaned &= release(slave);
  cleaned &= release(master);
  if (saved >= 0) cleaned &= release(saved);
  snprintf(json, sizeof(json), "{\"all_closed\":%s}", boolean(cleaned));
  result("terminal.cleanup", cleaned, json);
}
int main(int argc, char **argv) {
  evidence = stdout;
  signal(SIGPIPE, SIG_IGN);
  if (argc == 2 && !strcmp(argv[1], "--hang")) for (;;) pause();
  if (argc >= 2 && !strcmp(argv[1], "--tty")) {
    if (argc == 4 && !strcmp(argv[2], "--result")) evidence = fopen(argv[3], "w");
    if (!evidence) return 3;
    terminal();
    if (evidence != stdout && fclose(evidence)) return 4;
    return failures ? 1 : 0;
  }
  if (argc != 2 || strcmp(argv[1], "--suite")) return 64;
  struct utsname host; uname(&host);
#ifdef __aarch64__
  const char *arch = "aarch64";
#else
  const char *arch = "x86_64";
#endif
  char json[256];
  snprintf(json, sizeof(json), "{\"probe\":\"m2-stdio\",\"os\":\"%s\",\"arch\":\"%s\",\"page_size\":%ld}", IsWindows() ? "Windows" : host.sysname, arch, sysconf(_SC_PAGESIZE));
  result("facts", true, json);
  bytes_and_alias();
  backpressure();
  for (int i = 0; i < 3; ++i) wait_probe(i);
  stdin_matrix();
  output_matrix(1);
  output_matrix(2);
  if (!IsWindows()) terminal();
  return failures ? 1 : 0;
}
