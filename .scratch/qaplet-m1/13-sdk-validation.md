# 13 · Sealed SDK distribution and external pure C application

2026-11-02. Product source is
`b636f15426b4f4deba4e878ff036843ca2581ffd` on `codex/m1-13`. It contains
integration `ee84e7e5fc1b870c658b44d3ea5333bd570260a4` through merge
`4010e4ffe2744721d7c19c0a5e111abbef7023ae`; both blocking tickets 11 and 12
were resolved and merged before this ticket was claimed. Product and recipe
bytes stayed frozen through the final producer, external compilation and
matching Ubuntu, Windows and ARM64 consumers. The resolution commit changes
only this record and ticket 13.

## Delivered public boundary

The existing `scripts/sdk.py` producer now publishes a real
`qaplet-sdk.zip` (13,490,877 bytes, 65 members). Its 43 SDK components include
the public header, both actual static archives, licenses, a README, complete
pure C application and immutable source modules. ZIP inventory is explicit and
includes `sdk/lib/.aarch64/libqaplet.a`. The same transport supplies the audited
`qaplet-host.com`, `qaplet.com` and `qaplet-example.com`, both CPU sidecars/maps,
build logs, portable ARM64 loader and helper notices.

`sdk/manifest.json` declares ABI 1, implemented capabilities (98303), capability
names, all 14 effective finite defaults, architecture-specific archive identities,
engine/input/toolchain/source/recipe identity and the three executable identities.
The live application `--metadata` output independently queries the public SDK
and must agree with those declared defaults and capabilities. No engine or
Runtime implementation changed for this ticket, and no M0 lock/recipe was rewritten.

The additive `sdk.py package-consume` operation verifies the actual sealed ZIP
and complete producer inventory, extracts it under a fresh POSIX `/tmp` directory
outside both source checkouts, compiles only extracted `sdk/application.c` with
`sdk/include` and the supplied `sdk/lib/libqaplet.a`/hidden ARM64 sibling, then
reuses the existing portable linker, packer, publication and both-CPU audits.
No private Qaplet/engine header or source participates in that compilation.
The compiler's C/POSIX and native process-exit adapter headers remain toolchain
materials. Reusable supplier inputs stay in the main physical `.cache`; the
external application is a one-use fixture. Its source, package, commands, cwd,
raw logs, original-packer output and audited binaries remain recorded.

The final externally compiled example is byte-identical to the supplied example
APE. The package is checked before and after external consumption; both static
archive hashes, source modules and all supplied materials remain unchanged.

The application registers copied Host constants/functions before execution,
uses static and dynamic source ESM and top-level await, registers an event
receiver, and receives exact uint64 tokens and copied bytes from a real worker.
The owner pumps, retrieves the module result and application result 42, then
the worker requests stop and the owner confirms STOPPED. The owner closes while
the worker retains the synchronized control handle; the worker verifies CLOSED
post/query/stop behavior, exits and is joined. Host FILE/userdata stay alive
through close. The Host reads the actual copied bytes, closes/unlinks its FILE,
then finally destroys the Runtime. Resource setup uses native Windows TEMP or
POSIX `/tmp` with mkstemp/fdopen, retaining the measured ticket 11 portability fix.

Eight required application cases cover this complete path, ordinary rejection,
queue-full refusal, deadline termination, missing module, argument errors, ABI
and metadata. Failures execute cleanup and return raw exit 1; arguments return
2 and success returns 0. The ABI workload uses a real short prefix and canary,
short config/limits/options/progress/result/snapshot/diagnostic/value inputs,
compatible zero tails and nonzero tails, incompatible versions and unknown
flags/reserved fields. Actual bad inputs preserve state; larger compatible
outputs leave zero tails intact. A real worker's independent invalid-event
diagnostic does not change the retained owner exception diagnostic. Short and
zero-capacity diagnostics retain code/length/truncation behavior.

## Retained TDD and refusal evidence

Paths here are under this worktree's `out/m1-13/`; originals were retained.

| Slice | Actual red and green evidence |
| --- | --- |
| Sealed external package | `red-package/report.json`: ticket 11 producer has no sealed package/application. `green-package/{build,package-consume}.json`: actual first ZIP extracted and compiled externally; all seven initial application cases pass. |
| Packaged ABI workload | `red-abi/report.json`: the initial application treats `--abi` as a missing entry and exits 1. `green-abi/report.json`: new ABI workload compiled externally against the existing supplied public SDK passes. This is pre-existing Runtime ABI behavior, not a fabricated Runtime defect/fix. Final sealed package includes and repeats it. |
| Malformed ZIP reporting | `red-invalid-zip/probe.json` and `stderr.log`: a receipt-matched invalid ZIP raises BadZipFile and leaves no consumer report. `green-invalid-zip/{probe,run}.json`: the committed exception handling returns a structured failure with zero compiler commands. |
| Sealed package refusals | `refusals/report.json`: changed archive byte, missing hidden ARM64 archive, changed public header, duplicate ZIP member and altered manifest finite default all reject through real package-consume calls before any compiler command. Original ZIP remains unchanged. |

`final/` preserves the successful producer before the malformed-ZIP reporting
fix; it is superseded by `final-v2/`. Neither its receipt nor the first
`green-package/` package is silently promoted to the final recipe. All raw red,
green and superseded packages, binaries, sources and logs remain available.

## Final identities

Final producer: `out/m1-13/final-v2/build.json`; handoff:
`out/m1-13/READY.json`. Exact identities are also recorded in those original files.

| Identity | SHA-256 |
| --- | --- |
| Producer report | `4e0d95ca42a4f6fced34dae2a4f55bbeccc952c41201217dadc8ad63d51e9811` |
| SDK aggregate | `778434131e9cfa1448963a1bd507d08f42c501d4decd25297c2788b9f0b569a2` |
| SDK recipe | `89bd63717e3f4134b15096dda379a36c065580c93a1fe3f7d3f45661f81d7c8b` |
| Sealed ZIP | `4d3a1802ff41e7d2a655bb800083d87a0b41c938e625bfd3758a4b03c5ef5723` |
| x86_64 static archive | `36ebd78c828c10f010628acfc7592c5092dbb37ef45fb0fdd353027b096084e7` |
| aarch64 static archive | `7459256aa99a9a8212e72b5334afeb48167b35832cad10bee586ab773d6586d2` |
| C Host APE | `2c9b3a4c0b6334cfc6021cb0c1031dda449a80a3c55b2dbd335f4c8796684e88` |
| CLI APE | `723d8cd5384bf84767a109b7db09fcab797038a7cef0daf678de335acb3d053c` |
| Complete application APE | `dc1e572109699b0f6837e14c4ede715468ca9ea907d164fc342b72604af79719` |
| External package-consumer report | `5cc32afc22ffed3a261c480dcbca6af2659c0e70f00265b480f89c61569d2261` |

Pinned input_id remains
`f07f23c64bfcfb4a92722723e3e673213534a6fa81e690c401bd9fa10e0fd178`.
`final-v2/source-identity.json` verifies all 57 SDK recipe file bytes against
their committed blobs. The producer truthfully retains Linux Git's existing
Markdown working-tree dirtiness; no unrelated newline/global Git changes mask
that observation. Only the new byte-bound SDK README gained an explicit LF rule.

## Matching actual executions

| Target / boundary | Result and original evidence |
| --- | --- |
| Ubuntu x64 WSL2, kernel 6.6.87.2-microsoft-standard-WSL2, 4096 pages, supplied APEs | `final-v2/run.json`: 98/98 pass, comprising 48 Host, 42 CLI and 8 application cases. No timeout/skip. |
| Ubuntu actual external ZIP compilation, `/tmp` application, public header/archive only | `final-v2/package-consume.json`: 8/8 pass after both-CPU APE audits and final supplier rechecks. Rebuilt APE matches the supplied example SHA above. |
| Normal-user Windows x64 native PE, kernel 10.0.22000, 4096 pages | `final-v2/windows-user-run.json`: 97/97 required pass; the established optional symlink fixture is blocked. All eight application cases pass with raw exits 0/1/2 and real resource teardown. |
| Linux ARM64 full-system guest, kernel 6.8.0-147-generic-64k, actual 65536-byte pages / smaps 64 KiB | Main checkout `out/m1/13-guest-final-v2/run.json`: 98/98 pass. No timeout/skip, unchanged before/after payload hashes and clean QEMU exit. |

The guest mode is `emulated-system`: QEMU 6.2.0, `virt-6.2,gic-version=3`,
cortex-a72, TCG single CPU, 512 MiB, no disk/network. Its sibling `selection.json`
binds the exact producer, SDK, recipe, pinned inputs and payload manifest.
Guest report SHA is
`c7bf720cfb651fd702406dfef1d6c5133b1626b2f0e47499f02008856c812afb`;
payload manifest SHA is
`86461ad6ba58b64cdd0f072d241eb12658152b911ce6a3e13870880637c116a3`.
`final-v2/platform-verification.json` independently verifies the full 98-case
guest catalog/argv/output/exit, every referenced raw log (197 distinct files),
payload identities before/after, matching SDK/recipe selections and Windows
raw outputs. Windows report SHA is
`989865f9e613a3cab700f8fcf0a6fcdf5d81c53490e3e92d17a3ce215ecd59b3`.

`out/m1-13/final-v2-check.json` passes Ubuntu `dev.py check`: 219 repository
tests with 75 established optional-scope skips separately reported, plus the
actual system C bootstrap. Those skips are not counted as product passes.
Existing required lifetime coverage still executes 1,000 Runtime cycles.

Replay from the source checkout uses the established workflow:

```sh
python3 scripts/sdk.py build --output-dir out/sdk/artifacts --report out/sdk/build.json
python3 scripts/sdk.py run --build-report out/sdk/build.json --artifact out/sdk/artifacts/qaplet-host.com --loader-dir SUPPLIED_LOADERS --report out/sdk/run.json
python3 scripts/sdk.py package-consume --build-report out/sdk/build.json --artifact out/sdk/artifacts/qaplet-host.com --output-dir out/sdk/external --loader-dir SUPPLIED_LOADERS --report out/sdk/external.json
```

The actual recorded commands use the main physical cache and supplied native
loader directory. Windows consumes the supplied PE bytes without rebuilding;
the guest consumes the exact APE/loader/module bytes. Complete native macOS and
Linux ARM64 Cross, full M1 Full/strict aggregation and release readiness remain
with tickets 14–16. These measured boundaries do not claim those unfinished
matrices or M2 external wakers/I/O cancellation.
