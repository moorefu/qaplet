# 02 · Copied values / safe diagnostics validation

2026-11-02: implementation on `codex/m1-02`, initial source commit `c8bece0`.
The final combined producer records commit
`b75d38bb42478407df021580948e90d6f8a0dfe8`. No supplier lock, engine source/overlay
or frozen M0 recipe was changed.

The public contract is `include/qaplet.h`. The existing `src/values.c` boundary
captures strict scalars, exact signed/unsigned 64-bit BigInt, UTF-8 strings and
ordinary-buffer Uint8Array views in owned native storage. Native snapshots meter
these copies. The result buffer belongs to each C caller; insufficient capacity
reports the payload length and performs no partial copy. Diagnostics use bounded
safe text without getters, arbitrary conversions, Proxy traps or stack lookup.

## Public TDD evidence

The supplied C Host includes the public-header-only `host-values.c`, copied from
`tests/native/sdk_values.c`. Tests use the approved SDK / pure C consumer seam.
The initial full Ubuntu producer is retained at
`out/m1-02/red-integers/build.json`, with the original static SDK and Host APE in
the sibling `artifacts/`. `actual-run.json` records the real BigInt failure;
`run.json` separately retains the earlier missing-explicit-loader refusal.

The ignored replay helper `out/m1-02/iterate.py` uses `foundation.command` for
bounded compile/link/run commands and raw logs. It recompiles current SDK and
public Host files on Ubuntu into unique physical main `.cache/build/sdk-values/`
directories and links the already verified pinned-engine objects from that
initial producer. These iterations are TDD evidence, not final distribution
producers. Each `result.json` binds the current source/recipe bytes, baseline
producer, commands, actual Host artifact, host facts and outcomes.

| Slice | Red evidence directory | Green evidence directory |
| --- | --- | --- |
| Exact BigInt | `red-integers/actual-run.json` | `green-integers/result.json` |
| UTF-8 strings / retry | `red-strings/` | `green-strings/` |
| Uint8Array | `red-bytes/` | `green-bytes-fixed/` |
| Safe exception diagnostics | `red-diagnostics/` | `green-diagnostics/` |
| Capability / exact byte limit / UTF-8 diagnostic truncation | `red-value-limits/` | `green-value-limits/` |
| Combined Promise values / safe rejection diagnostics | `red-promise-values/` | `green-promise-values/` |

All paths in that table are under `out/m1-02/`. The intermediate
`green-bytes/` failure is retained: a function fixture was written as a declaration
without a name. Correcting it to a function expression allowed the intended
unsupported-function branch to execute; no product workaround was added.

`green-value-limits/result.json` passed all values/diagnostics slices and baseline
state, ABI, creation, resource, lifecycle and owner-thread cases. It verifies
signed minimum/maximum, unsigned maximum, values beyond Number precision,
oversized positive/negative BigInt; every scalar including NaN, infinities and
negative zero; string NUL and strict Unicode; short/zero buffers and retry;
resizable tracking/fixed views, detached views, shared backing, proxies and other
typed brands; exact 1 MiB strings/bytes and over-limit encoded strings; preserved
copies and rejected-call results; metered native-storage failure; safe Error and
primitive text, independent concurrent diagnostics and codepoint-safe truncation.

`out/m1-02/pre-integration-check.json` records the repository's prescribed Ubuntu
`dev.py check` passing with the physical main cache. Existing opt-in evidence
skips retain their original scope and are not counted as ticket 02 passes.

The current integration tip `08a2f148b117aaf84a7b7a7b690d6374f26ed95f`
(including completed ticket 03) was merged as `b9dcf06`. The combined regression
first failed on a missing safe rejected-entry message, then passed after the
existing execution driver captured only the first decisive failure at its jobs
checkpoint. Promise BigInt/string/byte results use the same values boundary;
rejections handled before the checkpoint never become diagnostics. The complete
merged public Host regression passed in `green-promise-values/result.json`.

## Final distribution / supplied-host validation

The retained final producer is `out/m1-02/verified/build.json`; distribution is
the sibling `artifacts/`. The Ubuntu consumer `run.json` passed all 15 catalog
cases. `check.json` passed the repository check: 219 tests with 75 existing
opt-in skips, kept visible and not counted as M1 product passes.

| Identity | SHA-256 / value |
| --- | --- |
| Pinned unified input_id | `f0b8dbcf6e44b58248e45bd755bbce5414b8715d3895caf5318efb31b48e5ead` |
| SDK recipe | `741dcaef7abd30d7e4dfeeb4e9b27b3093ba1d46ef2b24169440516575798686` |
| SDK components aggregate | `c67dc72464ce6c657dbe43c7173888632e82952d5c9103967f902f19c376b6e3` |
| qaplet-host.com | `418761f66c4066990d7f627d979bcf17a1c7b5d960c125f9e26961c09a42203c` |

The producer retains raw commands, both target archives/maps, public header and
Host sources, helper/packer/audit evidence, source/recipe hashes and supplier
verification before and after the build. Ubuntu Git retains its observed dirty
Markdown line-ending state; exact SDK recipe bytes and distributed components
are bound independently, rather than asserting a clean tree.

All 15 cases passed on each executed target, with no ticket 02 case skipped:

| Target | Retained report |
| --- | --- |
| Ubuntu x64, WSL2, 4096-byte pages | Worktree `out/m1-02/verified/run.json` |
| Windows x64, normal-user native PE | Worktree `out/m1-02/verified/windows-user-run.json` |
| ARM64 full-system QEMU, 65536-byte pages | Main checkout `out/m1/02-guest-verified/run.json` |

The parent executed the supplied Windows and ARM64 consumers before resolution.
The guest's sibling `selection.json` and `payload.json` bind the verified
producer, pinned input, SDK/recipe identities and exact Host/loader bytes.
Actual guest facts are aarch64, Linux `6.8.0-147-generic-64k`, 65536-byte pages,
`emulated-system`; file digests are unchanged after execution. This is actual
kernel/page coverage, not native ARM64 or physical performance evidence.

Ticket 02 is resolved. New ARM64 4K execution is not claimed by this ticket;
ticket 01's earlier 4K evidence remains bound to its earlier artifact.
macOS, native ARM64 Cross, complete M1 strict Full aggregation and release
readiness are outside this ticket's local executed evidence.
