# 06 · Synchronous Host modules validation

2026-11-02: implementation on `codex/m1-06`, source commit
`bc99e75f501fbe4a14a73aff1911e9d8dd1cd26f`, based on integration
`6139b75eeb90fed7e4c90587ca29bdc4ea638d2e`. The supplier lock and engine
overlays are unchanged by this ticket.

The public `qaplet_register_host_module` accepts complete, versioned C
declarations before execution and atomically copies their UTF-8 names, export
descriptors and constant payloads. Registered Host identities count in the
existing finite module registry; lazy C modules use the existing normalize/load
path for static and dynamic ESM. No C module is published before full declaration
validation. Copied descriptors remain alive through normal engine finalization.
Function code/userdata are Host-owned and must survive until close returns.
The factory still runs no external Host resources before publication.

The bridge reuses `src/values.c` strict conversion, without conversion hooks.
Borrowed arguments are copied into metered native storage for callback duration.
`qaplet_host_set_result` copies synchronously into an engine value, defaults to
undefined and preserves the first accepted return. Its Runtime/callback-ID token
does not retain or dereference a callback stack pointer; expired and outside
tokens reject. Ordinary callbacks throw intrinsic Errors with direct own numeric
codes, including `HOST_ERROR=19`; allocator/budget first causes remain permanent.
Capability bit 11 denotes this implemented Host bridge.

## Public TDD evidence

Tests use the previously agreed public SDK / public-header-only supplied C Host
seam. `scripts/sdk.py` distributes and compiles `host-host-modules.c` against its
public header and static archives, delivers `modules/host-bridge.mjs`, and runs
the added `qaplet-host.host-modules` catalog case. No parallel test framework or
new engine-access seam was added.

All paths below are relative to this worktree. Raw logs, expected/actual status,
bounded commands, original Host artifacts, hashes and preservation receipts are
retained under `out/m1-06/`. The ignored `iterate.py` uses existing
`foundation.command` and the retained verified engine archive for quick public
boundary iterations; its reports explicitly are not final SDK producers.

| Slice | Actual retained result |
| --- | --- |
| Missing Host-module capability | `red-capability-run/result.json`: failed assertion, exit 1 |
| Capability after implementation | `green-capability/result.json`: pass |
| First real ESM typed callback | `typed-callback-first/result.json`: failed with intrinsic `invalid length`, exit 1 |
| Safe public diagnostic of same failure | `typed-callback-diagnostic/result.json`: failure retained |
| Corrected Uint8Array construction with all optional engine argument slots | `green-typed-callback/result.json`: pass |
| Registration retry, full types, limits, stale IDs, reentry | `host-boundaries-first/result.json`: pass |
| Permanent bridge storage refusal and existing value/Promise/budget/lifecycle regressions | `host-allocation-first/result.json`: pass |
| Final additional invalid setter, UTF-8 names and ordinary unhandled failure/reuse | `final-local-slices/result.json`: pass |

The initial `red-capability/build.json` truthfully has status `fail`: a source
edit overlapped its producer and the final recipe-change check rejected it.
Its actual audited artifact is retained, and the separate red execution records
the real missing-capability assertion. It is never relabeled as a successful
distribution. Every iteration artifact was copied under its own output directory
with a `preservation.json` hash comparison, preserving original reports.

The Host case observes deep-copy independence after overwriting original names,
declarations and string/byte payloads; exact signed/unsigned 64-bit values;
Number/NaN/-0, null/bool/undefined; UTF-8/NUL; temporary setter buffers; independent
Uint8Array slices; default undefined and duplicate return retention. Negative
cases cover duplicate exports/modules, invalid native UTF-8, oversized constants,
reserved namespace/NUL, count refusal, registration after pump/execution,
non-owner calls, expired and different-active-callback setter IDs, unsupported
objects/functions/boxed values/proxies/shared/detached views, out-of-range
BigInts, lone surrogates, 32/33 arguments and exact/over 1 MiB payloads. Native
callback counters prove refused arguments do not invoke C. Reentry checks eval,
run, pump, registration, get-result, close and destroy return BUSY while snapshot
and setters work. Error.prototype setters are not invoked. A small public native
storage limit produces a real bridge-copy refusal; even with JS catch the public
result and lifecycle remain MEMORY_LIMIT/FAILED, with close available.

## Committed audited producer and supplied consumer

`out/m1-06/final/build.json` passes the Ubuntu SDK build, both archive components,
fat APE audits and supplier/recipe rechecks. The producer records source commit
above. Its Linux Git status truthfully reports working-tree dirtiness from the
Windows-created Markdown execution context; all C/Python/header recipe files use
LF and Windows Git was clean before the build. No global Git configuration or
old evidence was changed to hide that observation.

| Identity | SHA-256 |
| --- | --- |
| Pinned input | `f0b8dbcf6e44b58248e45bd755bbce5414b8715d3895caf5318efb31b48e5ead` |
| SDK recipe | `4492f6dbd10327658f5d19d0455bc6217ee43bda74803cfe15e08d1288993029` |
| SDK components aggregate | `ef580a49579327673a2aeee907ad1ff43b31038bbc83ff585359c94953195152` |
| qaplet-host.com | `653f0189237d4b2caaf8a16ecc3e290ea944d21084dc6cb3cf886347bad56a0c` |

`out/m1-06/final/run.json` passes all 23 public Host cases using the supplied
SDK/APE bytes on Ubuntu x64, WSL2 kernel `6.6.87.2-microsoft-standard-WSL2`,
4096-byte pages, native x64 loader. Expected stdout, actual stdout/stderr,
exit codes, 30-second per-case supervisor limits and independent C architecture/
page facts are retained. No case skipped or timed out. The lifecycle regression
performs its existing 1,000 create/close/destroy cycles.

`out/m1-06/check.json` passes `scripts/dev.py check` (preflight, existing
repository tests and real Ubuntu C bootstrap) with the physical repository-root
cache. Integration `6139b75` was already merged before the final producer.

The final READY pointer and exact identities were sent to the parent before
ticket resolution. Both supplied-byte executions pass all 23 catalog cases with
the exact final identities above:

| Target | Retained report |
| --- | --- |
| Windows x64, normal-user native PE, 4096-byte pages | Worktree `out/m1-06/final/windows-user-run.json` |
| ARM64 full-system QEMU, 65536-byte guest pages | Main checkout `out/m1/06-guest-final/run.json` |

The guest's sibling `selection.json` and `payload.json` bind the exact SDK,
producer, recipe, input, Host APE and delivered source fixtures. Guest kernel/CPU/
page measurements are separate from the outer x64 supervisor. No required case
skipped or timed out. Parent confirmed both actual runs before resolution.
The integration tip remained `6139b75` at completion; ticket 05's independent
candidate input was not yet integrated and no earlier evidence was relabeled.
This record does not claim the complete M1 Cross/Full matrix, macOS/native ARM64
or release readiness.
