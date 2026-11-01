# QuickJS source input

`source.lock.json` records the actual Bellard `refs/heads/master` commit, acquisition
time, complete upstream and patched file inventories, their SHA-256 identities,
the original license digest, and the ordered patch manifest digest. The official
[Bellard download page](https://bellard.org/quickjs/) identifies the
[upstream repository](https://github.com/bellard/quickjs).

Only metadata, the unmodified upstream `LICENSE`, and patches belong here. Source
checkouts and failed candidates stay under the repository-local `.cache/quickjs/`
directory by default. No
QuickJS compilation or Runtime compatibility is established by this source lock.

Run from the repository root (use `python3` on Linux):

```sh
# Explicit initial acquisition or dependency update; resolves master once.
python scripts/quickjs_source.py bump --report out/quickjs-bump.json

# Explicit acquisition of the existing lock; never resolves master or updates it.
python scripts/quickjs_source.py fetch --report out/quickjs-fetch.json

# Build prerequisite: strictly offline, read-only source/metadata verification.
python scripts/quickjs_source.py verify --report out/quickjs-source.json
```

All operations support `--lock`, `--cache`, `--report`, and `--timeout` (1–300
seconds per Git command). `bump --repository /absolute/local/repository` is an
explicit local fixture/mirror seam; its actual local repository identity is
recorded instead of claiming an official fetch. The default and only accepted
remote repository is `https://github.com/bellard/quickjs.git`.
The default cache is `<repository>/.cache/quickjs/`; an explicit `--cache` selects
another isolated cache. All required actual input data belongs under the current
repository's `.cache/`, including when invoking the CLI from WSL.

Exit statuses are `0` for verified input, `1` for invalid/modified input, and `2`
for a missing prerequisite or failed upstream command. Reports use their own
`quickjs-source` schema version 1, include measured host identity and raw command
logs, and keep `m0_core_status` at `not_run`. They do not use the foundation report
validator. Each invocation keeps a separate `evidence/<uuid>/` log directory.
Consumers use `source.path`, `source.commit`, `source.sha256`, and
`source.lock_sha256` only after a successful `verify`.

Verification hashes every source file and rejects changed, missing, additional,
or symlinked source entries, the wrong detached checkout commit, a modified
license, and any patch manifest/content change. `.git/` is checkout metadata;
source identity excludes it, while the detached HEAD must match the lock. Source
file byte identity is portable between Windows and POSIX; it does not assert
identical filesystem permissions or build output. No Git executable is required
for offline verification, and it never fetches, pulls, or repairs a cache.

`patches.json` starts with an explicit empty array. To introduce a patch, put it
below `patches/`, add an entry in application order, and run explicit `bump`:

```json
{
  "schema_version": 1,
  "patches": [
    {
      "path": "patches/0001-description.patch",
      "sha256": "<actual SHA-256 of the patch bytes>",
      "origin": "<source URL, issue, or author identity>",
      "reason": "<why this engine change is required>"
    }
  ]
}
```

The CLI checks patch digests before acquisition and runs real `git apply --check`
and `git apply` in order. It records upstream and patched source separately.
`fetch` reproduces the same patch sequence and compares both identities.
`.gitattributes` preserves metadata, license, and patch bytes across checkout.

A failed candidate does not replace the accepted lock. If lock publication fails,
the previous license and patch-manifest bytes are restored so the accepted input
still verifies. Git command timeouts terminate the process tree and retain raw
output; noncanonical manifest paths are rejected with a fresh failure report.
Restore a previous
reviewed metadata commit and its verified cache to roll back; use an explicit
new `--cache` directory to diagnose corruption without erasing prior evidence.
Ordinary builds must call `verify` and stop on any nonzero status. They must not
call `bump` or `fetch` implicitly.

The current M0 overlay keeps the exact `e621135dc542e000e31038bedc52944aa0adaf1a`
upstream commit and original license. Actual ARM64 allocations exceeded the
header's 47-bit NaN-boxed pointer range, causing `JS_NewContextRaw` to dereference
a truncated pointer. The one-line ordered header patch selects the existing
full-pointer JSValue representation for ARM64; x86 behavior remains unchanged.
The original cache is preserved and the derived source has its own manifest and
cache key. This update did not resolve moving master or invoke `bump`.

The permanent M0 probe checks public JSValue pointer/tag round-tripping with
bit47 set on ARM64, without dereferencing that synthetic pointer, then exercises
actual context/object/eval/free behavior. The original header deterministically
fails this check even if ASLR happens to place current allocations below bit47.
Details and raw regressions are in the [64K validation record](../../.scratch/qaplet-m0/arm64-64k-validation.md).
