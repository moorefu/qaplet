/* SPDX-License-Identifier: MIT */
#ifndef QAPLET_H
#define QAPLET_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

#define QAPLET_ABI_VERSION 1u
/* Engineering default maxima. The copied instance limits are authoritative. */
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
  QAPLET_PAYLOAD_TOO_LARGE = 22, QAPLET_EXECUTION_STOPPED = 23,
  QAPLET_RESOURCE_LIMIT = 24, QAPLET_INVALID_HANDLE = 25,
  QAPLET_CONTROL_FAILURE = 26, QAPLET_PROCESS_ERROR = 27
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
  /* Explicit M2 factory and separate native cleanup state. */
  QAPLET_CAP_M2_RUNTIME = 1u << 17,
  QAPLET_CAP_M2_WAIT = 1u << 18,
  QAPLET_CAP_M2_WORK = 1u << 21,
  QAPLET_CAP_M2_TIMERS = 1u << 19,
  QAPLET_CAP_HOST_NOTIFY = 1u << 20,
  QAPLET_CAP_PROCESS = 1u << 22,
  QAPLET_CAP_RESOURCE_LIMITS = 1u << 13,
  QAPLET_CAP_COOPERATIVE_STOP = 1u << 14,
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
  /* Per-value encoded bytes, synchronous callback argc, and work per pump. */
  uint64_t bridge_value_bytes, host_arguments, pump_jobs, pump_events;
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
  /* Monotonic event counters saturate at UINT64_MAX. */
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
#define QAPLET_LIMITS_INIT {sizeof(qaplet_limits), QAPLET_ABI_VERSION, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}
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

/* M2 structures are independent: no M1 layout or reserved field changes.
 * Capacities describe the finite configuration, not available resource APIs.
 * M2 waiting owns a private connected-socket waker, with no JS/helper thread.
 * Timer, work, process and standard-stream APIs are separate capabilities. */
typedef struct {
  uint32_t struct_size, abi_version;
  uint64_t worker_threads, helper_threads, worker_stack_bytes, helper_stack_bytes;
  uint64_t work_handlers, handler_concurrency, timers, reactor_registrations;
  uint64_t native_handles, processes, keepalive_tokens, operations;
  uint64_t operation_data_bytes, work_input_bytes, work_output_bytes, io_block_bytes;
  uint64_t process_arguments, process_data_bytes, pump_io_bytes;
} qaplet_m2_limits;
typedef struct {
  uint32_t struct_size, abi_version;
  uint64_t flags;
  qaplet_m2_limits limits;
  uint64_t reserved[4];
} qaplet_m2_config;
typedef enum {
  QAPLET_NATIVE_RUNNING, QAPLET_NATIVE_CLEANING, QAPLET_NATIVE_QUIESCENT
} qaplet_native_state;

typedef enum {
  QAPLET_ERROR_DOMAIN_NONE, QAPLET_ERROR_DOMAIN_ERRNO, QAPLET_ERROR_DOMAIN_WIN32
} qaplet_error_domain;
typedef enum {
  QAPLET_ROLLBACK_NONE, QAPLET_ROLLBACK_COMPLETE, QAPLET_ROLLBACK_INCOMPLETE
} qaplet_rollback_state;
/* Independent, caller-owned M2 error record; no pointers or OS handle values.
 * Domain ERRNO includes POSIX returned errors and Cosmopolitan's mapped socket
 * errno values. Zero means
 * no associated native error. Primary and cleanup errors never replace each
 * other. A later control fault can be reported alongside a prior stop cause. */
typedef struct {
  uint32_t struct_size, abi_version;
  qaplet_code primary_reason;
  uint32_t primary_error_domain;
  int32_t primary_error;
  uint32_t control_error_domain;
  int32_t control_error;
  qaplet_code cleanup_reason;
  uint32_t cleanup_error_domain;
  int32_t cleanup_error;
  qaplet_native_state native_state;
  qaplet_rollback_state creation_rollback;
  uint64_t native_uncertain_handles;
} qaplet_m2_diagnostic;
#define QAPLET_M2_DIAGNOSTIC_INIT {sizeof(qaplet_m2_diagnostic), QAPLET_ABI_VERSION, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}
typedef enum {
  QAPLET_DRIVE_IDLE, QAPLET_DRIVE_PROGRESS, QAPLET_DRIVE_WAIT_TIMEOUT,
  QAPLET_DRIVE_UNSETTLED_ENTRY, QAPLET_DRIVE_ERROR, QAPLET_DRIVE_STOPPED,
  QAPLET_DRIVE_QUIESCENT
} qaplet_drive_reason;
typedef struct {
  uint32_t struct_size, abi_version;
  qaplet_phase phase;
  uint32_t progressed;
  qaplet_code code;
  qaplet_drive_reason reason;
} qaplet_drive_result;
typedef struct {
  uint32_t struct_size, abi_version;
  uint64_t capabilities;
  qaplet_lifecycle lifecycle;
  qaplet_phase phase;
  qaplet_code termination_reason, cleanup_reason;
  qaplet_native_state native_state;
  uint32_t driving;
  qaplet_limits limits;
  qaplet_m2_limits m2_limits;
  uint64_t engine_current_bytes, engine_peak_bytes;
  uint64_t native_current_bytes, native_peak_bytes;
  uint64_t native_handles, native_handles_peak;
  uint64_t reactor_registrations, reactor_registrations_peak;
  uint64_t keepalive_tokens, keepalive_tokens_peak;
  uint64_t wake_requests, wake_signals, wait_calls;
  /* Core control error, separate from the first termination cause. POSIX errno
   * is domain 1 (including Cosmopolitan's socket errno mapping); 0 means none. */
  uint32_t control_error_domain;
  int32_t control_error;
  /* close returned an error and ownership cannot be proved. Such resources
   * stay charged separately, prevent quiescence/destroy, and are never retried
   * by numeric fd (which may already name another thread's resource). */
  uint64_t native_uncertain_handles;
} qaplet_m2_snapshot;
/* Opaque value identity: never modify its fields. Copies denote the same token.
 * Runtime identity and generation reject cross-instance and stale releases.
 * These values cannot validate a Runtime pointer after destroy. */
typedef struct { uint64_t runtime_id, slot, generation; } qaplet_keepalive;
typedef struct { uint64_t runtime_id, slot, generation; } qaplet_timer;
typedef void (*qaplet_timer_callback)(qaplet_runtime *, const qaplet_timer *, void *userdata);
#define QAPLET_TIMER_INIT {0, 0, 0}
/* One-shot, default ref, owner-only M2 resource. delay_ms is a nonnegative
 * safe integer (<= 2^53-1) and its absolute nanosecond deadline must fit.
 * The callback is never synchronous with create. Code/userdata stay borrowed
 * through callback return, even after self-cancellation. For an unclaimed
 * timer the borrow ends at cancellation or owner terminal cleanup. The callback
 * borrows its token pointer only during the call; a value copy may be retained.
 * Timer operations may reenter owner JS/Host/timer callbacks; eval, nested
 * drives, close and destroy remain forbidden there. Output changes on success
 * only. Completion/cancel/stop/close invalidate the identity. A callback's own
 * record stays charged until it returns, including after self-cancellation. */
qaplet_code qaplet_timer_create(qaplet_runtime *, uint64_t delay_ms,
    qaplet_timer_callback, void *userdata, qaplet_timer *output, qaplet_diagnostic *);
/* Repeats from its original monotonic schedule, skipping missed periods.
 * period_ms must be at least 1. Self-cancel prevents rearming. */
qaplet_code qaplet_interval_create(qaplet_runtime *, uint64_t period_ms,
    qaplet_timer_callback, void *userdata, qaplet_timer *output, qaplet_diagnostic *);
/* FIFO deferred callback. Registrations made during a driver slice become
 * eligible in the next slice. Shares the fixed timer count/storage budget. */
qaplet_code qaplet_immediate_create(qaplet_runtime *, qaplet_timer_callback,
    void *userdata, qaplet_timer *output, qaplet_diagnostic *);
qaplet_code qaplet_timer_cancel(qaplet_runtime *, const qaplet_timer *, qaplet_diagnostic *);
qaplet_code qaplet_timer_ref(qaplet_runtime *, const qaplet_timer *, int referenced, qaplet_diagnostic *);
typedef struct {
  uint32_t struct_size, abi_version;
  uint64_t current, peak, referenced, ready, reserved_bytes, next_timeout_ms;
} qaplet_timer_snapshot;
#define QAPLET_TIMER_SNAPSHOT_INIT {sizeof(qaplet_timer_snapshot), QAPLET_ABI_VERSION, 0, 0, 0, 0, 0, UINT64_MAX}
/* Any thread, including CLOSED. Full size required. current includes records
 * held by an executing callback; reserved_bytes is the fixed SDK slot table.
 * next_timeout_ms is the rounded-up monotonic delay to any timer, including
 * unref; UINT64_MAX means no scheduled timer. It is a snapshot, not a wake
 * registration or a keepalive promise. ready timers have delay 0. The Host
 * must separately arrange notifications for cross-thread work. */
qaplet_code qaplet_get_timer_snapshot(qaplet_runtime *, qaplet_timer_snapshot *, qaplet_diagnostic *);
#define QAPLET_KEEPALIVE_INIT {0, 0, 0}
#define QAPLET_M2_LIMITS_INIT {sizeof(qaplet_m2_limits), QAPLET_ABI_VERSION, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}
#define QAPLET_M2_CONFIG_INIT {sizeof(qaplet_m2_config), QAPLET_ABI_VERSION, 0, QAPLET_M2_LIMITS_INIT, {0}}
#define QAPLET_DRIVE_RESULT_INIT {sizeof(qaplet_drive_result), QAPLET_ABI_VERSION, 0, 0, 0, 0}
#define QAPLET_M2_SNAPSHOT_INIT {sizeof(qaplet_m2_snapshot), QAPLET_ABI_VERSION, 0, 0, 0, 0, 0, 0, 0, QAPLET_LIMITS_INIT, QAPLET_M2_LIMITS_INIT, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}

typedef void (*qaplet_notify_function)(void *userdata);
typedef struct {
  uint32_t struct_size, abi_version;
  qaplet_notify_function function;
  void *userdata;
  uint64_t flags, reserved[2];
} qaplet_host_notify;
#define QAPLET_HOST_NOTIFY_INIT {sizeof(qaplet_host_notify), QAPLET_ABI_VERSION, NULL, NULL, 0, {0}}
/* Owner, OPEN M2, once before first execution/valid pump. Copies the declaration;
 * a non-NULL function is required. No replacement or unregister. The native
 * function runs synchronously on any publishing thread, outside SDK locks.
 * It must return promptly, never block, execute JS or call any SDK operation
 * (SDK reentry returns BUSY). Use a nonblocking Host-owned notification facility.
 * Hints may merge or be redundant; inspect authoritative state on the owner.
 * The internal waker remains independent. No SDK wait fd/HANDLE is exported.
 * Event acceptance, stop, token release, explicit wake and native cleanup can
 * notify; a hint does not assert business completion or keep the loop alive.
 * Code/userdata remain Host-owned: retain until native QUIESCENT AND all external
 * producers/calls have been withdrawn/joined. close never waits for a hook.
 * In-flight hooks keep cleanup pending and destroy BUSY; quiesce may be retried.
 * Fixed control storage is charged from creation; dispatch allocates nothing. */
qaplet_code qaplet_set_host_notify(qaplet_runtime *, const qaplet_host_notify *, qaplet_diagnostic *);
typedef struct {
  uint32_t struct_size, abi_version;
  uint32_t installed, reserved;
  uint64_t inflight, inflight_peak;
} qaplet_notify_snapshot;
#define QAPLET_NOTIFY_SNAPSHOT_INIT {sizeof(qaplet_notify_snapshot), QAPLET_ABI_VERSION, 0, 0, 0, 0}
/* Any thread, including CLOSED. Native leases, never business event counts.
 * Zero inflight does not prove absence of future external calls. Full output
 * size required; M1 handles return UNSUPPORTED. No callback pointer is exposed. */
qaplet_code qaplet_get_notify_snapshot(qaplet_runtime *, qaplet_notify_snapshot *, qaplet_diagnostic *);

/* Explicit opt-in; base retains exactly the M1 configuration contract.
 * Failure stores NULL in output and attempts all feasible unpublished cleanup.
 * Use the extended factory to distinguish complete rollback from an OS close
 * error whose resource identity cannot safely be retried. */
qaplet_code qaplet_create_m2(const qaplet_config *base, const qaplet_m2_config *,
                            qaplet_runtime **output, qaplet_diagnostic *);
/* Same factory semantics, with a required full-size independent error record.
 * Success has ROLLBACK_NONE. Failure has COMPLETE or INCOMPLETE. INCOMPLETE
 * records uncertain kernel resources: it is not a successful rollback, a live
 * Runtime, a transferable OS handle or permission to retry a numeric fd.
 * No internal executor or Host callback exists before publication; storage
 * with no future user is released even if kernel resource cleanup is unknown.
 * No SDK continuation can prove those retired resource identities closed. */
qaplet_code qaplet_create_m2_with_diagnostic(const qaplet_config *, const qaplet_m2_config *,
                                          qaplet_runtime **, qaplet_m2_diagnostic *, qaplet_diagnostic *);
/* Any thread; synchronized POD copy without JS, including CLOSED. M1 handles
 * return UNSUPPORTED. primary_reason is the retained execution first cause,
 * primary_error belongs only to that cause, control_error also reports a later
 * control fault, and cleanup fields retain the first cleanup failure.
 * creation_rollback is NONE for a published Runtime. Full output size required. */
qaplet_code qaplet_get_m2_diagnostic(qaplet_runtime *, qaplet_m2_diagnostic *, qaplet_diagnostic *);
/* Any thread, including CLOSED. M1 handles report their M1 capability set and
 * zero M2 limits; querying does not enable M2. Output must have the full size. */
qaplet_code qaplet_get_m2_snapshot(qaplet_runtime *, qaplet_m2_snapshot *, qaplet_diagnostic *);
/* M2 owner-only drives. max_wait_ms is finite (0 never waits). An empty
 * instance returns IDLE immediately, even for a positive wait budget. These
 * calls wait only while a ref source (currently a Host keepalive) exists.
 * Ready work runs before any wait. max_wait is cumulative active waiting,
 * never reset by redundant wakes; it does not bound JS/native call duration.
 * An unresolvable entry Promise
 * returns reason UNSETTLED_ENTRY, code OK and phase PENDING; pump keeps its M1
 * no-progress behavior. run continues until natural idle/error/stop.
 * An idle call preserves the last completed result. No call closes the Runtime. */
qaplet_code qaplet_run_once(qaplet_runtime *, uint64_t max_wait_ms,
                           qaplet_drive_result *, qaplet_diagnostic *);
qaplet_code qaplet_run(qaplet_runtime *, qaplet_drive_result *, qaplet_diagnostic *);
/* Create: owner, outside callbacks/drives, OPEN M2 only. Output is written only
 * on success. Count/storage are bounded from creation. Release: any thread,
 * exactly once; old/cross-instance tokens return INVALID_HANDLE. Release wakes
 * the owner; stop/close invalidate outstanding tokens without waiting for the
 * external producer. Withdraw/join all producers before destroy. */
qaplet_code qaplet_keepalive_create(qaplet_runtime *, qaplet_keepalive *, qaplet_diagnostic *);
qaplet_code qaplet_keepalive_release(qaplet_runtime *, const qaplet_keepalive *, qaplet_diagnostic *);
/* Any thread, M2 only. A coalescible hint, not business work or loop keepalive.
 * Terminal/CLOSED hints are harmless. Accepted state changes (event, release,
 * stop) still return OK if the later wake syscall fails. The core fault is
 * independently latched and reported by snapshot/owner drive. Not signal-safe. */
qaplet_code qaplet_wake(qaplet_runtime *, qaplet_diagnostic *);
/* M2 owner, CLOSED only, no JS. Zero only checks. Native cleanup is separate
 * from lifecycle; an empty instance is immediately QUIESCENT after close.
 * Future resource capabilities must finish physical cleanup before quiescence.
 * destroy returns BUSY while managed native cleanup is incomplete. External
 * producers must still be withdrawn and joined before final destroy. */
qaplet_code qaplet_quiesce(qaplet_runtime *, uint64_t max_wait_ms,
                          qaplet_drive_result *, qaplet_diagnostic *);

/* Direct-child process subset. Mutations and get_status belong to the owner;
 * qaplet_get_process_status and qaplet_get_process_snapshot are POD queries.
 * argv includes argv[0]; path never searches PATH/PATHEXT or invokes a shell.
 * REPLACE_ENV makes env a complete KEY=VALUE mapping, including an empty one;
 * otherwise the M2 factory's environment snapshot is used. Strings reject NUL.
 * Relative path/cwd resolve against the immutable Runtime cwd.
 * PIPE (the default) is explicitly UNSUPPORTED until the pipe capability exists.
 * INHERIT borrows Host C descriptors 0/1/2; IGNORE uses a null endpoint.
 * Windows duplicates those descriptor handles independently of GetStdHandle.
 * Spawn copies input before acceptance. Ordinary OS errors preserve the entry
 * result and leave the Runtime OPEN. Native errors are returned in status.
 * Exit/reap, owner delivery and physical helper-thread exit are separate facts.
 * get_status observes native facts and performs nonblocking native retirement;
 * only owner drive hands completion over and disarms its default ref. Pure C
 * handoff creates no JS execution; an active entry keeps its cumulative ledger.
 * release is BUSY until reaped; completed records retain process capacity until
 * release or close, and physical cleanup retains capacity until helper join.
 * Helpers share the finite instance helper/stack/handle/registration budgets.
 * Close initiates force termination/reap of direct children. Host must not reap
 * SDK children or enable automatic child reaping. If native identity is lost,
 * SDK never signals that retired PID and retains cleanup error/BUSY ownership.
 * No process-tree, graceful-signal or pipe/EOF contract is implied. */
typedef enum { QAPLET_PROCESS_PIPE, QAPLET_PROCESS_INHERIT, QAPLET_PROCESS_IGNORE } qaplet_process_stdio;
enum { QAPLET_PROCESS_REPLACE_ENV = 1u << 0 };
typedef struct {
  uint32_t struct_size, abi_version;
  uint64_t runtime_id, slot, generation;
} qaplet_process;
typedef struct {
  uint32_t struct_size, abi_version;
  uint64_t flags;
  qaplet_string path, cwd;
  const qaplet_string *argv, *env;
  size_t argc, env_count;
  uint32_t stdio[3], reserved;
} qaplet_process_options;
typedef struct {
  uint32_t struct_size, abi_version;
  qaplet_code code;
  uint32_t error_domain;
  int32_t error;
  uint32_t exited, reaped, delivered, referenced, helper_active;
  uint32_t native_exit_code, native_signal;
  int32_t raw_wait_status;
} qaplet_process_status;
typedef struct {
  uint32_t struct_size, abi_version;
  uint64_t processes, processes_peak, referenced, retained_results;
  uint64_t helper_threads, helper_threads_peak, helper_stack_bytes, helper_stack_bytes_peak;
  uint64_t environment_bytes, accepted, completed;
} qaplet_process_snapshot;
#define QAPLET_PROCESS_INIT {sizeof(qaplet_process), QAPLET_ABI_VERSION, 0, 0, 0}
#define QAPLET_PROCESS_OPTIONS_INIT {sizeof(qaplet_process_options), QAPLET_ABI_VERSION, 0, {NULL,0}, {NULL,0}, NULL, NULL, 0, 0, {0}, 0}
#define QAPLET_PROCESS_STATUS_INIT {sizeof(qaplet_process_status), QAPLET_ABI_VERSION, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}
#define QAPLET_PROCESS_SNAPSHOT_INIT {sizeof(qaplet_process_snapshot), QAPLET_ABI_VERSION, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}
qaplet_code qaplet_process_spawn(qaplet_runtime *, const qaplet_process_options *,
    qaplet_process *, qaplet_process_status *, qaplet_diagnostic *);
qaplet_code qaplet_process_get_status(qaplet_runtime *, const qaplet_process *, qaplet_process_status *, qaplet_diagnostic *);
/* Any thread: synchronized token-level POD copy only. No collection, join,
 * owner delivery, JS or Host callback. Runtime/generation/released tokens are
 * checked under the copy lock. Close invalidates results, so CLOSED queries
 * return INVALID_HANDLE; the Host must join all callers before destroy. */
qaplet_code qaplet_get_process_status(qaplet_runtime *, const qaplet_process *, qaplet_process_status *, qaplet_diagnostic *);
qaplet_code qaplet_process_set_ref(qaplet_runtime *, const qaplet_process *, uint32_t, qaplet_diagnostic *);
qaplet_code qaplet_process_release(qaplet_runtime *, const qaplet_process *, qaplet_diagnostic *);
/* Thread-safe POD query, including CLOSED. */
qaplet_code qaplet_get_process_snapshot(qaplet_runtime *, qaplet_process_snapshot *, qaplet_diagnostic *);

/* Named native work, M2 only. Declarations and names are copied atomically.
 * Zero byte/concurrency fields inherit the finite instance defaults. No
 * replacement/unregister. First accepted execution or valid pump freezes them.
 * Code/userdata remain Host-owned and valid until native QUIESCENT (also when
 * close already returned). Host allocations are outside the SDK byte budget.
 * A worker receives only native data. Its context/output/input are borrowed
 * until return. Set output.length <= capacity; no completion-time allocation
 * is needed. Returning an ordinary error fails this task, preserving native
 * error_domain/error. Cancellation is cooperative; never kill the worker. */
typedef struct qaplet_work_context qaplet_work_context;
typedef struct {
  void *data;
  size_t capacity, length;
  uint32_t error_domain;
  int32_t error;
} qaplet_work_output;
typedef qaplet_code (*qaplet_work_function)(qaplet_work_context *, const void *, size_t,
                                          qaplet_work_output *, void *userdata);
typedef struct {
  uint32_t struct_size, abi_version;
  const char *name;
  size_t name_length;
  qaplet_work_function function;
  void *userdata;
  uint64_t input_bytes, output_bytes, concurrency;
  uint64_t flags, reserved[2];
} qaplet_work_handler;
typedef struct { uint64_t runtime_id, slot, generation; } qaplet_work_task;
typedef enum {
  QAPLET_WORK_QUEUED, QAPLET_WORK_RUNNING, QAPLET_WORK_SUCCEEDED,
  QAPLET_WORK_FAILED, QAPLET_WORK_CANCELLED
} qaplet_work_state;
typedef struct {
  uint32_t struct_size, abi_version;
  qaplet_work_state state;
  qaplet_code code;
  uint32_t error_domain;
  int32_t error;
  void *buffer;
  size_t capacity, length;
} qaplet_work_result;
typedef struct {
  uint32_t struct_size, abi_version;
  uint64_t handlers, handlers_peak, operations, operations_peak;
  uint64_t queued, running, retained_results, pending_handoffs;
  uint64_t data_bytes, data_bytes_peak, worker_threads, worker_threads_peak;
  uint64_t worker_stack_bytes, worker_stack_bytes_peak;
  uint64_t accepted, rejected, completed, cancelled;
  uint32_t start_error_domain;
  int32_t start_error;
} qaplet_work_snapshot;
#define QAPLET_WORK_HANDLER_INIT {sizeof(qaplet_work_handler), QAPLET_ABI_VERSION, NULL, 0, NULL, NULL, 0, 0, 0, 0, {0}}
#define QAPLET_WORK_TASK_INIT {0, 0, 0}
#define QAPLET_WORK_RESULT_INIT {sizeof(qaplet_work_result), QAPLET_ABI_VERSION, 0, 0, 0, 0, NULL, 0, 0}
#define QAPLET_WORK_SNAPSHOT_INIT {sizeof(qaplet_work_snapshot), QAPLET_ABI_VERSION, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}
/* Owner, outside drives/callbacks. Submit copies input and reserves the complete
 * output/record before publishing acceptance. Saturation is RESOURCE_LIMIT,
 * with output unchanged. Actual SDK allocation refusal permanently fails the
 * Runtime. Every task defaults ref until owner completion handoff, including
 * the complete jobs checkpoint. Neither onEvent nor business queue space is
 * required. Retained results and physical use continue charging capacity. */
qaplet_code qaplet_register_work_handler(qaplet_runtime *, const qaplet_work_handler *, qaplet_diagnostic *);
qaplet_code qaplet_work_submit(qaplet_runtime *, const char *name, size_t name_length,
                               const void *input, size_t input_length, qaplet_work_task *, qaplet_diagnostic *);
/* Owner only. Nonterminal results/releases return BAD_STATE. BUFFER_TOO_SMALL
 * reports length without copying bytes; retry never reruns the task. Release
 * invalidates this token; cross-Runtime and stale generations are INVALID_HANDLE.
 * A released logically cancelled task keeps physical buffers/record charged
 * until its native call returns. Release never proves syscall/thread exit.
 * close invalidates result access and retires records only after physical use.
 * Neither logical cancellation nor close proves native exit/join. */
qaplet_code qaplet_work_copy_result(qaplet_runtime *, const qaplet_work_task *, qaplet_work_result *, qaplet_diagnostic *);
qaplet_code qaplet_work_release(qaplet_runtime *, const qaplet_work_task *, qaplet_diagnostic *);
/* Any thread, native POD only. A context is valid only within its worker call;
 * cancelled reports cooperative stop/close, not rollback or syscall exit. */
qaplet_code qaplet_get_work_snapshot(qaplet_runtime *, qaplet_work_snapshot *, qaplet_diagnostic *);
int qaplet_work_cancelled(qaplet_work_context *);

/* Global query, POD snapshots, post_event and request_stop are thread-safe. Other operations
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
 * snapshots, post_event, request_stop and this setter may reenter a Host callback. */
qaplet_code qaplet_host_set_result(qaplet_runtime *, uint64_t call_id, const qaplet_value *, qaplet_diagnostic *);
/* Script semantics; retains the instance's global state. source is a borrowed
 * pointer with explicit byte length, strictly validated as UTF-8 before acceptance.
 * Supports undefined/null/bool/Number, BigInt in [-2^63,2^64-1], strict UTF-8
 * strings and safely copied ordinary-buffer Uint8Array views. BigInt uses INT64
 * through INT64_MAX, then UINT64; Number remains DOUBLE (including NaN/-0).
 * Objects/functions are never implicitly converted. Strings may contain NUL;
 * lone surrogates are INVALID_UTF8. A string/byte result is limited to
 * limits.bridge_value_bytes encoded bytes (default QAPLET_VALUE_BYTES_MAX).
 * Shared/detached/out-of-bounds views and
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
/* Same module/cache semantics with limits tightened for this execution only. */
qaplet_code qaplet_run_entry_with_options(qaplet_runtime *, const char *path, size_t length,
                                          const qaplet_execution_options *, qaplet_progress *, qaplet_diagnostic *);
/* Script evaluation with an explicit absolute file URL filename for relative
 * dynamic import. The base need not exist. Ordinary eval has no relative base.
 * The combined variant applies per-execution tightening to that same Script. */
qaplet_code qaplet_eval_with_base(qaplet_runtime *, const char *source, size_t length,
                                 const char *base_url, size_t base_length,
                                 qaplet_progress *, qaplet_diagnostic *);
qaplet_code qaplet_eval_with_base_and_options(qaplet_runtime *, const char *source, size_t length,
                                             const char *base_url, size_t base_length,
                                             const qaplet_execution_options *, qaplet_progress *, qaplet_diagnostic *);
/* qaplet_eval, qaplet_run_entry and qaplet_eval_with_base are default-options
 * convenience calls. All options-bearing variants accept NULL or zero fields
 * to inherit instance maxima; nonzero limits may only tighten them. Rejected
 * options do not accept execution, change the module cache or clear results.
 * An accepted execution
 * has one absolute CLOCK_MONOTONIC deadline (default 30000 ms), including host
 * waits, and one cumulative dispatch limit (default 100000 jobs/events).
 * Exhaustion permanently FAILS the Runtime even if JS catches an exception.
 * Checks are cooperative; arbitrary native calls/GC are not hard-preempted.
 * NULL options inherit instance limits. Options cannot increase those limits. */
qaplet_code qaplet_eval_with_options(qaplet_runtime *, const char *source, size_t length,
                                    const qaplet_execution_options *, qaplet_progress *, qaplet_diagnostic *);
/* Runs at most limits.pump_jobs and limits.pump_events (defaults 256 and 32),
 * without sleeping. Jobs settle before the
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
/* Any thread may irreversibly request stop while the handle remains alive.
 * OK acknowledges acceptance, independently of event queue capacity. Acceptance,
 * event posts and owner claims share one order. Ordinary work is then rejected;
 * claimed work exits cooperatively and unclaimed events/jobs never run again.
 * Owner interrupt polling or a subsequent pump confirms STOPPED/DONE, returning
 * EXECUTION_STOPPED (23). M2 also wakes system waiting; native calls are not preempted.
 * Repeated calls in STOP_REQUESTED/STOPPED/FAILED/CLOSED succeed without changing
 * state or the first recorded stop/budget/allocation termination reason.
 * close releases remaining user work without running finally/cleanup JS.
 * Host must withdraw producers and join in-flight calls before destroy. */
qaplet_code qaplet_request_stop(qaplet_runtime *, qaplet_diagnostic *);
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
/* Owner may close OPEN (including PENDING), STOP_REQUESTED, STOPPED or FAILED
 * when no JS/Host callback is executing; dangerous reentry returns BUSY.
 * Close shuts event acceptance before releasing raw queued work and JS refs,
 * retains copied Host bindings through engine finalization, then publishes
 * CLOSED. It runs no user jobs/finally/cleanup JS and does not wait for workers.
 * Host function code/userdata must remain valid until close returns.
 * A queryable CLOSED handle and its synchronization survive until destroy;
 * valid posts are BAD_STATE, snapshots remain available, repeated stop/close
 * succeed without repeating cleanup or replacing the execution first cause.
 * Any detectable close error is a separate call diagnostic, not a new first
 * termination reason. Current engine/native release APIs return no cleanup error.
 * destroy requires CLOSED and is called exactly once by the owner.
 * Before destroy the host must withdraw producers and join all in-flight calls.
 * The SDK cannot detect future calls or dangling pointers. After successful
 * destroy every alias is invalid. A failed create never publishes a handle. */
qaplet_code qaplet_close(qaplet_runtime *, qaplet_diagnostic *);
qaplet_code qaplet_destroy(qaplet_runtime *, qaplet_diagnostic *);
#ifdef __cplusplus
}
#endif
#endif
