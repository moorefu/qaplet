# BOOT-03 Nightly input validation

Date: 2026-11-01 (Asia/Shanghai). Branch: `codex/m0-04-nightly`.

BOOT-03 is resolved. The original artifact from the official Cosmopolitan
Nightly workflow was acquired, verified against GitHub's artifact digest/length,
cached on WSL ext4 and locked. True disconnected-network verification and exact
cache restoration passed; same-length archive corruption and missing cache were
refused. This certifies the toolchain input/cache, not product APE or the M0 gate.

The canonical cosmo.zip transport failures below are retained as history. The
successful source is a distinct official GitHub Nightly artifact, not a claim
that both channels package identical bytes.

## Execution evidence

- Initial integration ancestry: `efeed76` was an ancestor of the ticket checkout.
- Actual WSL host: Linux x86_64,
  `6.6.87.2-microsoft-standard-WSL2`, measured page size 4096.
- Source drive D: about 45.5 GB free; physical C: VHD backing drive about 21.3 GB
  free during acquisition preflight. The ext4 virtual filesystem reports about
  1 TB available; that value is not treated as physical backing space.
- `python3 -m unittest discover -s tests/contracts -p test_nightly_toolchain.py -v`:
  14 passed in WSL. Tests run the real CLI, real shell fixture version commands,
  ZIP/filesystem/digest operations and observable reports. Synthetic archives
  are marked local-import and never count as an official Nightly input.
- Red-green observations covered missing-lock evidence, explicit import/offline
  path, malformed lock paths, insufficient space, incomplete receipt rejection,
  exact locked-cache restoration, writable-cache refusal, and bump lineage.
  Additional corruption/rollback/network regressions passed.
- Required `make check OUT=out/nightly/check-wsl`: passed, 36 contracts with one
  Windows-only skip; static CI check and real Linux system-C bootstrap passed.
  This verifies implementation regression, not actual Nightly/product support.
- The same check passed in the ordinary Windows user context with installed
  `C:/msys64/usr/bin/make.exe`, `PYTHON=python CC=gcc`, and
  `OUT=out/nightly/check-windows`: 36 contracts, 11 platform-specific skips;
  static CI check and real Windows system-C bootstrap passed.
- Final checks after merging QuickJS and adding actual GitHub/symlink handling:
  `make check OUT=out/nightly/final-check-wsl` passed 50 tests (one Windows-only
  skip); ordinary Windows `make check PYTHON=python CC=gcc
  OUT=out/nightly/final-check-windows` passed 50 tests (13 platform skips).
  Both static CI checks and real system-C bootstraps passed. Fourteen Nightly
  contracts pass in WSL; five applicable contracts pass in Windows and nine
  POSIX/tool-execution contracts are explicitly skipped there.

The regression suite covers missing cache; rolling/archive digest drift;
truncated ZIPs; traversal and symlink archive members; non-executable tools;
altered extracted files; writable caches; preservation of old lock/cache after
failed bump; retention of previous caches after successful bump; and a deliberate
network-boundary failure with retained raw stderr. Normal verify/path make no
network calls. The network test substitutes only the external curl boundary,
not internal implementation functions.

## Local acquisition failures retained

1. WSL and Windows direct HTTPS to the official Nightly channel both timed out
   connecting. The publisher mirror `https://justine.lol/cosmopolitan/cosmocc.zip`
   also timed out; no mirror identity is assumed.
2. The normal user's Windows Internet Settings HTTPS proxy was found. The CLI
   uses it without writing proxy settings or credentials into command logs.
   Its actual `download` command reached a TLS handshake timeout, curl exit 28.
3. A final Windows Python urllib/OpenSSL HEAD request through configured proxy
   also timed out during TLS handshake. No TLS checks were disabled.
4. An independent source HEAD query from WSL `/tmp` failed with an HTTP/2 framing
   error. The Windows worktree `.git` indirection is also not WSL git compatible;
   that is unrelated to archive or libc identity. `source_head` stays null.
5. The same CLI ran on GitHub Actions Linux x64, run `41227913122`, job
   `124615836863`, and also failed at official cosmo.zip connection timeout
   (curl 28). Its evidence-only artifact `13052232589` was retained and retrieved
   by the integration agent. This is an acquisition failure, not a compiler test.

Local generated evidence (ignored, not committed):

- `out/nightly/official-fetch.json` and its `evidence/<uuid>/` raw curl logs.
- `out/nightly/windows-download.json` and its raw curl TLS timeout log.
- `out/nightly/network/urllib-official-head.json`, `.stdout.log`, `.stderr.log`.

These failures are scoped to their actual execution/network contexts. They do
not imply the official service is down or that the machine lacks WSL.

## Provenance and next acceptance step

[Official README](https://github.com/jart/cosmopolitan/blob/master/README.md#getting-started)
was checked directly and advertises the exact rolling cosmocc ZIP URL.
[Toolchain documentation](https://github.com/jart/cosmopolitan/blob/master/tool/cosmocc/README.md)
was also checked, including Python/shell entry and WSL interop notes. These docs
do not establish which source commit built any unmeasured Nightly archive.

## Actual official Nightly input

[Official run 41169443573](https://github.com/jart/cosmopolitan/actions/runs/41169443573)
completed successfully from workflow `.github/workflows/nightly-cosmocc.yml` at
commit `aefad27c09ffda64c6ac64bd69f758e9f9c7b91d`. The original `cosmocc`
artifact `13031425078` was downloaded through authenticated GitHub API access;
the downloaded bytes and all ZIP CRCs were independently checked. Its provenance
receipt includes the original run/artifact API objects, measured acquisition
time, original command and transparent output/timing capture notes.

| Measurement | Actual value |
| --- | --- |
| Original ZIP size | 419,652,152 bytes |
| Original ZIP SHA-256 | `f2f984d3a9790674fc0da11c1c00a32d4fe3f8dafde99af74b0c6dc24a2a16cf` |
| Retrieved at (UTC, preserved receipt text) | `2026-10-31T23:31:51.8024857Z` |
| Manifest | 3,043 file/link entries, including 208 internal symlinks |
| Original executable entries | 77; preserved from ZIP, no permission repair |
| `bin/cosmocc` | GCC 14.1.0; SHA-256 `e6176e65392c4fcfa0467f09d7f1d8c94dd20d0291641ff70af174951c46b565` |
| `bin/x86_64-linux-cosmo-gcc` | GCC 14.1.0; SHA-256 `79e74b53b2eaf584ccc7e91c210df6caa58e88e49bcb57ac6fd31c771aef9c8f` |
| `bin/aarch64-linux-cosmo-gcc` | GCC 14.1.0; SHA-256 `e5aadb78991710a02938d483954b1750ad20a693b39b45fb888ff4be10ec099d` |
| Native Linux APE loader | `bin/ape-x86_64.elf`, SHA-256 `bf65f331e87544761b8ed3a2c03a61c1b61072c8776b689a67b06f50eefd546f` |
| libc commit mapping | unproven, null; workflow run SHA is a separate provenance fact |

The authoritative [lock](../../toolchains/cosmocc.lock.json) contains the full
measured versions, archive/file/link/compiler identities and source URL. The
[acquisition receipt](../../toolchains/cosmocc.acquisition.json) is preserved
byte-for-byte (including original line endings); its SHA-256 is in the lock.
[Validation summary](../../toolchains/validation.json) is tracked. Raw logs in
`toolchains/evidence/` are local generated outputs and are ignored by Git; preserve
them locally or through CI artifacts during worktree cleanup rather than staging
them. Fresh execution reports and raw logs belong under `out/`.

The native Linux loader explicitly launched the two APE GCC executables. The
shell driver ran through its own shebang. No WSLInterop or global binfmt setting
was changed. Symlink target text is locked; absolute/escaping/dangling/cyclic
links and archive members written through a link parent fail before executing
tools. Every cache verification checks the original ZIP and full extracted
manifest before version queries. The actual source archive retained modes and
links; broad assumptions about generic artifact permission loss were not applied.

## Actual offline and corruption experiment

The successful import command was:

```sh
python3 scripts/nightly_toolchain.py fetch \
  --archive /mnt/d/Workspace/dairn/qaplet/out/m0-integration/network-candidates/official-nightly-cosmocc-41169443573.zip \
  --acquisition-report toolchains/cosmocc.acquisition.json \
  --backing-dir /mnt/c --report out/nightly/github-import.json
```

Actual cache root:
`/home/moore/.cache/qaplet/toolchains/cosmocc/f2f984d3a9790674fc0da11c1c00a32d4fe3f8dafde99af74b0c6dc24a2a16cf/`.
The source D: filesystem, ext4 cache and physical C: backing volume were measured
separately. Physical backing had about 18.6 GB free at final import, above the
4 GiB floor; the ext4 virtual capacity was not substituted for that observation.

```sh
unshare --user --map-root-user --net \
  python3 scripts/nightly_toolchain.py verify \
  --report out/nightly/offline-unshare.json
```

This passed with actual disconnected networking in a temporary user/network
namespace. The same isolation was used for the actual-input experiment:

1. A nonexistent cache returned blocked and ran no tool/network commands.
2. A copy of the actual 419,652,152-byte ZIP had its first byte flipped. Length
   remained identical; explicit fetch into an empty alternate cache refused its
   changed digest before extraction or any compiler commands.
3. The untouched original ZIP restored the committed lock into another empty
   ext4 cache without network or lock changes. That restored cache then passed
   verification; the main cache passed another repeated offline verification.
4. The authoritative lock bytes and existing primary cache were preserved.
   Temporary damaged/restoration copies were removed after their measured
   results/raw logs were retained.

`path`/`verify` remain strictly offline. `fetch` with the committed GitHub lock and
missing cache requires the exact original artifact import, and blocks without
an archive rather than silently requesting canonical rolling bytes. Portable
recovery instructions and deliberate bump are documented in
[toolchains/README.md](../../toolchains/README.md). Product Hello/eval, fat-APE
construction and host probes remain later tickets.
