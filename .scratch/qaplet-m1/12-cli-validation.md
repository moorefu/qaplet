# M1 ticket 12: shared Runtime CLI validation

2026-11-02. Implemented on `codex/m1-12`; final product source is
`41b9a89f4b46499d3a7f9209eacce93b84da4a41`, after merging current integration
`32d455e55522ba5b8fdfb3b600d013435e6fb6f0`. Source and recipe bytes stayed frozen
through the final producer and all three matching consumers. Later ticket
resolution commits change only documentation.

## Public behavior

The delivered `qaplet.com` is a public-header-only SDK adapter. It implements
`run <file> [-- args...]`, `eval --source <text>`, `--help` and `--version`.
Optional preceding `--timeout-ms` and `--max-dispatches` configure the same
finite instance budgets. Run uses ESM and prints no namespace. Eval uses Script
and prints only copied supported values, including exact 64-bit integers,
UTF-8 strings with NUL, and hex bytes. Object/thenable conversion hooks and
diagnostic getters are not invoked. The actual CLI and pure C Host compare
Script/ESM, cache, rejection, result, budget and pending behavior.

Capability bit 16 declares `qaplet:runtime`. Creation copies explicit-length
strict UTF-8 argv strings and an optional absolute native cwd; the default is
the independently captured OS cwd. JS receives a frozen argv array and primitive
cwd. Later native buffer edits and chdir do not change snapshots or resolution.
CLI argv includes only the application arguments after `--`. Both builtins
reserve module identities from creation; a module capacity below two is refused.
Configured `bridge_value_bytes` bounds argv before allocation. Native snapshot
storage remains part of the CLOSED handle until destroy.

SDK PENDING with no ready work remains PENDING/no-progress. The CLI has no
external event producers and immediately emits `UNSETTLED_ENTRY`, closes and
exits 1. Raw OS exits are 0/1/2 for success/execution/arguments. On Windows the
adapter explicitly calls native `ExitProcess` after SDK close/destroy and stdio
flush; it does not normalize the observed shifted Host convention or reuse
the cooperative-stop enum value 23.

## Retained red/green slices

All paths below are relative to this worktree's `out/m1-12/`. Original logs,
developer sources, SDK distributions and artifacts remain retained.

| Slice | Actual evidence |
| --- | --- |
| Missing runtime builtin | `red-runtime/{build,run}.json`: actual Host import fails. `green-runtime/run.json`: runtime case passes; old capacity fixtures visibly fail after builtin count increases. Final fixtures account for both declared modules. |
| Copied creation config | `red-snapshot/report.json`: prior supplied SDK rejects the new nonzero config tail. `green-snapshot/report.json`: copied argv/cwd and read-only JS snapshot pass. |
| Actual CLI eval dispatch | `red-cli-dispatch/report.json`: actual CLI exits 2 with no result. `green-cli-dispatch-2/report.json`: Script `6 * 7` prints `42`, exit 0. The earlier missing libc declaration compile error is retained in `green-cli-dispatch/`. |
| Copied value formatting | `red-cli-values/report.json`: undefined/null/bool/integers have missing output and string/bytes need a result buffer. `green-cli-values/report.json`: all types pass, with raw NUL UTF-8 checked from byte logs. |
| Entry/help/arguments/budgets | `red-cli-entry/report.json` retains the eval-only adapter failures; `green-cli-entry/report.json` records implemented behavior. One unknown-builtin expectation was corrected to the existing SDK `UNSUPPORTED` code 6. Final catalog passes. |
| Empty entry argument | `red-empty-file/report.json`: native Windows raw exit 1 for an empty filename. Parser now rejects it as arguments; final raw exit 2. |
| Resource-limit integration | `red-config-limit/report.json`: five argv bytes are incorrectly accepted with a configured four-byte value limit. `green-config-limit/report.json`: public factory returns VALUE_TOO_LARGE and a null output. |
| Native Windows exit | `windows-raw-probe/report.json`: actual PE raw 0, 1, 2 and unsettled 1, without shifted-code normalization. Final audited Windows cases repeat the contract. |
| Supplied artifact refusal | `refusals/map-run.json` and `refusals/cli-run.json`: copied CPU-map and CLI-byte mutations reject before any product command (zero commands), while originals stay unchanged. |

Developer probes use the prior verified supplier objects plus separately built
SDK sources and real processes. They do not claim audited final distributions.
The existing `sdk.py` producer independently builds both final fat APEs, runs
the shared audits and packages public SDK/Host fixtures. Its complete delivered
inventory binds SDK archives (including `.aarch64`), CLI/Host bytes, sidecars,
CPU maps, build logs, loader and helper notices. Consumers rehash before and
after execution. Existing delivered module fixtures are never edited by cases.

## Final identities

Final producer: `out/m1-12/final/build.json`; READY pointer:
`out/m1-12/READY.json`.

| Identity | SHA-256 |
| --- | --- |
| Producer report | `da7d08076381e86f1a54d701d10bc615ae521ae0ae700e4e0cc812fbb1f0dea1` |
| SDK aggregate | `56f3db4226cfaa75242bf9120e572b721defa3cf19fcb624cb23f6b7abd62535` |
| SDK recipe | `7dd6a8104c9ed3d9491bbd960f3b0f4e687f95f8deb63694153385411c6067a8` |
| Host APE | `229cfc57b37b640adc67d49809747ec7ffd8a3ee6bb71630b9d31d94baac9035` |
| CLI APE | `723d8cd5384bf84767a109b7db09fcab797038a7cef0daf678de335acb3d053c` |
| Pinned input_id | `f07f23c64bfcfb4a92722723e3e673213534a6fa81e690c401bd9fa10e0fd178` |

The supplier lock and frozen M0 recipes are unchanged. The producer truthfully
records Linux Git's Markdown working-tree dirtiness. `final/source-identity.json`
separately verifies every SDK recipe file against its committed source blob;
all exact bytes match. No line-ending or global Git setting masks the observation.

## Actual matching consumers

| Target | Result and original report |
| --- | --- |
| Ubuntu x64 WSL2, kernel 6.6.87.2-microsoft-standard-WSL2, 4096 pages | `final/run.json`: 87/87 pass (45 Host + 42 CLI), no timeout/skip. |
| Normal-user Windows x64 native PE, kernel 10.0.22000, 4096 pages | `final/windows-user-run.json`: 86/86 required pass; one existing optional file-symlink fixture blocked and explicitly not claimed. All 42 CLI cases pass with raw OS exits and exact UTF-8/NUL bytes. |
| Linux ARM64 full-system QEMU, kernel 6.8.0-147-generic-64k, measured 65536 pages / smaps 64 KiB | Main checkout `out/m1/12-guest-final/run.json`: 87/87 pass, no timeout/skip; QEMU cleanly exits and all payload hashes remain unchanged. |

Guest mode is `emulated-system`, QEMU 6.2.0, `virt-6.2,gic-version=3`, cortex-a72,
TCG single CPU. Sibling `selection.json` binds the final producer, SDK, recipe,
input and `payload.json`; manifest SHA is
`1a19712fd253178a208683ececed5e6c3a6b8345f528847db7a65a4b832f82a4`.
Guest report SHA is
`ee39cf734dc2767ece11300678c3ad65ced468bf1097c44db17018823e68a0f8`.
`final/platform-verification.json` independently verifies the exact 87-case
catalog, common SDK/recipe identities, all 261 referenced raw guest logs, raw
UTF-8 contents and unchanged before/after payload hashes.

Both final unsettled CLI cases return raw 1 in milliseconds on Ubuntu/Windows
and within the same recorded guest second. No supervisor timeout is credited
as product completion. Existing lifecycle, event, module, stop and resource
regressions run alongside the new cases, including the 1,000-cycle Host case.

`out/m1-12/final-check.json` passes Ubuntu `dev.py check`: 219 tests with 75
existing optional-scope skips separately retained, plus the real system-C
bootstrap. Those skips do not describe the 87-case product catalog.

Replay uses the existing producer/consumer boundary:

```sh
python3 scripts/sdk.py build --output-dir out/m1-12/final/artifacts --report out/m1-12/final/build.json
python3 scripts/sdk.py run --build-report out/m1-12/final/build.json --artifact out/m1-12/final/artifacts/qaplet-host.com --loader-dir /path/to/supplied/native/loaders --report out/m1-12/final/run.json
python3 scripts/dev.py check --output-dir out/m1-12/final-check --report out/m1-12/final-check.json
```

The recorded commands supply the main physical cache automatically, and preserve
actual cwd, intervals, timeouts, raw logs and native loader identities. Windows
uses supplied PE bytes without `--loader-dir`; the guest uses the selected
source-built ARM64 loader and immutable source-module payload.

All five ticket criteria have executed coverage. This increment does not certify
the complete native Cross matrix, complete M1 Full, release readiness, or M2
external wakers/I/O scheduling. Integration remained `32d455e` at final handoff;
the later lifecycle ticket is concurrent and is not relabeled as tested here.
