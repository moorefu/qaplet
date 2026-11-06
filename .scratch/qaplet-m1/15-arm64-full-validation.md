# 15 · Current SDK kernel evidence adapter

2026-11-02. Ticket 13 is an ancestor of this worktree's starting integration
commit `31155083b3677bf9888d14d6c01dd8758d551707`; the documentation tip
`5cda583d14917722c3a26b7e6cdbd470ef309d21` was absorbed before implementation.
The adapter development records below retain their original limited scope.
Matching clean-source Full evidence and the completed required mutation replay
are recorded in the final section. Ticket 15 is resolved on that evidence.

## Public boundary and scope

`scripts/sdk_guest.py export` verifies the supplied producer, current product
recipe, sealed ZIP, complete distribution/public SDK components, all three
audited executable slices and producer-derived ARM64 loader. Its versioned
selection binds the actual producer report, input lock, SDK/recipe, source
commit, ZIP and payload manifest. Export emits the existing complete 98-case
Host/CLI/application catalog. It does not certify an execution.

`import` and read-only `verify` consume the actual locked full-system report,
using `core_gate.Evidence` for original paths, verified original ZIPs and raw
file identities. `core_gate.verify_guest_transport` is the shared existing M0
system/serial/hash/cleanup contract. M0 retains its separate exact case catalog
and verdict. The adapter independently reconstructs every required SDK command,
exit and output expectation; a self-consistent report's claimed pass is
insufficient. It verifies measured CPU/page/kernel facts, supplier/kernel/config
identities, before/after loader/APE/module hashes and clean process completion.

The original M0 guest boot stays separate in CI. The existing measured page-gap
job adds an SDK-only 64K boot; native page coverage remains preferred, and an
available native 64K SDK run avoids guest substitution. The native Cross
consumer remains ticket 14's boundary. No runtime/engine, unified supplier lock,
M0 recipe, source selection or offline rule changed in this adapter.

Evaluator file identities are reported separately from the SDK product recipe,
so later evidence-gate changes do not imply that unchanged SDK binaries were
rebuilt. `--expected-commit` can require an exact Qaplet producer source commit.
The original-archive mirror remains an independent gate; all adapter reports
retain `release_ready=false`. M2 waker/I/O cancellation/scheduler remains outside
this ticket.

## Actual tracer and TDD

`out/m1-15/tracer/{export,payload,selection,guest,inputs}.json` preserves a fresh
official export and actual 98-case execution on ARM64 `aarch64`, 65,536-byte
pages, kernel `6.8.0-147-generic-64k`, `emulated-system`. The guest is the locked
QEMU 6.2.0 `virt-6.2,gic-version=3`/`cortex-a72`/single-thread TCG system, with
signed fixed Ubuntu inputs and RAM-only/no-network cleanup. The guest runner's
original raw serial/stdout/stderr and command provenance remain beside it.

This tracer deliberately uses ticket 13's preserved actual producer
`out/m1-13/final-v2/build.json`, SDK aggregate
`778434131e9cfa1448963a1bd507d08f42c501d4decd25297c2788b9f0b569a2`,
recipe `89bd63717e3f4134b15096dda379a36c065580c93a1fe3f7d3f45661f81d7c8b`.
It establishes a real adapter red/green closure; it does not promote that old
producer to the combined 14/15 acceptance source.

Observed vertical red/green slices at the public CLI:

- Export initially has no official CLI; the new exporter passes all 98 catalog
  assertions against the genuine delivered package.
- Import initially rejects the completed real guest as unimplemented; the
  semantic adapter then accepts the unchanged real closure.
- A mutation replacing the actual system command's executable with
  `qemu-aarch64` initially passes. The shared verifier now requires the actual
  full-system executable and rejects it.
- An added unselected `-drive` initially passes despite RAM-only cleanup claims.
  The shared verifier now checks the complete locked isolated command.
- A report output colliding with a copied real producer initially overwrites
  that copy on rejection. Output protection now rejects without changing bytes.

`out/m1-15/contracts.log` records eight passing public CLI tests. With
`QAPLET_SDK_GUEST_TEST_OUT=out/m1-15/contracts`, the successful/failed reports
and mutation fixtures are retained. The suite covers healthy ZIP transport,
an extracted report changed without changing the original ZIP, and a semantic
forgery with updated serial bytes, raw log identities and actual claims. The
latter reaches the authoritative public-output check and rejects. Sixteen
further genuine-report mutations cover missing/wrong supplier inputs, wrong
kernel/page/mode/config/loader, missing/duplicate/blocked cases, wrong argv,
forged actual output, missing raw logs, reversed intervals, missing deadlines
and failed cleanup. Required cases cannot be skipped or inferred from a boot.

The Ubuntu `make check OUT=out/m1-15/check` run passed 227 ordinary tests
(83 existing explicit-data/platform skips) and the real C bootstrap. These
ordinary skips do not count as M1 evidence acceptance. The retained actual
235-case current-input M0 Full closure passed the changed shared verifier at
`out/m1-15/m0-current-replay.json` (`status=pass`, `m0_core_status=pass`, no
errors). It is historical M0 evidence, not new SDK Full evidence; original ZIPs,
reports, raw logs, and supplier/offline facts were preserved.

## Final matching-source acceptance

2026-11-03 (Asia/Shanghai). The clean product source is
`21bd317f4246571f0048789ca97167632c09f924`. All 57 product recipe files match
that commit's Git blobs. The guest/evidence evaluator files remain unchanged
from the source-ready adapter, and the ticket 14 integration tip
`dd28a861c782a4c1844d7a5cd4f4e0d27d7a3f1b` was absorbed before resolution;
its additions are the ticket 14 documentation and resolution only.

The unchanged-source [Full retry 41489263809](https://github.com/moorefu/qaplet/actions/runs/41489263809)
completed `success`. All five native jobs, actual M0 and SDK 64K guest boots,
semantic imports, six-target Core aggregation and current offline proof passed.
The only skipped guest step was the unneeded 4K merge conditional. The current
retry's native Linux ARM64 SDK report measures `aarch64`, native execution,
4,096-byte pages and kernel `7.0.0-1017-azure`; it completed 1,000 lifecycle
cycles. The actual page-gap plan therefore supplemented only 64K.

The retry's locked system guest measured Linux/aarch64, 65,536-byte pages and
kernel `6.8.0-147-generic-64k`, explicitly `emulated-system`. Its SDK boot passed
all 98 required Host, CLI and application cases, with no failed or blocked
cases. These include real static/dynamic ESM/TLA, Promise/jobs, allocation and
GC/resource pressure, interrupt/stop first causes, copied worker events, owner
pump and close/destroy/resource cleanup. Raw serial facts, stdout/stderr,
timeouts, before/after payload hashes, locked kernel/config/rootfs/QEMU and
cleanup are preserved. QEMU exited normally, without timeout, disks, network
or host-global changes. No qemu-user or cross-compilation substitute is used.

All final acceptance identities belong to this one retry producer:

| Input | SHA-256 |
| --- | --- |
| Producer report | `23aed03868e9d6444e5df5c9107f733948900795b3a1334a31124d1250bf2828` |
| SDK aggregate | `7ec19ce8f7dc6eacbe40d650077a60bf463eff77da4baa87552c3b9f81048ed1` |
| Product recipe | `5a86423264e76e2213d12ca9fbfbc1023fb1360122a81cd28bd00edcf2d068f8` |
| Retry sealed SDK ZIP | `2524580d49e556dc94cbfd51e1a662953279a121f6b051a3b99123a878e7bbb6` |
| C Host APE | `24d31ff5e53cc954ddbf10f71a2e5865d4935f84aa27c88ac523ca745ac7d931` |
| CLI APE | `723d8cd5384bf84767a109b7db09fcab797038a7cef0daf678de335acb3d053c` |
| Complete application APE | `dc1e572109699b0f6837e14c4ede715468ca9ea907d164fc342b72604af79719` |
| Original SDK guest report | `b56184ed41aeae75a95edc743c85e01ea885d40f8fcb1a21dc50bef41b1cd62c` |

The unchanged input ID is
`f07f23c64bfcfb4a92722723e3e673213534a6fa81e690c401bd9fa10e0fd178`.
Both actual static archives are still byte-bound components of the retry SDK
and ZIP; the hidden ARM64 archive is present. Equal SDK aggregates across
different runs do not make their producer reports or ZIPs interchangeable.

Original-provider transport receipt:
`out/m1/ci/14-final-full-retry-complete-collection.json` under the main checkout.
It verifies nine original archives, 101,068,856 bytes in total. Short physical
roots `out/m1/ci/14-final-full-retry/14r-ARTIFACTID` avoid collisions with prior
same-source attempts. The derived selections restore actual provider names by
ID from `14-final-full-retry-artifacts.json`. The critical provider IDs are SDK
build **13162749024**, page-gap **13162819905**, and Full outer closure
**13163039687**. The outer closure contains the original 12 M0 provider ZIPs.
No original report, ZIP, log, CI identity or outcome was rewritten.

This worktree's `out/m1-15/final-retry/` preserves:

- `sdk-inputs.json` and `sdk-verified.json`: the official read-only SDK guest
  verifier passes, requiring exact Qaplet source commit `21bd317...`, actual
  matching retry CI identities and the complete ZIP/SDK/three-APE payload.
- `m0-inputs.json` and `m0-verified.json`: the unchanged complete Core consumer
  replays **235 cases across six targets**, including actual deep64K,
  loader/link audits, current supplier inputs and the real offline pair.
  Offline includes six binary and two helper comparisons and all 14 actual
  supplier refusals; recipe comparison, network isolation and nonroot rules
  remain enforced. It reports pass with no errors, using the current retry
  evidence rather than the historical 235-case baseline.
- `selection-provenance.json`: exact retry/producer/package/provider identities
  and explicit original-path mappings. `prepare_final_retry.py` beside the
  `final-retry/` directory records the one-use derivation.
- `contracts.log` and `contracts/`: all **eight** final public CLI tests passed
  in **357.169 seconds**, without skips, including the 16 provenance mutation
  subcases. Twenty expected verification-refusal reports and the actual healthy
  import/export fixtures are retained. Changed extracted bytes fail original
  ZIP transport; a rehashed raw serial/log/actual forgery reaches and fails the
  authoritative public-output contract.

The six guest import/transport/provenance test methods and their 16 mutation
subcases select only the final retry closure. The two separate export/protected
output tests use ticket 14's already-built clean local `21bd317` producer as an
export fixture: the public exporter intentionally refuses a local re-export of
a CI producer without matching execution origin. This fixture is not native,
64K or Full acceptance evidence. All final platform, package and kernel joins
above use only retry **41489263809**.

## Preserved failures and independent local observations

The earlier `0f4f765` Full attempt `41463967070` retained the native failures
diagnosed and corrected by ticket 14. The first clean `21bd317` Full attempt
`41486421618` retained the three HTTP 503 failures fetching the exact locked
`noble-updates-InRelease`; its guest did not run. Its successful native results
and different sealed ZIP keep their original scope and do not replace the
retry's package/producer identities.

While that external service was unavailable, `out/m1-15/final-ci-supplement/`
recorded actual local execution of the first clean run's original SDK bytes
(98 cases) and current M0 bytes (17 startup, 20 I/O and 12 deep assertions).
All local guests retain `ci=null`. The unchanged official SDK/startup/I/O/deep
importers rejected their mixed CI/local origin, and those refusal reports are
preserved. Neither fake CI environment nor an evaluator bypass was used.
Those raw local observations and the prior `combined-tracer/` rehearsal remain
independent limited-scope evidence; only the successful retry supplies final
official kernel/Full acceptance.

This closes the ticket 15 kernel and applicable existing Full boundary. It
does not issue the combined M1 strict completion verdict owned by ticket 16.
The deferred long-term original-archive mirror remains separate and
`release_ready=false`; M2 waker, I/O cancellation and scheduler boundaries are
unchanged.
