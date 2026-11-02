# 15: ARM64 64K 与相关 Full 门禁验收

**What to build:** 在真实 ARM64 64K 内核验证新输入的 SDK 关键合同，按既有 Full 规则补齐受影响的 Core/配方回归与原始证明。

**Blocked by:** [13：可分发 SDK 包与完整纯 C 示例](13-sdk-distribution-c-example.md)

**Status:** claimed

**Execution state:** implementing；已确认 13 合入祖先，按已确认 Q32 边界实施 SDK guest 导出/导入与实际 64K/Full 验收。

**Execution prerequisites:** 匹配的新制品、ARM64 64K 原生环境或固定完整系统 guest，以及当前 Full 所需的受影响输入和实际运行资源。

**Spec stories:** 58、59、60。

## Acceptance criteria

- [ ] 原生页覆盖优先，固定完整系统 QEMU 可补缺，guest 内实测 CPU/page-size 并标 native/emulated-system。
- [ ] 变化后的实际 SDK/C Host 制品在 64K 执行必要 ESM/jobs/GC/interrupt/线程事件/关闭合同，记录真实身份。
- [ ] 保留现有敏感改动 Full 选择及受影响的 Core/loader/配方/offline 验证规则，不继承旧输入成绩。
- [ ] 供应输入/guest/日志/摘要缺失或错误拒绝；qemu-user/交叉编译不能替代实际内核覆盖。
- [ ] 长期原件镜像仍为独立发布门禁，M1/Full 证明不自动签发 release-ready。

## Validation and evidence

通过本票实际提供的公开 SDK/CLI 与纯 C Host 外部边界验证，观察公开状态、稳定错误 code、JS/native 副作用、复制数据、退出码和资源关闭事实。复用已有真实 producer、供应制品、宿主 consumer 与原始证据模式；每票执行自身验收及适用的 Cross/Full 门禁，平台收尾票不替代此前回归。

证据记录当前实现/输入/配方/SDK/APE 身份、实际命令、期望与实际结果、stdout/stderr 原始日志、退出码、时限、宿主与执行方式。外部 supervisor 超时表示用例未完成，不能作为产品正确停止/超时的证明；缺资源、失败、skip 与未完成保持可见，不继承旧 M0 成绩。

## Scope boundary

本票交付一个可单独演示和验收的增量，只声明已经实现并实际验证的范围；未实现能力不以成功 stub 公开。完整 M1 合同以实施规格为准；M2 的 OS waker、I/O 取消、迟到 completion 和完整调度，以及独立发布门禁，继续保持原分界。

## Context

[正式 M1 规格](../spec.md) · [已确认设计](../design.md) · [任务索引与依赖](../README.md) · [开发入口](../../../docs/development.md) · [证据合同](../../../docs/development/evidence.md) · [独立审查材料](../../../docs/agents/review.md)

## Comments

2026-11-02：用户以 `ok` 确认 16 票拆分及阻塞依赖，按 to-tickets 发布为 ready-for-agent。本票尚未执行；满足阻塞依赖后才可领取。

