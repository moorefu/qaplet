# Locked cosmocc Nightly

`scripts/nightly_toolchain.py` is the shared CLI. A production lock is generated
only after actual official acquisition, ZIP checks, full file hashing, and three
successful compiler version commands. A missing lock is a missing prerequisite;
there is no placeholder fallback.

The committed lock is the original `cosmocc` artifact from the official
[Nightly run 41169443573](https://github.com/jart/cosmopolitan/actions/runs/41169443573),
artifact `13031425078`, not the cosmo.zip server's packaging. Its original ZIP is
419,652,152 bytes, SHA-256
`f2f984d3a9790674fc0da11c1c00a32d4fe3f8dafde99af74b0c6dc24a2a16cf`.
The receipt is `cosmocc.acquisition.json`; measured versions, full content/link
manifest and source identities are in `cosmocc.lock.json`. `validation.json`
records the offline/restore/refusal summary. Raw logs in `evidence/` are local
generated outputs and are ignored by Git; fresh CLI runs retain reports and
raw logs under `out/`.

```sh
# Initial cosmo.zip acquisition in a checkout without a lock (separate provider).
python3 scripts/nightly_toolchain.py fetch --backing-dir /mnt/c

# Normal consumers: strictly offline, no repair or update.
python3 scripts/nightly_toolchain.py verify
python3 scripts/nightly_toolchain.py path

# Explicitly restore this GitHub lock from the original archive copy.
python3 scripts/nightly_toolchain.py fetch --archive /path/to/original-nightly.zip \
  --acquisition-report toolchains/cosmocc.acquisition.json --backing-dir /mnt/c

# Deliberate GitHub Nightly update; retains previous caches.
python3 scripts/nightly_toolchain.py bump --archive /path/to/new-nightly.zip \
  --acquisition-report /path/to/new-acquisition.json --backing-dir /mnt/c
```

Defaults are `toolchains/cosmocc.lock.json`,
`<repository>/.cache/toolchains/cosmocc/<archive-sha256>/{archive.zip,tree/}`, and
`out/nightly-toolchain.json`. `--lock`, `--cache-dir`, and `--report` override them.
All required actual inputs belong under the current repository's `.cache/`,
including through its WSL path. Explicit `--cache-dir` remains available for
isolated fixtures. Source/cache free space is measured on their actual volumes.
WSL caches on a VHD filesystem additionally require explicit `--backing-dir` for
its physical volume; a Windows-drive DrvFS cache does not claim VHD storage.
Acquisition requires a 4 GiB free-space floor by default and checks expanded ZIP
size plus a 512 MiB reserve before extraction. A failed disk/network/compiler or
integrity check cannot replace the lock. `fetch` with an existing lock never
changes it: a verified cache is reused, and missing cache requires the exact
locked archive length/SHA-256. Rolling upstream drift fails and requires `bump`.
An existing GitHub-provider lock with missing cache blocks when `--archive` is
omitted; it never silently downloads canonical cosmo.zip bytes. Obtain the
original ZIP through the signed-in [artifact page](https://github.com/jart/cosmopolitan/actions/runs/41169443573/artifacts/13031425078)
or its authenticated GitHub API URL from the lock, and import it with the command
above. Authentication remains outside the offline verifier and tokens are never
written to the lock or command logs. Retain the original verified ZIP externally:
GitHub artifacts can expire. An expired artifact is not a reason to change the
lock or substitute new rolling bytes.

For a Windows or remote download followed by Linux measurements:

```sh
python scripts/nightly_toolchain.py download \
  --download-to out/nightly-download/cosmocc.zip \
  --report out/nightly-download/acquisition.json --download-timeout 900
python3 scripts/nightly_toolchain.py fetch \
  --archive out/nightly-download/cosmocc.zip \
  --acquisition-report out/nightly-download/acquisition.json \
  --backing-dir /mnt/c
```

`download` only saves original bytes and a receipt; it does not validate compiler
execution or write a lock. The receipt records successful authenticated HTTPS
from the fixed official URL, measurement time, length and digest. Import
rechecks the bytes against that trusted receipt before assigning official origin.
cosmo.zip receipt commands must match the published curl option grammar: full
allowlisted options, one official URL, authenticated HTTPS for transfers and
redirects, and no duplicate options. Both historical and current command forms
are accepted; abbreviations, short-option clusters, config overrides and extra
transfers are rejected. Current acquisition disables implicit curl config.
The receipt is local evidence, not an upstream signature; preserve it and its raw
logs. The scoped acquisition workflow uses this same command and never updates
the lock. Windows uses its configured HTTPS proxy when curl has no explicit
`HTTPS_PROXY`; proxy values and credentials are not serialized.

The GitHub receipt additionally contains the original run/artifact API objects.
Import requires repository `jart/cosmopolitan`, workflow
`.github/workflows/nightly-cosmocc.yml`, a successful completed run, artifact name
`cosmocc`, matching run/head/artifact identities, and the API SHA-256/length equal
to the measured original ZIP. The lock preserves the immutable run/artifact IDs,
workflow commit, API URL, metadata identities and receipt digest. It does not
claim this artifact ZIP equals canonical cosmo.zip packaging.

`--archive` without a receipt creates a `local-import` test/recovery candidate.
Such a lock cannot be consumed by ordinary `verify`/`path`; explicitly using
`--allow-local-import` is for CLI fixture tests only. Existing production locks
can recover from an exact archive copy without a new receipt because their
authoritative digest already identifies those bytes.

Each extracted file's digest, length and execute bit is locked. Relative symlinks
are locked by their target text and validated resolution within the tree. ZIP
paths cannot escape; absolute/escaping/dangling/cyclic links, links used as
extraction parents, encrypted members, special files and duplicate entries are rejected,
and the cache is made read-only where the filesystem represents those modes.
Verification checks the original ZIP and all extracted files before running
version commands; missing, writable regular files or modified caches are refused.
On capable POSIX filesystems, directory no-write and file execute bits remain
strictly checked. For a detected WSL DrvFS mount, a disposable sibling probe
measures chmod behavior and reports `filesystem_permissions`. If regular-file
no-write cannot be enforced, verification blocks. If file execute bits or
directory no-write cannot be represented, the report explicitly records those
limits under `drvfs-no-metadata-v1`: execute identity comes from the unchanged
original ZIP and lock, all regular files must still be non-writable, tools must
actually execute, and complete bytes/types/entry sets/link targets and safe link
resolution remain checked. A different filesystem cannot opt into this policy.
Verification never silently repairs an accepted cache. Versions are measurements,
not evidence that Hello/eval or the product matrix has passed. The report has its
own `report_kind: nightly-toolchain`, schema version 1 and named `nightly.*`
cases; the BOOT-01 report-schema validator does not certify this separate format.

The actual upstream ZIP retains its 77 executables and 208 symlinks. No execute
bits are repaired or invented. Both GCC APE version queries explicitly use the
included native Linux x86_64 ELF loader, whose digest is also recorded, bypassing
WSLInterop without changing global binfmt configuration.

`source_head` is a separate optional observation, currently null. The bundled
`libc_source` remains `{status: unproven, commit: null}`: neither the source
repository HEAD nor GCC's version string proves which Cosmopolitan commit built
the archive. Version 1 deliberately refuses unsupported claims of a proven
mapping. Empty/placeholder hashes, missing compiler identities and malformed
locks are rejected.

Upstream references checked during implementation:

- [Official acquisition instructions](https://github.com/jart/cosmopolitan/blob/master/README.md#getting-started)
  name `https://cosmo.zip/pub/cosmocc/cosmocc.zip`.
- [Toolchain documentation](https://github.com/jart/cosmopolitan/blob/master/tool/cosmocc/README.md)
  describes compiler wrappers, portable tools, and APE shell entry from Python.
- [WSL notes](https://github.com/jart/cosmopolitan/blob/master/README.md#wsl)
  describe the Windows interop hazard. This CLI does not change global binfmt
  settings. Product builds must record their actual loader and execution mode.
