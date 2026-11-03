# 04: 子进程参数、stdio 与继承隔离探针

**What to build:** 真实启动直接子进程，观察 argv/env/cwd、三路 stdio、退出/reap 和内部句柄隔离，收口 ProcessHost 的 OS 行为。

**Blocked by:** None (can start immediately)。

**Status:** ready-for-agent

**Execution state:** awaiting_cross；独立实现和本地 Linux/Windows/实际 ARM64 64K 已完成，macOS 两目标及 Linux ARM64 native 待统一 Cross，尚未 resolved。

**Execution prerequisites:** 已满足本票直接 blocker；Ubuntu 构建环境及固定输入可用，复用仓库约定的输入、缓存和证据方式。ready-for-agent 表示需求已就绪，不绕过 blocker。

**Spec stories:** 51、52、53、54、55、56、57、64、65、100、103。

**Confirmed decisions:** Q12、Q14、Q22、Q29、Q30、Q41、Q49。

**Spec acceptance groups:** AC-15、AC-16、AC-18、AC-26。这些是本票涉及的父规格验收组；复杂组由多票分别完成子合同，再由完整矩阵重放，不把局部成功冒充整组完成。

## Acceptance criteria

- [x] 实测显式可执行路径、Unicode/空格/引号/反斜杠/元字符 argv、cwd、完整环境替换和缺程序错误，不隐式调用 shell。
- [x] pipe/inherit/ignore 均有真实子进程结果，退出与 EOF 两种顺序可区分。
- [x] 加入额外 waker、文件和描述符 fixture，证明仅授权 stdio 可继承，内部传递元信息不造成句柄泄漏或 EOF 悬挂。
- [x] 真实强制终止并 reap 直接子进程，保留 native exit/signal/error，覆盖创建失败和重复回收。
- [ ] Ubuntu producer、各适用宿主与实际 64K 的探针结果保持身份和原始日志，不宣称整树或优雅终止能力。

## Blocking details

无实施票前置，可立即领取；仍须遵守已完成 M1 基线与开发约定。

## Validation and evidence

本票的纵向边界是 Ubuntu producer、供应原生探针、真实 OS 操作与结构化 consumer 报告。先实测再选择 backend，保留成功、失败、能力限制和实际资源清理；探针结果不自动成为产品支持声明。

使用明确接受结果和可控 producer/owner 交接复现竞态，不用随机 sleep 猜顺序。受控系统故障夹具与正常产品分别记录输入及配方；外部 supervisor 超时表示未完成，不能证明产品已成功停止或清理。

每票执行自身正负验收与适用 Cross/Full；24–27 的最终矩阵和长测不替代此前平台检查。全部产品与 SDK 构建在 Ubuntu，其他宿主消费供应制品；同一 APE 字节跨宿主验证，目标 SDK archive/slice 分别标识。

证据绑定当前产品源码、配方、SDK/APE/loader/供应原 ZIP、实际命令与日志、OS/kernel/CPU/页大小/执行方式、实际次数/时长及清理结果。缺项、fail、必需 skip/not_run 或中断保持可见，不能外推旧 M0/M1 成绩。

## Scope boundary

本票交付一个可独立演示和验收的增量，只声明已经完成并实际验证的能力；不提供成功 stub、隐式 fallback 或未经验证的 OS ABI。

资源从首次接入起就受父规格有限默认值、容量预留和安全 stop/close/quiesce/destroy 约束。19/20 负责跨资源联立与组合故障验收，不允许此前延后硬限额或首次关闭安全。完整默认表、线程/重入、结果保留和原生 lifetime 以父规格为准。

保留 M1 旧创建与 ABI 合同；完整 Node 兼容、网络产品 API、整树清理、24 小时发布 soak 和独立发布镜像门禁继续保持原阶段边界。当前任务发布不等于实现或通过。

## Context

[父规格与全部合同](../spec.md) · [任务索引与依赖](../README.md) · [已确认拆分及覆盖映射](../ticket-proposal.md) · [开发入口](../../../docs/development.md) · [SDK 入口](../../../docs/development/sdk.md) · [证据合同](../../../docs/development/evidence.md) · [独立审查材料](../../../docs/agents/review.md)

## Comments

2026-11-03：用户明确回复“按 27 张发布”，确认 27 张纵向任务及 38 条直接阻塞边；本票按已确认提案独立发布为 ready-for-agent，尚未领取或实施。父规格保持不变。


2026-11-03：领取独立 worktree `codex/m2-04-process`，确认集成祖先并合入 `8fd870e`。按已确认 T01/T02 公开 CLI/原生探针 seam 执行真实 red/green；产物与旧 M0 配方隔离。

2026-11-03：独立 `m2-process.com`、无 CRT Windows PE child 与公开 CLI 合同完成。Ubuntu producer 和同摘要 Linux 13 项、Windows 18 项、实际 ARM64 64K 13 项均通过；Windows CLI 9/9、Ubuntu 8 过及唯一 Windows 专属项不适用。保留 posix_spawn 的真实环境差分、trace duplicate 导致的 EOF 挂起、HANDLE 数值复用误判和修正后的正向继承控制。上述勾选仅覆盖已测本地三目标，不代表完整 Cross；原始报告、失败历史、准确 CLI、SHA 和边界见[中文证据](../04-process-validation.md)。合入 `0460f71`，原件 out 保留，未 push。
