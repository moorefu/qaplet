/* SPDX-License-Identifier: MIT */
/* Independent OS feasibility evidence, not a Runtime implementation. */
#define _COSMO_SOURCE 1
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <spawn.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/utsname.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>
#include "libc/dce.h"
#include "libc/nt/accounting.h"
#include "libc/nt/process.h"
#include "libc/nt/runtime.h"
#include "libc/nt/struct/startupinfoex.h"
#include "libc/nt/struct/byhandlefileinformation.h"
#include "libc/nt/synchronization.h"
#include "libc/proc/ntspawn.h"
#include "libc/str/str.h"
#include "libc/sysv/pib.h"
/* The pinned files.h has a duplicate NtSidIdentifierAuthority definition.
   Declare the supplied ABI directly; no replacement implementation. */
extern bool32 DuplicateHandle(int64_t, int64_t, int64_t, int64_t *, uint32_t, bool32, uint32_t);
extern uint32_t GetCurrentDirectory(uint32_t, char16_t *);
extern bool32 GetFileInformationByHandle(int64_t, struct NtByHandleFileInformation *);
extern intptr_t __kloghandle(void);
extern intptr_t __klog_handle;
extern int sys_close(int);

extern char **environ;
static int failures, cleanup_ok = 1, live_children;
static int startup_duplicate, startup_duplicate_closed;
static const unsigned char input[] = {'I', 0, 255, 'N', '\n'};
static const unsigned char errors[] = {'E', 0, 254, 'R'};
static const char *boolean(int value) { return value ? "true" : "false"; }
static void result(const char *id, bool pass, const char *actual) {
  printf("{\"case_id\":\"%s\",\"status\":\"%s\",\"actual\":%s}\n", id, pass ? "pass" : "fail", actual);
  fflush(stdout);
  failures += !pass;
}
static int closed(int *fd) {
  if (*fd < 0) return 1;
  int old = *fd;
  *fd = -1;
  int value = close(old);
  errno = 0;
  int checked = fcntl(old, F_GETFD);
  int ok = !value && checked == -1 && errno == EBADF;
  cleanup_ok &= ok;
  return ok;
}
static int receive(int fd, unsigned char *bytes, int capacity) {
  int total = 0;
  for (;;) {
    int got = read(fd, bytes + total, capacity - total);
    if (got < 0 && errno == EINTR) continue;
    if (got < 0) return -1;
    if (!got) return total;
    total += got;
    if (total == capacity) return -2;
  }
}
static pid_t wait_child(pid_t pid, int *status, int options) {
  pid_t value;
  do { value = waitpid(pid, status, options); } while (value < 0 && errno == EINTR);
  if (value > 0) --live_children;
  return value;
}
static int child(int argc, char **argv) {
  char cwd[4096];
  int count = 0;
  while (environ[count]) ++count;
  int oscount = count, protocol = 0;
  char16_t *os_environment = 0;
  if (IsWindows()) {
    oscount = 0;
    os_environment = GetEnvironmentStrings();
    if (os_environment) for (char16_t *p = os_environment; *p; ++oscount) {
      protocol += *p == '_';
      while (*p) ++p;
      ++p;
    }
  }
  unsigned char diagnostic[6] = {
    argc == 9 && !strcmp(argv[2], "") && !strcmp(argv[3], "参数 空格") &&
    !strcmp(argv[4], "two words") && !strcmp(argv[5], "quote\"double") &&
    !strcmp(argv[6], "slash\\tail\\") && !strcmp(argv[7], "literal&|;$()<>*") &&
    !strcmp(argv[8], "apostrophe'"),
    count == 2 && getenv("QAPLET_ONLY") && !strcmp(getenv("QAPLET_ONLY"), "完整 值") && !getenv("QAPLET_PARENT_LEAK"),
    getcwd(cwd, sizeof(cwd)) && getenv("QAPLET_CWD") && !strcmp(cwd, getenv("QAPLET_CWD")), count, oscount, protocol
  };
  unsigned char data[16];
  int amount = receive(0, data, sizeof(data));
  int a = amount == sizeof(input) ? write(1, data, amount) : -1;
  int b = write(2, diagnostic, sizeof(diagnostic));
  int c = write(2, errors, sizeof(errors));
  if (count != 2) for (int i = 0; i < count; ++i) {
    const char *end = strchr(environ[i], '=');
    if (end) write(2, environ[i], end - environ[i]);
    write(2, ",", 1);
  }
  if (count != 2) write(2, "|", 1);
  if (os_environment) {
    /* Preserve names added only to libc's environ, separately from the actual
       OS block and the protocol carriers posix_spawn supplies in that block. */
    for (int i = 0; i < count; ++i) {
      char16_t name[256];
      const char *end = strchr(environ[i], '=');
      if (!end || end - environ[i] >= 255) continue;
      for (int n = 0; n < end - environ[i]; ++n) name[n] = (unsigned char)environ[i][n];
      name[end - environ[i]] = 0;
      int found = 0;
      for (char16_t *p = os_environment; *p;) {
        int n = 0;
        while (name[n] && p[n] == name[n]) ++n;
        found |= !name[n] && p[n] == '=';
        while (*p) ++p;
        ++p;
      }
      if (!found) { write(2, environ[i], end - environ[i]); write(2, ",", 1); }
    }
    write(2, "|", 1);
    for (char16_t *p = os_environment; *p;) {
      int length = 0, found = 0;
      while (p[length] && p[length] != '=') ++length;
      for (int i = 0; i < count; ++i) {
        int n = 0;
        while (n < length && environ[i][n] && (unsigned char)environ[i][n] == p[n]) ++n;
        found |= n == length && environ[i][n] == '=';
      }
      if (!found) {
        for (int n = 0; n < length; ++n) { unsigned char c = p[n] < 128 ? p[n] : '?'; write(2, &c, 1); }
        write(2, ",", 1);
      }
      while (*p) ++p;
      ++p;
    }
    FreeEnvironmentStrings(os_environment);
  }
  return a == sizeof(input) && b == 6 && c == sizeof(errors) && !memcmp(data, input, sizeof(input)) ? 37 : 98;
}
/* The bounded fixture owns all descriptors below 256. Close every open one
   after authorized dup2, including non-CLOEXEC fixtures. This is a probe
   recipe, not a product-wide fd enumeration/close algorithm. */
static int spawn_child(const char *self, const char *launcher, const char *cwd,
                       const char *mode, int io[3][2], int policy, pid_t *pid) {
  posix_spawn_file_actions_t actions;
  int error = posix_spawn_file_actions_init(&actions);
  if (error) return error;
  if (cwd) error = posix_spawn_file_actions_addchdir_np(&actions, cwd);
  for (int i = 0; i < 3 && !error; ++i) {
    if (policy == 0) error = posix_spawn_file_actions_adddup2(&actions, io[i][i == 0 ? 0 : 1], i);
    if (policy == 2) error = posix_spawn_file_actions_addopen(&actions, i, "/dev/null", i ? O_WRONLY : O_RDONLY, 0);
  }
  for (int fd = 3; fd < 256 && !error; ++fd)
    if (fcntl(fd, F_GETFD) != -1) error = posix_spawn_file_actions_addclose(&actions, fd);
  char envcwd[8192];
  snprintf(envcwd, sizeof(envcwd), "QAPLET_CWD=%s", cwd ? cwd : "");
  char *environment[] = {"QAPLET_ONLY=完整 值", envcwd, 0};
  char *args[16];
  int n = 0;
  if (launcher) args[n++] = (char *)launcher;
  args[n++] = (char *)self;
  args[n++] = (char *)mode;
  args[n++] = "";
  args[n++] = "参数 空格";
  args[n++] = "two words";
  args[n++] = "quote\"double";
  args[n++] = "slash\\tail\\";
  args[n++] = "literal&|;$()<>*";
  args[n++] = "apostrophe'";
  args[n] = 0;
  if (!error) error = posix_spawn(pid, args[0], &actions, 0, args, environment);
  if (!error) ++live_children;
  int destroyed = posix_spawn_file_actions_destroy(&actions);
  cleanup_ok &= !destroyed;
  return error;
}
static int make_pipes(int io[3][2]) {
  for (int i = 0; i < 3; ++i) io[i][0] = io[i][1] = -1;
  for (int i = 0; i < 3; ++i) if (pipe2(io[i], O_CLOEXEC)) return 0;
  return 1;
}
static void clean_pipes(int io[3][2]) {
  for (int i = 0; i < 3; ++i) for (int j = 0; j < 2; ++j) closed(&io[i][j]);
}
static void pipe_case(const char *self, const char *launcher, const char *cwd) {
  int io[3][2], status = -1;
  pid_t pid = -1;
  int made = make_pipes(io);
  int spawned = made ? spawn_child(self, launcher, cwd, "--child", io, 0, &pid) : errno;
  closed(&io[0][0]); closed(&io[1][1]); closed(&io[2][1]);
  int wrote = !spawned ? write(io[0][1], input, sizeof(input)) : -1;
  closed(&io[0][1]);
  unsigned char out[32] = {0}, err[1024] = {0};
  int got = !spawned ? receive(io[1][0], out, sizeof(out)) : -1;
  int goterr = !spawned ? receive(io[2][0], err, sizeof(err)) : -1;
  int waited = !spawned ? wait_child(pid, &status, 0) : -1;
  char json[1024];
  bool reaped = waited == pid && pid > 0;
  char *names = goterr > 10 ? (char *)err + 10 : "", *generated = strchr(names, '|');
  if (generated) *generated++ = 0;
  char *consumed = generated ? strchr(generated, '|') : 0;
  if (consumed) *consumed++ = 0;
  snprintf(json, sizeof(json), "{\"posix_spawn\":%d,\"argv_exact\":%s,\"environment_replaced\":%s,\"cwd_exact\":%s,\"environment_count\":%u,\"os_environment_count\":%u,\"protocol_environment_count\":%u,\"environment_names\":\"%s\",\"libc_generated_names\":\"%s\",\"os_only_names\":\"%s\",\"explicit_path\":true,\"shell\":false,\"raw_wait_status\":%d,\"exit_status\":%d,\"reaped\":%s}", spawned, boolean(goterr >= 10 && err[0] == 1), boolean(goterr >= 10 && err[1] == 1), boolean(goterr >= 10 && err[2] == 1), err[3], err[4], err[5], names, generated ? generated : "", consumed ? consumed : "", status, WIFEXITED(status) ? WEXITSTATUS(status) : -1, boolean(reaped));
  bool success = !spawned && reaped && WIFEXITED(status) && WEXITSTATUS(status) == 37 && goterr >= 10 && err[0] && err[2];
  result(IsWindows() ? "process.posix-spawn-difference" : "process.argv-env-cwd", success && (IsWindows() ? !err[1] && err[3] > 2 : goterr == 10 && err[1]), json);
  bool exact = got == sizeof(input) && !memcmp(out, input, sizeof(input)) && goterr >= 10 && !memcmp(err + 6, errors, sizeof(errors));
  snprintf(json, sizeof(json), "{\"stdin_bytes\":%d,\"stdout_bytes\":%d,\"stderr_bytes\":%d,\"binary_exact\":%s,\"stdout_eof\":%s,\"stderr_eof\":%s,\"reaped\":%s}", wrote, got, goterr >= 10 ? 4 : -1, boolean(exact), boolean(got >= 0), boolean(goterr >= 0), boolean(reaped));
  result("process.pipe", wrote == 5 && exact && reaped, json);
  clean_pipes(io);
}
static void mode_cases(const char *self, const char *launcher, const char *cwd) {
  int io[3][2], saved[3] = {-1, -1, -1}, status = -1;
  int made = make_pipes(io), restored = 1;
  for (int i = 0; i < 3; ++i) {
    saved[i] = fcntl(i, F_DUPFD_CLOEXEC, 40);
    restored &= saved[i] >= 0 && dup2(io[i][i == 0 ? 0 : 1], i) == i;
  }
  pid_t pid = -1;
  int error = made && restored ? spawn_child(self, launcher, cwd, "--child", io, 1, &pid) : EIO;
  for (int i = 0; i < 3; ++i) {
    if (saved[i] >= 0) restored &= dup2(saved[i], i) == i;
    closed(&saved[i]);
  }
  cleanup_ok &= restored;
  closed(&io[0][0]); closed(&io[1][1]); closed(&io[2][1]);
  int wrote = !error ? write(io[0][1], input, 5) : -1;
  closed(&io[0][1]);
  unsigned char out[32] = {0}, err[1024] = {0};
  int got = !error ? receive(io[1][0], out, sizeof(out)) : -1;
  int goterr = !error ? receive(io[2][0], err, sizeof(err)) : -1;
  int waited = !error ? wait_child(pid, &status, 0) : -1;
  bool exact = got == 5 && !memcmp(out, input, 5) && goterr >= 10 && !memcmp(err + 6, errors, 4);
  char json[768];
  snprintf(json, sizeof(json), "{\"posix_spawn\":%d,\"authorized_stdio\":3,\"stdin_bytes\":%d,\"stdout_bytes\":%d,\"binary_exact\":%s,\"parent_stdio_restored\":%s,\"reaped\":%s,\"raw_wait_status\":%d}", error, wrote, got, boolean(exact), boolean(restored), boolean(waited == pid && pid > 0), status);
  result("process.inherit", !error && wrote == 5 && exact && restored && waited == pid && WIFEXITED(status) && WEXITSTATUS(status) == 37, json);
  clean_pipes(io);
  pid = -1; status = -1;
  error = spawn_child(self, launcher, cwd, "--ignore", io, 2, &pid);
  waited = !error ? wait_child(pid, &status, 0) : -1;
  snprintf(json, sizeof(json), "{\"posix_spawn\":%d,\"exit_status\":%d,\"stdin_eof\":%s,\"output_discarded\":%s,\"managed_pipe_count\":0,\"reaped\":%s,\"raw_wait_status\":%d}", error, WIFEXITED(status) ? WEXITSTATUS(status) : -1, boolean(WIFEXITED(status) && WEXITSTATUS(status) == 43), boolean(WIFEXITED(status) && WEXITSTATUS(status) == 43), boolean(waited == pid && pid > 0), status);
  result("process.ignore", !error && waited == pid && WIFEXITED(status) && WEXITSTATUS(status) == 43, json);
}
static void state_case(const char *self, const char *launcher, const char *cwd, int kind) {
  int io[3][2], status = -1;
  int made = make_pipes(io);
  pid_t pid = -1;
  const char *mode = kind == 0 ? "--eof" : kind == 1 ? "--exit" : "--ready-hang";
  int error = made ? spawn_child(self, launcher, cwd, mode, io, 0, &pid) : EIO;
  closed(&io[0][0]); closed(&io[1][1]); closed(&io[2][1]);
  unsigned char out[32] = {0}, err[32] = {0};
  int got = -1, goterr = -1, running = 0, killed = -1, waited = -1, after = -2, after_error = 0;
  bool reaped_before_drain = false;
  if (!error && kind == 0) {
    got = receive(io[1][0], out, sizeof(out));
    goterr = receive(io[2][0], err, sizeof(err));
    running = wait_child(pid, &status, WNOHANG) == 0;
    write(io[0][1], "G", 1);
    closed(&io[0][1]);
    waited = wait_child(pid, &status, 0);
  } else if (!error && kind == 1) {
    waited = wait_child(pid, &status, 0);
    reaped_before_drain = waited == pid;
    got = receive(io[1][0], out, sizeof(out));
    goterr = receive(io[2][0], err, sizeof(err));
  } else if (!error) {
    got = read(io[1][0], out, 1);
    running = got == 1 && out[0] == 'R' && wait_child(pid, &status, WNOHANG) == 0;
    killed = kill(pid, SIGKILL);
    waited = wait_child(pid, &status, 0);
    errno = 0;
    after = wait_child(pid, &status, WNOHANG);
    after_error = errno;
  }
  char json[1024];
  if (kind == 0) {
    snprintf(json, sizeof(json), "{\"posix_spawn\":%d,\"stdout_eof\":%s,\"stderr_eof\":%s,\"running_after_eof\":%s,\"gate_released\":true,\"exit_status\":%d,\"reaped\":%s,\"raw_wait_status\":%d}", error, boolean(got == 0), boolean(goterr == 0), boolean(running), WIFEXITED(status) ? WEXITSTATUS(status) : -1, boolean(waited == pid && pid > 0), status);
    result("process.eof-before-exit", !error && !got && !goterr && running && waited == pid && WIFEXITED(status) && WEXITSTATUS(status) == 41, json);
  } else if (kind == 1) {
    snprintf(json, sizeof(json), "{\"posix_spawn\":%d,\"reaped_before_drain\":%s,\"stdout_after_reap\":%d,\"stderr_after_reap\":%d,\"stdout_eof\":%s,\"stderr_eof\":%s,\"exit_status\":%d,\"raw_wait_status\":%d}", error, boolean(reaped_before_drain), got, goterr, boolean(got >= 0), boolean(goterr >= 0), WIFEXITED(status) ? WEXITSTATUS(status) : -1, status);
    result("process.exit-before-eof", !error && reaped_before_drain && got == 4 && goterr == 4 && !memcmp(out, "late", 4) && !memcmp(err, errors, 4) && WIFEXITED(status) && WEXITSTATUS(status) == 42, json);
  } else {
    snprintf(json, sizeof(json), "{\"posix_spawn\":%d,\"ready_gate\":%s,\"observed_running\":%s,\"kill\":%d,\"reaped\":%s,\"raw_wait_status\":%d,\"native_exited\":%s,\"native_exit_status\":%d,\"native_signaled\":%s,\"native_signal\":%d,\"repeat_wait\":%d,\"repeat_wait_errno\":%d,\"repeat_wait_echild\":%s,\"direct_child_only\":true}", error, boolean(got == 1 && out[0] == 'R'), boolean(running), killed, boolean(waited == pid && pid > 0), status, boolean(WIFEXITED(status)), WIFEXITED(status) ? WEXITSTATUS(status) : -1, boolean(WIFSIGNALED(status)), WIFSIGNALED(status) ? WTERMSIG(status) : 0, after, after_error, boolean(after == -1 && after_error == ECHILD));
    result("process.kill-reap", !error && running && killed == 0 && waited == pid && after == -1 && after_error == ECHILD, json);
  }
  clean_pipes(io);
}
static void failure_cases(const char *self, const char *launcher, const char *cwd) {
  int io[3][2], made = make_pipes(io);
  for (int kind = 0; kind < 2; ++kind) {
    pid_t pid = -1;
    char missing[8192], json[768];
    snprintf(missing, sizeof(missing), "%s/absent-do-not-create", cwd);
    /* Missing executable uses no launcher: the missing path is the actual
       posix_spawn path, never an argument for a successfully launched loader. */
    int error = made ? spawn_child(kind ? self : missing, kind ? launcher : 0,
                                  kind ? missing : cwd, "--ignore", io, 2, &pid) : EIO;
    int status = -1, waited = -1, killed = -1;
    if (!error && pid > 0) { killed = kill(pid, SIGKILL); waited = wait_child(pid, &status, 0); }
    snprintf(json, sizeof(json), "{\"posix_spawn\":%d,\"expected_errno\":%d,\"pid\":%d,\"no_child\":%s,\"unexpected_child_kill\":%d,\"unexpected_child_wait\":%d,\"raw_wait_status\":%d}", error, ENOENT, pid, boolean(error == ENOENT && pid == -1), killed, waited, status);
    result(kind ? "process.bad-cwd" : "process.missing", error == ENOENT && pid == -1, json);
  }
  clean_pipes(io);
}
static void inheritance_case(const char *self, const char *launcher, const char *cwd) {
  int io[3][2], waker[2] = {-1, -1}, file = -1, extra = -1, sock = -1;
  char path[8192], json[1024];
  snprintf(path, sizeof(path), "%s/private-file", cwd);
  int made = make_pipes(io) && !pipe(waker);
  file = open(path, O_CREAT | O_TRUNC | O_RDWR, 0600);
  extra = file >= 0 ? fcntl(file, F_DUPFD, 197) : -1;
  sock = socket(AF_INET, SOCK_STREAM, 0);
  pid_t pid = -1;
  int error = made && file >= 0 && extra >= 0 && sock >= 0 ? spawn_child(self, launcher, cwd, "--fd-check", io, 0, &pid) : EIO;
  closed(&io[0][0]); closed(&io[1][1]); closed(&io[2][1]);
  unsigned char observed[3] = {255, 0, 0}, byte = 0;
  int got = !error ? read(io[1][0], observed, 3) : -1;
  int status = -1, running = !error && got == 3 && wait_child(pid, &status, WNOHANG) == 0;
  closed(&waker[1]);
  int eof = made ? read(waker[0], &byte, 1) : -1;
  if (!error) write(io[0][1], "G", 1);
  closed(&io[0][1]);
  int waited = !error ? wait_child(pid, &status, 0) : -1;
  snprintf(json, sizeof(json), "{\"posix_spawn\":%d,\"fixture_file\":%s,\"fixture_socket\":%s,\"fixture_extra_fd\":%d,\"waker_kind\":\"pipe\",\"waker_eof_while_child_running\":%s,\"child_observed_running\":%s,\"unexpected_descriptors\":%u,\"cosmo_startup_trace_duplicate_observed\":%s,\"cosmo_startup_trace_duplicate_closed_or_absent\":%s,\"authorized_stdio\":3,\"reaped\":%s,\"raw_wait_status\":%d}", error, boolean(file >= 0), boolean(sock >= 0), extra, boolean(eof == 0 && running), boolean(running), observed[0], boolean(observed[1]), boolean(observed[2]), boolean(waited == pid && pid > 0), status);
  result("process.inheritance", !error && running && eof == 0 && observed[0] == 0 && observed[2] && waited == pid && WIFEXITED(status) && WEXITSTATUS(status) == 44, json);
  clean_pipes(io); closed(&waker[0]); closed(&waker[1]); closed(&file); closed(&extra); closed(&sock);
  cleanup_ok &= !unlink(path);
}
static int copy_file(const char *from, const char *to) {
  int inputfd = open(from, O_RDONLY | O_CLOEXEC), outputfd = open(to, O_CREAT | O_TRUNC | O_WRONLY | O_CLOEXEC, 0700);
  unsigned char data[8192], check[8192];
  int ok = inputfd >= 0 && outputfd >= 0, got;
  while (ok && (got = read(inputfd, data, sizeof(data))) > 0) ok = write(outputfd, data, got) == got;
  closed(&outputfd);
  lseek(inputfd, 0, SEEK_SET);
  outputfd = open(to, O_RDONLY | O_CLOEXEC);
  while (ok && (got = read(inputfd, data, sizeof(data))) > 0) ok = read(outputfd, check, got) == got && !memcmp(data, check, got);
  if (ok) ok = read(outputfd, check, 1) == 0;
  closed(&inputfd); closed(&outputfd);
  return ok;
}
static size_t u16len(const char16_t *s) { size_t n = 0; while (s[n]) ++n; return n; }
static void windows_inheritance_control(char *const args[], char16_t *app, char16_t *cwd, char16_t *environment, int64_t sentinel) {
  struct NtStartupInfoEx startup = {0};
  struct NtProcessInformation process = {0};
  startup.StartupInfo.cb = sizeof(startup);
  size_t size = 0;
  InitializeProcThreadAttributeList(0, 1, 0, &size);
  startup.lpAttributeList = malloc(size);
  int initialized = startup.lpAttributeList && InitializeProcThreadAttributeList(startup.lpAttributeList, 1, 0, &size);
  int listed = initialized && UpdateProcThreadAttribute(startup.lpAttributeList, 0, kNtProcThreadAttributeHandleList, &sentinel, sizeof(sentinel), 0, 0);
  char16_t command[8192];
  mkntcmdline(command, args, 8192);
  int created = listed && CreateProcess(app, command, 0, 0, true, 0x80000 | 0x400 | 0x08000000, environment, cwd, &startup.StartupInfo, &process);
  unsigned error = created ? 0 : GetLastError();
  if (created) ++live_children;
  if (initialized) DeleteProcThreadAttributeList(startup.lpAttributeList);
  free(startup.lpAttributeList);
  uint32_t waited = created ? WaitForSingleObject(process.hProcess, 10000) : -1, status = -1;
  if (created && waited == 0) --live_children;
  int queried = created && waited == 0 && GetExitCodeProcess(process.hProcess, &status);
  if (created && waited != 0) { TerminateProcess(process.hProcess, 99); WaitForSingleObject(process.hProcess, 10000); }
  bool released = (!process.hThread || CloseHandle(process.hThread)) && (!process.hProcess || CloseHandle(process.hProcess));
  cleanup_ok &= released;
  char json[512];
  snprintf(json, sizeof(json), "{\"CreateProcess\":%s,\"winerror\":%u,\"deliberately_inherited_sentinel\":true,\"sentinel_identity_detected\":%s,\"native_exit_code\":%u,\"native_wait\":%u,\"process_handles_closed\":%s}", boolean(created), error, boolean(queried && status == 44), status, waited, boolean(released));
  result("process.native-inheritance-control", created && queried && status == 44 && released, json);
}
static void windows_kill(const char *program, char16_t *app, char16_t *cwd, char16_t *environment) {
  int io[3][2];
  int made = make_pipes(io), duplicated = 1;
  int64_t handles[3] = {0};
  struct NtStartupInfoEx startup = {0};
  struct NtProcessInformation process = {0};
  for (int i = 0; i < 3; ++i) {
    int fd = io[i][i == 0 ? 0 : 1];
    duplicated &= fd >= 0 && DuplicateHandle(-1, __get_pib()->fds.p[fd].handle, -1, &handles[i], 0, true, 2);
    startup.StartupInfo.stdiofds[i] = handles[i];
  }
  startup.StartupInfo.cb = sizeof(startup);
  startup.StartupInfo.dwFlags = 0x100;
  size_t size = 0;
  InitializeProcThreadAttributeList(0, 1, 0, &size);
  startup.lpAttributeList = malloc(size);
  int initialized = startup.lpAttributeList && InitializeProcThreadAttributeList(startup.lpAttributeList, 1, 0, &size);
  int listed = initialized && UpdateProcThreadAttribute(startup.lpAttributeList, 0, kNtProcThreadAttributeHandleList, handles, sizeof(handles), 0, 0);
  char16_t command[8192];
  char *args[] = {(char *)program, "--ready-hang", 0};
  mkntcmdline(command, args, 8192);
  int created = made && duplicated && listed && CreateProcess(app, command, 0, 0, true, 0x80000 | 0x400 | 0x08000000, environment, cwd, &startup.StartupInfo, &process);
  unsigned create_error = created ? 0 : GetLastError();
  if (created) ++live_children;
  if (initialized) DeleteProcThreadAttributeList(startup.lpAttributeList);
  free(startup.lpAttributeList);
  for (int i = 0; i < 3; ++i) if (handles[i]) cleanup_ok &= CloseHandle(handles[i]);
  closed(&io[0][0]); closed(&io[1][1]); closed(&io[2][1]);
  unsigned char byte = 0;
  int ready = created ? read(io[1][0], &byte, 1) : -1;
  uint32_t before = created ? WaitForSingleObject(process.hProcess, 0) : -1;
  int killed = created && TerminateProcess(process.hProcess, 0xD00D);
  uint32_t waited = created ? WaitForSingleObject(process.hProcess, 10000) : -1, status = -1;
  int queried = created && waited == 0 && GetExitCodeProcess(process.hProcess, &status);
  if (created && waited == 0) --live_children;
  bool released = (!process.hThread || CloseHandle(process.hThread)) && (!process.hProcess || CloseHandle(process.hProcess));
  cleanup_ok &= released;
  uint32_t after = created ? WaitForSingleObject(process.hProcess, 0) : 0;
  unsigned after_error = GetLastError();
  char json[1024];
  snprintf(json, sizeof(json), "{\"CreateProcess\":%s,\"create_winerror\":%u,\"ready_gate\":%s,\"observed_running\":%s,\"TerminateProcess\":%s,\"native_exit_code\":%u,\"native_wait\":%u,\"reaped\":%s,\"process_handles_closed\":%s,\"after_close_wait\":%u,\"after_close_winerror\":%u,\"direct_child_only\":true}", boolean(created), create_error, boolean(ready == 1 && byte == 'R'), boolean(before == 258), boolean(killed), status, waited, boolean(queried && waited == 0), boolean(released), after, after_error);
  result("process.native-kill-reap", created && ready == 1 && byte == 'R' && before == 258 && killed && queried && status == 0xD00D && released && after == -1u && after_error == 6, json);
  clean_pipes(io);
  struct NtStartupInfo plain = {.cb = sizeof(plain)};
  process = (struct NtProcessInformation){0};
  char16_t absent[8192];
  size_t length = u16len(app);
  memcpy(absent, app, length * 2);
  memcpy(absent + length, u".absent", 8 * 2);
  mkntcmdline(command, args, 8192);
  int missing = CreateProcess(absent, command, 0, 0, false, 0x400 | 0x08000000, environment, cwd, &plain, &process);
  unsigned missing_error = GetLastError();
  bool no_child = !missing && !process.hProcess && !process.hThread;
  if (missing) { TerminateProcess(process.hProcess, 99); WaitForSingleObject(process.hProcess, 10000); CloseHandle(process.hThread); CloseHandle(process.hProcess); }
  snprintf(json, sizeof(json), "{\"CreateProcess\":%s,\"winerror\":%u,\"no_child\":%s}", boolean(missing), missing_error, boolean(no_child));
  result("process.native-create-failure", no_child && (missing_error == 2 || missing_error == 3), json);
}
static void windows_native(const char *program, const char *cwd) {
  int io[3][2];
  int64_t handles[3] = {0}, sentinel = 0;
  char16_t widecwd[4096], oldcwd[4096], app[4096], command[8192], block[8192];
  char rawcwd[8192], envcwd[8192], handlearg[128], path[4096], sentinelpath[8192];
  GetCurrentDirectory(4096, oldcwd);
  int moved = chdir(cwd);
  uint32_t cwdlength = GetCurrentDirectory(4096, widecwd);
  tprecode16to8(rawcwd, sizeof(rawcwd), widecwd);
  /* Restore via the native path before collecting any observations. */
  char restore[8192];
  tprecode16to8(restore, sizeof(restore), oldcwd);
  cleanup_ok &= !chdir(restore);
  snprintf(envcwd, sizeof(envcwd), "QAPLET_CWD=%s", rawcwd);
  size_t offset = u16len(u"QAPLET_ONLY=完整 值") + 1;
  memcpy(block, u"QAPLET_ONLY=完整 值", offset * 2);
  tprecode8to16(block + offset, 8192 - offset - 1, envcwd);
  offset += u16len(block + offset) + 1;
  block[offset] = 0;
  int made = make_pipes(io), duplicated = 1;
  for (int i = 0; i < 3; ++i) {
    int fd = io[i][i == 0 ? 0 : 1];
    /* Pinned-library fixture adapter only: obtain the real pipe HANDLE and
       duplicate it before supplying the explicit native inheritance list. */
    duplicated &= fd >= 0 && DuplicateHandle(-1, __get_pib()->fds.p[fd].handle, -1, &handles[i], 0, true, 2);
  }
  snprintf(sentinelpath, sizeof(sentinelpath), "%s/sentinel-file", cwd);
  int sentinelfd = open(sentinelpath, O_CREAT | O_TRUNC | O_RDWR | O_CLOEXEC, 0600);
  duplicated &= sentinelfd >= 0 && DuplicateHandle(-1, __get_pib()->fds.p[sentinelfd].handle, -1, &sentinel, 0, true, 2);
  struct NtByHandleFileInformation sentinelinfo = {0};
  duplicated &= GetFileInformationByHandle(sentinel, &sentinelinfo);
  snprintf(handlearg, sizeof(handlearg), "%lld:%u:%u:%u", (long long)sentinel, sentinelinfo.dwVolumeSerialNumber, sentinelinfo.nFileIndexHigh, sentinelinfo.nFileIndexLow);
  snprintf(path, sizeof(path), "%s\\原生 子程序.exe", rawcwd);
  char *args[] = {path, "--child", "", "参数 空格", "two words", "quote\"double",
                  "slash\\tail\\", "literal&|;$()<>*", "apostrophe'", handlearg, 0};
  mkntcmdline(command, args, 8192);
  tprecode8to16(app, 4096, path);
  struct NtStartupInfoEx startup = {0};
  struct NtProcessInformation process = {0};
  startup.StartupInfo.cb = sizeof(startup);
  startup.StartupInfo.dwFlags = 0x100;
  for (int i = 0; i < 3; ++i) startup.StartupInfo.stdiofds[i] = handles[i];
  size_t size = 0;
  InitializeProcThreadAttributeList(0, 1, 0, &size);
  startup.lpAttributeList = malloc(size);
  int initialized = startup.lpAttributeList && InitializeProcThreadAttributeList(startup.lpAttributeList, 1, 0, &size);
  int listed = initialized && UpdateProcThreadAttribute(startup.lpAttributeList, 0, kNtProcThreadAttributeHandleList, handles, sizeof(handles), 0, 0);
  int created = !moved && cwdlength && made && duplicated && listed && program &&
      CreateProcess(app, command, 0, 0, true, 0x80000 | 0x400 | 0x08000000, block, widecwd, &startup.StartupInfo, &process);
  unsigned create_error = created ? 0 : GetLastError();
  if (created) ++live_children;

  if (initialized) DeleteProcThreadAttributeList(startup.lpAttributeList);
  free(startup.lpAttributeList);
  for (int i = 0; i < 3; ++i) if (handles[i]) cleanup_ok &= CloseHandle(handles[i]);
  closed(&io[0][0]); closed(&io[1][1]); closed(&io[2][1]);
  int wrote = created ? write(io[0][1], input, 5) : -1;
  closed(&io[0][1]);
  unsigned char out[32] = {0}, err[32] = {0};
  int got = created ? receive(io[1][0], out, sizeof(out)) : -1;
  int goterr = created ? receive(io[2][0], err, sizeof(err)) : -1;
  uint32_t waited = created ? WaitForSingleObject(process.hProcess, 10000) : -1, status = -1;
  int queried = created && waited == 0 && GetExitCodeProcess(process.hProcess, &status);
  if (created && waited == 0) --live_children;
  if (created && waited != 0) {
    TerminateProcess(process.hProcess, 99);
    WaitForSingleObject(process.hProcess, 10000);
  }
  bool reaped = created && waited == 0 && queried;
  bool handles_closed = (!process.hThread || CloseHandle(process.hThread)) && (!process.hProcess || CloseHandle(process.hProcess));
  cleanup_ok &= handles_closed;
  char json[1024];
  bool exact = created && reaped && status == 37 && goterr == 10 && err[0] && err[1] && err[2] && err[3] == 2 && err[4];
  snprintf(json, sizeof(json), "{\"CreateProcess\":%s,\"winerror\":%u,\"argv_exact\":%s,\"environment_replaced\":%s,\"cwd_exact\":%s,\"environment_count\":%u,\"explicit_path\":true,\"shell\":false,\"exit_status\":%u,\"raw_wait_status\":%u,\"reaped\":%s}", boolean(created), create_error, boolean(err[0]), boolean(err[1]), boolean(err[2]), err[3], status, waited, boolean(reaped));
  result("process.argv-env-cwd", exact, json);
  snprintf(json, sizeof(json), "{\"application_environment_exact\":%s,\"os_environment_count\":%u,\"libc_startup\":false,\"explicit_handle_count\":3,\"sentinel_inherited\":%s,\"sentinel_numeric_reused\":%s,\"sentinel_identity\":\"volume+file-index\",\"stdin_bytes\":%d,\"stdout_bytes\":%d,\"stderr_bytes\":%d,\"binary_exact\":%s,\"process_handles_closed\":%s}", boolean(err[1]), err[3], boolean(!err[4]), boolean(err[5]), wrote, got, goterr >= 6 ? goterr - 6 : -1, boolean(got == 5 && !memcmp(out, input, 5) && goterr == 10 && !memcmp(err + 6, errors, 4)), boolean(handles_closed));
  result("process.native-environment", exact && wrote == 5 && got == 5 && !memcmp(out, input, 5) && !memcmp(err + 6, errors, 4) && handles_closed, json);
  clean_pipes(io);
  args[1] = "--sentinel-check";
  windows_inheritance_control(args, app, widecwd, block, sentinel);
  if (sentinel) cleanup_ok &= CloseHandle(sentinel);
  closed(&sentinelfd);
  cleanup_ok &= !unlink(sentinelpath);
  windows_kill(path, app, widecwd, block);
}
int main(int argc, char **argv) {
  signal(SIGPIPE, SIG_IGN);
  if (argc >= 2 && strcmp(argv[1], "--suite")) {
    /* Cosmopolitan creates a trace stderr duplicate at startup, after the
       parent's close actions. The controlled child explicitly releases this
       startup-owned duplicate before declaring its stdio endpoints closed. */
    intptr_t trace = __kloghandle();
    startup_duplicate = trace > 2;
    startup_duplicate_closed = !startup_duplicate || (IsWindows() ? CloseHandle(trace) : !sys_close(trace));
    __klog_handle = -1;
  }
  if (argc >= 2 && !strcmp(argv[1], "--child")) return child(argc, argv);
  if (argc >= 2 && !strcmp(argv[1], "--hang")) for (;;) pause();
  if (argc >= 2 && !strcmp(argv[1], "--ignore")) {
    unsigned char byte;
    return read(0, &byte, 1) == 0 && write(1, input, 5) == 5 && write(2, errors, 4) == 4 ? 43 : 98;
  }
  if (argc >= 2 && !strcmp(argv[1], "--eof")) {
    close(1); close(2);
    unsigned char byte;
    return read(0, &byte, 1) == 1 && byte == 'G' ? 41 : 98;
  }
  if (argc >= 2 && !strcmp(argv[1], "--exit")) return write(1, "late", 4) == 4 && write(2, errors, 4) == 4 ? 42 : 98;
  if (argc >= 2 && !strcmp(argv[1], "--ready-hang")) { unsigned char byte; write(1, "R", 1); read(0, &byte, 1); return 98; }
  if (argc >= 2 && !strcmp(argv[1], "--fd-check")) {
    unsigned char count = 0, gate;
    for (int fd = 3; fd < 256; ++fd) count += fcntl(fd, F_GETFD) != -1;
    unsigned char record[] = {count, startup_duplicate, startup_duplicate_closed};
    write(1, record, sizeof(record));
    return read(0, &gate, 1) == 1 && gate == 'G' && count == 0 ? 44 : 98;
  }
  struct utsname host;
  uname(&host);
#ifdef __aarch64__
  const char *arch = "aarch64";
#else
  const char *arch = "x86_64";
#endif
  printf("{\"case_id\":\"facts\",\"status\":\"pass\",\"actual\":{\"probe\":\"m2-process\",\"os\":\"%s\",\"arch\":\"%s\",\"page_size\":%ld}}\n", host.sysname, arch, sysconf(_SC_PAGESIZE));
  fflush(stdout);
  char temporary[] = "/tmp/qaplet-m2-process-XXXXXX", cwd[4096], original[4096], self[8192], nativecopy[8192];
  char *directory = mkdtemp(temporary);
  const char *launcher = 0, *native_child = 0;
  for (int i = 2; i + 1 < argc; i += 2) {
    if (!strcmp(argv[i], "--launcher")) launcher = argv[i + 1];
    if (!strcmp(argv[i], "--native-child")) native_child = argv[i + 1];
  }
  if (!directory || !realpath(directory, cwd) || !realpath(argv[0], original)) return 3;
  char unicodecwd[8192];
  snprintf(unicodecwd, sizeof(unicodecwd), "%s/工作 目录", cwd);
  if (mkdir(unicodecwd, 0700) || !realpath(unicodecwd, cwd)) return 3;
  snprintf(self, sizeof(self), "%s/程序 空格.com", cwd);
  int copied = copy_file(original, self);
  char json[512];
  snprintf(json, sizeof(json), "{\"same_bytes\":%s,\"unicode_space_executable\":true,\"unicode_space_cwd\":true}", boolean(copied));
  result("process.path", copied, json);
  if (!copied) return 3;
  setenv("QAPLET_PARENT_LEAK", "must not reach child", 1);
  pipe_case(self, launcher, cwd);
  if (IsWindows()) {
    snprintf(nativecopy, sizeof(nativecopy), "%s/原生 子程序.exe", cwd);
    int copied_native = native_child && copy_file(native_child, nativecopy);
    if (copied_native) windows_native(nativecopy, cwd);
    else result("process.native-environment", false, "{\"fixture_missing\":true}");
    if (copied_native) cleanup_ok &= !unlink(nativecopy);
  }
  mode_cases(self, launcher, cwd);
  state_case(self, launcher, cwd, 0);
  state_case(self, launcher, cwd, 1);
  inheritance_case(self, launcher, cwd);
  state_case(self, launcher, cwd, 2);
  failure_cases(self, launcher, cwd);
  cleanup_ok &= !unlink(self);
  cleanup_ok &= !rmdir(cwd);
  cleanup_ok &= !rmdir(directory);
  snprintf(json, sizeof(json), "{\"all_descriptors_closed\":%s,\"temporary_files_removed\":%s,\"live_direct_children\":%d}", boolean(cleanup_ok), boolean(cleanup_ok), live_children);
  result("process.cleanup", cleanup_ok && live_children == 0, json);
  return failures || !cleanup_ok ? 1 : 0;
}
