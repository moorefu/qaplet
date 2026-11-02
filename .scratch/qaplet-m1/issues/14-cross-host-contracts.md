# 14: Cross 合同矩阵与生命周期循环验收

**What to build:** 在主要宿主消费同字节 CLI/C Host APE，取得完整 Linux 合同和 Cross 关键行为、循环次数的实际证据。

**Blocked by:** [13：可分发 SDK 包与完整纯 C 示例](13-sdk-distribution-c-example.md)

**Status:** claimed

**Execution state:** implementing；以已合入 01–13 的 integration `31155083b3677bf9888d14d6c01dd8758d551707` 为基线，按 Q32 公开边界进行 TDD；完整原生 Cross 实际执行前不声明验收。

**Execution prerequisites:** 匹配的新 SDK/CLI/C Host 供应制品，以及 Windows x64、macOS x64/ARM64、Linux ARM64 和 Linux Fast 的实际执行资源。

**Spec stories:** 58、59、60。

## Acceptance criteria

- [ ] Linux Fast 全部本规格正/负合同必需执行，至少 1,000 次创建/关闭/销毁。
- [ ] Windows x64、macOS x64/ARM64、Linux ARM64 消费供应制品，分别至少 100 次生命周期及关键 SDK 合同。
- [ ] 核对实际 SDK/APE 身份、OS/CPU/loader/page-size/mode，逐例记录期望/实际、退出码、时限和原始日志。
- [ ] 各票已有 smoke/敏感变更回归继续运行，本票补齐完整矩阵与声明，不把首次跨平台检查推迟到收尾。
- [ ] 缺目标、失败、skip 或未完成明确阻断对应通过，固定引擎循环/TLA 只声明实际通过子集。

## Validation and evidence

通过本票实际提供的公开 SDK/CLI 与纯 C Host 外部边界验证，观察公开状态、稳定错误 code、JS/native 副作用、复制数据、退出码和资源关闭事实。复用已有真实 producer、供应制品、宿主 consumer 与原始证据模式；每票执行自身验收及适用的 Cross/Full 门禁，平台收尾票不替代此前回归。

证据记录当前实现/输入/配方/SDK/APE 身份、实际命令、期望与实际结果、stdout/stderr 原始日志、退出码、时限、宿主与执行方式。外部 supervisor 超时表示用例未完成，不能作为产品正确停止/超时的证明；缺资源、失败、skip 与未完成保持可见，不继承旧 M0 成绩。

## Scope boundary

本票交付一个可单独演示和验收的增量，只声明已经实现并实际验证的范围；未实现能力不以成功 stub 公开。完整 M1 合同以实施规格为准；M2 的 OS waker、I/O 取消、迟到 completion 和完整调度，以及独立发布门禁，继续保持原分界。

## Context

[正式 M1 规格](../spec.md) · [已确认设计](../design.md) · [任务索引与依赖](../README.md) · [开发入口](../../../docs/development.md) · [证据合同](../../../docs/development/evidence.md) · [独立审查材料](../../../docs/agents/review.md)

## Comments

2026-11-02：用户以 `ok` 确认 16 票拆分及阻塞依赖，按 to-tickets 发布为 ready-for-agent。本票尚未执行；满足阻塞依赖后才可领取。

2026-11-02：领取本票；13 已 resolved 且属于本分支祖先。沿用 Q32 已批准的 SDK/CLI/纯 C Host 与现有 producer/native consumer seam，不新增私有测试边界。复用同一 m0.yml 原生 job 和供应 SDK 字节；缺项、skip、timeout 与 TCG 均不冒充必需原生 Cross 成绩。

