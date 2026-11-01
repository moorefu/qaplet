# v1.5 对齐规格：从既有 M0 进入 M1

**Status:** ready-for-agent
**Confirmed:** 2026-11-01，grill-with-docs 六项决策及最终理解已获用户确认。
**Execution state:** 本轮为文档与任务规划。现有输入锁、源码、缓存、probe 和 workflow 不因本文自动迁移。
**Baseline:** [v1.5 实施修订](../../docs/plans/qaplet-v1-implementation-plan-v1.5.md)与 [ADR 0004](../../docs/adr/0004-v15-platform-baseline.md)。

## Goal

复用已完成的平台基础，明确新输入与新合同的增量工作，使正式 M1 有可执行前置，并保留历史成果的适用范围。

## 已确认的执行决策

1. QuickJS 目标为 `2833510432045f72915919cef680ba59e5153b16`。先升级、复核 ARM64 full-pointer 补丁并重新验收；通过后切换 M1 构建输入。
2. 沿用完整系统 QEMU 补齐 ARM64 64K；固定 guest 输入并实测页大小，区分 native/emulated-system，不增加物理真机硬前置。
3. 正式 M1 前增加独立 M0-A ESM/Promise 微型 C Host 闭环。
4. 统一 `toolchains/qaplet.lock.json` / v3 作为 M0-A 增量前置，覆盖消费者与证据链；现有 `.cache/` 内容和可验证路径优先复用。
5. M1 可用已验缓存；长期原 ZIP 镜像与同 SHA 恢复验收在发布前完成。
6. M1 做最小 SDK、owner-thread 约束、有界 POD 队列和协作式停止；跨 OS 唤醒、I/O 取消、迟到 completion 和完整调度由 M2 完成。

## 现状与目标映射

| 领域 | 已有证据 | v1.5 增量/边界 |
| --- | --- | --- |
| cosmocc | Run `41169443573` / Artifact `13031425078`；原 ZIP `419652152` 字节，SHA-256 `f2f984d3a9790674fc0da11c1c00a32d4fe3f8dafde99af74b0c6dc24a2a16cf`；实际获取、校验、清单和离线恢复已完成 | 复用同一原件与既有证明；每次使用继续验证，不重新追最新 Nightly |
| 归档权限/链接 | 现有审计记录 3,043 个 file/link 条目、208 symlinks、77 executable entries，未进行权限修复 | 按实际元数据验证；仅确有缺失时采用经审计的恢复规则，不强制执行一次无必要 chmod |
| QuickJS | 当前为 `e621135dc542e000e31038bedc52944aa0adaf1a`，有已锁定 ARM64 full-pointer overlay | 升级到目标提交后重算 Tree、内容、patch 与配方身份；相同 VERSION 日期不能继承旧成绩 |
| 锁入口 | 构建消费 QuickJS/cosmocc 分锁及独立 helper 锁 | 统一 v3 后只由一个入口决定输入；历史分锁若作为清单被引用，必须有精确摘要绑定，不再独立选版本 |
| M0-A | Hello/eval、错误、指针/对象等已测；eval 使用 `JS_EVAL_TYPE_GLOBAL` | 新增 ESM 加载、Promise job 驱动与微型 C Host；不把旧 global-eval 记成新合同通过 |
| M0-B/C | 既有六目标 startup/I/O、64K guest、离线和只读 Core gate 已通过原合同 | 原结论保持 resolved；目标引擎、补丁、配方或制品变化后重新签发对应覆盖 |
| SDK/CLI | M0 未交付正式 Runtime/SDK | M1 从统一 Factory、ESM/TLA/jobs/预算、最小 CLI 和纯 C Host 建立公开合同 |
| CI | 已有实际 Ubuntu producer、同字节 consumer、QEMU 补缺及独立服务 probe workflow | Fast/Cross/Full 是后续调度目标，不以重命名文件代表已实现 |
| 原件留存 | 本地原 ZIP 与缓存恢复已有证据 | 长期镜像目标及权限尚未确定，作为发布门禁独立落实 |
| 服务相关工作 | 独立 SCM/Job ABI probe、测试及 workflow 已完成各自合同，限制保留 | 作为未来应用/系统参考；不纳入当前平台的 M3/M8/M9 业务交付 |

事实来源：[输入与工具链](../qaplet-m0/nightly-toolchain-validation.md)、[QuickJS](../qaplet-m0/quickjs-source-validation.md)、[64K 修正](../qaplet-m0/arm64-64k-validation.md)、[完整 Core](../qaplet-m0/core-evidence-gate-validation.md)、[最终审查](../qaplet-m0/final-review-validation.md)。这些是既有输入和范围的历史证据，本轮未重新执行它们。

## M1 范围与前置

01 → 02 → 03 完成后进入正式 M1；04/05 与 M1 OS 无关工作并行。M1 覆盖统一 Runtime/Context Factory、显式 ESM normalize/load、TLA/Promise/jobs、GC/内存/栈/中断预算、最小 CLI、版本化 C Embed SDK、模块注册与纯 C Host 示例。JS API 操作限定 owner thread；POD 队列有界，跨线程只提交数据/停止请求，由 owner 处理。

M2 承接跨 OS waker、Reactor/WorkPool、I/O 取消、迟到 completion、Process/TTY 与完整调度。M1 的最小事件/停止合同不等于这些 OS 语义已冻结。验收需要实际 Linux Fast 和相应主要宿主 Cross 证据；完整门禁按新输入、制品身份、实际 OS/架构/页大小及执行方式判定。

06 的长期镜像不阻断 M1 开发，但阻断正式发布。已有缓存仍须验证；原件缺失、身份不符或无可用同 SHA 来源时 fail-closed。M1 不引入业务服务应用、Node 全量兼容、QJB、网络/TLS或大资源产品验收。

## 完成与验证

每张实现任务遵循本地外部命令、实际 OS 行为和结构化证据 seam。Ubuntu 构建制品和 SDK，其他平台验证所提供的原始字节；可复用缓存位于主工作区物理 `.cache/`，输出位于 `out/`。审查按仓库规则生成并验证独立轴材料。

本规格不签发新锁 pinned、新 M0-A/B/C pass、M1 完成或 release-ready；这些结论由对应任务的实际结果产生。历史 M0 状态和文件保持可追溯，撤回本轮规划可回退文档而不触碰既有制品、缓存或系统配置。
