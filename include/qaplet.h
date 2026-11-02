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
#define QAPLET_HOST_ARGUMENTS_MAX 32u
typedef struct qaplet_runtime qaplet_runtime;
typedef enum {
  QAPLET_OK = 0, QAPLET_INVALID_ARGUMENT = 1, QAPLET_ABI_MISMATCH = 2,
  QAPLET_WRONG_THREAD = 3, QAPLET_BUSY = 4, QAPLET_BAD_STATE = 5,
  QAPLET_UNSUPPORTED = 6, QAPLET_JS_EXCEPTION = 7,
  QAPLET_VALUE_TYPE_UNSUPPORTED = 8, QAPLET_BUFFER_TOO_SMALL = 9,
  QAPLET_SOURCE_TOO_LARGE = 10, QAPLET_MEMORY_LIMIT = 11,
  QAPLET_INVALID_UTF8 = 12, QAPLET_VALUE_TOO_LARGE = 13,
  QAPLET_MODULE_RESOLVE = 14, QAPLET_MODULE_LOAD = 15, QAPLET_MODULE_LIMIT = 16,
  QAPLET_DEADLINE_EXCEEDED = 17, QAPLET_DISPATCH_LIMIT = 18,
  QAPLET_HOST_ERROR = 19, QAPLET_NO_HANDLER = 20, QAPLET_QUEUE_FULL = 21,
  QAPLET_PAYLOAD_TOO_LARGE = 22
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
  QAPLET_CAP_EXECUTION_BUDGETS = 1u << 9,
  QAPLET_CAP_MODULE_POLICY = 1u << 10,
  QAPLET_CAP_HOST_MODULES = 1u << 11,
  QAPLET_CAP_POD_EVENTS = 1u << 12,
  QAPLET_CAP_RUNTIME_SNAPSHOT = 1u << 16,
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
  uint64_t execution_timeout_ms, execution_dispatches;
  uint64_t event_queue_slots, event_payload_bytes;
} qaplet_limits;
typedef struct { const char *data; size_t length; } qaplet_string;

typedef struct {
  uint32_t struct_size, abi_version;
  uint64_t flags;
  qaplet_limits limits;
  uint64_t reserved[4];
  const qaplet_string *argv;
  size_t argc;
  const char *cwd;
  size_t cwd_length;
} qaplet_config;
/* Zero fields inherit the finite instance limits. Nonzero values may only
 * tighten them. flags and reserved must be zero. Never retained after a call. */
typedef struct {
  uint32_t struct_size, abi_version;
  uint64_t flags, timeout_ms, max_dispatches;
  uint64_t reserved[4];
} qaplet_execution_options;
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
  uint64_t execution_dispatches;
  uint64_t queue_depth, queue_peak;
  uint64_t events_accepted, events_rejected, events_consumed, events_dropped;
} qaplet_snapshot;
typedef struct {
  uint32_t struct_size, abi_version;
  uint64_t capabilities;
} qaplet_abi;
/* STRING/BYTES data has explicit length; embedded NUL is allowed in strings.
 * Integers become BigInt, DOUBLE becomes Number. Other fields are ignored. */
typedef struct {
  uint32_t struct_size, abi_version;
  qaplet_value_type type;
  uint32_t boolean;
  double number;
  int64_t signed_integer;
  uint64_t unsigned_integer;
  const void *data;
  size_t length;
} qaplet_value;
/* Arguments/data are borrowed until return. Copy anything retained by the Host.
 * A nonzero return throws an intrinsic Error with a numeric own `code` property.
 * The runtime/call_id pair is valid for setters only during this callback. */
typedef qaplet_code (*qaplet_host_function)(qaplet_runtime *, uint64_t call_id,
    size_t argc, const qaplet_value *argv, void *userdata);
typedef enum { QAPLET_HOST_CONSTANT, QAPLET_HOST_FUNCTION } qaplet_host_export_kind;
typedef struct {
  uint32_t struct_size, abi_version;
  const char *name;
  size_t name_length;
  qaplet_host_export_kind kind;
  uint32_t reserved;
  qaplet_value value;
  qaplet_host_function function;
  void *userdata;
} qaplet_host_export;
typedef struct {
  uint32_t struct_size, abi_version;
  const char *name;
  size_t name_length;
  const qaplet_host_export *exports;
  size_t export_count;
  uint64_t flags, reserved[2];
} qaplet_host_module;
#define QAPLET_LIMITS_INIT {sizeof(qaplet_limits), QAPLET_ABI_VERSION, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}
#define QAPLET_CONFIG_INIT {sizeof(qaplet_config), QAPLET_ABI_VERSION, 0, QAPLET_LIMITS_INIT, {0}, NULL, 0, NULL, 0}
#define QAPLET_EXECUTION_OPTIONS_INIT {sizeof(qaplet_execution_options), QAPLET_ABI_VERSION, 0, 0, 0, {0}}
#define QAPLET_DIAGNOSTIC_INIT {sizeof(qaplet_diagnostic), QAPLET_ABI_VERSION, 0, 0, NULL, 0, 0}
#define QAPLET_PROGRESS_INIT {sizeof(qaplet_progress), QAPLET_ABI_VERSION, 0, 0, 0}
#define QAPLET_RESULT_INIT {sizeof(qaplet_result), QAPLET_ABI_VERSION, 0, 0, 0, 0, 0, 0, NULL, 0, 0}
#define QAPLET_SNAPSHOT_INIT {sizeof(qaplet_snapshot), QAPLET_ABI_VERSION, 0, 0, 0, 0, QAPLET_LIMITS_INIT, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}
#define QAPLET_ABI_INIT {sizeof(qaplet_abi), QAPLET_ABI_VERSION, 0}
#define QAPLET_VALUE_INIT {sizeof(qaplet_value), QAPLET_ABI_VERSION, 0, 0, 0, 0, 0, NULL, 0}
#define QAPLET_HOST_EXPORT_INIT {sizeof(qaplet_host_export), QAPLET_ABI_VERSION, NULL, 0, 0, 0, QAPLET_VALUE_INIT, NULL, NULL}
#define QAPLET_HOST_MODULE_INIT {sizeof(qaplet_host_module), QAPLET_ABI_VERSION, NULL, 0, NULL, 0, 0, {0}}

/* Global query, POD snapshots and post_event are thread-safe. Other operations
 * belong to the thread that successfully created the Runtime. Output pointers
 * and diagnostics are borrowed only for the duration of a call. */
qaplet_code qaplet_query_abi(qaplet_abi *output);
/* argv is copied at creation as strict UTF-8 strings with explicit lengths
 * (including NUL). NULL argv requires argc=0. cwd is an optional absolute native
 * directory path; NULL/0 captures the OS cwd. Paths reject NUL/invalid UTF-8.
 * qaplet:runtime exports a frozen argv array and primitive cwd snapshot. Later
 * Host buffer changes/chdir do not change them or the module resolution base.
 * Both qaplet builtins reserve module identities from creation. */
qaplet_code qaplet_create(const qaplet_config *, qaplet_runtime **output, qaplet_diagnostic *);
/* Registration copies names, declarations and constants atomically. Names use
 * a nonempty host: name. Module/export names are strict UTF-8 without NUL.
 * No replacement/unregister.
 * First accepted eval/run_entry or valid pump locks registration permanently.
 * Host function code and userdata must remain valid through close returning. */
qaplet_code qaplet_register_host_module(qaplet_runtime *, const qaplet_host_module *, qaplet_diagnostic *);
/* Owner callback only. Validates and synchronously copies the value. No setter
 * means undefined; a second successful setter is BAD_STATE, preserving the first.
 * Outside a callback or with an expired call_id returns BAD_STATE. Query,
 * snapshots, post_event and this setter may reenter a Host callback. */
qaplet_code qaplet_host_set_result(qaplet_runtime *, uint64_t call_id, const qaplet_value *, qaplet_diagnostic *);
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
 * Canonical load/link/evaluation failures are frozen too; repair requires a new
 * Runtime. Unresolvable requests consume no module identity. Symlinks use OS
 * realpath, while hard links retain distinct URLs. Only ordinary .js/.mjs or
 * extensionless source files are supported. URL query/fragment/authority,
 * encoded separators/NUL, UNC/device paths and other formats are rejected.
 * Completed runs have undefined results without converting the namespace. */
qaplet_code qaplet_run_entry(qaplet_runtime *, const char *path, size_t length, qaplet_progress *, qaplet_diagnostic *);
/* Script evaluation with an explicit absolute file URL filename for relative
 * dynamic import. The base need not exist. Ordinary eval has no relative base. */
qaplet_code qaplet_eval_with_base(qaplet_runtime *, const char *source, size_t length,
                                 const char *base_url, size_t base_length,
                                 qaplet_progress *, qaplet_diagnostic *);
/* qaplet_eval is the default-options convenience call. An accepted execution
 * has one absolute CLOCK_MONOTONIC deadline (default 30000 ms), including host
 * waits, and one cumulative dispatch limit (default 100000 jobs/events).
 * Exhaustion permanently FAILS the Runtime even if JS catches an exception.
 * Checks are cooperative; arbitrary native calls/GC are not hard-preempted.
 * NULL options inherit instance limits. Options cannot increase those limits. */
qaplet_code qaplet_eval_with_options(qaplet_runtime *, const char *source, size_t length,
                                    const qaplet_execution_options *, qaplet_progress *, qaplet_diagnostic *);
/* Runs at most 256 jobs and 32 events, without sleeping. Jobs settle before the
 * next event is claimed. Existing executions share their ledger with events;
 * idle events and their runnable jobs own independent cross-pump ledgers.
 * Empty jobs with an unsettled entry
 * remain PENDING with progressed=0. Rejections are judged only after a complete
 * jobs checkpoint. Ordinary errors settle remaining jobs before DONE, preserving
 * state and allowing a new execution. An inactive pump reports no progress and
 * OK; get_result retains the completed outcome until the next accepted execution.
 * Outputs and diagnostic buffers are borrowed only for this call. */
qaplet_code qaplet_pump(qaplet_runtime *, qaplet_progress *, qaplet_diagnostic *);
/* Any thread, including a Host callback, may submit copied POD while the handle
 * remains alive. A successful call has copied all bytes into a fixed queue slot;
 * it does not acknowledge delivery. NULL data is valid only at length zero.
 * Zero limits select 256 slots and 1024 payload bytes. Invalid input/oversize is
 * rejected before lifecycle, then NO_HANDLER/QUEUE_FULL; refusal never enqueues.
 * JS imports qaplet:events and calls onEvent(fn) exactly once. Owner pump invokes
 * fn({type: Number, token: BigInt, payload: independent Uint8Array}) in FIFO
 * acceptance order. uint64 token zero is valid. The return value is ignored,
 * including Promises; runnable jobs and unhandled rejections still checkpoint. */
qaplet_code qaplet_post_event(qaplet_runtime *, uint32_t type, uint64_t token,
                             const void *data, size_t length, qaplet_diagnostic *);
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
