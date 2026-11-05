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
The M2-runtime bit currently covers the empty Runtime lifecycle and ready-work
drive only. Timer, OS waiting, work, process and stdio capabilities are absent.

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
`qaplet_process_release` remains BUSY before reap. STOPPED/FAILED owner
confirmation starts the same termination path before close. After close,
retry bounded `qaplet_quiesce`; destroy remains BUSY until physical cleanup.
The Host must never reap SDK children itself.

`package-consume` also builds and runs `host-process.c` using only the sealed
SDK outside the checkout. `fault-consume` separately builds `process-faults.c`
with recorded OS wrappers for deterministic helper/TLS handoffs, real native
errors and accepted wake failure. These controlled fixtures are distinct from
normal product evidence.
