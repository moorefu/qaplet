# 04：新输入的同字节跨宿主及 ABI 回归（BOOT-04B/06/07）

**Status:** claimed
**Blocked by:** [03：M0-A 增量](03-m0-a-esm-promise.md)。
**Execution prerequisites:** 新产物与身份可用；对应原生 runner 或已锁定完整 ARM64 guest 可执行。

## Goal

为新 QuickJS/配方/产物重新签发 M0-B/C 覆盖，复用旧探针而不借用旧摘要或旧通过状态。

## Scope

Ubuntu 构建一次，原字节分发到 Linux x64、Windows x64、macOS x64/ARM64、Linux ARM64 4K/64K；helper、重命名、签名或其他派生身份分别记录。复用 startup、native I/O、64K 和 Core consumer；对目标引擎及其变化影响的合同重新执行。ARM64 原生优先，缺失页配置可用固定完整系统 QEMU/TCG guest，guest 内确认 CPU/kernel/page size；native/emulated-system 分开计数。

## Out of Scope

物理 ARM64 64K 硬前置、服务应用功能、把 M0 probe 当成正式 Node/SDK 功能、为各宿主分别构建另一份 fat APE。

## Source Files

`scripts/startup.py`、`arm64_guest.py`、`pages64k.py`、`native_io.py`、`core_gate.py`、现有 fixture/consumer 配置与相关 CI 编排；保留旧配置/报告的来源标识。

## Acceptance criteria

- [ ] 新覆盖精确绑定统一锁、新源码/补丁/配方和各自 artifact SHA；不能混入旧 producer。
- [ ] 各实际宿主或完整 guest 有 OS/执行架构/kernel/page size/执行方式和 helper 身份。
- [ ] 同字节 ESM/Promise 基础行为及变化影响的 I/O/64K 正负合同可复验。
- [ ] 空/缺失/冲突/过期报告、错误 artifact、错误页配置、qemu-user 替代完整 guest 均不能通过。
- [ ] 本票可与 M1 OS 无关实现并行；相关 M1 Cross 验收及 Host/SDK OS ABI 冻结消费对应的新证据。

## Tests

在真实配置上运行共享 probe/consumer，并注入旧摘要、错 producer、页大小/执行方式冲突和缺项；失败同样保留日志。按输入/工具链变更影响重做所需离线/边界验收。

## Negative Cases

helper 冒称直接运行、TCG 冒称物理 native、x64 4K 冒充 ARM64、旧报告和新输入混用、guest 日志缺失、失败被过滤、服务结果误入 Core。

## Evidence

新覆盖清单、原始日志/串口、报告身份、同字节对照与拒绝结果；未接入资源给出 not_run/blocked 原因。

## Rollback

回退新 consumer/编排，保留历史与新结果；不撤销旧输入在旧合同下的完成结论。

## Context

[ADR 0003](../../../docs/adr/0003-m0-ci-qemu-validation.md) · [ADR 0004](../../../docs/adr/0004-v15-platform-baseline.md) · [原完整 Core](../../qaplet-m0/core-evidence-gate-validation.md)

## Comments

2026-11-02：任务 04 已由 codex/v15-04-cross-host 领取，基于 b17bba21437544cb8f4e6b7d6a723518898510c8（01–03 已合并）；先按共享 CLI/实际制品/既有只读 consumer 完成红绿合同，再于 05 配方冻结后执行新证据。
