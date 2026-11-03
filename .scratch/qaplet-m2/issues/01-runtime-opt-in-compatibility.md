# 01: M2 Runtime 的可选创建与兼容生命周期

**What to build:** 纯 C Host 显式创建 M2 空实例，查询有效配置、分步推进并安全 close/quiesce/destroy，同时继续使用旧 M1 SDK 合同。必要的 owner 驱动保护和原生保留边界重构随这条闭环先落地。

**Blocked by:** None (can start immediately)。

**Status:** ready-for-agent

**Execution state:** implemented_pending_cross；2026-11-03 公开 SDK 与本地 Ubuntu/Windows 供应物合同已实现并实测，完整 Cross/Full 尚待集成分支取得，未标 accepted/resolved。

**Execution prerequisites:** 已满足本票直接 blocker；Ubuntu 构建环境及固定输入可用，复用仓库约定的输入、缓存和证据方式。ready-for-agent 表示需求已就绪，不绕过 blocker。

**Spec stories:** 1、2、3、4、5、6、9、74、75、78、86、87、89、93、105、108、109。

**Confirmed decisions:** Q2、Q4、Q25、Q26、Q34、Q36、Q37、Q45。

**Spec acceptance groups:** AC-01、AC-02、AC-21、AC-24。这些是本票涉及的父规格验收组；复杂组由多票分别完成子合同，再由完整矩阵重放，不把局部成功冒充整组完成。

## Acceptance criteria

- [ ] 旧头文件消费者与 M1 创建、值桥、模块、事件和关闭合同保持兼容；旧布局、错误数值和 reserved 含义不变。
- [ ] 独立 M2 创建配置、实例能力及快照可用；只声明本票实际实现的子集，未实现能力不以成功占位符提供。
- [ ] 完整调用范围的 owner/重入保护成立；空实例 run_once(0)、无进展 pump、CLOSED 查询和 destroy 前置条件可由公开接口验收。
- [ ] 空实例 close 与原生清理状态分开表达；后续原生保留所需边界先建立，不能在 CLOSED 后重新写回 FAILED。
- [ ] 有限默认配置、结构大小/版本/未知字段拒绝和逐阶段创建失败回滚可实测，失败不发布半成品。

## Blocking details

无实施票前置，可立即领取；仍须遵守已完成 M1 基线与开发约定。

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


2026-11-03：已领取票据。按父规格 T01/T02 已确认边界，以公开 SDK 纯 C Host 测试 M2 独立创建、配置、实例能力、空实例驱动、owner/重入与 CLOSED/native 状态；以冻结的 M1 公共头文件消费者验证旧布局/数值及值桥、模块、事件、关闭合同。首条新接口红灯日志：`out/m2-01/red-empty.log`。复杂资源取消、等待 backend 与跨平台总验收保持后续票据原范围。

2026-11-03：本地实现完成。Ubuntu 正常供应目录 105/105，Windows 同字节 104 pass + 1 已知可选 symlink blocked，包外普通消费者 8/8、独立受控系统故障消费者及 Ubuntu 完整 check 通过。完整命令、候选配方/SDK 身份、红绿、原始失败和剩余范围见 [实施记录](../01-runtime-validation.md)。等待集成分支 Cross/Full，验收复选框暂不勾选；后续原生资源必须沿已建立 retain/quiesce 边界接入，空实例成绩不证明已有资源回收或 OS backend 支持。
