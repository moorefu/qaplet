# Locked cosmocc Nightly

`scripts/nightly_toolchain.py` is the shared CLI. A production lock is generated
only after actual official acquisition, ZIP checks, full file hashing, and three
successful compiler version commands. A missing lock is a missing prerequisite;
there is no placeholder fallback.

```sh
# Initial official acquisition and measurement (existing WSL, ext4 cache).
python3 scripts/nightly_toolchain.py fetch --backing-dir /mnt/c

# Normal consumers: strictly offline, no repair or update.
python3 scripts/nightly_toolchain.py verify
python3 scripts/nightly_toolchain.py path

# Explicitly obtain the same locked bytes in a clean checkout/cache.
python3 scripts/nightly_toolchain.py fetch --backing-dir /mnt/c

# Deliberate input update; leaves previous content-addressed caches intact.
python3 scripts/nightly_toolchain.py bump --backing-dir /mnt/c
```

Defaults are `toolchains/cosmocc.lock.json`,
`~/.cache/qaplet/toolchains/cosmocc/<archive-sha256>/{archive.zip,tree/}`, and
`out/nightly-toolchain.json`. `--lock`, `--cache-dir`, and `--report` override them.
In WSL the cache should be on ext4; `--backing-dir /mnt/c` measures the physical
volume holding this machine's VHD, separately from ext4 virtual capacity.
Acquisition requires a 4 GiB free-space floor by default and checks expanded ZIP
size plus a 512 MiB reserve before extraction. A failed disk/network/compiler or
integrity check cannot replace the lock. `fetch` with an existing lock never
changes it: a verified cache is reused, and missing cache requires the exact
locked archive length/SHA-256. Rolling upstream drift fails and requires `bump`.

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
The receipt is local evidence, not an upstream signature; preserve it and its raw
logs. The scoped acquisition workflow uses this same command and never updates
the lock. Windows uses its configured HTTPS proxy when curl has no explicit
`HTTPS_PROXY`; proxy values and credentials are not serialized.

`--archive` without a receipt creates a `local-import` test/recovery candidate.
Such a lock cannot be consumed by ordinary `verify`/`path`; explicitly using
`--allow-local-import` is for CLI fixture tests only. Existing production locks
can recover from an exact archive copy without a new receipt because their
authoritative digest already identifies those bytes.

Each extracted file's digest, length and execute bit is locked. ZIP paths cannot
escape, symlinks and special files are rejected, duplicate file entries fail,
and the cache is made read-only. Verification checks the original ZIP and all
extracted files before running version commands; missing, writable or modified
caches are refused. It never silently repairs them. Versions are measurements,
not evidence that Hello/eval or the product matrix has passed. The report has its
own `report_kind: nightly-toolchain`, schema version 1 and named `nightly.*`
cases; the BOOT-01 report-schema validator does not certify this separate format.

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
