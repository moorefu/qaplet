# 03：M0-A 的 ESM/Promise 微型 C Host 增量（BOOT-04）

**Status:** ready-for-agent
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

- [ ] 统一 v3 入口决定目标 QuickJS 与原固定 cosmocc，构建前后身份和缓存校验有证据。
- [ ] 在 Ubuntu x64 实际编译并运行微型 C Host / APE：ESM 导出或依赖求值产生已知结果（如 42），Promise jobs 完成可断言。
- [ ] 模块加载/求值错误、job 抛错或未完成均为可观察非通过状态；无空用例成功。
- [ ] stdout/stderr、退出码、时限、清理、source/toolchain/build/artifact/host 均可追溯。
- [ ] 新增合同由实际 evidence 签发；旧 global-eval 成绩保持原范围，通过后才将正式 M1 标为可进入。

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

尚未实施。
