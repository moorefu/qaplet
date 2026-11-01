/* SPDX-License-Identifier: MIT
 * Application-private Windows ABI declarations. Not Core or Embed SDK API. */
#ifndef QAPLET_APP_SCM_ABI_H_
#define QAPLET_APP_SCM_ABI_H_
#include <stdint.h>
#include <stddef.h>

#if defined(__x86_64__)
#include "libc/nt/thunk/msabi.h"
typedef uint16_t AppScmWChar;
typedef int64_t AppScmHandle;
typedef void (__msabi *AppScmMain)(uint32_t, AppScmWChar **);
typedef uint32_t (__msabi *AppScmHandler)(uint32_t, uint32_t, void *, void *);
struct AppScmEntry { AppScmWChar *name; AppScmMain main; };
struct AppScmStatus {
  uint32_t type, state, accepted, winerr, specific, checkpoint, wait_hint;
};
struct AppScmStatusProcess {
  struct AppScmStatus status;
  uint32_t process_id, flags;
};
struct AppScmAPI {
  int32_t (__msabi *dispatcher)(const struct AppScmEntry *);
  AppScmHandle (__msabi *register_handler)(const AppScmWChar *, AppScmHandler, void *);
  int32_t (__msabi *set_status)(AppScmHandle, const struct AppScmStatus *);
  AppScmHandle (__msabi *create_event)(void *, int32_t, int32_t, const AppScmWChar *);
  uint32_t (__msabi *wait)(AppScmHandle, uint32_t);
  int32_t (__msabi *set_event)(AppScmHandle);
  int32_t (__msabi *close)(AppScmHandle);
  uint32_t (__msabi *last_error)(void);
};
struct AppScmResult {
  uint32_t main_calls, handler_calls, stop_calls, status_mask;
  uint32_t first_error, wait_result, event_closes;
};
_Static_assert(sizeof(AppScmHandle) == 8, "Win64 handle width");
_Static_assert(sizeof(struct AppScmEntry) == 16, "SERVICE_TABLE_ENTRYW");
_Static_assert(offsetof(struct AppScmEntry, main) == 8, "service callback offset");
_Static_assert(sizeof(struct AppScmStatus) == 28, "SERVICE_STATUS");
_Static_assert(offsetof(struct AppScmStatus, checkpoint) == 20, "checkpoint offset");
_Static_assert(sizeof(struct AppScmStatusProcess) == 36, "SERVICE_STATUS_PROCESS");
_Static_assert(offsetof(struct AppScmStatusProcess, process_id) == 28, "PID offset");
_Static_assert(__atomic_always_lock_free(4, 0), "callback counters require lock-free atomics");
_Static_assert(__atomic_always_lock_free(8, 0), "callback handles require lock-free atomics");
void appscm_setup(const struct AppScmAPI *, const char *);
void appscm_snapshot(struct AppScmResult *);
void __msabi appscm_main(uint32_t, AppScmWChar **);
uint32_t __msabi appscm_handler(uint32_t, uint32_t, void *, void *);
#endif
#endif
