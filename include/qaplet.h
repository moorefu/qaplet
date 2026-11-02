/* SPDX-License-Identifier: MIT */
#ifndef QAPLET_H
#define QAPLET_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

#define QAPLET_ABI_VERSION 1u
#define QAPLET_VALUE_BYTES_MAX (1024u * 1024u)
typedef struct qaplet_runtime qaplet_runtime;
typedef enum {
  QAPLET_OK = 0, QAPLET_INVALID_ARGUMENT = 1, QAPLET_ABI_MISMATCH = 2,
  QAPLET_WRONG_THREAD = 3, QAPLET_BUSY = 4, QAPLET_BAD_STATE = 5,
  QAPLET_UNSUPPORTED = 6, QAPLET_JS_EXCEPTION = 7,
  QAPLET_VALUE_TYPE_UNSUPPORTED = 8, QAPLET_BUFFER_TOO_SMALL = 9,
  QAPLET_SOURCE_TOO_LARGE = 10, QAPLET_MEMORY_LIMIT = 11,
  QAPLET_INVALID_UTF8 = 12, QAPLET_VALUE_TOO_LARGE = 13,
  QAPLET_MODULE_RESOLVE = 14, QAPLET_MODULE_LOAD = 15, QAPLET_MODULE_LIMIT = 16
} qaplet_code;
typedef enum {
  QAPLET_OPEN, QAPLET_STOP_REQUESTED, QAPLET_STOPPED, QAPLET_FAILED, QAPLET_CLOSED
} qaplet_lifecycle;
typedef enum {
  QAPLET_PHASE_IDLE, QAPLET_PHASE_RUNNING, QAPLET_PHASE_PENDING, QAPLET_PHASE_DONE
} qaplet_phase;
typedef enum {
  QAPLET_VALUE_UNDEFINED, QAPLET_VALUE_NULL, QAPLET_VALUE_BOOL, QAPLET_VALUE_DOUBLE,
  QAPLET_VALUE_INT64, QAPLET_VALUE_UINT64, QAPLET_VALUE_STRING, QAPLET_VALUE_BYTES
} qaplet_value_type;
enum {
  QAPLET_CAP_LIFECYCLE = 1u << 0, QAPLET_CAP_SYNC_SCRIPT = 1u << 1,
  QAPLET_CAP_SCALAR_RESULT = 1u << 2, QAPLET_CAP_OWNER_THREAD = 1u << 3,
  QAPLET_CAP_POD_SNAPSHOT = 1u << 4, QAPLET_CAP_RESOURCE_BASE = 1u << 5,
  QAPLET_CAP_COPIED_VALUES = 1u << 6,
  QAPLET_CAP_PROMISE_JOBS = 1u << 7,
  QAPLET_CAP_FILE_MODULES = 1u << 8,
  QAPLET_DIAGNOSTIC_TRUNCATED = 1u << 0
};

/* Every structure begins with size and ABI. Initialize with its INIT macro.
 * Compatible larger structures must have zero-filled unknown tail bytes.
 * Unknown flags/reserved values are rejected. Zero limits select finite defaults.
 * No private engine header, Context, or JS value crosses this interface. */
typedef struct {
  uint32_t struct_size, abi_version;
  uint64_t engine_heap_bytes, engine_stack_bytes, native_storage_bytes, source_bytes;
  uint64_t module_count, source_cache_bytes;
} qaplet_limits;
typedef struct {
  uint32_t struct_size, abi_version;
  uint64_t flags;
  qaplet_limits limits;
  uint64_t reserved[4];
} qaplet_config;
typedef struct {
  uint32_t struct_size, abi_version;
  qaplet_code code;
  uint32_t flags;
  char *message;
  size_t capacity, length;
} qaplet_diagnostic;
typedef struct {
  uint32_t struct_size, abi_version;
  qaplet_phase phase;
  uint32_t progressed;
  qaplet_code code;
} qaplet_progress;
typedef struct {
  uint32_t struct_size, abi_version;
  qaplet_value_type type;
  qaplet_code code;
  double number;
  uint32_t boolean;
  int64_t signed_integer;
  uint64_t unsigned_integer;
  void *buffer;
  size_t capacity, length;
} qaplet_result;
typedef struct {
  uint32_t struct_size, abi_version;
  qaplet_lifecycle lifecycle;
  qaplet_phase phase;
  qaplet_code termination_reason;
  uint32_t reserved;
  qaplet_limits limits;
  uint64_t engine_current_bytes, engine_peak_bytes;
  uint64_t native_current_bytes, native_peak_bytes;
  uint64_t module_count, source_cache_bytes;
} qaplet_snapshot;
typedef struct {
  uint32_t struct_size, abi_version;
  uint64_t capabilities;
} qaplet_abi;
#define QAPLET_LIMITS_INIT {sizeof(qaplet_limits), QAPLET_ABI_VERSION, 0, 0, 0, 0, 0, 0}
#define QAPLET_CONFIG_INIT {sizeof(qaplet_config), QAPLET_ABI_VERSION, 0, QAPLET_LIMITS_INIT, {0}}
#define QAPLET_DIAGNOSTIC_INIT {sizeof(qaplet_diagnostic), QAPLET_ABI_VERSION, 0, 0, NULL, 0, 0}
#define QAPLET_PROGRESS_INIT {sizeof(qaplet_progress), QAPLET_ABI_VERSION, 0, 0, 0}
#define QAPLET_RESULT_INIT {sizeof(qaplet_result), QAPLET_ABI_VERSION, 0, 0, 0, 0, 0, 0, NULL, 0, 0}
#define QAPLET_SNAPSHOT_INIT {sizeof(qaplet_snapshot), QAPLET_ABI_VERSION, 0, 0, 0, 0, QAPLET_LIMITS_INIT, 0, 0, 0, 0, 0, 0}
#define QAPLET_ABI_INIT {sizeof(qaplet_abi), QAPLET_ABI_VERSION, 0}

/* Global query and POD snapshots are thread-safe. All remaining operations
 * belong to the thread that successfully created the Runtime. Output pointers
 * and diagnostics are borrowed only for the duration of a call. */
qaplet_code qaplet_query_abi(qaplet_abi *output);
qaplet_code qaplet_create(const qaplet_config *, qaplet_runtime **output, qaplet_diagnostic *);
/* Script semantics; retains the instance's global state. source is a borrowed
 * pointer with explicit byte length, strictly validated as UTF-8 before acceptance.
 * Supports undefined/null/bool/Number, BigInt in [-2^63,2^64-1], strict UTF-8
 * strings and safely copied ordinary-buffer Uint8Array views. BigInt uses INT64
 * through INT64_MAX, then UINT64; Number remains DOUBLE (including NaN/-0).
 * Objects/functions are never implicitly converted. Strings may contain NUL;
 * lone surrogates are INVALID_UTF8. A string/byte result is limited to
 * QAPLET_VALUE_BYTES_MAX encoded bytes. Shared/detached/out-of-bounds views and
 * Proxy wrappers are unsupported. Only genuine Promises are waited on; arbitrary
 * thenables are not inspected. Runnable jobs or an unsettled entry yield PENDING.
 * While PENDING, another eval is BUSY and get_result is BAD_STATE. */
qaplet_code qaplet_eval(qaplet_runtime *, const char *source, size_t length, qaplet_progress *, qaplet_diagnostic *);
/* Run source ESM using the cwd captured at creation. Relative imports use the
 * importing file. Canonical identities/source bytes remain until close and a
 * module body is evaluated once. import.meta.url/main are initialized once.
 * Completed runs have undefined results without converting the namespace. */
qaplet_code qaplet_run_entry(qaplet_runtime *, const char *path, size_t length, qaplet_progress *, qaplet_diagnostic *);
/* Script evaluation with an explicit absolute file URL filename for relative
 * dynamic import. The base need not exist. Ordinary eval has no relative base. */
qaplet_code qaplet_eval_with_base(qaplet_runtime *, const char *source, size_t length,
                                 const char *base_url, size_t base_length,
                                 qaplet_progress *, qaplet_diagnostic *);
/* Runs at most 256 jobs, without sleeping. Empty jobs with an unsettled entry
 * remain PENDING with progressed=0. Rejections are judged only after a complete
 * jobs checkpoint. Ordinary errors settle remaining jobs before DONE, preserving
 * state and allowing a new execution. An inactive pump reports no progress and
 * OK; get_result retains the completed outcome until the next accepted eval.
 * Outputs and diagnostic buffers are borrowed only for this call. */
qaplet_code qaplet_pump(qaplet_runtime *, qaplet_progress *, qaplet_diagnostic *);
/* Set output.buffer/capacity for STRING/BYTES. length is the payload byte count;
 * string payloads have no extra terminator. BUFFER_TOO_SMALL reports length,
 * copies no payload, and preserves the result for retry without re-executing JS.
 * The caller's buffer/capacity are preserved. Other fields are copied outputs.
 * A completed result/error lasts until the next accepted eval or close. Rejected
 * calls leave it intact; no JS or native storage pointer is exposed to callers.
 * Diagnostics belong to each caller, retain code even at capacity zero, and
 * NUL-terminate when capacity is positive. length excludes that terminator and
 * describes safely harvested text bounded to 1023 bytes; TRUNCATED also signals
 * source-text omission. NUL in exception text is escaped as two bytes "\\0".
 * Only primitive thrown strings and own string data messages on intrinsic Error
 * objects are inspected. Getters, proxies and conversion hooks are never run. */
qaplet_code qaplet_get_result(qaplet_runtime *, qaplet_result *, qaplet_diagnostic *);
qaplet_code qaplet_get_snapshot(qaplet_runtime *, qaplet_snapshot *, qaplet_diagnostic *);
/* close releases execution resources, keeps a queryable CLOSED handle, and is
 * idempotent. destroy requires CLOSED and is called exactly once by the owner.
 * Before destroy the host must withdraw producers and join all in-flight calls.
 * After successful destroy every alias is invalid. */
qaplet_code qaplet_close(qaplet_runtime *, qaplet_diagnostic *);
qaplet_code qaplet_destroy(qaplet_runtime *, qaplet_diagnostic *);
#ifdef __cplusplus
}
#endif
#endif
