# M1 final review fixes

Status: claimed

The single fresh implementer started from clean reviewed integration
`beab3190c7b298dc1b5729396a3897bf216e5bf2`. The parent specification stays claimed
until matching new-product acceptance and independent reviews finish.

## Independent axes

Standards (`out/reviews/m1r2/standards-report.md` in the main checkout):

- P2 documented violation: selected original transport archives can be overwritten.
- P3 maintainability judgment: duplicated native storage admission/accounting/free.
- P3 maintainability judgment: duplicated Host source/copy/compile/link inventories.

Spec (`out/reviews/m1r2/spec-report.md` in the main checkout):

- P1 original archive preservation, independently identified.
- P2 missing per-execution options for module entry and explicit-base Script eval.

Both complete reports remain separate; this record combines implementation and
validation only. The heuristic findings retain their original axis and priority.

## Public boundary red/green

All paths below are relative to this worktree unless stated otherwise.

- `out/review-fixes/archive-red.log`: four real CLI failures, showing that cloned
  fixture ZIP hashes change for strict top-level, strict nested M0, standalone
  SDK guest and Core output collisions. Real provider ZIPs were never targets.
- `out/review-fixes/archive-green.log`: all four refuse before creating an
  evidence directory, retaining identical before/after hashes and raw CLI logs.
- `out/review-fixes/actual-archive-preservation.log`: initial real-provider clone
  test used relative retained-fixture references incorrectly; only the clones
  were overwritten. This failed attempt is retained. The corrected test resolves
  its private fixture root before selecting files.
- `out/review-fixes/module-red/build.json` and its raw `qaplet-host-link` stderr:
  the delivered public-header-only Host cannot link `qaplet_run_entry_with_options`.
  The test requests a 300-dispatch execution under a 1000-dispatch instance.
- `out/review-fixes/module-green/{build,run}.json`: SDK producer and all 98
  supplied public Host/CLI/application cases pass, including the tightened module
  dispatch ledger across PENDING and pumps.
- `out/review-fixes/base-red/build.json`: public Host link fails specifically on
  the missing combined `qaplet_eval_with_base_and_options` symbol.
- `out/review-fixes/base-green/{build,run}.json`: producer and all 98 cases pass;
  a real relative dynamic import exhausts its tightened 300-dispatch budget
  across pumps under a 1000-dispatch Runtime maximum.
- `out/review-fixes/actual-archive-preservation-green.log`: the broad first guard
  resolved every historical per-log mapping and exceeded the 30-second test
  deadline. No provider original was modified. The common guard now inspects
  selection edges, roots and archive references only. The final real-provider
  clone test passes in 1.070 seconds at
  `out/review-fixes/actual-archive-preservation-final.log`; both selected top-level
  and nested M0 original/clone hashes are preserved, without evidence execution.
- `out/review-fixes/sdk-preservation-red.log`: 12 overwritten private fixture
  files across SDK run/matrix/package-consume exposed the adjacent distribution,
  loader and native-input-directory paths. Direct producer-report protection
  already held. No real supplied SDK or provider material was targeted.
- `out/review-fixes/preservation-green.log`: three public CLI tests / 25 subcases
  pass: the original four archive collisions, 15 SDK consumer/material
  combinations, and all three guest-export outputs against both producer and
  distribution input. Before/after hashes, raw output and exit receipts survive.
- `out/review-fixes/guest-selection-red.log`: one additional standalone guest
  input-manifest -> selection-file -> distribution edge could still overwrite a
  cloned ZIP when no transports were supplied. The guard now follows explicit
  selection files (and M0 publication selection wrappers), without traversing
  historical log mappings. `out/review-fixes/preservation-final.log` passes all
  four tests / 26 CLI cases in 6.146 seconds.
- `out/review-fixes/final-local/{build,run,package-consume}.json`: combined C API
  and review-refactor producer passes, all 98 native cases pass with 1000 lifecycle
  cycles, and the sealed package is compiled outside the checkout and passes all
  eight application cases. Recipe contains 57 unique files; this provisional
  producer records beab319 plus dirty source honestly. The final selection-file
  traversal adjustment happened afterward; these outputs are retained under
  their actual recipe identity, not relabelled as the frozen candidate.
- `out/review-fixes/check.json`: documented Ubuntu `dev.py check` passes input
  audit, CI contracts, system C bootstrap and ordinary regression. Its 314 tests
  include 165 unselected evidence skips, so this is repository-check scope,
  separate from required matching-product strict acceptance. The last traversal
  adjustment will receive a fresh ordinary check after the source freeze.
- Frozen source candidate: `f805cbf65bb473b17f199af075136a814edecbc4`;
  `codex/m1-integration` was already up to date at freeze. Matching actual Full
  `41511643549` was dispatched on that exact product commit, independently of
  the evaluator-only refinement below.
- `out/review-fixes/frozen-local/{build,run,package-consume}.json`: the f805
  producer passes, all 98 public native cases / 1000 lifecycle cycles pass, and
  the eight-case external public SDK package workload passes. Local metadata
  honestly reports `working_tree_dirty=true`: Ubuntu Git sees the existing
  Windows checkout's Markdown CRLF differences. Windows Git is clean; no
  unrelated Markdown or Git configuration was changed to hide this. The
  read-only `candidate-product-files.json` separately verifies all 57 product
  files against exact f805 Git blobs, with `changed: []`.
- Frozen local SDK is
  `a591e77bd3b534d8bf107f43a743925548c4838a89fcc735273557deb4c1a2b8`,
  recipe `efb2562732e4b138737b9c24a18f13ee837bcd3397fa86dde8528a2b973a2c63`;
  these local bytes are not mixed into CI acceptance. `frozen-check.json`
  passes 315 ordinary tests (165 explicit-data/platform skips), CI contracts
  and actual system C bootstrap.
- `out/review-fixes/mapped-guest-red.log`: a mapped selection without transports
  exposed the remaining standalone guest path. The evaluator now supplies its
  explicitly selected mapped roots to the same early guard. This changes only
  `sdk_guest.py` and its public CLI tests, outside all 57 product recipe files;
  f805 product execution reports remain original. All five tests / 27 cases pass
  in `mapped-preservation-green.log` (6.196 seconds). The final strict run must
  record this refined evaluator/test source identity separately from f805.

## Implemented boundaries and review refactors

`guard_evidence_outputs` runs before output-directory creation and any consumer
execution. The strict runner protects its generated evidence directory too;
Core, SDK run/matrix/package-consume and standalone SDK guest import/export share
the same metadata boundary. Export checks report, payload manifest and selection
together. SDK build semantics remain separate.

The additive ABI 1 operations are `qaplet_run_entry_with_options` and
`qaplet_eval_with_base_and_options`. Old calls delegate with NULL options. Both
use the existing structure/owner/state checks and execution ledger. Delivered C
tests additionally cover defaults/zero tails, cache reuse, invalid/over-max
options without result/cache/storage changes, PENDING/stop/close rejection
priority, foreign owner and callback reentry, and tightened host-wait deadlines.
The real external application demonstrates module options in its deadline mode.

Result copies, rejection records and module/registry storage now use
`qaplet_native_alloc/free` for one admission/current/peak/locking/first-cause
implementation. The separately metered retained source cache keeps its original
allocation/free path. Host copy, public names, compile steps/objects, link objects
and recipe membership derive from `HOST_UNITS`; the textually included value
consumer remains shipped/hashed and is not compiled twice. These are review-stage
refactors: existing public resource, failure, ownership and distribution tests
verify behavior; no structural tests were introduced.

## Acceptance boundary

The reviewed product `21bd317f4246571f0048789ca97167632c09f924` / Full
`41489263809` is a baseline fixture only after these recipe changes. It must not
be relabelled as acceptance of this candidate. Ubuntu builds use the pinned f07
source/compiler and main physical `.cache`; other platforms consume supplied
bytes. No CI join bypass, synthetic CI identity or old M0 acceptance is allowed.
Source candidate and matching actual Full/strict results will be recorded here.
