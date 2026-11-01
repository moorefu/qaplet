# BOOT-04B native baseline and remaining 64K work

2026-11-01: ticket 06 remains **claimed**. This baseline adds executable native validation and five CI jobs; it does not complete the six-target startup matrix. It started at integration `dc5c0c3` in branch `codex/m0-06-cross-host`, with public CLI / actual OS / evidence seams confirmed by the parent spec.

## Implemented and measured

`scripts/startup.py run` consumes a successful `ape-build` v1 report and the supplied artifact directory. It compares both artifact SHA-256 and size before launch, independently decodes fat headers and sidecars, selects a verified native loader, measures OS/kernel/Python architecture/page size, then checks Hello facts, eval facts, `42`, an exception and a syntax error. Error paths require semantic stderr and a nonzero exit status; Windows actually returns 256, Linux 1. Each run retains exact argv/cwd/timestamps/timeouts/raw stdout/stderr, inherited source/toolchain/build identity, runner image/run allowlist and artifact/loader hashes after execution. Native reports record QEMU as `not_applicable`. Mac checks include translation sysctls; ARM64 Mac also runs native `codesign --verify --strict` against the separate helper.

The unchanged producer products from CI05 (`out/m0-integration/ci05`) passed five cases on Ubuntu WSL2 x86_64/4096 and Windows x86_64/4096 in the ordinary user execution context. Evidence is under `out/implement-spec/06/out/ticket06/{linux,windows-user}/startup.json`. The restricted Windows invocation timed out and retained a fail report under `windows/`; it is not substituted for the ordinary-user report or called a product pass. Hello remains `5671ea00e3ba10ce544fce487eaf77cdca7f31a60d947b2930fe7982599d983b`, eval `61c52cd18bee95de3302405dde84c69f22dd5ff8debc3581d4ad83e01122bc8e`.

`startup.py matrix` validates the five required cases and their actual output, retains failures, requires unchanged producer identities, and distinguishes all six measured targets. A single real native report correctly leaves the other five targets blocked. `guest-manifest` exports exact payload paths/digests and five guest argv records for 07's generic full-system transport; exporting is not execution evidence. Guest report import, page-gap CI scheduling and full matrix aggregation remain to be connected after 07's transport and 64K corrections land.

## Ubuntu-built Mac helper

`toolchains/startup-loaders.lock.json` separately pins official Zig 0.15.2, archive size/SHA, compiler SHA, the exact locked Nightly `ape-m1.c` and the cross-build flags. `startup_loaders.py fetch` is explicit; `verify` and `build` are offline. The verifier compares every extracted Zig file/type/path with the pinned original archive, rejects redirected inputs and extra files, and repeats verification after building. All reusable physical inputs and caches are in the main repository `.cache/`, selected explicitly for worktree runs. Reports and products stay in `out/`.

The successful Ubuntu build used main `.cache/tools/zig/0.15.2`, `.cache/toolchains/cosmocc` and `.cache/build/startup-loaders`. Report: `out/implement-spec/06/out/ticket06/loaders/helpers.json`, ID `1d1ba007-a41b-4903-aa03-159eca4f2903`. The separate ARM64 Mach-O is 58672 bytes, SHA-256 `8632685b1e5d301bd883df4f873f62f3ec14d45bf2268298e8e1a720e16abf3c`. The parser verified native ARM64 executable format, sole dependency `/usr/lib/libSystem.B.dylib`, an ad-hoc CodeDirectory and every code-page SHA. Native Mac OS acceptance is **not_run**, pending CI. The three other loaders are unchanged locked Nightly files. Product APEs are never signed or assimilated.

The workflow builds products and helpers only in the Ubuntu producer job. Five explicit native consumers download that one artifact, verify measured OS/architecture, and upload failure evidence with `always()`. Labels were checked against [GitHub's current runner reference](https://docs.github.com/en/actions/reference/runners/github-hosted-runners): `ubuntu-24.04`, `windows-2022`, `macos-15-intel`, `macos-15`, `ubuntu-24.04-arm`. Pages are measured, not inferred from labels. The current [download action documentation](https://github.com/actions/download-artifact) supplies v8 and documents permission loss; only the verified separate loader has execute bits restored.

## 64K diagnosis and handoff

07 booted an actual locked ARM64 65536-byte-page guest. The original ARM64 loader crashed with exit 139; a diagnostic single-LOAD loader reached the unchanged product and reported that ELF address/offset were incongruent modulo `AT_PAGESZ`. No 64K pass is claimed.

Actual Ubuntu distinguishing links are preserved under `out/implement-spec/06/out/ticket06/`:

- `aligned/`: applying max/common-page-size 65536 to both CPUs fails x86 real-mode `R_X86_64_PC16` relocation.
- `max-only/report.json`: max-page-size 65536 alone links successfully, but official `apelink` repacks the ARM64 LOADs at 16K boundaries. The ARM64 executable LOAD has p_offset 376832 (mod 64K 49152), p_vaddr 34359738368 (mod 64K 0), p_align 65536; the independent 64K audit rejects it.
- `aligned2/`: an exact per-CPU official link/fixupobj/apelink/pecheck recipe reaches the same packer failure.
- `64k-draft/`: preserved implementation and test drafts before removing the unproven default changes. No failed producer recipe or 64K assertion is enabled in this native-baseline commit.

07 owns the unified locked source loader and corrected packer after source research. The default product build remains 05's proven recipe until that work is validated. New final bytes will need every native and guest host rerun; historical successful reports cannot certify new hashes. The source-research context is `.scratch/qaplet-m0/arm64-loader-source-research.md` in integration.

## Validation

TDD red/green cycles covered changed bytes rejected before execution, actual five-case native execution, missing Zig blocked without fetching/creating its cache, and incomplete six-host matrices remaining blocked. With explicit CI05 products and the built helper manifest, all three focused startup contracts pass.

Ubuntu `make check OUT=out/ticket06/check-final` passed: 68 contracts, 0 failures, 6 explicit skips; both CI static checks and real system-C bootstrap passed. The first full run had an existing DrvFS fixture directory-rename permission failure; its isolated rerun and the complete final run passed. No expensive full product-input scans were repeated just for static verification. No remote CI has been run by this ticket branch yet.

## Native CI and execute-permission consumer repair

The baseline subsequently ran in [M0 run 41266351246](https://github.com/moorefu/qaplet/actions/runs/41266351246), integration `ab254628cfe6d4fc0f04d449229912c447bcb71d`. The producer, Linux x64 and Windows jobs succeeded. Downloaded original failure reports and raw logs remain under `out/m0-integration/ci06-native/`; these are retained unchanged.

The `macos-15` report measured native aarch64, Darwin kernel 24.6.0, page size 16384, `sysctl.proc_translated=0`, runner image `macos15 / 20261028.0489.1`. Its Ubuntu-built ARM64 Mach-O helper passed native `codesign --verify --strict` and retained SHA-256 `440b1a20332538a28dd84294088b7b94e4c3c513cc9f377571ede52cc9e1d180`. Every product case then returned 127 with `not found (maybe chmod +x)`. The exact locked `ape-m1.c` calls `access(path, X_OK)` at line 353 and emits that diagnostic at line 1087. Artifact transport drops execute bits; the consumer had restored only loader permissions. Actual Mac product success after repair remains pending a real CI rerun.

The focused consumer change restores POSIX execute metadata on the two product files only after **both** expected SHA/size checks and fat/sidecar inspection finish. It records actual before/after permission modes and `os.access(X_OK)`; a false result remains blocked, preserving no-exec/access-policy refusal. Windows keeps its existing PE route. No product content, signature or format is changed.

The Ubuntu public-CLI red/green regression uses temporary POSIX copies of the supplied CI05 artifacts (no build, reusable cache or mount change). Both start at 0644 with X_OK false; the old runner leaves them at 0644, so the regression fails. The fixed runner restores 0755, passes all five actual native cases, and retains the same before/after hashes. A subsequent tampered eval is rejected before any command or permission mutation, with both artifacts still 0644. Reports are preserved under `out/implement-spec/06/out/startup-permission-contracts/transport-0644-uxfjdxf4/` (`report.json` and `rejected.json`). Focused contracts: 5 tests, 0 failures, 1 explicit skip because this Ubuntu session has no existing `/dev/shm` no-exec mount; no mount is created to fabricate that coverage.

The native Linux ARM64 4K report has passing Hello/eval facts but three JS cases exiting -11. This separate failure is owned by 07's confirmed QuickJS representation repair; the execute-permission change does not claim to fix it. The complete six-target matrix and ticket 06 remain claimed.

Focused-change validation limitation: both aggregate `make check` attempts reached 70 contracts with exactly one existing supplier-fixture failure (`test_drvfs_cache_preserves_archive_modes_and_refuses_dirty_inputs_before_execution`), 8 explicit skips and no other failures. The failure is a DrvFS permission denial renaming that fixture's `.candidate-*` directory to its content-key directory; it occurs outside the modified consumer files. The same fixture passed the bounded isolated rerun. Both CI static checks passed, and `make test-bootstrap OUT=out/ticket06/execute-permissions-final` passed separately after the aggregate stopped before bootstrap. This change does not claim a green aggregate run; the earlier 68-test baseline pass remains historical. No supplier code or mount/global setting was changed to hide the failure.
