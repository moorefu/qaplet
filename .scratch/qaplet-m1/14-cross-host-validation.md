# 14 · Native SDK contracts and lifecycle evidence

Ticket 14's native Cross acceptance is resolved against the final clean source
`21bd317f4246571f0048789ca97167632c09f924` and original provider execution from
run `41486421618`. This record separates actual native acceptance from earlier
provisional bytes and the explicitly failed remote Full guest acquisition.

## Product boundary and CI

The existing public SDK Host executes a real 1,000-iteration
create/close/destroy loop. `sdk.py run` now reports its checked raw result as
structured `lifecycle` evidence: Linux x64 requires at least 1,000 cycles;
other native Cross targets require at least 100. The supplied workload actually
executes 1,000 on every host. A failed/uncompleted workload records zero completed
cycles and cannot sign the lifecycle criterion.

`sdk.py matrix --profile fast|cross` reuses the public 48 Host, 42 CLI and eight
complete-application cases, and the existing `core_gate.Evidence` resolver.
It independently reconstructs the complete required catalog, process argv,
expected output/exit, per-case SDK/APE identity, raw logs, intervals/deadlines,
producer context, native host facts, selected loader and lifecycle threshold.
Explicit evidence-root mappings preserve original POSIX/Windows paths.
Duplicate report/case identities, guest or translated execution, required skip,
failure, timeout, missing cases and missing targets cannot sign a pass.
The measured Windows file-symlink fixture remains optional and visibly blocked
only for its actual fixture-refusal exit/output; Linux must execute it.

The shared `m0.yml` Ubuntu build job produces the sealed SDK, executes complete
Linux Fast, compiles the actual ZIP externally using only public material, and
checks the Fast evidence. Its narrowly scoped SDK upload explicitly includes
hidden files, including `sdk/lib/.aarch64/libqaplet.a`. Each existing M0 native
job downloads the same SDK distribution and executes its complete SDK catalog
on the same measured host. The existing matrix job adds the separate SDK Cross
verdict. M0 sensitivity, read-only permissions, trusted-cache policy, exact
guest catalog and Full/offline verdict remain intact.

Ticket 15's independent SDK 64K payload runs in the existing measured page-gap
job, after preserving the original M0 boot/report. The native job already runs
SDK contracts if the native ARM64 kernel has 64K pages. The SDK guest importer,
native matrix and final strict gate retain separate scopes. Evaluator source
identities are separate from the SDK product recipe. None signs release-ready
or M2 waker/I/O cancellation/scheduler support.

## TDD and provisional local execution

Starting integration `31155083b3677bf9888d14d6c01dd8758d551707` contains resolved
ticket 13. Documentation tip `5cda583d14917722c3a26b7e6cdbd470ef309d21` was
absorbed before source commit `b6f0569399decb969941e988a3e7337c181909aa`.
Final ticket 14 source is `ff0d75a875477f9599331891a0fffc56f825dbdc`;
combined frozen 14/15 candidate is
`0f4f765ac619dfa2052fda5dae213431fb374dc7`.

Original local reports/logs remain under this worktree's `out/m1-14/`:

| Boundary | Actual retained result |
| --- | --- |
| Missing public matrix CLI | Initial required CLI test failed before implementation. `tdd/red-reproductions.json` replays exact pre-ticket Git blobs and retains raw process logs; missing operation produces no report. |
| Missing mandatory SDK CI | Original public `check-ci` accepted a workflow with all SDK execution removed. The new checker rejects it. Exact pre-ticket reproduction, changed workflow, green refusal and raw logs are in `tdd/`. |
| Legacy evidence | `tdd/red-legacy-native.json` rejects the actual ticket 13 native report without structured lifecycle evidence. No historical report was rewritten. |
| Native semantic refusals | `tdd/green-reproductions.json` and `tdd/mutations/` preserve real-report mutations for missing case, short lifecycle, guest substitution and required skip. All reject through the public matrix CLI. |
| Ubuntu supplied execution | `local/run.json`: actual Linux x64 WSL2, kernel `6.6.87.2-microsoft-standard-WSL2`, 4096-byte pages, all 98 cases and 1,000 lifecycle cycles pass. `local/fast-matrix.json` independently accepts the raw closure. |
| External ZIP consumer | `local/package-consume.json`: actual Ubuntu external `/tmp` application compile using supplied public header/static archives; eight application cases pass. |
| Native Windows x64 | Parent's `local/windows-user-run.json`: 97 required cases and 1,000 cycles pass on native PE; established optional symlink fixture remains blocked. |
| Incomplete native Cross | `local/partial-linux-windows.json` accepts Linux/Windows and visibly blocks macOS x64/ARM64 and native Linux ARM64. It does not sign complete Cross. |
| Repository checks | `final-source-check.json`: Ubuntu `dev.py check`, 222 total tests (147 pass, 75 existing explicit-data/platform skips separately reported), plus real system C bootstrap. Native positive replay and all four mutations were enabled. |
| Combined source checks | `combined-source-check.json`: frozen `0f4f765` passes `dev.py check` and the real bootstrap, 230 total tests (149 pass, 81 explicit-data/platform skips). The actual current-recipe SDK exporter and native replay/mutations were enabled; historical ticket 13 guest inputs were not relabeled as the new recipe. |

The local producer `local/build.json` records source
`b6f0569399decb969941e988a3e7337c181909aa`. Its product recipe exactly matches
the frozen combined source; subsequent combined changes were evaluator,
workflow, tests and documentation. Local producer/SDK identities still differ
from a fresh combined CI producer and are not combined with unrelated CI
native results. `LOCAL-READY.json` binds these provisional report bytes:

- SDK aggregate: `8c7d12c0f8bff28583c8d52bb76ffd94c0340b85ef6da910a7ac40e30c95f8d6`
- SDK recipe: `ac585ea4efdbbb82a3de3b1a78795974d43f46405b3497084e0649300f822c55`
- SDK ZIP: `9558081a990d3eb58d2a421b35fe7516f243cce35ca6ea2289e671e70a5e10ad`

The claimed ESM/cycle/TLA behavior is limited to the actual supplied catalog
and fixed engine fixtures. Supervisor timeout is uncompleted execution, never
proof of product stop/deadline behavior. Ordinary repository-check skips are
not mandatory SDK successes. Original failures, packages and raw outputs remain
available.

## Final matching-source native acceptance

The clean source is `21bd317f4246571f0048789ca97167632c09f924`. Its original
[run 41486421618](https://github.com/moorefu/qaplet/actions/runs/41486421618)
executed complete CI Linux Fast (98 cases, 1,000 cycles), eight actual external
ZIP application cases and the following native consumers on the same supplied
SDK/Host/CLI/example bytes. All 98 total cases pass on every native target;
Windows has 97 required cases, with its optional symlink case actually passing
on this CI host. There are no required skips, timeouts or incomplete cases.

| Native target | Kernel | Actual pages | Required / total cases | Completed cycles |
| --- | --- | --- | --- | --- |
| Linux x86_64 | 7.0.0-1017-azure | 4096 | 98 / 98 | 1000 |
| Linux aarch64 | 7.0.0-1017-azure | 4096 | 98 / 98 | 1000 |
| Windows x86_64 | 10.0.20348 | 4096 | 97 / 98 | 1000 |
| Darwin x86_64 | 24.6.0 | 4096 | 98 / 98 | 1000 |
| Darwin aarch64 | 24.6.0 | 16384 | 98 / 98 | 1000 |

Each original report retains actual native mode, CPU/execution architecture,
loader identity/route, CI run/commit/image, complete per-case expected/actual
output, raw stdout/stderr, command/cwd, exit, timeout and interval. POSIX targets
use the verified delivered ELF/Mach-O loader; Windows uses native PE. The two
target static archives remain SHA-identical to ticket 13, while the Host
fixture/source and SDK manifest/recipe changed under their new identities.

| Final CI identity | SHA-256 |
| --- | --- |
| Producer report | `13038eac607c28d70c1a0249172a47b38a4cc4f3ffc169e37393bcfe27a1d307` |
| SDK aggregate | `7ec19ce8f7dc6eacbe40d650077a60bf463eff77da4baa87552c3b9f81048ed1` |
| SDK recipe | `5a86423264e76e2213d12ca9fbfbc1023fb1360122a81cd28bd00edcf2d068f8` |
| SDK ZIP | `44f2e58fdd96a2346f414bd690faf9b5741ecf720598e276d30bdd0628b63205` |
| Host APE | `24d31ff5e53cc954ddbf10f71a2e5865d4935f84aa27c88ac523ca745ac7d931` |
| CLI APE | `723d8cd5384bf84767a109b7db09fcab797038a7cef0daf678de335acb3d053c` |
| Complete C example APE | `dc1e572109699b0f6837e14c4ede715468ca9ea907d164fc342b72604af79719` |
| x86_64 static archive | `36ebd78c828c10f010628acfc7592c5092dbb37ef45fb0fdd353027b096084e7` |
| aarch64 static archive | `7459256aa99a9a8212e72b5334afeb48167b35832cad10bee586ab773d6586d2` |

The parent's original nine provider ZIPs, exact SHA/length, CRC and extracted
inventory are retained in main checkout
`out/m1/ci/14-final-full-collection.json`, with physical roots in
`out/m1/ci/14-final-full/`. The outer M0 Full archive's local transport alias
`14-clean-full-13161659399` is mapped back to its original provider name from
`14-final-full-artifacts-2.json`; no original report is rewritten.

This worktree's `out/m1-14/final/selection.json` explicitly selects the original
provider transports, source `21bd317`, run `41486421618`, producer, five native
reports and longest-prefix original path mappings. `replay.json` retains 1,074
byte-bound evidence references and the actual existing `sdk.py matrix --profile
cross` invocation; `cross.json` independently passes all five targets.
`ci-fast-package.json` additionally binds original CI Linux Fast and all eight
external application raw results; `ci-fast.json` passes the existing Fast CLI.
The execution-preparation chmod in the old loader selector was confined to
byte-verified loader fixture copies, preserving all original provider inputs.
Ticket 16 owns the evaluator-only pure loader-verification seam before final
independent review; product recipes/startup execution are unchanged by it.

The final clean local boundary is separately retained under
`out/m1-14/clean/`: build, 98 cases/1,000 cycles, package eight, Fast replay and
repository checks/bootstrap pass. `CLEAN-READY.json` verifies all 57 product
recipe files against committed blobs. These local SDK bytes (aggregate
`0c3b64e34528f35cda578d45c34562364bee7fdd2acdfde6a9e5b1a13cb345a3`)
are not mixed with the CI aggregate above. The repository check ran 230 total
tests: 149 pass, 81 explicit-data/platform skips; actual native replay/mutations
and current-recipe SDK exports were enabled.

Run `41486421618` itself concluded **failure**: locked 64K guest acquisition
received HTTP503 and no guest executed. Consequently the M0 Full aggregate/Core
closure did not pass; the separate SDK native matrix step did pass. Raw status,
jobs, acquisition failure and downstream missing-guest reports remain in the
original provider closure. Native success does not replace that failure or
certify 64K. A subsequent unchanged-source Full retry `41489263809` is tracked
separately by tickets 15–16 and subsequently completed **success** on unchanged
source `21bd317`, including genuine SDK64K and M0 Full execution. The parent
retains its complete original closure at main
`out/m1/ci/14-final-full-retry-complete-collection.json`. Although its SDK
aggregate/recipe equal the values above, its sealed package is different:
`2524580d49e556dc94cbfd51e1a662953279a121f6b051a3b99123a878e7bbb6`.
No result from that different producer/run is spliced into this ticket's first
Cross closure. Tickets 15–16 consume the retry's single complete producer set.
`release_ready=false`; final strict and release gates remain separate.

## Original native failures and bounded diagnosis

The original Full run `41463967070` stays pinned to `0f4f765`. The parent
retained and verified original provider ZIPs in main checkout
`out/m1/ci/14-15-macos-collection.json`. macOS Intel (kernel 24.6.0, native
x86_64, 4096-byte pages) and ARM64 (same kernel, native aarch64, 16384-byte
pages) each actually ran all 98 cases and 1,000 lifecycle cycles. Both failed
`qaplet-host.module-aliases` at the final valid.js unlink; Intel additionally
failed the final `qaplet-host.stop-running` gate. These failures block Cross.

Original provider archive identities:

- Intel ID `13150249915`, SHA
  `96e8bcf14ebf0242dfb7402d87432c1c848252aad733585363a3c61b0ca45aaf`.
- ARM64 SHA
  `8374483072010065722d8158806e782ee4d936150e24214dae34931ab112f5bd`.

The isolated native commands already give red-capable short loops: supplied
loader + Host `--module-aliases` (about 8 ms on both targets), and Intel Host
`--stop-running` (about 9 ms). Before probes, ranked falsifiable hypotheses were:

1. Upper/lower filename aliasing causes two unlinks of one directory entry:
   predict equal dev/inode and valid.js ENOENT after removing VALID.js.
2. An Intel CPU-clock API fails before the required 20 ms CPU progress:
   predict a clock-specific return/errno and failure stage.
3. Both clocks return success but subtraction mismeasures owner CPU:
   predict inconsistent raw process/self samples and insufficient observed delta.
4. Runtime stop ordering is wrong despite valid CPU observation:
   predict a nonconforming stop/snapshot/post return or accepted reason.

Diagnostic candidate `5e4f7724818912061e16fe9640e3717d64fad02d` keeps the original
required assertions intact. Tagged failure-only C diagnostics and the temporary
`scripts/probes/sdk_native_diagnose.py` repeat each failing public case five
times on each existing native Mac job. Repetitions verify supplied identities,
record raw process evidence and explicitly make no acceptance claim. No cause
or fix is asserted until matching actual native facts distinguish the ranked
hypotheses. The diagnostic build/source/evidence use a fresh identity and never
overwrite original `0f4f765` results.

Verified native diagnostic run `41469391051`, source `5e4f772`, reproduced the
same failures in the complete catalog and all five bounded repetitions.
Original archives and collection remain in main checkout
`out/m1/ci/14-diagnostic-macos-collection.json` and `14-diagnostic-macos/`.
Both Macs report equal device/inode for the two case spellings, distinct OS
realpath strings, and ENOENT for the lowercase spelling immediately after the
uppercase unlink. The SDK module/case/hardlink assertions had all passed; the
fixture attempted to remove the same directory entry again.

Intel's stop record consistently reports variant 0, stage 1,
`clock_failure=1`, errno 22, zero successful CPU reads. That is precisely the
initial `CLOCK_PROCESS_CPUTIME_ID` query. Stop, repeated stop, snapshot and
refused post return 0/0/0/5; accepted lifecycle/phase/reason are 1/1/23 and
marks remain zero. The fixture correctly refused to claim CPU progress; these
facts do not establish a Runtime or libc product defect requiring a product
change.

Candidate `e2dfb675ab7fec8d5e1fdad03a7f77a5ab76b3e5` corrects cleanup using the
measured entry identity, adds an explicit post-unlink existence/ENOENT
assertion, and keeps every module identity, count, evaluation and resource
assertion. It replaces only the process CPU query with user+system CPU from
`getrusage(RUSAGE_SELF)`, retaining worker thread CPU subtraction and the
required 20 ms owner progress threshold. This alternate measurement still
needs native proof. Temporary diagnostics remain for that regression; the
complete required native run retains empty-stderr assertions, while the
diagnostic-only wrapper records tagged positive measurement facts without
claiming acceptance. If the worker clock also fails, the unchanged gate fails
and records an independent `RUSAGE_THREAD` availability sample for a subsequent
probe. Cleanup of all temporary diagnostics precedes the final clean Full run.

Native Cross `41474690025` on `d4c7122` confirms the alias correction on both
Macs; ARM64 now passes all 98 cases. Intel process `getrusage` succeeds but
`CLOCK_THREAD_CPUTIME_ID` and the independent `RUSAGE_THREAD` query both return
EINVAL. All five required repetitions retain the failure. The parent preserved
the original verified closure in main
`out/m1/ci/14-cpu-alternative-macos-collection.json`. No Runtime/API/libc change
is indicated by these observer failures.

Candidate `216098944940247cead938285b4f1ff7839cd1f7` keeps precise subtraction
where available. If the worker clock is unavailable, its busy sampler brackets
process CPU reads inside monotonic wall reads and computes
`max(0, process_delta - wall_delta - margin)`. Because the test has only owner
and worker, and worker CPU cannot exceed that encompassing wall interval, the
result is a conservative owner CPU lower bound. The margin includes four
accounting-tick/timeval endpoints and two measured monotonic resolution units.
Unknown resolution/failed samples and insufficient progress remain failures.
It first holds the owner in a native condition barrier: busy worker CPU alone
must not reach the 20 ms owner bound. Only after release into the original pure
JS loop may the still-required 20 ms bound complete.

Fresh local `out/m1-14/cpu-bound/{build,check,run}.json` pass: complete Linux 98
cases, 1,000 lifecycle cycles, repository checks and real bootstrap. Forced
two-CPU `forced-two-cpu-uncontended.json` passes ten case repetitions, with ten
synchronous/async CPU observations of 20.912–23.352 ms after 61–63 ms wall,
40.000002 ms margin, approximately 80 ms actual worker CPU in each owner-held
control and zero control owner bound. The stressed two-CPU closure also passed
while an independent one-CPU negative fixture contended for CPU; its slower
sample was preserved, then isolated runs confirmed the short stable loop.
`forced-one-cpu.json` correctly fails closed: after 2.006 seconds, measured
owner bound remains zero, while stop still returns/records its correct code 23.
That forced observer limitation is not converted into product success or skip.
Native Mac regression was then actually completed by successful Cross run
`41480615619` on that exact source. Both Macs passed all 98 cases and 1,000
cycles. Main `out/m1/ci/14-cpu-bound-macos-collection.json` preserves the original
successful ZIPs. Intel's ten owner lower bounds were 21.609–38.941 ms after
40.002 ms margins; the owner-held controls consumed 74.0–75.2 ms worker CPU
while every control owner bound remained zero. ARM64 retained precise
20.011–24.028 ms samples. Original Intel/ARM archive SHAs are respectively
`8db1981c9ae9c45934354603a1c0f3d21580c472e4e84ac54177d1ba9c1b2a83`
and `aebe4a173279ed1d99df2407a80843e06ae9c6d3727f7f8a38d060b877c8ea22`.

Only after that native regression, commit `21bd317` removed all temporary
DEBUG tags, forced modes, extra API probes and the repeat workflow/driver,
retaining the normal conservative backend, owner-held safety assertion,
20 ms progress gate and all module identity/cleanup assertions. The current
integration tip was merged (already an ancestor); all 57 final product inputs
match committed blobs. The clean local checks and final original-source CI
native acceptance are recorded above. All old failed/provisional/diagnostic
archives, SDKs and raw logs remain available; only the final ticket/validation
documentation is committed after acceptance, with product/workflow source
remaining frozen at `21bd317`.
