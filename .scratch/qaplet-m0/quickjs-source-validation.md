# BOOT-02 QuickJS source-lock validation

2026-11-01 local time (UTC+08). This validates the source input only. No QuickJS
compilation, Cosmopolitan compatibility, Core APE, or Runtime capability is
claimed.

## Locked input

The [official Bellard download page](https://bellard.org/quickjs/) was read and
its official GitHub link followed before acquisition. The actual CLI resolved
`https://github.com/bellard/quickjs.git`, `refs/heads/master`, rather than copying
a planning reference SHA.

| Identity | Actual value |
| --- | --- |
| Commit | `e621135dc542e000e31038bedc52944aa0adaf1a` |
| Acquisition UTC | `2026-10-31T17:47:52.375016+00:00` |
| Source SHA-256 | `708c970fcd2d4f26f4e7c1a1f1de2464d16c4d8defb5bafd8c0bdbf21b953447` |
| Source inventory | 109 regular files, 6,435,499 bytes |
| Original LICENSE SHA-256 | `598fd7fc928e4350abce36e337ba5a1346923c5c692f5be92c3d8e29ddd7c18d` |
| Ordered patch-manifest SHA-256 | `671e09884c6aecf658799b947d15de92871f7c44c78ac7f6ca42e7a4f6cbfaec` |
| Patch set | Explicitly empty; upstream and supplied source identities match |

The [source lock](../../third_party/quickjs/source.lock.json), original
[license](../../third_party/quickjs/LICENSE), [patch manifest](../../third_party/quickjs/patches.json),
and [usage/recovery contract](../../third_party/quickjs/README.md) are tracked.
The manifest SHA-256 is over compact, key-sorted UTF-8 JSON of the path/size/hash
inventory, with forward-slash paths sorted by Unicode code point. The same byte
inventory is checked on Windows and POSIX. Filesystem permission equivalence is
not asserted.

## Actual commands and evidence

Commands were run in the assigned worktree
`D:\Workspace\dairn\qaplet\out\implement-spec\03`. JSON reports and unique raw
command-log directories are retained below its ignored `out/quickjs-validation/`.
They remain local evidence; source bytes remain outside version control.

| Command | Report | Result |
| --- | --- | --- |
| `python scripts/quickjs_source.py bump --report out/quickjs-validation/bump-final.json` | `bump-final.json` | pass: resolved actual master, fetched exact commit, checked detached HEAD and clean Git status, recorded source/license/patch identities |
| `python scripts/quickjs_source.py fetch --cache out/quickjs-refetch --report out/quickjs-validation/refetch-retry.json` | `refetch-retry.json` | pass: separate cache, exact locked commit and identical source; no `ls-remote`, no lock rewrite |
| `python scripts/quickjs_source.py verify --report out/quickjs-validation/verify-final.json` | `verify-final.json` | pass: offline, no external commands |
| `python scripts/quickjs_source.py verify --cache out/quickjs-refetch --report out/quickjs-validation/verify-refetch-final.json` | `verify-refetch-final.json` | pass: second cache has identical identity |
| WSL `python3 scripts/quickjs_source.py verify --report out/quickjs-validation/verify-wsl-final.json` | `verify-wsl-final.json` | pass: same Windows-fetched source verified from actual Linux x86_64, WSL2 kernel `6.6.87.2-microsoft-standard-WSL2`, 4096-byte pages |
| WSL `make check OUT=out/quickjs-validation/wsl-check-final` | `wsl-check-final/check-ci.json`, `wsl-check-final/bootstrap.json` | pass: 36 contracts (one Windows-only skip), static checks, real system C bootstrap |
| Windows direct `check-ci`, contract discovery, and system C bootstrap entry points | `windows-check-ci.json`, `windows-bootstrap.json` | pass: Windows lacks `make` on PATH; 35-contract full discovery passed with four Linux-only skips, followed by all 11 final source contracts passing |

The complete source cache path is available from each successful report's
`source.path`. Default cache is
`out/quickjs/e621135dc542e000e31038bedc52944aa0adaf1a-671e09884c6aecf6/source`.
Consumers must run `verify` successfully against their selected cache before
using it. Reports use standalone `quickjs-source` schema 1 and keep Core status
`not_run`; the foundation-report validator does not certify these reports.

The initial restricted Windows fetch (`bump.json`) failed with
`SEC_E_NO_CREDENTIALS`; the ordinary user-context retry (`bump-user.json`) passed.
The first separate-cache fetch (`refetch.json`) failed during a TLS handshake;
one bounded retry (`refetch-retry.json`) passed. Neither failure was treated as
proof the source was available, and the original logs were preserved. Successful
acquisition used the authorized ordinary user context. No certificate checks
were disabled.

The first WSL contract run exposed Linux Git discovering the parent worktree's
Windows-formatted `.git` path and failing before upstream access. The CLI now
runs source-supply Git commands from a neutral filesystem root and uses explicit
cache repository paths. The final WSL suite passed. An automatic approval check
for one WSL verification timed out without a safety decision; its permitted
single retry completed successfully.

## Contract tests and red/green evidence

`tests/contracts/test_quickjs_source.py` tests the external CLI with real isolated
Git repositories and real subprocess logs, without importing the implementation.
The eleven contracts cover:

- Missing lock/cache blocks without fetching; successful bump records the actual
  resolved commit and original license; offline verification works with empty
  PATH and keeps the same identity.
- Exact-commit fetch remains fixed after upstream master advances and never
  rewrites the lock or resolves moving master.
- Explicit ordered patches are applied by real `git apply` and reproduced by
  fresh fetch; patch manifest or byte changes fail verification.
- Changed source, additional source file, missing original license, and wrong
  checkout HEAD fail offline; malformed lock/schema/ref/time/repository fail.
- Failed upstream access preserves the accepted lock and raw failure logs;
  corrupted existing cache cannot cause a bump to replace the prior lock.
- Portable source-path order is fixed across Windows/POSIX; missing license or
  patch metadata is rejected before any fetch command runs.

The initial missing-lock test failed before the CLI existed. Subsequent slices
were observed red before implementing bump, exact-commit fetch, patch-manifest
verification, ordered patch application, preservation of the prior lock on
corrupt cache, portable path ordering, lock validation, and early metadata
rejection. Existing source-corruption cases also pass at the same public seam.

## Integration and recovery

Build entry points should call
`python scripts/quickjs_source.py verify --report <OUT>/quickjs-source.json` and
stop on nonzero status. Explicit network entry points can call `fetch`; an
intentional dependency update calls `bump` and reviews the metadata diff.
No shared Makefile, foundation schema, or development documentation was changed
by ticket 03.

Restore a prior reviewed metadata commit plus its verified cache to roll back.
Failed candidates/logs are retained; no automatic fallback to a planning SHA,
old fork, or moving HEAD exists. Remaining product compilation and offline
rebuild acceptance belong to tickets 05 and 09.
