# 12: 共用 Runtime 的最小 CLI

**What to build:** qaplet run/eval 使用与 C SDK 相同的 loader、结果和预算，提供 argv/cwd 快照、help/version 和正确退出码。

**Blocked by:** [02：复制结果、严格值类型与安全诊断](02-result-values-diagnostics.md)、[05：模块失败固定与路径拒绝合同](05-module-cache-path-policy.md)、[07：跨 pump 的执行期限与总调度量](07-execution-budgets.md)

**Status:** claimed

**Execution state:** implementing；2026-11-02 已领取，在 codex/m1-12 实施公开 SDK / CLI 垂直验收。

**Execution prerequisites:** Ubuntu 构建环境与固定输入可用，且已合入本票所列阻塞任务；使用仓库约定的共享输入/缓存与现有构建、证据入口。

**Spec stories:** 18、55、56。

## Acceptance criteria

- [ ] run 源码文件/应用参数、eval 显式 source 及 help/version 实际可用；eval 为 Script，run 为 ESM。
- [ ] qaplet:runtime 提供创建时只读 argv/cwd，SDK 与 CLI 同一 factory、身份与错误行为。
- [ ] run 不打印 namespace，eval 只打印支持类型，不隐式转换对象或执行诊断 getter。
- [ ] CLI 未完成且无可推进工作时立即 UNSETTLED_ENTRY，退出码成功/执行失败/参数错误为 0/1/2。
- [ ] 实际 CLI 进程和纯 C 消费者对照成功、缓存/模块错误、rejection、结果/预算行为，保留原始日志与新制品身份。

## Validation and evidence

通过本票实际提供的公开 SDK/CLI 与纯 C Host 外部边界验证，观察公开状态、稳定错误 code、JS/native 副作用、复制数据、退出码和资源关闭事实。复用已有真实 producer、供应制品、宿主 consumer 与原始证据模式；每票执行自身验收及适用的 Cross/Full 门禁，平台收尾票不替代此前回归。

证据记录当前实现/输入/配方/SDK/APE 身份、实际命令、期望与实际结果、stdout/stderr 原始日志、退出码、时限、宿主与执行方式。外部 supervisor 超时表示用例未完成，不能作为产品正确停止/超时的证明；缺资源、失败、skip 与未完成保持可见，不继承旧 M0 成绩。

## Scope boundary

本票交付一个可单独演示和验收的增量，只声明已经实现并实际验证的范围；未实现能力不以成功 stub 公开。完整 M1 合同以实施规格为准；M2 的 OS waker、I/O 取消、迟到 completion 和完整调度，以及独立发布门禁，继续保持原分界。

## Context

[正式 M1 规格](../spec.md) · [已确认设计](../design.md) · [任务索引与依赖](../README.md) · [开发入口](../../../docs/development.md) · [证据合同](../../../docs/development/evidence.md) · [独立审查材料](../../../docs/agents/review.md)

## Comments

2026-11-02：用户以 `ok` 确认 16 票拆分及阻塞依赖，按 to-tickets 发布为 ready-for-agent。本票尚未执行；满足阻塞依赖后才可领取。

