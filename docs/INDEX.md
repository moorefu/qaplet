# Qaplet 文档入口

## 当前路线

- [v1.5 执行计划](plans/qaplet-v1-implementation-plan-v1.5.md)：通用 Runtime、SDK、工具链、包格式与发行；包含 2026-11-01 用户确认的实施修订。
- [v1.5 对齐规格与证据映射](../.scratch/qaplet-v15-alignment/spec.md)：既有成果、新增缺口及 M1 入口。
- [对齐任务与依赖](../.scratch/qaplet-v15-alignment/README.md)：从统一 v3 锁开始，完成目标 QuickJS 与 M0-A 增量后进入正式 M1。
- [基线与验收连续性 ADR](adr/0004-v15-platform-baseline.md)：QEMU 补缺、镜像发布门禁和 M1/M2 SDK 边界。
- [正式 M1 实施规格](../.scratch/qaplet-m1/spec.md)：resolved，Q1–Q32 的接口、所有权、默认预算与拒绝合同已实现，最终独立审查与集成验收完成。
- [正式 M1 任务与依赖](../.scratch/qaplet-m1/README.md)：01–16 与父规格已解决；严格入口汇总当前成功 Full。
- [正式 M1 最终集成与审查](../.scratch/qaplet-m1/final-review-validation.md)：两轴最终零发现、完整原件保存审计和清理后主工作区 98/98 严格回归。
- [正式 M1 已确认设计](../.scratch/qaplet-m1/design.md)：2026-11-02 完成 Q1–Q32；确定生命周期、模块缓存、默认预算、SDK 数据/错误/关闭与事件队列合同，C 原型和逐票实现由后续规格承接。
- [正式 M2 实施规格](../.scratch/qaplet-m2/spec.md)：ready-for-agent；Q1–Q53 已确认，规定显式循环驱动、原生异步交接、取消及两阶段清理、有限默认值和真实平台验收，尚未实施。
- [正式 M2 任务与依赖](../.scratch/qaplet-m2/README.md)：已按用户确认发布 27 张独立任务、38 条直接阻塞边；初始 01–05 可领取，全部尚未实施。

当前构建通过[统一 v3 入口](development/input-lock.md)消费已验目标 QuickJS，旧分锁仅作为摘要绑定的组件清单。正式 M1 已实现 Runtime、静态 SDK、最小 CLI 和纯 C 应用；[SDK 开发入口](development/sdk.md)及[严格证据入口](development/evidence.md#正式-m1-严格入口)记录用法。当前产品源码 `43193d6e9163aaccece5e3c30d96bcbabd3cd260` 的成功 Full `41542426189` 对应五原生宿主、64K 与当前 M0 Full，评估器源码身份独立报告。旧[对齐 04](../.scratch/qaplet-v15-alignment/cross-host-revalidation.md)/[05](../.scratch/qaplet-v15-alignment/tiered-ci-validation.md)成绩保留原范围；后续产品配方变化须重新验收。06 原 ZIP 镜像发布门禁仍暂缓，`release_ready=false`。[选型样例](plans/qaplet-toolchain-lock-selected.json)仍不作为构建输入。

## 开发与术语

- [开发入口](development.md)：Ubuntu 构建、其他宿主消费制品、仓库根 `.cache/`、证据及 CI 查询。
- [术语](../GLOSSARY.md)、[ADR](adr/)、[本地任务约定](agents/issue-tracker.md)、[独立审查材料](agents/review.md)。

## 历史与参考

- [v1.4 总体计划](plans/qaplet-v1-implementation-plan.md)和[已完成 M0](../.scratch/qaplet-m0/README.md)保留原基线、任务与验收结论。旧成果不自动证明新输入或新增合同。
- 服务相关探针、脚本、测试和独立 workflow 继续留在仓库，作为未来应用及系统行为参考；具体业务应用不进入当前 v1.5 交付主线。
- [工程流程复盘](../.scratch/qaplet-retro/validation.md)保留已建立的检查和已知执行限制。
