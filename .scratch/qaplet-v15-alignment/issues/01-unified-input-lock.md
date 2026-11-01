# 01：统一 v3 构建输入锁（LOCK-01）

**Status:** ready-for-agent
**Blocked by:** 无。
**Execution prerequisites:** Ubuntu、现有锁及可验证缓存可读；先读 `docs/development.md`。本文不是已经实施的迁移。

## Goal

本地、CI、离线重构和证据消费者通过一个 v3 入口确定相同输入，作为新 M0-A 的前置。

## Scope

实现 `toolchains/qaplet.lock.json` / `qaplet.toolchain-lock.v3` 的读取、验证、迁移与消费者切换。审计现有 QuickJS/cosmocc/helper 锁中身份和清单的映射，区分当前已验基线与 02 的待升级目标。原件、组件 Manifest、source Commit/Tree/VERSION、patch、派生工具和配方身份分开。旧分锁可保留作历史或被精确摘要绑定的清单，迁移后不再独立决定输入版本。

现有 cosmocc 原 ZIP 与清单可复用并重新核验；原 ZIP 权限/链接按实际观测处理。保留仓库 `.cache/` 已验内容和兼容读取路径，不以重排缓存目录冒充锁迁移。选型样例中未知的新 source/manifest 字段不能填造；`selected_pending_validation` 仅允许显式获取与受控探针，普通/发行构建须满足其完整输入合同。

## Out of Scope

执行 QuickJS 升级、实现 ESM/SDK、重新选取 Nightly、部署长期镜像、改变历史验收结论。

## Source Files

`toolchains/qaplet.lock.json`（待建）、`docs/plans/qaplet-toolchain-lock-selected.json`、`third_party/quickjs/source.lock.json`、`toolchains/cosmocc.lock.json`、helper 来源锁；`scripts/quickjs_source.py`、`nightly_toolchain.py`、`build_ape.py`、`native_toolchain.py`、`offline_integrity.py`、证据/CI 消费者与相应合同测试。实际代码边界由实施选择，全部入口须纳入迁移清单。

## Acceptance criteria

- [ ] v3 字段、状态转换、清单摘要算法与迁移说明完整；精确输入从单一入口解析。
- [ ] 构建、输入校验、离线、CI 和证据消费者一致；未消费的旧权威路径被可执行检查发现。
- [ ] 原 ZIP digest/size、组件清单、源码/补丁与派生 helper 身份保持独立；Run HEAD 关联不升级为 libc 源码或签名证明。
- [ ] 现有缓存可验证复用；任何新格式物化使用 staging/原子发布，失败保留原数据。
- [ ] 未验证目标不能变成 pinned；长期镜像状态单独作为发布门禁，不阻断 M1 的已验缓存使用。

## Tests

通过外部入口重放现有数据的迁移/验证、普通构建与实际离线消费；逐一核对入口解析的身份，执行错版本/字段/清单/旧锁冲突拒绝。已有回归按新入口更新，不能只测试 schema 可解析。

## Negative Cases

空/未知 schema、占位摘要、Commit/Tree/patch 不符、两个入口选出不同输入、组件缺失、原件损坏、helper 身份遗漏、离线缺缓存、误将 selected 状态用于普通发行构建。

## Evidence

迁移映射、入口覆盖清单、前后 identity 对照、真实命令/退出码/日志、拒绝报告及缓存保留证明。引用现有验证时标明其输入与范围。

## Rollback

回退消费者与新锁入口，恢复已记录的旧入口版本；保留原锁、原 ZIP 和缓存，不删除历史证据。

## Context

[对齐规格](../spec.md) · [v1.5 §1](../../../docs/plans/qaplet-v1-implementation-plan-v1.5.md) · [ADR 0004](../../../docs/adr/0004-v15-platform-baseline.md)

## Comments

2026-11-01：使用 `implement` 实施。统一入口、旧清单精确绑定、消费者切换、候选限制和迁移 CLI 已实现；Ubuntu 合同回归及实际 Hello/eval 构建已执行，最终配方的断网双构建与独立 code-review 进行中。最终证据和验收勾选在两轴审查及适用验证完成后更新。
