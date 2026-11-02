# 15 · Current SDK kernel evidence adapter

2026-11-02. Ticket 13 is an ancestor of this worktree's starting integration
commit `31155083b3677bf9888d14d6c01dd8758d551707`; the documentation tip
`5cda583d14917722c3a26b7e6cdbd470ef309d21` was absorbed before implementation.
Ticket 15 remains claimed while matching combined-source Full acceptance is
being coordinated with ticket 14. The records below are adapter development
evidence, not final M1 Cross/Full completion.

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

Pending the frozen combined 14/15 producer and its actual native/64K/Full
execution. No ticket acceptance box or resolved state is claimed by the tracer.
