# 03：M0-A 的 ESM/Promise 微型 C Host 增量（BOOT-04）

**Status:** resolved
**Blocked by:** [01：统一锁](01-unified-input-lock.md)、[02：QuickJS 升级](02-quickjs-upgrade.md)。
**Execution prerequisites:** Ubuntu 与已验证目标引擎/固定 cosmocc 输入可用；无需正式 SDK。

## Goal

用微型 C Host 建立新输入下的 ESM 与 Promise jobs 真实闭环，作为正式 M1 的进入条件。

## Scope

复用既有 Ubuntu APE builder、原件/组件校验、派生 Linux 工具视图及结构化报告。构建目标引擎的最小 ESM fixture，驱动 Promise pending jobs，验证模块结果、job 完成、异常和退出。使用显式可控的模块来源，覆盖最小依赖加载，不提前形成通用产品 resolver 或稳定 SDK ABI。记录原生 Hello、微型 Host、fat APE 的实际命令、flags、SHA 与运行方式。

## Out of Scope

正式 Context Factory/Runtime API、完整 TLA/resolver 产品合同、Reactor、Node API、QJB、网络、长期镜像发布、整个跨宿主矩阵完成。

## Source Files

现有 `scripts/build_ape.py`、`scripts/ape_helpers.py`、原生测试/fixture、统一锁与 evidence 消费路径；具体 Host/fixture 路径在实施时确定，避免占位目录。

## Acceptance criteria

- [x] 统一 v3 入口决定目标 QuickJS 与原固定 cosmocc，构建前后身份和缓存校验有证据。
- [x] 在 Ubuntu x64 实际编译并运行微型 C Host / APE：ESM 导出或依赖求值产生已知结果（如 42），Promise jobs 完成可断言。
- [x] 模块加载/求值错误、job 抛错或未完成均为可观察非通过状态；无空用例成功。
- [x] stdout/stderr、退出码、时限、清理、source/toolchain/build/artifact/host 均可追溯。
- [x] 新增合同由实际 evidence 签发；旧 global-eval 成绩保持原范围，通过后才将正式 M1 标为可进入。

## Tests

同一共享入口验证成功 ESM、依赖、Promise 完成及错误路径，重建后核对输入与产物身份；由外部命令和真实引擎执行判定，不以内部调用次数代替结果。

## Negative Cases

输入缺失/错摘要、模块缺失/语法错、求值异常、job 异常、Promise 不完成/超时、退出码不符、报告写入失败、只运行 global-eval 却提交 ESM 成绩。

## Evidence

M0-A 增量报告、可分发原字节、原始日志及失败报告。02/03 实际探针通过后，按 01 定义完成目标锁的验证状态转换；不因本票通过声明 M1 或 release-ready。

## Rollback

回退新增 Host/fixture 和对应报告消费者；保留旧 M0 probe 与原始证据，不改系统 loader 配置。

## Context

[对齐规格](../spec.md) · [原构建验收](../../qaplet-m0/fat-ape-build-validation.md) · [v1.5 M0-A/M1](../../../docs/plans/qaplet-v1-implementation-plan-v1.5.md)

## Comments

2026-11-02：已在 `codex/v15-03-esm-promise` 领取；基于含任务 02 的 `44d7479`，通过既有 builder/startup/evidence 外部边界执行 TDD。

2026-11-02：受控候选与晋级后的新普通 producer/startup 均为 17/17，实际负向探针和普通
`dev.py check` 通过；任务 resolved，最终集成两轴审查由整体实施流程继续执行。

## Answer

在现有 `quickjs-eval` C Host 内加入固定 ESM export/dependency 和 Promise pending jobs，
保留原两种制品 family。新增十项真实行为覆盖模块缺失/解析/求值失败、真正入队 job 抛错、
未处理 rejection、未完成 Promise 及 256-job 上限；共享 builder/startup/Core 使用 17 项清单。
旧七项报告不能签发新增合同。实现、命令、原始身份、红绿与失败范围见
[03 验收](../esm-promise-validation.md)。

02 的精确引擎及 ARM64 64K 验证与 03 的实际 Ubuntu x64 ESM/jobs 共同支持新 recipe 晋级
`pinned`。候选报告保留原 state/SHA，晋级后通过 `dev.py build-ape` 全新普通构建并重新执行。
最终普通 `dev.py check` 为 197 tests、0 failures/errors、51 个明确 opt-in skips；八项新增
Host/consumer 合同与六项独立实际拒绝/deadline 全部执行通过。

正式 M1 可进入；本票不宣称 M1 实现、完整新跨宿主/ABI、当前 recipe 的完整 offline pair 或
release-ready。04/05 的后续验收继续约束对应冻结，06 仍为 needs-info，没有原件上传或发布。
