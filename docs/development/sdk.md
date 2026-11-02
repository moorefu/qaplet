# Embed SDK development

The initial SDK provides one owner-thread Runtime with one private Context,
synchronous Script evaluation, strict copied results, synchronized POD snapshots and
separate close/destroy operations. `include/qaplet.h` is the public contract.
The engine allocator tracks backing ownership from its first allocation.
Before publication, creation installs only engine intrinsics: no user JavaScript,
Host modules or external native resources run. Failed creation therefore reclaims
outstanding backing blocks directly, without traversing a partial engine graph.
Published instances use normal Context/Runtime finalization during close.
Query capability bits before using a feature; cooperative stop is added by
a subsequent M1 ticket.

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
eval can return PENDING, and each pump executes at most 256 runnable jobs. Empty
jobs do not finish an unsettled entry Promise. Rejections are judged at a complete
jobs checkpoint; ordinary errors settle runnable jobs before DONE and preserve
the Runtime for reuse. Only genuine Promises are waited on, without consulting
thenable getters or Promise constructors. Closing a PENDING execution releases
its references and remaining jobs without running user cleanup JavaScript.

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
views, lone surrogates and values above the 1 MiB byte limit without conversion
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

The existing v3 lock continues to identify the unchanged supplier/M0 recipes.
Each SDK producer additionally binds all SDK and shared recipe files, effective
compile/link/archive settings, exact static archive components and final Host
bytes. `sdk_recipe.sha256` and `sdk.sha256` are separate identities; runtime
results in `run` reports retain both. An SDK build or local smoke does not certify
the full M1 Cross/Full matrix or release readiness.
