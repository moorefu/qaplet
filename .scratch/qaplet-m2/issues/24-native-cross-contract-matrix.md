# 24: 五原生目标的合同矩阵与生命周期

**What to build:** 五个原生目标消费同一供应产品，完整重放 M1/M2 适用合同、真实 backend 和各目标生命周期/混合异步用例。

**Blocked by:** [23 · 完整 SDK 分发与包外 C Host 示例](23-sdk-package-c-host-example.md)。

**Status:** claimed

**Execution state:** local_complete / cross_deferred

**Execution prerequisites:** 23已本地完成并独立合入`6ab8b73c`，接收提交`a5439c9d`；本票开发前置已满足。领取时SKU3已有Linux/Windows普通槽位成绩；最终当前SKU4的两端普通及专项均另有实际执行。正式验收仍要求五原生完整适用合同及每目标M2生命周期1000次、混合异步100次，缺失宿主保留not_run。验证范围及正式欠项见[本地开发方式](../local-development.md#2026-11-05剩余开发前置与验证范围)。

**Local validation:** 当前最终供应已转为共同祖先 `8a0badb3` 的 SKU4，Linux/Windows
18 个明确 mandatory 分组均已实际执行并严格重放；两端分别完成真正 M2
create/close/quiesce/destroy 1000 次及混合100次。公开正负、真实 backend、M1兼容、
信号16/9项及false/null原始责任分别保存。macOS两原生及Linux ARM64原生仍not_run，
不以guest、旧M1循环或本地两槽位签发五原生/正式Cross/Full/release-ready。
范围、失败历史和精确原件见[24本地验证](../24-native-matrix-validation.md)。

**Independent local reception:** 已由fresh merger独立no-ff合入`dd9e700b`，source
`e1d483f4`与合并tree相同；7092原件/外部闭包合前合后逐项核验、两次只读strict
重放及合后Ubuntu预检通过。Local development: complete，正式状态仍claimed /
local_complete / cross_deferred；三缺失native仍not_run，不放行27或父M2验收。
主收据为`out/qaplet-m2/merge-24-native-matrix/handoff.json`。

**Spec stories:** 1、99、100、101、102、103、104、105、106、107、108、109、110。

**Confirmed decisions:** Q14、Q24、Q25、Q26、Q31、Q34、Q36、Q37、Q41、Q42、Q43、Q45、Q46、Q48、Q49、Q50、Q51、Q52。

**Spec acceptance groups:** AC-01、AC-02、AC-03、AC-04、AC-05、AC-06、AC-07、AC-08、AC-09、AC-10、AC-11、AC-12、AC-13、AC-14、AC-15、AC-16、AC-17、AC-18、AC-19、AC-20、AC-21、AC-22、AC-23、AC-24、AC-25、AC-26、AC-27、AC-28。这些是本票涉及的父规格验收组；复杂组由多票分别完成子合同，再由完整矩阵重放，不把局部成功冒充整组完成。

## Acceptance criteria

- [ ] Linux x64/ARM64、Windows x64、macOS x64/ARM64 均有实测 OS/kernel/CPU/页大小/translation 及精确供应身份。
- [ ] 逐目标运行全部适用公开正负合同、backend 探针和 M1 兼容回归，明确 mandatory inventory，不以笼统 unsupported 跳过。
- [ ] 每目标实际至少 1,000 次 create/close/quiesce/destroy 和 100 次混合异步，覆盖完成/取消/失败及资源回基线。
- [ ] 线程/重入、借用 stdio、Console/TTY、信号、继承与迟到完成等平台差异保留原始值和失败历史。
- [ ] producer/consumer/实际执行日志及计数准确绑定；缺失、fail、skip、not_run 和外层 timeout 非通过，不签发 release-ready。

## Blocking details

- [23 · 完整 SDK 分发与包外 C Host 示例](23-sdk-package-c-host-example.md)：五原生目标消费完整供应 SDK/CLI/C Host。

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

2026-11-06：23独立接收后领取本票，使用fresh实现者、独立分支/worktree推进当前产品
的mandatory inventory和Linux/Windows本地矩阵。优先复用同SKU3已封存原件，补足当前
特殊合同及真正M2 create/close/quiesce/destroy和混合循环；M1的1000循环不改贴为M2。
与25/26只协调公共验证fixture，不增加依赖边。正式其余原生、CI及全范围验收仍待实测。

2026-11-06：最终clean source `e1d483f4d53fc32dff81fc31c0f95b20762031b4`已独立
本地接收为no-ff `dd9e700b2baa740bdc9a5b537773413bf90e4aeb`；当前18组两端通过，
其余三原生保留not_run。原父规格、Blocked by及五条AC不改，仍claimed/cross_deferred。

2026-11-06：本地可完成范围已收口为local_complete / cross_deferred，仍claimed、上述
正式AC不勾选。公共C9生命周期/持久Runtime工具以独立slice供25/26复用；24不接管
实际64K、10000次/一小时及最终门禁的原有责任。产品core/API/default helper4未改，
一次SDK4共同修正只涉及既有mixed/TTY正向fixture的受控交接。

