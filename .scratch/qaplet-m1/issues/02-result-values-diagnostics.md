# 02: 复制结果、严格值类型与安全诊断

**What to build:** eval/get_result 交付完整标量、精确 64 位整数、UTF-8 和字节结果；缓冲区不足可重读，错误诊断不执行用户代码。

**Blocked by:** [01：纯 C Host 的最小 Runtime 闭环](01-runtime-c-host.md)

**Status:** ready-for-agent

**Execution state:** not_started；尚未领取、实现或验收。

**Execution prerequisites:** Ubuntu 构建环境与固定输入可用，且已合入本票所列阻塞任务；使用仓库约定的共享输入/缓存与现有构建、证据入口。

**Spec stories:** 9、23、24、51、53、54。

## Acceptance criteria

- [ ] 完整值类型与 unsupported 对象/函数/thenable 的拒绝可通过公开 C 消费者观察，整数不经过 Number 丢精度。
- [ ] 严格 UTF-8、字符串 NUL、孤立 surrogate 和 Uint8Array 边界正确；返回数据不借用失效 JS/宿主缓冲区。
- [ ] BUFFER_TOO_SMALL 提供所需长度并保留结果；未接受调用不覆盖旧执行结果。
- [ ] 调用方诊断相互独立，截断/NUL/零容量不丢错误 code，不调用 getter/toString/Proxy trap。
- [ ] 每个成功和失败分支通过真实引擎及纯 C 外部结果验证。

## Validation and evidence

通过本票实际提供的公开 SDK/CLI 与纯 C Host 外部边界验证，观察公开状态、稳定错误 code、JS/native 副作用、复制数据、退出码和资源关闭事实。复用已有真实 producer、供应制品、宿主 consumer 与原始证据模式；每票执行自身验收及适用的 Cross/Full 门禁，平台收尾票不替代此前回归。

证据记录当前实现/输入/配方/SDK/APE 身份、实际命令、期望与实际结果、stdout/stderr 原始日志、退出码、时限、宿主与执行方式。外部 supervisor 超时表示用例未完成，不能作为产品正确停止/超时的证明；缺资源、失败、skip 与未完成保持可见，不继承旧 M0 成绩。

## Scope boundary

本票交付一个可单独演示和验收的增量，只声明已经实现并实际验证的范围；未实现能力不以成功 stub 公开。完整 M1 合同以实施规格为准；M2 的 OS waker、I/O 取消、迟到 completion 和完整调度，以及独立发布门禁，继续保持原分界。

## Context

[正式 M1 规格](../spec.md) · [已确认设计](../design.md) · [任务索引与依赖](../README.md) · [开发入口](../../../docs/development.md) · [证据合同](../../../docs/development/evidence.md) · [独立审查材料](../../../docs/agents/review.md)

## Comments

2026-11-02：用户以 `ok` 确认 16 票拆分及阻塞依赖，按 to-tickets 发布为 ready-for-agent。本票尚未执行；满足阻塞依赖后才可领取。

