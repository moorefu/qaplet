/* SPDX-License-Identifier: MIT */
#ifndef QAPLET_RUNTIME_INTERNAL_H
#define QAPLET_RUNTIME_INTERNAL_H
#include "qaplet.h"
#include "quickjs.h"
#include <pthread.h>

typedef union qaplet_allocation qaplet_allocation;
struct qaplet_runtime {
  pthread_t owner;
  pthread_mutex_t mutex;
  JSRuntime *engine;
  JSContext *context;
  qaplet_allocation *engine_allocations;
  qaplet_snapshot snapshot;
  qaplet_result result;
  int has_result;
};
qaplet_code qaplet_check_structure(const void *, size_t);
qaplet_code qaplet_check_diagnostic(qaplet_diagnostic *);
qaplet_code qaplet_report(qaplet_diagnostic *, qaplet_code);
qaplet_code qaplet_owner(qaplet_runtime *);
qaplet_code qaplet_store_scalar(qaplet_runtime *, JSValueConst);
#endif
