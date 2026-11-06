# 04 · Real file ESM / explicit module pump validation

2026-11-02: standalone ticket 04 source was committed as
`ced0ce80a8b36e07091eeb01212589edbc010511` on `codex/m1-04`.
After its actual supplied-host checks, ticket 07 integration tip
`6aea27022b4a2c28453c03deaf2397d0606b9cab` was merged as `d945e37`.
The combined module/budget source is committed as
`2f15dd6aa90e7466bf89e1a6b81c3fba064c0da8`; its final merge
check reported already up to date. No engine source, overlay, supplier lock, toolchain or
frozen M0 recipe changed.

## Public behavior

`qaplet_run_entry` reads real ESM files, retains canonical realpath file URL
identities and source bytes, and evaluates retained compiled module values.
Static imports, dynamic imports and completing TLA share the existing explicit
Promise/job pump. Unresolved TLA stays PENDING without progress, rejects a
second execution as BUSY, and closes without running remaining user work.
Module runs report undefined; an exported `then` is never invoked by the Host
to assimilate a namespace. A function-based two-module cycle is demonstrated;
this does not claim every cyclic async graph completes.

One registry serves explicit roots and imports. Modules are reserved before
recursive compile-only resolution, so cycles see coherent host identity.
`import.meta.url/main` are initialized once before evaluation. A dependency
later explicitly run keeps `main=false` and its original side effects. Entry
paths use the creation cwd after the process changes cwd. Script dynamic import
uses the explicit absolute file base passed to `qaplet_eval_with_base`; ordinary
eval has no relative base. Script eval remains fresh rather than entering the
file-module registry.

The file-module capability is bit 8. Finite defaults are 256 module identities,
4 MiB per source and 32 MiB retained source bytes. Snapshot counters match
actual delivered fixture lengths and do not increase on repeat cache hits.
Discoverable root source-size refusals preserve the previous result. Close
releases module source/registry/engine references; immutable cwd configuration
remains part of the CLOSED handle's native baseline until destroy. The supplied
SDK includes every actual `sdk/modules` fixture in its component inventory.

## Standalone producer retained unchanged

| Identity | SHA-256 / value |
| --- | --- |
| Unified input_id | `f0b8dbcf6e44b58248e45bd755bbce5414b8715d3895caf5318efb31b48e5ead` |
| SDK recipe | `5888dc6371b0a9e1e5e7206c655efe68799709e6519e65a4195a1cd0bf2c5192` |
| SDK components aggregate | `c7968682499566d4a64d4417d06756895e8cb220ad79549ab77627bd6078449c` |
| qaplet-host.com | `31e31da2fe16942194a3b67113c03eec70fe008952634239f7e05b61a2260413` |

Retained in this worktree: `out/m1-04/verified/build.json`, with SDK and Host in
`out/m1-04/verified/artifacts/`. The producer records both CPU outputs, archive
components, the source/recipe inventory, APE audit, before/after verified inputs
and raw commands/logs. Reusable inputs/builds use the physical main `.cache`.
The existing Linux view of Windows Markdown line endings remains honestly
recorded as dirty; exact recipe bytes independently identify the compiled code.

## Executed validation

Ubuntu x64 WSL2, 4096-byte pages: `out/m1-04/verified/run.json` passes all 20
public C Host cases. The five module cases observe real static files, TLA and
dynamic imports, the supported cycle, namespace `then`, fixed creation cwd,
first metadata, canonical and explicit-file-URL reuse, precise source-cache
bytes, relative Script import with/without an explicit base, source/count/cache
limits, unresolved TLA, BUSY, and pending close. Previous value, Promise,
lifetime, owner and 1,000-cycle checks also pass against this exact SDK.

`out/m1-04/verified/check.json` passes the repository `dev.py check`, including
the actual system C bootstrap. The ordinary suite ran 219 tests with 75 existing
opt-in evidence skips; those skips are not module product passes.

Raw TDD artifacts remain under `out/m1-04/`. `red-static/run-pinned.json` shows
the public file entry rejected as UNSUPPORTED. `green-static-fixed/run.json`
passes the initial static graph. `red-limits/run.json` passes all async/identity
cases and exposes source-refusal result loss and premature cwd-baseline release;
the final producer/consumer above verifies both fixes. Earlier loader selection
and compilation failures remain unmodified with their original scope.

The standalone producer READY pointer was sent to the parent before resolution.
The same 20 cases passed normal-user Windows x64 native PE/4096-byte execution
in worktree `out/m1-04/verified/windows-user-run.json` and full-system emulated
ARM64/65536-byte execution in main checkout `out/m1/04-guest-verified/run.json`.
The latter's sibling selection/payload bind the exact SDK, APE and delivered
source files. They remain standalone module proof and are not relabeled as the
later combined module/budget source.

## Combined module / budget integration

Both file runs and Script evaluation with an explicit import base now call the
shared execution begin/checkpoint path. File compilation/load does not reset the
deadline, and a check between compile and evaluation avoids starting a module
body after a deadline already expired. Module native allocation refusals use
the shared first-cause latch and cannot be mislabeled as later resolution errors.

`red-ledger/run.json` (under `out/m1-04/`) retains an audited combined producer
whose public async/module-budget tests fail before the shared module begin hook.
The new real-source case exercises a 600-job TLA chain over two pumps with a
cumulative quota of 300, pending TLA across a host wait, an infinite module body
interrupted by its deadline, and SDK native-storage refusal during resolution.
Every budget failure is verified as permanent, with its stable result/reason
and normal close. The combined final producer is `out/m1-04/combined/build.json`.

| Combined identity | SHA-256 |
| --- | --- |
| SDK recipe | `3193304ebcdf47ae2ac847a18e3fba7c61aea19c00cf0fc4c00457e9fb479f9c` |
| SDK components aggregate | `e237281d53604280b2a2d56eec99877aba89c48204296124beaa0be8979e8baa` |
| qaplet-host.com | `0f8896abb1ff10187a49531c9b0de29f15239c305843c699c71586368875d3c5` |

`out/m1-04/combined/run.json` passes all 22 public C Host cases on Ubuntu x64
WSL2/4096-byte pages, including the unchanged ticket 07 budget regression and
the new real file ESM budget case. `out/m1-04/combined/check.json` passes the
repository check again. The combined READY pointer was sent to the parent
before resolution for Windows and ARM64 64K supplied-byte execution.

Both supplied-byte executions pass all 22 cases with the exact combined
identities above:

| Target | Retained report |
| --- | --- |
| Windows x64, normal-user native PE, 4096-byte pages | Worktree `out/m1-04/combined/windows-user-run.json` |
| Full-system emulated ARM64, 65536-byte guest pages | Main checkout `out/m1/04-guest-combined/run.json` |

The guest's sibling `selection.json` and payload bind the exact producer, SDK,
APE, recipe/input identity and every actual source fixture. Guest CPU/page facts
are separate from the outer x64 supervisor. No required ticket 04 case skipped;
supervisor timeout is false. The Ubuntu real ESM budget process completed in
about 0.192 seconds against its 30-second supervisor deadline, independently
checking product deadline/result codes rather than treating process timeout as
success. Final integration tip remained `6aea270`; its merge check was already
up to date before ticket resolution.

Extended failure/path-refusal coverage, the narrow
module-name NUL engine overlay, Host modules, CLI, events, and the full M1
Cross/Full matrix belong to their own tickets. This record makes no macOS,
physical ARM64, complete M1 or release-readiness claim.
