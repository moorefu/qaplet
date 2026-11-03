/* SPDX-License-Identifier: MIT */
#ifndef QAPLET_RUNTIME_INTERNAL_H
#define QAPLET_RUNTIME_INTERNAL_H
#include "qaplet.h"
#include "quickjs.h"
#include <pthread.h>

typedef union qaplet_allocation qaplet_allocation;
typedef struct qaplet_rejection qaplet_rejection;
typedef struct qaplet_module qaplet_module;
typedef struct qaplet_module_alias qaplet_module_alias;
typedef struct qaplet_host_record qaplet_host_record;
typedef struct qaplet_timer_slot qaplet_timer_slot;
typedef struct { uint64_t generation; int active; } qaplet_keepalive_slot;
typedef struct {
  uint64_t deadline_ns, dispatch_limit;
} qaplet_execution_budget;
struct qaplet_runtime {
  pthread_t owner;
  pthread_mutex_t mutex;
  pthread_cond_t native_condition;
  uint64_t native_retained;
  int driver_active, m2_enabled;
  qaplet_timer_slot *timers;
  size_t timer_storage_size;
  uint64_t timer_count, timer_peak, timer_sequence;
  int timer_callback_active;
  qaplet_m2_limits m2_limits;
  qaplet_native_state native_state;
  qaplet_code cleanup_reason;
  uint64_t runtime_id, clock_last_ns;
  qaplet_keepalive_slot *keepalives;
  size_t keepalive_storage_size;
  int wake_fd[2], wake_pending, loop_closed;
  uint64_t native_handles, native_handles_peak, reactor_registrations, reactor_registrations_peak;
  uint64_t native_uncertain_handles;
  uint64_t keepalive_tokens, keepalive_tokens_peak, wake_requests, wake_signals, wait_calls;
  int control_error, cleanup_error, primary_error;
  JSRuntime *engine;
  JSContext *context;
  qaplet_allocation *engine_allocations;
  qaplet_snapshot snapshot;
  qaplet_result result;
  unsigned char *result_data;
  JSValue string_char_code_at;
  JSValue typed_array_byte_length;
  JSClassID uint8_array_class, array_buffer_class;
  JSAtom diagnostic_message_atom;
  char safe_diagnostic[1024];
  size_t safe_diagnostic_length;
  int has_safe_diagnostic, safe_diagnostic_truncated;
  int has_result;
  JSValue entry_promise;
  qaplet_code execution_code;
  qaplet_rejection *rejections;
  char *initial_cwd;
  qaplet_string *initial_argv;
  size_t initial_argc;
  qaplet_module *modules, *active_module;
  qaplet_module_alias *module_aliases;
  JSValue module_exception;
  qaplet_code module_exception_code;
  int module_execution;
  uint64_t execution_deadline_ns;
  uint64_t execution_dispatch_limit;
  int execution_active;
  qaplet_host_record *host_modules;
  uint64_t host_call_serial, host_active_call;
  JSValue host_return;
  int host_return_set, host_registration_locked;
  JSValue event_handler;
  unsigned char *event_slots;
  size_t event_stride, event_storage_size, event_head;
  int event_handler_ready, event_accepting;
};
qaplet_code qaplet_check_structure(const void *, size_t);
qaplet_code qaplet_check_diagnostic(qaplet_diagnostic *);
qaplet_code qaplet_report(qaplet_diagnostic *, qaplet_code);
qaplet_code qaplet_owner(qaplet_runtime *);
qaplet_code qaplet_create_internal(const qaplet_config *, const qaplet_m2_config *, qaplet_runtime **, qaplet_diagnostic *, qaplet_m2_diagnostic *);
qaplet_code qaplet_m2_configure(const qaplet_m2_config *, const qaplet_limits *, qaplet_m2_limits *);
void qaplet_driver_set(qaplet_runtime *, int);
qaplet_code qaplet_loop_init(qaplet_runtime *);
void qaplet_loop_shutdown(qaplet_runtime *);
/* Requires mutex. Never invokes JS. A wake fault cannot undo accepted state. */
void qaplet_wake_locked(qaplet_runtime *);
void qaplet_control_failure_locked(qaplet_runtime *, int);
/* Requires mutex; monotonic failures/backwards time permanently latch failure. */
int qaplet_clock_locked(qaplet_runtime *, uint64_t *);
/* Providers retain before publishing accepted native work and release only
 * after physical exit/join and all resource data retirement. Releasing a
 * logical result is not sufficient. No provider may retain after close. */
qaplet_code qaplet_native_retain(qaplet_runtime *);
void qaplet_native_release(qaplet_runtime *);
/* Internal ready-work drive; caller holds the whole-call owner guard. */
qaplet_code qaplet_pump_ready(qaplet_runtime *, qaplet_progress *, qaplet_diagnostic *, int automatic);
/* CLI-only automatic drive, returning at entry completion before a new ledger. */
qaplet_code qaplet_drive_entry(qaplet_runtime *, qaplet_drive_result *, qaplet_diagnostic *);
qaplet_code qaplet_timers_init(qaplet_runtime *);
void qaplet_timers_dispose(qaplet_runtime *);
int qaplet_timers_pending(qaplet_runtime *);
/* Requires acceptance mutex. No JS; deadline includes unref if loop is alive. */
int qaplet_timers_ref_locked(qaplet_runtime *);
int qaplet_timers_deadline_locked(qaplet_runtime *, uint64_t *);
qaplet_code qaplet_timers_deliver(qaplet_runtime *, unsigned *);
qaplet_lifecycle qaplet_get_lifecycle(qaplet_runtime *);
qaplet_code qaplet_store_scalar(qaplet_runtime *, JSValueConst);
qaplet_code qaplet_copy_js_value(qaplet_runtime *, JSValueConst, qaplet_result *, unsigned char **);
int qaplet_utf8_valid(const char *, size_t);
qaplet_code qaplet_values_init(qaplet_runtime *);
void qaplet_values_reset(qaplet_runtime *);
void qaplet_values_dispose(qaplet_runtime *);
void qaplet_capture_exception(qaplet_runtime *, JSValueConst);
qaplet_code qaplet_report_result(qaplet_runtime *, qaplet_diagnostic *, qaplet_code);
qaplet_code qaplet_execution_checkpoint(qaplet_runtime *, qaplet_progress *, unsigned);
void qaplet_execution_init(qaplet_runtime *);
void qaplet_execution_dispose(qaplet_runtime *);
qaplet_code qaplet_modules_init(qaplet_runtime *, const qaplet_config *);
qaplet_code qaplet_check_runtime_config(const qaplet_config *);
void qaplet_runtime_snapshot_dispose(qaplet_runtime *);
void *qaplet_native_alloc(qaplet_runtime *, size_t);
void qaplet_native_free(qaplet_runtime *, void *, size_t);
qaplet_code qaplet_modules_register_host(qaplet_runtime *, const char *);
JSModuleDef *qaplet_host_load(qaplet_runtime *, const char *);
JSModuleDef *qaplet_events_load(qaplet_runtime *);
JSModuleDef *qaplet_runtime_load(qaplet_runtime *);
JSModuleDef *qaplet_timers_load(qaplet_runtime *);
qaplet_code qaplet_events_init(qaplet_runtime *);
void qaplet_events_dispose(qaplet_runtime *);
int qaplet_events_pending(qaplet_runtime *);
qaplet_code qaplet_events_deliver(qaplet_runtime *, unsigned *);
qaplet_code qaplet_events_check_limits(const qaplet_limits *);
void qaplet_host_dispose(qaplet_runtime *);
void qaplet_modules_dispose(qaplet_runtime *);
void qaplet_modules_complete(qaplet_runtime *);
qaplet_code qaplet_module_exception_code(qaplet_runtime *, JSValueConst);
qaplet_code qaplet_eval_filename(qaplet_runtime *, const char *, size_t, const char *, const qaplet_execution_options *, const qaplet_execution_budget *, qaplet_progress *, qaplet_diagnostic *);
/* The caller owns the Runtime and has completed call preflight. */
qaplet_code qaplet_check_execution_options(const qaplet_runtime *, const qaplet_execution_options *);
/* Prepare before native input/registry mutation; begin commits that same budget. */
qaplet_code qaplet_execution_prepare(qaplet_runtime *, const qaplet_execution_options *, qaplet_execution_budget *);
qaplet_code qaplet_execution_begin(qaplet_runtime *, const qaplet_execution_budget *);
qaplet_code qaplet_execution_check(qaplet_runtime *);
qaplet_code qaplet_execution_dispatch(qaplet_runtime *);
/* Same check/claim as dispatch, while holding the acceptance mutex. */
qaplet_code qaplet_execution_dispatch_locked(qaplet_runtime *);
/* Shared first-cause latch. The Runtime mutex must already be held. */
void qaplet_record_termination_locked(qaplet_runtime *, qaplet_code);
#endif
