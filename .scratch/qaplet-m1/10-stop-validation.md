# 10 · Cooperative stop under saturation and execution

Implemented on `codex/m1-10`. Final source is
`dfd0c7ac76d51f317ad4869e340bdf1d7eaed892`, including integration
`9174bfad759d105c2eb2dec6b0d7614ea3e4c865` and ticket 05's new pinned input.
All five ticket 10 criteria have actual public-boundary evidence. Full M1
native Cross, aggregate Full and release readiness are not claimed.

## Product boundary

`qaplet_request_stop` is any-thread, irreversible, and uses no business queue
slot or allocation. Capability bit 14 is `QAPLET_CAP_COOPERATIVE_STOP`; stable
reason 23 is `QAPLET_EXECUTION_STOPPED`, avoiding the existing lifecycle names.
The existing Runtime mutex orders event acceptance, owner event claims and stop.
Owner job dispatch checks the same first-cause latch under that mutex; accepted
execution begin also rejects a stop that won the acceptance order.

Stop changes OPEN to STOP_REQUESTED and immediately rejects ordinary work.
Owner interrupt/boundary checks or a subsequent pump confirm STOPPED/DONE,
including an idle unresolved entry. Already claimed work exits cooperatively;
unclaimed events and jobs are never retried or drained. Close releases their
references and raw POD without scheduling user cleanup/finally JavaScript.
Stop/deadline/dispatch/actual allocation refusal retain whichever cause was
recorded first. Repeated stop on STOP_REQUESTED/STOPPED/FAILED/CLOSED is a
successful no-op. Owner lifecycle reads are synchronized with any-thread stop.

The existing SDK producer/catalog packages `sdk/host-stop.c`, compiles it using
only the supplied public header and static SDK, and adds four Host cases. No
new test framework, engine patch, OS waker, cancellation or recoverable interrupt
was added. The engine allocator's unpublished-factory rollback remains intact.

## Public C Host observations

- `--stop`: empty and saturated queues accept stop independently of capacity;
  ordinary posts are refused immediately, with invalid/oversize POD checked
  before lifecycle. The public snapshot observes STOP_REQUESTED before owner
  confirmation. A short diagnostic retains reason 23 and truncation facts.
  Queued events and runnable/suspended jobs remain unexecuted through close.
- `--stop-running`: synchronous and async loops arm a worker from JavaScript.
  With only owner and worker present, the worker subtracts its own thread CPU
  from process CPU and observes at least 20 ms of measured owner CPU before
  stop. The loop after arming allocates nothing and calls no native functions.
  Polling sleeps only yield; elapsed wall time does not determine race order.
  Clock-call failure or insufficient measured progress fails the case. A
  controlled C callback and an idle PENDING entry separately prove owner
  confirmation. The callback permits setter/snapshot/repeated stop while owner
  pump/close remain BUSY.
- `--stop-ordering`: the owner blocks inside an already claimed event's C
  callback. The worker fills both remaining slots, observes QUEUE_FULL and
  accepts stop before releasing the owner. Public facts prove three accepted,
  one consumed and two unclaimed events. Close drops exactly two; consumed and
  dispatch counters do not increase, no native mark occurs and no retry runs.
- `--stop-causes`: the worker accepts stop during a held callback, then the owner
  returns after the nominal deadline has elapsed; stop remains first. The
  inverse waits for a public FAILED/deadline snapshot before repeated stop.
  Actual dispatch exhaustion and caught allocation refusal survive later stop.
  Stop followed by a genuine setter heap-allocation refusal remains STOPPED
  with reason 23. Snapshots/results prove repeated FAILED/CLOSED stop has no
  state or resource-counter side effect.

All stop cases use stable product returns, phases, snapshots and native side
effects. A supervisor timeout fails execution and is never a stop/deadline pass.

## Red/green and Windows diagnosis

Original outputs are retained under this worktree's `out/m1-10/`.
`red-capability` links the ticket 08 supplied SDK and actually fails the missing
public bit-14 capability assertion; `red-original.c` preserves its original
test. `green-capability-retry`, `idle-queue-first`,
`running-ordering-fixture-fixed`, `causes-first` and `stop-complete` retain real
passing vertical slices. `green-capability` preserves a failed local iteration
command selecting an older include path. `running-ordering-first` preserves the
fixture's unsupported module-namespace result; the corrected fixture fulfills
with undefined. Neither setup failure is relabeled as product evidence.

The first audited f0b source `5c95086817dc8304f36f1a808d46b9c580a846c1`
and artifact remain immutable under `final/`. Ubuntu passed all 33 cases and the
parent's main `out/m1/10-guest-final/run.json` passed all 33 on ARM64 64K.
`final/windows-user-run.json` honestly retains 32/33 pass: `stop-running`
returned raw 256, without a timeout. No SDK defect was inferred from that result.

The diagnosing-bugs loop is retained in `debug/README.md` and original raw
reports. The supplied Windows reproduction failed 2/3 times. Minimizing to the
synchronous CPU-loop variant failed 10/10. Ranked hypotheses were shown before
tagged instrumentation: zero initial CPU mistaken for clock failure, unexpected
termination reason, or extra user work. Tagged debug-only runs showed nine
failures with valid initial CPU 0, later owner CPU 31,250,000 ns, marks 0 and
reason 23/STOP_REQUESTED. Clock calls succeeded; native CPU readings advanced in
15,625,000 ns increments. The fixture incorrectly conflated valid zero CPU time
with a failed clock call.

Commit `779fab4` separates clock-call success from its numeric result while
preserving the measured-progress and product-stop assertions. The minimized
Windows regression passes 20/20 and the original four-variant scenario passes
10/10, raw 0, empty stderr and no timeout. Tagged sources/artifacts remain only
in the explicitly marked `out/m1-10/debug/`; no debug tags ship in product or
Host source. This diagnosis changed only the public Host fixture.

After ticket 05 actually joined integration, it was merged as `dfd0c7a`, with
both policy/stop inventories, Host objects and capability bits retained. A
fresh final producer and all affected acceptance were rerun on the combined
source and new input. The earlier f0b receipts remain historical evidence.

## Final exact identities

Final producer: `out/m1-10/combined-final/build.json` (report UUID
`4ed60e8d-dc4d-45e2-9dc9-f91187e11df4`). READY pointer is
`out/m1-10/combined-final/READY.json`; artifacts are under its `artifacts/`.

| Identity | SHA-256 |
| --- | --- |
| Producer report | `6555eb1194198d37e24a468a0d58c8addfc60639f32f3b09f65482139e203eee` |
| Pinned input_id | `f07f23c64bfcfb4a92722723e3e673213534a6fa81e690c401bd9fa10e0fd178` |
| Raw pinned v3 lock | `eb0c7dcdb401ca3739f9e3ce863604e486b526d534579417d17b3e05b9da32f9` |
| SDK recipe | `de2d70651db5b3e5179b75471af0e1cc7bd40c305514d62ab007aa2a5a977fb3` |
| SDK aggregate | `b5c81446404a2f7bac48e2fe3d46741abc30d4f8f758ccde437a24fce7dca953` |
| x86_64 static archive | `4b12821610e4e8567660ad7ce9ef0e6e63031a311e0db6bd0081b3b17872dcae` |
| ARM64 static archive | `fec86326caa146c811eafeb01cf06ee062f6b73d2d01d4251b1528b07507eee3` |
| C Host fat APE | `b889c6bea984189363029fc26e5e16983d739bfc633d4cfb02ddde9ec6e46bc0` |

The engine/source/patch/toolchain and helper identities are individually bound
in the original producer report. Both archive components, including
`sdk/lib/.aarch64/libqaplet.a`, are packaged and independently checked by native
consumers. Source remained frozen from producer through matching acceptance.
Linux Git truthfully reports `working_tree_dirty=true` for Windows-created
Markdown line endings; raw `git-status` logs preserve that observation. Exact
C/header/Python/module recipe inventories are verified before and after build.

## Matching executed consumers

`combined-final/run.json` passes 38/38 on Ubuntu x64 WSL2, kernel
6.6.87.2-microsoft-standard-WSL2, 4096-byte pages. All expected stdout, empty
stderr, raw exit 0 and no timeout. Existing 1,000-cycle lifetime, values,
Promises, modules/policy, Host modules, budgets and event regressions also pass.

The parent executed normal-user Windows x64 native PE; original report is
`combined-final/windows-user-run.json`: all 37 required cases pass, including
all four stop groups. Kernel is 10.0.22000 and pages are 4096. The optional
`module-symlinks` fixture remains explicitly blocked/unavailable, with raw
exit 19712 and actual privilege facts retained; no Windows symlink pass is
claimed. All required cases have raw exit 0 and no timeout. SDK, recipe and
APE identities match the table.

The parent's main checkout `out/m1/10-guest-combined-final/` contains
`selection.json`, `payload.json` and `run.json`: all 38/38 pass on actual
full-system Linux aarch64, kernel 6.8.0-147-generic-64k, measured pages 65536
and smaps 64 KiB. QEMU 6.2.0 uses virt-6.2/gic-v3/cortex-a72,
TCG single CPU, 512 MiB. Selection binds the same producer, input, SDK and
recipe; payload manifest SHA-256 is
`e12f23d8ae0847f075fa47f2cd428fc21ed2308a6f4d677e48e6affb103ebebc`.
Before/after payload hashes, raw serial/per-command logs, expected/actual
stdout/stderr/exit, deadlines and clean shutdown are retained. No timeout or
skip is counted as success. This is emulated kernel coverage, not native ARM64
Cross or a physical-performance claim.

`combined-check.json` passes `dev.py check`: 219 tests with 75 existing optional
scope skips plus a real system C bootstrap. Initial `final-check.json` remains
the separate f0b check. Reusable inputs/build intermediates stay in the main
repository physical `.cache/`; final and raw evidence stays in `out/`.

## Replay and scope

```sh
python3 scripts/sdk.py build --output-dir out/m1-10/combined-final/artifacts --report out/m1-10/combined-final/build.json
python3 scripts/sdk.py run --build-report out/m1-10/combined-final/build.json --artifact out/m1-10/combined-final/artifacts/qaplet-host.com --loader-dir /mnt/d/Workspace/dairn/qaplet/.cache/toolchains/cosmocc/official-assimilate-linux-x86_64-v1/f2f984d3a9790674fc0da11c1c00a32d4fe3f8dafde99af74b0c6dc24a2a16cf/tree/bin --report out/m1-10/combined-final/run.json
python3 scripts/dev.py check --report out/m1-10/combined-check.json
```

Build replay needs a new/empty output directory; originals must be preserved.
The Windows supplied consumer omits the loader directory and never compiles.
Actual commands, intervals, raw logs and host facts are authoritative in the
original reports. Final integration merge recheck found `9174bfad` already
included; unrelated tickets 09/12 were not waited on or relabeled as tested.
Complete native macOS/native ARM64 Cross, complete M1 Full/strict aggregation,
ticket 11's comprehensive lifetime contract and release readiness remain
unexecuted by this increment. M2 OS waking/cancellation/recovery stays excluded.
