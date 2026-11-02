# 06: Qaplet Host Module 的同步调用闭环

**What to build:** 纯 C 宿主注册 host: 常量和函数，由 ESM 导入调用，完成有类型的参数/返回值与可捕获宿主错误。

**Blocked by:** [02：复制结果、严格值类型与安全诊断](02-result-values-diagnostics.md)、[04：文件 ESM、动态 import 与 TLA 闭环](04-file-esm-tla.md)

**Status:** claimed

**Execution state:** implementing；2026-11-02 由 ticket 06 implementer 在 codex/m1-06 领取，使用已确认公开 SDK/纯 C Host seam。

**Execution prerequisites:** Ubuntu 构建环境与固定输入可用，且已合入本票所列阻塞任务；使用仓库约定的共享输入/缓存与现有构建、证据入口。

**Spec stories:** 6、7、8、9、10、11、49。

## Acceptance criteria

- [ ] 注册声明/常量深拷贝，函数代码与 userdata 保留到 close；重名、首次执行后注册、替换/注销拒绝。
- [ ] 支持完整桥接类型、默认 32 参数和 1 MiB 单值边界，参数仅借用到当前回调返回。
- [ ] 返回 setter 同步复制，未设为 undefined，重复设置拒绝并保留第一次，回调外使用拒绝。
- [ ] 宿主普通错误转成带稳定 code 的 JS Error，JS 可捕获；不暴露裸 JSValue/JSContext。
- [ ] 回调中的危险重入为 BUSY，允许的查询/setter 可用；纯 C/ESM 双向调用有真实证据。

## Validation and evidence

通过本票实际提供的公开 SDK/CLI 与纯 C Host 外部边界验证，观察公开状态、稳定错误 code、JS/native 副作用、复制数据、退出码和资源关闭事实。复用已有真实 producer、供应制品、宿主 consumer 与原始证据模式；每票执行自身验收及适用的 Cross/Full 门禁，平台收尾票不替代此前回归。

证据记录当前实现/输入/配方/SDK/APE 身份、实际命令、期望与实际结果、stdout/stderr 原始日志、退出码、时限、宿主与执行方式。外部 supervisor 超时表示用例未完成，不能作为产品正确停止/超时的证明；缺资源、失败、skip 与未完成保持可见，不继承旧 M0 成绩。

## Scope boundary

本票交付一个可单独演示和验收的增量，只声明已经实现并实际验证的范围；未实现能力不以成功 stub 公开。完整 M1 合同以实施规格为准；M2 的 OS waker、I/O 取消、迟到 completion 和完整调度，以及独立发布门禁，继续保持原分界。

## Context

[正式 M1 规格](../spec.md) · [已确认设计](../design.md) · [任务索引与依赖](../README.md) · [开发入口](../../../docs/development.md) · [证据合同](../../../docs/development/evidence.md) · [独立审查材料](../../../docs/agents/review.md)

## Comments

2026-11-02：用户以 `ok` 确认 16 票拆分及阻塞依赖，按 to-tickets 发布为 ready-for-agent。本票尚未执行；满足阻塞依赖后才可领取。

