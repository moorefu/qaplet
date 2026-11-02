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

## f805 actual evidence and the subsequent alias correction

Full `41511643549` on f805 failed SDK guest import because the raw serial lacked
two entire frames: `qaplet-host.stop-ordering STDOUT` and
`qaplet-host.diagnostics EXIT`. The stop-ordering raw START/END timestamps were
equal and EXIT was zero; QEMU exited zero without timeout. Its retained init had
both correct emit commands. The responsible emission/capture layer was not
proven; no Runtime or strict-parser change was made. All nine originals
(100,871,119 bytes) remain verified in MAIN
`out/m1/ci/review-fixes-first-all-collection.json` / `review-fixes-full1/`.

`out/review-fixes/diagnose/` retains the exact captured-frame red loop, unchanged
five-line minimized trace, original-byte inventory and same-byte local controls:
one stop-ordering process, twenty repetitions and the complete 98-command SDK
payload all pass on the locked aarch64/65536 kernel. QEMU executable, kernel and
config hashes match the failed CI guest. Those reports have CI=null and never
replace the incomplete original. The real first-run negative preflight remains
failed at its nonpassing guest (`first-incomplete-preflight.json`). An initial
path-only derivation attempt mishandled native PE loader records without a path;
its missing-config preflight/setup attempt is preserved separately.

The unchanged-source retry Full `41514820432` genuinely completed successfully.
Its nine originals (101,070,392 bytes) are preserved in MAIN
`out/m1/ci/review-fixes-retry-collection.json` / `review-fixes-full2/`; actual
provider names are restored by ID from `review-fixes-retry-artifacts.json`.
Only those retry producer/package/native/guest/Core inputs were selected.

- SDK provider 13173499541; pagegap SDK + M0 guest 13176015165;
  Full outer 13174309542.
- SDK `3a4da395ca5c397bad226c0c047cb364ddaad97bdabc1edbdbb41b8bfaf0dcb1`;
  recipe `efb2562732e4b138737b9c24a18f13ee837bcd3397fa86dde8528a2b973a2c63`;
  sealed ZIP `1aa79c76926be65abfa2ffc1200c7c65ff37d302002dd7d6b6ae49117ceee849`.
- `out/review-fixes/final/preflight-ubuntu.json`: M1 and independent M0 pass;
  all five native targets have 1000 lifecycle cycles, actual SDK 64K covers 98
  commands, package C consumer covers eight, M0 Full covers 235. Required/optional
  gaps are empty and `release_ready=false`.
- `final/native-ubuntu.log`: all three real POSIX supplied-SDK reader contracts
  pass, preserving loader mode and bytes on successful and failed validation.
- `final/strict-windows.json`: the first complete run executed 91/91 with zero
  failures/skips and two fixture errors. A new provider ordering selected a
  Windows native report first, exposing backslash mapping sources in the derived
  selection. The two raw-log mutations could not find their clone. Original
  reports/provider bytes were unchanged. Only the new derived mapping source
  spelling was normalized to `/`, and all evidence/fixtures were preserved.
- `final/accepted-strict-windows.json`: the full fresh rerun genuinely passes
  91/91 with zero failures/errors/skips and source rechecks. Its path-only
  selection is `final/normalized-selection/`. Product snapshots before and after
  match all 57 f805 Git blobs. `final/prior-candidate-summary.json` preserves this
  real result under its original candidate scope.

A subsequent private-fixture probe on the documented Ubuntu DrvFS demonstrated
that `ORIGINAL.ZIP` can be the same file as selected `original.zip`, while
PosixPath lexical resolution retains different spelling. The f805 guard therefore
still replaced that clone. This is the same original-material preservation
finding; the genuine f805 results above cannot close the newly demonstrated
alias gap and must not be relabelled after the recipe changes below.

`alias-red.log` retains twelve real clone-only failures: four case-aliased provider
roots, four archive hardlinks and four archive case aliases, including nested M0.
The shared guard now adds existing file identity and ancestor directory identity
checks, caching each stat. Explicit same-file aliases and case-aliased provider
roots are refused before any output creation. Only private fixture copies are
hardlinked; no provider original is ever a mutation target.

`alias-green-ubuntu.log` and `alias-green-windows.log` pass all six public tests /
39 CLI cases on both actual Ubuntu DrvFS and Windows. Case-sensitive filesystems
use a hardlink for the archive alias and the canonical provider root as an
explicit control; actual case-alias presence is recorded in each receipt. A new
matching product Full and complete 92-test strict catalog remain required after
this recipe-listed guard correction. The parent specification stays claimed.

The corrected alias candidate's local SDK build and all 98 supplied public
Host/CLI/application cases (1000 lifecycle cycles) pass at
`out/review-fixes/aliases/local/{build,run}.json`. The documented Ubuntu ordinary
check passes 317 tests (165 unselected-data/platform skips) plus real system C
bootstrap at `aliases/check.json`; ordinary skips are separate from acceptance.
The completed f805 `out/review-fixes/final/` tree and sibling `final-*.log` files
are closed permanently for writes and may be losslessly archived by the parent.
Corrected matching acceptance will use a new `out/review-fixes/alias-final/`
directory, preserving all previous records and original source identities.

## Independent m1r4 follow-up and preservation completion

The independent reports at MAIN `out/reviews/m1r4/` reviewed exactly b92 against
beab. Spec returned zero findings. Standards returned two hard P2 findings
(unprotected offline recipe comparison and ignored path mappings) and one P3
heuristic (overlapping SDK guest collision policies). These remain distinct axes;
all three Standards findings are addressed by this correction.

`m1r4-red-{windows,ubuntu}.log` retain ten actual public CLI failures: three
omitted selection files, six standalone/nested M0 mapped paths and one unrelated
SDK output wrongly swallowed by a mapping root. The finite documented selection
schema includes loader_manifest and package_consumer. One shared EvidencePaths
resolver now supplies both the metadata guard and actual Evidence reader, with
each nested M0 base, relative configuration bases and longest-prefix mappings.
The four intermediate direct-base fixture failures remain in
`m1r4-direct-base-red.log`; final public green reports supersede them.

The adjacent loose-log audit produced three additional actual red cases on both
platforms at `m1r4-raw-red-{windows,ubuntu}.log`: overwriting an already-read raw
log, a later unread raw log, and a strict output on incomplete preflight. All
mutation targets are private copies. A shared final writer now checks exact
observed evidence paths and preserves any pre-existing destination when
validation fails or is incomplete. New report paths retain running/progress and
failure records. Terminal JSON explicitly identifies retained bytes through
status=fail/report_written=false and a nonzero exit; successful unrelated output
replacement remains supported. The intentional behavior is documented in the
evidence contract. Standalone SDK guest uses the same final writer; its redundant
local equality and broad mapping-root checks have been removed.

Final public preservation plus loose-log regressions pass all ten methods on
Windows (8.101s) and Ubuntu/DrvFS (23.830s), with logs
`m1r4-public-green-{windows,ubuntu}-final.log`. Ubuntu ordinary validation at
`m1r4-correction/check.json` passes 322 tests with 167 explicit unselected-data or
platform skips plus actual system C bootstrap; the FIFO progress contract passes.
Those ordinary skips are separate from required acceptance.

The first standalone guest focused setup used a nonexistent selection and skipped;
that log is retained. The corrected selection then correctly rejected the old
f805 SDK recipe before the new loose-serial positive branch. Its six failures in
`m1r4-guest-green-windows-2.log` are mismatched-product fixture attempts, not
semantic acceptance. The new guest regression and unrelated-existing-output
positive control require the forthcoming matching producer. The full required
catalog grows from 92 to 97 methods.

Prior-candidate b92 Full 41524394334 genuinely completed success, with all nine
original ZIPs independently verified by the parent under MAIN
`out/m1/ci/alias-fix-full/`. These results retain b92 scope. This correction changes
recipe-listed workflow_support.py, so new matching Full and strict acceptance
remain required. Closed `final/` and prior `alias-final/` data are unchanged;
current correction attempts use `out/review-fixes/m1r4-correction/`, and matching
acceptance will use a new sibling. The parent specification remains claimed.

The finite observed-index collector was itself checked at the public CLI: an
invalid selected M0 object containing evidence_index=null caused two real red
failures in unrelated-output controls. `m1r4-untrusted-index-red.log` preserves
them. Only the actual top-level reader index and independent M0 reader result
are now consulted, never similarly named fields in input metadata. Final source
checks at `m1r4-source-green-{windows,ubuntu}.log` pass 11 Windows / 12 Ubuntu
methods, including the existing fresh-failure and POSIX FIFO contracts. The final
ordinary check is `m1r4-correction/check-final.json` (322 tests, 167 explicit
ordinary skips, actual C bootstrap). Terminal retention also exposes the original
validation reason and progress path; its final focused check passes.

The final recipe was rebuilt and validated at
`m1r4-correction/local-final/{build,run,package-consume}.json`: build pass, 98
public Host/CLI/application cases, 1000 actual lifecycle cycles, and eight actual
public C cases compiled and audited outside the source tree. Guest export passes
both actual CLI tests at `m1r4-export-final-ubuntu.log`. The final local SDK SHA is
bc0e87e95181f609fd99716b2ad67f9e7e7021efc29f2423394f3045973f5a7a;
recipe 443ca66ee13cfaa740c4a9f6b804acb0dd5f770a64b81ef08178c6ae0856d212.
This is honest provisional local evidence created while HEAD was b92 with these
working changes; it is not a producer for the forthcoming frozen commit. Earlier
provisional local build/run/package/export results remain separately under
`m1r4-correction/local/`. No original provider report or archive was rewritten.

## Independent m1r5 follow-up

The separate MAIN `out/reviews/m1r5/` packets reviewed beab through160ea7b and
confirmed all m1r4 paths resolved. Standards reported a hard P3 absolute-deadline
overflow refusal that changed module identity/storage and a heuristic P3 for
duplicated preservation test subprocess/receipt code. Spec independently reported
P2 for standalone SDK matrix raw logs selected through input-report alone. These
three findings retain their original axes and priorities.

An independent real public matrix probe converged with Spec: actual copied local
native evidence passed its unrelated-output control, then the collision returned
status=pass/exit0 and replaced a private stdout log with JSON. The original source
log retained its SHA. `sdk-matrix-audit/probe.json` preserves that red. The new
registered raw-root test was red on both OSes; SDK input-report metadata now binds
its exact evidence/<report_id> directory before any output. All SDK consuming
operations use the shared observed-input/incomplete-validation final writer;
build remains a separate producer. The real Ubuntu copied-evidence probe is green
at `m1r5-correction/actual-matrix-green/probe.json` (control0, collision2, all
original/private bytes intact). Existing SDK reader tests retain separate output
files for each successful and rejected validation.

The public C SDK red at `m1r5-correction/deadline-red/run.json` executed all98
contracts and failed exactly module-budgets. After INVALID_ARGUMENT for an accepted
boundary timeout, module_count changed2→3, native current1972→2348 and native
peak1975→2839; result42 remained available. Full raw stderr and all other results
are retained. The test also covers NULL, zero and explicit boundary options on
entry, explicit-base eval and ordinary eval, comparing the entire public snapshot
and prior result before valid tightened controls. Default controls remain in the
same packaged public test.

The execution driver now prepares the absolute deadline and dispatch budget
without mutation before input storage or module resolution. Begin commits that
same prepared budget; base eval passes it into the shared evaluator, and idle
event pump preparation uses the same rule. The shared ledger still spans pending
jobs/host waits and subsequent pumps. The public ABI is unchanged.

Preservation tests now share the subprocess/log writer and hash/receipt/refusal
assertion, retaining every alias/root and unrelated-output control; no structural
test was added. Behavioral suites pass all ten methods on Windows and Ubuntu,
with logs `m1r5-refactor-green-{windows,ubuntu}.log`. The affected ordinary Ubuntu
check passes323 tests with167 explicit ordinary skips plus actual C bootstrap at
`m1r5-correction/check.json`. Matching required acceptance now has98 methods.

160ea7b Full41533243382 genuinely passed; all nine originals101075206 bytes are
verified under MAIN `out/m1/ci/selection-fix-full/`, with the original expired
acquisition attempt also retained. This is prior-candidate proof because current
C/recipe bytes changed. Its97-method strict suite is not run or relabelled as
current acceptance. `corrected-final/` remains a prior-candidate prepared tree;
the next matching closure will use a new sibling and the parent specification
remains claimed.

The corrected m1r5 local SDK at `m1r5-correction/local/` passes build and all98
public execution contracts, including every new overflow refusal and valid
control, plus1000 actual lifecycle cycles. The external package consumer passes
all eight public C cases. `readers-complete/report.json` and its raw log pass all
three actual local POSIX reader/hash/mode contracts and both guest export CLI
contracts (5 tests,24.679s). An initial ignored replay helper chose the wrong
toolchain directory and failed before testing; its separate
`readers/setup-failure.json` is retained. No product source changed to accommodate
that setup correction.

The local SDK SHA is e7a92fb2a36569cd55eb0f5382a869c360cdd50e522b2abf9b2b9d6d0f4c6754,
recipe2489d34a07b5a86c1efa5c899c6b2f9ffe6173ef5dd89aed2b56ca02948f64ff.
These local reports honestly retain160ea7b/working-change producer context with
CI=null and do not replace the next frozen-commit provider closure. All source
code is now ready for its next freeze; matching Full,98 strict methods, matching
POSIX replay and independent reviews remain required before final clearance.
