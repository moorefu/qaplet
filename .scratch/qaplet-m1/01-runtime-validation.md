# 01 · Minimal Runtime / C Host validation

2026-11-02: ticket 01 implemented on `codex/m1-01`. Runtime source commit
`3c0d68202670dc7304d26f242ba4ee36a500dc8e`; header LF policy commit
`d70162caa00bd0e89b5158e306c8c61498baca3a`. The latter is the final producer's
recorded commit. No frozen M0 recipe, unified lock, engine source or overlay changed.

The public contract is `include/qaplet.h`; implementation separates lifecycle /
factory (`src/runtime.c`) from scalar results / diagnostics (`src/values.c`). The
pure C Host uses only the distributed public header and static SDK archive.
Supported capabilities are lifecycle, synchronous Script, scalar results, owner
thread, POD snapshots and the implemented heap/stack/native/source resource base.
No module, Promise pump, host bridge, event or stop capability is advertised.

## Executed identities

| Identity | SHA-256 / value |
| --- | --- |
| Pinned unified input_id | `f0b8dbcf6e44b58248e45bd755bbce5414b8715d3895caf5318efb31b48e5ead` |
| SDK recipe | `16cf9de350d72f0dd90b065d20d84051f3607483eda760f940f99ac9cb50bd7f` |
| SDK components aggregate | `e61144d601b2298f6cd1b58be9b43f5ec722eeb8b361118ce7546ed456ced2f7` |
| qaplet-host.com | `45c23ab3a62a0f1a5584ad577ca66d5872b95c31899e9b90b624b0fc1f33eb52` |

Retained producer: `out/m1-01/verified/build.json`, supplied distribution in
`out/m1-01/verified/artifacts/` in this worktree. It records raw commands, source /
recipe files, archive components, headers, both CPU maps, PE imports, loader /
packer identity and before/after supplier verification. Build intermediates and
reusable inputs use the physical main checkout `.cache`.

Ubuntu Git observes pre-existing CRLF Markdown as dirty while Windows Git's
autocrlf view is clean. The report retains this `working_tree_dirty=true`
observation and raw status, rather than asserting a clean tree. The SDK's exact
C/Python/header recipe bytes and component digests are independently bound;
`.gitattributes` now preserves LF headers across checkouts.

## Actual execution

All eight cases passed per target: scalar; retained/fresh state and exception
reuse; close/destroy order and 1,000 lifecycle cycles; owner-thread rejection and
thread-safe snapshots; 128 heap configurations with failed-create null output
and repeated recovery; finite source/heap/stack behavior; ABI and bounded
diagnostics; actual architecture/page facts. No required ticket 01 case skipped.

| Target | Retained report |
| --- | --- |
| Ubuntu x64, WSL2, 4096-byte pages | Worktree `out/m1-01/verified/run.json` |
| Windows x64, normal user native PE | Worktree `out/m1-01/verified/windows-user-run.json` |
| ARM64 full-system QEMU, 65536-byte pages | Main checkout `out/m1/01-guest-verified/run.json` |
| ARM64 full-system QEMU, 4096-byte pages | Main checkout `out/m1/01-guest-4k-verified/run.json` |

The guest reports measure kernel/CPU/page configuration and bind the supplied
Host + loader. Their sibling `selection.json` and payload bind the verified
producer, SDK and recipe. Guest execution is emulated-system, not native ARM64 or
physical performance evidence. Parent captured both guest and Windows runs.

`out/m1-01/fixed/check.json`: the repository Ubuntu `dev.py check` passed after
the allocation fix, including the real system C bootstrap. Its ordinary suite ran
219 tests, with 75 existing opt-in evidence tests skipped; those skips are not
counted as M1 passes. `out/m1-01/verified/identity-refusals/result.json` records
actual CLI rejection of altered Host / SDK-header copies before any product
command. Original M0 evidence remains its original scope.

## Creation regression and correction

The first artifact `fe8ede1ced4b6fbb68abf4103939741e5de50ab71961a8a9b3662ce848c634f6`
passed x64 but segfaulted in the public creation sweep on ARM64 4K and 64K.
`out/m1-01/diagnosis/minimal-before.json` minimizes it to one 37,888-byte heap
configuration. `boundaries-before.json` records Runtime creation succeeding,
Context creation returning null after allocation refusal, then Runtime teardown
segfaulting. The pinned `JS_NewContextRaw` allocation-failure path can free a
Context after adding its GC header, leaving a partial graph unsafe for teardown.

The SDK allocator now retains ownership of each backing block. Failed,
unpublished creation reclaims outstanding blocks directly: no user JS, Host
module or external native resource has run at this boundary. Published Runtime
close still uses ordinary Context/Runtime finalization. No engine patch or
exception-text matching was added. The permanent public C sweep verifies null
failure handles and a new create/eval/close/destroy recovery after every failure,
including zero engine bytes after recovered close. The original full sweep and
all target cases pass with the final bytes. Tagged diagnostic instrumentation
exists only in ignored, clearly named `out/.../diagnosis` / shared diagnostic
build artifacts; product sources contain none.

## Scope

Ticket 01 is complete. The later M1 module/value/jobs/event/stop/resource and
distribution contracts remain their own tickets. macOS and native ARM64 Cross,
complete M1 Full / strict evidence aggregation and release readiness are not
certified here. Replay commands and the additive SDK identity policy are in
`docs/development/sdk.md`.
