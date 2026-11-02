# Embed SDK development

The initial SDK provides one owner-thread Runtime with one private Context,
synchronous Script evaluation, strict copied results, synchronized POD snapshots and
separate close/destroy operations. `include/qaplet.h` is the public contract.
The engine allocator tracks backing ownership from its first allocation.
Before publication, creation installs only engine intrinsics: no user JavaScript,
Host modules or external native resources run. Failed creation therefore reclaims
outstanding backing blocks directly, without traversing a partial engine graph.
Published instances use normal Context/Runtime finalization during close.
Query capability bits before using a feature.

The Host-module capability adds pre-execution `qaplet_register_host_module`.
Registration validates the complete declaration before publishing an identity;
it copies UTF-8 names, declarations and constant payloads in native storage.
Registered `host:` modules share the finite module registry with source ESM.
The first accepted eval/run or valid pump locks registration; duplicates and
replacement are rejected. C function code and userdata belong to the Host and
must remain valid until close returns. Copied descriptors remain alive through
normal engine finalization. No Host resources run in the unpublished factory.

Callbacks execute synchronously on the owner, with a finite default of 32 strictly bridged
arguments. Each string/byte value defaults to a 1 MiB limit. The instance's
`limits.host_arguments` and `limits.bridge_value_bytes` configure those maxima.
Arguments are borrowed
until callback return; `qaplet_host_set_result` synchronously copies its value.
An unset return is undefined and a repeated setter preserves the first return.
The setter uses a Runtime and monotonically unique callback ID, so an expired
callback never requires dereferencing an old callback stack. Outside/expired
setters return BAD_STATE; other owner execution operations return BUSY during
callbacks, while ABI/snapshot queries and copied event posts remain available. Ordinary callback
errors throw intrinsic JS Errors with an own numeric `code`; HOST_ERROR is 19.
Actual storage refusals and execution first causes remain permanent even when
JavaScript catches the thrown error. The distributed `host-host-modules.c` and
`modules/host-bridge.mjs` exercise this contract through the public header/SDK.

Promise/jobs capability adds explicit owner-driven `qaplet_pump`. An accepted
eval can return PENDING, and each pump obeys `limits.pump_jobs` (default 256). Empty
jobs do not finish an unsettled entry Promise. Rejections are judged at a complete
jobs checkpoint; ordinary errors settle runnable jobs before DONE and preserve
the Runtime for reuse. Only genuine Promises are waited on, without consulting
thenable getters or Promise constructors. Closing a PENDING execution releases
its references and remaining jobs without running user cleanup JavaScript.

Cooperative-stop capability (bit 14) adds any-thread `qaplet_request_stop`.
Success acknowledges an irreversible request independently of event capacity;
the same mutex orders stop, event acceptance and owner claims. STOP_REQUESTED
immediately refuses ordinary work. Owner interrupt polling or the next pump
confirms STOPPED/DONE with `EXECUTION_STOPPED` (23), including idle PENDING.
Already claimed work exits cooperatively; unclaimed events/jobs remain raw
until close releases them without scheduling user finally/cleanup JavaScript.
Stop, deadline, dispatch and actual allocation refusals preserve whichever cause
was recorded first. Repeated stop in STOP_REQUESTED/STOPPED/FAILED/CLOSED is a
successful no-op. No OS waker, native-call cancellation or recovery is provided.
The distributed `host-stop.c` covers controlled callback/queue handoffs and
measured owner CPU progress before interrupting synchronous and async loops.

POD-events capability (bit 12) adds any-thread `qaplet_post_event`. Native input
has a uint32 type, uint64 token (including zero), byte pointer and explicit length.
The fixed queue defaults to 256 slots with 1024 inline payload bytes each;
`limits.event_queue_slots/event_payload_bytes` configure finite capacities.
Queue slots and one owner scratch slot are allocated and charged at creation.
The `qaplet:events` builtin also reserves one module identity from creation,
including when never imported. No external Host callback or resource runs before
the factory publishes the Runtime.

JS calls `onEvent(fn)` exactly once; replacement and unregister are rejected.
This window is independent of the locked C declarations and works from a TLA
entry. Before registration, valid posts return `NO_HANDLER` (20). Invalid input
and oversize payloads are checked before lifecycle, then handler and capacity;
`PAYLOAD_TOO_LARGE` (22) and `QUEUE_FULL` (21) refuse the entire event. Success
copies all bytes and denotes acceptance. Multiple producers share the Runtime
mutex's acceptance order; owner pump claims that FIFO without blocking for work.
It calls `fn({type, token, payload})`, with Number, exact BigInt and an independent
Uint8Array. Source buffers can be immediately reused. Returned values and pending
Promises are ignored, while runnable jobs and rejection checkpoints still settle.

Each pump defaults to at most 256 jobs and 32 event callbacks, configurable through
`limits.pump_jobs/pump_events`; jobs settle before the
next event. Events share an active entry's ledger. Otherwise each event and its
jobs starts an independent ledger, retained across pumps until its checkpoint.
Ordinary event exceptions settle runnable jobs before DONE and permit reuse;
claimed events are never retried. Snapshots expose depth/peak and accepted,
rejected, consumed and dropped counters. Close first shuts acceptance, drops raw
queued POD and releases the handler without scheduling user JavaScript. The
distributed `host-events.c` and `modules/events-entry.mjs` exercise worker/owner
barriers and source ESM delivery through the public SDK.

File-module capability adds `qaplet_run_entry` for real source ESM. The loader
resolves dependencies against their importer and entry paths against the cwd
captured at creation. It retains canonical realpath file URLs, source bytes,
compiled module values and known failures in one registry until close. Cached
modules keep their first `import.meta.url/main`, even if later run explicitly.
`qaplet_eval_with_base` gives a Script an explicit absolute file URL for relative
dynamic imports; ordinary eval has no relative import base. Run results are
undefined and do not assimilate the module namespace, including an export named
`then`. Actual module evaluation Promises and TLA use the same explicit pump.

Finite defaults are 256 module identities, 4 MiB per source and 32 MiB retained
module source bytes. Snapshots expose module/source-cache counts; repeat cache
hits do not charge again. Discoverable root source-size refusals preserve the
previous completed result. Close releases registry/source/engine references;
the immutable cwd configuration stays in the CLOSED handle's native baseline
until destroy. The SDK distribution includes the actual `sdk/modules` source
fixtures used by the public C Host, so supplied consumers need no rebuild.
Module-policy capability bit 10 adds strict path refusals and the frozen failure
contract. Invalid requests consume no identity; normalized load/parse/link/eval
failures count as modules and retain their result until close. Source edits and
symlink retargets do not hot-update an existing identity or successful alias;
a new Runtime observes repaired files. Hard links retain distinct canonical
URLs. Native Windows drives are uppercase in URLs, with path case supplied by
OS realpath rather than global lowercasing. Only regular source files are read;
nonblocking open avoids waiting on a FIFO before rejecting it.

Module names reject NUL, including computed dynamic names at the engine's narrow
module-name conversion boundary. Ordinary bridge strings retain embedded NUL.
Query/fragment, nonempty authority, extra leading URL slashes, encoded NUL or
separators, UNC/device paths, unknown schemes and unsupported formats reject
before source evaluation. The public Host creates mutable fixtures in a private
temporary directory; supplied SDK source fixtures stay unchanged and are checked
both before and after consumption. `sdk.py --controlled-probe` explicitly admits
selected input for candidate probes only; ordinary builds/consumers reject it.
File runs and Script imports with an explicit base start the same execution
ledger as ordinary eval, including TLA jobs, host waits and module body loops.
Execution budgets use one absolute monotonic deadline (default 30,000 ms) and
one cumulative dispatch limit (default 100,000 jobs), preserved across PENDING
and every pump. `qaplet_config.limits` configures finite instance maxima;
`qaplet_eval_with_options` can only tighten them for that execution. NULL or zero
options inherit the instance maxima. Exhaustion returns `DEADLINE_EXCEEDED` or
`DISPATCH_LIMIT` and permanently marks the Runtime FAILED, including when async
machinery turns an engine interruption into a rejection. The allocator and
execution driver share an independent first-cause latch. Only a new accepted
execution on an OPEN Runtime starts a new ledger. Snapshots expose its cumulative
dispatch count; query and close remain available after failure. Interrupt polling
also ends a JavaScript loop; native callbacks, compiler/GC and cleanup retain
their cooperative boundary rather than a hard preemption guarantee.

Resource-limits capability bit 13 adds the complete effective finite configuration
in snapshots. Zero fields select defaults; creation copies and fixes capacities.
The source/cache/module and bridge input checks refuse calls without failing the
Runtime. Once a metered engine or SDK allocation is actually refused, the first
cause is permanent, including caught errors and accepted Script source copies.
SDK control/diagnostics, registry, queue/scratch, rejection records, callback
argument frames and bridge copies count toward native storage; retained module
source bytes use the separate cache ledger. Host userdata, external worker
storage and OS/libc allocations are outside these budgets. Engine accounting
starts at its first backing allocation and reports current/peak bytes, not RSS.
Automatic GC remains enabled; no pump forces full GC. Event counters saturate
at UINT64_MAX. `host-resources.c` exercises real small limits, caught storage
refusals and failed factory recovery through the supplied public SDK.

The copied-values capability adds exact signed/unsigned 64-bit BigInt, strict
UTF-8 strings (including NUL), and safely copied Uint8Array views. Number stays
double. Results reject arbitrary objects, functions, proxies, shared/detached
views, lone surrogates and values above the configured byte limit (default 1 MiB) without conversion
hooks. `qaplet_get_result` uses the caller's buffer and explicit capacity; a
short buffer returns the required payload length and leaves the stable result
available for retry. String payloads do not include an extra NUL terminator.

Every diagnostic is caller-owned. Safe exception text is bounded to 1023 bytes;
it uses primitive thrown strings or own string data messages on intrinsic Error
objects, and never calls a getter, Proxy trap or user conversion. Short/zero
diagnostic buffers retain the stable code, and positive capacities receive a
NUL terminator with a truncation flag where needed. UTF-8 truncation retains
whole codepoints. Result copies count toward native storage and are released
by the next accepted execution or close; rejected calls preserve the old result.

Build on Ubuntu with verified pinned inputs and the main worktree cache:

```sh
python3 scripts/sdk.py build --output-dir out/sdk/artifacts --report out/sdk/build.json
python3 scripts/sdk.py run --build-report out/sdk/build.json \
  --artifact out/sdk/artifacts/qaplet-host.com \
  --loader-dir /path/to/supplied/native/loaders --report out/sdk/run.json
```

On Windows use `python` and omit `--loader-dir` (the native PE route).
The run command validates supplied SDK/APE bytes and executes the C Host; it
never compiles. Loader selection, process evidence, APE audits and packaging
reuse the existing consumers. Output must be new or empty for each build.

The SDK ships `sdk/include/qaplet.h`, `sdk/lib/libqaplet.a` and its concomitant
`sdk/lib/.aarch64/libqaplet.a`, a public-only C example, and license notices.
Ubuntu's locked `cosmocc` can compile that example against the public include
directory and link the archive. The producer compiles the distributed
`sdk/host-contract.c` against that public include directory and archive before
packaging the Host APE. Its scalar case follows the minimal example; additional
cases check the currently implemented SDK contracts.

Runtime-snapshot capability (bit 16) adds `qaplet_config.argv/argc` and optional
`cwd/cwd_length`. Every argument is an explicit-length strict UTF-8 string,
including ordinary embedded NUL, bounded by `limits.bridge_value_bytes`.
The factory copies the strings and native
directory base; later buffer edits and process `chdir` do not change them.
NULL/zero cwd captures the native OS cwd; an explicit cwd must name an absolute
native directory. Paths reject NUL and invalid UTF-8. `qaplet:runtime` exports
the primitive cwd and a frozen argv array. Both this builtin and `qaplet:events`
reserve module identities from creation, so the minimum module capacity is two.
The immutable native snapshot belongs to the handle until destroy.

The same Ubuntu producer also delivers audited `qaplet.com`, both CPU sidecars
and maps, with the shared helper notices and loader. The supplied consumer
checks the complete distribution before and after running both public products.
`--cli-artifact` can explicitly select the sibling CLI; its default is
`qaplet.com` next to the supplied Host. Neither consumer compiles.

CLI syntax is `qaplet run <file> [-- args...]` and
`qaplet eval --source <text>`, plus `--help` and `--version`. The runtime argv
contains only the application arguments after `--`. Optional `--timeout-ms N`
and `--max-dispatches N` precede the command and configure the same finite SDK
instance budgets; zero selects its defaults. Run uses source ESM and prints no
namespace. Eval uses Script and prints only the copied SDK value: scalar text,
raw UTF-8 string bytes, or `0x`-prefixed lowercase hex bytes, then a newline.
It never converts an arbitrary JS object for display or diagnostics.

The CLI owns no external event producers. PENDING with a no-progress pump
therefore immediately prints `UNSETTLED_ENTRY` and closes the Runtime. SDK Hosts
keep the original PENDING/no-progress contract and may later supply an event.
Raw OS exits are 0 for success, 1 for execution/budget/unsettled failure, and 2
for CLI arguments. On Windows the CLI performs SDK close/destroy and flushes
stdio before an explicit native `ExitProcess`, avoiding the supplied libc's
shifted POSIX-wait-status exit path. CLI evidence compares raw UTF-8 log bytes,
including NUL, and preserves the actual OS exit without normalization.

The existing v3 lock continues to identify the unchanged supplier/M0 recipes.
Each SDK producer additionally binds all SDK and shared recipe files, effective
compile/link/archive settings, exact static archive components and final Host
bytes. `sdk_recipe.sha256` and `sdk.sha256` are separate identities; runtime
results in `run` reports retain both. An SDK build or local smoke does not certify
the full M1 Cross/Full matrix or release readiness.
