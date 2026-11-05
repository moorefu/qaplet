# Qaplet 文档入口

## 当前路线

- [v1.5 执行计划](plans/qaplet-v1-implementation-plan-v1.5.md)：通用 Runtime、SDK、工具链、包格式与发行；包含 2026-11-01 用户确认的实施修订。
- [v1.5 执行优化](plans/qaplet-v1.5-execution-optimization.md)：当前生效的本地 Linux/Windows 开发顺序、风险定向 CI、交付里程碑与条件工期。
- [当前工程与效率审计](reviews/qaplet-development-efficiency-review.md)：实际分支、能力/平台状态和改造前计时；不将本地实现等同阶段或发布验收。
- [真实 npm 兼容路线](plans/qaplet-npm-compatibility-roadmap.md)：固定发布包调查、Clack 优先、pi-tui 后续和未实现 API 矩阵。
- [v1.5 对齐规格与证据映射](../.scratch/qaplet-v15-alignment/spec.md)：既有成果、新增缺口及 M1 入口。
- [对齐任务与依赖](../.scratch/qaplet-v15-alignment/README.md)：从统一 v3 锁开始，完成目标 QuickJS 与 M0-A 增量后进入正式 M1。
- [基线与验收连续性 ADR](adr/0004-v15-platform-baseline.md)：QEMU 补缺、镜像发布门禁和 M1/M2 SDK 边界。
- [正式 M1 实施规格](../.scratch/qaplet-m1/spec.md)：resolved，Q1–Q32 的接口、所有权、默认预算与拒绝合同已实现，最终独立审查与集成验收完成。
- [正式 M1 任务与依赖](../.scratch/qaplet-m1/README.md)：01–16 与父规格已解决；严格入口汇总当前成功 Full。
- [正式 M1 最终集成与审查](../.scratch/qaplet-m1/final-review-validation.md)：两轴最终零发现、完整原件保存审计和清理后主工作区 98/98 严格回归。
- [正式 M1 已确认设计](../.scratch/qaplet-m1/design.md)：2026-11-02 完成 Q1–Q32；确定生命周期、模块缓存、默认预算、SDK 数据/错误/关闭与事件队列合同，C 原型和逐票实现由后续规格承接。
- [正式 M2 实施规格](../.scratch/qaplet-m2/spec.md)：实施中；Q1–Q53 的接口、安全和正式验收合同保持不变。
- [正式 M2 任务与依赖](../.scratch/qaplet-m2/README.md)：01–10 resolved，11–19 本地完成并独立合入、跨平台欠项保留；20验证最终集成供件，随后20→21→22，23等待22。开发与正式验收轴见[本地执行方式](../.scratch/qaplet-m2/local-development.md)。

当前构建通过[统一 v3 入口](development/input-lock.md)消费已验目标 QuickJS，旧分锁仅作为摘要绑定的组件清单。正式 M1 已实现 Runtime、静态 SDK、最小 CLI 和纯 C 应用；[SDK 开发入口](development/sdk.md)及[严格证据入口](development/evidence.md#正式-m1-严格入口)记录用法。当前产品源码 `43193d6e9163aaccece5e3c30d96bcbabd3cd260` 的成功 Full `41542426189` 对应五原生宿主、64K 与当前 M0 Full，评估器源码身份独立报告。旧[对齐 04](../.scratch/qaplet-v15-alignment/cross-host-revalidation.md)/[05](../.scratch/qaplet-v15-alignment/tiered-ci-validation.md)成绩保留原范围；后续产品配方变化须重新验收。06 原 ZIP 镜像发布门禁仍暂缓，`release_ready=false`。[选型样例](plans/qaplet-toolchain-lock-selected.json)仍不作为构建输入。

## 开发与术语

- [开发入口](development.md)：Ubuntu 构建、其他宿主消费制品、仓库根 `.cache/`、证据及 CI 查询。
- [术语](../GLOSSARY.md)、[ADR](adr/)、[本地任务约定](agents/issue-tracker.md)、[独立审查材料](agents/review.md)。

## 历史与参考

- [v1.4 总体计划](plans/qaplet-v1-implementation-plan.md)和[已完成 M0](../.scratch/qaplet-m0/README.md)保留原基线、任务与验收结论。旧成果不自动证明新输入或新增合同。
- 服务相关探针、脚本、测试和独立 workflow 继续留在仓库，作为未来应用及系统行为参考；具体业务应用不进入当前 v1.5 交付主线。
- [工程流程复盘](../.scratch/qaplet-retro/validation.md)保留已建立的检查和已知执行限制。
