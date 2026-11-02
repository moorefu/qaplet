# 10: 饱和与运行中的协作式停止

**What to build:** 工作线程在队列满、JS 循环或入口 PENDING 时仍可 request_stop；验证拒收、已领取/未领取边界、幂等与首因。

**Blocked by:** [08：跨线程 POD 到 JS 的有界事件桥](08-bounded-pod-events.md)

**Status:** ready-for-agent

**Execution state:** not_started；尚未领取、实现或验收。

**Execution prerequisites:** Ubuntu 构建环境与固定输入可用，且已合入本票所列阻塞任务；使用仓库约定的共享输入/缓存与现有构建、证据入口。

**Spec stories:** 41、42、43、44、45。

## Acceptance criteria

- [ ] request_stop 不消耗队列槽，成功代表接受，进入 STOP_REQUESTED 后拒收普通工作。
- [ ] 统一接受顺序决定 stop/入队/领取；已领取事件协作退出，未领取事件停止执行，不自动重试。
- [ ] 同步 CPU 循环、Promise/async 流程、无就绪工作的 PENDING 和回调阶段停止均由 owner 明确确认。
- [ ] 重复 stop 在停止/FAILED/CLOSED 状态无副作用成功；期限与 stop 并发保留首次记录，不依赖异常文字。
- [ ] 剩余事件/jobs 在关闭时释放，不额外执行用户 finally/清理 JS；每个竞态由可控交接和真实结果证明。

## Validation and evidence

通过本票实际提供的公开 SDK/CLI 与纯 C Host 外部边界验证，观察公开状态、稳定错误 code、JS/native 副作用、复制数据、退出码和资源关闭事实。复用已有真实 producer、供应制品、宿主 consumer 与原始证据模式；每票执行自身验收及适用的 Cross/Full 门禁，平台收尾票不替代此前回归。

证据记录当前实现/输入/配方/SDK/APE 身份、实际命令、期望与实际结果、stdout/stderr 原始日志、退出码、时限、宿主与执行方式。外部 supervisor 超时表示用例未完成，不能作为产品正确停止/超时的证明；缺资源、失败、skip 与未完成保持可见，不继承旧 M0 成绩。

## Scope boundary

本票交付一个可单独演示和验收的增量，只声明已经实现并实际验证的范围；未实现能力不以成功 stub 公开。完整 M1 合同以实施规格为准；M2 的 OS waker、I/O 取消、迟到 completion 和完整调度，以及独立发布门禁，继续保持原分界。

## Context

[正式 M1 规格](../spec.md) · [已确认设计](../design.md) · [任务索引与依赖](../README.md) · [开发入口](../../../docs/development.md) · [证据合同](../../../docs/development/evidence.md) · [独立审查材料](../../../docs/agents/review.md)

## Comments

2026-11-02：用户以 `ok` 确认 16 票拆分及阻塞依赖，按 to-tickets 发布为 ready-for-agent。本票尚未执行；满足阻塞依赖后才可领取。

