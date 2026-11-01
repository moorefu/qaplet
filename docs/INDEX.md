# Qaplet 文档入口

## 当前路线

- [v1.5 执行计划](plans/qaplet-v1-implementation-plan-v1.5.md)：通用 Runtime、SDK、工具链、包格式与发行；包含 2026-11-01 用户确认的实施修订。
- [v1.5 对齐规格与证据映射](../.scratch/qaplet-v15-alignment/spec.md)：既有成果、新增缺口及 M1 入口。
- [对齐任务与依赖](../.scratch/qaplet-v15-alignment/README.md)：从统一 v3 锁开始，完成目标 QuickJS 与 M0-A 增量后进入正式 M1。
- [基线与验收连续性 ADR](adr/0004-v15-platform-baseline.md)：QEMU 补缺、镜像发布门禁和 M1/M2 SDK 边界。

当前构建通过[统一 v3 入口](development/input-lock.md)消费已验目标 QuickJS 与 ESM/Promise 微型 Host 配方，旧分锁仅作为精确摘要绑定的组件清单。任务 02/03 的实际证据支持晋级 `pinned` 并进入正式 M1；新跨宿主矩阵、分级 CI 与发布镜像仍待 04/05/06。[选型样例](plans/qaplet-toolchain-lock-selected.json)仍不作为构建输入。

## 开发与术语

- [开发入口](development.md)：Ubuntu 构建、其他宿主消费制品、仓库根 `.cache/`、证据及 CI 查询。
- [术语](../GLOSSARY.md)、[ADR](adr/)、[本地任务约定](agents/issue-tracker.md)、[独立审查材料](agents/review.md)。

## 历史与参考

- [v1.4 总体计划](plans/qaplet-v1-implementation-plan.md)和[已完成 M0](../.scratch/qaplet-m0/README.md)保留原基线、任务与验收结论。旧成果不自动证明新输入或新增合同。
- 服务相关探针、脚本、测试和独立 workflow 继续留在仓库，作为未来应用及系统行为参考；具体业务应用不进入当前 v1.5 交付主线。
- [工程流程复盘](../.scratch/qaplet-retro/validation.md)保留已建立的检查和已知执行限制。
