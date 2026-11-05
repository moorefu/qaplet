# 27: M2 严格证据入口与最终集成判定

**What to build:** 只读消费完整供应原件，独立核对当前 M2 功能矩阵、平台、次数和长测，以及 M1/M0 回归，给出可复验的通过或阻断结论。

**Blocked by:** [24 · 五原生目标的合同矩阵与生命周期](24-native-cross-contract-matrix.md)、[25 · 实际 ARM64 64K 的新合同与清理验收](25-arm64-64k-contract-matrix.md)、[26 · Linux/Windows Full 长测与资源稳定性](26-full-soak-lifecycle.md)。

**Status:** ready-for-agent

**Execution state:** not_started

**Execution prerequisites:** 等待24、25、26实际全范围验收；本地完成标记不能满足最终判定前置。工具准备不得提前签发M2完成或release-ready。验证范围及正式欠项见[本地开发方式](../local-development.md#2026-11-05剩余开发前置与验证范围)。

**Spec stories:** 1、74、75、76、99、100、101、102、103、104、105、106、110。

**Confirmed decisions:** Q1、Q7、Q14、Q25、Q48、Q49、Q53。

**Spec acceptance groups:** AC-27、AC-28。这些是本票涉及的父规格验收组；复杂组由多票分别完成子合同，再由完整矩阵重放，不把局部成功冒充整组完成。

## Acceptance criteria

- [ ] 显式选择并复核源码/配方/SDK/APE/loader/provider 原 ZIP/原始日志闭包，产品和评估器身份分开，禁止覆盖原件。
- [ ] 逐项核对 53 项决策、110 条故事和 28 组验收的实际责任与成绩，五原生/实际 64K/包外 C/Full 次数和时长均完整。
- [ ] 摘要、清单、来源/提交/运行绑定、重复/矛盾报告、旧输入与缺原始日志的负向变异均拒绝。
- [ ] 必需缺失/fail/skip/not_run/中断/外层 timeout 不聚合为通过；预检、probe、构建和产品运行分开表达。
- [ ] 保留现有 M1/M0 适用回归和独立发布门禁范围，24h/M3/M4/长期镜像不被冒称完成；最终独立审查按仓库先准备两轴材料。

## Blocking details

- [24 · 五原生目标的合同矩阵与生命周期](24-native-cross-contract-matrix.md)：最终结论需要五原生适用合同及循环原件。
- [25 · 实际 ARM64 64K 的新合同与清理验收](25-arm64-64k-contract-matrix.md)：最终结论需要实际 64K 新合同原件。
- [26 · Linux/Windows Full 长测与资源稳定性](26-full-soak-lifecycle.md)：最终结论需要 Linux/Windows 实际次数及长测原件。

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

