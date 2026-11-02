# 07: 跨 pump 的执行期限与总调度量

**What to build:** 循环脚本或无限 jobs 在公开的有限预算内永久终止；交还宿主等待或反复 pump 均不能重置账本。

**Blocked by:** [03：Promise 与 jobs 的显式分步执行](03-promise-job-pump.md)

**Status:** resolved

**Execution state:** completed；2026-11-02 在 `codex/m1-07` 实现并验收公开执行预算；Ubuntu producer、Ubuntu/Windows/ARM64 64K 的全部 16 项 C Host consumer 通过，见[验收记录](../07-budgets-validation.md)。

**Execution prerequisites:** Ubuntu 构建环境与固定输入可用，且已合入本票所列阻塞任务；使用仓库约定的共享输入/缓存与现有构建、证据入口。

**Spec stories:** 35、38、39。

## Acceptance criteria

- [x] 默认 30 秒单调时钟期限和 100,000 jobs/事件总调度量实际执行，单次选项只收紧实例上限。
- [x] Script 循环、跨 pump jobs 链及交还宿主等待分别验证期限/计数累积和明确终止 code。
- [x] 中断点与 owner 边界使用独立终止记录，不依赖引擎一次异常返回或异常文字推断停止。
- [x] 预算耗尽后生命周期 FAILED，不允许再次运行；查询、close 和后续控制操作遵循已有状态合同。
- [x] 公开小额度负载在真实引擎中收敛，外部 supervisor 杀进程不计产品通过；保留普通异常可复用行为。

## Validation and evidence

通过本票实际提供的公开 SDK/CLI 与纯 C Host 外部边界验证，观察公开状态、稳定错误 code、JS/native 副作用、复制数据、退出码和资源关闭事实。复用已有真实 producer、供应制品、宿主 consumer 与原始证据模式；每票执行自身验收及适用的 Cross/Full 门禁，平台收尾票不替代此前回归。

证据记录当前实现/输入/配方/SDK/APE 身份、实际命令、期望与实际结果、stdout/stderr 原始日志、退出码、时限、宿主与执行方式。外部 supervisor 超时表示用例未完成，不能作为产品正确停止/超时的证明；缺资源、失败、skip 与未完成保持可见，不继承旧 M0 成绩。

## Scope boundary

本票交付一个可单独演示和验收的增量，只声明已经实现并实际验证的范围；未实现能力不以成功 stub 公开。完整 M1 合同以实施规格为准；M2 的 OS waker、I/O 取消、迟到 completion 和完整调度，以及独立发布门禁，继续保持原分界。

## Context

[正式 M1 规格](../spec.md) · [已确认设计](../design.md) · [任务索引与依赖](../README.md) · [开发入口](../../../docs/development.md) · [证据合同](../../../docs/development/evidence.md) · [独立审查材料](../../../docs/agents/review.md)

## Comments

2026-11-02：用户以 `ok` 确认 16 票拆分及阻塞依赖，按 to-tickets 发布为 ready-for-agent。本票尚未执行；满足阻塞依赖后才可领取。

2026-11-02：按 Q32 已确认的公开 SDK / 纯 C Host seam 完成 TDD，保留默认字段、期限、累计调度量和执行选项的 red/green 原始字节与日志。已向集成 owner 发送最终 audited producer READY，随后同字节 Windows 普通用户 native PE 与 ARM64 64K full-system guest 各通过全部 16 项。当前集成 `00355b6` 已在最终构建前合入；没有改输入锁或引擎补丁。

## Answer

最终 producer 源码提交 `8fc46d722df573f156cfbd693cac34f2156db2fa`，预算 feature 提交 `ff516a2`；[完整验收记录](../07-budgets-validation.md)保存输入、SDK/recipe/APE 身份和 Ubuntu、Windows、ARM64 64K 的原始证据指针。默认 30,000 ms 期限在引擎 interrupt 和 owner 边界协作执行；累计调度量跨 pump 保留，并用真实无限 Promise stream 验证默认 100,000 jobs 终止。`qaplet_eval_with_options` 只允许收紧，原 `qaplet_eval` 保持便捷入口。期限/调度量错误 code 为 17/18，独立首因覆盖 async rejection 表象并永久 FAILED；查询、结果、关闭保留首因，普通异常仍可复用。

本票提供共享执行 begin/check/dispatch C seam；实际事件调度在依赖本票的 08 沿用该账本，不提前声明事件 API 已验收。文件模块由并行 04 合并后接入同一 begin/checkpoint；本票没有引入模块成功 stub。完整 M1 Cross/Full、macOS/native ARM64 与 release-ready 继续按各后续票推进。

