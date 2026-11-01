# 05：Fast/Cross/Full 分级持续集成（CI-FAST/CROSS/FULL）

**Status:** claimed
**Blocked by:** [03：M0-A 增量](03-m0-a-esm-promise.md)。
**Execution prerequisites:** 仓库/runner 可读写且新产物可供分发；各测试按已实现能力接入。

## Goal

把已有可运行 CI 分阶段映射到 Fast/Cross/Full，日常探索仍在本地，CI 复用同一输入、脚本和证据规则。

## Scope

Fast 每提交检查输入/源码/最小 Engine；Cross 每 PR 运行同字节主要宿主 smoke，Engine/Host/SDK/include/锁/patch 等敏感变更提升检查；Full 按日/RC 执行所需架构/页配置、高风险 ABI、安全及已实现长期合同。原生 ARM64 与完整 QEMU 补缺政策保持一致。CI 构建在 Ubuntu，其他宿主消费制品/SDK；cache 恢复继续校验，仅可信事件写可信缓存，失败也上传证据。

## Out of Scope

仅重命名 workflow 就宣布完成、在 workflow 内另写探针逻辑、未实现功能的空门禁、以服务专有功能阻断 Core、替代本地调试。

## Source Files

`.github/workflows/`、共享 builder/probe/consumer 与 `scripts/dev.py`、`ci_evidence.py`、CI 静态合同测试；具体文件名按现有入口兼容性选择。

## Acceptance criteria

- [ ] 三层触发条件、敏感路径提升规则及已接入用例均可执行检查。
- [ ] producer/consumer 的新锁、commit、runner image、原字节 SHA 与日志可对应，不能用另一 commit 的 CI 判定本次变更。
- [ ] cache 命中仍核验；PR 不写可信缓存，不泄露凭据，不自动切 rolling Nightly。
- [ ] 对未实现/无资源的 Full 项明确状态及阶段；必需项缺失阻断对应验收/发布。
- [ ] M1 具有实际 Linux Fast 与主要宿主 Cross 证据；Full 后续按能力接入，不把模板视为完成。

## Tests

本地静态/编排与敏感路径合同，真实 runner 上执行共享脚本，并核验下载的报告、原件与 producer 身份；区分本地重放、CI 静态检查和远程运行结果。

## Negative Cases

空矩阵、缺必需 job、continue-on-error 掩盖失败、cache-hit 绕过校验、错 SHA/commit/image、无日志、跳过被记成通过、未实现 Node/soak 用例冒绿。

## Evidence

分层配置、实际 run/job/commit、artifact digest、共享消费者结果及本地重放范围说明。

## Rollback

恢复现有工作流编排，保留原始日志、制品和独立服务 workflow，不降低既有验证合同。

## Context

[v1.5 §13](../../../docs/plans/qaplet-v1-implementation-plan-v1.5.md) · [CI 工具](../../../docs/development/ci.md) · [对齐规格](../spec.md)

## Comments

已领取：在 codex/v15-05-tiered-ci 实施；基线 b17bba21437544cb8f4e6b7d6a723518898510c8，包含任务 01–03。

2026-11-02：首个实现提交 `536fa7d` 已冻结 builder `check_ci` 字节并刷新统一锁的 recipe 绑定；
outside-check_ci AST 与原 producer 相同，证据在主工作区 `out/v15/05/recipe-refresh.json`。
共享 `ci_tiers.py` 负责层级/敏感路径/显式状态；原 `m0.yml` identity 保留，增加可信缓存、
实际 Full offline pair 与原始 ZIP/Core consumer 编排。`ci_full.py` 准备 transport/manifest，
不替代已有消费者。新测试从公共 CLI 拒绝缓存绕过、不可信 save、缺必需 job、空矩阵、
skip/continue-on-error 掩盖 probe、空 collection 与其他 commit。

本地 Ubuntu `dev.py check` 已通过（201 tests，62 个显式平台/数据 skips，真实 bootstrap pass）；
这是通用回归与静态成绩，不是远程 Fast/Cross/Full 成绩。合并 04 后将复核，共享真实 pair 的
seal/完整远程 run 与原 ZIP 下载仍待执行；任务因此保持 claimed。所有实际文件保留主工作区
`out/v15/05/`，不将旧 commit 的 CI 成绩重标为本次通过。

