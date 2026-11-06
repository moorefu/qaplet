# Input review fixes and repository-cache validation

Date: 2026-11-01 (Asia/Shanghai). Worktree: `out/implement-spec/input-review-fixes`,
branch `codex/m0-input-review-fixes`. The clean starting HEAD was `cf0d125`;
`git merge-base --is-ancestor cf0d125 HEAD` returned 0 before editing.

This is input-layer repair and validation only. It does not close the remaining
M0 Core build, runtime, host, page-size, SDK or evidence-gate requirements.

The already confirmed seams are public supplier CLIs, actual external commands,
OS behavior and structured evidence. Tests never import supplier internals.
All corruptions and permission failures use isolated synthetic fixtures; accepted
locks, licenses, receipts, original archives and accepted caches were not edited
by this fix pass. The integration owner separately manages actual-cache copying.

## Standards

Supplied review: **2 hard findings and 1 judgement call; all 3 resolved**.

1. **P2, failed QuickJS publication invalidated the accepted metadata.**
   The prior `third_party/quickjs/README.md` contract says, “A failed candidate
   does not replace the accepted lock.” Its recovery procedure restores a
   reviewed metadata commit and verified cache together. Red: a local upstream
   fixture advances to a different LICENSE; Windows marks its previous lock
   read-only. `bump` exits 1 and preserves the lock, but changes LICENSE bytes.
   Green: changed metadata is atomically staged, the lock is the commit point,
   and failed publication restores previous metadata bytes. The same test checks
   all previous bytes and successful offline `verify` with empty PATH.

2. **P2, abbreviated curl overrides could claim authenticated origin.**
   `toolchains/README.md` requires “successful authenticated HTTPS from the fixed
   official URL.” Red: otherwise valid receipts containing `--insec`,
   `--proxy-insec`, abbreviated `--conf`, `--next`, another positional URL or
   `--url` were accepted as official. Green: an explicit full-option grammar
   rejects unknown/repeated options, short clusters, protocol/config overrides
   and extra transfers. Real historical/current command forms remain accepted;
   GitHub-provider receipt validation and local-evidence trust context remain
   unchanged. Receipt regressions run actual fixture version commands only on
   accepted forms; rejected forms run no tools and create no lock.

3. **Possible Duplicated Code, judgement call with a real timeout defect.**
   QuickJS duplicated `foundation.command` using `subprocess.run`, which killed
   only the root. The spec asks that “探针超时有界并清理资源”. Red: a one-second
   Windows Git command with inherited child output pipes took 8.16 seconds;
   WSL returned promptly but its descendant survived and wrote a marker.
   An elapsed-only WSL test initially passed, so the retained regression also
   checks the descendant's observable effect. Green: QuickJS uses the shared
   process-tree cleanup; the helper gains only keyword-only `cwd`/`env` support.
   Both Windows and WSL retain raw output, explicit neutral Git cwd and Git
   environment flags, return within the bound, and prevent the descendant marker.
   Nightly already uses this same shared runner.

No additional outstanding Standards finding was identified in the scoped manual
diff review. These are separate counts from the Spec axis below.

## Spec

Supplied review: **2 hard findings resolved**. Additional actual-CI defect:
**1 hard finding resolved locally**. Total: **3 resolved; 0 known outstanding**.

The parent spec requires results to include “结果及原因”, and says
“通过必须有实际执行” and “用例失败是fail”. A failed invocation must not leave
an earlier successful report as its apparent result.

1. **P2, `.` source-manifest path bypassed report finalization.** Red: the
   manifest had a valid canonical digest but path `.` or `./`; CLI exited 1 with
   IndexError and retained a preexisting pass report. Green: empty, dot, parent,
   trailing/repeated-separator and nonportable components are rejected before
   indexing. Each malformed-path invocation writes a fresh fail report and runs
   no commands.

2. **P2, encrypted ZIP entry bypassed report finalization.** Red: ZIP encryption
   bits caused zipfile's password-required RuntimeError; CLI exited 1 while a
   previous pass report survived. Green: encrypted members are rejected during
   archive preflight, before payload extraction; a fresh fail report is written,
   no tool is run, and no lock/cache is published.

3. **Actual CI, aliased extraction parent rejected a safe internal link.**
   Integration commit `cf0d125`, run `41254667473`,
   macOS-15 job `124695186181`, failed contract regressions with
   `Archive symlink escapes extraction: bin/cosmoc++`; the other three reported
   jobs succeeded. The toolchain contract permits relative links with validated
   resolution inside the tree. Red: a real local directory alias reproduces the
   same CLI failure (the macOS `/var` to `/private/var` shape). Green: canonicalize
   the extraction root before comparing link destinations. Alias-parent import
   and offline verify pass. Existing escaping, cyclic and link-parent archive
   refusal tests remain intact. A new hosted macOS result is still required;
   this record does not claim one.

## New storage policy and measured DrvFS behavior

The user subsequently required all actual input/cache data under the current
repository's `.cache/`, with builds on Ubuntu and other hosts consuming artifacts
and SDKs. Supplier defaults now select `.cache/quickjs` and
`.cache/toolchains/cosmocc`; explicit paths remain for isolated fixtures. The
integration owner handles environment/build defaults and CI orchestration.

The main repository D: mount is WSL 9p/DrvFS without POSIX metadata. The measured
root probe (`out/m0-integration/cache-capabilities.json`) found working links and
execution, file chmod 0444 reported as 0555, and directory chmod 0555 reported as
0777. No global mount/WSL setting was changed.

Red: exact-lock recovery of a tiny POSIX-locked fixture under an isolated main
`.cache/` directory failed with `Restored compiler/extracted content differs
from lock`. Green: a detected DrvFS mount gets a disposable sibling chmod probe
and an explicit `filesystem_permissions` report. `drvfs-no-metadata-v1` retains
the original ZIP/lock execute identity and records unrepresentable execute and
directory no-write bits. Regular-file no-write remains mandatory; inability to
enforce it blocks verification. Capable POSIX filesystems retain full checks.
Archive length/digest, complete file bytes/types/entry sets, link targets/safe
resolution and actual tool execution remain required. There is no ignore flag
or fallback for unrecognized filesystems, and accepted lock bytes are unchanged.

The CLI regression also checks writable files, changed bytes, an extra entry and
changed link targets: each fails before commands. A subsequent red case showed
the obsolete universal WSL/VHD prerequisite; green scopes that prerequisite to
non-DrvFS cache storage. Actual DrvFS source/cache disk measurements are retained
without claiming the cache is in a VHD. Explicit backing-volume measurement
remains supported.

## Local checks

All build/compile validation in this fix pass ran on Ubuntu. Windows runs only
the supplier contract suites, including real local Git and OS permission tests.

- Initial Windows supplier suite: 31 tests, 11 platform skips; passed.
- Initial Ubuntu `make check OUT=out/input-review/wsl-check`: static checks,
  56 tests (2 platform skips), and real C bootstrap passed.
- Final post-DrvFS Windows suppliers: **32 tests, 12 POSIX/platform skips; pass**.
  Ubuntu aggregate: **57 tests, 2 platform skips; pass**, static checks and real
  Ubuntu C bootstrap also pass. Outputs are retained in
  `out/input-review/windows-suppliers-final.log` and
  `out/input-review/wsl-check-final.log`; reports and raw bootstrap logs live
  under `out/input-review/wsl-check-final/`.
- A subsequent performance cleanup avoids resolving each regular file's full
  absolute path repeatedly on DrvFS. All 18 Nightly contracts pass after that
  cleanup, including the main-cache DrvFS regression.
- The main-cache fixture is selected explicitly with
  `QAPLET_TEST_CACHE_ROOT=/mnt/d/Workspace/dairn/qaplet/.cache`; it is temporary
  and does not touch accepted inputs.
- `git diff --check` passed. Actual input locks/license/patch manifest and the
  GitHub acquisition receipt have no diff against the fixed starting point.

The historical input validation records and committed `toolchains/validation.json`
remain unchanged: their original source identities and evidence bytes are not
rewritten to match a new implementation or storage location.

## Actual migrated input verification

After the integration owner's per-file-verified migration completed, read-only
verification ran against the **main** repository cache and authoritative locks:

- QuickJS `verify --cache D:/Workspace/dairn/qaplet/.cache/quickjs` passed on
  Windows and its `/mnt/d/...` equivalent passed on Ubuntu. Reports:
  `out/input-review/actual-repository-quickjs-windows.json` and
  `out/input-review/actual-repository-quickjs-wsl.json`.
- Ubuntu Nightly `verify --cache-dir /mnt/d/Workspace/dairn/qaplet/.cache/toolchains/cosmocc`
  passed against the original GitHub-provider lock, matching the original
  archive, full tree/link metadata and all three compiler versions. Report:
  `out/input-review/actual-repository-nightly.json`, with separate raw command
  logs and explicit measured DrvFS limits. This invocation began before the
  small manifest performance cleanup; its implementation digest is retained
  honestly in the report.
- No accepted cache was refrozen, repaired or rewritten. The mode probe uses a
  disposable sibling outside the accepted content-addressed cache. No original
  archive or acquisition receipt was changed.

Unchanged metadata SHA-256 values:

| File | SHA-256 |
| --- | --- |
| QuickJS lock | `5b8b1a94c4321e2c806ad640ead17c19bd5e1db01b5231f5197362ce8b2349d1` |
| Original QuickJS LICENSE | `598fd7fc928e4350abce36e337ba5a1346923c5c692f5be92c3d8e29ddd7c18d` |
| Ordered patch manifest | `671e09884c6aecf658799b947d15de92871f7c44c78ac7f6ca42e7a4f6cbfaec` |
| Nightly lock | `0617d6536f4dcf027889c1bd8c191bf34944c89fb3dd227aee07e9bb3c3f8469` |
| GitHub acquisition receipt | `6bf1a137ffb96afca109aa7cd3c62fde3e87256d8083350189fa7242c0ae2a7d` |

## Integration handoff

Merged integration policy tip `0a8065c` after the input-fix commit `8cf6d05`.
The merge had no conflicts. Its foundation changes are the Ubuntu build guard
and repository-local environment cache default; the input fix's shared command
cwd/environment support remains intact. The updated AGENTS/development policy
was read, and `git diff --check` passed. No broad suite or large-cache verification
was repeated for the conflict-free merge. The integration owner reported its
Ubuntu policy checks green; ticket 05 consumes the current verifier again in the
canonical Ubuntu build. No hosted macOS rerun or full M0 completion is claimed.

## Independent re-review

Both reviewers inspected the pinned fix tip `92f01db` against `cf0d125` after integration merge `10214fe`.

### Standards

0 actionable findings. The two documented contract breaches and the process-runner duplication with a demonstrated timeout defect are resolved. The reviewer checked the rollback, complete curl grammar, shared bounded cleanup and measured DrvFS branch, including retained regression and actual-cache evidence.

### Spec

0 actionable findings. Both stale-success-report paths now produce failure evidence; canonical extraction roots handle the macOS alias-parent case. Full content/type/link validation and regular-file no-write checks remain on DrvFS, with strict POSIX permission checks elsewhere. An optional restricted-context WSL rerun was unavailable to this reviewer; this does not change the recorded ordinary-user Ubuntu checks or actual-cache verification.

Final review counts: Standards 0; Spec 0. No compiler was run on Windows or macOS for this re-review.

## Ubuntu build and repository cache policy

The user subsequently required Ubuntu builds, artifact/SDK consumption on other platforms, and one repository-root `.cache/`. Commit `0a8065c` records this in AGENTS/development/spec, restricts bootstrap scheduling to Ubuntu, blocks non-Ubuntu bootstrap compilation before compiler selection, and ignores `.cache/`. The new CLI policy checks were observed red then green; Ubuntu `make check` passed 52 tests with two platform skips, static checks and actual Ubuntu system C bootstrap. Evidence is `out/m0-integration/checks/ubuntu-policy-{red,green,check}/`.

Main-workspace reusable source, the original Nightly ZIP and extracted toolchain, guest inputs/unpacked data, prior environment data and project build caches were migrated into `D:\Workspace\dairn\qaplet\.cache`. Every copied regular file was compared by SHA-256, and symlink targets/types were preserved. After actual QuickJS/Nightly verification passed, a second content inventory comparison preceded removal of each old project cache directory. Old `/home/moore/.cache/qaplet/` entries now contain compatibility links into repository `.cache/`, retaining historical evidence paths while storing the bytes in the requested directory. The completed migration record is `out/m0-integration/cache-migration.json`; measured filesystem behavior is `out/m0-integration/cache-capabilities.json`. Shared pre-existing user tools outside this project's cache were not moved.

The canonical-cache Hello/eval build `80c4e13b-17e4-4f39-9cee-13f1415358c0` produced the same two fat bytes as before migration. A Windows consumption-only preflight matched both producer hashes before/after all five commands; Hello/facts/default eval passed, and thrown/syntax errors produced the expected exception text and nonzero raw Windows status 256. This preflight is retained at `out/m0-integration/windows-startup-preflight/`; formal task 06 matrix evidence remains separate. Task 05 still needs its full native regression and actual Ubuntu CI build/upload acceptance.
