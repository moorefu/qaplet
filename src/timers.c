/* SPDX-License-Identifier: MIT */
#include "runtime_internal.h"
#include <string.h>
#include <math.h>
#include <errno.h>

struct qaplet_timer_slot {
  uint64_t generation, deadline, sequence, period;
  int state, referenced, immediate;
  qaplet_timer_callback callback;
  void *userdata;
  JSValue function;
};

qaplet_code qaplet_timers_init(qaplet_runtime *runtime) {
  if (!runtime->m2_enabled) return QAPLET_OK;
  if (runtime->m2_limits.timers > SIZE_MAX / sizeof(*runtime->timers)) return QAPLET_INVALID_ARGUMENT;
  runtime->timer_storage_size = runtime->m2_limits.timers * sizeof(*runtime->timers);
  if (runtime->timer_storage_size > runtime->snapshot.limits.native_storage_bytes - runtime->snapshot.native_current_bytes)
    return QAPLET_RESOURCE_LIMIT;
  runtime->timers = qaplet_native_alloc(runtime, runtime->timer_storage_size);
  if (runtime->timers) for (uint64_t i = 0; i < runtime->m2_limits.timers; ++i) runtime->timers[i].function = JS_UNDEFINED;
  return runtime->timers ? QAPLET_OK : QAPLET_MEMORY_LIMIT;
}
void qaplet_timers_dispose(qaplet_runtime *runtime) {
  pthread_mutex_lock(&runtime->mutex);
  qaplet_timer_slot *slots = runtime->timers;
  runtime->timers = NULL;
  runtime->timer_count = 0;
  pthread_mutex_unlock(&runtime->mutex);
  if (slots) for (uint64_t i = 0; i < runtime->m2_limits.timers; ++i) JS_FreeValue(runtime->context, slots[i].function);
  qaplet_native_free(runtime, slots, runtime->timer_storage_size);
}
static qaplet_code preflight(qaplet_runtime *runtime) {
  if (!runtime) return QAPLET_INVALID_ARGUMENT;
  if (!pthread_equal(runtime->owner, pthread_self())) return QAPLET_WRONG_THREAD;
  if (runtime->driver_active && !runtime->timer_callback_active && !runtime->host_active_call) return QAPLET_BUSY;
  return QAPLET_OK;
}
static qaplet_code create(qaplet_runtime *runtime, uint64_t delay, uint64_t period, int immediate,
    qaplet_timer_callback callback, void *userdata, qaplet_timer *output, qaplet_diagnostic *diagnostic) {
  qaplet_code code = qaplet_check_diagnostic(diagnostic);
  if (code) return code;
  code = preflight(runtime);
  if (code) return qaplet_report(diagnostic, code);
  if (!callback || !output || delay > UINT64_C(9007199254740991) || delay > UINT64_MAX / 1000000)
    return qaplet_report(diagnostic, QAPLET_INVALID_ARGUMENT);
  pthread_mutex_lock(&runtime->mutex);
  uint64_t now;
  if (!runtime->m2_enabled) code = QAPLET_UNSUPPORTED;
  else if (runtime->snapshot.lifecycle != QAPLET_OPEN || runtime->loop_closed) code = QAPLET_BAD_STATE;
  else if (!qaplet_clock_locked(runtime, &now)) code = QAPLET_CONTROL_FAILURE;
  else if (delay * 1000000 > UINT64_MAX - now) code = QAPLET_INVALID_ARGUMENT;
  else if (runtime->timer_count == runtime->m2_limits.timers || runtime->timer_sequence == UINT64_MAX) code = QAPLET_RESOURCE_LIMIT;
  else {
    code = QAPLET_RESOURCE_LIMIT;
    for (uint64_t i = 0; i < runtime->m2_limits.timers; ++i) {
      qaplet_timer_slot *slot = &runtime->timers[i];
      if (slot->state || slot->generation == UINT64_MAX) continue;
      ++slot->generation;
      slot->deadline = now + delay * 1000000;
      slot->period = period * 1000000;
      slot->immediate = immediate;
      slot->sequence = ++runtime->timer_sequence;
      slot->state = slot->referenced = 1;
      slot->callback = callback; slot->userdata = userdata;
      *output = (qaplet_timer){runtime->runtime_id, i, slot->generation};
      if (++runtime->timer_count > runtime->timer_peak) runtime->timer_peak = runtime->timer_count;
      code = QAPLET_OK;
      break;
    }
  }
  pthread_mutex_unlock(&runtime->mutex);
  return qaplet_report(diagnostic, code);
}
qaplet_code qaplet_timer_create(qaplet_runtime *r, uint64_t delay, qaplet_timer_callback cb, void *p, qaplet_timer *t, qaplet_diagnostic *d) {
  return create(r, delay, 0, 0, cb, p, t, d);
}
qaplet_code qaplet_interval_create(qaplet_runtime *r, uint64_t period, qaplet_timer_callback cb, void *p, qaplet_timer *t, qaplet_diagnostic *d) {
  qaplet_code code=qaplet_check_diagnostic(d);
  if (code) return code;
  if (!period) return qaplet_report(d, QAPLET_INVALID_ARGUMENT);
  return create(r, period, period, 0, cb, p, t, d);
}
qaplet_code qaplet_immediate_create(qaplet_runtime *r, qaplet_timer_callback cb, void *p, qaplet_timer *t, qaplet_diagnostic *d) {
  return create(r, 0, 0, 1, cb, p, t, d);
}
static qaplet_timer_slot *lookup(qaplet_runtime *runtime, const qaplet_timer *timer) {
  if (!runtime->timers || timer->runtime_id != runtime->runtime_id || timer->slot >= runtime->m2_limits.timers || !timer->generation)
    return NULL;
  qaplet_timer_slot *slot = &runtime->timers[timer->slot];
  return slot->state && slot->state != 3 && slot->generation == timer->generation ? slot : NULL;
}
static qaplet_code change(qaplet_runtime *runtime, const qaplet_timer *timer, int referenced, int cancel, qaplet_diagnostic *diagnostic) {
  qaplet_code code = qaplet_check_diagnostic(diagnostic);
  if (code) return code;
  code = preflight(runtime);
  if (code) return qaplet_report(diagnostic, code);
  if (!timer || (!cancel && referenced != 0 && referenced != 1)) return qaplet_report(diagnostic, QAPLET_INVALID_ARGUMENT);
  pthread_mutex_lock(&runtime->mutex);
  qaplet_timer_slot *slot;
  JSValue released = JS_UNDEFINED;
  if (!runtime->m2_enabled) code = QAPLET_UNSUPPORTED;
  else if (!(slot = lookup(runtime, timer))) code = QAPLET_INVALID_HANDLE;
  else if (cancel) {
    if (slot->state == 2) slot->state = 3;
    else {
      slot->state = 0; --runtime->timer_count;
      released = slot->function; slot->function = JS_UNDEFINED;
    }
    slot->referenced = 0;
  } else slot->referenced = referenced;
  pthread_mutex_unlock(&runtime->mutex);
  JS_FreeValue(runtime->context, released);
  return qaplet_report(diagnostic, code);
}
qaplet_code qaplet_timer_cancel(qaplet_runtime *r, const qaplet_timer *t, qaplet_diagnostic *d) { return change(r, t, 0, 1, d); }
qaplet_code qaplet_timer_ref(qaplet_runtime *r, const qaplet_timer *t, int ref, qaplet_diagnostic *d) { return change(r, t, ref, 0, d); }
qaplet_code qaplet_get_timer_snapshot(qaplet_runtime *runtime, qaplet_timer_snapshot *output, qaplet_diagnostic *diagnostic) {
  qaplet_code code = qaplet_check_diagnostic(diagnostic);
  if (code) return code;
  code = qaplet_check_structure(output, sizeof(*output));
  if (!code && !runtime) code = QAPLET_INVALID_ARGUMENT;
  if (code) return qaplet_report(diagnostic, code);
  qaplet_timer_snapshot value = QAPLET_TIMER_SNAPSHOT_INIT;
  pthread_mutex_lock(&runtime->mutex);
  if (!runtime->m2_enabled) code = QAPLET_UNSUPPORTED;
  else {
    value.current = runtime->timer_count; value.peak = runtime->timer_peak;
    value.reserved_bytes = runtime->timers ? runtime->timer_storage_size : 0;
    uint64_t now = 0, deadline;
    int scheduled = qaplet_timers_deadline_locked(runtime, &deadline);
    if (runtime->timer_count && !qaplet_clock_locked(runtime, &now)) code = QAPLET_CONTROL_FAILURE;
    if (scheduled) {
      uint64_t delta = deadline > now ? deadline - now : 0;
      value.next_timeout_ms = delta / 1000000 + (delta % 1000000 != 0);
    }
    if (runtime->timers) for (uint64_t i = 0; i < runtime->m2_limits.timers; ++i) {
      qaplet_timer_slot *slot = &runtime->timers[i];
      if (slot->state && slot->referenced) ++value.referenced;
      if (slot->state == 1 && slot->deadline <= now) ++value.ready;
    }
  }
  pthread_mutex_unlock(&runtime->mutex);
  if (!code) memcpy((char *)output + 8, (char *)&value + 8, sizeof(value) - 8);
  return qaplet_report(diagnostic, code);
}
int qaplet_timers_ref_locked(qaplet_runtime *runtime) {
  if (runtime->timers) for (uint64_t i = 0; i < runtime->m2_limits.timers; ++i)
    if (runtime->timers[i].state && runtime->timers[i].referenced) return 1;
  return 0;
}
int qaplet_timers_deadline_locked(qaplet_runtime *runtime, uint64_t *deadline) {
  int found = 0;
  if (runtime->timers) for (uint64_t i = 0; i < runtime->m2_limits.timers; ++i)
    if (runtime->timers[i].state == 1 && (!found || runtime->timers[i].deadline < *deadline)) {
      *deadline = runtime->timers[i].deadline; found = 1;
    }
  return found;
}
int qaplet_timers_pending(qaplet_runtime *runtime, int immediate, uint64_t cutoff) {
  pthread_mutex_lock(&runtime->mutex);
  uint64_t now;
  int pending = 0;
  if (runtime->timer_count && runtime->snapshot.lifecycle == QAPLET_OPEN && qaplet_clock_locked(runtime, &now))
    for (uint64_t i=0;i<runtime->m2_limits.timers;++i) {
      qaplet_timer_slot *slot=&runtime->timers[i];
      if (slot->state==1 && slot->immediate==immediate && slot->deadline<=now && (!immediate || slot->sequence<=cutoff)) {pending=1;break;}
    }
  pthread_mutex_unlock(&runtime->mutex);
  return pending;
}
qaplet_code qaplet_timers_deliver(qaplet_runtime *runtime, unsigned *claimed, int immediate, uint64_t cutoff) {
  *claimed = 0;
  pthread_mutex_lock(&runtime->mutex);
  qaplet_timer_slot *selected = NULL;
  uint64_t index = 0, now;
  qaplet_code code = QAPLET_OK;
  if (qaplet_clock_locked(runtime, &now)) {
    for (uint64_t i = 0; runtime->timers && i < runtime->m2_limits.timers; ++i) {
      qaplet_timer_slot *slot = &runtime->timers[i];
      if (slot->state == 1 && slot->immediate==immediate && (!immediate || slot->sequence<=cutoff) && slot->deadline <= now && (!selected || slot->deadline < selected->deadline ||
          (slot->deadline == selected->deadline && slot->sequence < selected->sequence))) { selected = slot; index = i; }
    }
    if (selected) code = qaplet_execution_dispatch_locked(runtime);
  } else code = QAPLET_CONTROL_FAILURE;
  if (!selected || code) { pthread_mutex_unlock(&runtime->mutex); return code; }
  selected->state = 2;
  qaplet_timer timer = {runtime->runtime_id, index, selected->generation};
  qaplet_timer_callback callback = selected->callback;
  void *userdata = selected->userdata;
  *claimed = 1;
  pthread_mutex_unlock(&runtime->mutex);
  runtime->timer_callback_active = 1;
  if (!JS_IsUndefined(selected->function)) {
    JSValue returned = JS_Call(runtime->context, selected->function, JS_UNDEFINED, 0, NULL);
    if (JS_IsException(returned)) {
      JSValue error = JS_GetException(runtime->context);
      code = qaplet_execution_check(runtime);
      if (!code) {
        qaplet_capture_exception(runtime, error);
        code = qaplet_module_exception_code(runtime, error);
      }
      JS_FreeValue(runtime->context, error);
    }
    JS_FreeValue(runtime->context, returned);
  } else callback(runtime, &timer, userdata);
  runtime->timer_callback_active = 0;
  pthread_mutex_lock(&runtime->mutex);
  JSValue released = JS_UNDEFINED;
  if (selected->period && selected->state == 2 && qaplet_clock_locked(runtime, &now)) {
    /* Keep the accepted origin, skipping missed periods without a catch-up
     * burst. Callback return values never participate in rearming. */
    uint64_t steps = (now - selected->deadline) / selected->period + 1;
    if (steps > (UINT64_MAX - selected->deadline) / selected->period) {
      qaplet_control_failure_locked(runtime, EOVERFLOW);
      code = QAPLET_CONTROL_FAILURE;
    } else {
      selected->deadline += steps * selected->period;
      selected->state = 1;
    }
  }
  if (selected->state != 1) {
    selected->state = selected->referenced = 0;
    --runtime->timer_count;
    released = selected->function; selected->function = JS_UNDEFINED;
  }
  pthread_mutex_unlock(&runtime->mutex);
  JS_FreeValue(runtime->context, released);
  return code;
}

static JSClassID timer_class;
static pthread_once_t timer_class_once = PTHREAD_ONCE_INIT;
static void allocate_class(void) { JS_NewClassID(&timer_class); }
static void timer_finalizer(JSRuntime *engine, JSValue value) { js_free_rt(engine, JS_GetOpaque(value, timer_class)); }
static JSValue timer_error(JSContext *context, qaplet_code code) {
  JSValue error = JS_NewError(context);
  if (JS_IsException(error)) return error;
  if (JS_DefinePropertyValueStr(context, error, "code", JS_NewInt32(context, code), JS_PROP_C_W_E) < 0 ||
      JS_DefinePropertyValueStr(context, error, "message", JS_NewString(context, "Timer operation failed"), JS_PROP_C_W_E) < 0) {
    JS_FreeValue(context, error); return JS_EXCEPTION;
  }
  return JS_Throw(context, error);
}
static JSValue timer_method(JSContext *context, JSValueConst self, int argc, JSValueConst *argv, int magic) {
  (void)argc; (void)argv;
  qaplet_runtime *runtime = JS_GetContextOpaque(context);
  qaplet_timer *token = JS_GetOpaque(self, timer_class);
  if (!token) return timer_error(context, QAPLET_INVALID_HANDLE);
  /* JS runs on the owner under a drive/eval. It is an explicit safe seam. */
  int prior = runtime->timer_callback_active; runtime->timer_callback_active = 1;
  qaplet_code code;
  if (magic == 3) {
    pthread_mutex_lock(&runtime->mutex);
    qaplet_timer_slot *slot = lookup(runtime, token);
    int referenced = slot && slot->referenced;
    pthread_mutex_unlock(&runtime->mutex);
    runtime->timer_callback_active = prior;
    return slot ? JS_NewBool(context, referenced) : timer_error(context, QAPLET_INVALID_HANDLE);
  }
  code = magic == 0 ? qaplet_timer_cancel(runtime, token, NULL) : qaplet_timer_ref(runtime, token, magic == 1, NULL);
  runtime->timer_callback_active = prior;
  return code ? timer_error(context, code) : JS_UNDEFINED;
}
static void unused_callback(qaplet_runtime *r, const qaplet_timer *t, void *p) { (void)r; (void)t; (void)p; }
static JSValue set_timer(JSContext *context, JSValueConst self, int argc, JSValueConst *argv, int kind) {
  (void)self;
  qaplet_runtime *runtime = JS_GetContextOpaque(context);
  double delay=0;
  if (argc != (kind==2 ? 1 : 2) || !JS_IsFunction(context, argv[0]) || (kind!=2 && (!JS_IsNumber(argv[1]) ||
      JS_ToFloat64(context, &delay, argv[1]) || !isfinite(delay) || delay < (kind==1 ? 1 : 0) || floor(delay) != delay || delay > 9007199254740991.0)))
    return timer_error(context, QAPLET_INVALID_ARGUMENT);
  JSValue handle = JS_NewObjectClass(context, timer_class);
  if (JS_IsException(handle)) return handle;
  qaplet_timer *token = js_mallocz(context, sizeof(*token));
  if (!token) { JS_FreeValue(context, handle); return JS_EXCEPTION; }
  JS_SetOpaque(handle, token);
  int prior = runtime->timer_callback_active; runtime->timer_callback_active = 1;
  qaplet_code code = kind==2 ? qaplet_immediate_create(runtime, unused_callback, NULL, token, NULL) :
    kind==1 ? qaplet_interval_create(runtime, (uint64_t)delay, unused_callback, NULL, token, NULL) :
    qaplet_timer_create(runtime, (uint64_t)delay, unused_callback, NULL, token, NULL);
  runtime->timer_callback_active = prior;
  if (code) { JS_FreeValue(context, handle); return timer_error(context, code); }
  runtime->timers[token->slot].function = JS_DupValue(context, argv[0]);
  return handle;
}
static JSValue set_timeout(JSContext *c, JSValueConst s, int n, JSValueConst *v) { return set_timer(c,s,n,v,0); }
static JSValue set_interval(JSContext *c, JSValueConst s, int n, JSValueConst *v) { return set_timer(c,s,n,v,1); }
static JSValue set_immediate(JSContext *c, JSValueConst s, int n, JSValueConst *v) { return set_timer(c,s,n,v,2); }
static JSValue clear_timeout(JSContext *context, JSValueConst self, int argc, JSValueConst *argv) {
  (void)self;
  return argc == 1 ? timer_method(context, argv[0], 0, NULL, 0) : timer_error(context, QAPLET_INVALID_ARGUMENT);
}
static const JSCFunctionListEntry timer_methods[] = {
  JS_CFUNC_MAGIC_DEF("cancel", 0, timer_method, 0),
  JS_CFUNC_MAGIC_DEF("ref", 0, timer_method, 1),
  JS_CFUNC_MAGIC_DEF("unref", 0, timer_method, 2),
  JS_CFUNC_MAGIC_DEF("hasRef", 0, timer_method, 3),
};
static int initialize_timers(JSContext *context, JSModuleDef *module) {
  const char *names[] = {"setTimeout", "clearTimeout", "setInterval", "clearInterval", "setImmediate", "clearImmediate"};
  JSCFunction *callbacks[] = {set_timeout, clear_timeout, set_interval, clear_timeout, set_immediate, clear_timeout};
  for (unsigned i=0;i<6;++i) {
    JSValue function = JS_NewCFunction(context, callbacks[i], names[i], i%2 || i==4 ? 1 : 2);
    if (JS_IsException(function) || JS_SetModuleExport(context, module, names[i], function)<0) return -1;
  }
  return 0;
}
JSModuleDef *qaplet_timers_load(qaplet_runtime *runtime) {
  pthread_once(&timer_class_once, allocate_class);
  JSClassDef definition = {"QapletTimer", .finalizer = timer_finalizer};
  if (JS_NewClass(runtime->engine, timer_class, &definition) < 0) return NULL;
  JSValue prototype = JS_NewObject(runtime->context);
  if (JS_IsException(prototype)) return NULL;
  if (JS_SetPropertyFunctionList(runtime->context, prototype, timer_methods, sizeof(timer_methods) / sizeof(*timer_methods)) < 0) {
    JS_FreeValue(runtime->context, prototype); return NULL;
  }
  JS_SetClassProto(runtime->context, timer_class, prototype);
  JSModuleDef *module = JS_NewCModule(runtime->context, "qaplet:timers", initialize_timers);
  const char *names[] = {"setTimeout", "clearTimeout", "setInterval", "clearInterval", "setImmediate", "clearImmediate"};
  if (module) for (unsigned i=0;i<6;++i) if (JS_AddModuleExport(runtime->context,module,names[i])<0) return NULL;
  return module;
}
