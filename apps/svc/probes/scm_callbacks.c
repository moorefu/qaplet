/* SPDX-License-Identifier: MIT
 * SCM threads are foreign threads: no libc, TLS, JS, allocation or pthread calls.
 * Every external call in this TU is a pre-resolved raw Win32 MS-ABI pointer. */
#define _COSMO_SOURCE 1
#include "scm_abi.h"
#if defined(__x86_64__)
static struct AppScmAPI api;
static AppScmWChar name[80];
static AppScmHandle status_handle, event_handle;
static struct AppScmResult result;

static void remember_error(uint32_t error) {
  uint32_t empty = 0;
  __atomic_compare_exchange_n(&result.first_error, &empty, error, 0, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
}

static void report_status(uint32_t state, uint32_t error) {
  struct AppScmStatus status = {16, state, state == 4 ? 1 : 0, error, 0,
                              state == 2 || state == 3 ? 1 : 0,
                              state == 2 || state == 3 ? 3000 : 0};
  if (api.set_status(__atomic_load_n(&status_handle, __ATOMIC_SEQ_CST), &status))
    __atomic_fetch_or(&result.status_mask, 1u << state, __ATOMIC_SEQ_CST);
  else
    remember_error(api.last_error());
}

uint32_t __msabi appscm_handler(uint32_t control, uint32_t type, void *event, void *context) {
  (void)type;
  (void)event;
  (void)context;
  __atomic_fetch_add(&result.handler_calls, 1, __ATOMIC_SEQ_CST);
  if (control == 1) {
    __atomic_fetch_add(&result.stop_calls, 1, __ATOMIC_SEQ_CST);
    report_status(3, 0);
    if (!api.set_event(__atomic_load_n(&event_handle, __ATOMIC_SEQ_CST)))
      remember_error(api.last_error());
    return 0;
  }
  return control == 4 ? 0 : 120;
}

void __msabi appscm_main(uint32_t argc, AppScmWChar **argv) {
  (void)argc;
  (void)argv;
  __atomic_fetch_add(&result.main_calls, 1, __ATOMIC_SEQ_CST);
  AppScmHandle status = api.register_handler(name, appscm_handler, 0);
  if (!status) {
    remember_error(api.last_error());
    return;
  }
  __atomic_store_n(&status_handle, status, __ATOMIC_SEQ_CST);
  report_status(2, 0);
  AppScmHandle event = api.create_event(0, 1, 0, 0);
  if (!event) {
    uint32_t error = api.last_error();
    remember_error(error);
    report_status(1, error);
    return;
  }
  __atomic_store_n(&event_handle, event, __ATOMIC_SEQ_CST);
  report_status(4, 0);
  uint32_t waited = api.wait(event, 20000);
  __atomic_store_n(&result.wait_result, waited, __ATOMIC_SEQ_CST);
  uint32_t error = waited == 0 ? 0 : waited == 258 ? 1460 : api.last_error();
  if (error) remember_error(error);
  report_status(1, error);
  if (api.close(event))
    __atomic_fetch_add(&result.event_closes, 1, __ATOMIC_SEQ_CST);
  else
    remember_error(api.last_error());
}

/* These two entry points run only on the original Cosmopolitan-created thread. */
void appscm_setup(const struct AppScmAPI *resolved, const char *ascii_name) {
  api = *resolved;
  for (unsigned i = 0; i < 79 && ascii_name[i]; ++i) name[i] = (unsigned char)ascii_name[i];
}

void appscm_snapshot(struct AppScmResult *out) {
#define COPY(field) out->field = __atomic_load_n(&result.field, __ATOMIC_SEQ_CST)
  COPY(main_calls); COPY(handler_calls); COPY(stop_calls); COPY(status_mask);
  COPY(first_error); COPY(wait_result); COPY(event_closes);
#undef COPY
}
#endif
