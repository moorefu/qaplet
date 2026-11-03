/* SPDX-License-Identifier: MIT */
/* Private, bounded OS feasibility fixture. No Runtime or public PTY API. */
#define _COSMO_SOURCE 1
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/utsname.h>
#include <sys/wait.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>
#include "libc/dce.h"
#include "libc/nt/console.h"
#include "libc/nt/createfile.h"
#include "libc/nt/enum/accessmask.h"
#include "libc/nt/enum/creationdisposition.h"
#include "libc/nt/enum/filesharemode.h"
#include "libc/nt/runtime.h"
#include "libc/nt/events.h"
#include "libc/nt/synchronization.h"
/* Exact declaration from this pinned files.h. Including the full header hits
   its duplicate NtSidIdentifierAuthority definition; the linked wrapper is
   exercised by the actual Windows probe, never inferred from compilation. */
bool32 DuplicateHandle(int64_t, int64_t, int64_t, int64_t *, uint32_t, bool32, uint32_t);

static FILE *evidence;
static const char *gate_directory;
static const char *fixture_root;
static int failures;
static const char *boolean(int n) { return n ? "true" : "false"; }
static void result(const char *id, int pass, const char *json) {
  fprintf(evidence, "{\"case_id\":\"%s\",\"status\":\"%s\",\"actual\":%s}\n", id, pass ? "pass" : "fail", json);
  fflush(evidence);
  failures += !pass;
}
static int closed(int fd) {
  if (fd < 0) return 0;
  int ok = !close(fd);
  errno = 0;
  return ok && fcntl(fd, F_GETFD) == -1 && errno == EBADF;
}
static int same(const struct termios *a, const struct termios *b) {
  return a->c_iflag == b->c_iflag && a->c_oflag == b->c_oflag &&
    a->c_cflag == b->c_cflag && a->c_lflag == b->c_lflag &&
    !memcmp(a->c_cc, b->c_cc, sizeof(a->c_cc)) &&
    cfgetispeed(a) == cfgetispeed(b) && cfgetospeed(a) == cfgetospeed(b);
}
static const char *stdin_type(void) {
  struct stat s;
  uint32_t mode;
  if (fstat(0, &s)) return "invalid";
  if (IsWindows() && GetConsoleMode(GetStdHandle((uint32_t)-10), &mode)) return "Console";
  if (isatty(0)) return "TTY";
  if (S_ISFIFO(s.st_mode) || S_ISSOCK(s.st_mode)) return "pipe";
  if (S_ISREG(s.st_mode)) return "file";
  return "other";
}
static void stdin_routes(int terminal) {
  char json[512], path[4096] = {0};
  int save = dup(0), save_error = save < 0 ? errno : 0, pipes[2] = {-1, -1};
  int made = !pipe(pipes), pipe_error = made ? 0 : errno;
  FILE *temporary = 0; int file = -1;
  if (fixture_root) {
    int length = snprintf(path, sizeof(path), "%s/qaplet-m2-tty-XXXXXX", fixture_root);
    if (length < 0 || length >= sizeof(path)) errno = ENAMETOOLONG;
    else file = mkstemp(path);
  } else {
    /* The guest has no controller directory; use the OS temporary-file API. */
    temporary = tmpfile();
    if (temporary) file = fileno(temporary);
  }
  int file_error = file < 0 ? errno : 0;
  int setup = save >= 0 && made && file >= 0, route_ok = 1;
  const char *names[] = {"pipe", "file", "invalid", "terminal"};
  for (int i = 0; setup && i < 4; ++i) {
    if (i == 3 && terminal < 0) continue;
    if (i == 2) route_ok &= !close(0);
    else route_ok &= dup2(i == 0 ? pipes[0] : i == 1 ? file : terminal, 0) == 0;
    const char *type = stdin_type();
    snprintf(json, sizeof(json), "{\"type\":\"%s\",\"actual_fd\":0,\"private_terminal_inference\":false}", type);
    char id[64]; snprintf(id, sizeof(id), "stdin.%s", names[i]);
    result(id, route_ok && !strcmp(type, i == 3 ? "TTY" : names[i]), json);
    route_ok &= dup2(save, 0) == 0;
  }
  int original_restored = save >= 0 && dup2(save, 0) == 0;
  int clean = 1;
  if (save >= 0) clean &= closed(save);
  if (pipes[0] >= 0) clean &= closed(pipes[0]);
  if (pipes[1] >= 0) clean &= closed(pipes[1]);
  if (temporary) { clean &= !fclose(temporary); errno = 0; clean &= fcntl(file, F_GETFD) == -1 && errno == EBADF; }
  else if (file >= 0) { clean &= closed(file); clean &= !unlink(path); }
  snprintf(json, sizeof(json), "{\"owned_routes_closed\":%s,\"original_fd0_restored\":%s,\"fixture_root_supplied\":%s,\"setup\":{\"dup_errno\":%d,\"pipe_errno\":%d,\"file_errno\":%d},\"routes_completed\":%s}", boolean(clean), boolean(original_restored), boolean(fixture_root != 0), save_error, pipe_error, file_error, boolean(setup && route_ok));
  result("stdin.cleanup", setup && route_ok && original_restored && clean, json);
}
static void console_stdin(int64_t input) {
  int64_t original = GetStdHandle((uint32_t)-10);
  int native_set = SetStdHandle((uint32_t)-10, input);
  uint32_t mode = 0;
  int actual_console = native_set && GetConsoleMode(GetStdHandle((uint32_t)-10), &mode);
  int restore = SetStdHandle((uint32_t)-10, original) && GetStdHandle((uint32_t)-10) == original;
  char json[512]; snprintf(json, sizeof(json), "{\"type\":\"%s\",\"source\":\"GetStdHandle-STD_INPUT_HANDLE\",\"private_terminal_inference\":false,\"native_standard_handle_rerouted\":%s,\"native_standard_handle_console\":%s,\"original_standard_handle_restored\":%s,\"runtime_fd0_rerouted\":false,\"runtime_fd0_type\":\"%s\",\"scope\":\"native-Console-route-separate-from-libc-fd0\"}", actual_console ? "Console" : "unmeasured", boolean(native_set), boolean(actual_console), boolean(restore), stdin_type());
  result("stdin.console", actual_console && restore, json);
}
struct Lease { int active, fd; struct termios original; uint32_t input, output; int64_t hin, hout; };
static int restore_termios(int fd, const struct termios *original) {
  struct termios checked;
  if (tcsetattr(fd, TCSANOW, original) || tcgetattr(fd, &checked)) return 0;
  if (IsXnu() && !(original->c_lflag & PENDIN) && (checked.c_lflag & PENDIN)) {
    /* XNU marks raw -> canonical input for reprocessing. FIONREAD runs that
       processing without consuming bytes or flushing the queue. Compare the
       entire resulting termios below; never mask PENDIN out of evidence. */
    int available = 0;
    if (ioctl(fd, FIONREAD, &available) || tcgetattr(fd, &checked)) return 0;
  }
  return same(original, &checked);
}
static int acquire(struct Lease *l) {
  if (l->active) return EBUSY;
  if (IsWindows()) {
    uint32_t check, check_output;
    if (!GetConsoleMode(l->hin, &l->input) || !GetConsoleMode(l->hout, &l->output)) return EIO;
    l->active = 1; /* restore responsibility starts before first mutation */
    if (!SetConsoleMode(l->hin, (l->input & ~6u) | 8u) ||
        !SetConsoleMode(l->hout, l->output | 4u) ||
        !GetConsoleMode(l->hin, &check) || check != ((l->input & ~6u) | 8u) ||
        !GetConsoleMode(l->hout, &check_output) || check_output != (l->output | 4u)) return EIO;
    return 0;
  }
  if (tcgetattr(l->fd, &l->original)) return errno;
  struct termios raw = l->original, measured;
  cfmakeraw(&raw);
  l->active = 1;
  if (tcsetattr(l->fd, TCSANOW, &raw) || tcgetattr(l->fd, &measured) || !same(&raw, &measured)) return EIO;
  return 0;
}
static int release(struct Lease *l) {
  if (!l->active) return 0;
  int ok;
  if (IsWindows()) {
    uint32_t a, b;
    ok = SetConsoleMode(l->hin, l->input) && SetConsoleMode(l->hout, l->output) &&
      GetConsoleMode(l->hin, &a) && GetConsoleMode(l->hout, &b) && a == l->input && b == l->output;
  } else {
    ok = restore_termios(l->fd, &l->original);
  }
  if (ok) l->active = 0;
  return ok ? 0 : EIO;
}
struct Stop { pthread_mutex_t mutex; pthread_cond_t cond; int requested; };
static void *request_stop(void *opaque) {
  struct Stop *s = opaque;
  pthread_mutex_lock(&s->mutex); s->requested = 1; pthread_cond_signal(&s->cond); pthread_mutex_unlock(&s->mutex);
  return 0;
}
static int observed_stop(void) {
  struct Stop s = {.mutex = PTHREAD_MUTEX_INITIALIZER, .cond = PTHREAD_COND_INITIALIZER};
  pthread_t thread;
  if (pthread_create(&thread, 0, request_stop, &s)) return 0;
  pthread_mutex_lock(&s.mutex); while (!s.requested) pthread_cond_wait(&s.cond, &s.mutex); pthread_mutex_unlock(&s.mutex);
  int done = !pthread_join(thread, 0);
  return !pthread_cond_destroy(&s.cond) && !pthread_mutex_destroy(&s.mutex) && done && s.requested;
}
static int snapshot(struct Lease *l, char *json, size_t size) {
  if (IsWindows()) {
    uint32_t a = 0, b = 0;
    int ok = GetConsoleMode(l->hin, &a) && GetConsoleMode(l->hout, &b);
    snprintf(json, size, "{\"input_mode\":%u,\"output_mode\":%u}", a, b);
    return ok;
  }
  struct termios t = {0}; char cc[sizeof(t.c_cc) * 2 + 1];
  int ok = !tcgetattr(l->fd, &t);
  for (int i = 0; i < sizeof(t.c_cc); ++i) snprintf(cc + 2 * i, 3, "%02x", (unsigned char)t.c_cc[i]);
  snprintf(json, size, "{\"iflag\":%llu,\"oflag\":%llu,\"cflag\":%llu,\"lflag\":%llu,\"cc_hex\":\"%s\",\"ispeed\":%u,\"ospeed\":%u}", (unsigned long long)t.c_iflag, (unsigned long long)t.c_oflag, (unsigned long long)t.c_cflag, (unsigned long long)t.c_lflag, cc, (unsigned)cfgetispeed(&t), (unsigned)cfgetospeed(&t));
  return ok;
}
static void leases(struct Lease *l) {
  const char *paths[] = {"normal", "error", "stop", "close"};
  char json[3072], id[64];
  for (int i = 0; i < 4; ++i) {
    struct Lease base = *l;
    int owned_input = -1; int64_t owned_hin = 0, owned_hout = 0;
    if (i == 3) {
      if (IsWindows()) {
        DuplicateHandle(-1, l->hin, -1, &owned_hin, 0, false, 2);
        DuplicateHandle(-1, l->hout, -1, &owned_hout, 0, false, 2);
        l->hin = owned_hin; l->hout = owned_hout;
      } else { owned_input = dup(l->fd); l->fd = owned_input; }
    }
    char before[512], modified[512], after[512];
    int before_ok = snapshot(l, before, sizeof(before));
    int got = acquire(l), conflict = acquire(l);
    int modified_ok = snapshot(l, modified, sizeof(modified));
    uint32_t readback = 0;
    struct termios measured = {0};
    int changed = IsWindows() ? GetConsoleMode(l->hin, &readback) && readback != l->input :
      !tcgetattr(l->fd, &measured) && !same(&measured, &l->original);
    int trigger = 1, raw_error = 0, close_verified = 1;
    if (i == 1) {
      if (IsWindows()) { uint32_t invalid; trigger = !GetConsoleMode(-1, &invalid); raw_error = GetLastError(); }
      else { errno = 0; int invalid = tcgetattr(-1, &measured); raw_error = errno; trigger = invalid == -1 && raw_error != 0; }
    } else if (i == 2) trigger = observed_stop();
    /* Restore runs on the owner after actual OS error / cooperative stop. */
    int restored = !release(l), again = !release(l);
    int after_ok = snapshot(l, after, sizeof(after));
    restored &= before_ok && modified_ok && after_ok && !strcmp(before, after);
    if (i == 3) {
      if (IsWindows()) { uint32_t mode; close_verified = CloseHandle(owned_hin) && CloseHandle(owned_hout) && !GetConsoleMode(owned_hin, &mode) && !GetConsoleMode(owned_hout, &mode) && GetConsoleMode(base.hin, &mode); }
      else close_verified = owned_input >= 0 && closed(owned_input) && fcntl(base.fd, F_GETFD) >= 0;
    }
    snprintf(json, sizeof(json), "{\"acquire\":%d,\"conflict_busy\":%s,\"changed_readback\":%s,\"restored_all_fields\":%s,\"idempotent\":%s,\"path_triggered\":%s,\"raw_error\":%d,\"close_verified\":%s,\"before_input_flags\":%llu,\"changed_input_flags\":%llu,\"before_output_flags\":%llu,\"before_control_flags\":%llu,\"before_local_flags\":%llu,\"before_input_speed\":%u,\"before_output_speed\":%u}", got, boolean(conflict == EBUSY), boolean(changed), boolean(restored), boolean(again), boolean(trigger), raw_error, boolean(close_verified),
      (unsigned long long)(IsWindows() ? l->input : l->original.c_iflag), (unsigned long long)(IsWindows() ? readback : measured.c_iflag),
      (unsigned long long)(IsWindows() ? l->output : l->original.c_oflag), (unsigned long long)l->original.c_cflag,
      (unsigned long long)l->original.c_lflag, (unsigned)cfgetispeed(&l->original), (unsigned)cfgetospeed(&l->original));
    size_t length = strlen(json);
    snprintf(json + length - 1, sizeof(json) - length + 1, ",\"before\":%s,\"modified\":%s,\"restored\":%s}", before, modified, after);
    snprintf(id, sizeof(id), "lease.%s", paths[i]);
    result(id, !got && conflict == EBUSY && changed && restored && again && trigger && close_verified && (i != 1 || raw_error != 0), json);
    if (i == 3) *l = base;
  }
}
/* Strict scalar decoder: rejects overlong, surrogate, out-of-range and unfinished input. */
struct Decoder { unsigned value, minimum; int pending, scalars; };
static int decode(struct Decoder *d, unsigned c) {
  if (!d->pending) {
    if (c < 128) { ++d->scalars; return 1; }
    if (c >= 0xc2 && c <= 0xdf) { d->value = c & 31; d->minimum = 128; d->pending = 1; }
    else if (c >= 0xe0 && c <= 0xef) { d->value = c & 15; d->minimum = 2048; d->pending = 2; }
    else if (c >= 0xf0 && c <= 0xf4) { d->value = c & 7; d->minimum = 65536; d->pending = 3; }
    else return 0;
  } else {
    if ((c & 0xc0) != 0x80) return 0;
    d->value = (d->value << 6) | (c & 63);
    if (!--d->pending) {
      if (d->value < d->minimum || d->value > 0x10ffff || (d->value >= 0xd800 && d->value <= 0xdfff)) return 0;
      ++d->scalars;
    }
  }
  return 1;
}
static int valid(const unsigned char *p, int n, int *count) {
  struct Decoder d = {0};
  for (int i = 0; i < n; ++i) if (!decode(&d, p[i])) return 0;
  *count = d.scalars;
  return !d.pending;
}
static const unsigned char text[] = "A\xe4\xb8\xad\xf0\x9f\x99\x82Z";
static void invalid_text(void) {
  const unsigned char bad[][4] = {{0xc0,0xaf}, {0xed,0xa0,0x80}, {0xf4,0x90,0x80,0x80}, {0xe4,0xb8}, {0x80}};
  const int lengths[] = {2,3,4,2,1};
  int rejected = 0, n = 0;
  for (int i = 0; i < 5; ++i) rejected += !valid(bad[i], lengths[i], &n);
  result("text.invalid", rejected == 5, rejected == 5 ?
    "{\"strict_rejected\":5,\"replacement_decode\":false,\"console_writes\":0,\"policy\":\"reject-before-Console-encoding\"}" : "{\"strict_rejected\":0}");
}
static void binary_stream(void) {
  int fd[2]; unsigned char byte = 0; int exact = !pipe(fd), total = 0;
  for (int i = 0; exact && i < 256; ++i) {
    byte = i;
    if (write(fd[1], &byte, 1) != 1 || read(fd[0], &byte, 1) != 1 || byte != i) exact = 0;
    else ++total;
  }
  int cleanup = closed(fd[0]) && closed(fd[1]);
  char json[256]; snprintf(json, sizeof(json), "{\"all_byte_values\":%d,\"nul_preserved\":%s,\"invalid_utf8_preserved\":%s,\"exact\":%s,\"pipes_closed\":%s,\"console_binary\":\"UNSUPPORTED\"}", total, boolean(exact), boolean(exact), boolean(exact), boolean(cleanup));
  result("stream.binary", exact && cleanup && total == 256, json);
}
static int transfer(int from, int to, const unsigned char *bytes, int n) {
  unsigned char got[64];
  for (int i = 0; i < n; ++i) if (write(from, bytes + i, 1) != 1) return 0;
  int total = 0;
  while (total < n) {
    struct pollfd wait = {to, POLLIN, 0};
    if (poll(&wait, 1, 1000) != 1 || read(to, got + total, 1) != 1) return 0;
    ++total;
  }
  return !memcmp(got, bytes, n);
}
static void posix_text(struct Lease *l, int master) {
  int acquired = !acquire(l), scalar = 0;
  int input = acquired && transfer(master, l->fd, text, sizeof(text) - 1);
  int output = acquired && transfer(l->fd, master, text, sizeof(text) - 1);
  int strict = valid(text, sizeof(text) - 1, &scalar), restored = !release(l);
  char json[384];
  snprintf(json, sizeof(json), "{\"input_exact\":%s,\"output_exact\":%s,\"one_byte_chunks\":true,\"hex\":\"41e4b8adf09f99825a\",\"input_hex\":\"41e4b8adf09f99825a\",\"unicode_scalars\":%d,\"strict_valid\":%s,\"mode_restored\":%s}", boolean(input), boolean(output), scalar, boolean(strict), boolean(restored));
  result("text.chunks", input && output && strict && scalar == 4 && restored, json);
  const unsigned char vt[] = "\033[31mX\033[0m";
  acquired = !acquire(l);
  int raw = acquired && transfer(l->fd, master, vt, sizeof(vt) - 1), reset = !release(l);
  result("text.vt", raw && reset, raw && reset ?
    "{\"subset\":\"SGR-31-and-reset\",\"transport\":\"raw-PTY-bytes\",\"exact_hex\":\"1b5b33316d581b5b306d\",\"rendering_claim\":false}" : "{\"transport\":\"failed\"}");
  const unsigned char malformed[] = {0xc0,0xaf,0xed,0xa0,0x80,0x51};
  acquired = !acquire(l);
  int exact = acquired && transfer(master, l->fd, malformed, sizeof(malformed));
  reset = !release(l); int scalar_count = 0, rejected = !valid(malformed, sizeof(malformed), &scalar_count);
  snprintf(json, sizeof(json), "{\"input_hex\":\"c0afeda08051\",\"raw_transport_exact\":%s,\"strict_rejected\":%s,\"replacement_as_byte_proof\":false,\"mode_restored\":%s}", boolean(exact), boolean(rejected), boolean(reset));
  result("text.invalid-input", exact && rejected && reset, json);
}
struct Resize { int count, cols, rows, setup; };
static int resize_notify_fd = -1;
static void resize_notify(int signal) {
  int saved = errno;
  if (signal == SIGWINCH) write(resize_notify_fd, "W", 1);
  errno = saved;
}
static int close_owned(int *fd) {
  if (*fd < 0) return 1;
  int ok = closed(*fd); *fd = -1; return ok;
}
static void posix_resize(void) {
  /* A session leader's exit can revoke its controlling terminal on Darwin.
     Keep resize's terminal independent of the lease/text/borrowed terminal. */
  int master = posix_openpt(O_RDWR | O_NOCTTY), slave = -1;
  if (master >= 0 && !grantpt(master) && !unlockpt(master)) slave = open(ptsname(master), O_RDWR | O_NOCTTY);
  int ready[2] = {-1,-1}, gate[2] = {-1,-1}, reply[2] = {-1,-1}, notify[2] = {-1,-1};
  int made = slave >= 0 && !pipe(ready) && !pipe(gate) && !pipe(reply) && !pipe(notify);
  struct Resize observed = {0}; struct winsize first = {.ws_row = 24, .ws_col = 80}, last = {.ws_row = 33, .ws_col = 100};
  int initial = made && !ioctl(master, TIOCSWINSZ, &first);
  pid_t child = initial ? fork() : -1;
  if (!child) {
    close(master); close(ready[0]); close(gate[1]); close(reply[0]);
    sigset_t set; sigemptyset(&set); sigaddset(&set, SIGWINCH);
    resize_notify_fd = notify[1];
    struct sigaction action = {.sa_handler = resize_notify}; sigemptyset(&action.sa_mask);
    int blocked = !sigprocmask(SIG_BLOCK, &set, 0);
    int installed = !sigaction(SIGWINCH, &action, 0);
    observed.setup = blocked && installed && !fcntl(notify[1], F_SETFL, O_NONBLOCK) &&
      setsid() >= 0 && !ioctl(slave, TIOCSCTTY, 0) && !tcsetpgrp(slave, getpgrp());
    write(ready[1], &observed.setup, sizeof(observed.setup));
    char byte; read(gate[0], &byte, 1);
    if (observed.setup && !sigprocmask(SIG_UNBLOCK, &set, 0)) {
      struct pollfd pending = {notify[0], POLLIN, 0}; int status;
      do { status = poll(&pending, 1, 2000); } while (status < 0 && errno == EINTR);
      if (status == 1 && (pending.revents & POLLIN)) { char signals[16]; int n = read(notify[0], signals, sizeof(signals)); if (n > 0) observed.count = n; }
    }
    struct winsize actual = {0};
    if (!ioctl(slave, TIOCGWINSZ, &actual)) { observed.cols = actual.ws_col; observed.rows = actual.ws_row; }
    write(reply[1], &observed, sizeof(observed));
    /* The parent queries the still-live controlling terminal before ack. */
    read(gate[0], &byte, 1);
    _Exit(0);
  }
  int prepared = 0, resized = 0, reaped = 0, clean = 1, response = 0, queried = 0, acknowledged = 0;
  struct winsize checked = {0};
  if (child > 0) {
    clean &= close_owned(&ready[1]); clean &= close_owned(&gate[0]); clean &= close_owned(&reply[1]);
    clean &= close_owned(&notify[0]); clean &= close_owned(&notify[1]);
    if (read(ready[0], &prepared, sizeof(prepared)) == sizeof(prepared) && prepared) {
      struct winsize middle = {.ws_row = 31, .ws_col = 90};
      resized = !ioctl(master, TIOCSWINSZ, &middle) && !ioctl(master, TIOCSWINSZ, &last);
    }
    write(gate[1], "R", 1);
    struct pollfd wait = {reply[0], POLLIN, 0};
    if (poll(&wait, 1, 3000) == 1) response = read(reply[0], &observed, sizeof(observed)) == sizeof(observed);
    if (response) { queried = !ioctl(slave, TIOCGWINSZ, &checked); acknowledged = write(gate[1], "Q", 1) == 1; }
    else kill(child, SIGKILL);
    int status = 0; reaped = waitpid(child, &status, 0) == child && WIFEXITED(status) && WEXITSTATUS(status) == 0;
  }
  for (int i = 0; i < 2; ++i) { clean &= close_owned(&ready[i]); clean &= close_owned(&gate[i]); clean &= close_owned(&reply[i]); clean &= close_owned(&notify[i]); }
  int terminal_closed = close_owned(&slave); terminal_closed &= close_owned(&master);
  char json[640];
  snprintf(json, sizeof(json), "{\"initial_cols\":80,\"initial_rows\":24,\"resize_calls\":2,\"notifications\":%d,\"latest_cols\":%d,\"latest_rows\":%d,\"queried_cols\":%d,\"queried_rows\":%d,\"controlled_foreground\":%s,\"child_reaped\":%s,\"fixture_pipes_closed\":%s,\"query_before_child_exit\":%s,\"private_terminal_closed\":%s,\"notification\":\"SIGWINCH-self-pipe\"}", observed.count, observed.cols, observed.rows, checked.ws_col, checked.ws_row, boolean(prepared), boolean(reaped), boolean(clean), boolean(response && queried && acknowledged), boolean(terminal_closed));
  result("tty.resize", initial && resized && observed.count >= 1 && observed.cols == 100 && observed.rows == 33 && queried && acknowledged && checked.ws_col == 100 && checked.ws_row == 33 && reaped && clean && terminal_closed, json);
}
struct Cancel {
  pthread_mutex_t mutex; pthread_cond_t cond; int prepared, released, control[2], endpoint, returned, endpoint_ready;
  int64_t event, console;
};
static void *wait_borrowed(void *opaque) {
  struct Cancel *c = opaque;
  pthread_mutex_lock(&c->mutex); c->prepared = 1; pthread_cond_signal(&c->cond);
  while (!c->released) pthread_cond_wait(&c->cond, &c->mutex);
  pthread_mutex_unlock(&c->mutex);
  if (IsWindows()) { int64_t handles[] = {c->event, c->console}; c->returned = WaitForMultipleObjects(2, handles, false, 5000) == 0; }
  else { struct pollfd p[] = {{c->control[0], POLLIN, 0}, {c->endpoint, POLLIN, 0}};
    int ready = poll(p, 2, 5000); c->endpoint_ready = ready > 0 && (p[1].revents & POLLIN);
    c->returned = ready > 0 && (p[0].revents & POLLIN); }
  return 0;
}
static void borrowed(struct Lease *l, int master) {
  char json[768]; struct Cancel c = {.mutex = PTHREAD_MUTEX_INITIALIZER, .cond = PTHREAD_COND_INITIALIZER, .control = {-1,-1}};
  int shared_mode = 0, shared_flags = 0, restored = 0, owner_open = 0, endpoints = 0, duplicate = -1;
  int64_t copy = 0;
  if (IsWindows()) {
    uint32_t saved = 0, checked = 0;
    int made = DuplicateHandle(-1, l->hin, -1, &copy, 0, false, 2);
    int got = GetConsoleMode(l->hin, &saved);
    shared_mode = made && got && SetConsoleMode(copy, saved ^ 4u) && GetConsoleMode(l->hin, &checked) && checked == (saved ^ 4u);
    restored = SetConsoleMode(l->hin, saved) && GetConsoleMode(copy, &checked) && checked == saved;
    FlushConsoleInputBuffer(copy);
    c.event = CreateEvent(0, true, false, 0); c.console = copy;
    endpoints = c.event != 0 && made;
  } else {
    duplicate = dup(l->fd); c.endpoint = duplicate;
    struct termios saved = {0}, changed, measured;
    int got = !tcgetattr(l->fd, &saved); changed = saved; changed.c_lflag ^= ECHO;
    shared_mode = got && !tcsetattr(duplicate, TCSANOW, &changed) && !tcgetattr(l->fd, &measured) && same(&changed, &measured);
    int flags = fcntl(l->fd, F_GETFL);
    shared_flags = flags >= 0 && !fcntl(duplicate, F_SETFL, flags | O_NONBLOCK) && (fcntl(l->fd, F_GETFL) & O_NONBLOCK);
    restored = got && flags >= 0 && !fcntl(l->fd, F_SETFL, flags) && restore_termios(l->fd, &saved);
    restored &= flags >= 0 && fcntl(l->fd, F_GETFL) == flags && fcntl(duplicate, F_GETFL) == flags;
    endpoints = !pipe(c.control);
  }
  pthread_t worker; int created = endpoints && !pthread_create(&worker, 0, wait_borrowed, &c);
  int input_ready = IsWindows(), requested = 0;
  if (created) {
    pthread_mutex_lock(&c.mutex); while (!c.prepared) pthread_cond_wait(&c.cond, &c.mutex); pthread_mutex_unlock(&c.mutex);
    if (IsWindows()) requested = SetEvent(c.event);
    else {
      /* Both sources are ready before the waiter proceeds: control must win
         without starting a read on the shared endpoint. */
      struct pollfd pending = {duplicate, POLLIN, 0};
      input_ready = write(master, "B\n", 2) == 2 && poll(&pending, 1, 1000) == 1 && (pending.revents & POLLIN);
      requested = write(c.control[1], "C", 1) == 1;
    }
    pthread_mutex_lock(&c.mutex); c.released = 1; pthread_cond_signal(&c.cond); pthread_mutex_unlock(&c.mutex);
  }
  int joined = created && !pthread_join(worker, 0);
  int clean;
  if (IsWindows()) { clean = CloseHandle(copy); clean &= CloseHandle(c.event); uint32_t checked; owner_open = GetConsoleMode(l->hin, &checked); }
  else { clean = close_owned(&duplicate); clean &= close_owned(&c.control[0]); clean &= close_owned(&c.control[1]); owner_open = fcntl(l->fd, F_GETFD) >= 0; }
  clean &= !pthread_cond_destroy(&c.cond) && !pthread_mutex_destroy(&c.mutex);
  snprintf(json, sizeof(json), "{\"duplicate_shares_mode\":%s,\"duplicate_shares_status_flags\":%s,\"shared_state_restored\":%s,\"cancel_observed\":%s,\"joined\":%s,\"owner_still_open\":%s,\"owned_wait_endpoints_closed\":%s,\"borrowed_ready_on_cancel\":%s,\"unsafe_borrow\":\"UNSUPPORTED\",\"strategy\":\"cancel-owned-wait-before-borrowed-read\",\"pending_shared_read_cancel\":\"UNSUPPORTED\"}", boolean(shared_mode), boolean(shared_flags), boolean(restored), boolean(c.returned), boolean(joined), boolean(owner_open), boolean(clean), boolean(c.endpoint_ready));
  result("tty.borrowed", shared_mode && (IsWindows() || (shared_flags && c.endpoint_ready)) && restored && input_ready && requested && c.returned && joined && owner_open && clean, json);
}
static int gate(const char *name) {
  if (!gate_directory) return 0;
  char path[4096]; snprintf(path, sizeof(path), "%s/%s.ready", gate_directory, name);
  FILE *file = fopen(path, "w"); if (!file) return 0; fputs("ready", file); fclose(file);
  snprintf(path, sizeof(path), "%s/%s.done", gate_directory, name);
  struct timespec start, current; clock_gettime(CLOCK_MONOTONIC, &start);
  do { if (!access(path, F_OK)) return 1; poll(0, 0, 1); clock_gettime(CLOCK_MONOTONIC, &current); }
  while (current.tv_sec - start.tv_sec < 10);
  return 0;
}
static void console_resize(struct Lease *l) {
  int got = !acquire(l), events = 0, cols = 0, rows = 0;
  struct NtConsoleScreenBufferInfo before = {0}, after = {0};
  int first = GetConsoleScreenBufferInfo(l->hout, &before);
  FlushConsoleInputBuffer(l->hin);
  int resized = got && gate("resize"), queried = GetConsoleScreenBufferInfo(l->hout, &after);
  uint32_t count = 0, read = 0; struct NtInputRecord input;
  while (GetNumberOfConsoleInputEvents(l->hin, &count) && count) {
    if (!ReadConsoleInput(l->hin, &input, 1, &read) || read != 1) break;
    if (input.EventType == kNtWindowBufferSizeEvent) { ++events; cols = input.Event.WindowBufferSizeEvent.dwSize.X; rows = input.Event.WindowBufferSizeEvent.dwSize.Y; }
  }
  int restored = !release(l);
  char json[512]; snprintf(json, sizeof(json), "{\"initial_cols\":%d,\"initial_rows\":%d,\"resize_calls\":2,\"notifications\":%d,\"latest_cols\":%d,\"latest_rows\":%d,\"queried_cols\":%d,\"queried_rows\":%d,\"mode_restored\":%s,\"notification\":\"WINDOW_BUFFER_SIZE_EVENT\"}", before.dwSize.X, before.dwSize.Y, events, cols, rows, after.dwSize.X, after.dwSize.Y, boolean(restored));
  result("tty.resize", got && first && before.dwSize.X == 80 && before.dwSize.Y == 24 && resized && queried && events >= 1 && cols == 100 && rows == 33 && after.dwSize.X == 100 && after.dwSize.Y == 33 && restored, json);
}
static void console_text(struct Lease *l) {
  int got = !acquire(l), input_ok = got && FlushConsoleInputBuffer(l->hin) && gate("text");
  const uint16_t expected[] = {65, 0x4e2d, 90};
  int total = 0, actual_units[5] = {0}, transport_ok = input_ok; uint32_t count, read; struct NtInputRecord event;
  while (transport_ok && total < 3) {
    if (WaitForSingleObject(l->hin, 2000) != 0 || !GetNumberOfConsoleInputEvents(l->hin, &count) || !count ||
        !ReadConsoleInput(l->hin, &event, 1, &read) || read != 1) { transport_ok = 0; input_ok = 0; break; }
    if (event.EventType == kNtKeyEvent && event.Event.KeyEvent.bKeyDown && event.Event.KeyEvent.uChar.UnicodeChar) {
      actual_units[total] = event.Event.KeyEvent.uChar.UnicodeChar;
      if (event.Event.KeyEvent.uChar.UnicodeChar != expected[total]) input_ok = 0;
      ++total;
    }
  }
  struct Decoder decoder = {0}; int output_ok = got, strict = 1; uint32_t wrote;
  for (int i = 0; i < sizeof(text) - 1; ++i) {
    strict &= decode(&decoder, text[i]);
    if (!decoder.pending) {
      unsigned cp = text[i] < 128 ? text[i] : decoder.value;
      char16_t wide[2]; int n = 1;
      if (cp > 65535) { cp -= 65536; wide[0] = 0xd800 + (cp >> 10); wide[1] = 0xdc00 + (cp & 1023); n = 2; }
      else wide[0] = cp;
      output_ok &= WriteConsole(l->hout, wide, n, &wrote, 0) && wrote == n;
    }
  }
  int restored = !release(l); char json[512];
  snprintf(json, sizeof(json), "{\"input_exact\":%s,\"output_exact\":%s,\"one_byte_chunks\":true,\"hex\":\"41e4b8adf09f99825a\",\"input_hex\":\"41e4b8ad5a\",\"unicode_scalars\":%d,\"strict_valid\":%s,\"mode_restored\":%s,\"input_utf16_units\":%d,\"input_units\":[%d,%d,%d],\"adapter\":\"strict-incremental-UTF8-to-WriteConsoleW;ConPTY-UTF8-to-BMP-key-events\"}", boolean(input_ok && total == 3), boolean(output_ok), decoder.scalars, boolean(strict && !decoder.pending), boolean(restored), total, actual_units[0],actual_units[1],actual_units[2]);
  result("text.chunks", input_ok && total == 3 && output_ok && strict && decoder.scalars == 4 && restored, json);
  got = !acquire(l); struct NtConsoleScreenBufferInfo info = {0};
  int position = GetConsoleScreenBufferInfo(l->hout, &info);
  const char16_t sgr[] = u"\033[31mX\033[0m";
  int written = WriteConsole(l->hout, sgr, sizeof(sgr) / 2 - 1, &wrote, 0) && wrote == sizeof(sgr) / 2 - 1;
  struct NtCharInfo cell = {0}; struct NtSmallRect region = {info.dwCursorPosition.X, info.dwCursorPosition.Y, info.dwCursorPosition.X, info.dwCursorPosition.Y};
  int rendered = ReadConsoleOutput(l->hout, &cell, (struct NtCoord){1,1}, (struct NtCoord){0,0}, &region);
  restored = !release(l);
  snprintf(json, sizeof(json), "{\"subset\":\"SGR-31-and-reset\",\"rendered_character\":%u,\"red_foreground\":%s,\"attributes\":%u,\"mode_restored\":%s}", cell.Char.UnicodeChar, boolean((cell.Attributes & 7) == 4), cell.Attributes, boolean(restored));
  result("text.vt", got && position && written && rendered && cell.Char.UnicodeChar == 88 && (cell.Attributes & 7) == 4 && restored, json);
}
static void console_input_boundary(struct Lease *l, int supplementary) {
  int got = !acquire(l), seen = 0, units[16] = {0}, n = 0;
  int setup = got && FlushConsoleInputBuffer(l->hin) && gate(supplementary ? "supplementary" : "invalid");
  while (setup && !seen && n < 16) {
    struct NtInputRecord event; uint32_t count = 0, read = 0;
    if (WaitForSingleObject(l->hin, 2000) != 0 || !GetNumberOfConsoleInputEvents(l->hin, &count) || !count || !ReadConsoleInput(l->hin, &event, 1, &read) || read != 1) break;
    if (event.EventType == kNtKeyEvent && event.Event.KeyEvent.bKeyDown && event.Event.KeyEvent.uChar.UnicodeChar) {
      units[n++] = event.Event.KeyEvent.uChar.UnicodeChar; seen = units[n - 1] == 81;
    }
  }
  int restored = !release(l), exact = n == 3 && units[0] == 0xd83d && units[1] == 0xde42 && units[2] == 81;
  char array[256] = "[", json[768]; int offset = 1;
  for (int i = 0; i < n; ++i) offset += snprintf(array + offset, sizeof(array) - offset, "%s%d", i ? "," : "", units[i]);
  snprintf(array + offset, sizeof(array) - offset, "]");
  snprintf(json, sizeof(json), "{\"input_hex\":\"%s\",\"observed_utf16_units\":%s,\"sentinel_seen\":%s,\"replacement_as_byte_proof\":false,\"support\":\"%s\",\"mode_restored\":%s}", supplementary ? "f09f998251" : "c0afeda08051", array, boolean(seen), supplementary && exact ? "EXACT_OBSERVED" : "UNSUPPORTED", boolean(restored));
  result(supplementary ? "text.supplementary-input" : "text.invalid-input", setup && seen && restored, json);
}
int main(int argc, char **argv) {
  evidence = stdout;
  for (int i = 1; i + 1 < argc; ++i) if (!strcmp(argv[i], "--result")) evidence = fopen(argv[i + 1], "w");
  for (int i = 1; i + 1 < argc; ++i) if (!strcmp(argv[i], "--gate")) gate_directory = argv[i + 1];
  for (int i = 1; i + 1 < argc; ++i) if (!strcmp(argv[i], "--fixture-root")) fixture_root = argv[i + 1];
  if (!evidence) return 2;
  if (argc > 1 && !strcmp(argv[1], "--hang")) for (;;) pause();
  int console = argc > 1 && !strcmp(argv[1], "--console");
  char json[1024]; struct utsname u; uname(&u);
  snprintf(json, sizeof(json), "{\"probe\":\"m2-tty\",\"os\":\"%s\",\"arch\":\"%s\",\"page_size\":%ld}", u.sysname, u.machine, sysconf(_SC_PAGESIZE));
  if (!console) result("facts", 1, json);
  snprintf(json, sizeof(json), "{\"type\":\"%s\",\"actual_fd\":0,\"private_terminal_inference\":false}", stdin_type());
  result(console ? "stdin.terminal" : "stdin.observed", 1, json);
  if (IsWindows() && !console) { stdin_routes(-1); return failures ? 1 : 0; }
  struct Lease lease = {0}; int master = -1, slave = -1;
  if (IsWindows()) {
    lease.hin = CreateFile(u"CONIN$", kNtGenericRead | kNtGenericWrite, kNtFileShareRead | kNtFileShareWrite, 0, kNtOpenExisting, 0, 0);
    lease.hout = CreateFile(u"CONOUT$", kNtGenericRead | kNtGenericWrite, kNtFileShareRead | kNtFileShareWrite, 0, kNtOpenExisting, 0, 0);
    console_stdin(lease.hin);
  } else {
    master = posix_openpt(O_RDWR | O_NOCTTY);
    if (master >= 0 && !grantpt(master) && !unlockpt(master)) slave = open(ptsname(master), O_RDWR | O_NOCTTY);
    lease.fd = slave;
  }
  if (!IsWindows()) stdin_routes(slave);
  leases(&lease);
  if (!IsWindows()) { posix_resize(); posix_text(&lease, master); }
  else { console_resize(&lease); console_text(&lease); console_input_boundary(&lease, 0); console_input_boundary(&lease, 1); }
  borrowed(&lease, master);
  invalid_text();
  binary_stream();
  int cleanup = !release(&lease);
  if (IsWindows()) cleanup &= CloseHandle(lease.hin) && CloseHandle(lease.hout);
  else cleanup &= closed(slave) && closed(master);
  result("tty.cleanup", cleanup, cleanup ? "{\"all_owned_endpoints_closed\":true,\"lease_released\":true}" : "{\"all_owned_endpoints_closed\":false}");
  if (evidence != stdout) fclose(evidence);
  return failures ? 1 : 0;
}
