# Qaplet 文档入口

## 当前路线

- [v1.5 执行计划](plans/qaplet-v1-implementation-plan-v1.5.md)：通用 Runtime、SDK、工具链、包格式与发行；包含 2026-11-01 用户确认的实施修订。
- [v1.5 对齐规格与证据映射](../.scratch/qaplet-v15-alignment/spec.md)：既有成果、新增缺口及 M1 入口。
- [对齐任务与依赖](../.scratch/qaplet-v15-alignment/README.md)：从统一 v3 锁开始，完成目标 QuickJS 与 M0-A 增量后进入正式 M1。
- [基线与验收连续性 ADR](adr/0004-v15-platform-baseline.md)：QEMU 补缺、镜像发布门禁和 M1/M2 SDK 边界。

当前构建仍消费现有分锁；`toolchains/qaplet.lock.json`、目标 QuickJS 提交和新增 ESM/Promise 探针由对齐任务实现。计划文件及[选型样例](plans/qaplet-toolchain-lock-selected.json)不代表这些迁移已经执行。

## 开发与术语

- [开发入口](development.md)：Ubuntu 构建、其他宿主消费制品、仓库根 `.cache/`、证据及 CI 查询。
- [术语](../GLOSSARY.md)、[ADR](adr/)、[本地任务约定](agents/issue-tracker.md)、[独立审查材料](agents/review.md)。

## 历史与参考

- [v1.4 总体计划](plans/qaplet-v1-implementation-plan.md)和[已完成 M0](../.scratch/qaplet-m0/README.md)保留原基线、任务与验收结论。旧成果不自动证明新输入或新增合同。
- 服务相关探针、脚本、测试和独立 workflow 继续留在仓库，作为未来应用及系统行为参考；具体业务应用不进入当前 v1.5 交付主线。
- [工程流程复盘](../.scratch/qaplet-retro/validation.md)保留已建立的检查和已知执行限制。
