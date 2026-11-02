/* SPDX-License-Identifier: MIT */
#include "runtime_internal.h"
#include <stdlib.h>
#include <string.h>

int qaplet_utf8_valid(const char *text, size_t length) {
  const unsigned char *bytes = (const unsigned char *)text;
  for (size_t i = 0; i < length;) {
    unsigned char first = bytes[i++];
    if (first < 0x80) continue;
    unsigned count;
    uint32_t point, minimum;
    if (first >= 0xc2 && first <= 0xdf) { count = 1; point = first & 0x1f; minimum = 0x80; }
    else if (first >= 0xe0 && first <= 0xef) { count = 2; point = first & 0x0f; minimum = 0x800; }
    else if (first >= 0xf0 && first <= 0xf4) { count = 3; point = first & 7; minimum = 0x10000; }
    else return 0;
    if (count > length - i) return 0;
    while (count--) {
      unsigned char next = bytes[i++];
      if ((next & 0xc0) != 0x80) return 0;
      point = (point << 6) | (next & 0x3f);
    }
    if (point < minimum || point > 0x10ffff || (point >= 0xd800 && point <= 0xdfff)) return 0;
  }
  return 1;
}

qaplet_code qaplet_values_init(qaplet_runtime *runtime) {
  JSContext *context = runtime->context;
  runtime->string_char_code_at = JS_UNDEFINED;
  runtime->typed_array_byte_length = JS_UNDEFINED;
  JSValue global = JS_GetGlobalObject(context);
  JSValue constructor = JS_GetPropertyStr(context, global, "String");
  JS_FreeValue(context, global);
  if (JS_IsException(constructor)) return QAPLET_MEMORY_LIMIT;
  JSValue prototype = JS_GetPropertyStr(context, constructor, "prototype");
  JS_FreeValue(context, constructor);
  if (JS_IsException(prototype)) return QAPLET_MEMORY_LIMIT;
  runtime->string_char_code_at = JS_GetPropertyStr(context, prototype, "charCodeAt");
  JS_FreeValue(context, prototype);
  if (JS_IsException(runtime->string_char_code_at)) return QAPLET_MEMORY_LIMIT;
  JSValue zero = JS_NewInt32(context, 0);
  JSValue exemplar = JS_NewTypedArray(context, 1, &zero, JS_TYPED_ARRAY_UINT8);
  if (JS_IsException(exemplar)) return QAPLET_MEMORY_LIMIT;
  runtime->uint8_array_class = JS_GetClassID(exemplar);
  prototype = JS_GetPrototype(context, exemplar);
  JS_FreeValue(context, exemplar);
  if (JS_IsException(prototype)) return QAPLET_MEMORY_LIMIT;
  JSValue typed_prototype = JS_GetPrototype(context, prototype);
  JS_FreeValue(context, prototype);
  if (JS_IsException(typed_prototype)) return QAPLET_MEMORY_LIMIT;
  JSAtom atom = JS_NewAtom(context, "byteLength");
  JSPropertyDescriptor descriptor;
  int found = atom ? JS_GetOwnProperty(context, &descriptor, typed_prototype, atom) : -1;
  JS_FreeAtom(context, atom);
  JS_FreeValue(context, typed_prototype);
  if (found != 1) return QAPLET_MEMORY_LIMIT;
  runtime->typed_array_byte_length = descriptor.getter;
  JS_FreeValue(context, descriptor.value);
  JS_FreeValue(context, descriptor.setter);
  exemplar = JS_NewArrayBufferCopy(context, NULL, 0);
  if (JS_IsException(exemplar)) return QAPLET_MEMORY_LIMIT;
  runtime->array_buffer_class = JS_GetClassID(exemplar);
  JS_FreeValue(context, exemplar);
  runtime->diagnostic_message_atom = JS_NewAtom(context, "message");
  return runtime->diagnostic_message_atom ? QAPLET_OK : QAPLET_MEMORY_LIMIT;
}

void qaplet_values_reset(qaplet_runtime *runtime) {
  if (runtime->result_data) {
    free(runtime->result_data);
    pthread_mutex_lock(&runtime->mutex);
    runtime->snapshot.native_current_bytes -= runtime->result.length;
    pthread_mutex_unlock(&runtime->mutex);
    runtime->result_data = NULL;
  }
  runtime->result = (qaplet_result)QAPLET_RESULT_INIT;
  runtime->has_safe_diagnostic = runtime->safe_diagnostic_truncated = 0;
  runtime->safe_diagnostic_length = 0;
}

void qaplet_values_dispose(qaplet_runtime *runtime) {
  qaplet_values_reset(runtime);
  JS_FreeValue(runtime->context, runtime->string_char_code_at);
  JS_FreeValue(runtime->context, runtime->typed_array_byte_length);
  JS_FreeAtom(runtime->context, runtime->diagnostic_message_atom);
}

static qaplet_code copy_result(qaplet_runtime *runtime, qaplet_result *result, unsigned char **data, const void *bytes, size_t length) {
  if (!length) return QAPLET_OK;
  pthread_mutex_lock(&runtime->mutex);
  uint64_t available = runtime->snapshot.limits.native_storage_bytes - runtime->snapshot.native_current_bytes;
  unsigned char *copy = length <= available ? malloc(length) : NULL;
  if (!copy) {
    qaplet_record_termination_locked(runtime, QAPLET_MEMORY_LIMIT);
    pthread_mutex_unlock(&runtime->mutex);
    return QAPLET_MEMORY_LIMIT;
  }
  runtime->snapshot.native_current_bytes += length;
  if (runtime->snapshot.native_current_bytes > runtime->snapshot.native_peak_bytes)
    runtime->snapshot.native_peak_bytes = runtime->snapshot.native_current_bytes;
  pthread_mutex_unlock(&runtime->mutex);
  memcpy(copy, bytes, length);
  *data = copy;
  result->length = length;
  return QAPLET_OK;
}

/* The saved pristine builtin receives only a verified primitive string and
 * integer index. No mutable property, conversion hook or user code is called. */
static int code_unit(qaplet_runtime *runtime, JSValueConst string, uint32_t index, int32_t *unit) {
  JSValue argument = JS_NewInt32(runtime->context, index);
  JSValue value = JS_Call(runtime->context, runtime->string_char_code_at, string, 1, &argument);
  int failed = JS_IsException(value) || JS_ToInt32(runtime->context, unit, value);
  JS_FreeValue(runtime->context, value);
  return failed;
}

static qaplet_code string_size(qaplet_runtime *runtime, JSValueConst string, size_t *size) {
  JSValue length_value = JS_GetPropertyStr(runtime->context, string, "length");
  uint32_t length;
  int failed = JS_IsException(length_value) || JS_ToUint32(runtime->context, &length, length_value);
  JS_FreeValue(runtime->context, length_value);
  if (failed) return QAPLET_MEMORY_LIMIT;
  if (length > runtime->snapshot.limits.bridge_value_bytes) return QAPLET_VALUE_TOO_LARGE;
  size_t total = 0;
  for (uint32_t i = 0; i < length; ++i) {
    int32_t unit;
    if (code_unit(runtime, string, i, &unit)) return QAPLET_MEMORY_LIMIT;
    if (unit >= 0xd800 && unit <= 0xdbff) {
      if (++i == length) return QAPLET_INVALID_UTF8;
      if (code_unit(runtime, string, i, &unit)) return QAPLET_MEMORY_LIMIT;
      if (unit < 0xdc00 || unit > 0xdfff) return QAPLET_INVALID_UTF8;
      total += 4;
    } else if (unit >= 0xdc00 && unit <= 0xdfff) return QAPLET_INVALID_UTF8;
    else total += unit < 0x80 ? 1 : unit < 0x800 ? 2 : 3;
    if (total > runtime->snapshot.limits.bridge_value_bytes) return QAPLET_VALUE_TOO_LARGE;
  }
  *size = total;
  return QAPLET_OK;
}

static void capture_text(qaplet_runtime *runtime, JSValueConst text) {
  JSContext *context = runtime->context;
  JSValue length_value = JS_GetPropertyStr(context, text, "length");
  uint32_t length;
  int failed = JS_IsException(length_value) || JS_ToUint32(context, &length, length_value);
  JS_FreeValue(context, length_value);
  if (failed) { JS_FreeValue(context, JS_GetException(context)); return; }
  size_t used = 0;
  int truncated = 0;
  for (uint32_t i = 0; i < length; ++i) {
    int32_t unit;
    if (code_unit(runtime, text, i, &unit)) { JS_FreeValue(context, JS_GetException(context)); return; }
    uint32_t point = unit;
    if (unit >= 0xd800 && unit <= 0xdbff) {
      if (++i == length) return;
      if (code_unit(runtime, text, i, &unit)) { JS_FreeValue(context, JS_GetException(context)); return; }
      if (unit < 0xdc00 || unit > 0xdfff) return;
      point = 0x10000 + ((point - 0xd800) << 10) + (unit - 0xdc00);
    } else if (unit >= 0xdc00 && unit <= 0xdfff) return;
    unsigned char encoded[4];
    size_t count;
    if (!point) { encoded[0] = '\\'; encoded[1] = '0'; count = 2; }
    else if (point < 0x80) { encoded[0] = point; count = 1; }
    else if (point < 0x800) {
      encoded[0] = 0xc0 | (point >> 6); encoded[1] = 0x80 | (point & 63); count = 2;
    } else if (point < 0x10000) {
      encoded[0] = 0xe0 | (point >> 12); encoded[1] = 0x80 | ((point >> 6) & 63);
      encoded[2] = 0x80 | (point & 63); count = 3;
    } else {
      encoded[0] = 0xf0 | (point >> 18); encoded[1] = 0x80 | ((point >> 12) & 63);
      encoded[2] = 0x80 | ((point >> 6) & 63); encoded[3] = 0x80 | (point & 63); count = 4;
    }
    if (count > sizeof(runtime->safe_diagnostic) - 1 - used) { truncated = 1; break; }
    memcpy(runtime->safe_diagnostic + used, encoded, count);
    used += count;
  }
  runtime->safe_diagnostic[used] = 0;
  runtime->safe_diagnostic_length = used;
  runtime->safe_diagnostic_truncated = truncated;
  runtime->has_safe_diagnostic = 1;
}

void qaplet_capture_exception(qaplet_runtime *runtime, JSValueConst exception) {
  if (runtime->has_safe_diagnostic) return;
  if (JS_IsString(exception)) capture_text(runtime, exception);
  else if (JS_IsError(runtime->context, exception)) {
    /* Only the intrinsic Error brand reaches descriptor inspection. Applying
     * this API to arbitrary objects would invoke Proxy/exotic traps. */
    JSPropertyDescriptor descriptor;
    int found = JS_GetOwnProperty(runtime->context, &descriptor, exception, runtime->diagnostic_message_atom);
    if (found == 1) {
      if (!(descriptor.flags & JS_PROP_GETSET) && JS_IsString(descriptor.value)) capture_text(runtime, descriptor.value);
      JS_FreeValue(runtime->context, descriptor.value);
      JS_FreeValue(runtime->context, descriptor.getter);
      JS_FreeValue(runtime->context, descriptor.setter);
    } else if (found < 0) JS_FreeValue(runtime->context, JS_GetException(runtime->context));
  }
}

static qaplet_code store_bytes(qaplet_runtime *runtime, JSValueConst value, qaplet_result *result, unsigned char **data) {
  JSContext *context = runtime->context;
  size_t offset, ignored_length, element_size, backing_length;
  JSValue buffer = JS_GetTypedArrayBuffer(context, value, &offset, &ignored_length, &element_size);
  if (JS_IsException(buffer)) {
    JS_FreeValue(context, JS_GetException(context));
    return QAPLET_VALUE_TYPE_UNSUPPORTED;
  }
  qaplet_code code = QAPLET_VALUE_TYPE_UNSUPPORTED;
  if (JS_GetClassID(buffer) == runtime->array_buffer_class) {
    /* JS_GetTypedArrayBuffer's length is stale for resized length-tracking
     * views in the pinned engine. The saved native getter reads current state. */
    JSValue size = JS_Call(context, runtime->typed_array_byte_length, value, 0, NULL);
    double length;
    if (JS_IsException(size) || JS_ToFloat64(context, &length, size)) {
      JS_FreeValue(context, JS_GetException(context));
      code = QAPLET_MEMORY_LIMIT;
    } else if (length > runtime->snapshot.limits.bridge_value_bytes) code = QAPLET_VALUE_TOO_LARGE;
    else {
      unsigned char *bytes = JS_GetArrayBuffer(context, &backing_length, buffer);
      size_t count = (size_t)length;
      if (offset <= backing_length && count <= backing_length - offset && (bytes || !count)) {
        result->type = QAPLET_VALUE_BYTES;
        code = copy_result(runtime, result, data, count ? bytes + offset : NULL, count);
      }
    }
    JS_FreeValue(context, size);
  }
  JS_FreeValue(context, buffer);
  return code;
}

qaplet_code qaplet_check_structure(const void *value, size_t known) {
  if (!value) return QAPLET_INVALID_ARGUMENT;
  const uint32_t *prefix = value;
  if (prefix[0] < known || prefix[0] > 65536 || prefix[1] != QAPLET_ABI_VERSION)
    return QAPLET_ABI_MISMATCH;
  const unsigned char *bytes = value;
  for (size_t i = known; i < prefix[0]; ++i)
    if (bytes[i]) return QAPLET_ABI_MISMATCH;
  return QAPLET_OK;
}

qaplet_code qaplet_check_diagnostic(qaplet_diagnostic *diagnostic) {
  if (!diagnostic) return QAPLET_OK;
  qaplet_code code = qaplet_check_structure(diagnostic, sizeof(*diagnostic));
  if (code) return code;
  if (diagnostic->capacity && !diagnostic->message) return QAPLET_INVALID_ARGUMENT;
  return QAPLET_OK;
}

qaplet_code qaplet_report(qaplet_diagnostic *diagnostic, qaplet_code code) {
  static const char *const messages[] = {
    "OK", "INVALID_ARGUMENT", "ABI_MISMATCH", "WRONG_THREAD", "BUSY", "BAD_STATE",
    "UNSUPPORTED", "JS_EXCEPTION", "VALUE_TYPE_UNSUPPORTED", "BUFFER_TOO_SMALL",
    "SOURCE_TOO_LARGE", "MEMORY_LIMIT", "INVALID_UTF8", "VALUE_TOO_LARGE",
    "MODULE_RESOLVE", "MODULE_LOAD", "MODULE_LIMIT"
  };
  if (diagnostic) {
    const char *message = (unsigned)code < sizeof(messages)/sizeof(messages[0]) ? messages[code] :
        code == QAPLET_DEADLINE_EXCEEDED ? "DEADLINE_EXCEEDED" :
        code == QAPLET_DISPATCH_LIMIT ? "DISPATCH_LIMIT" :
        code == QAPLET_HOST_ERROR ? "HOST_ERROR" : code == QAPLET_NO_HANDLER ? "NO_HANDLER" :
        code == QAPLET_QUEUE_FULL ? "QUEUE_FULL" : code == QAPLET_PAYLOAD_TOO_LARGE ? "PAYLOAD_TOO_LARGE" : "UNKNOWN";
    size_t length = strlen(message), copied = 0;
    if (diagnostic->capacity) {
      copied = length < diagnostic->capacity - 1 ? length : diagnostic->capacity - 1;
      memcpy(diagnostic->message, message, copied);
      diagnostic->message[copied] = 0;
    }
    diagnostic->code = code;
    diagnostic->length = length;
    diagnostic->flags = copied < length ? QAPLET_DIAGNOSTIC_TRUNCATED : 0;
  }
  return code;
}

qaplet_code qaplet_report_result(qaplet_runtime *runtime, qaplet_diagnostic *diagnostic, qaplet_code code) {
  if (!runtime->has_safe_diagnostic || code != QAPLET_JS_EXCEPTION) return qaplet_report(diagnostic, code);
  if (diagnostic) {
    size_t length = runtime->safe_diagnostic_length, copied = 0;
    if (diagnostic->capacity) {
      copied = length < diagnostic->capacity - 1 ? length : diagnostic->capacity - 1;
      /* Preserve UTF-8 codepoint boundaries when the caller supplies a short
       * diagnostic buffer. length still describes the complete bounded text. */
      while (copied && copied < length && ((unsigned char)runtime->safe_diagnostic[copied] & 0xc0) == 0x80) --copied;
      memcpy(diagnostic->message, runtime->safe_diagnostic, copied);
      diagnostic->message[copied] = 0;
    }
    diagnostic->code = code;
    diagnostic->length = length;
    diagnostic->flags = copied < length || runtime->safe_diagnostic_truncated ? QAPLET_DIAGNOSTIC_TRUNCATED : 0;
  }
  return code;
}

qaplet_code qaplet_copy_js_value(qaplet_runtime *runtime, JSValueConst value, qaplet_result *result, unsigned char **data) {
  if (JS_IsUndefined(value)) result->type = QAPLET_VALUE_UNDEFINED;
  else if (JS_IsNull(value)) result->type = QAPLET_VALUE_NULL;
  else if (JS_IsBool(value)) {
    result->type = QAPLET_VALUE_BOOL;
    result->boolean = JS_ToBool(runtime->context, value);
  } else if (JS_IsNumber(value)) {
    result->type = QAPLET_VALUE_DOUBLE;
    JS_ToFloat64(runtime->context, &result->number, value);
  } else if (JS_IsBigInt(runtime->context, value)) {
    uint64_t low, high;
    /* The 64-bit conversion API truncates modulo 2^64. Inspect an exact
     * signed-128 conversion before choosing the public 64-bit type. */
    if (JS_ToBigInt128Sat(runtime->context, &low, &high, value))
      return QAPLET_VALUE_TYPE_UNSUPPORTED;
    if ((high == 0 && low <= INT64_MAX) || (high == UINT64_MAX && low > INT64_MAX)) {
      result->type = QAPLET_VALUE_INT64;
      memcpy(&result->signed_integer, &low, sizeof(low));
    } else if (high == 0) {
      result->type = QAPLET_VALUE_UINT64;
      result->unsigned_integer = low;
    } else return QAPLET_VALUE_TYPE_UNSUPPORTED;
  } else if (JS_IsObject(value) && JS_GetClassID(value) == runtime->uint8_array_class) {
    return store_bytes(runtime, value, result, data);
  } else if (JS_IsString(value)) {
    size_t length, encoded;
    qaplet_code code = string_size(runtime, value, &length);
    if (code) return code;
    const char *text = JS_ToCStringLen(runtime->context, &encoded, value);
    if (!text) return QAPLET_MEMORY_LIMIT;
    result->type = QAPLET_VALUE_STRING;
    code = encoded == length && qaplet_utf8_valid(text, encoded) ? copy_result(runtime, result, data, text, length) : QAPLET_INVALID_UTF8;
    JS_FreeCString(runtime->context, text);
    return code;
  } else return QAPLET_VALUE_TYPE_UNSUPPORTED;
  return QAPLET_OK;
}

qaplet_code qaplet_store_scalar(qaplet_runtime *runtime, JSValueConst value) {
  return qaplet_copy_js_value(runtime, value, &runtime->result, &runtime->result_data);
}

qaplet_code qaplet_get_result(qaplet_runtime *runtime, qaplet_result *output, qaplet_diagnostic *diagnostic) {
  qaplet_code code = qaplet_check_diagnostic(diagnostic);
  if (code) return code;
  code = qaplet_check_structure(output, sizeof(*output));
  if (!code && output->capacity && !output->buffer) code = QAPLET_INVALID_ARGUMENT;
  if (!code) code = qaplet_owner(runtime);
  if (!code && (runtime->snapshot.lifecycle == QAPLET_CLOSED || !runtime->has_result)) code = QAPLET_BAD_STATE;
  if (code) return qaplet_report(diagnostic, code);
  {
    void *buffer = output->buffer;
    size_t capacity = output->capacity;
    memcpy((char *)output + 8, (char *)&runtime->result + 8, sizeof(*output) - 8);
    output->buffer = buffer;
    output->capacity = capacity;
    code = runtime->result.code;
    if (!code && output->length > capacity) code = QAPLET_BUFFER_TOO_SMALL;
    if (!code && output->length) memcpy(buffer, runtime->result_data, output->length);
  }
  return qaplet_report_result(runtime, diagnostic, code);
}
