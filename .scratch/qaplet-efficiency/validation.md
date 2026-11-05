# v1.5 效率修改的本地验证

2026-11-05，按用户任务书及“先本地 Linux/Windows”指示实施。CI 工作区 `codex/ci-efficiency` 初始基于 `3d03f855`，独立于 M2 14/15。远程未 push/执行；不修改 main、标签、账单、额度或保护设置。

## 实际变更

- `ci_tiers.py` 以实际 diff 分层/工作流闭包，移除 M2 分支名强制 Full；未知风险升级，公开 ABI 五 native，专用 Win fixture 定向 Windows。
- 原五个 workflow/check 身份保留，统一 scope action 选择重作业；Fast 独立 SDK producer，Full offline 独立 job/路径命名空间与 Core 原件选择；并发保护 Full/RC/手工长证据。
- `dev.py check-fast`、GNUmakefile 快捷目标、`local_check.py` 功能选择；供应 identity/raw bytes/current recipe 严格匹配，无自动下载/guest。原 Makefile/evidence.py/foundation.py 的 pinned v3 字节不变。
- `ci_evidence.py efficiency`/`efficiency-actions`/`reuse`；开发原结果复用不新签 OS/CI/Full，指标未知保留 null。
- 工程现状审计、执行优化、真实 npm 兼容路线及 INDEX/开发文档更新。

## 已执行

| 范围 | 真实结果 |
|---|---|
| Ubuntu `dev.py check` | 399 tests，199 pass / 200 skip；真实系统 C bootstrap pass，input-lock/CI/preflight pass。skip 是依赖显式历史证据/其他宿主的既有范围，不称执行通过 |
| 最终增量 CI policy/timing/reuse/target-plan 合同 | 23 项，21 pass / 2 显式资源 skip；Windows fixture 缺真实报告保持 blocked，Full plan 不可窄化 |
| local_check 专用合同 | 10/10；旧/坏/partial supplier 拒绝，预检无执行，合同预期变化改变digest，TTY产品未实现明确blocked |
| workflow 工具合同 | 27 项，25 pass / 2 skip；SCM 5项中4 pass/1 skip；Job 9项中7 pass/2 skip |
| 当前 SDK supplier | Ubuntu公开 `sdk.py build` 78 commands，209.761s pass，原 report ID `2d50cd2d-ab23-4413-ad89-06dc2fcac7d7`；显式供应原件保持 |
| 最终 all profile | 118/118，包括1000生命周期；20.659s（Runtime13.209s，普通C编译0.312s） |
| 最终 engine profile | 40/40，包括1000生命周期；14.201s（Runtime7.030s，普通C编译0.309s） |
| 成功执行复用 | all、engine 原结果重核pass；0新产品执行，原report/producer/CI身份保留 |
| 代表性变更计划 | docs/values/header/Win fixture/Process/SCM/shared build/unknown 八组前后同输入；仅调度比较，不是远程性能结果 |
| npm调查 | 发布 metadata + 3个tarball按registry SRI通过，只读分析，无install或应用运行 |

原件位于 CI 工作区 `out/efficiency/`、`out/local-check-validation/`、`out/local-check-*-supplied-003/`；改造前实际 API/步骤基准位于主 `out/qaplet-efficiency/audit-baseline/`。最终合同/命令的后续增量结果应附在本记录，不回写旧范围。

## 保留的真实失败与限制

第一次最小 C 实现误走完整包外链接/helper链，all187.640s，其中前后工具链核验63.814/63.424s；没有把它当优化成绩。改为 Ubuntu普通公开头调用者 `cc -c`，记录实际source/header/driver/cc1/as前后内容与ELF对象，Runtime产品供应/发行合同不改。

第一次cold构建途中原Makefile改动被v3锁拒绝，97.131s失败（SDK子过程96.534s）。已恢复原Makefile，新GNUmakefile包装；失败cache原路径/字节保留，不自动覆写成green。后续使用fresh显式supplier成功，不能称失败cache已通过。

无actionlint/Actions服务端执行或改后远端时长；旧Fast样本与本次WSL热检查不同宿主/范围，不计算提速百分比。cold供件不满足L0 90秒目标，和热检查分列。未做新macOS/ARM64/64K/Full/Release，不标M2完成。

本轮并行完成的既有14已独立合入根 `e11a032448a4e86d64cf37dddffa01b41e9f6a5f`（实际no-ff merge `25d3eb12`）；它的Linux179/Windows178+optional1、包外65×2及故障12/10属于14新组合供应，见主 `out/qaplet-m2/merge-14-fusion-js12/HANDOFF.md`，不能与CI工作区旧12产品的118项相加成新全树成绩。15继续自己的系统分支修复。
