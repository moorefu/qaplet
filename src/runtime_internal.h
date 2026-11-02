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
  char *initial_cwd;
  qaplet_module *modules, *active_module;
  qaplet_module_alias *module_aliases;
  JSValue module_exception;
  qaplet_code module_exception_code;
  int module_execution;
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
qaplet_code qaplet_modules_init(qaplet_runtime *);
void qaplet_modules_dispose(qaplet_runtime *);
void qaplet_modules_complete(qaplet_runtime *);
qaplet_code qaplet_module_exception_code(qaplet_runtime *, JSValueConst);
qaplet_code qaplet_eval_filename(qaplet_runtime *, const char *, size_t, const char *, qaplet_progress *, qaplet_diagnostic *);
#endif
