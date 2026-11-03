# 22: CLI 中断交接与清理超时报告

**What to build:** 受支持 SIGINT/SIGTERM、Windows Ctrl+C 唤醒并协作停止 CLI，恢复终端、启动 child 回收，并如实报告 5 秒原生清理等待耗尽。

**Blocked by:** [21 · CLI 异步运行与入口结果保存](21-cli-async-run-results.md)。

**Status:** ready-for-agent

**Execution state:** not_started

**Execution prerequisites:** 已满足本票直接 blocker；Ubuntu 构建环境及固定输入可用，复用仓库约定的输入、缓存和证据方式。ready-for-agent 表示需求已就绪，不绕过 blocker。

**Spec stories:** 64、66、69、70、84、85、86、87、88、93、96、97、98、100。

**Confirmed decisions:** Q13、Q23、Q26、Q27、Q30、Q34、Q41、Q42、Q43、Q45、Q50、Q51。

**Spec acceptance groups:** AC-07、AC-18、AC-19、AC-20、AC-23、AC-24、AC-25、AC-26。这些是本票涉及的父规格验收组；复杂组由多票分别完成子合同，再由完整矩阵重放，不把局部成功冒充整组完成。

## Acceptance criteria

- [ ] 真实 OS 中断经合适 signal-safe 入口转交 owner；不能在 Unix signal handler 直接调用普通加锁 SDK stop。
- [ ] SDK 自身不安装进程级 handler；CLI 在等待、执行、队列饱和和混合资源场景均能接受受支持中断。
- [ ] owner 终止后不再用户 JS，终端模式按租约恢复，直接子进程启动强制终止和真实回收。
- [ ] 5 秒是累计主动原生等待预算，耗尽报告未完成并退出 1，不假装 destroy、不提前释放仍在使用数据。
- [ ] 保留各 OS 实际中断、退出和清理日志；不可捕获强杀/外层 supervisor timeout 不成为成功恢复或硬耗时保证。

## Blocking details

- [21 · CLI 异步运行与入口结果保存](21-cli-async-run-results.md)：中断交接需要实际 M2 CLI 驱动与退出路径。

## Validation and evidence

本票通过实际提供的公开 SDK、JS/最小 CLI 或包外纯 C Host 边界验收，观察公开状态、稳定错误 code、复制数据、JS/native 副作用、输出/退出和资源回收事实。不得以私有布局、内部调用次数或完整异常字符串代替合同。

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

