/* SPDX-License-Identifier: MIT */
#include "runtime_internal.h"
#include <limits.h>
#include <string.h>

/* One allocation owns the complete copied declaration, names and payloads.
 * These records outlive JS_FreeRuntime, including native engine finalizers. */
struct qaplet_host_record {
  qaplet_host_record *next;
  size_t allocation_size, count;
  uint32_t id;
  char *name;
  qaplet_host_export exports[];
};

static qaplet_code valid_value(const qaplet_value *value, uint64_t limit) {
  qaplet_code code = qaplet_check_structure(value, sizeof(*value));
  if (code) return code;
  if ((unsigned)value->type > QAPLET_VALUE_BYTES) return QAPLET_VALUE_TYPE_UNSUPPORTED;
  if (value->type == QAPLET_VALUE_BOOL && value->boolean > 1) return QAPLET_INVALID_ARGUMENT;
  if (value->type == QAPLET_VALUE_STRING || value->type == QAPLET_VALUE_BYTES) {
    if (value->length && !value->data) return QAPLET_INVALID_ARGUMENT;
    if (value->length > limit) return QAPLET_VALUE_TOO_LARGE;
    if (value->type == QAPLET_VALUE_STRING && !qaplet_utf8_valid(value->data, value->length))
      return QAPLET_INVALID_UTF8;
  }
  return QAPLET_OK;
}

static JSValue to_js(qaplet_runtime *runtime, const qaplet_value *value) {
  JSContext *context = runtime->context;
  switch (value->type) {
    case QAPLET_VALUE_UNDEFINED: return JS_UNDEFINED;
    case QAPLET_VALUE_NULL: return JS_NULL;
    case QAPLET_VALUE_BOOL: return JS_NewBool(context, value->boolean);
    case QAPLET_VALUE_DOUBLE: return JS_NewFloat64(context, value->number);
    case QAPLET_VALUE_INT64: return JS_NewBigInt64(context, value->signed_integer);
    case QAPLET_VALUE_UINT64: return JS_NewBigUint64(context, value->unsigned_integer);
    case QAPLET_VALUE_STRING: return JS_NewStringLen(context, value->data ? value->data : "", value->length);
    case QAPLET_VALUE_BYTES: {
      JSValue buffer = JS_NewArrayBufferCopy(context, value->data, value->length);
      if (JS_IsException(buffer)) return buffer;
      /* The public engine constructor expects its three argument slots to be
       * present even when optional JS arguments are omitted. */
      JSValue arguments[] = {buffer, JS_UNDEFINED, JS_UNDEFINED};
      JSValue result = JS_NewTypedArray(context, 3, arguments, JS_TYPED_ARRAY_UINT8);
      JS_FreeValue(context, buffer);
      return result;
    }
  }
  return JS_EXCEPTION;
}

qaplet_code qaplet_host_set_result(qaplet_runtime *runtime, uint64_t call_id,
                                  const qaplet_value *value, qaplet_diagnostic *diagnostic) {
  qaplet_code code = qaplet_check_diagnostic(diagnostic);
  if (code) return code;
  code = valid_value(value, runtime ? runtime->snapshot.limits.bridge_value_bytes : QAPLET_VALUE_BYTES_MAX);
  if (!code && !runtime) code = QAPLET_INVALID_ARGUMENT;
  if (!code && !pthread_equal(runtime->owner, pthread_self())) code = QAPLET_WRONG_THREAD;
  if (!code && (!call_id || call_id != runtime->host_active_call || runtime->host_return_set))
    code = QAPLET_BAD_STATE;
  if (!code) {
    JSValue copy = to_js(runtime, value);
    if (JS_IsException(copy)) {
      JS_FreeValue(runtime->context, JS_GetException(runtime->context));
      code = QAPLET_MEMORY_LIMIT;
    } else {
      runtime->host_return = copy;
      runtime->host_return_set = 1;
    }
  }
  return qaplet_report(diagnostic, code);
}

static int add_size(size_t *total, size_t amount) {
  if (amount > SIZE_MAX - *total) return 0;
  *total += amount;
  return 1;
}

static qaplet_code valid_name(const char *name, size_t length, int module) {
  if (!name || !length || length == SIZE_MAX || memchr(name, 0, length)) return QAPLET_INVALID_ARGUMENT;
  if (!qaplet_utf8_valid(name, length)) return QAPLET_INVALID_UTF8;
  if (module) {
    if (length <= 5 || memcmp(name, "host:", 5)) return QAPLET_INVALID_ARGUMENT;
  }
  return QAPLET_OK;
}

qaplet_code qaplet_register_host_module(qaplet_runtime *runtime, const qaplet_host_module *module,
                                      qaplet_diagnostic *diagnostic) {
  qaplet_code code = qaplet_check_diagnostic(diagnostic);
  if (code) return code;
  code = qaplet_check_structure(module, sizeof(*module));
  if (!code && (module->flags || module->reserved[0] || module->reserved[1] ||
      (module->export_count && !module->exports) || module->export_count > INT_MAX)) code = QAPLET_INVALID_ARGUMENT;
  if (!code) code = valid_name(module->name, module->name_length, 1);
  size_t size = sizeof(qaplet_host_record);
  if (!code && (module->export_count > (SIZE_MAX - size) / sizeof(qaplet_host_export) ||
      !add_size(&size, module->export_count * sizeof(qaplet_host_export)) ||
      !add_size(&size, module->name_length + 1))) code = QAPLET_INVALID_ARGUMENT;
  for (size_t i = 0; !code && i < module->export_count; ++i) {
    const qaplet_host_export *item = &module->exports[i];
    code = qaplet_check_structure(item, sizeof(*item));
    if (!code && (item->reserved || (unsigned)item->kind > QAPLET_HOST_FUNCTION ||
        (item->kind == QAPLET_HOST_FUNCTION && !item->function))) code = QAPLET_INVALID_ARGUMENT;
    if (!code) code = valid_name(item->name, item->name_length, 0);
    for (size_t j = 0; !code && j < i; ++j)
      if (item->name_length == module->exports[j].name_length &&
          !memcmp(item->name, module->exports[j].name, item->name_length)) code = QAPLET_INVALID_ARGUMENT;
    if (!code && !add_size(&size, item->name_length + 1)) code = QAPLET_INVALID_ARGUMENT;
    if (!code && item->kind == QAPLET_HOST_CONSTANT) {
      code = valid_value(&item->value, runtime ? runtime->snapshot.limits.bridge_value_bytes : QAPLET_VALUE_BYTES_MAX);
      if (!code && (item->value.type == QAPLET_VALUE_STRING || item->value.type == QAPLET_VALUE_BYTES) &&
          !add_size(&size, item->value.length)) code = QAPLET_INVALID_ARGUMENT;
    }
  }
  if (!code) code = qaplet_owner(runtime);
  if (!code && runtime->snapshot.lifecycle != QAPLET_OPEN) code = QAPLET_BAD_STATE;
  if (!code && runtime->host_registration_locked) code = QAPLET_BAD_STATE;
  for (qaplet_host_record *entry = !code ? runtime->host_modules : NULL; entry; entry = entry->next)
    if (strlen(entry->name) == module->name_length && !memcmp(entry->name, module->name, module->name_length))
      code = QAPLET_INVALID_ARGUMENT;
  if (!code && runtime->snapshot.module_count >= runtime->snapshot.limits.module_count) code = QAPLET_MODULE_LIMIT;
  if (!code && runtime->host_modules && runtime->host_modules->id == INT_MAX) code = QAPLET_MODULE_LIMIT;
  if (code) return qaplet_report(diagnostic, code);
  qaplet_host_record *entry = qaplet_native_alloc(runtime, size);
  if (!entry) return qaplet_report(diagnostic, QAPLET_MEMORY_LIMIT);
  entry->allocation_size = size; entry->count = module->export_count;
  entry->id = runtime->host_modules ? runtime->host_modules->id + 1 : 0;
  char *cursor = (char *)(entry->exports + entry->count);
  entry->name = cursor;
  memcpy(cursor, module->name, module->name_length); cursor += module->name_length; *cursor++ = 0;
  for (size_t i = 0; i < entry->count; ++i) {
    entry->exports[i] = module->exports[i];
    qaplet_host_export *item = &entry->exports[i];
    item->name = cursor;
    memcpy(cursor, module->exports[i].name, item->name_length); cursor += item->name_length; *cursor++ = 0;
    if (item->kind == QAPLET_HOST_CONSTANT &&
        (item->value.type == QAPLET_VALUE_STRING || item->value.type == QAPLET_VALUE_BYTES)) {
      if (item->value.length) memcpy(cursor, item->value.data, item->value.length);
      item->value.data = cursor; cursor += item->value.length;
    }
  }
  code = qaplet_modules_register_host(runtime, entry->name);
  if (code) qaplet_native_free(runtime, entry, size);
  else { entry->next = runtime->host_modules; runtime->host_modules = entry; }
  return qaplet_report(diagnostic, code);
}

static JSValue host_throw(qaplet_runtime *runtime, qaplet_code code) {
  JSContext *context = runtime->context;
  JSValue error = JS_NewError(context);
  if (JS_IsException(error)) return error;
  if (JS_DefinePropertyValueStr(context, error, "code", JS_NewInt32(context, code), JS_PROP_C_W_E) < 0 ||
      JS_DefinePropertyValueStr(context, error, "message", JS_NewString(context, "Host function failed"), JS_PROP_C_W_E) < 0) {
    JS_FreeValue(context, error);
    return JS_EXCEPTION;
  }
  return JS_Throw(context, error);
}

static JSValue trampoline(JSContext *context, JSValueConst self, int argc, JSValueConst *argv,
                          int magic, JSValue *data) {
  (void)self; (void)magic;
  qaplet_runtime *runtime = JS_GetContextOpaque(context);
  int32_t id, index;
  JS_ToInt32(context, &id, data[0]); JS_ToInt32(context, &index, data[1]);
  qaplet_host_record *entry = runtime->host_modules;
  while (entry && entry->id != (uint32_t)id) entry = entry->next;
  if (!entry || index < 0 || (size_t)index >= entry->count) return host_throw(runtime, QAPLET_HOST_ERROR);
  qaplet_code code = qaplet_execution_check(runtime);
  if (!code && (uint64_t)argc > runtime->snapshot.limits.host_arguments) code = QAPLET_VALUE_TOO_LARGE;
  if (!code && runtime->host_call_serial == UINT64_MAX) code = QAPLET_BAD_STATE;
  if (code) return host_throw(runtime, code);
  size_t storage = (size_t)argc * (sizeof(qaplet_result) + sizeof(qaplet_value) + sizeof(void *));
  qaplet_result *copies = storage ? qaplet_native_alloc(runtime, storage) : NULL;
  if (storage && !copies) return host_throw(runtime, QAPLET_MEMORY_LIMIT);
  qaplet_value *arguments = argc ? (qaplet_value *)(copies + argc) : NULL;
  unsigned char **payloads = argc ? (unsigned char **)(arguments + argc) : NULL;
  int converted = 0;
  for (; converted < argc; ++converted) {
    copies[converted] = (qaplet_result)QAPLET_RESULT_INIT;
    code = qaplet_copy_js_value(runtime, argv[converted], &copies[converted], &payloads[converted]);
    if (code) break;
    qaplet_result *copy = &copies[converted];
    arguments[converted] = (qaplet_value)QAPLET_VALUE_INIT;
    qaplet_value *argument = &arguments[converted];
    argument->type = copy->type; argument->boolean = copy->boolean; argument->number = copy->number;
    argument->signed_integer = copy->signed_integer; argument->unsigned_integer = copy->unsigned_integer;
    argument->data = payloads[converted]; argument->length = copy->length;
  }
  runtime->host_return = JS_UNDEFINED; runtime->host_return_set = 0;
  if (!code) {
    runtime->host_active_call = ++runtime->host_call_serial;
    qaplet_host_export *item = &entry->exports[index];
    code = item->function(runtime, runtime->host_active_call, argc, arguments, item->userdata);
    runtime->host_active_call = 0;
    if ((unsigned)code > QAPLET_PAYLOAD_TOO_LARGE) code = QAPLET_HOST_ERROR;
  }
  for (int i = 0; i <= converted && i < argc; ++i)
    qaplet_native_free(runtime, payloads[i], copies[i].length);
  qaplet_native_free(runtime, copies, storage);
  qaplet_code termination = qaplet_execution_check(runtime);
  if (termination) code = termination;
  JSValue result = runtime->host_return;
  runtime->host_return = JS_UNDEFINED;
  if (code) { JS_FreeValue(context, result); return host_throw(runtime, code); }
  return result;
}

static int initialize_module(JSContext *context, JSModuleDef *module) {
  qaplet_runtime *runtime = JS_GetContextOpaque(context);
  JSValue private = JS_GetModulePrivateValue(context, module);
  int32_t id; JS_ToInt32(context, &id, private); JS_FreeValue(context, private);
  qaplet_host_record *entry = runtime->host_modules;
  while (entry && entry->id != (uint32_t)id) entry = entry->next;
  if (!entry) return -1;
  for (size_t i = 0; i < entry->count; ++i) {
    qaplet_host_export *item = &entry->exports[i];
    JSValue data[] = {JS_NewInt32(context, entry->id), JS_NewInt32(context, i)};
    JSValue value = item->kind == QAPLET_HOST_CONSTANT ? to_js(runtime, &item->value) :
        JS_NewCFunctionData(context, trampoline, 0, 0, 2, data);
    if (JS_IsException(value)) return -1;
    if (JS_SetModuleExport(context, module, item->name, value) < 0) return -1;
  }
  return 0;
}

JSModuleDef *qaplet_host_load(qaplet_runtime *runtime, const char *name) {
  qaplet_host_record *entry = runtime->host_modules;
  while (entry && strcmp(entry->name, name)) entry = entry->next;
  if (!entry) return NULL;
  JSModuleDef *module = JS_NewCModule(runtime->context, name, initialize_module);
  if (!module) return NULL;
  if (JS_SetModulePrivateValue(runtime->context, module, JS_NewInt32(runtime->context, entry->id)) < 0) return NULL;
  for (size_t i = 0; i < entry->count; ++i)
    if (JS_AddModuleExport(runtime->context, module, entry->exports[i].name) < 0) return NULL;
  return module;
}

void qaplet_host_dispose(qaplet_runtime *runtime) {
  while (runtime->host_modules) {
    qaplet_host_record *entry = runtime->host_modules;
    runtime->host_modules = entry->next;
    qaplet_native_free(runtime, entry, entry->allocation_size);
  }
}
