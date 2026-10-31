# BOOT-03 Nightly input validation

Date: 2026-11-01 (Asia/Shanghai). Branch: `codex/m0-04-nightly`.

Implementation is in progress. Ten CLI contracts passed in the existing
Ubuntu-22.04 WSL2 environment. Actual official acquisition has not succeeded yet;
no production lock, archive digest, compiler version or libc commit is invented.
The acquisition workflow is being used solely to get original upstream bytes
after local transport failures, using the same CLI as local acquisition.

## Execution evidence

- Initial integration ancestry: `efeed76` was an ancestor of the ticket checkout.
- Actual WSL host: Linux x86_64,
  `6.6.87.2-microsoft-standard-WSL2`, measured page size 4096.
- Source drive D: about 45.5 GB free; physical C: VHD backing drive about 21.3 GB
  free during acquisition preflight. The ext4 virtual filesystem reports about
  1 TB available; that value is not treated as physical backing space.
- `python3 -m unittest discover -s tests/contracts -p test_nightly_toolchain.py -v`:
  10 passed in WSL. Tests run the real CLI, real shell fixture version commands,
  ZIP/filesystem/digest operations and observable reports. Synthetic archives
  are marked local-import and never count as an official Nightly input.
- Red-green observations covered missing-lock evidence, explicit import/offline
  path, malformed lock paths, insufficient space, incomplete receipt rejection,
  exact locked-cache restoration, writable-cache refusal, and bump lineage.
  Additional corruption/rollback/network regressions passed.

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

Once official acquisition succeeds, import its original ZIP and receipt in WSL,
measure all tools and the full extracted tree in ext4, commit the actual lock,
then perform repeated offline verification and a damaged-copy rejection using
that real input. Product Hello/eval and full host probes belong to later tickets.
