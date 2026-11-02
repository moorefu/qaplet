/* SPDX-License-Identifier: MIT */
#ifndef QAPLET_RUNTIME_INTERNAL_H
#define QAPLET_RUNTIME_INTERNAL_H
#include "qaplet.h"
#include "quickjs.h"
#include <pthread.h>

typedef union qaplet_allocation qaplet_allocation;
typedef struct qaplet_rejection qaplet_rejection;
struct qaplet_runtime {
  pthread_t owner;
  pthread_mutex_t mutex;
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
  uint64_t execution_deadline_ns;
  uint64_t execution_dispatch_limit;
  int execution_active;
};
qaplet_code qaplet_check_structure(const void *, size_t);
qaplet_code qaplet_check_diagnostic(qaplet_diagnostic *);
qaplet_code qaplet_report(qaplet_diagnostic *, qaplet_code);
qaplet_code qaplet_owner(qaplet_runtime *);
qaplet_code qaplet_store_scalar(qaplet_runtime *, JSValueConst);
int qaplet_utf8_valid(const char *, size_t);
qaplet_code qaplet_values_init(qaplet_runtime *);
void qaplet_values_reset(qaplet_runtime *);
void qaplet_values_dispose(qaplet_runtime *);
void qaplet_capture_exception(qaplet_runtime *, JSValueConst);
qaplet_code qaplet_report_result(qaplet_runtime *, qaplet_diagnostic *, qaplet_code);
qaplet_code qaplet_execution_checkpoint(qaplet_runtime *, qaplet_progress *, unsigned);
void qaplet_execution_init(qaplet_runtime *);
void qaplet_execution_dispose(qaplet_runtime *);
/* The caller owns the Runtime and has completed call preflight. */
qaplet_code qaplet_check_execution_options(const qaplet_runtime *, const qaplet_execution_options *);
qaplet_code qaplet_execution_begin(qaplet_runtime *, const qaplet_execution_options *);
qaplet_code qaplet_execution_check(qaplet_runtime *);
qaplet_code qaplet_execution_dispatch(qaplet_runtime *);
/* Shared first-cause latch. The Runtime mutex must already be held. */
void qaplet_record_termination_locked(qaplet_runtime *, qaplet_code);
#endif
