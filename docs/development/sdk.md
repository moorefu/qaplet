# Embed SDK development

Mixed-resource lifetime validation is available through `sdk.py mixed-consume`.
Ubuntu extracts the sealed ZIP outside the repository, compiles the delivered
`host-mixed.c` and separate `mixed-faults.c` against its public header/archive,
then runs work, timers, direct children, byte pipes, regular stdio and terminal
leases in the same Runtime. The explicit six-helper test configuration covers
the simultaneous Windows process/file/Console combination; it does not change
the finite default of four helpers.

`sdk_mixed.py` replays those supplied consumer bytes on Windows, including real
ConPTY retirement and full native terminal-mode readback. Pass the SDK producer
with `--producer`, the Ubuntu mixed consumer report with `--consumer`, the
original supplied Host with `--supplied-host`, and consumer output with
`--artifact-dir`. Both commands retain per-case raw logs and code/data identities.

Entry DONE, drive IDLE, STOPPED, JS CLOSED and native QUIESCENT are separate
observations. A request thread publishes stop; the owner starts cancellation and
direct-child force/reap before close. Close releases synchronous Host bindings
and the JS engine without executing pending user then/catch/finally jobs.
Work code/data stay valid through native quiescence. HostNotify code/data and
the Runtime additionally stay valid until future external calls are withdrawn
and all in-flight producers are joined, even if native quiescence was observed.

The mixed nonreturning-work and uncertain-close cases intentionally report
`native_cleanup_complete: false`: destroy remains BUSY while other safe
resources retire. Repeated zero-wait quiesce calls can continue collection after
a cleanup error. Recorded finite waits are cumulative active-wait budgets,
not a hard limit on the entire close/quiesce/native-destructor call. A supervisor
timeout is an incomplete execution, never proof of native cleanup.

M2 `qaplet:work` exports `submit(name, input)`. The Host registers the named
native handler before the first execution. `name` must be a string and `input`
a genuine, non-shared `Uint8Array`; neither argument is implicitly coerced.
Acceptance copies its current view bytes and reserves the handler's full output
capacity using the same bounded WorkPool as C submission.

```js
import { submit } from 'qaplet:work';
const task = submit('read', encodedPath);
const bytes = await task.promise; // a copied Uint8Array
```

The Task has `cancel()`, `ref()`, `unref()` and `hasRef()`. New tasks default to
ref. `cancel()` records cancellation; repeated accepted cancellation succeeds,
while completion winning first throws an Error with `code: 29`
(`ALREADY_COMPLETED`). Cancellation rejects `promise` with `code: 28`
(`CANCELLED`), without inventing a native error. Work operation errors have own
numeric `code`, `errorDomain` and `error` properties; ordinary native errors
retain their domain/value and do not permanently fail the Runtime.

The owner settles the Promise through the completion category and completes its
runnable jobs before releasing the task's default ref and native result. A
cancelled call still running physically retains its copied buffers and capacity.
`unref()` only changes natural loop exit; an explicit pump/run_once can deliver
ready unref work. Keeping a completed Task or Promise does not retain native
results, and discarding a Task does not cancel it. Finished tasks report
`hasRef() === false`; ref/unref then have no effect. Stop/close release their JS
references without running remaining then/catch/finally callbacks. Work code and
userdata remain valid until native quiescence. The existing public C submit,
result-copy and release callback/drive guards retain their documented behavior.

M2 `qaplet:process` exports `spawn(path, options)`. `path`, `cwd` and every
argument are strict strings. `argv` is the complete C argument vector, including
argv[0]; omitting it uses the executable path as argv[0]. `env` is a complete
replacement object with string values, or an array of `KEY=value` strings.
Omitting it uses the Runtime factory's immutable environment snapshot. The
optional `stdio` array has exactly three entries (`pipe`, `inherit`, `ignore`),
and defaults to three pipes. Paths are explicit; relative paths use the Runtime
cwd, with the same validation, native errors and inheritance rules as C spawn.

The Process has `stdin`, `stdout`, `stderr` (a byte endpoint, or null for an
inherited/ignored stream), `exited`, `closed`, `cancel()`, `ref()`, `unref()` and
`hasRef()`. `exited` resolves after actual direct-child exit/reap with `code`,
`exited`, `reaped`, `exitCode`, `signal`, `errorDomain`, and `error`. Accepted
process cancellation rejects it with code 28 and preserves those actual native
facts. `closed` resolves after reap and all managed endpoints end. It remains
separate from helper exit and the Host's native quiescence barrier. A ref Process
keeps the loop alive through reap, managed pipe closure and the complete owner
jobs checkpoint. Unref and garbage collection do not cancel it.
If a native wait fails and the SDK cannot establish reap, `exited` rejects with
the actual unreaped/error facts and `closed` stays pending. The default reference
remains until explicit unref/stop/close. Native ownership and capacity remain
retained even after unref; cleanup errors and BUSY destroy report that uncertainty.

`endpoint.read(n)` and `endpoint.write(bytes)` return an IO Task with `promise`
and `cancel()`. Reads use a positive integer bound and resolve to
`{bytes: Uint8Array, eof: boolean}`; an unavailable read stays pending. Writes
accept a genuine non-shared Uint8Array, copy its current view before acceptance,
and resolve to `{bytesWritten}`. Errors preserve `code`, `errorDomain`, `error`,
and the write's actual `bytesWritten`; read errors also preserve a copied
`bytes` array. Inspect the byte count before retrying a failed write. Each
endpoint permits one unfinished physical operation; a second returns BUSY.
`finish()` closes the writing side after its accepted write completes, while
`close()` immediately requests shutdown/cancellation. Both expose completion
through `endpoint.closed`; finishing before or after awaiting the write is valid.
The byte endpoint also provides `ref()`, `unref()` and `hasRef()` for its IO.

```js
import { spawn } from 'qaplet:process';
const child = spawn('/bin/cat');
async function count(endpoint) {
  let total = 0;
  for (;;) {
    const { bytes, eof } = await endpoint.read(65536).promise;
    total += bytes.length;
    if (eof) return total;
  }
}
const output = Promise.all([count(child.stdout), count(child.stderr)]);
await child.stdin.write(new Uint8Array([65, 0, 255])).promise;
child.stdin.finish();
const status = await child.exited;
const lengths = await output;
await child.closed;
```

The adapter uses the same public C IO tokens, shared operation/data reservations
and completion rotation as native callers. Result reservations and Promise roots
remain until the complete jobs checkpoint; physical IO/helpers retain their
storage until completion/join even after logical release. Stop/close drop the JS
roots without running remaining then/catch/finally callbacks, start direct-child
cancellation, and leave native cleanup to close/quiesce/destroy.
An ordinary completion callback error returns to the Host after its jobs
checkpoint, preserving an OPEN Runtime and its existing child/IO resources for
further owner drive.

`python3 scripts/sdk.py build` produces `qaplet-sdk.zip` on Ubuntu with the
public header, x86_64 and hidden `.aarch64` static archives, complete pure C
application, source modules, licenses, manifest and audited Host/CLI/application
APEs. Preserve the hidden archive when copying. `sdk/manifest.json` identifies
both target components, ABI 1, capability names/bits, all effective finite
defaults, engine/toolchain/recipe/source and each supplied executable.

`sdk.py run --build-report FILE --artifact .../qaplet-host.com` verifies the
entire delivered ZIP, manifest, SDK and APE bytes before and after supplied
execution. `sdk.py package-consume --build-report FILE --artifact
.../qaplet-host.com --output-dir NEW_DIR --loader-dir SUPPLIED_LOADERS
--report FILE` performs the additional Ubuntu compile boundary: it extracts
the actual ZIP under a fresh POSIX `/tmp` application directory outside both
source checkouts, compiles only `sdk/application.c` with `sdk/include` and
the supplied static archives, publishes/audits the resulting APE using the
existing linker protocol, then runs the same required application catalog.
Commands, external cwd, source/archive/ZIP identities and raw logs are retained.
That one-use application fixture is separate from reusable `.cache` inputs.

The complete application registers Host declarations, runs static/dynamic ESM
and TLA, receives copied worker events, pumps on the owner, gets results,
requests/confirms stop, closes while the worker retains the control handle,
joins it, releases a real Host FILE and finally destroys the Runtime.
Its explicit rejection, queue-full, deadline and missing-module modes exit 1
after cleanup; argument errors exit 2. `--abi` validates short prefixes,
zero/nonzero tails, unknown flags/reserved fields and independent worker
diagnostics against the supplied public SDK. `--metadata` independently queries
the live capabilities and defaults and must agree with the manifest. The
package README contains copyable build/run commands and Host ownership rules.

The initial SDK provides one owner-thread Runtime with one private Context,
synchronous Script evaluation, strict copied results, synchronized POD snapshots and
separate close/destroy operations. `include/qaplet.h` is the public contract.
The engine allocator tracks backing ownership from its first allocation.
Before publication, creation installs only engine intrinsics: no user JavaScript,
Host modules or external native resources run. Failed creation therefore reclaims
outstanding backing blocks directly, without traversing a partial engine graph.
Published instances use normal Context/Runtime finalization during close.
Query capability bits before using a feature.

Close accepts OPEN (including PENDING), STOP_REQUESTED, STOPPED and FAILED on
the owner outside JS/Host callbacks; callback reentry returns BUSY. It first
closes event acceptance, drops raw queued events and retained JS references,
then finalizes the Context/Runtime while copied Host bindings still exist.
Only after native engine release does it publish CLOSED. It runs no user jobs,
finally blocks or FinalizationRegistry cleanup and never waits for producers.
The current release APIs (`JS_FreeContext`, `JS_FreeRuntime`, native frees)
return no cleanup failure; a future detectable cleanup error belongs to the
close call diagnostic and must not replace the recorded execution first cause.

The CLOSED control handle, fixed configuration and mutex remain valid until
destroy. Producers may still query snapshots, receive BAD_STATE for valid
posts, or issue idempotent stop; input errors retain their validation priority.
The Host withdraws producers and joins every in-flight call before owner
destroy. Neither future calls nor stale pointers can be detected after release.
Host code/userdata/resources stay Host-owned and valid through close returning;
the Host may then release them and finally destroy the drained handle.
`host-close.c` exercises all closure states, observed producer overlap, raw
queued work suppression and actual allocation/stop competition in both orders.

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
`qaplet_eval_with_options`, `qaplet_run_entry_with_options`, and the combined
`qaplet_eval_with_base_and_options` can only tighten them for that execution.
The original module/base calls remain ABI 1 default-options conveniences.
Rejected options preserve the previous result, state and module/source cache;
the absolute deadline is prepared before module identity, base or source storage
can change, including an otherwise valid timeout whose addition would overflow.
The packaged `host-modules.c` exercises invalid structures/over-max values,
zero/default options, cached runs, PENDING refusal priority and both tightened
cross-pump dispatch and host-wait deadlines. NULL or zero
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

`sdk.py run` runs the complete 98-case Host/CLI/application catalog and the
actual public lifetime loop (1,000 create/close/destroy cycles). It records the
Linux x64 minimum of 1,000 and other native target minimum of 100; the current
Host executes 1,000 on every target. The known Windows file-symlink fixture
remains explicitly optional/blocked only for its measured 77/19712 exit.

`sdk.py matrix --profile fast|cross --build-report BUILD --artifact HOST
--loader-dir LOADERS --input-report RUN` independently consumes supplied native
evidence. Repeat `--input-report`, or select roots containing `run.json` using
`--input-dir`. Fast requires native Linux x64; Cross requires all five native
targets. The consumer checks the authoritative catalog, raw output and exits,
argv, intervals and deadlines, measured host/loader, SDK/package/APE/producer
joins and actual lifecycle totals. Missing, skipped, timed-out, translated or
guest execution cannot complete native Cross. Explicit original-path mappings
and evaluator source identities are retained. CI augments the existing native
jobs and explicitly transports hidden SDK files. Full/strict/release gates
remain separate.

For a measured missing ARM64 kernel configuration, export the same verified
distribution and run the existing locked full-system guest:

```sh
python3 scripts/sdk_guest.py export --build-report out/sdk/build.json \
  --distribution out/sdk/artifacts --page-size 65536 \
  --manifest out/sdk-64k/payload.json --selection out/sdk-64k/selection.json \
  --report out/sdk-64k/export.json
python3 scripts/arm64_guest.py run --cache /main/checkout/.cache/guests \
  --page-size 65536 --manifest out/sdk-64k/payload.json \
  --timeout 900 --command-timeout 60 --report out/sdk-64k/guest.json
python3 scripts/sdk_guest.py import --selection out/sdk-64k/selection.json \
  --guest-report out/sdk-64k/guest.json --report out/sdk-64k/import.json
```

Export verifies the current SDK product recipe, complete ZIP/distribution,
public components, three executable slices and producer-derived ARM64 loader.
It emits all 98 required public cases; export success has
`evidence_verified=false`. Import and its read-only `verify` alias replay the
locked system/TCG command, kernel/config and signed-input identities, actual
serial CPU/page facts, original raw logs, before/after payload hashes, cleanup
and the authoritative process catalog. Guest results remain
`execution_mode=emulated-system`. Native coverage takes priority; qemu-user and
cross compilation supply no kernel coverage.

Moved/offline evidence uses `--input-manifest FILE`, with schema 1,
`profile: "sdk-guest"`, explicit `selection` and `guest_report` references,
and optional `path_mappings`/`transports` in the shared `core_gate.Evidence`
format. Reports retain original paths and evaluator source identities separately
from the product recipe. `--expected-commit SHA` additionally requires the exact
Qaplet producer source commit. Import verifies its selected kernel scope only;
it never substitutes for native Cross, the original M0/Core/offline Full proof,
or the deferred original-archive mirror gate. `release_ready` remains false.

The complete read-only M1 join uses the existing strict runner:

```sh
python3 scripts/check_evidence.py --config out/m1-selection.json \
  --report out/m1-evidence.json
```

Select schema 1 / `profile: "m1-evidence"` with an explicit `input_manifest`.
The [M1 manifest contract](evidence.md#正式-m1-严格入口) selects original provider
ZIPs, one SDK producer, the external public-only package consumer, exactly five
native reports, measured 64K coverage and a separate current M0 Full manifest.
The entry replays actual contracts without executing products or changing loader
modes; its required mutation regressions cannot skip. `--preflight-only` has a
distinct scope and performs no mutation regressions. Product and evaluator source
identities remain separate. The successful current closure uses product commit
`21bd317f4246571f0048789ca97167632c09f924` / Full `41489263809`; see the
[integration record](../../.scratch/qaplet-m1/16-evidence-validation.md).

## M2 显式空实例增量

`qaplet_create` 保持 M1 布局、reserved 和旧模式。新增的
`qaplet_create_m2(base, m2, output, diagnostic)` 使用独立 `qaplet_m2_config`
和 `qaplet_m2_limits`，0 选择规格的有限默认值。`qaplet_query_abi` 查询库能力；
`qaplet_get_m2_snapshot` 查询当前实例启用的能力、两套有效限额、生命周期、
执行阶段与原生清理状态，CLOSED 后仍可查询。M1 实例不取得 M2 能力。

当前 `QAPLET_CAP_M2_RUNTIME` 只覆盖显式空实例创建、已有就绪 jobs/events 的驱动、
独立原生清理状态及空实例 quiesce。它不签发 timer、Reactor/OS 等待、WorkPool、
process 或 stdio 支持。配置中的后续资源容量不会启用对应资源或创建线程。

`qaplet_run_once` 执行一轮有界就绪工作，`qaplet_run` 持续推进至自然空闲、
错误、停止或没有可推进来源的 PENDING 入口。`qaplet_drive_result` 分开返回
进展、阶段、code 和原因；单独悬挂 Promise 返回 `UNSETTLED_ENTRY` 原因，旧
`qaplet_pump` 仍保持 PENDING/progressed=0。空实例即使给出正等待预算也立即空闲。
无进展推进不替换最近结果；整个 pump/run/run_once/quiesce 调用受重入保护。

M2 close 释放 JS 并保留 CLOSED 句柄；`qaplet_quiesce` 仅接受 CLOSED，0 检查，
有限等待可重试，不执行 JS。空实例立即达到 `QAPLET_NATIVE_QUIESCENT`。后续原生
provider 必须保留物理清理责任直到退出/join 与存储退休后才能解除；未清理时
destroy 返回 BUSY。宿主仍须撤下并 join 外部生产者，不能用原生 quiescent 推断
未来外部调用已经停止。

当前供应目录包含 105 项普通公开合同，并以冻结 `m1-qaplet.h` 编译独立旧头文件
消费者验证新 archive。`scripts/sdk.py fault-consume` 则从同一供应 ZIP 包外编译
`m2-faults.c`，以显式系统符号包装运行四个初始化失败阶段、资源回滚及
PENDING/CLOSED 整段驱动拒绝检查；其独立 scope、源码身份、链接参数和
`controlled_system_fixture` 标记不混入普通产品目录。示例：

```sh
python3 scripts/sdk.py fault-consume --build-report out/sdk/build.json \
  --artifact out/sdk/artifacts/qaplet-host.com \
  --output-dir out/sdk/fault-artifacts --loader-dir /path/to/supplied/loaders \
  --report out/sdk/fault-consume.json
```

该增量的本地成绩与未完成矩阵见 [票据 01 实施记录](../../.scratch/qaplet-m2/01-runtime-validation.md)。

M2 直接子进程的 `qaplet_process_cancel` 可在任意线程调用，不进入 JS，也不是
signal-safe。它与原生退出发布在同一接受锁下排序：取消先接受后，status.code 为
`CANCELLED`，重复取消为 OK；退出先发布则为 `ALREADY_COMPLETED`。实际
exit/signal/native error、reap、owner 交付与 helper 物理退出独立保留。取消只强制
终止并回收直接子进程，不承诺整树或温和信号，也不等于输出管道关闭。
release 在 reap 前仍为 BUSY；helper 未真实 join 时，已 release 的槽和预算仍保留。
owner 确认 STOPPED/FAILED 即走相同终止责任，无须先 close；close/quiesce 不执行
用户 jobs/finally。失去 child identity 后不再向数字 PID 重发信号。

`sdk.py package-consume` 现在实际包外编译并执行全部公开 Process catalog；
`fault-consume` 以独立配方编译 `process-faults.c`，保留 helper 入场/TLS gate、
真实 terminate error、取消接受后 wake 故障、创建 rollback 与身份丢失证据。
Windows 只消费 Ubuntu 供应的同字节 APE/child；其 HANDLE 复用用例在真实 close
后立即持有 OS 复用事件，再验证旧 token 与迟到 helper 不影响新对象。

## M2 C Host 的 owned Process pipes

`QAPLET_CAP_PROCESS_IO`（bit 23）交付默认 stdin/stdout/stderr 三路 pipe；普通
I/O 错误为新增 `QAPLET_IO_ERROR=30`，旧错误值和 M1 POD 布局不变。公开独立
版本化端点、操作、结果和快照不暴露 fd/HANDLE。`qaplet_process_get_stdio` 的
stream 0/1/2 对应三路，inherit/ignore 返回 UNSUPPORTED。

owner 在驱动/回调之外提交 bounded read/write、finish/close、copy/release；取消
和 POD 查询可任意线程调用，不进入 JS；HostNotify 一律 BUSY。读 PENDING 与 EOF
分开，短读返回当前可用字节。写先复制，终态保存 OS 已确认接受的字节数，失败或
取消不回滚；该数不是 child 消费量。取消后原生 I/O 未退出时 copy 为 BUSY，确保
字节数稳定；release 立即使 token 失效，但物理调用、helper join 和 owner handoff
均完成之前不能归还 record/data 额度。start_error_domain/error 保存启动原生错误。

`qaplet_io_finish` 让已接受的 stdin 写完成后正常关写；`qaplet_io_close` 立即取消
并关端点。stdout/stderr 独立拉取保序，无消费者不继续采集；单方向一个未完成操作，
额外请求 BUSY。默认单块 64KiB，每 pump 256KiB，Work 与 I/O 合计 128 operations /
8MiB data，数据仍属于 16MiB native 总额。`qaplet_get_io_snapshot` 同时报告 I/O
占用和共享 current/peak；计量不把结果释放误当成线程物理退出。

child reap、exit 交付和 managed stdio end 独立；已退出仍可读剩余输出，EOF 也不证明
child 已退出。ref 持续到 reap、管道 EOF/显式 close 和完成 handoff；process release
在 stdio 未结束时仍 BUSY。显式 process cancel 保留剩余输出，Runtime stop/FAILED/
close 则关闭 managed endpoints 并保留必要原生存储，quiesce 只收原生责任。

POSIX owned pipe 使用 nonblocking owner poll；EPIPE 不改宿主 SIGPIPE handler/mask，
也不吞调用前已有的 pending SIGPIPE。Windows 用实际 named-overlapped pipe，借助
有限 helper 等待 native event 后唤醒内部 waker，不短轮询。创建期从仍 owned 的读端
查询实际 server output quota 并据此分块写；临时 child ends 按继承协议及时关闭。
独立探针保留“大 pending write 取消虽返回 0、peer 已读到字节”的真实反例，不把
它外推为精确零副作用。该 route 只覆盖本票 owned Process pipes，17 的宿主 stdio
仍须独立判能力。

`package-consume` 实跑上述公开 Process/I/O catalog；`fault-consume` 另以供应 ZIP
的 `io-faults.c` 包外编译独立 APE，记录 syscall 包装、真实 1024-byte 前缀、原生
错误、接受后 wake 失败、分配拒绝即 owner 启动清理、实际 fd/HANDLE 复用，以及
Windows helper 启动拒绝和 TLS 析构 gate。Linux/Windows 本地成绩与 deferred Cross
见票 15 实施记录；这些受控配方不混入普通产品或借用 stdio 支持声明。

普通 helper I/O wait 失败先结为保留原生错误的操作终态；仍取消并等待实际内核完成，
再经 HostNotify 唤醒外部 owner。该恢复不依赖 owner 周期轮询；event、OVERLAPPED、
buffer 在完成证明与 helper join 前继续保留。无法维持清理等待时保留不确定责任，
不把 helper 退出冒充内核已停止使用数据。

## TTY / Console 显式租约

能力 `QAPLET_CAP_TTY` 使用 `qaplet_tty_inspect/acquire/join/release`，端点沿用公开 IO
token、共享 operation/data、native handle/registration 和 helper 限额。真实当前
标准路由必须是相同终端；`join` 明确将另一个 stream 加入已有独占组，不打开隐藏
CONIN$/CONOUT$。第二个独立租约（含另一个 SDK Runtime）拒绝终端别名冲突；跨进程、
分别加载的 SDK 副本及 SDK 外原生修改仍由 Host 协调。原标准句柄始终借用不关闭。
POSIX 除真实 fstat dev/ino/rdev，还使用针对该端点的正值 `tcgetsid(fd)` 作为控制终端
别名佐证；不是普通 `getsid(0)` 或“同一进程即同一终端”。`/dev/tty` 与控制 pts 的
不同 fstat、相同终端查询有独立红绿；同进程/session 的无关 PTY 仍可同时租用。

JS 扩展既有 `qaplet:stdio`：`acquireTerminal(stream,{raw,vt})` 返回 IO facade，
`joinTerminal` 加入输出，`getSize/watchResize/unwatchResize/releaseTerminal` 提供
租约操作。初始化导出的 stdin/stdout/stderr 仍只反映已显式接入的端点，不因 import
接管。独立 `close` 只停该方向；其 `.closed` 等整组原生安全恢复。任一成员 release、
终端 IO 错误、owner 确认 stop 或 Runtime close 都结束整组。C release 在物理 IO/
join 尚未完成时可为 BUSY；JS release 返回该成员的 closed Promise。

POSIX 整份 termios 仅保存一次；共享 OFD flags、Windows 各 Console mode 按 join
逆序恢复并真实读回，最后恢复原 owner 状态。组内所有必要 IO/helper 实际退出和 join
之前不恢复。恢复失败返回原生 IO_ERROR，保留句柄、注册、租约和冲突责任，可原生重试；
cleanup 首错保留。普通 IO/JS 错误不自动使整个 Runtime 终止，stop/close/quiesce 不靠
执行用户 JS 收尾。不可捕获强杀不承诺终端恢复。

Host 的 SIGWINCH self-pipe 或 Console WINDOW_BUFFER_SIZE_EVENT 调用 any-thread
`qaplet_tty_notify_resize`；该函数不是 signal-safe，SDK 不安装进程 signal handler。
固定 pending 状态合并 latest，owner 交付时才查询最终尺寸；revision 不当业务计数。
Console input-only 的 size/watch 明确 UNSUPPORTED，显式 join output 后可用。ref/unref
控制观察保活；unref 不取消，显式 pump 仍可交付。关闭/取消后的旧代次通知不复活新端点。

Console 每次 write 要求完整且严格合法 UTF-8；非法或截断输入接受前拒绝，不跨独立
write 缓存半 scalar。单 write 的原生 chunk/pump 可切断多字节 scalar；完整 Unicode
文本块可连续写。BMP 输入、补充平面输出及 SGR31/reset 分开验收，ConPTY 补充平面
输入与原始非法 UTF-8 检测保持 UNSUPPORTED。POSIX raw TTY 与 pipe 任意 bytes/NUL
独立验收。Console `length/bytesWritten` 是 OS 已接受完整 scalar 对应的原 UTF-8 前缀；
独立版本化 `qaplet_get_tty_write_status` 与 JS `nativeUtf16Units/partialScalar` 保存
真实半 surrogate 副作用，前缀不能盲作重试 offset；旧 byte IO 结构/语义不变。

首次 Console lease 按需启动一个实例 join 控制线程，占共享 helper4 中的一个、一个
native handle 和独立计量的请求栈；存活到 Runtime close/quiesce。每个 Console IO
另用一个 helper，input 另预留 cancel event。该线程只在真实 pthread_join 后发布
可靠 wake，不恢复终端或进入 JS；owner 随后恢复整组。closed 的固定控制记录保持 ref
到安全交付。控制线程自身 TLS/物理退出未证实仍阻止 quiescence/destroy，不再生 reaper
或用周期短轮询掩盖。close 后须有限/可重试 quiesce，不能假定可立即 destroy。

`python3 scripts/sdk.py tty-consume --build-report FILE --artifact FILE --output-dir DIR
--loader-dir DIR --report FILE` 在 Ubuntu 从原 SDK ZIP 包外编译并运行正常与独立故障
TTY consumer。`python scripts/sdk_tty.py run --producer FILE --build-report FILE
--artifact-dir DIR --report FILE` 在 Windows 消费相同 APE 字节。报告保存完整模式原件、
原生 Console/controller 操作、真正 join/取消/关闭事实及故障配方；真实 PTY/ConPTY
仅是 Host 验证设施。当前本地与 deferred Cross/Full 范围见票 18 实施记录。
