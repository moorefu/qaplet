# 09: 完整资源配置、计量与永久分配失败

**What to build:** 宿主可查询和收紧全套有效限额；源码、模块、队列及桥接受约束，预检拒绝可继续使用，实际分配拒绝永久 FAILED。

**Blocked by:** [05：模块失败固定与路径拒绝合同](05-module-cache-path-policy.md)、[08：跨线程 POD 到 JS 的有界事件桥](08-bounded-pod-events.md)

**Status:** ready-for-agent

**Execution state:** not_started；尚未领取、实现或验收。

**Execution prerequisites:** Ubuntu 构建环境与固定输入可用，且已合入本票所列阻塞任务；使用仓库约定的共享输入/缓存与现有构建、证据入口。

**Spec stories:** 36、37、38、40、50。

## Acceptance criteria

- [ ] 落实引擎堆 64 MiB、栈 1 MiB、SDK 存储 16 MiB、单源码 4 MiB、源码缓存 32 MiB、模块 256 及全部队列/桥接默认值。
- [ ] 0 使用有限默认，无无限模式；容量初始化后固定，配置和所有大小/时间换算先检查溢出。
- [ ] 缓存命中不重复收费，内建/host/失败记录计模块，Script 源码不累计入模块缓存，宿主/OS 存储边界明确。
- [ ] 从创建首个分配起计量，实际引擎/SDK 分配拒绝即使被 JS 捕获仍 FAILED；预检超限只拒绝调用。
- [ ] 快照有当前/峰值、队列及事件计数和原因，自动 GC 不被当作硬额度；低额度真实负载验证所有资源失败与初始化清理。

## Validation and evidence

通过本票实际提供的公开 SDK/CLI 与纯 C Host 外部边界验证，观察公开状态、稳定错误 code、JS/native 副作用、复制数据、退出码和资源关闭事实。复用已有真实 producer、供应制品、宿主 consumer 与原始证据模式；每票执行自身验收及适用的 Cross/Full 门禁，平台收尾票不替代此前回归。

证据记录当前实现/输入/配方/SDK/APE 身份、实际命令、期望与实际结果、stdout/stderr 原始日志、退出码、时限、宿主与执行方式。外部 supervisor 超时表示用例未完成，不能作为产品正确停止/超时的证明；缺资源、失败、skip 与未完成保持可见，不继承旧 M0 成绩。

## Scope boundary

本票交付一个可单独演示和验收的增量，只声明已经实现并实际验证的范围；未实现能力不以成功 stub 公开。完整 M1 合同以实施规格为准；M2 的 OS waker、I/O 取消、迟到 completion 和完整调度，以及独立发布门禁，继续保持原分界。

## Context

[正式 M1 规格](../spec.md) · [已确认设计](../design.md) · [任务索引与依赖](../README.md) · [开发入口](../../../docs/development.md) · [证据合同](../../../docs/development/evidence.md) · [独立审查材料](../../../docs/agents/review.md)

## Comments

2026-11-02：用户以 `ok` 确认 16 票拆分及阻塞依赖，按 to-tickets 发布为 ready-for-agent。本票尚未执行；满足阻塞依赖后才可领取。

