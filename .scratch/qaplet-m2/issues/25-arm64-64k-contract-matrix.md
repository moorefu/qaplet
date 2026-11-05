# 25: 实际 ARM64 64K 的新合同与清理验收

**What to build:** 当前 M2 产品在真实 65536 页 Linux ARM64 内核中重放公开合同、backend 与循环；原生优先，以固定完整系统 guest 补缺。

**Blocked by:** [23 · 完整 SDK 分发与包外 C Host 示例](23-sdk-package-c-host-example.md)。

**Status:** claimed

**Execution state:** local_complete

**Local development:** complete

**Execution prerequisites:** 23已本地完成并独立合入`6ab8b73c`，接收提交`a5439c9d`；本票开发前置已满足，已领取。当前SKU3的guest export不算执行，正式验收仍要求真实ARM64/65536完整适用合同及M2生命周期1000次、混合异步100次，旧产品170项成绩不能替代。验证范围及正式欠项见[本地开发方式](../local-development.md#2026-11-05剩余开发前置与验证范围)。

**Spec stories:** 99、100、101、102、103、104、105、107、109、110。

**Confirmed decisions:** Q14、Q24、Q26、Q31、Q34、Q36、Q37、Q41、Q45、Q48、Q49、Q50、Q51、Q52。

**Spec acceptance groups:** AC-01、AC-02、AC-03、AC-04、AC-05、AC-06、AC-07、AC-08、AC-09、AC-10、AC-11、AC-12、AC-13、AC-14、AC-15、AC-16、AC-17、AC-18、AC-19、AC-20、AC-21、AC-22、AC-23、AC-24、AC-25、AC-26、AC-27、AC-28。这些是本票涉及的父规格验收组；复杂组由多票分别完成子合同，再由完整矩阵重放，不把局部成功冒充整组完成。

## Acceptance criteria

- [x] guest 或原生内实测 ARM64/65536，固定并记录 QEMU/TCG/machine/CPU/kernel/config/rootfs 与传输前后产品身份。
- [x] 消费当前供应 CLI、C Host 与目标 SDK slice，执行全部适用 M1/M2 正负合同和相关深层 ABI，不复用旧输入成绩。
- [x] 实际至少 1,000 次生命周期和 100 次混合异步，覆盖等待/取消/迟到完成、终端和 child/stdio 释放。
- [x] 原生与 emulated-system 分开；qemu-user 不替代内核，TCG 不被外推为物理性能。
- [x] 必需缺项、失败、skip 或超时不通过；原件、逐项日志、计数和清理事实可供独立消费。

## Blocking details

- [23 · 完整 SDK 分发与包外 C Host 示例](23-sdk-package-c-host-example.md)：64K 验收消费当前完整制品及目标 SDK。

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

2026-11-06：23独立接收后领取本票，fresh实现者在独立分支/worktree复用固定完整系统
guest与主物理.cache，消费当前SKU3及明确供应的公开C验证fixture。保留真实内核页大小、
传输前后字节、journal/seal、实际次数和清理事实；不以export、qemu-user或旧SDK替代。
与24/26协调fixture接口和固定供件，不添加阻塞边；未实际执行部分保持可见。

2026-11-06：当前统一SKU4真实ARM64/64K本票适用矩阵完成，319条完整guest命令、
394条语义记录、1000生命周期/100混合实际循环（warmup另计）；原始seal/transport、
native清理与false/null边界及旧SKU3失败均保留。[本地验证与交接](../25-actual64k-validation.md)
给出精确映射、原件指针与工具迭代限制。状态claimed/local_complete/cross_deferred，
待独立接收；不占native ARM槽位、不签发父M2/正式Cross/小时soak/最终Review/27或发布。

