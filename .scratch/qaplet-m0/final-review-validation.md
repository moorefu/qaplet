# M0 全量双轴审查（2026-11-01）

**Status:** claimed / findings-being-fixed
**Base:** efeed762290823ae01130ee9217127cf21aab4c3
**Reviewed HEAD:** 4f5417ae54c33f983cb103f33107d92d0e0fba72
**Diff:** `git diff efeed762290823ae01130ee9217127cf21aab4c3...HEAD`

两轴使用独立 fresh context；完整 commit list、diff 和原始报告保存在 `out/m0-integration/final-review-inputs/` 与 `out/m0-integration/final-review/`。以下保留两轴原结论，修复状态另行追加。

## Standards
Reviewed pinned HEAD `4f5417ae54c33f983cb103f33107d92d0e0fba72` against `efeed762290823ae01130ee9217127cf21aab4c3`.

1. **[P2] Documented packaging breach — independent helper distributions omit notices.** [svc_job.py](D:/Workspace/dairn/qaplet/scripts/svc_job.py:104) and [svc_scm.py](D:/Workspace/dairn/qaplet/scripts/svc_scm.py:89) copy the source-built ARM64 loader into separately uploaded application bundles, but never create the notice directory. The same omission exists in standalone [native_io.py](D:/Workspace/dairn/qaplet/scripts/native_io.py:113) and [pages64k.py](D:/Workspace/dairn/qaplet/scripts/pages64k.py:69) outputs. The hunk is `shutil.copyfile(helpers["loader"]["path"], output / "ape-aarch64.elf")`; only [build_ape.py](D:/Workspace/dairn/qaplet/scripts/build_ape.py:222) publishes the accompanying files. [third_party/ape-helpers/README.md](D:/Workspace/dairn/qaplet/third_party/ape-helpers/README.md:42) states, “Producer distributions include source metadata and license text”; [THIRD_PARTY_NOTICES.md](D:/Workspace/dairn/qaplet/THIRD_PARTY_NOTICES.md:11) identifies `artifacts/ape-helper-notices/{LICENSE,source.lock.json,HACL-LICENSE.txt}`. The saved SCM artifact directory also contains no notices. Publish the applicable notices in every independently distributed producer output. The combined M0 bundle already contains the startup producer’s copy.

2. **[P3] Possible Duplicated Code — judgement call.** Artifact publication repeats `for suffix in (".com", ".com.dbg", ".aarch64.elf")` followed by the same two-CPU map discovery/count check in [build_ape.py](D:/Workspace/dairn/qaplet/scripts/build_ape.py:271), [native_io.py](D:/Workspace/dairn/qaplet/scripts/native_io.py:110), [pages64k.py](D:/Workspace/dairn/qaplet/scripts/pages64k.py:61), [svc_scm.py](D:/Workspace/dairn/qaplet/scripts/svc_scm.py:68), and [svc_job.py](D:/Workspace/dairn/qaplet/scripts/svc_job.py:83). A shared publication helper would keep suffix/layout checks consistent while retaining each producer’s separate audit and report.

**Totals:** 2 findings: 1 documented-standard breach and 1 baseline-smell judgement. No other documented-standard breaches identified. Read-only source review; no new builds, Windows fixtures, or external provenance verification. Existing pass results were not substituted for code inspection.


## Spec

2 findings.

- **[P2] Make the isolated worker's report directory writable by its target UID.** [offline_integrity.py:208](D:/Workspace/dairn/qaplet/scripts/offline_integrity.py:208) places the child's report inside the parent evidence directory, which [line 420](D:/Workspace/dairn/qaplet/scripts/offline_integrity.py:420) creates before dropping privileges. With normal Ubuntu POSIX permissions and umask 022, that new directory is root-owned 0755; the UID 1000 worker cannot create its nested evidence directory or report. The documented `sudo python3 scripts/offline_integrity.py preflight --uid 1000 --gid 1000 --report out/fresh/preflight.json` therefore fails before preflight/build execution. No ownership/access setup exists. DrvFS's documented permission behavior masks this in the retained run. Spec: [issue 09:13](D:/Workspace/dairn/qaplet/.scratch/qaplet-m0/issues/09-offline-integrity.md:13), “维护者能断网重构相同输入”.

- **[P2] Accept a recipe comparison with no changed inputs.** [core_gate.py:793](D:/Workspace/dairn/qaplet/scripts/core_gate.py:793) unconditionally indexes `changed_inputs[0]`; [line 802](D:/Workspace/dairn/qaplet/scripts/core_gate.py:802) then requires exactly one changed `scripts/build_ape.py`. A fresh disconnected pair built from the unchanged current recipe should have equal tested/current inventories and `changed_inputs: []`, but the consumer necessarily rejects it with `IndexError`. The historical `check_ci`-only difference can remain supported without requiring every future valid run to reproduce that difference. Spec: [issue 09:17](D:/Workspace/dairn/qaplet/.scratch/qaplet-m0/issues/09-offline-integrity.md:17), “在独立构建目录从锁定缓存重复构建foundation制品”, and [issue 10:17](D:/Workspace/dairn/qaplet/.scratch/qaplet-m0/issues/10-core-evidence-gate.md:17), “生成只包含实测状态的实施报告”.

Limits: read-only source/evidence review; no new builds, native fixtures, or service operations. The retained full manifest independently evaluated pass (175 cases, 618 evidence items). These findings concern rerunning/reusing the workflow, not a claim that its retained executions failed. No additional scope-creep finding.


Standards：2 项，最严重为本轴 P2；Spec：2 项，最严重为本轴 P2。两轴独立保留，不合并或重排。
