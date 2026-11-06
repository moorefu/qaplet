# ARM64 Linux 64K native APE loader source research

Research completed 2026-11-01. This note identifies an independently pinned, official source recipe for a separate helper. Source acquisition and identity checks completed; this research task did not compile a helper or claim a 64K runtime pass. Task 07 owns the Ubuntu build and real guest verification.

## Result and provenance boundary

Use official Cosmopolitan commit `aefad27c09ffda64c6ac64bd69f758e9f9c7b91d`, tree `39b98a5a1aca0ce501d3c8b5cc80c630468b3b63`. The exact commit exists in the official repository and GitHub reports its commit signature verified. This commit was selected independently for the new helper because it is the recorded Nightly run head; that selection does **not** prove it supplied the bundled libc or the original binary loader. Keep `toolchains/cosmocc.lock.json libc_source.status=unproven`, its null source commit, and its original ZIP identity unchanged. [Official commit](https://api.github.com/repos/jart/cosmopolitan/git/commits/aefad27c09ffda64c6ac64bd69f758e9f9c7b91d), [locked Nightly run](https://github.com/jart/cosmopolitan/actions/runs/41169443573).

The source files below were fetched through the connected GitHub contents API with that exact commit ref. Raw web cache misses were not treated as HTTP 404. UTF-8 bytes were staged without changes; all seven compile inputs, BUILD.mk and LICENSE have computed Git blob SHA-1s matching official directory/tree metadata. SHA-256 values below are measured from the staged bytes.

Cache: `D:/Workspace/dairn/qaplet/.cache/sources/ape-loader/aefad27c09ffda64c6ac64bd69f758e9f9c7b91d/`. Acquisition manifest with exact API/raw URLs, sizes, SHA-256, Git blob SHA-1 and license: [source-acquisition.json](../../out/m0-integration/loader64k-research/source-acquisition.json). Official metadata snapshots: [commit](../../out/m0-integration/loader64k-research/official-git-commit.json), [ape directory](../../out/m0-integration/loader64k-research/official-ape-directory.json), [libc tree](../../out/m0-integration/loader64k-research/official-libc-tree.json).

## Minimal dependency closure

The upstream ARM64 target links exactly four objects, from `ape/start.S`, `ape/loader.c`, `ape/launch.S` and `ape/systemcall.S`. The C file includes only `ape/ape.h`; the assembly files also include `libc/dce.h` and/or `libc/macros.h`. Those three headers have no further includes. Thus seven source/header files are sufficient; no libc archive, crt object, generated header, loader.lds, aarch64.lds or full libc rebuild belongs in the native helper link. [ARM64 target and flags](https://github.com/jart/cosmopolitan/blob/aefad27c09ffda64c6ac64bd69f758e9f9c7b91d/ape/BUILD.mk#L69-L131), [C include](https://github.com/jart/cosmopolitan/blob/aefad27c09ffda64c6ac64bd69f758e9f9c7b91d/ape/loader.c#L19), [startup includes](https://github.com/jart/cosmopolitan/blob/aefad27c09ffda64c6ac64bd69f758e9f9c7b91d/ape/start.S#L19-L21), [launch include](https://github.com/jart/cosmopolitan/blob/aefad27c09ffda64c6ac64bd69f758e9f9c7b91d/ape/launch.S#L19), [syscall include](https://github.com/jart/cosmopolitan/blob/aefad27c09ffda64c6ac64bd69f758e9f9c7b91d/ape/systemcall.S#L19).

| Input | Bytes | SHA-256 | Git blob SHA-1 |
| --- | ---: | --- | --- |
| [ape/loader.c](https://github.com/jart/cosmopolitan/blob/aefad27c09ffda64c6ac64bd69f758e9f9c7b91d/ape/loader.c) | 32507 | `00b41aa7e4abbb55c8023191bb44eaf96faf2d383a9b06cc56df3d95b6972634` | `b684934970d69ecd37d576817a267fc767b9c2b6` |
| [ape/ape.h](https://github.com/jart/cosmopolitan/blob/aefad27c09ffda64c6ac64bd69f758e9f9c7b91d/ape/ape.h) | 488 | `5861cbc541905297d405bbead806f1c37d16f94f39aed4a862f8cc37bd2db81c` | `d2f5896120980c98609bb5835a726b3cf999001b` |
| [ape/start.S](https://github.com/jart/cosmopolitan/blob/aefad27c09ffda64c6ac64bd69f758e9f9c7b91d/ape/start.S) | 3034 | `299cee077a338f1a3234a3ba1ac894e43a4c8359151fef3b5274687c970ac5eb` | `29a661601c0d74100d206fb6c1bd0e79df1e8c5b` |
| [ape/launch.S](https://github.com/jart/cosmopolitan/blob/aefad27c09ffda64c6ac64bd69f758e9f9c7b91d/ape/launch.S) | 3196 | `c0382ab8ecb528af7c968aba212546411ad86b214109295bef0aac61b82ec24c` | `f710fdec758bfc2ddf64e4d92caad80ed595e70a` |
| [ape/systemcall.S](https://github.com/jart/cosmopolitan/blob/aefad27c09ffda64c6ac64bd69f758e9f9c7b91d/ape/systemcall.S) | 2466 | `5e8dfc47b98fdeae806f7b7b0372ed0e0856aab830e127140bf4d7ebb515d499` | `91daedc95f9b329541b9eb83406c2cd9297a5066` |
| [libc/dce.h](https://github.com/jart/cosmopolitan/blob/aefad27c09ffda64c6ac64bd69f758e9f9c7b91d/libc/dce.h) | 3965 | `ae5bad2373734ff82bccfd1ee5c42bd3b64cf79e7494851f3e5515fef8b92afd` | `c4bc0758bd9d274e2bd52d21a87b160335ee24a1` |
| [libc/macros.h](https://github.com/jart/cosmopolitan/blob/aefad27c09ffda64c6ac64bd69f758e9f9c7b91d/libc/macros.h) | 11911 | `72cff3706d029dca2da74ab9d2a7776dec01d88c36978b8f6128d567f3102eae` | `21553447cf3641732eb4a4ea73df1674f32f0bd7` |

The root [LICENSE](https://github.com/jart/cosmopolitan/blob/aefad27c09ffda64c6ac64bd69f758e9f9c7b91d/LICENSE) is ISC. loader.c carries the 2021 Justine Alexandra Roberts Tunney notice; the assembly files carry her 2023 notices. Preserve those original comments and the ISC license in source/notice packaging. BUILD.mk, the root LICENSE, `ape/aarch64.lds`, `tool/build/fixupobj.c` and `tool/cosmocc/bin/cosmocc` were additionally cached as recipe/provenance evidence; only the seven table entries are compile inputs.

## Source-derived Ubuntu recipe

The upstream ARM64 loader compilation uses `SUPPORT_VECTOR=33` (Linux plus FreeBSD), `-fpie -Os -ffreestanding -mgeneral-regs-only`, and disables asynchronous unwind tables, stack protector, compiler ident and GNU unique symbols. Its linker recipe is `-pie -static -nostdlib --no-dynamic-linker -z norelro`, but sets **both page-size options to 0x4000**. For a separate 64K helper, retain that source/compile recipe and change those two linker parameters to `0x10000`. [Pinned official recipe](https://github.com/jart/cosmopolitan/blob/aefad27c09ffda64c6ac64bd69f758e9f9c7b91d/ape/BUILD.mk#L69-L124).

Run only on Ubuntu. This exact shell recipe uses the existing verified native Linux tool view of the locked compiler; every reusable/intermediate input remains under the main repository .cache. The commands below have not yet been executed by this research task.

```sh
set -eu
ROOT=/mnt/d/Workspace/dairn/qaplet
PIN=aefad27c09ffda64c6ac64bd69f758e9f9c7b91d
ARCHIVE=f2f984d3a9790674fc0da11c1c00a32d4fe3f8dafde99af74b0c6dc24a2a16cf
SRC="$ROOT/.cache/sources/ape-loader/$PIN"
TC="$ROOT/.cache/toolchains/cosmocc/official-assimilate-linux-x86_64-v1/$ARCHIVE/tree"
B="$ROOT/.cache/build/ape-loader64k/$PIN"
mkdir -p "$B/tmp"
export TMPDIR="$B/tmp" LC_ALL=C SOURCE_DATE_EPOCH=0
CC="$TC/bin/aarch64-linux-cosmo-gcc"
LD="$TC/bin/aarch64-linux-cosmo-ld.bfd"
OBJCOPY="$TC/bin/aarch64-linux-cosmo-objcopy"

"$CC" -DNDEBUG -DSUPPORT_VECTOR=33 -iquote "$SRC" \
  -Wall -Wextra -fpie -Os -ffreestanding -mgeneral-regs-only \
  -fno-asynchronous-unwind-tables -fno-stack-protector \
  -fno-ident -fno-gnu-unique -g \
  -c "$SRC/ape/loader.c" -o "$B/loader.o"
for unit in start launch systemcall; do
  "$CC" -iquote "$SRC" -fpie -mgeneral-regs-only -g \
    -c "$SRC/ape/$unit.S" -o "$B/$unit.o"
done
"$LD" -pie -static -nostdlib --no-dynamic-linker -z norelro \
  -z common-page-size=0x10000 -z max-page-size=0x10000 \
  -o "$B/ape-aarch64.elf.dbg" \
  "$B/start.o" "$B/loader.o" "$B/launch.o" "$B/systemcall.o"
"$OBJCOPY" -g "$B/ape-aarch64.elf.dbg" "$B/ape-aarch64.elf"
"$TC/bin/aarch64-linux-cosmo-readelf" -h -l -d -r "$B/ape-aarch64.elf"
```

The upstream make target applies `fixupobj` after objcopy. Do **not** copy that last step blindly for a native Linux-only helper: pinned `fixupobj.c` sets non-relocatable AArch64 ELF OSABI to FreeBSD (9), writes APE modern e_flags, and rewrites TLS instructions. The standalone source uses no libc TLS. Keeping the direct linker/objcopy result with ELF OSABI 0 is a deliberate native Linux derivation; record that identity and the omitted postprocessing. This is an inference from official code, pending runtime verification. [Final upstream step](https://github.com/jart/cosmopolitan/blob/aefad27c09ffda64c6ac64bd69f758e9f9c7b91d/ape/BUILD.mk#L105-L107), [postprocessor](https://github.com/jart/cosmopolitan/blob/aefad27c09ffda64c6ac64bd69f758e9f9c7b91d/tool/build/fixupobj.c#L598-L629), [OSABI and flags functions](https://github.com/jart/cosmopolitan/blob/aefad27c09ffda64c6ac64bd69f758e9f9c7b91d/tool/build/fixupobj.c#L277-L284).

GNU ld defines `max-page-size` as the maximum supported memory page size and `common-page-size` as the layout optimization page size. The intended static check is therefore not merely that command flags were present: inspect every actual PT_LOAD, require `p_align >= 65536` and `p_vaddr % 65536 == p_offset % 65536`, then verify actual rounded mapping permissions/bounds. Require machine AArch64, native ELF OSABI 0, an entrypoint in an executable LOAD, no PT_INTERP, no DT_NEEDED, and no runtime relocations needing an absent dynamic linker. Source inspection establishes the recipe; only the real 65536-page guest establishes launch behavior. [GNU ld options](https://sourceware.org/binutils/docs/ld/Options.html).

## The helper uses AT_PAGESZ; the producer must also relink

This pinned loader does **not** hardcode 16K as its operating page size. `ApeLoader` walks the initial aux vector and reads `AT_PAGESZ`; 4096 is only its missing-auxv fallback. `Spawn` validates the target ELF congruence modulo that real page size and uses it for segment rounding/mmap offsets. The values 4096 and 16384 in MAP_ANONYMOUS/MAP_NORESERVE definitions are OS mmap flag values, not a page-size ceiling. [Auxv read](https://github.com/jart/cosmopolitan/blob/aefad27c09ffda64c6ac64bd69f758e9f9c7b91d/ape/loader.c#L1000-L1015), [congruence and rounding](https://github.com/jart/cosmopolitan/blob/aefad27c09ffda64c6ac64bd69f758e9f9c7b91d/ape/loader.c#L647-L690), [mmap use](https://github.com/jart/cosmopolitan/blob/aefad27c09ffda64c6ac64bd69f758e9f9c7b91d/ape/loader.c#L751-L791).

Task 07's actual 64K full-system guest already separates two failures: the original locked loader exits 139 by itself; an explicitly diagnostic altered-layout copy reaches its usage text (exit 1) and then rejects the unchanged fat Hello with the precise target congruence diagnostic. That diagnostic copy is not a release/helper fix. The [original report](../../out/implement-spec/07/out/diagnostic-guest.json) and [diagnostic-layout report](../../out/implement-spec/07/out/diagnostic-layout-guest.json) retain the raw logs and actual guest identities.

The locked `bin/cosmocc` source byte-for-byte equals the official pinned driver fetched here (SHA-256 `e6176e65392c4fcfa0467f09d7f1d8c94dd20d0291641ff70af174951c46b565`). That narrowly proves the driver text identity; it does not identify bundled libc source. This driver defaults AArch64 common/max-page-size to 16384 at line 429, then appends user LDARGS after its defaults at lines 697–698. The locked generated `aarch64.lds` (SHA-256 `d898d6be54a52a7019cca5a68ea7310086901df8ffd12c2ec500eb819ef3dec7`) uses `CONSTANT(MAXPAGESIZE)` and `CONSTANT(COMMONPAGESIZE)` for section/data-segment placement rather than a hardcoded 16K layout. The pinned official source has the same parameterized design. [Driver](https://github.com/jart/cosmopolitan/blob/aefad27c09ffda64c6ac64bd69f758e9f9c7b91d/tool/cosmocc/bin/cosmocc#L423-L429), [argument ordering](https://github.com/jart/cosmopolitan/blob/aefad27c09ffda64c6ac64bd69f758e9f9c7b91d/tool/cosmocc/bin/cosmocc#L689-L706), [link-script source](https://github.com/jart/cosmopolitan/blob/aefad27c09ffda64c6ac64bd69f758e9f9c7b91d/ape/aarch64.lds#L78-L131).

The driver mechanically forwards explicit page-size options after its defaults, but the producer needs a CPU-aware choice. Task 06 reports that universally applying both `max-page-size=65536` and `common-page-size=65536` breaks x86 real-mode PC16 relocations. That observed producer failure does not apply to the separate freestanding ARM64 helper recipe above. Do not use universal common-page-size=64K as an established producer fix.

Task 06 subsequently measured successful CPU links with MAX64K-only, preserving the driver's existing per-CPU COMMON defaults:

```sh
-Wl,-z,max-page-size=65536
```

The original driver now has a measured CPU-link route. The later packer extension below identifies why the resulting fat ARM offsets still lose 64K congruence and the smallest source-supported packing correction. Actual BUILDLOG plus both sidecar and packed headers must confirm the complete route. No page-size flag alone proves that the current locked libc passes pthread/TLS/mmap/ZIP on 64K. [Driver option forwarding](https://github.com/jart/cosmopolitan/blob/aefad27c09ffda64c6ac64bd69f758e9f9c7b91d/tool/cosmocc/bin/cosmocc#L601-L624), [GNU ld ordering](https://sourceware.org/binutils/docs/ld/Options.html).

## Handoff and remaining checks

1. Task 07 builds the separate helper on Ubuntu, records original compiler/native-view identities and exact compile/link argv, hashes the new helper, and runs it in the existing real 64K guest. A no-argument usage exit of 1 is expected; it is not itself a loader failure.
2. Use the measured official-driver MAX64K-only link route with a derived packer preserving incoming PT_LOAD alignment, as detailed below; record new Hello/eval/deep-probe identities and both sidecar/fat CPU headers. Inspect the bundled ARM64 loader resource separately: the original cosmocc apelink invocation still embeds its locked bin/ape-aarch64.elf. A recorded packaging override must embed the separately built helper for seamless shell startup; never overwrite the verified original toolchain tree.
3. Run the new helper against the unchanged old Hello to preserve the expected congruence rejection, then against relinked Hello/eval/deep probes. A successful new loader plus congruent targets is required before investigating further libc behavior.
4. Preserve source receipts/licenses in distribution metadata and default to offline source/hash validation after this explicit acquisition. Original ZIP, toolchain lock, original helper and libc unproven state remain unchanged.

Uncertainties: neither derived-helper recipe has been compiled in this research task; final helper/packer hashes and real 64K execution remain with task 07; the independently fetched source pin is not provenance proof for shipped helpers or bundled libc; MAX64K-only CPU linking succeeds per task 06 but final packing still needs the change and verification described below; 64K pthread/TLS/mmap/ZIP behavior remains an execution question.

## Packer extension: official apelink discards stronger alignment

The exact source path at this pin is [tool/build/apelink.c](https://github.com/jart/cosmopolitan/blob/aefad27c09ffda64c6ac64bd69f758e9f9c7b91d/tool/build/apelink.c); the official contents API returned an actual 404 for `ape/apelink.c`. Upstream source SHA-256 is `6e236b3a10b1ed13eadf389de8b826cb6d57e220271a45890626080e6265c895`, Git blob `b0ec719f15cd8b2a325cf166b020c7c0c7d79727`, 86578 bytes. The unmodified file and dependency sources below are now in the same exact-commit cache; no patch or compilation was performed by this research task.

There is no public or hidden page/alignment option in this pinned parser. `GetOpts` passes only `hvgsGBo:l:k:S:M:V:` to getopt and has no long-option or environment-alignment path. `-g`, `-G`, `-B` and `-S` control debugging, loader lookup, binfmt bypass and shell text; `-V` controls the OS support vector, not page alignment. [Complete parser](https://github.com/jart/cosmopolitan/blob/aefad27c09ffda64c6ac64bd69f758e9f9c7b91d/tool/build/apelink.c#L997-L1065), [manual](https://github.com/jart/cosmopolitan/blob/aefad27c09ffda64c6ac64bd69f758e9f9c7b91d/tool/build/apelink.c#L61-L160).

The source explains task 06's measured result. `ValidateElfImage` checks each incoming PT_LOAD's power-of-two p_align and p_vaddr/p_offset congruence, but assumes a CPU page size of 16384 for AArch64. Later `ThirdPass` independently hardcodes the same 16384, aligns the destination image to that value, and adds `offset - in->minload` to copied program-header offsets. It leaves p_align unchanged. Consequently a valid input with p_align=65536 can emerge with offset shifted by only a multiple of 16384, violating the stronger declared alignment. Task 06 measured exactly this after successful MAX64K-only input links: the packed ARM executable LOAD had offset 376832 (residue 49152 modulo 65536), vaddr 34359738368 (residue 0), and p_align 65536. [Input validation](https://github.com/jart/cosmopolitan/blob/aefad27c09ffda64c6ac64bd69f758e9f9c7b91d/tool/build/apelink.c#L468-L479), [p_align validation](https://github.com/jart/cosmopolitan/blob/aefad27c09ffda64c6ac64bd69f758e9f9c7b91d/tool/build/apelink.c#L537-L575), [packing/rebasing](https://github.com/jart/cosmopolitan/blob/aefad27c09ffda64c6ac64bd69f758e9f9c7b91d/tool/build/apelink.c#L1519-L1589).

Task 06's new measurements supersede the earlier pending producer experiment: universal MAX64K-only reaches successful CPU links while preserving the existing COMMON defaults; universal COMMON64K triggers x86 PC16 relocation failure; both official-driver MAX64K-only and a custom per-CPU link still encounter this subsequent packing defect. The supported direction is therefore the original driver plus MAX64K-only and a separately derived packer that preserves input alignment. Manual per-CPU linking does not bypass the packer problem.

### Smallest justified packing change

The packing invariant is mathematical: every rebased target offset must satisfy `new_offset = old_offset + delta`, where `delta` is divisible by every retained PT_LOAD p_align. Because validation requires powers of two, taking their maximum is sufficient. Retain the existing architecture/PE/Mach-O alignment requirements, raise `image_align` to at least the maximum PT_LOAD p_align, and choose the destination image offset congruent to `in->minload` modulo that combined alignment. The resulting delta is divisible by the alignment. A source-level implementation can use this localized change in ThirdPass:

```diff
-  vaddr = 0;
+  vaddr = in->minload;
   /* existing PE/default image_align selection stays here */
+  for (i = 0; i < in->elf->e_phnum; ++i) {
+    Elf64_Phdr *phdr = GetElfProgramHeaderAddress(in->elf, in->size, i);
+    if (phdr->p_type == PT_LOAD)
+      image_align = MAX(image_align, phdr->p_align);
+  }
   while ((offset & (image_align - 1)) != (vaddr & (image_align - 1))) {
     ++offset;
   }
```

This preserves program-header flags, virtual addresses, segment boundaries and incoming alignment declarations; it introduces padding before the copied image. It does not merge protection classes or replace the file with one RWX LOAD. The `in->minload` residue matters for inputs whose first copied file offset is not zero modulo the strongest alignment. The common case observed by task 06 has zero residue, but hardcoding zero is not the general delta-preservation rule. [minload planning](https://github.com/jart/cosmopolitan/blob/aefad27c09ffda64c6ac64bd69f758e9f9c7b91d/tool/build/apelink.c#L1441-L1461), [unchanged copy extent](https://github.com/jart/cosmopolitan/blob/aefad27c09ffda64c6ac64bd69f758e9f9c7b91d/tool/build/apelink.c#L1612-L1616).

One scope detail for implementation/review: `ValidateElfImage` may merge adjacent equal-permission loads before ThirdPass and retain the first header's p_align. For current producer inputs the relevant LOAD alignments are uniformly 65536. If the patch claims full support for arbitrary mixed p_align inputs, retain the original maximum alignment through that merge or only merge compatible alignments; do not silently discard a larger alignment from a removed header. The localized patch above preserves the validated retained headers. Check all final fat headers again after packing, since current validation runs on the incoming image. [Merge loop](https://github.com/jart/cosmopolitan/blob/aefad27c09ffda64c6ac64bd69f758e9f9c7b91d/tool/build/apelink.c#L515-L535).

### Additional source closure and licenses

The packer can be built without a libc rebuild using the locked x86_64 compiler, SDK headers, crt/ape objects and libcosmo.a. Beyond those supplied SDK inputs, it needs four C units: `apelink.c`, `parsesupportvector.c`, HACL `blake2b.c`, and HACL `notice.c`. The source-local extra headers are `tool/build/lib/lib.h` and the six HACL headers listed below. The SDK already supplies all 35 ordinary direct headers, plus their transitive headers and the mandatory normalizer. The [provider receipt](../../out/m0-integration/loader64k-research/packer-locked-sdk-providers.json) records the exact 45 direct/header/compiler/link-provider entries copied from the authoritative toolchain lock; the complete lock governs all SDK inputs. The Ubuntu compile must emit a dependency file to record its effective transitive header closure. [Packer includes](https://github.com/jart/cosmopolitan/blob/aefad27c09ffda64c6ac64bd69f758e9f9c7b91d/tool/build/apelink.c#L19-L55), [parser helper](https://github.com/jart/cosmopolitan/blob/aefad27c09ffda64c6ac64bd69f758e9f9c7b91d/tool/build/lib/parsesupportvector.c), [Blake2b includes](https://github.com/jart/cosmopolitan/blob/aefad27c09ffda64c6ac64bd69f758e9f9c7b91d/third_party/haclstar/blake2b.c#L29-L38).

The read-only [libcosmo archive-index receipt](../../out/m0-integration/loader64k-research/packer-libcosmo-index.json) finds the needed HACL entry points, its notice and ParseSupportVector absent; it finds ELF/runtime and SDK-renamed zlib providers present. Thus merely compiling apelink.c against libcosmo.a is incomplete. HACL's `__static_yoink("hacl_star_notice")` also requires notice.c. This is provider inspection, not proof of a completed link.

| Source-local input | Bytes | SHA-256 | License |
| --- | ---: | --- | --- |
| [tool/build/apelink.c](https://github.com/jart/cosmopolitan/blob/aefad27c09ffda64c6ac64bd69f758e9f9c7b91d/tool/build/apelink.c) | 86578 | `6e236b3a10b1ed13eadf389de8b826cb6d57e220271a45890626080e6265c895` | ISC |
| [tool/build/lib/lib.h](https://github.com/jart/cosmopolitan/blob/aefad27c09ffda64c6ac64bd69f758e9f9c7b91d/tool/build/lib/lib.h) | 218 | `721720a315af170c9ba5fd990cd034f1fa7169cd44e4e16feff7c2ee2e217915` | ISC |
| [tool/build/lib/parsesupportvector.c](https://github.com/jart/cosmopolitan/blob/aefad27c09ffda64c6ac64bd69f758e9f9c7b91d/tool/build/lib/parsesupportvector.c) | 3668 | `d1ea19d95cca73a24c3ef00921ea3e4f13ec13308540483cf06a84ecd4bd2c1d` | ISC |
| [third_party/haclstar/haclstar.h](https://github.com/jart/cosmopolitan/blob/aefad27c09ffda64c6ac64bd69f758e9f9c7b91d/third_party/haclstar/haclstar.h) | 10647 | `7a93c26f145646480bc3fa8694cd4f1e971ae0aa1ac6157522dc18abb817860a` | MIT |
| [third_party/haclstar/blake2b.c](https://github.com/jart/cosmopolitan/blob/aefad27c09ffda64c6ac64bd69f758e9f9c7b91d/third_party/haclstar/blake2b.c) | 57684 | `5b56cef5903470d4c86e79f5d4a5ccb0350cb36b531db90f9771200d9a65d1a6` | MIT |
| [third_party/haclstar/blake2b.h](https://github.com/jart/cosmopolitan/blob/aefad27c09ffda64c6ac64bd69f758e9f9c7b91d/third_party/haclstar/blake2b.h) | 1661 | `6de19ffd2a638f230ae05ef37c5f9116ee0dfc87594a67f8ead39f5db1dc0cd0` | MIT |
| [third_party/haclstar/int128.h](https://github.com/jart/cosmopolitan/blob/aefad27c09ffda64c6ac64bd69f758e9f9c7b91d/third_party/haclstar/int128.h) | 3123 | `2040ce4f92c625d79d234da1ea95b994b6d7d4427d011da5500aa4352f9fefd9` | MIT |
| [third_party/haclstar/loop.h](https://github.com/jart/cosmopolitan/blob/aefad27c09ffda64c6ac64bd69f758e9f9c7b91d/third_party/haclstar/loop.h) | 2779 | `0f30607f030e5b6b5264b4f89219f74cc8ac6bfd510da79318df107bd260b0ad` | MIT |
| [third_party/haclstar/macros.h](https://github.com/jart/cosmopolitan/blob/aefad27c09ffda64c6ac64bd69f758e9f9c7b91d/third_party/haclstar/macros.h) | 1193 | `c122904d357fcff5c191e6651c68c62a8c0a29ea1ff626920074254c29210818` | MIT |
| [third_party/haclstar/optional.h](https://github.com/jart/cosmopolitan/blob/aefad27c09ffda64c6ac64bd69f758e9f9c7b91d/third_party/haclstar/optional.h) | 912 | `38c8b138502a9a20eae2b348237c9f70b1cc3efa265e66d95bb293c43b12d52d` | MIT |
| [third_party/haclstar/notice.c](https://github.com/jart/cosmopolitan/blob/aefad27c09ffda64c6ac64bd69f758e9f9c7b91d/third_party/haclstar/notice.c) | 163 | `bd6f7dce5e060527b3a471ace35d8f5a74a4411ccd10dd0fc172f642e470a2e1` | MIT |

The root/project-owned sources retain ISC notices. HACL blake2b.c contains its full MIT grant and 2016–2023 INRIA/CMU/Microsoft/HACL contributor notices; `notice.c` embeds the attribution and must remain linked. The packer source is independently pinned; neither these new source receipts nor a future successful derived build prove the source identity of the shipped Nightly apelink or libc. [HACL license text](https://github.com/jart/cosmopolitan/blob/aefad27c09ffda64c6ac64bd69f758e9f9c7b91d/third_party/haclstar/blake2b.c#L1-L28), [embedded notice](https://github.com/jart/cosmopolitan/blob/aefad27c09ffda64c6ac64bd69f758e9f9c7b91d/third_party/haclstar/notice.c).

### Source-derived host x86_64 packer build on Ubuntu

The following is an adapted, explicit form of the locked driver's host compile/link recipe. It avoids invoking the original packer while building its replacement. `PACKER_C` should identify a separately copied, reviewed patch of the cached original; never modify the cached upstream file. No recipe in this note was executed by the research task.

```sh
# ROOT, PIN, SRC, TC and ARCHIVE are as in the loader recipe above.
B="$ROOT/.cache/build/apelink64k/$PIN"
LIB="$TC/x86_64-linux-cosmo/lib"
CC="$TC/bin/x86_64-linux-cosmo-gcc"
mkdir -p "$B/tmp"
export TMPDIR="$B/tmp" LC_ALL=C SOURCE_DATE_EPOCH=0
# Task 07 prepares this derived source and records its original/patch/result hashes.
PACKER_C="$B/apelink.c"

compile_packer_unit() {
  "$CC" -fportcosmo -std=gnu17 -Os -g -fno-pie \
    -D__COSMOPOLITAN__ -D__COSMOCC__ -D_COSMO_SOURCE \
    -nostdinc -iquote "$TC/include" -iquote "$SRC" -isystem "$TC/include" \
    -include libc/integral/normalize.inc \
    -mno-red-zone -mno-tls-direct-seg-refs -fno-semantic-interposition \
    -ffunction-sections -fdata-sections -MD -MF "$2.d" \
    -c "$1" -o "$2"
}
compile_packer_unit "$PACKER_C" "$B/apelink.o"
compile_packer_unit "$SRC/tool/build/lib/parsesupportvector.c" "$B/parsesupportvector.o"
compile_packer_unit "$SRC/third_party/haclstar/notice.c" "$B/hacl-notice.o"
# Upstream specifically overrides Blake2b to O2 to avoid excessive stack/code size.
"$CC" -fportcosmo -std=gnu17 -O2 -g -fno-pie \
  -D__COSMOPOLITAN__ -D__COSMOCC__ -D_COSMO_SOURCE \
  -nostdinc -iquote "$TC/include" -iquote "$SRC" -isystem "$TC/include" \
  -include libc/integral/normalize.inc \
  -mno-red-zone -mno-tls-direct-seg-refs -fno-semantic-interposition \
  -ffunction-sections -fdata-sections -MD -MF "$B/blake2b.o.d" \
  -c "$SRC/third_party/haclstar/blake2b.c" -o "$B/blake2b.o"

"$CC" -static -nostdlib -no-pie -fuse-ld=bfd \
  -Wl,-z,noexecstack -Wl,-z,norelro -Wl,--gc-sections \
  -Wl,-T,"$LIB/ape.lds" \
  -Wl,-z,common-page-size=4096 -Wl,-z,max-page-size=16384 \
  -o "$B/apelink-linux-x86_64" \
  "$LIB/ape.o" "$LIB/crt.o" \
  "$B/apelink.o" "$B/parsesupportvector.o" "$B/blake2b.o" "$B/hacl-notice.o" \
  "$LIB/libcosmo.a"
"$TC/bin/fixupobj" "$B/apelink-linux-x86_64"
"$TC/bin/x86_64-linux-cosmo-readelf" -h -l "$B/apelink-linux-x86_64"
"$B/apelink-linux-x86_64" -h
```

The intended host result is the raw x86_64 ELF from the SDK link, not an APE created by another packer. Confirm ELF magic/OSABI and actual Ubuntu execution before using it. Keeping the host tool's existing common/max settings avoids introducing the x86 PC16 failure into this bootstrap. The packer's output layout is controlled by its reviewed source change, independently of the host tool's own link-page settings. [Locked driver normalization/compile defaults](https://github.com/jart/cosmopolitan/blob/aefad27c09ffda64c6ac64bd69f758e9f9c7b91d/tool/cosmocc/bin/cosmocc#L359-L396), [x86 SDK/CRT/link selection](https://github.com/jart/cosmopolitan/blob/aefad27c09ffda64c6ac64bd69f758e9f9c7b91d/tool/cosmocc/bin/cosmocc#L416-L425), [post-link fixup](https://github.com/jart/cosmopolitan/blob/aefad27c09ffda64c6ac64bd69f758e9f9c7b91d/tool/cosmocc/bin/cosmocc#L674-L688), [HACL optimization rule](https://github.com/jart/cosmopolitan/blob/aefad27c09ffda64c6ac64bd69f758e9f9c7b91d/third_party/haclstar/BUILD.mk#L54-L75).

Task 07 still needs to prove the complete link against this locked SDK, record effective headers and unresolved-symbol diagnostics if any, compare original versus patched packer on the same MAX64K sidecars, inspect every final fat PT_LOAD at 65536, and run the new external/bundled ARM helper in the real 64K guest. Windows/macOS consumption must inspect/run the same newly packed bytes. No compiler/product source, original archive or lock was mutated by the research task.

