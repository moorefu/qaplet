# M0 全量双轴审查（2026-11-01）

**Status:** resolved
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

## 单一 fresh implementer 修复

修复分支 `codex/m0-final-review-fixes` / `out/implement-spec/final-review-fixes` 从 integration `722b9cf9a4a6f6d59391e4f1ade6f529785ee2c4` 开始；提交前重新合入 integration，结果 already up to date。沿用用户已确认的 public CLI、实际 OS 与结构化 evidence/module seams，没有再委派实施者。

- Standards P2/P3：五个 producer 复用 `build_ape.publish_ape`，发布三个二进制、恰好每 CPU 一份 map、source-built ARM64 loader 和 `ape-helper-notices/{LICENSE,source.lock.json,HACL-LICENSE.txt}`。不改变各自 C flags、独立链接/符号/ABI 审计或应用边界。模块 fixture 验证实际字节及缺失/重复 map 拒绝；Ubuntu 实际五种 producer 输出全部通过额外 publication consumer。原本独立 SCM/Job 交付 SHA 分别仍为 `f0c4c88e253cab7266e4de4bffdbe8a3aa767b65b59a2aba93e07377e949d0f3`、`0410864e8232379a7559410978f804da534b1550107f9a39ccdc050ef1bfaffb`。
- Spec P2（POSIX 权限）：只将本次新建的 evidence UUID 目录交给目标 UID/GID，并设 0750；不递归 chown 仓库、缓存或已有目录。真实 Ubuntu `/tmp` fixture 先红：root:root / 0755 的 UUID 目录使 worker 创建嵌套 evidence 时 `PermissionError`、退出 2；修复后目标 UUID 为 1000:1000 / 0750，祖先仍 root:root / 0755，worker 实测 UID/GID 1000、独立 namespace、仅 lo、literal 外连 ENETUNREACH=101，退出 0。完整目录 modes、父/子 namespace 与原始 stderr 都保留。
- Spec P2（零差异 receipt）：按两份 inventory 的相对输入闭包精确计算差异，重新测量当前仓库每个输入；真实相同配方允许 `changed_inputs: []`。假空差异、当前字节错误、输入闭包差异仍拒绝。历史仅 `build_ape.py/check_ci` 的差异仍须完整 AST 证明；函数主体变化不获得等价声明。新 publication 代码属于新配方，原 09 proof 没有被改写。
- Merger CLI observation：直接 `--guest-report` 相对路径改为调用者 cwd；manifest 内字段仍相对 manifest 父目录。真实已执行的 4K guest consumer 在另一个 cwd 下先红（错误查找 manifest 目录的 guest.json），后绿；仅 guest scope，Core 仍为 not_run，没有新增 guest 执行或服务生命周期声明。

四项原轴 finding 与额外 CLI observation 的 red/green receipts、五种 producer 输出、普通 Linux consumers 和检查日志保存在 `out/m0-integration/final-review-fixes/`。原始两轴内容及旧 09/10 proof 均保留。许可边界、libc source unproven、network TLS unknown/unverified 和 SVC 非 Core 阻断项不变。

## 新配方实际断网 proof 与全量 Core

2026-11-01 13:38:19–14:06:50（本机 UTC+08:00），固定上述功能源码后完成一对全新的 Ubuntu disconnected builds。外层 UUID `a0e55b0a-c162-4100-b02e-22c46bb0606a`，实际 worker 为 1000:1000，parent net `4026531840` / child `4026532227`，仅 lo、无 IPv4 route、ENETUNREACH=101。两个 build 各 192 次实际 execve，14 个拒绝各 1 次，全部 trace 都为 0 Internet attempts。16/16 cases pass；六个交付二进制和独立 loader/packer 均逐字节相同；21,180 个 cache files / 2,815,491,333 字节 before/after 精确相同。物理可复用缓存仍为主仓库 `.cache/`。

当前 tested/current recipe inventory 都为 `0c661f4bf650da1a0371ad3c06abcad916e018195e77d71a935b9009b005a19b`，`changed_inputs: []` 来自真实相同输入，不套用旧 proof。逐文件 recipe snapshot 与空的实际 diff 都保留。旧 consumer `722b9cf` 消费这份实际新数据时重现 `IndexError: list index out of range`；修复后的 Windows / Ubuntu full Core 均 pass，175 probes / 617 evidence items（零差异无需读取历史 builder snapshot，因此比旧 gate 少一项）。全部 14 个 canonical CI1129 ZIP、原始字节与语义仍由 gate 核验，SVC 独立保留。

新 Hello / eval 仍分别为 `467f8889db7a671cd238a7cc56df933951fea686c7c524480f4112870da3695d` / `426a7b05367fc6fd8d4831eebb2439782b3b66141af115094cc1efa766b05cd4`。四个 standalone producer 的二进制也保留原本 local SHA；I/O/deep 与 CI 的既有 ZIP UTF-8 flag 差异没有被隐藏或改成全局身份相等。没有重跑 Windows 服务生命周期；只消费既有独立 SVC 指针。

| 稳定输出（仓库根目录相对路径） | SHA-256 |
| --- | --- |
| `out/m0-integration/final-review-fixes/offline-current/offline-integrity.json` | `08d70de72ce27081db7e4c748878ded2f1e0617a2b79ddde77155b4d4412be30` |
| `out/m0-integration/final-review-fixes/offline-current/recipe-current-vs-tested.json` | `835214a2d6c5345abc6af0d784100b11151b7c713f14f966719e47a4aec34c4a` |
| `out/m0-integration/core-gate/inputs-linux.json` | `7684479f2e83e84d9484e48d7e3f96fc48d35867ebadec02029bfc293a330457` |
| `out/m0-integration/core-gate/inputs.json` | `3911d7752d3dc7fba62c37626e1baeaf74c89daed46e0f67f572c143003bca37` |
| `out/m0-integration/final-review-fixes/core-gate-linux.json` | `97140f8e01c1f00569fe26a71d8eff8167bb296f5e7331d33640d72536fe6db3` |
| `out/m0-integration/final-review-fixes/core-gate-windows.json` | `82c326e0b82370f2243a73765e4382a97b913af114a0ff7b328d7b891257dc85` |

canonical manifests 已转为选择新 proof；替换前两份原文件逐字节保存在 `out/m0-integration/core-gate/retained-before-final-fixes/`，所有旧完整 gate reports 和 09 proof 均未覆盖。稳定输出另有同字节的两份新 manifests。

## 最终检查与结论

四项原轴 finding 和额外 CLI observation 均已修复。Ubuntu 最终 `make check` 显式选中完整实际 Core、4K guest、五 producer publications 及 supplied I/O/Job artifacts：149 tests / 0 failures / 19 explicit skips（860.694 秒），静态 CI 检查与真实 C bootstrap 均 pass；未选实际数据的先前普通检查为 148 / 0 / 48 skips，原日志保留。Windows public consumer 最终 27/27 pass（118.386 秒）；实际 4K 相对 CLI 和拒绝语义均包含在内。另有真实 POSIX root-to-nonroot preflight 5/5、actual publication 3/3 与 recipe comparison module 2/2 的独立 red/green 记录。

Windows 初次新 manifest 使用 forward slashes，full gate 通过，但既有 mutation fixture 的 native-path 字符串替换产生 30 个 `StopIteration` errors；该日志及原 manifest 单独保留，转换为与旧 canonical 一致的 native Windows 路径后 27/27 通过。此项是证据选择文件格式修正，没有更改 build recipe 或改写 offline proof。

22 个 staged recipe inputs 的 SHA/长度逐一与实际 pair 相等，避免 Git 换行规范化在合并时改变已测配方；19 份 standalone publication source/recipe 原始字节也另存 snapshot。最终日志包括 `make-check-final.log`、`core-consumers-windows-final.log`、`publication-actual.log`、`posix-{red,green}/process.json`、`zero-diff-original-consumer-red.json` 和 `index-recipe-verification.json`，均位于上述稳定输出。未执行新的 Windows SCM/Job 生命周期或宣称更广的 handle/tree 安全性；原独立 SVC 结果与未知 libc/TLS 边界继续保留。
