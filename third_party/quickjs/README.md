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
# Explicit reviewed target selection into isolated candidate metadata.
# Copy the reviewed patches.json/patches/ there first.
python scripts/quickjs_source.py select --commit 2833510432045f72915919cef680ba59e5153b16 \
  --lock out/quickjs-candidate/source.lock.json --report out/quickjs-selection.json

# Explicit acquisition of the existing lock; never resolves master or updates it.
python scripts/quickjs_source.py fetch --report out/quickjs-fetch.json

# Build prerequisite: strictly offline, read-only source/metadata verification.
python scripts/quickjs_source.py verify --controlled-probe --report out/quickjs-source.json
```

All operations support `--lock`, `--cache`, `--report`, and `--timeout` (1–300
seconds per Git command). Production `bump` is disabled by the v3 authority.
`select` requires an exact `--commit`, an isolated new metadata path under `out/`
or `.cache/`, and a report outside that metadata/cache. It acquires the official
object without resolving master and does not promote the unified lock.
`fetch --mirror /absolute/local/git-repository` explicitly restores the same
Commit/Tree/content from already cached Git objects while retaining the official
locked repository identity; it neither resolves a moving ref nor changes the lock.
`bump --fixture-lock --repository /absolute/local/repository` is an
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
identical filesystem permissions or build output. Production v3 verification also
checks the actual Git Tree and VERSION using Git; it never fetches, pulls, or
repairs a cache.

`patches.json` records one ordered ARM64 overlay. To introduce a patch, put it
below `patches/`, add an entry in application order, and explicitly select a new
isolated candidate before generating and validating its v3 lock:

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

The current candidate selects `2833510432045f72915919cef680ba59e5153b16`; its
VERSION remains `2026-06-04`. The previous `e621135dc542e000e31038bedc52944aa0adaf1a`
manifest is retained in `history/` and Git history, together with its cached bytes
and original evidence. Actual ARM64 allocations exceeded the
header's 47-bit NaN-boxed pointer range, causing `JS_NewContextRaw` to dereference
a truncated pointer. The one-line ordered header patch selects the existing
full-pointer JSValue representation for ARM64; x86 behavior remains unchanged.
The original cache is preserved and the derived source has its own manifest and
cache key. The target's header is byte-identical to the old upstream header, so
the overlay remains unchanged. Actual target ARM64 pointer regression evidence
is recorded by [ticket 02](../../.scratch/qaplet-v15-alignment/issues/02-quickjs-upgrade.md).

`engine_compatibility_id` hashes the repository, exact commit, original and
patched content identities, and ordered patch identity. Build fingerprints
include it; equal VERSION text never authorizes bytecode/object compatibility.
There is currently no persistent bytecode reader/cache to migrate. Objects are
built in a fresh unique directory, and the target uses a distinct source cache.

The permanent M0 probe checks public JSValue pointer/tag round-tripping with
bit47 set on ARM64, without dereferencing that synthetic pointer, then exercises
actual context/object/eval/free behavior. The original header deterministically
fails this check even if ASLR happens to place current allocations below bit47.
Details and raw regressions are in the [64K validation record](../../.scratch/qaplet-m0/arm64-64k-validation.md).
