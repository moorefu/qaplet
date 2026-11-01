/* SPDX-License-Identifier: MIT
 * Independent M0-SVC probe; callbacks never enter JavaScript. */
#define _COSMO_SOURCE 1
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "libc/dce.h"
#include "scm_abi.h"
#if defined(__x86_64__)
#include "libc/nt/dll.h"
#include "libc/nt/runtime.h"
#endif

#if defined(__aarch64__)
#define ARCH "aarch64"
#else
#define ARCH "x86_64"
#endif

int main(int argc, char **argv) {
  if (argc == 1 || (argc == 2 && !strcmp(argv[1], "--ordinary"))) {
    printf("{\"probe\":\"svc-scm\",\"entry\":\"ordinary\",\"arch\":\"%s\",\"page_size\":%ld,\"windows\":%s,\"scm_calls\":0}\n",
           ARCH, sysconf(_SC_PAGESIZE), IsWindows() ? "true" : "false");
    return 0;
  }
  /* Runtime discrimination precedes every DLL lookup, also in a fat x64 slice. */
  if (!IsWindows()) {
    fprintf(stderr, "SCM is unavailable on this host; ordinary entry remains portable\n");
    return 69;
  }
#if defined(__x86_64__)
  int service = argc == 4 && !strcmp(argv[1], "--service");
  int missing = argc == 2 && !strcmp(argv[1], "--resolve-missing");
  if (!service && !missing && !(argc == 2 && !strcmp(argv[1], "--resolve"))) return 64;
  struct AppScmAPI api;
  int64_t advapi = LoadLibraryA("advapi32.dll");
  int64_t kernel = LoadLibraryA("kernel32.dll");
  const char *failed = !advapi ? "advapi32.dll" : !kernel ? "kernel32.dll" : 0;
  uint32_t error = failed ? GetLastError() : 0;
  /* Raw pointers are always explicitly MS ABI, independently of libc wrappers. */
#define RESOLVE(field, dll, symbol) do { \
  api.field = (__typeof__(api.field))GetProcAddress(dll, symbol); \
  if (!api.field && !failed) { failed = symbol; error = GetLastError(); } \
} while (0)
  RESOLVE(dispatcher, advapi, "StartServiceCtrlDispatcherW");
  RESOLVE(register_handler, advapi, "RegisterServiceCtrlHandlerExW");
  RESOLVE(set_status, advapi, "SetServiceStatus");
  RESOLVE(create_event, kernel, "CreateEventW");
  RESOLVE(wait, kernel, "WaitForSingleObject");
  RESOLVE(set_event, kernel, "SetEvent");
  RESOLVE(close, kernel, "CloseHandle");
  RESOLVE(last_error, kernel, "GetLastError");
#undef RESOLVE
  if (missing && !failed && !GetProcAddress(advapi, "QapletDeliberatelyMissingScmExport")) {
    failed = "QapletDeliberatelyMissingScmExport";
    error = GetLastError();
  }
  int status = 0;
  if (failed) {
    printf("{\"probe\":\"svc-scm\",\"entry\":\"resolve\",\"status\":\"unavailable\",\"export\":\"%s\",\"winerror\":%u}\n", failed, error);
    status = 1;
  } else if (!service) {
    puts("{\"probe\":\"svc-scm\",\"entry\":\"resolve\",\"status\":\"pass\",\"exports\":8,\"service_entry_size\":16,\"status_size\":28,\"status_process_size\":36,\"abi\":\"ms-x64\"}");
  } else {
    if (!argv[2][0] || strlen(argv[2]) >= 80) return 64;
    /* Reserve evidence on the original thread, before SCM starts foreign ones. */
    FILE *log = fopen(argv[3], "wx");
    if (!log) return 73;
    appscm_setup(&api, argv[2]);
    AppScmWChar name[80] = {0};
    for (unsigned i = 0; argv[2][i]; ++i) name[i] = (unsigned char)argv[2][i];
    const struct AppScmEntry table[] = {{name, appscm_main}, {0, 0}};
    int32_t dispatched = api.dispatcher(table);
    uint32_t dispatch_error = dispatched ? 0 : api.last_error();
    struct AppScmResult result;
    appscm_snapshot(&result);
    fprintf(log, "{\"probe\":\"svc-scm\",\"entry\":\"service\",\"dispatcher\":%d,\"dispatch_error\":%u,\"main_calls\":%u,\"handler_calls\":%u,\"stop_calls\":%u,\"status_mask\":%u,\"first_error\":%u,\"wait_result\":%u,\"event_closes\":%u,\"callback_js_calls\":0}\n",
            dispatched, dispatch_error, result.main_calls, result.handler_calls, result.stop_calls,
            result.status_mask, result.first_error, result.wait_result, result.event_closes);
    if (fclose(log)) status = 1;
    if (!dispatched || result.first_error || result.main_calls != 1 || result.stop_calls < 1 || result.status_mask != 30 || result.event_closes != 1) status = 1;
  }
  if (advapi) FreeLibrary(advapi);
  if (kernel) FreeLibrary(kernel);
  return status;
#else
  return 69;
#endif
}
