# 08 · Bounded POD event validation

2026-11-02. Source commit f07ed1ad42375a2cefe8f1181108844318e26bca on
codex/m1-08, after merging current integration 33ca5439 (already up to date).
Source remained frozen through matching Windows and ARM64 consumer runs. Only
this validation record and the ticket resolution were added afterward.

## Implemented boundary

Any-thread qaplet_post_event copies uint32 type, uint64 token (including zero)
and explicit-length payload bytes under the Runtime mutex into preallocated,
fixed-width inline POD slots. Defaults are 256 queued slots and 1024 bytes per
payload, with configurable finite limits and overflow guards. One extra slot
is preallocated for owner scratch. Queue storage counts toward native storage
from initialization. qaplet:events reserves one module identity from creation.
Only engine intrinsics and SDK-owned storage exist before factory publication.

onEvent(fn) permits one owner-JS registration, separately from the locked C
Host declaration window. NO_HANDLER=20, QUEUE_FULL=21 and PAYLOAD_TOO_LARGE=22
are stable additions; previous numeric codes remain unchanged. Capability bit12
advertises events. Posts validate input before lifecycle/handler/capacity and
never partially accept or overwrite an event. Each owner delivery creates an
independent Uint8Array, exact BigInt token and Number type. Returns (including
actual Promises and thenables) are ignored. Runnable jobs and unhandled
rejections still settle at complete checkpoints. Ordinary event failures allow
reuse after jobs settle and never retry the consumed event.

Pump limits are 256 jobs and 32 event callbacks. Jobs settle before the next
event claim. Active top-level executions share their ledger; otherwise one
event and its runnable jobs get an independent ledger retained across pumps.
Allocation/deadline/dispatch failures retain the shared first cause. Close
shuts acceptance under the same mutex, drops raw POD, frees queue/handler and
engine references without scheduling JS. Host producers must still withdraw
and join before destroy. No stop implementation, hidden thread or M2 waker is
provided by this ticket.

## Preserved red/green paths (under this worktree's out/m1-08/)

- red-registration/build.json: successful initial audited producer;
  contract-run.json observes the absent builtin as a returned failing Host test.
  Earlier run.json refused an old loader bundle identity and actual-run.json
  recorded a missing explicit loader; neither is counted as product execution.
- green-registration/result.json: once-only builtin registration succeeds.
- red-copy/result.json plus evidence/link.stderr.log: missing public event API
  produces a real link failure. green-copy/result.json executes copied delivery.
- queue-first/result.json: event queue passes; existing snapshot baseline
  assertions fail because initialization now reserves storage/module identity.
- scheduling-first/result.json: adjusted public baselines and jobs/rejection
  behavior pass. threads-first/result.json passes controlled worker barriers.
- budgets-entry-first/result.json passes idle/shared budget and real ESM/TLA.
- red-no-progress/result.json: quota refusal incorrectly reported progress.
  green-no-progress/result.json verifies no claim/no progress and all event cases.
- red-callback-event-code/result.json: a real callback post refusal was clamped
  to HOST_ERROR. green-callback-event-code/result.json retains code22 and reruns
  event/Host cases, including an external native counter untouched by close.

Iteration records replay existing bounded build_ape.run commands against the
public header/archive/Host; they do not certify an audited SDK distribution.
Original iteration files, output artifacts and raw logs remain retained. Final
distribution uses the independent scripts/sdk.py producer and consumer below.

## Audited Ubuntu source and identities

| Identity | Value |
| --- | --- |
| Source commit | f07ed1ad42375a2cefe8f1181108844318e26bca |
| Pinned input_id | f0b8dbcf6e44b58248e45bd755bbce5414b8715d3895caf5318efb31b48e5ead |
| Raw lock SHA-256 | 861ae65bbbb87743091103e1e5707dfe2feb422fa9b35ca9652ddc80610e2c0e |
| SDK recipe | c362f9833fae26c0a2f9110d5711ec61b7e08dca8ccf77bab0c7c5cfdebfad70 |
| SDK aggregate | f901bee5559e3b425e8ef8a352b3e2f9861fcf8c0ddac6815b0fc205af4a8967 |
| C Host APE | 81f54a587acc516d8f3cf60e6089d7bc68f2ebf33643da7b90b157487d843c81 |

out/m1-08/final/build.json passes both archive components, fat APE/PE/64K
layout audits and before/after supplier/source/recipe rechecks. Artifacts are
under final/artifacts/. Public Host source host-events.c and actual source
fixture sdk/modules/events-entry.mjs are packaged and individually identified.
Physical reusable inputs/intermediates use the main checkout .cache. Linux Git
truthfully reports working_tree_dirty=true from Windows Markdown line endings;
exact recipe inventories identify C/header/Python/module bytes.

The committed-source repository check final-check.json passed 219 tests with
75 recorded skips (existing optional evidence/system scope), plus the real
system-C bootstrap. This is not a full M1 Cross/Full gate.

Ubuntu x64 WSL2, kernel 6.6.87.2-microsoft-standard-WSL2, 4096-byte pages:
final/run.json passes 29/29 cases, all expected stdout, empty stderr and exit0,
with no supervisor timeout. Six new event cases cover copies/exact values,
atomic refusal/saturation/wrap/raw close drop, 32-event pumps, jobs-first slices,
ignored return Promises/thenable getters, delayed handled rejection, unhandled
returned rejection, throwing callback jobs/reuse, controlled two-producer FIFO,
owner/C-callback/worker barriers, independent/shared cumulative ledgers,
host-wait deadline, caught allocation first cause, factory storage failure,
and actual source ESM static builtin/TLA waiting for a worker event. Existing
SDK lifecycle/value/Promise/module/budget/Host regressions all pass, including
1,000 create/close/destroy cycles.

## Matching supplied native consumers

The parent performed normal-user Windows x64 execution, recorded in this
worktree's out/m1-08/final/windows-user-run.json: pass 29/29, Windows kernel
10.0.22000, native x86_64, 4096-byte pages. Its SDK aggregate, recipe and APE
hashes match the table above. The consumer executed supplied PE bytes and did
not compile. Every case has expected stdout, empty stderr and exit0; no timeout.

The main checkout out/m1/08-guest-final/{selection.json,payload.json,run.json}
binds the same producer/SDK/input/recipe and packaged source fixtures to the
actual ARM64 full-system execution: 29/29 pass. The guest is Linux aarch64,
kernel 6.8.0-147-generic-64k, measured page size 65536 with smaps 64 KiB, QEMU
6.2.0, virt-6.2/gic-v3/cortex-a72, TCG single CPU. The producer report selected
has SHA-256 c02a03e80ac8f16242daee91f6ce3850288daf150d439e4e9998eb4e1ba06b71;
payload manifest SHA-256 is
c8b99dc04089e6ebb414cc12e5aa7cff98e863660fa0a5339ddee0c5e927c9a6.
The runner retains per-command raw stdout/stderr, expected/actual output,
exit/time/deadline and before/after payload hashes, plus guest kernel/rootfs,
QEMU and serial identities. The C facts case independently prints aarch64 and
65536. No supervisor timeout or skip is counted as product success.

## Replay and scope

The final producer and Ubuntu consumer used:

```sh
python3 scripts/sdk.py build --output-dir out/m1-08/final/artifacts --report out/m1-08/final/build.json
python3 scripts/sdk.py run --build-report out/m1-08/final/build.json --artifact out/m1-08/final/artifacts/qaplet-host.com --loader-dir /mnt/d/Workspace/dairn/qaplet/.cache/toolchains/cosmocc/official-assimilate-linux-x86_64-v1/f2f984d3a9790674fc0da11c1c00a32d4fe3f8dafde99af74b0c6dc24a2a16cf/tree/bin --report out/m1-08/final/run.json
python3 scripts/dev.py check --output-dir out/m1-08/final-check --report out/m1-08/final-check.json
```

The supplied Windows SDK consumer uses python scripts/sdk.py run with the same
producer/artifact and no loader directory; its native PE command records are
authoritative. ARM64 runs the commands/files explicitly selected by payload.json.
All actual cwd, command arguments, process intervals, raw logs and timeouts are
recorded in the original reports rather than inferred from these examples.

After matching consumer completion, merging current integration 33ca5439 again
reported already up to date. Independent ticket 05's new engine/input work was
not merged at that point and is not relabeled as tested by these f0b reports.
All five ticket acceptance criteria have actual public-boundary coverage. Full
native Cross, complete M1 Full, release readiness, cooperative stop, concurrency
lifetime completion and M2 remain outside this increment's executed claim.
