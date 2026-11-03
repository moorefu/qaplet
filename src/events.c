/* SPDX-License-Identifier: MIT */
#include "runtime_internal.h"
#include <string.h>

/* Every preallocated slot has the same inline byte capacity. No slot retains
 * pointers, JS values or cleanup callbacks. The final slot is owner scratch. */
typedef struct {
  uint64_t token;
  size_t length;
  uint32_t type;
  unsigned char payload[];
} event_slot;

qaplet_code qaplet_events_check_limits(const qaplet_limits *limits) {
  if (limits->event_payload_bytes > SIZE_MAX - sizeof(event_slot) - 7 ||
      limits->event_queue_slots >= SIZE_MAX) return QAPLET_INVALID_ARGUMENT;
  size_t stride = (sizeof(event_slot) + (size_t)limits->event_payload_bytes + 7) & ~(size_t)7;
  if (limits->event_queue_slots + 1 > SIZE_MAX / stride) return QAPLET_INVALID_ARGUMENT;
  return QAPLET_OK;
}

qaplet_code qaplet_events_init(qaplet_runtime *runtime) {
  runtime->event_stride = (sizeof(event_slot) + (size_t)runtime->snapshot.limits.event_payload_bytes + 7) & ~(size_t)7;
  runtime->event_storage_size = runtime->event_stride * ((size_t)runtime->snapshot.limits.event_queue_slots + 1);
  runtime->event_slots = qaplet_native_alloc(runtime, runtime->event_storage_size);
  if (!runtime->event_slots) return QAPLET_MEMORY_LIMIT;
  runtime->event_accepting = 1;
  return QAPLET_OK;
}

void qaplet_events_dispose(qaplet_runtime *runtime) {
  pthread_mutex_lock(&runtime->mutex);
  runtime->event_accepting = runtime->event_handler_ready = 0;
  uint64_t depth = runtime->snapshot.queue_depth;
  runtime->snapshot.events_dropped = depth > UINT64_MAX - runtime->snapshot.events_dropped ?
      UINT64_MAX : runtime->snapshot.events_dropped + depth;
  runtime->snapshot.queue_depth = 0;
  pthread_mutex_unlock(&runtime->mutex);
  JS_FreeValue(runtime->context, runtime->event_handler);
  runtime->event_handler = JS_UNDEFINED;
  qaplet_native_free(runtime, runtime->event_slots, runtime->event_storage_size);
  runtime->event_slots = NULL;
}

qaplet_code qaplet_post_event(qaplet_runtime *runtime, uint32_t type, uint64_t token,
                             const void *data, size_t length, qaplet_diagnostic *diagnostic) {
  qaplet_code code = qaplet_check_diagnostic(diagnostic);
  if (code) return code;
  if (!runtime) return qaplet_report(diagnostic, QAPLET_INVALID_ARGUMENT);
  if (!data && length) code = QAPLET_INVALID_ARGUMENT;
  if (!code && length > runtime->snapshot.limits.event_payload_bytes) code = QAPLET_PAYLOAD_TOO_LARGE;
  pthread_mutex_lock(&runtime->mutex);
  if (!code && (runtime->snapshot.lifecycle != QAPLET_OPEN || !runtime->event_accepting)) code = QAPLET_BAD_STATE;
  if (!code && !runtime->event_handler_ready) code = QAPLET_NO_HANDLER;
  if (!code && runtime->snapshot.queue_depth == runtime->snapshot.limits.event_queue_slots) code = QAPLET_QUEUE_FULL;
  if (code) {
    if (runtime->snapshot.events_rejected != UINT64_MAX) ++runtime->snapshot.events_rejected;
  }
  else {
    size_t index = (runtime->event_head + (size_t)runtime->snapshot.queue_depth) %
        (size_t)runtime->snapshot.limits.event_queue_slots;
    event_slot *slot = (event_slot *)(runtime->event_slots + runtime->event_stride * index);
    slot->type = type; slot->token = token; slot->length = length;
    if (length) memcpy(slot->payload, data, length);
    if (runtime->snapshot.events_accepted != UINT64_MAX) ++runtime->snapshot.events_accepted;
    if (++runtime->snapshot.queue_depth > runtime->snapshot.queue_peak)
      runtime->snapshot.queue_peak = runtime->snapshot.queue_depth;
    qaplet_wake_locked(runtime);
  }
  pthread_mutex_unlock(&runtime->mutex);
  return qaplet_report(diagnostic, code);
}

int qaplet_events_pending(qaplet_runtime *runtime) {
  pthread_mutex_lock(&runtime->mutex);
  int pending = runtime->event_accepting && runtime->snapshot.lifecycle == QAPLET_OPEN && runtime->snapshot.queue_depth;
  pthread_mutex_unlock(&runtime->mutex);
  return pending;
}

qaplet_code qaplet_events_deliver(qaplet_runtime *runtime, unsigned *claimed) {
  *claimed = 0;
  event_slot *copy = (event_slot *)(runtime->event_slots + runtime->event_stride *
      (size_t)runtime->snapshot.limits.event_queue_slots);
  pthread_mutex_lock(&runtime->mutex);
  qaplet_code code = qaplet_execution_dispatch_locked(runtime);
  if (code || !runtime->snapshot.queue_depth) {
    pthread_mutex_unlock(&runtime->mutex);
    return code;
  }
  event_slot *slot = (event_slot *)(runtime->event_slots + runtime->event_stride * runtime->event_head);
  memcpy(copy, slot, sizeof(*slot));
  if (slot->length) memcpy(copy->payload, slot->payload, slot->length);
  runtime->event_head = (runtime->event_head + 1) % (size_t)runtime->snapshot.limits.event_queue_slots;
  --runtime->snapshot.queue_depth;
  if (runtime->snapshot.events_consumed != UINT64_MAX) ++runtime->snapshot.events_consumed;
  *claimed = 1;
  pthread_mutex_unlock(&runtime->mutex);

  JSContext *context = runtime->context;
  JSValue buffer = JS_NewArrayBufferCopy(context, copy->payload, copy->length);
  JSValue payload = JS_EXCEPTION, event = JS_UNDEFINED, returned = JS_EXCEPTION;
  if (!JS_IsException(buffer)) {
    JSValue args[] = {buffer, JS_NewInt32(context, 0), JS_NewInt64(context, copy->length)};
    payload = JS_NewTypedArray(context, 3, args, JS_TYPED_ARRAY_UINT8);
  }
  JS_FreeValue(context, buffer);
  if (!JS_IsException(payload)) {
    event = JS_NewObject(context);
    if (!JS_IsException(event)) {
      JSValue type = JS_NewUint32(context, copy->type);
      if (!JS_IsException(type)) {
        int defined = JS_DefinePropertyValueStr(context, event, "type", type, JS_PROP_C_W_E);
        if (defined >= 0) {
          JSValue token = JS_NewBigUint64(context, copy->token);
          if (!JS_IsException(token) &&
              JS_DefinePropertyValueStr(context, event, "token", token, JS_PROP_C_W_E) >= 0 &&
              JS_DefinePropertyValueStr(context, event, "payload", JS_DupValue(context, payload), JS_PROP_C_W_E) >= 0 &&
              !qaplet_execution_check(runtime))
            returned = JS_Call(context, runtime->event_handler, JS_UNDEFINED, 1, &event);
        }
      }
    }
  }
  if (JS_IsException(returned)) {
    JSValue error = JS_GetException(context);
    code = qaplet_execution_check(runtime);
    if (!code) {
      qaplet_capture_exception(runtime, error);
      code = qaplet_module_exception_code(runtime, error);
    }
    JS_FreeValue(context, error);
  }
  JS_FreeValue(context, returned);
  JS_FreeValue(context, event);
  JS_FreeValue(context, payload);
  return code;
}

static JSValue on_event(JSContext *context, JSValueConst self,
                        int argc, JSValueConst *argv) {
  (void)self;
  qaplet_runtime *runtime = JS_GetContextOpaque(context);
  if (argc != 1 || !JS_IsFunction(context, argv[0]))
    return JS_ThrowTypeError(context, "onEvent requires one function");
  if (!JS_IsUndefined(runtime->event_handler))
    return JS_ThrowTypeError(context, "Event handler already registered");
  runtime->event_handler = JS_DupValue(context, argv[0]);
  pthread_mutex_lock(&runtime->mutex);
  runtime->event_handler_ready = 1;
  pthread_mutex_unlock(&runtime->mutex);
  return JS_UNDEFINED;
}

static int initialize_events(JSContext *context, JSModuleDef *module) {
  JSValue function = JS_NewCFunction(context, on_event, "onEvent", 1);
  if (JS_IsException(function)) return -1;
  return JS_SetModuleExport(context, module, "onEvent", function);
}

JSModuleDef *qaplet_events_load(qaplet_runtime *runtime) {
  JSModuleDef *module = JS_NewCModule(runtime->context, "qaplet:events", initialize_events);
  if (module && JS_AddModuleExport(runtime->context, module, "onEvent") < 0) return NULL;
  return module;
}
