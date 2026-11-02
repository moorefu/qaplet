# 07 · Execution budget validation

2026-11-02: ticket 07 is implemented on `codex/m1-07`. The feature commits are
`1d85866` (monotonic deadline) and `ff516a2` (cumulative budget and execution
options). Final producer source is `8fc46d722df573f156cfbd693cac34f2156db2fa`,
after merging the current integration tip `00355b6`. This includes ticket 02's
strict values/diagnostics and ticket 03's actual-Promise pump. No input lock,
engine overlay, supplier version, toolchain or frozen M0 recipe changed.

## Public behavior

`qaplet_config.limits` supplies finite instance ceilings: zero selects 30,000 ms
and 100,000 dispatches. `qaplet_eval_with_options` accepts a versioned, borrowed
options structure; NULL/zero inherits the instance ceiling, and nonzero values
may only tighten it. ABI, flags, reserved/tail bytes, caps and time arithmetic
are checked before accepting an execution. Rejected calls preserve its previous
result. `qaplet_eval` remains the default-options convenience call.

An accepted execution owns one absolute CLOCK_MONOTONIC deadline and one
cumulative dispatch count. Returning PENDING and repeated pumps preserve both.
The engine interrupt checks the independent first-cause latch, as do owner
boundaries and dispatch claims. Deadline and dispatch termination are stable
codes 17 `DEADLINE_EXCEEDED` and 18 `DISPATCH_LIMIT`; the Runtime is permanently
FAILED even when async machinery converts an interrupt into a rejection. The
allocator and native value/rejection storage use the same first-cause latch.
Ordinary JS errors still settle runnable jobs within that same budget and allow
reuse if no permanent failure occurs. Exact-quota completion succeeds when no
further job is ready. Query/get_result/close preserve the terminal reason;
new eval and pump are rejected after FAILED. Capability bit 9 advertises budgets.

The internal `qaplet_execution_begin(runtime, options)`,
`qaplet_execution_check(runtime)` and `qaplet_execution_dispatch(runtime)` hooks
are shared C execution seams for the subsequent module/event integration.
Only already provided Script/Promise/jobs APIs are claimed by this increment.
No event API or module-success stub was added. Native calls, parser/compiler,
GC and cleanup retain cooperative boundaries rather than hard preemption.

## Executed identities

| Identity | SHA-256 / value |
| --- | --- |
| Final source | `8fc46d722df573f156cfbd693cac34f2156db2fa` |
| Pinned unified input_id | `f0b8dbcf6e44b58248e45bd755bbce5414b8715d3895caf5318efb31b48e5ead` |
| SDK recipe | `ed68530d6187cec45bf692a18e95d3c081bf3c308289e6febd6218f9887c3379` |
| SDK components aggregate | `aa546f7707295740103e66eec1672614d5466fc6faa34d355760f6744d1eb1ee` |
| qaplet-host.com | `a37d71d519335a072db5bc9fc3d3ac55ee677a2b2666bbccd0370111d1b4cde4` |

The final Ubuntu audited producer is worktree `out/m1-07/final/build.json` and
its supplied SDK/APE is `out/m1-07/final/artifacts/`. It records both CPU archive
and executable components, APE/PE audits, effective recipes, actual commands,
raw stdout/stderr, and supplier verification before and after the build. All
reusable inputs/intermediates resolve to the physical main checkout `.cache`.
Linux's existing Markdown line-ending view is truthfully retained as
`working_tree_dirty=true`; source/SDK recipe inventories bind exact bytes.

## Actual validation

Ubuntu x64 WSL2, 4096-byte pages: final `out/m1-07/final/run.json` passes all
16 cases in the existing `scripts/sdk.py` catalog. The public-header-only Host
is compiled against the distributed header and supplied static archive. Its
budget case observes:

- Default queried ceilings and an actual infinite Promise stream stopped at
  100,000 cumulative jobs, across hundreds of pumps.
- A 300-job ceiling after a first 256-job pump; count 300 and permanent failure
  after the next pump. New accepted executions get fresh ledgers; exact-quota
  3-job executions succeed repeatedly.
- A finite-loop red followed by true no-exit Script and async/job loops stopped
  by the engine, including catch/rejection paths.
- An unsettled actual Promise, a no-progress pump, an owner-controlled
  CLOCK_MONOTONIC wait exceeding its 200 ms ceiling, then a no-progress terminal
  pump. Host time and the prior pump did not reset the deadline.
- Zero/inherited options, smaller deadline/dispatch quotas, widening refusal,
  ABI/short/zero-tail/nonzero-tail/flags/reserved refusals, arithmetic overflow
  refusal, and previous-result retention after rejected calls.
- Caught allocation refusal followed by a no-exit loop still ending with the
  original MEMORY_LIMIT; ordinary JS failure followed by an infinite jobs
  stream ending with DISPATCH_LIMIT; result/diagnostic code agreement,
  permanent FAILED, repeated close, zero engine bytes and retained first cause.

The complete budget case takes about 0.4 seconds on this Ubuntu host. No
supervisor expired; every product negative returns its own stable code/phase.
The other 15 cases re-exercise strict values, safe diagnostics, Promise
checkpoints, ordinary-error reuse, owner rules, resource refusal and lifetime.

`out/m1-07/final/check.json` passes the Ubuntu `scripts/dev.py check` entry,
including the real system C bootstrap. Its ordinary contract suite ran 219
tests with 75 pre-existing opt-in evidence skips. Those skips are not claimed
as product or full M1 matrix passes.

TDD evidence is retained under worktree `out/m1-07/`:

- `red-defaults/build.json` is a real producer failure at public Host compile
  because execution limit fields were absent; `green-defaults/build.json` and
  `run.json` pass the initial default-limit slice.
- `red-deadline` returns a public expected/actual mismatch before the interrupt
  implementation; `green-deadline` passes.
- `red-dispatch` returns an expected/actual mismatch at the second pump before
  cumulative accounting; `green-dispatch` passes.
- `red-options` fails public C compilation before the versioned option/API
  exists; `green-options` passes.
- `green-full-budgets` and `green-terminal-boundaries` pass the expanded actual
  infinite-loop, host-wait, first-cause and stable diagnostic checks.

These later cycles use the prior ticket 03 pattern: ignored one-off `tdd.py`,
the retained verified engine archive, and existing `foundation.command` for
raw process evidence. Every iteration saves the exact header/C input bytes,
archive/artifact digests, commands, deadlines, raw output and exit/timeout
facts. They do not claim an audited distribution; the final `sdk.py` producer
and consumer independently cover the complete committed recipe.

The final audited producer READY pointer and exact source/SDK/recipe/APE
identities were sent to the integration owner before ticket resolution.
The integration owner then ran the same supplied bytes on both additional
targets; all 16 cases passed on each, with no product supervisor timeout:

| Target | Retained report |
| --- | --- |
| Windows x64, normal-user native PE, 4096-byte pages | Worktree `out/m1-07/final/windows-user-run.json` |
| ARM64 full-system QEMU, 65536-byte guest pages | Main checkout `out/m1/07-guest-final/run.json` |

The ARM64 report's sibling `selection.json` and `payload.json` bind the exact
producer, SDK, recipe, pinned input and guest files. The guest independently
measures aarch64, kernel `6.8.0-147-generic-64k`, 65536-byte pages and unchanged
payload SHA-256 before/after execution. Its mode is `emulated-system`, separate
from the outer Ubuntu x64 supervisor. No required ticket 07 case skipped.
The complete Cross/Full matrix, macOS/native ARM64 and release readiness remain
outside this ticket's local execution claims.
