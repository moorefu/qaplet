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
  int has_result;
  JSValue entry_promise;
  qaplet_code execution_code;
  qaplet_rejection *rejections;
  uint64_t execution_deadline_ns;
  int execution_active;
};
qaplet_code qaplet_check_structure(const void *, size_t);
qaplet_code qaplet_check_diagnostic(qaplet_diagnostic *);
qaplet_code qaplet_report(qaplet_diagnostic *, qaplet_code);
qaplet_code qaplet_owner(qaplet_runtime *);
qaplet_code qaplet_store_scalar(qaplet_runtime *, JSValueConst);
qaplet_code qaplet_execution_checkpoint(qaplet_runtime *, qaplet_progress *, unsigned);
void qaplet_execution_init(qaplet_runtime *);
void qaplet_execution_dispose(qaplet_runtime *);
/* The caller owns the Runtime and has completed call preflight. */
qaplet_code qaplet_execution_begin(qaplet_runtime *);
qaplet_code qaplet_execution_check(qaplet_runtime *);
/* Shared first-cause latch. The Runtime mutex must already be held. */
void qaplet_record_termination_locked(qaplet_runtime *, qaplet_code);
#endif
