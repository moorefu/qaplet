# 09 · Finite resource limits and allocation refusal validation

2026-11-02. Source commit `724f3ced14d170a6f4ecefb74c33a660da423d85`
on `codex/m1-09`, after merging integration
`c817a2a1d2ea0f9b6e4470cac50c7df08c06dbc9` (including ticket 10).
Source/recipe bytes stayed frozen through matching supplied native consumers;
only this validation record and ticket resolution were added afterward.

## Implemented boundary

Capability bit 13 (`QAPLET_CAP_RESOURCE_LIMITS`) advertises the complete
effective finite configuration. The existing heap/stack/native/source/cache,
module, execution and queue limits now also include `bridge_value_bytes`,
`host_arguments`, `pump_jobs` and `pump_events`. Their zero defaults are 1 MiB,
32, 256 and 32. All effective limits are copied into the public snapshot at
creation. Zero means a finite default; execution options only tighten timeout
and cumulative dispatch limits. Size multiplication/addition and millisecond
conversion are checked before use. Host argument capacity must fit the engine's
public integer argc and the complete metered argument frame.

The concrete classification gap was `eval_source` in `src/runtime.c`: an input
below `limits.source_bytes` whose accepted SDK source copy exhausted native
storage returned SOURCE_TOO_LARGE without failing the Runtime. It now uses the
same metered native allocator as registry/queue storage, so that actual refusal
latches MEMORY_LIMIT. Discoverable source/cache/module and bridge-input limit
refusals remain nonterminal. The public `resource-storage` fixture distinguishes
those boundaries without a private allocator call.

Callback argument descriptors and copied payloads now consume native storage.
The control object already includes bounded diagnostic storage; registry,
queue plus scratch, rejection records, results and source copies are metered.
Retained module source bytes have their separate cache ledger. Repeated module
cache hits do not charge twice, failed and declared builtin/Host identities
consume module capacity, and Script eval does not accumulate module sources.
OS/libc temporary storage, Host userdata and external workers remain outside
these budgets; engine current/peak measures backing allocations, not RSS.
Automatic GC remains enabled and is not forced by each pump. Event telemetry
counters saturate instead of wrapping.

The independent first-cause latch applies from the engine's first allocation,
including caught engine/SDK refusal. Zero-size allocation requests do not latch
failure; non-growing engine realloc retains its existing backing block. No
exception text classifier or engine patch was added. Unpublished factory
failure keeps direct backing-block reclamation; published Runtime close keeps
ordinary Context/Runtime finalization and Host descriptor lifetime.

## Preserved red and green evidence

All paths below are beneath this worktree's `out/m1-09/`; original artifacts and
raw command logs remain retained.

- `red-storage/build.json` is an audited supplied SDK producer. Its `run.json`
  fails only the new `resource-storage` case at the expected MEMORY_LIMIT
  assertion; the actual public source-copy refusal was nonterminal.
- `green-storage-red-config/run.json` passes the fixed storage case and all prior
  cases, while `resource-configuration` fails at the missing effective bridge
  defaults. Its independent audited producer/artifacts are retained.
- `green-config/build.json` and `run.json` pass all 36 cases after the effective
  defaults/configuration implementation.
- `combined/build.json` and `run.json` validate committed combined source with
  all five resource cases and the integrated stop regressions: 43/43 pass on
  Ubuntu, expected stdout, empty stderr, raw exit 0, and no supervisor timeout.

The five resource cases cover all finite default fields, copied fixed limits,
overflow refusals, exact and oversized bridge values, callback suppression on
preflight refusal, 33 arguments under a larger finite capacity, two-job and
two-event pump slices, fixed queue accounting, real rejection/Host-registration
storage pressure, caught bridge and engine allocation refusal, 32 native
factory limits and fresh successful recovery after each. Existing public cases
retain source/cache/module capacity, cache-hit/failed-identity accounting,
128 bounded engine initialization configurations, stack-exception reuse,
deadline/dispatch first causes, event counters and 1,000 lifecycle cycles.

## Audited identities and local check

| Identity | Value |
| --- | --- |
| Source commit | `724f3ced14d170a6f4ecefb74c33a660da423d85` |
| Pinned input_id | `f07f23c64bfcfb4a92722723e3e673213534a6fa81e690c401bd9fa10e0fd178` |
| SDK recipe | `523f3825f0e51e60e55e5ff69d993b703d95f7be48e58cf25bcb0e90826a5d01` |
| SDK aggregate | `eb80159d71d7b5d2449cfbc4b9e91fe26ddddd3934e8f31c034abb700f1f2b6b` |
| C Host APE | `2104198b8659f6c77568f597bdf79707319c2860fd191055be003ebfbccc4977` (4,875,478 bytes) |
| x64 static archive | `b1d52e9e9e6d107e63938c3ae96d3c1f5df3824a34d8b123ce1e778139a55b56` |
| ARM64 static archive | `9e7fa865329dbd9d28bc31999ff02bcfaae61960801342a33ce8c00055de987b` |

`combined/build.json` passes Ubuntu-only compilation, both archive components,
fat APE/PE/64K layout/symbol audits, pinned supplier and source verification
before/after, and exact recipe rechecks. `sdk/host-resources.c` is packaged with
the public header and static archives, including `lib/.aarch64/libqaplet.a`.
Linux Git records `working_tree_dirty=true` from the known Windows Markdown
line-ending observation. `combined/source-recipe-receipt.json` independently
checks all 42 producer recipe file hashes against exact committed Git blobs;
every file matches. No input lock, pinned engine or frozen M0 recipe changed.

Ubuntu x64 WSL2 kernel `6.6.87.2-microsoft-standard-WSL2`, 4096-byte pages,
executed supplied bytes using the verified official x64 ELF loader. The committed
source `combined/check.json` passes `scripts/dev.py check`: 219 contract tests
with 75 declared optional evidence/system skips, input-lock/CI contracts and
real system-C bootstrap. The shared physical cache is the main checkout
`.cache`; build intermediates remain there and evidence/artifacts remain in out.

## Matching supplied native consumers

The parent ran `combined/windows-user-run.json` as the normal Windows user:
42/42 required cases pass, 43 total. The optional module-symlinks fixture remains
explicitly blocked with raw process exit 19712 and native Windows error 1314;
it is not reported as a product pass. Required cases have expected stdout,
empty stderr and raw exit 0, without supervisor timeout. Actual host is Windows
x86_64, kernel 10.0.22000, 4096-byte pages. The selected SDK, recipe and Host
APE hashes match the table above; no consumer compiled source.

The main checkout `out/m1/09-guest-combined/{selection,payload,run}.json`
binds the same producer/SDK/recipe/input and packaged module fixtures to an
actual ARM64 full-system execution: 43/43 pass. The guest reports aarch64/Linux,
kernel 6.8.0-147-generic-64k, getconf page size 65536 and smaps 64 KiB, with
`execution_mode=emulated-system`. QEMU 6.2.0 ran virt-6.2/gic-v3/cortex-a72,
single-CPU TCG and 512 MiB RAM; it exited normally without supervisor timeout.
Every transferred hash stayed unchanged before/after. The C facts case
independently prints aarch64 and 65536. Per-case stdout/stderr, timing, stable
product results, raw exits, kernel/rootfs/QEMU and serial identities are retained.

The selected producer-report SHA-256 is
`7729cb0657a23790b28b6bbf18a4d05fb507c2d78b98e45f7342f4fe3674f0ef`;
the payload-manifest SHA-256 is
`30bd5d0f80a82b460475cb04fb10d91c23545d0b9a8e5ea5d6fb5d64504c6d12`.
This is supplied 64K execution, not native Linux ARM64 Cross coverage or a
new complete M1 Cross/Full claim.

## Replay and scope

From this worktree on Ubuntu:

```sh
python3 scripts/sdk.py build --output-dir out/m1-09/combined/artifacts \
  --report out/m1-09/combined/build.json
python3 scripts/sdk.py run --build-report out/m1-09/combined/build.json \
  --artifact out/m1-09/combined/artifacts/qaplet-host.com \
  --loader-dir /mnt/d/Workspace/dairn/qaplet/.cache/toolchains/cosmocc/official-assimilate-linux-x86_64-v1/f2f984d3a9790674fc0da11c1c00a32d4fe3f8dafde99af74b0c6dc24a2a16cf/tree/bin \
  --report out/m1-09/combined/run.json
python3 scripts/dev.py check --report out/m1-09/combined/check.json
```

The output directory must be new or empty for a fresh producer; replay should
choose fresh output/report paths and retain these originals. SDK smoke and
local checks do not claim native macOS/Linux ARM64 Cross coverage, a new full
M1 gate, offline SDK reproducibility or release readiness. The unchanged f07
engine input's prior qualified M0 Full evidence is separate from this SDK run.
