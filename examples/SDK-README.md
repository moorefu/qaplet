# Qaplet Embed SDK

M2 JavaScript can call a Host's pre-registered WorkPool handler with
`import { submit } from 'qaplet:work'` and
`const task = submit('handlerName', new Uint8Array(inputBytes))`.
`await task.promise` yields copied `Uint8Array` output. Task `cancel()`, `ref()`,
`unref()` and `hasRef()` control logical cancellation and loop liveness;
discarding the handle does not cancel work. Errors have numeric `code`,
`errorDomain` and `error` properties. Cancellation rejects with `CANCELLED`
(28); cancel after completion throws `ALREADY_COMPLETED` (29). The default ref
and native result remain through owner Promise settlement and its complete jobs
checkpoint. Physical native calls retain their buffers until they return. Host
work code/userdata stay valid through native quiescence, including after close.

This package supplies ABI 1 public C declarations, static Cosmopolitan archives
for x86_64 and aarch64, source modules, the CLI, and audited portable C Hosts.
The hidden `lib/.aarch64/libqaplet.a` is required by cosmocc; preserve it when
copying or archiving. `manifest.json` records the finite defaults, capabilities,
target components, selected inputs, recipe and the supplied executable identities.

On Ubuntu, with the pinned cosmocc toolchain available, extract this package to
an application directory outside the Qaplet checkout, then run:

```sh
cosmocc -Os -D_GNU_SOURCE -I sdk/include sdk/application.c sdk/lib/libqaplet.a -lm -lpthread -o application.com
./application.com sdk/modules/application.mjs
./application.com --reject sdk/modules/application.mjs
./application.com --queue-full sdk/modules/application.mjs
./application.com --deadline sdk/modules/application-loop.mjs
./application.com sdk/modules/application-absent.mjs
./application.com --metadata
```

The verified producer uses the shared APE linker with 64 KiB ARM64 alignment,
source-built portable loaders, publication sidecars and audits. Use that workflow
for distribution. The `package-consume` operation extracts this exact ZIP outside
the source checkout, compiles the application and its public ABI workload using only its
header/library materials, audits the resulting APE, and executes its catalog.

The complete example registers copied Host constants/functions, imports source
ESM statically and dynamically, waits at top-level await, receives copied events
from a real worker, pumps on the owner and retrieves results. The worker requests
stop; the owner confirms it and closes. The worker then uses the retained CLOSED
control handle, exits and is joined. The Host releases its real FILE resource
and finally destroys the Runtime. The temporary resource uses native Windows
TEMP or POSIX `/tmp` with mkstemp/fdopen. No engine header or JS value is exposed.

Module runs use `qaplet_run_entry_with_options`; `--deadline` tightens that
execution to 20 ms while keeping the Runtime's finite defaults. For a Script
with relative dynamic imports, use `qaplet_eval_with_base_and_options` with an
explicit absolute file URL and the same `qaplet_execution_options` structure.
NULL options or zero fields inherit the instance limits. Tightened deadlines
and dispatch counts persist through every PENDING return and pump. The original
`qaplet_run_entry` and `qaplet_eval_with_base` calls keep their default behavior.

`--abi` runs the packaged public ABI workload, including short structures,
compatible zero tails, unknown flags/reserved values and independent caller
diagnostics from a real worker thread. `--metadata` prints live SDK defaults.

Success exits 0; the reproducible rejection, queue-full, deadline and missing
module paths exit 1 after cleanup. Invalid arguments exit 2. Host resources,
worker storage and native calls are Host-owned and outside SDK memory budgets.
Pumping has no OS waker or I/O cancellation; the Host synchronizes its worker.

M2 is explicit: call `qaplet_create_m2` with the unchanged M1 base config plus
`qaplet_m2_config`. `qaplet_query_abi` reports library capabilities;
`qaplet_get_m2_snapshot` reports capabilities actually enabled on the instance,
finite effective limits, lifecycle, execution phase and native cleanup state.
Old `qaplet_create` handles do not acquire M2 capabilities or native resources.

Host standard streams require explicit `qaplet_stdio_attach(runtime, stream,
&endpoint, &info, diagnostic)` for stream 0/1/2 before importing `qaplet:stdio`.
`qaplet_stdio_inspect` classifies the actual POSIX fd or Windows `GetStdHandle`;
`kind` and `supported` are independent. Default creation does not inspect or
take over stdio. Without Host attachment the module exports three `null` values.
Attached `stdin`, `stdout` and `stderr` expose `kind`, `read(n)` or
`write(Uint8Array)` returning `{promise, cancel}`, plus `closed`, `finish`,
`close`, `ref`, `unref`, and `hasRef`. Read resolves `{bytes, eof}` and write
resolves `{bytesWritten}`; errors carry `code`, `errorDomain`, `error`, and
`bytesWritten`, plus a copied `bytes` array for consumed read data. Streams retain
their explicit attachment snapshot at first import. Close/finish and await `closed`
before the Host detaches a stream represented in JS; detach invalidates old JS
handles and does not rebind cached module exports.
Detach returns `BUSY` without closing or invalidating anything while a JS wrapper
still owns close delivery or its full jobs checkpoint. After C `qaplet_io_close`
and owner driving complete that handoff, the Host can retry detach safely.

POSIX pipes must already have Host-authorized `O_NONBLOCK`. Windows pipes must
be real overlapped byte pipes, with a queryable quota for writes. Synchronous
blocking pipes, unsupported devices, and TTY/Console byte attachment currently
return `UNSUPPORTED`, without changing flags or modes. TTY/Console classification
does not claim terminal text, UTF-8, resize, or lease support.

SDK close/EOF closes only its duplicated descriptor/HANDLE. A duplicate shares
flags, modes and often offsets: the Host must coordinate native stream replacement,
concurrent I/O and shared state through native quiescence. The SDK never changes
shared flags. Regular synchronous file calls use finite helper/thread-stack,
handle, operation and storage budgets. Cancel is logical; an in-flight file call
retains all capacity and the duplicate until actual exit/join. It stops keeping
the loop alive while still physically running; copying its result remains BUSY
until the actual byte count is stable. Close and finite quiesce preserve this
responsibility, and destroy stays BUSY while cleanup remains.

Idle standard streams do not keep the loop alive. Accepted referenced operations
retain through owner completion and all resulting Promise jobs. Retained C
results consume capacity without keeping the loop alive. `qaplet_stdio_detach`
invalidates that attachment and starts safe native retirement; attach again only
after coordinating the Host endpoint. `finish` closes the SDK write endpoint
after its current write, while `close` requests immediate cancellation. Both
preserve the Host original and already-written side effects.

When a Host closes a JS-wrapped endpoint through the C API without an IO task,
owner driving observes native POD status and queues the endpoint's already
reserved close notice in the normal completion category. `closed` settles on
the owner, with the same dispatch budget and full jobs checkpoint. Unref notices
wait for explicit driving. `qaplet_get_io_snapshot().pending_handoffs` includes
these close notices through that checkpoint, while operation/data counts stay
unchanged. Runtime close drops the pending JS work and completes native cleanup.
Ordinary JS callback errors keep an OPEN Runtime and its streams usable; only
terminal lifecycle transitions initiate shutdown.
The M2-runtime bit covers the explicit lifecycle and ready-work drive. Timers,
OS waiting, named work, direct children and owned process pipes have independent
capability bits. Host standard streams are not implicitly taken over.

`qaplet_pump` never waits. M2 `qaplet_run_once` drives one bounded ready-work round;
`qaplet_run` continues until idle, an error, stop, or an unsettled entry with no
source of progress. Progress, phase, error and return reason are independent.
An empty instance returns immediately even with a positive wait budget. Neither
drive automatically closes the Runtime or overwrites a result on an idle step.
The entire pump/run/run_once/quiesce call rejects nested owner operations.

Close releases JS and retains the CLOSED control handle. Query native state,
then use owner `qaplet_quiesce` (zero checks, finite waits) before destroy. An
empty M2 instance becomes native-quiescent immediately; future native resources
must retain their cleanup obligations until physical retirement. Host producers
must still be withdrawn and all in-flight calls joined before final destroy.

The shipped `m1-qaplet.h` and `host-m1-compat.c` compile a frozen M1-header
consumer against the current archive. `m2-faults.c` is a separate controlled
system-boundary fixture, never part of the normal product Host. On Ubuntu,
`scripts/sdk.py fault-consume` extracts the supplied ZIP outside the checkout,
links this public-only consumer with explicit syscall wrappers, audits its APE
and records four initialization refusals/resource retirement and whole-drive
reentry checks separately from normal runtime evidence.

For M2 direct children, `qaplet_process_cancel(runtime, &process, diagnostic)`
is an any-thread native control operation. Success records `QAPLET_CANCELLED`
in process status and starts forced termination of that direct child. Repeated
cancellation is successful; native completion published first returns
`QAPLET_ALREADY_COMPLETED`. Query actual `exited`, `reaped`, exit code, signal
and native error independently. Cancellation does not report a fake reap, close
managed output streams, or release a still-running helper's storage.
`qaplet_process_release` remains BUSY before reap and managed pipe end/handoff. STOPPED/FAILED owner
confirmation starts the same termination path before close. After close,
retry bounded `qaplet_quiesce`; destroy remains BUSY until physical cleanup.
The Host must never reap SDK children itself.

`package-consume` also builds and runs `host-process.c` using only the sealed
SDK outside the checkout. `fault-consume` separately builds `process-faults.c`
with recorded OS wrappers for deterministic helper/TLS handoffs, real native
errors and accepted wake failure. These controlled fixtures are distinct from
normal product evidence.

`QAPLET_CAP_PROCESS_IO` enables the default three owned byte pipes. Use
`qaplet_process_get_stdio` with stream 0/1/2 to get stdin/stdout/stderr tokens;
inherit/ignore have no managed endpoint. Submit bounded `qaplet_io_read` or
`qaplet_io_write`, observe `qaplet_get_io_status`, copy the stable result with
`qaplet_io_copy_result`, then `qaplet_io_release`. Submit independent stdout and
stderr reads; a second unfinished operation on the same endpoint returns BUSY.
PENDING read means temporarily unavailable; a successful explicit EOF ends that
read endpoint. The SDK reads only on demand, with finite OS backpressure.

Write input is copied before acceptance. A write result counts bytes confirmed
accepted by the OS, including a prefix before ordinary failure or cancellation;
it does not count child consumption. `qaplet_io_finish` drains an accepted stdin
write and then closes the write end. `qaplet_io_close` immediately requests
cancellation and endpoint shutdown. Operation cancel is any-thread native
control; read/write/finish/close/result copy/release belong to the owner outside
drives/callbacks. POD status/snapshots are any-thread; all reject HostNotify
reentry. Copy remains BUSY while cancelled physical I/O can still change its
byte count. Release invalidates the token immediately but does not free physical
buffers or a helper still exiting. Startup OS errors remain in the I/O snapshot.

Child exit/reap and stdout/stderr EOF are independent. A ref child stays live
through managed stdio end and owner completion handoff. Explicit process cancel
preserves buffered output for subsequent reads. Close/stop/FAILED initiate native
cleanup, without running JS. Work and I/O share the finite operation-count and
operation-data budgets inside the native-storage total; retained results, native
calls and pending handoffs continue charging their reservations.

The owned POSIX pipe adapter uses nonblocking owner poll and preserves the
Host's SIGPIPE disposition, thread mask and preexisting pending signal. Windows
uses real named pipes with overlapped parent handles, synchronous inherited
child ends, event waits and CancelIoEx. It queries the owned read end's actual
server output quota before inheritance, bounds native write chunks by that
quota, and closes the parent's temporary child ends after spawn. No borrowed
stdio route is implied. `io-faults.c` is a separate package-only system fixture
for real short transfer/error, accepted wake failure, allocation failure,
descriptor/HANDLE reuse, and Windows helper startup/physical-exit gates.
An ordinary helper I/O-wait error retains its native error and cancels the
pending request. The helper still signals the external Host when kernel I/O
actually completes; its event/data remain reserved until that proof and join.

`QAPLET_CAP_TTY` adds explicit terminal leases. `qaplet_tty_inspect` queries the
actual current standard route and its supported subset. `qaplet_tty_acquire`
saves the complete terminal mode before mutation and returns a normal IO
endpoint. Use `qaplet_tty_join` to add another explicitly selected standard
stream of the same terminal to that lease. Aliases conflict with a second
independent lease across SDK Runtimes. The SDK owns only non-inheritable
duplicates; the Host coordinates replacement, concurrent use and changes made
outside the SDK. Separately loaded SDK copies and other processes require Host
coordination. No private CONIN$/CONOUT$ fallback is opened.

In JS, `qaplet:stdio` exports `acquireTerminal(stream,{raw,vt})`; the returned
endpoint provides `joinTerminal`, `getSize`, `watchResize`, `unwatchResize` and
`releaseTerminal` alongside ordinary byte-task methods. The module's initial
stdin/stdout/stderr exports still describe only explicit existing attachments.

```js
const { acquireTerminal } = await import('qaplet:stdio');
const input = acquireTerminal(0, { raw: true });
const output = input.joinTerminal(1, { vt: true });
await output.write(new Uint8Array([65])).promise;
const chunk = await input.read(64).promise;
await input.releaseTerminal();
await output.closed;
```

An individual endpoint close stops that direction. Its `closed` barrier waits
for the whole lease group to restore; `releaseTerminal` / `qaplet_tty_release`
ends all members. C release can report BUSY while physical IO or join remains.
IO failure ends the group while an ordinary operation error can leave Runtime
OPEN. Owner-confirmed stop and Runtime close also initiate native group cleanup
without running user JS. All necessary helpers must physically exit and join
before the owner restores modes with complete native readback. POSIX termios is
saved once; file flags and Windows Console modes restore in reverse join order,
including dup/DuplicateHandle aliases. A failed restore retains its handle and
lease responsibility, reports the native error and excludes conflicting leases;
retry native cleanup. Forced process termination cannot guarantee restoration.

The Host supplies native resize notifications through
`qaplet_tty_notify_resize`, after its SIGWINCH self-pipe or Console window event.
The SDK installs no process signal handler; notify is not async-signal-safe.
One preallocated pending state coalesces notifications. Owner delivery queries
the current size and advances a revision; notification counts are not business
counts. Watches default to the endpoint's ref state; unref is not cancellation.
Console input size/watch is unsupported until an explicit output route joins.

POSIX raw TTY and ordinary pipes preserve arbitrary bytes independently of the
Console text adapter. Each Console write requires complete strict UTF-8;
invalid/truncated text is rejected before acceptance. Native pump boundaries
may split scalars. BMP Console input, supplementary Unicode output and the
SGR31/reset VT subset have distinct contracts. Raw invalid UTF-8 detection and
supplementary input through ConPTY remain unsupported. Console write results
count the original UTF-8 prefix of complete scalars accepted by the OS;
`qaplet_get_tty_write_status` and JS result/error `nativeUtf16Units` and
`partialScalar` additionally expose an accepted partial UTF-16 surrogate. Such
a prefix is not a safe retry offset. Ordinary byte IO keeps its existing count.

First Console acquisition starts one Runtime join control helper, charged to
the existing helper/handle/requested-stack limits until close/quiesce. Physical
Console IO consumes another helper; input also reserves a cancel event. Default
helper4 is shared with Process and file IO, including this control helper.
Actual Console worker join reliably wakes the owner; the control helper itself
still must physically exit and join before native quiescence. Small quotas
refuse before mutation/acceptance. Use close, bounded/retryable quiesce, then
destroy; a closed Runtime can retain native responsibility and return BUSY.

`sdk.py tty-consume` extracts the sealed SDK outside the checkout, compiles
normal and separately wrapped system-fault terminal consumers on Ubuntu, and
runs real native terminal fixtures. `sdk_tty.py run` replays those same supplied
bytes on another host. PTY/ConPTY creation belongs to these private Host
fixtures, not the public SDK. Other native targets, actual64K, Full and final
mixed-resource long runs remain separate acceptance scopes.
