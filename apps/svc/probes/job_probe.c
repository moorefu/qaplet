/* SPDX-License-Identifier: MIT
 * Application-private M0 Job probe. Only newly created fixtures are assigned.
 * No callbacks or foreign threads enter Cosmopolitan/JavaScript. */
#define _COSMO_SOURCE 1
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "libc/dce.h"
#if defined(__x86_64__)
#include "libc/nt/dll.h"
#include "libc/nt/runtime.h"
#include "libc/nt/struct/startupinfo.h"
#include "libc/nt/struct/processinformation.h"
#include "libc/nt/thunk/msabi.h"
typedef int64_t H;
struct Limits {
  int64_t user_time, job_time;
  uint32_t flags;
  uint64_t minimum_ws, maximum_ws;
  uint32_t active_limit;
  uint64_t affinity;
  uint32_t priority, scheduling;
};
struct Extended { struct Limits basic; uint64_t io[6], memory[4]; };
struct Accounting {
  int64_t user, kernel, period_user, period_kernel;
  uint32_t faults, total, active, terminated;
};
struct Pids { uint32_t assigned, listed; uint64_t pid[8]; };
_Static_assert(sizeof(struct Limits) == 64, "JOBOBJECT_BASIC_LIMIT_INFORMATION");
_Static_assert(offsetof(struct Limits, flags) == 16, "LimitFlags offset");
_Static_assert(offsetof(struct Limits, minimum_ws) == 24, "MinimumWorkingSetSize offset");
_Static_assert(offsetof(struct Limits, affinity) == 48, "Affinity offset");
_Static_assert(sizeof(struct Extended) == 144, "JOBOBJECT_EXTENDED_LIMIT_INFORMATION");
_Static_assert(offsetof(struct Extended, io) == 64, "IO_COUNTERS offset");
_Static_assert(offsetof(struct Extended, memory) == 112, "memory limits offset");
_Static_assert(sizeof(struct Accounting) == 48, "JOBOBJECT_BASIC_ACCOUNTING_INFORMATION");
_Static_assert(offsetof(struct Pids, pid) == 8, "JOBOBJECT_BASIC_PROCESS_ID_LIST");
_Static_assert(sizeof(struct NtStartupInfo) == 104, "STARTUPINFOW");
_Static_assert(sizeof(struct NtProcessInformation) == 24, "PROCESS_INFORMATION");
struct API {
  H (__msabi *create_job)(void *, const char16_t *);
  int32_t (__msabi *assign)(H, H);
  int32_t (__msabi *in_job)(H, H, int32_t *);
  int32_t (__msabi *set)(H, int32_t, const void *, uint32_t);
  int32_t (__msabi *query)(H, int32_t, void *, uint32_t, uint32_t *);
  int32_t (__msabi *terminate_job)(H, uint32_t);
  int32_t (__msabi *create_process)(const char16_t *, char16_t *, void *, void *, int32_t,
                                  uint32_t, void *, const char16_t *, struct NtStartupInfo *, struct NtProcessInformation *);
  uint32_t (__msabi *resume)(H);
  uint32_t (__msabi *wait)(H, uint32_t);
  int32_t (__msabi *terminate_process)(H, uint32_t);
  H (__msabi *open_process)(uint32_t, int32_t, uint32_t);
  int32_t (__msabi *close)(H);
  int32_t (__msabi *duplicate)(H, H, H, H *, uint32_t, int32_t, uint32_t);
  int32_t (__msabi *handle_info)(H, uint32_t *);
  int32_t (__msabi *handle_count)(H, uint32_t *);
  uint32_t (__msabi *module_name)(H, char16_t *, uint32_t);
  void (__msabi *sleep)(uint32_t);
  void (__msabi *exit_process)(uint32_t);
  int32_t (__msabi *exit_code)(H, uint32_t *);
  uint32_t (__msabi *error)(void);
} api;
static unsigned opened, closed, failures;
static H kernel;
static char16_t executable[32768];
static void result(const char *id, int pass, const char *actual) {
  printf("{\"case_id\":\"%s\",\"status\":\"%s\",\"actual\":%s}\n", id, pass ? "pass" : "fail", actual);
  fflush(stdout);
  if (!pass) ++failures;
}
static int resolve(int missing) {
  kernel = LoadLibraryA("kernel32.dll");
  if (!kernel) return 0;
#define LOAD(field, name) do { api.field = (__typeof__(api.field))GetProcAddress(kernel, name); if (!api.field) return 0; } while (0)
  LOAD(create_job, "CreateJobObjectW"); LOAD(assign, "AssignProcessToJobObject");
  LOAD(in_job, "IsProcessInJob"); LOAD(set, "SetInformationJobObject");
  LOAD(query, "QueryInformationJobObject"); LOAD(terminate_job, "TerminateJobObject");
  LOAD(create_process, "CreateProcessW"); LOAD(resume, "ResumeThread");
  LOAD(wait, "WaitForSingleObject"); LOAD(terminate_process, "TerminateProcess");
  LOAD(open_process, "OpenProcess"); LOAD(close, "CloseHandle"); LOAD(duplicate, "DuplicateHandle");
  LOAD(handle_info, "GetHandleInformation"); LOAD(handle_count, "GetProcessHandleCount");
  LOAD(module_name, "GetModuleFileNameW"); LOAD(sleep, "Sleep"); LOAD(exit_process, "ExitProcess");
  LOAD(exit_code, "GetExitCodeProcess"); LOAD(error, "GetLastError");
#undef LOAD
  if (missing) {
    void *p = GetProcAddress(kernel, "QapletDeliberatelyMissingJobExport");
    uint32_t error = api.error();
    char actual[128];
    snprintf(actual, sizeof(actual), "{\"resolved\":%d,\"winerror\":%u}", p != 0, error);
    result("job.missing-export", !p && error == 127, actual);
    return 2;
  }
  return 1;
}
static H own(H handle) { if (handle && handle != -1) ++opened; return handle; }
static int release(H *handle) {
  if (!*handle) return 1;
  int ok = api.close(*handle);
  if (ok) { ++closed; *handle = 0; }
  return ok;
}
static H job(void) {
  H handle = own(api.create_job(0, 0)); /* unnamed, non-inheritable, exclusively owned */
  if (!handle) return 0;
  struct Extended limits = {0}; limits.basic.flags = 0x2000;
  struct Extended observed = {0};
  uint32_t flags = 0;
  if (!api.set(handle, 9, &limits, sizeof(limits)) ||
      !api.query(handle, 9, &observed, sizeof(observed), 0) ||
      observed.basic.flags != 0x2000 || !api.handle_info(handle, &flags) || (flags & 1)) {
    release(&handle); return 0;
  }
  return handle;
}
static int spawn(const char *mode, struct NtProcessInformation *p) {
  char16_t command[32768]; size_t n = 0;
  command[n++] = '"';
  for (size_t i = 0; executable[i] && n < 32700; ++i) command[n++] = executable[i];
  command[n++] = '"'; command[n++] = ' ';
  for (size_t i = 0; mode[i]; ++i) command[n++] = (unsigned char)mode[i];
  command[n] = 0;
  struct NtStartupInfo startup = {0}; startup.cb = sizeof(startup);
  memset(p, 0, sizeof(*p));
  if (!api.create_process(executable, command, 0, 0, 0, 4 /* CREATE_SUSPENDED */, 0, 0, &startup, p)) return 0;
  own(p->hProcess); own(p->hThread); return 1;
}
static int member(H process, H handle) {
  int32_t belongs = 0; return api.in_job(process, handle, &belongs) && belongs;
}
static int cleanup(struct NtProcessInformation *p) {
  int ok = 1;
  if (p->hProcess && api.wait(p->hProcess, 0) == 258) {
    ok = api.terminate_process(p->hProcess, 99) && api.wait(p->hProcess, 3000) == 0;
  }
  ok &= release(&p->hThread); ok &= release(&p->hProcess); return ok;
}
static int empty(H handle, struct Accounting *a) {
  for (unsigned i = 0; i < 100; ++i) {
    if (!api.query(handle, 1, a, sizeof(*a), 0)) return 0;
    if (!a->active) return 1;
    api.sleep(10);
  }
  return 0;
}
static void assignment_denied(void) {
  H j = job(), query_only = 0; struct NtProcessInformation p = {0};
  int assigned = -1, before = -1, after = -1, cleaned = 1; uint32_t error = 0;
  if (j && spawn("--leaf", &p) && api.duplicate(-1, j, -1, &query_only, 4, 0, 0)) {
    own(query_only); before = member(p.hProcess, j);
    assigned = api.assign(query_only, p.hProcess); error = assigned ? 0 : api.error();
    after = member(p.hProcess, j);
  }
  cleaned &= cleanup(&p); cleaned &= release(&query_only); cleaned &= release(&j);
  char actual[256];
  snprintf(actual, sizeof(actual), "{\"assigned\":%d,\"winerror\":%u,\"before_member\":%d,\"after_member\":%d,\"resumed\":false,\"cleanup\":%s}", assigned, error, before, after, cleaned ? "true" : "false");
  result("job.assignment-denied", assigned == 0 && error == 5 && before == 0 && after == 0 && cleaned, actual);
}
static void tree_terminate(void) {
  H j = job(), child = 0; struct NtProcessInformation parent = {0};
  struct Pids list = {0}; struct Accounting account = {0};
  int assigned = 0, verified = 0, resumed = 0, child_member = 0, stopped = 0, cleaned = 1;
  uint32_t error = 0, parent_exit = 0, child_exit = 0; unsigned seen = 0;
  if (j && spawn("--parent", &parent)) {
    assigned = api.assign(j, parent.hProcess); error = assigned ? 0 : api.error();
    verified = assigned && member(parent.hProcess, j);
    resumed = verified && api.resume(parent.hThread) == 1;
    if (resumed) {
      for (unsigned i = 0; i < 200; ++i) {
        memset(&list, 0, sizeof(list));
        if (!api.query(j, 3, &list, sizeof(list), 0)) break;
        if (list.assigned == 2 && list.listed == 2) { seen = 2; break; }
        api.sleep(10);
      }
      for (unsigned i = 0; i < list.listed && i < 8; ++i) {
        if (list.pid[i] != parent.dwProcessId) child = own(api.open_process(0x1000 | 0x100000 | 1, 0, list.pid[i]));
      }
      child_member = child && member(child, j);
      stopped = api.terminate_job(j, 71) && api.wait(parent.hProcess, 3000) == 0 && child &&
                api.wait(child, 3000) == 0 && empty(j, &account) &&
                api.exit_code(parent.hProcess, &parent_exit) && api.exit_code(child, &child_exit);
    }
  }
  if (j) api.terminate_job(j, 99);
  if (child && api.wait(child, 0) == 258) api.terminate_process(child, 99);
  cleaned &= cleanup(&parent); cleaned &= release(&child); cleaned &= release(&j);
  char actual[640];
  snprintf(actual, sizeof(actual), "{\"assigned\":%d,\"winerror\":%u,\"verified_before_resume\":%d,\"resumed\":%d,\"listed\":%u,\"parent_pid\":%u,\"child_pid\":%llu,\"child_member\":%d,\"terminated\":%d,\"parent_exit\":%u,\"child_exit\":%u,\"total_processes\":%u,\"active_processes\":%u,\"cleanup\":%s}",
    assigned, error, verified, resumed, seen, parent.dwProcessId,
    (unsigned long long)(seen ? (list.pid[0] == parent.dwProcessId ? list.pid[1] : list.pid[0]) : 0),
    child_member, stopped, parent_exit, child_exit, account.total, account.active, cleaned ? "true" : "false");
  result("job.tree-terminate", assigned && verified && resumed && seen == 2 && child_member && stopped &&
         parent_exit == 71 && child_exit == 71 && account.total == 2 && !account.active && cleaned, actual);
}
static void nested_close(void) {
  H outer = job(), inner = job(); struct NtProcessInformation p = {0}; struct Accounting a = {0};
  int outer_assigned = 0, nested = 0, both = 0, stopped = 0, cleaned = 1;
  int first_close = 0, repeat_close = -1; uint32_t error = 0, repeat_error = 0;
  if (outer && inner && spawn("--leaf", &p)) {
    outer_assigned = api.assign(outer, p.hProcess);
    if (outer_assigned) { nested = api.assign(inner, p.hProcess); error = nested ? 0 : api.error(); }
    both = nested && member(p.hProcess, inner) && member(p.hProcess, outer);
    if (both && api.resume(p.hThread) == 1) {
      H stale = inner;
      first_close = release(&inner);
      repeat_close = api.close(stale); repeat_error = repeat_close ? 0 : api.error();
      stopped = api.wait(p.hProcess, 3000) == 0 && empty(outer, &a);
    }
  }
  if (inner) api.terminate_job(inner, 99);
  if (outer) api.terminate_job(outer, 99);
  cleaned &= cleanup(&p); cleaned &= release(&inner); cleaned &= release(&outer);
  char actual[384];
  snprintf(actual, sizeof(actual), "{\"outer_assigned\":%d,\"nested_assigned\":%d,\"winerror\":%u,\"both_memberships\":%d,\"close\":%d,\"repeat_close\":%d,\"repeat_winerror\":%u,\"stopped\":%d,\"active_processes\":%u,\"cleanup\":%s}",
    outer_assigned, nested, error, both, first_close, repeat_close, repeat_error, stopped, a.active, cleaned ? "true" : "false");
  result("job.nested-close", both && first_close && !repeat_close && repeat_error == 6 && stopped && cleaned, actual);
}
static void single(const char *id, int timeout) {
  H j = job(); struct NtProcessInformation p = {0}; struct Accounting a = {0};
  int assigned = 0, verified = 0, resumed = 0, stopped = 0, cleaned = 1; uint32_t observed = -1, code = 0;
  if (j && spawn(timeout ? "--leaf" : "--exit", &p)) {
    assigned = api.assign(j, p.hProcess); verified = assigned && member(p.hProcess, j);
    resumed = verified && api.resume(p.hThread) == 1;
    if (resumed) {
      observed = api.wait(p.hProcess, timeout ? 150 : 3000);
      if (timeout) { release(&j); stopped = api.wait(p.hProcess, 3000) == 0; }
      else stopped = observed == 0 && empty(j, &a);
      api.exit_code(p.hProcess, &code);
    }
  }
  if (j) api.terminate_job(j, 99);
  cleaned &= cleanup(&p); cleaned &= release(&j);
  char actual[320];
  snprintf(actual, sizeof(actual), "{\"assigned\":%d,\"verified_before_resume\":%d,\"resumed\":%d,\"wait_result\":%u,\"deadline_ms\":%u,\"stopped\":%d,\"exit_code\":%u,\"cleanup\":%s}", assigned, verified, resumed, observed, timeout ? 150 : 3000, stopped, code, cleaned ? "true" : "false");
  result(id, assigned && verified && resumed && observed == (timeout ? 258 : 0) && stopped && (timeout || code == 37) && cleaned, actual);
}
static int suite(void) {
  int32_t in = 0; struct Extended current = {0}; uint32_t error = 0, before = 0, after = 0, initial = 0;
  int queried = api.in_job(-1, 0, &in);
  int limits = in && api.query(0, 9, &current, sizeof(current), 0);
  if (in && !limits) error = api.error();
  char context[256];
  snprintf(context, sizeof(context), "{\"membership_query\":%d,\"in_job\":%d,\"immediate_limits_query\":%d,\"immediate_limit_flags\":%u,\"query_winerror\":%u,\"runner_assigned_or_terminated\":false}", queried, in, limits, current.basic.flags, error);
  result("job.context", queried && (!in || limits), context);
  int count_initial = api.handle_count(-1, &initial), warm_clean = 1;
  /* Preserve the initial observation; Win32's first CreateProcessW may retain
   * process-local state. Establish the repeat baseline with three bounded,
   * never-resumed fixtures, and require no continuing growth afterwards. */
  uint32_t warm_counts[3] = {0}, created_counts[3] = {0}, stopped_counts[3] = {0};
  for (unsigned i = 0; i < 3; ++i) {
    struct NtProcessInformation warm = {0};
    warm_clean &= spawn("--leaf", &warm);
    warm_clean &= api.handle_count(-1, &created_counts[i]);
    if (warm.hProcess) {
      warm_clean &= api.terminate_process(warm.hProcess, 99);
      warm_clean &= api.wait(warm.hProcess, 3000) == 0;
    }
    warm_clean &= api.handle_count(-1, &stopped_counts[i]);
    warm_clean &= cleanup(&warm);
    warm_clean &= api.handle_count(-1, &warm_counts[i]);
  }
  int count_before = api.handle_count(-1, &before);
  assignment_denied(); tree_terminate(); nested_close();
  single("job.early-exit", 0); single("job.timeout-cleanup", 1);
  int count_after = api.handle_count(-1, &after);
  char handles[480];
  snprintf(handles, sizeof(handles), "{\"opened\":%u,\"closed\":%u,\"initial\":%u,\"warmup_created_counts\":[%u,%u,%u],\"warmup_stopped_counts\":[%u,%u,%u],\"warmup_counts\":[%u,%u,%u],\"warmup_cleanup\":%s,\"before\":%u,\"after\":%u,\"count_query\":%s}", opened, closed, initial, created_counts[0], created_counts[1], created_counts[2], stopped_counts[0], stopped_counts[1], stopped_counts[2], warm_counts[0], warm_counts[1], warm_counts[2], warm_clean ? "true" : "false", before, after, count_initial && count_before && count_after ? "true" : "false");
  result("job.handles", opened == closed && count_initial && count_before && count_after && before == after && warm_clean && warm_counts[0] == warm_counts[1] && warm_counts[1] == warm_counts[2], handles);
  return failures != 0;
}
#endif
int main(int argc, char **argv) {
#if defined(__aarch64__)
  const char *arch = "aarch64";
#else
  const char *arch = "x86_64";
#endif
  if (argc == 1 || (argc == 2 && !strcmp(argv[1], "--ordinary"))) {
    printf("{\"probe\":\"svc-job\",\"arch\":\"%s\",\"page_size\":%ld,\"windows\":%s,\"job_calls\":0}\n", arch, sysconf(_SC_PAGESIZE), IsWindows() ? "true" : "false"); return 0;
  }
  if (!IsWindows()) { fputs("Job is unavailable on this host\n", stderr); return 69; }
#if defined(__x86_64__)
  int missing = argc == 2 && !strcmp(argv[1], "--resolve-missing");
  int loaded = resolve(missing);
  if (!loaded) { printf("{\"case_id\":\"job.resolve\",\"status\":\"fail\",\"actual\":{\"winerror\":%u}}\n", GetLastError()); return 1; }
  if (missing) { FreeLibrary(kernel); return failures ? 1 : 0; }
  if (!api.module_name(0, executable, 32768)) return 1;
  if (argc == 2 && !strcmp(argv[1], "--leaf")) { api.sleep(20000); api.exit_process(0); }
  if (argc == 2 && !strcmp(argv[1], "--exit")) api.exit_process(37);
  if (argc == 2 && !strcmp(argv[1], "--hang")) { api.sleep(20000); api.exit_process(0); }
  if (argc == 2 && !strcmp(argv[1], "--parent")) {
    struct NtProcessInformation child = {0};
    if (!spawn("--leaf", &child)) api.exit_process(72);
    if (api.resume(child.hThread) != 1) { cleanup(&child); api.exit_process(73); }
    release(&child.hThread); release(&child.hProcess);
    api.sleep(20000); api.exit_process(0);
  }
  result("job.resolve", 1, "{\"exports\":20,\"abi\":\"ms-x64\",\"basic_size\":64,\"extended_size\":144,\"accounting_size\":48}");
  int status = suite(); FreeLibrary(kernel); return status;
#else
  return 69;
#endif
}
