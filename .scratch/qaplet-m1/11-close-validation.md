# 11 · Complete close/destroy lifetime validation

2026-11-02. Final combined source is
`51e342f73a54e8dd295bc4bdf1c1a6890bfdb030` on `codex/m1-11`, containing
validated integration `e687b319f182689d81ca3ed3f9c33d8928c0d783` (tickets
09, 10 and 12). The later integration merge
`e4757401bf97a0907e2c1b559521ec0076349296` absorbs the documentation-only
`ef0c80a2e59de20bba5c33fc54abca212eb9aa26`; its delta changes no SDK recipe
file. Original combined source was `09958e03754d5aca1bc4196e8727d185caabab58`;
the final source adds only the diagnosed native FILE fixture correction.
Final source, SDK, CLI and C Host bytes stay frozen through supplied
platform validation.

## Implemented boundary and test-first finding

This increment distributes `sdk/host-close.c`, compiles it using only the
delivered public header and static SDK, and adds three required public C Host
cases to the existing producer/consumer catalog. It expands the public header
and SDK documentation with the full close/destroy preconditions and ownership
contract. It adds no native cleanup hook, synthetic failure injection or
parallel test framework.

The approved Q32 seams were retained. Each workload was added and actually
executed before moving to the next slice. The existing runtime mechanisms from
the blocking tickets already satisfy the exercised close behavior. Therefore
these are truthfully **pre-existing-green** tests, not evidence of a runtime
defect and subsequent runtime fix. No artificial red or new capability bit was
introduced. The runtime implementation and unpublished-factory rollback
boundary are unchanged by ticket 11; runtime/config changes in the combined
source belong to the validated ticket 12 merge.

The `close-states` case covers normal completion, ordinary JS exception,
OPEN/PENDING, STOP_REQUESTED/PENDING, STOPPED and permanent MEMORY_LIMIT
FAILED. A real Host callback verifies close and destroy reentry return BUSY.
Every non-CLOSED state rejects destroy with BAD_STATE. A malformed close
diagnostic returns ABI_MISMATCH without dropping the queue or closing the
instance; a subsequent valid close succeeds with its independent OK diagnostic.
Closed snapshots retain the execution first cause, show zero engine bytes,
empty queue/module/source cache, and preserve native control storage. Repeated
close does not change the retained native baseline or repeat event drops.

The Host keeps real Host-owned FILE resources and function userdata alive
through close returning, verifies their native side effects and closes them
itself before destroy. The corrected fixture creates an explicitly named
native temporary file using mkstemp/fdopen and removes it after fclose.
Queued events, runnable Promise jobs, finally callbacks
and FinalizationRegistry callbacks all have visible Host side effects if run;
close suppresses them. Source inspection confirms the existing order: close
first disables event acceptance under the shared mutex; releases event,
execution, registry and value references; calls normal Context/Runtime
finalization while copied Host declarations still exist; frees the Host
declarations; and only then publishes CLOSED. The fixture does not claim a
new public native-finalizer callback or privately inspect engine records.

The `close-concurrent` case uses real retained JS object graphs and a producer
thread. A barrier proves a full-queue post before close. Public snapshots
observe dropped events while lifecycle is still not CLOSED, proving actual
producer overlap with native teardown without sleep-based order guesses.
Posts return only the permitted full/closed refusals, CLOSED never exposes
engine bytes or queue/module/source storage, and each worker performs at least
100 CLOSED-handle calls. Close returns while the worker remains active. The
Host then withdraws it, joins all in-flight calls, and finally destroys the
handle. Stop can linearize before CLOSED and preserve EXECUTION_STOPPED, or
after CLOSED and remain a no-op. No old pointer is used after destroy.

The `close-competition` case configures an actual 512 KiB engine limit and
requests a 1 MiB copied byte return from a Host callback. The engine allocator
really refuses storage. Condition-variable handoffs force worker stop before
that refusal and, separately, refusal before worker stop. The public return,
progress, diagnostic and closed snapshot preserve EXECUTION_STOPPED or
MEMORY_LIMIT respectively. No diagnostic text classifier or private allocator
call decides the result.

Existing required cases retain 128 bounded engine factory configurations,
32 native factory configurations and recovery after failures, null unpublished
outputs, metered source/bridge/rejection/registration refusals, first-cause
stop/budget tests and 1,000 create/close/destroy cycles. Published runtimes use
normal engine finalization. Failed unpublished factories retain the explicitly
approved direct backing-block rollback, since no Host/user/native resource has
been published there.

Current `JS_FreeContext`, `JS_FreeRuntime` and native free APIs return no
recoverable cleanup failure. No synthetic cleanup error is claimed as tested.
The close call diagnostic is independent of the recorded execution first cause;
the documented rule remains that any future detectable cleanup error must use
that separate diagnostic without replacing the cause.

## Preserved incremental evidence

Paths below are under this worktree's `out/m1-11/` and retain raw commands,
stdout/stderr, timings, exits, producer identities and original artifacts.

- `states/build.json` and `states/run.json`: first lifetime slice, 44/44 pass.
- `concurrent/build.json` and `concurrent/run.json`: observed teardown overlap,
  45/45 pass, with no runtime implementation change.
- `competition/build.json` and `competition/run.json`: 45/46 pass. The new
  fixture initially imported its pressure module after queueing events; its
  setup pump legitimately delivered them before the competing call. The failed
  raw assertion is preserved as a fixture-ordering error, not a runtime red.
  Setup imports now complete before raw events are queued.
- `combined/build.json` and `combined/run.json`: committed combined source,
  corrected competition setup and integrated CLI/runtime snapshot cases;
  all 90/90 pass on Ubuntu. All expected stdout, empty stderr, raw exit 0 and
  no supervisor timeout. Each public process case has a 30-second deadline.
- `combined/check.json`: actual Ubuntu `dev.py check`, 219 tests with 75
  declared optional scope skips and real system-C bootstrap. Main physical
  cache is `/mnt/d/Workspace/dairn/qaplet/.cache`; artifacts and raw reports
  remain under this checkout's `out/`.
- `combined-final/build.json`, `combined-final/run.json` and
  `combined-final/check.json`: final corrected FILE fixture, all 90/90 Ubuntu
  process cases pass and the final local check passes. The exact original
  Windows consumer pattern then passes in `combined-final/windows-fix-run.json`;
  the parent independently repeats it in `combined-final/windows-user-run.json`.
  Both have 89/89 required pass, with only the established optional Windows
  file-symlink fixture explicitly blocked.

## Frozen final identities

Final producer report ID is `597c3add-d168-46ac-889d-4cfb95810407`;
`combined-final/READY.json` points to the original final report and artifacts. The complete
53-file distribution inventory, including both static archives and source
fixtures, is bound by the producer report and rechecked before/after consumers.

| Identity | SHA-256 |
| --- | --- |
| Producer report | `3cbedee072e721cacdafa556e11ff25c9f98df8589194e18f44d53a392fc5027` |
| Pinned input_id | `f07f23c64bfcfb4a92722723e3e673213534a6fa81e690c401bd9fa10e0fd178` |
| Raw pinned v3 lock | `eb0c7dcdb401ca3739f9e3ce863604e486b526d534579417d17b3e05b9da32f9` |
| SDK recipe | `27f21aaac7843f398618863424f593cc1723aa5c4388f1983b6d6c04b6e719ec` |
| SDK aggregate | `0329f0be8e4cd3bc745a43839ddee8969baa2777ca5c0891896cd67efed690ec` |
| x86_64 static archive | `36ebd78c828c10f010628acfc7592c5092dbb37ef45fb0fdd353027b096084e7` |
| ARM64 static archive | `7459256aa99a9a8212e72b5334afeb48167b35832cad10bee586ab773d6586d2` |
| C Host fat APE | `f7a9ffa684d4e48680253ad39c1ba027389ec5102c8853a08e72bf795d88c2b3` |
| CLI fat APE | `723d8cd5384bf84767a109b7db09fcab797038a7cef0daf678de335acb3d053c` |
| Ubuntu consumer report | `93c402d72438b06955786c508642dc15419bea9fe602972e35e1b023f67629c3` |

The pinned engine is QuickJS
`2833510432045f72915919cef680ba59e5153b16`; all source/patch/toolchain/helper
identities remain separately bound by the producer. Linux Git reports
`working_tree_dirty=true`, including this untracked validation draft and
Windows-created Markdown line endings; original git-status logs preserve the
truthful observation. Exact recipe/source bytes
are independently verified before and after the producer.

## Windows native FILE fixture diagnosis

The first normal-user Windows consumer, `combined/windows-user-run.json`,
failed all three new close cases at `host->resource = tmpfile()` before their
SDK workload. Raw exits were 256 and no supervisor timeout occurred. Other
required cases passed. The original full-system ARM64 64K run passed all
90 cases; it was allowed to finish with the original source frozen. Neither
result was substituted for a passing Windows result.

The diagnosing-bugs workflow established a fast actual reproduction:

```powershell
python scripts/sdk.py run --build-report out/m1-11/combined/build.json --artifact out/m1-11/combined/artifacts/qaplet-host.com --cli-artifact out/m1-11/combined/artifacts/qaplet.com --report out/m1-11/combined/windows-repro-run.json
```

This repeated the exact three setup failures in about 2.2 seconds. A minimized
single-case invocation with the same relative-backslash executable path also
failed at tmpfile; `windows-minimal-repro.json` records its exact argv and
raw result. Initial isolated absolute-path invocations passed three times in
about 9 ms each (`windows-fixture-repro.json`). Before further probes the
ranked falsifiable hypotheses were shown to the parent/user: any relative
startup path causes the failure; specifically relative backslash spelling
causes it; or an explicit native TMPDIR removes a temporary-path fallback.

`windows-path-comparison.json` changes one variable at a time with the same
frozen Ubuntu-built Host, working directory and subprocess capture boundary.
Relative-backslash paths failed 2/2; absolute-backslash paths passed 2/2;
relative-forward-slash paths passed 2/2; adding only TMPDIR=native TEMP to
relative-backslash invocations still failed 2/2. This supports the narrow
backslash-startup interaction and falsifies the other two broader hypotheses;
it does not claim a private libc root-cause implementation was inspected.

The fix uses the repository's existing module-policy native temporary-file
boundary: uname selects native Windows TEMP or POSIX /tmp; mkstemp/fdopen
creates a real Host-owned FILE; fclose/unlink observes actual native release.
It changes no Runtime mechanism, SDK interface, consumer runner, pinned libc
or engine input. The existing three public cases are the regression seam,
including the originally failing relative-backslash SDK consumer. No debug
instrumentation was added to product code; raw diagnostic probes remain in
clearly named ignored out files.

## Matching supplied consumers

Ubuntu x86_64 WSL2, kernel `6.6.87.2-microsoft-standard-WSL2`, measured pages
4096: 90/90 pass. The facts case independently reports x86_64/4096. Matching
normal-user Windows x86_64 native PE, kernel `10.0.22000`, pages 4096:
`combined-final/windows-user-run.json` passes 89/89 required cases. The optional
module-symlinks fixture remains blocked with native privilege facts and raw
exit 19712. Every required case has expected stdout, empty stderr, raw exit 0
and no timeout; SDK, recipe, CLI and Host bytes match the table. No consumer
compiled source.

The parent's main-checkout
`out/m1/11-guest-combined-final/{selection,payload,run}.json` binds the same
producer, input, SDK and recipe to actual full-system Linux aarch64 execution:
90/90 pass, including all three close cases and the independent facts case.
Kernel is `6.8.0-147-generic-64k`, measured page size 65536 and smaps 64 KiB;
execution mode is explicitly `emulated-system`. QEMU 6.2.0 uses
`virt-6.2,gic-version=3`, `cortex-a72`, single-CPU TCG and 512 MiB RAM. Payload
hashes stay unchanged before/after, serial/per-command logs retain expected
and actual stdout/stderr/exits and deadlines, and QEMU exits cleanly without
supervisor timeout. Payload-manifest SHA-256 is
`070334ae67badf9b4e76cb0dac128d29f72e925071e847d98aeb2f4d1ded2b9c`.
This is real emulated-kernel 64K coverage, not native ARM64 Cross or a physical
performance claim. The earlier original-package guest run remains separately
preserved under `out/m1/11-guest-combined/` and is not substituted for this
final-package result.

## Replay and scope

```sh
python3 scripts/sdk.py build --output-dir out/m1-11/combined-final/artifacts --report out/m1-11/combined-final/build.json
python3 scripts/sdk.py run --build-report out/m1-11/combined-final/build.json --artifact out/m1-11/combined-final/artifacts/qaplet-host.com --cli-artifact out/m1-11/combined-final/artifacts/qaplet.com --loader-dir /mnt/d/Workspace/dairn/qaplet/.cache/toolchains/cosmocc/official-assimilate-linux-x86_64-v1/f2f984d3a9790674fc0da11c1c00a32d4fe3f8dafde99af74b0c6dc24a2a16cf/tree/bin --report out/m1-11/combined-final/run.json
python3 scripts/dev.py check --report out/m1-11/combined-final/check.json
```

Fresh producer replay needs new/empty output and new report paths; preserve
these originals. Windows omits the loader directory and never compiles. These
results do not certify missing native macOS/Linux ARM64 Cross targets, a full
M1 aggregate/strict gate, offline SDK reproducibility or release readiness.
Emulated ARM64 kernel coverage is distinct from native Cross. M2 OS waking,
native-call cancellation, late completion and recovery stay outside this scope.
