# 03 · Promise / explicit pump validation

2026-11-02: ticket 03 source implemented on `codex/m1-03`, commit
`0a95766526e65f5cfe1e3920a3d6cadae03eeedc`. It is based on task 01 integration
`e07c2277fa2e5fa29923e7ef29aa970d8538ede8`; merging the current integration tip
before final validation reported already up to date. No unified lock, engine
source, overlay, toolchain or frozen M0 recipe changed.

## Public behavior

`qaplet_eval` recognizes genuine Promises through the pinned engine's Promise
state API without reading `then`, `constructor` or `Symbol.species`. Runtime
execution references and native-budgeted rejection records are owned by
`src/execution.c`. `qaplet_pump` executes at most 256 jobs per call. Empty jobs
with an unsettled entry remain PENDING without progress; a new eval is BUSY.
The future `run_entry` API is not advertised or stubbed by this increment.

Unhandled rejections are judged only after the full runnable jobs checkpoint.
Late handlers within that checkpoint remove records, including across pump
slices. Entry failure is reported once, and its tracker record is released.
Ordinary exceptions settle remaining jobs, preserve state, and permit a new
execution. Pending close releases both Promise and rejection references without
running remaining application jobs. Promise/jobs capability is bit 7; bit 6 is
reserved for concurrent ticket 02's value bridge.

## Executed identities

| Identity | SHA-256 / value |
| --- | --- |
| Pinned unified input_id | `f0b8dbcf6e44b58248e45bd755bbce5414b8715d3895caf5318efb31b48e5ead` |
| SDK recipe | `7e26259b6de6328c384115c00aff069807e4620319d828abb10130a108ac193d` |
| SDK components aggregate | `0f3e5738bb854d35f72ab4158d8eebee912100f12a3e80daa6322179f1990da1` |
| qaplet-host.com | `e3b2ca1efcfbc5154200513e90b1dc2ce963604540c67f35564398b04457663f` |

Retained producer: worktree `out/m1-03/verified/build.json`, with distributed
SDK and Host in `out/m1-03/verified/artifacts/`. The producer records the exact
source/recipe inventory, both CPU outputs, static archive components, APE audit,
before/after input verification, raw commands and logs. Inputs and build
intermediates use the physical main checkout `.cache`. Ubuntu's existing CRLF
Markdown view is honestly recorded as `working_tree_dirty=true`; exact SDK
recipe identities remain independently bound.

## Actual validation

Ubuntu x64 WSL2, 4096-byte pages: `out/m1-03/verified/run.json` passed all nine
public C Host cases, including the previous task 01 cases and new Promise case.
The latter observes a 600-job chain across three pumps, completion exactly at
256 jobs, a handler installed after the first pump, orphan/entry rejection,
sync/job failure side effects and reuse, no getters for thenables/Promise
proxies or constructor inspection, no-progress PENDING, foreign-thread pump
refusal, and zero engine bytes/native record reclamation after pending close.

`out/m1-03/verified/check.json`: repository `dev.py check` passed on Ubuntu,
including the real system C bootstrap. Its ordinary suite ran 219 tests with
75 pre-existing opt-in evidence skips; those skips are not M1 product passes.

TDD artifacts and original raw failures remain under `out/m1-03/`: `red-01`
contains the full audited producer and consumer where the actual Promise was
rejected as VALUE_TYPE_UNSUPPORTED; `green-01` verifies genuine-Promise pending
ownership. `red-02`/`green-02` add explicit bounded pumping/BUSY.
`red-03`/`green-03-fixed` add checkpoint rejection tracking. These later cycles
use the public C Host with the original verified engine archive via ignored
`out/m1-03/tdd.py`, preserving compile/run command results; they do not claim a
new audited distribution. The final audited producer/consumer above covers the
entire committed source. A diagnostic intermediate identified the pinned
Promise-state enum's non-Promise sentinel requiring an explicit signed cast;
the corrected public regression includes scalar/thenable/proxy outcomes.

The parent received the final READY pointer before ticket resolution, then
executed all nine cases with the same supplied bytes on both additional targets:

| Target | Retained report |
| --- | --- |
| Windows x64, normal user native PE, 4096-byte pages | Worktree `out/m1-03/verified/windows-user-run.json` |
| ARM64 full-system QEMU, 65536-byte guest pages | Main checkout `out/m1/03-guest-verified/run.json` |

Both reports passed all nine cases. The ARM64 report's sibling `selection.json`
and payload bind the exact producer, SDK and recipe; the guest records actual
CPU/page facts separately from its outer Ubuntu x64 supervisor. This is
emulated-system evidence, not physical ARM64 performance evidence. The previous
task 01's ARM64 4K result keeps its original scope; it is not relabeled as a new
task 03 run. No required ticket 03 case skipped. This increment does not certify
the complete M1 Cross/Full matrix, future modules/events/budgets, macOS or
release readiness.
