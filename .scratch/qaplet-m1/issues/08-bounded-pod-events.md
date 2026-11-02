# 08: 跨线程 POD 到 JS 的有界事件桥

**What to build:** 工作线程投递复制事件，owner pump 经 qaplet:events 接收；演示 FIFO、数据隔离、未注册、超大和满队列行为。

**Blocked by:** [06：Qaplet Host Module 的同步调用闭环](06-host-module-bridge.md)、[07：跨 pump 的执行期限与总调度量](07-execution-budgets.md)

**Status:** claimed

**Execution state:** in_progress；由 implement_08 在 codex/m1-08 领取，公开 SDK / supplied C Host 边界实施及验收。

**Execution prerequisites:** Ubuntu 构建环境与固定输入可用，且已合入本票所列阻塞任务；使用仓库约定的共享输入/缓存与现有构建、证据入口。

**Spec stories:** 27、28、29、30、31、32、33、34。

## Acceptance criteria

- [ ] 固定槽默认 256、载荷 1 KiB，type 为 uint32/Number、token 为 uint64/BigInt 且 0 合法，不保存指针或 JS 引用。
- [ ] onEvent 一次注册，未注册 NO_HANDLER；注册窗口独立于已锁定的 C 宿主模块声明。
- [ ] 成功完整复制；超大/QUEUE_FULL 完全拒绝，不等待、不覆盖、不重试，多生产者按接受顺序 FIFO。
- [ ] JS 收到独立 Uint8Array；修改宿主原缓冲区不影响数据，事件返回 Promise 不等待且 rejection 遵循检查点。
- [ ] 有入口时合用账本，空闲事件及其 jobs 使用跨 pump 独立账本；默认每 pump 最多 32 事件，jobs 未结算不领取下一事件。

## Validation and evidence

通过本票实际提供的公开 SDK/CLI 与纯 C Host 外部边界验证，观察公开状态、稳定错误 code、JS/native 副作用、复制数据、退出码和资源关闭事实。复用已有真实 producer、供应制品、宿主 consumer 与原始证据模式；每票执行自身验收及适用的 Cross/Full 门禁，平台收尾票不替代此前回归。

证据记录当前实现/输入/配方/SDK/APE 身份、实际命令、期望与实际结果、stdout/stderr 原始日志、退出码、时限、宿主与执行方式。外部 supervisor 超时表示用例未完成，不能作为产品正确停止/超时的证明；缺资源、失败、skip 与未完成保持可见，不继承旧 M0 成绩。

## Scope boundary

本票交付一个可单独演示和验收的增量，只声明已经实现并实际验证的范围；未实现能力不以成功 stub 公开。完整 M1 合同以实施规格为准；M2 的 OS waker、I/O 取消、迟到 completion 和完整调度，以及独立发布门禁，继续保持原分界。

## Context

[正式 M1 规格](../spec.md) · [已确认设计](../design.md) · [任务索引与依赖](../README.md) · [开发入口](../../../docs/development.md) · [证据合同](../../../docs/development/evidence.md) · [独立审查材料](../../../docs/agents/review.md)

## Comments

2026-11-02：implement_08 已确认工作树包含集成基线 33ca5439，读取 implement-spec/TDD、规格 Q23–Q27、ADR 0005–0007 及开发合同；按已确认 Q32 seam 执行公开 SDK / supplied C Host red→green。

2026-11-02：用户以 `ok` 确认 16 票拆分及阻塞依赖，按 to-tickets 发布为 ready-for-agent。本票尚未执行；满足阻塞依赖后才可领取。

