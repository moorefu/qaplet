# Qaplet 文档入口

## 当前路线

- [v1.5 执行计划](plans/qaplet-v1-implementation-plan-v1.5.md)：通用 Runtime、SDK、工具链、包格式与发行；包含 2026-11-01 用户确认的实施修订。
- [v1.5 对齐规格与证据映射](../.scratch/qaplet-v15-alignment/spec.md)：既有成果、新增缺口及 M1 入口。
- [对齐任务与依赖](../.scratch/qaplet-v15-alignment/README.md)：从统一 v3 锁开始，完成目标 QuickJS 与 M0-A 增量后进入正式 M1。
- [基线与验收连续性 ADR](adr/0004-v15-platform-baseline.md)：QEMU 补缺、镜像发布门禁和 M1/M2 SDK 边界。
- [正式 M1 实施规格](../.scratch/qaplet-m1/spec.md)：ready-for-agent，整理 Q1–Q32 的接口、所有权、默认预算与拒绝合同，沿用 Q32 的 CLI/纯 C Host 和真实证据边界；供后续拆票实施。
- [正式 M1 任务与依赖](../.scratch/qaplet-m1/README.md)：用户已确认的 16 个纵向切片，一票一文件、明确阻塞依赖；初始可领取任务为 01。
- [正式 M1 已确认设计](../.scratch/qaplet-m1/design.md)：2026-11-02 完成 Q1–Q32；确定生命周期、模块缓存、默认预算、SDK 数据/错误/关闭与事件队列合同，C 原型和逐票实现由后续规格承接。

当前构建通过[统一 v3 入口](development/input-lock.md)消费已验目标 QuickJS 与 ESM/Promise 微型 Host 配方，旧分锁仅作为精确摘要绑定的组件清单。任务 01–05 已完成：02/03 支持晋级 `pinned`；[04](../.scratch/qaplet-v15-alignment/cross-host-revalidation.md)记录所验输入的六目标 Core、页补缺与严格回归，[05](../.scratch/qaplet-v15-alignment/tiered-ci-validation.md)记录 Fast/Cross/Full 实际执行。后续配方变化须以新输入重新验收，不沿用旧执行成绩。06 原件镜像发布门禁由用户暂缓，release-ready 仍未满足；正式 M1/SDK 尚未实现，微型 Host 仅提供入口证据。[选型样例](plans/qaplet-toolchain-lock-selected.json)仍不作为构建输入。

## 开发与术语

- [开发入口](development.md)：Ubuntu 构建、其他宿主消费制品、仓库根 `.cache/`、证据及 CI 查询。
- [术语](../GLOSSARY.md)、[ADR](adr/)、[本地任务约定](agents/issue-tracker.md)、[独立审查材料](agents/review.md)。

## 历史与参考

- [v1.4 总体计划](plans/qaplet-v1-implementation-plan.md)和[已完成 M0](../.scratch/qaplet-m0/README.md)保留原基线、任务与验收结论。旧成果不自动证明新输入或新增合同。
- 服务相关探针、脚本、测试和独立 workflow 继续留在仓库，作为未来应用及系统行为参考；具体业务应用不进入当前 v1.5 交付主线。
- [工程流程复盘](../.scratch/qaplet-retro/validation.md)保留已建立的检查和已知执行限制。
