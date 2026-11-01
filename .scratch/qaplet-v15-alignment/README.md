# v1.5 对齐与 M1 入口

**Status:** ready-for-agent
**Execution state:** 计划已确认；以下实现任务尚未执行。既有 M0 的完成状态保持不变。

[当前总体计划](../../docs/plans/qaplet-v1-implementation-plan-v1.5.md) · [对齐规格](spec.md) · [决策 ADR](../../docs/adr/0004-v15-platform-baseline.md)

## 推进顺序

| 序号 | 任务 | Blocked by | 阶段作用 |
| --- | --- | --- | --- |
| 01 | [统一 v3 构建输入锁](issues/01-unified-input-lock.md) | 无 | M0-A 增量前置 |
| 02 | [目标 QuickJS 升级与补丁复核](issues/02-quickjs-upgrade.md) | 01 | M0-A 增量前置 |
| 03 | [ESM/Promise 微型 C Host](issues/03-m0-a-esm-promise.md) | 01, 02 | 通过后进入正式 M1 |
| 04 | [新输入的跨宿主与 ABI 回归](issues/04-cross-host-revalidation.md) | 03 | 与 M1 OS 无关开发并行；约束相关冻结/验收 |
| 05 | [Fast/Cross/Full 分级 CI](issues/05-tiered-ci.md) | 03 | 分阶段接入；M1 验收需相应 Fast/Cross 证据 |
| 06 | [原始 ZIP 长期镜像](issues/06-original-archive-retention.md) | 无；需存储目标与权限 | 发布前门禁，不阻断 M1 开发 |

`ready-for-agent` 表示任务合同可供实施；执行时仍检查 Blocked by 和资源前置。06 使用 `needs-info`，待落实存储及保留策略。

正式 M1 按总体计划实现 Engine、最小 CLI 与通用 C Embed SDK；本轮只冻结阶段边界，不提前承诺未实现 API。其具体接口、预算和逐票分解在进入 M1 spec 时细化。04/05 的证据影响 M1 验收及后续 OS ABI 冻结，不要求所有后续产品功能同时完成。

## 存量处理

旧 [M0 12 张任务](../qaplet-m0/README.md)及其 Answer、失败记录、制品身份和 Core gate 均保留。服务应用相关 probe、测试和 workflow 保留为参考，不新增业务应用里程碑。统一锁、源码切换、缓存物化及新探针运行由本目录任务执行，不能将本次计划更新记成实现成绩。

## 本轮计划检查（2026-11-01）

19 个文档/选型文件的本地检查通过：141 个本地链接目标存在，6 张任务的状态、必需章节和前置依赖一致且无环；选型 JSON 与既有 cosmocc 锁的 Run/Artifact/digest/size 一致。v1.4 原计划 SHA-256 未变，旧 12 张任务、实际源码/工具链锁、probe 和 workflow 未改。

`python scripts/dev.py preflight --report out/v15-plan-update/preflight.json` 通过：解析 26 个 Python 文件、无生成物被跟踪、工具 CI 静态合同有效。一次性文档检查结果在 `out/v15-plan-update/docs-check.json`；这些是规划与仓库预检，不代表执行新的 M0-A/B/C 或 M1 验收。
