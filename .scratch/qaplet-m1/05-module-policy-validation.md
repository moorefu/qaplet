# 05 · Frozen modules and strict module-name/path policy

Work in progress on `codex/m1-05`, based on integration `6139b75`.
Ticket remains claimed until its ordinary producer and required platform
verification complete. No complete M1 matrix or release-readiness claim.

## Exact input and selected probes

The original Bellard commit `2833510432045f72915919cef680ba59e5153b16`,
tree `b2763aa1a9395672adc131322f17d7fad7383053`, first ARM64 patch and
fixed cosmocc archive are unchanged. The second patch rejects static/computed
module-name NUL before C-string callbacks lose the length; ordinary JS strings,
property atoms and SDK copied strings retain NUL semantics. New patch SHA is
`5c559b810cd1e280ec3426ad18f64f6ea49068a9edfd8712ed1e9b1ba1317b66`.

Actual source cache: main `.cache/quickjs/2833510432045f72915919cef680ba59e5153b16-d418e131993e6539/source`.
Patched inventory `e4dee17eb0a1bb1c109b96f5236cf26f8ed346f9075c91d4ce3265f6ab85b805`;
engine compatibility `ca0f112ee82774eaa48aa1d2e646f6fb13d7862119e4797d819009f572307ad6`.
New input_id `f07f23c64bfcfb4a92722723e3e673213534a6fa81e690c401bd9fa10e0fd178`.

Fresh official Git fetch failed through WSL HTTP2, WSL HTTP1.1 and Windows
schannel; `out/m1-05/source-select*.json` retains each actual failure and raw
commands. With explicit parent approval, `offline-source-reconstruction.json`
records local transport from the fully verified original cached Git objects,
independent original Commit/Tree/full upstream inventory verification, ordered
patch application, and actual new manifest generation. No old cache was edited;
no successful new official acquisition is claimed. The original source lock
is retained under `third_party/quickjs/history/`.

`input-migration.json` records real v3 migration to selected; the selected
original remains `toolchains/m1-module-name-candidate.lock.json` and
`out/m1-05/selected-original.lock.json`. Ordinary selected SDK build and run
both reject before execution (`selected-ordinary-refusal/`).

`green-nul-controlled-v2/build.json` and `run.json` retain committed source
`a67995787168edf5facbd5f0354c4d8c41351c11` and 23/23 actual Ubuntu Host cases.
SDK recipe `f1e4c1f318a81219a1aa0eb35cdeb091ec7bd7c2ed796acafac1f386c5d6de32`,
SDK aggregate `5b0fdd0277528b9508a8e1c36f49e37a9a2586ad3f490e0d7b7cf1f8d1494f4a`,
Host APE `4e32ba5b699b62241fd59b72270a77f2e9b7fcd58c1f474c8da4d495ce46ff02`.
The NUL red original (`red-nul/`) imports the existing baseline incorrectly;
green covers static `\0`/`\u0000`, computed NUL before/after valid filename,
caught rejection, and successful ordinary copied NUL string.

Parent executed the same 23 cases on normal-user Windows x64 native PE
(`green-nul-controlled-v2/windows-frozen-run.json`) and full-system emulated
ARM64/65536-byte pages (main `out/m1/05-guest-controlled-nul/run.json` with
selection/payload). Exact `a679957` source was exported and every recipe file
verified against the producer because the working recipe advanced. The earlier
Windows recipe mismatch remains a refusal, not a relabeled pass.

Existing controlled M0 producer/startup pass 17/17 each in `m0-controlled/`.
Parent's main `out/m1/05-m0-controlled-guest/{export,payload,run,startup}.json`
records actual ARM64 64K 17/17 plus independent startup import. These actual
selected receipts are individually hashed in `promotion-inputs.json`; only
canonical lock_state changes to pinned. Fresh ordinary evidence remains required.

## Cache and path slices

`cache-controlled/{build,run}.json` passes 24/24 through the supplied public
SDK/Host. Temporary actual files prove successful source freeze/deletion,
load/parse/link/eval/TLA failure freeze, caught dynamic link failure before and
after file repair, fresh Runtime repair, retained partial effects, canonical
failure accounting and no new charge on cache hits. An unresolvable missing
request consumes no identity and can discover a newly created file later.

`red-policy/` retains a real Host compilation failure: this supplied libc has
`mknod` but no declared `mkfifo`; the fixture was corrected using the public
libc header. `red-policy-v2/` is an audited behavioral red: the extra-slash file
URL loads the existing module. All other 25 Host cases pass there, including
actual Ubuntu symlink normalization, frozen retargeted alias and distinct hard
link identities. Mutable fixtures never modify delivered SDK fixture bytes.
