/* SPDX-License-Identifier: MIT */
#include "runtime_internal.h"
#include <string.h>

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
    "SOURCE_TOO_LARGE", "MEMORY_LIMIT"
  };
  if (diagnostic) {
    const char *message = code <= QAPLET_MEMORY_LIMIT ? messages[code] :
        code == QAPLET_DEADLINE_EXCEEDED ? "DEADLINE_EXCEEDED" :
        code == QAPLET_DISPATCH_LIMIT ? "DISPATCH_LIMIT" : "UNKNOWN";
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

qaplet_code qaplet_store_scalar(qaplet_runtime *runtime, JSValueConst value) {
  qaplet_result *result = &runtime->result;
  if (JS_IsUndefined(value)) result->type = QAPLET_VALUE_UNDEFINED;
  else if (JS_IsNull(value)) result->type = QAPLET_VALUE_NULL;
  else if (JS_IsBool(value)) {
    result->type = QAPLET_VALUE_BOOL;
    result->boolean = JS_ToBool(runtime->context, value);
  } else if (JS_IsNumber(value)) {
    result->type = QAPLET_VALUE_DOUBLE;
    JS_ToFloat64(runtime->context, &result->number, value);
  } else return QAPLET_VALUE_TYPE_UNSUPPORTED;
  return QAPLET_OK;
}

qaplet_code qaplet_get_result(qaplet_runtime *runtime, qaplet_result *output, qaplet_diagnostic *diagnostic) {
  qaplet_code code = qaplet_check_diagnostic(diagnostic);
  if (code) return code;
  code = qaplet_check_structure(output, sizeof(*output));
  if (!code) code = qaplet_owner(runtime);
  if (!code && (runtime->snapshot.lifecycle == QAPLET_CLOSED || !runtime->has_result)) code = QAPLET_BAD_STATE;
  if (!code) {
    memcpy((char *)output + 8, (char *)&runtime->result + 8, sizeof(*output) - 8);
    code = runtime->result.code;
  }
  return qaplet_report(diagnostic, code);
}
