# 05 · Frozen modules and strict module-name/path policy

Completed on `codex/m1-05` after merging integration `d792f4b` (events).
Final implementation source is `c6d41f253914ec46e58375cf59d497c2bbff54b2`.
All ticket 05 verification is complete. No complete M1 matrix or release-readiness claim.

## Exact input and selected probes

The original Bellard commit `2833510432045f72915919cef680ba59e5153b16`,
tree `b2763aa1a9395672adc131322f17d7fad7383053`, first ARM64 patch and
fixed cosmocc archive are unchanged. The second patch rejects static/computed
module-name NUL before C-string callbacks lose the length; ordinary JS strings,
property atoms and SDK copied strings retain NUL semantics. New patch SHA is
`5c559b810cd1e280ec3426ad18f64f6ea49068a9edfd8712ed1e9b1ba1317b66`.

Actual source cache: main `.cache/quickjs/2833510432045f72915919cef680ba59e5153b16-d418e131993e6539/source`.
Patched inventory `e4dee17eb0a1bb1c109b96f5236cf26f8ed346f9075c91d4ce3265f6ab85b805`;
engine compatibility `ca0f112ee82774eaa48aa1d2e646f6fb13d7862119e4797d819009f572307ad6`.
New input_id `f07f23c64bfcfb4a92722723e3e673213534a6fa81e690c401bd9fa10e0fd178`.

Fresh official Git fetch failed through WSL HTTP2, WSL HTTP1.1 and Windows
schannel; `out/m1-05/source-select*.json` retains each actual failure and raw
commands. With explicit parent approval, `offline-source-reconstruction.json`
records local transport from the fully verified original cached Git objects,
independent original Commit/Tree/full upstream inventory verification, ordered
patch application, and actual new manifest generation. No old cache was edited;
no successful new official acquisition is claimed. The original source lock
is retained under `third_party/quickjs/history/`.

`input-migration.json` records real v3 migration to selected; the selected
original remains `toolchains/m1-module-name-candidate.lock.json` and
`out/m1-05/selected-original.lock.json`. Ordinary selected SDK build and run
both reject before execution (`selected-ordinary-refusal/`).

`green-nul-controlled-v2/build.json` and `run.json` retain committed source
`a67995787168edf5facbd5f0354c4d8c41351c11` and 23/23 actual Ubuntu Host cases.
SDK recipe `f1e4c1f318a81219a1aa0eb35cdeb091ec7bd7c2ed796acafac1f386c5d6de32`,
SDK aggregate `5b0fdd0277528b9508a8e1c36f49e37a9a2586ad3f490e0d7b7cf1f8d1494f4a`,
Host APE `4e32ba5b699b62241fd59b72270a77f2e9b7fcd58c1f474c8da4d495ce46ff02`.
The NUL red original (`red-nul/`) imports the existing baseline incorrectly;
green covers static `\0`/`\u0000`, computed NUL before/after valid filename,
caught rejection, and successful ordinary copied NUL string.

Parent executed the same 23 cases on normal-user Windows x64 native PE
(`green-nul-controlled-v2/windows-frozen-run.json`) and full-system emulated
ARM64/65536-byte pages (main `out/m1/05-guest-controlled-nul/run.json` with
selection/payload). Exact `a679957` source was exported and every recipe file
verified against the producer because the working recipe advanced. The earlier
Windows recipe mismatch remains a refusal, not a relabeled pass.

Existing controlled M0 producer/startup pass 17/17 each in `m0-controlled/`.
Parent's main `out/m1/05-m0-controlled-guest/{export,payload,run,startup}.json`
records actual ARM64 64K 17/17 plus independent startup import. These actual
selected receipts are individually hashed in `promotion-inputs.json`; only
canonical lock_state changes to pinned. Fresh ordinary evidence remains required.

## Cache and path slices

`cache-controlled/{build,run}.json` passes 24/24 through the supplied public
SDK/Host. Temporary actual files prove successful source freeze/deletion,
load/parse/link/eval/TLA failure freeze, caught dynamic link failure before and
after file repair, fresh Runtime repair, retained partial effects, canonical
failure accounting and no new charge on cache hits. An unresolvable missing
request consumes no identity and can discover a newly created file later.

`red-policy/` retains a real Host compilation failure: this supplied libc has
`mknod` but no declared `mkfifo`; the fixture was corrected using the public
libc header. `red-policy-v2/` is an audited behavioral red: the extra-slash file
URL loads the existing module. All other 25 Host cases pass there, including
actual Ubuntu symlink normalization, frozen retargeted alias and distinct hard
link identities. Mutable fixtures never modify delivered SDK fixture bytes.

## Ordinary source and integration verification

Canonical pinned lock SHA is
`eb0c7dcdb401ca3739f9e3ce863604e486b526d534579417d17b3e05b9da32f9`
(8,249 bytes); its input_id remains the selected input's exact value. Integration
`33ca543` (Host modules) was merged as `64f3d45`; both capability bits and both
public Host catalogs were retained. File path filtering follows exact registered
Host-name lookup, preserving the separately approved UTF-8 Host-name contract.

`ordinary-v1/` retains the new ordinary producer and a truthful 27/28 run: device
paths containing a drive colon were rejected as UNSUPPORTED rather than the
expected MODULE_RESOLVE. The classification was fixed in committed `122dff5`;
no engine/source/patch/toolchain/M0 recipe identity changed. The updated public
policy test also imports an explicitly registered `host:literal?name%2f`, proving
file URL rejection does not narrow Host identifiers.

`ordinary-check.json` and `ordinary-v2/check.json` pass `dev.py check`, including
actual system C bootstrap. The ordinary suite ran 219 tests with 75 existing
opt-in skips; those skips do not count as module or platform proof.

Fresh `m0-ordinary/build.json` and `startup.json` pass 17/17 each after pinning.
The same source/patch and fixed original supplier are independently verified
before and after build. Fresh ordinary Hello SHA is
`467f8889db7a671cd238a7cc56df933951fea686c7c524480f4112870da3695d`;
QuickJS eval SHA is
`ad1074c24cdb7b66fc5bddf30d5fcdac7d213140083b32c54eb29a3f9ae59efc`.
These are new runs and reports, not relabeled controlled probes.

Windows drive/case/hardlink is a required separate Host case. The file-symlink
case is actually attempted on Windows; an observed unavailable fixture exits 77,
is recorded `blocked, required=false`, and never becomes a symlink behavior pass.
Linux requires that actual case. POSIX FIFO is exercised on POSIX; Windows has
no claimed FIFO proof. A parent's independent normal-user Windows PowerShell
fixture passed hardlink identity and reported administrator privilege required
for file symlinks; main `out/m1/windows-links/b27d3262bd6c468b8ca2d693baa7f69a/report.json`.
The actual supplied C Host result remains authoritative for its own fixture.

`ordinary-v2/` retains a separate real fixture failure: every product policy
assertion passed through the actual FIFO creation step, but the supplied libc
`mknod` returned ENOSYS (38). A bounded standalone C diagnostic in
`fifo-diagnostic.json`, its `.c` and raw logs confirms that setup limitation; it
is not classified as a product refusal pass. The fixture now uses shell-free
fork/exec of native `/usr/bin/mkfifo` on Ubuntu or `/bin/busybox mkfifo` in the
retained guest, requires successful child exit, and independently checks S_ISFIFO
before invoking the SDK. Missing helpers fail the case. `policy-tracer.json`
records a successful public-only C Host link/run against the unchanged supplied
ordinary-v2 SDK archive, including actual FIFO rejection without hanging. It is
a narrow tracer, not the final fat-APE producer or cross-platform acceptance.

## Native Windows fixture diagnosis

The immutable ordinary-v1 Windows preprobe and two-repeat tight loop reproduce
alias cleanup failure and a relative-symlink resolution failure. The exact
original reports remain main `out/m1/05-windows-policy-preprobe/` and worktree
`out/m1-05/windows-alias-loop/6aa67beb8bdd4a7ca1ac5f38c02985e5/report.json`.
The fixture had selected Windows behavior from the SDK URL under test; native
`uname` instead reports `Windows`. Cosmopolitan's virtual `/tmp` returns a native
realpath of `/tmp/...`, so it was an invalid fixture oracle for a local-drive
case. Using native `TEMP` produces `/C/Users/...` and SDK
`file:///C:/Users/...`, with uppercase drive as required. No resolver fix is
inferred from the invalid virtual-temp oracle.

Narrow diagnostics under `windows-alias-diagnostic-v{2,3,4}-run/` bind real native
facts to the unchanged supplied SDK archive. The actual Windows `realpath` keeps
requested `VALID.JS` spelling, creating a distinct canonical URL from `valid.js`;
the SDK follows that authority and evaluates that distinct identity, without
inventing global case folding or inode deduplication. The public case now
compares snapshots and side effects to separately observed OS realpath facts.

The actual C symlink call returns zero, but `lstat` returns -1/ENOENT and the
immediate native `GetLastError` is 1314 (privilege not held). Thus no link exists
for an SDK behavior claim. The updated Host verifies native link existence and
retains this explicit unavailable fixture. Its observed Windows raw process
exit is **19712** (`77 << 8`), not an invented raw 77. The consumer keeps that
actual exit and records `blocked, required=false` only for this Windows fixture;
all required Windows path/drive/hardlink/cache/NUL cases remain mandatory.

`windows-policy-tracer-v2-run/53e084b5fbb44800839882c1eebcaa3c/report.json`
passes the four required policy modes against the unchanged supplied SDK, while
retaining the symlink blocked result with errno, lstat, WinError and raw exit.
Production/test sources contain no `[DEBUG-05-alias]` instrumentation; the tagged
throwaway diagnostics remain clearly named under ignored `out/` as retained
raw investigation artifacts. The final ordinary fat SDK is rebuilt separately.

## Final ordinary producer

Committed source `eeaadc933344185248dd940493ca3e7672f809d6`, including integration
`33ca543`, produces `ordinary-v4/build.json`. The final merge check was already
up to date. `ordinary-v4/run.json` passes all **28/28 required** public C Host
cases on Ubuntu x64/4096-byte pages with no missing fixture or supervisor timeout.
`ordinary-v4/check.json` passes the repository check again. Actual symlink target
format rejection and a real FIFO are executed; no directory or missing file
stands in for the FIFO proof.

| Identity | SHA-256 |
| --- | --- |
| SDK recipe | `7c1d0cbc347ca11b2b109f3f1cfb96d05fb3e17471e5ebb04300003df18ea7ee` |
| SDK aggregate | `ccd3eda8843430216ddd14489633e07f57a7bad93eb1434ad45ba375195a4b19` |
| Host APE | `cf47fe4ce3110af75b24350edbbc892be34cdb97ffb3da86e5c7036655ff7baa` |

The SDK's supplied component hashes are checked before and after each consumer;
mutable per-process fixture copies do not alter delivered `sdk/modules` bytes.
The final READY pointer was sent to the parent and the recipe remains frozen
through Windows/ARM64 execution. Those matching results and the new-input M0 Full receipt
are recorded below, with their exact scopes and original report identities.

## Final supplied-byte platform verification

The parent ran the unchanged final ordinary SDK/APE on normal-user Windows x64
native PE/4096-byte pages: `ordinary-v4/windows-user-run.json`. All **27 required
cases pass**; the separately attempted file-symlink fixture is explicitly
`blocked, required=false`, with actual raw exit 19712, errno 2, symlink result 0,
lstat -1 and native WinError 1314. It is not counted among successful cases. The
SDK and every delivered component are revalidated after the run.

The exact same final Host APE and supplied module fixtures pass **28/28** in the
parent's full-system emulated ARM64/65536-byte guest: main
`out/m1/05-guest-ordinary-v4/{selection,payload,run}.json`. The independently
measured guest is Linux aarch64 kernel `6.8.0-147-generic-64k`, page size 65536
and smaps 64 KiB, distinct from the outer x64 supervisor. Selection binds the
exact pinned input, SDK and recipe identities above; guest file hashes before
and after match. Actual symlink and BusyBox-created FIFO cases both execute.
No required case skipped and no supervisor timeout occurred.

The agent independently checked the producer/input/SDK identity equality,
required Windows case statuses and all 28 guest case statuses from these raw
reports. A duplicate native Windows supplied consumer run is retained separately
as `ordinary-v4/windows-agent-run.json`; it is not additional platform coverage.

## New-input M0 Full gate history

The parent pushed exact input-validation source
`bcac83f05aa7957c8a9bfa8452ba8f4c7c7d5c92` to
`codex/m1-05-input-validation`. Its pinned input and frozen M0 recipes are
identical to the final SDK's supplier input; this separate gate does not claim
to identify or validate the later final M1 SDK by that earlier source commit.

[Full run 41427050870](https://github.com/moorefu/qaplet/actions/runs/41427050870)
retains two actual nonpassing attempts. Attempt 1 passes Ubuntu build and all
five native startup/I/O targets (Windows x64, macOS x64/ARM64, Linux x64/ARM64),
but guest supply fetch receives HTTP 502 for pinned noble-updates InRelease on
all three attempts. Actual provider archive SHA/CRC and raw logs remain main
`out/m1/ci/05-guest-artifact/` with `05-guest-collection.json`.

Attempt 2 passes guest 64K, all native targets and startup matrix, but the final
Core archive collector rejects duplicate/invalid artifact identity across rerun
artifacts. The parent verified the original full-core ZIP (artifact 13132939021)
and retained it with raw logs under main `out/m1/ci/05-full-core-failure/`.
Neither failure is relabeled as a Full pass; neither is inferred to be an SDK
product failure.

A clean Full run was dispatched at the same exact committed ref:
[41431796320](https://github.com/moorefu/qaplet/actions/runs/41431796320).
The clean dispatch preserves the collector's duplicate-identity guard. It completed
**success** on attempt 1 at exact head `bcac83f05aa7957c8a9bfa8452ba8f4c7c7d5c92`;
actual API receipt is main `out/m1/ci/05-clean-full-status-2.json`. Under the
repository CI triage contract this is qualified successful Full evidence for the
new input and frozen M0 recipes. Initial API and dispatch receipts remain main
`out/m1/ci/05-clean-runs-initial.json` and `05-full-clean-dispatch-receipt.json`.
The prior failed run and its raw artifacts remain unchanged. This proves the
new-input M0 scope, not a complete future M1 SDK matrix.

## Combined events integration checkpoint

While clean Full was running, integration advanced to
`d792f4bd0d0c9bdca8da9f84f013827aa553f889` (ticket 08 bounded events).
The completed ordinary-v4 platform reports remain unchanged as their exact
standalone scope. The new integration was merged as `c6d41f2`; module policy
preserves exact `qaplet:events` builtin lookup before file URL filtering, all
capability bits, current limits/stat fields, the queue/Host source files and
both public Host catalogs.

The policy Host obtains the declared builtin module baseline from the public
snapshot (one `qaplet:events` identity at creation); file/failure/cache assertions
are relative to that baseline. The bounded-failure test allows the declared
builtin plus one failed file identity, then proves another file is refused.
No private layout or engine call count is used.

A fresh combined producer is under `combined-events-v1/` and its repository check
passes. Its new SDK/recipe/APE and supplied-byte platform outcomes will be
recorded separately; ordinary-v4 results are not relabeled for the merge.
The engine input, ordered overlays, raw pinned lock and frozen M0 recipes are
unchanged, so the clean M0 Full run retains its exact new-input scope.

### Combined producer identities and Ubuntu result

Committed combined source `c6d41f253914ec46e58375cf59d497c2bbff54b2` produces
`combined-events-v1/build.json`. The fresh Ubuntu consumer passes **34/34
required cases**, including every event case and the policy/frozen cache cases
with the declared builtin module count. The combined repository check passes.

| Combined identity | SHA-256 |
| --- | --- |
| SDK recipe | `0ef40fda2c72b80baf2930adb903e986ef59b92cb496649e6dc2ddd8306bee39` |
| SDK aggregate | `8e3c7faa2523866747eb66b4caa08149b0250867d780d231d593b5b8218c03b1` |
| Host APE | `f594de7f53de476b500244e1ffd5090cfc61e65801992b5fca764dd90e157861` |

The final combined READY pointer was sent to the parent; source and recipe remained
frozen through matching Windows/ARM64 execution, both completed successfully. Input_id and pinned raw lock
remain `f07f23...fd178` and `eb0c7d...32f9` as fully spelled above.

### Final combined supplied-byte results and closure

`combined-events-v1/windows-user-run.json` passes **33 required** public C Host
cases on normal-user Windows x64 native PE/4096-byte pages. The separately
attempted file-symlink case remains explicitly blocked, required=false, with raw
exit 19712, errno 2, symlink_result 0, lstat -1 and WinError 1314. This unavailable
fixture is not a behavior pass. All module-policy/drive/case/hardlink/cache/NUL
and event cases required on Windows pass. A duplicate agent run is retained as
`windows-agent-run.json`, without claiming extra target coverage.

Main `out/m1/05-guest-combined-events/{selection,payload,run}.json` binds the exact
combined producer/input/SDK/recipe above and passes **34/34** actual full-system
ARM64/65536-byte guest cases, including real symlinks and a BusyBox-created FIFO.
The independently measured guest architecture/kernel/pages and before/after
payload hashes remain in its raw report. No required case skipped or timed out.
The agent independently checked all case statuses and producer/input/SDK identity
equality from the original reports.

The clean Full provider ZIP, artifact **13135001644**, is SHA/CRC/member-verified
in main `out/m1/ci/05-clean-full-collection.json`. Its SHA-256 is
`2703e58fec926a56fa8ef38adbc767f3477098679d452058e826f7fb94cffb38`.
Original extracted Core report:
`out/m1/ci/05-clean-full/05-clean-full-core-13135001644/core.json`, SHA-256
`aceb984fd067423597fba4c84e63c4bbfb70fd203052ae6ce1f3bbd1b094ef4f`.
The actual report has `status=pass`, `m0_core_status=pass`, **235** case-index
entries, passing deep/offline scopes and exact current input_id
`f07f23c64bfcfb4a92722723e3e673213534a6fa81e690c401bd9fa10e0fd178`.
This is new-input M0 Full proof, not a complete M1 SDK Full claim.

Ticket 05 is resolved after these actual combined checks. All red artifacts,
failed network attempts, selected-to-pinned transition receipts, intermediate
SDK distributions, diagnostic prototypes and original raw reports remain
retained under the recorded worktree/main `out/` paths. No old result was
relabeled, source cache overwritten, supplier archive replaced or fallback used.
