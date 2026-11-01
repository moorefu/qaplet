# v1.5 对齐与 M1 入口

**Status:** ready-for-agent
**Execution state:** 任务 01 已完成并实际验证；统一 v3 使用既有 M0 输入。任务 02–06 仍按依赖推进，既有 M0 的原完成状态与输入范围保持不变。

[当前总体计划](../../docs/plans/qaplet-v1-implementation-plan-v1.5.md) · [正式对齐规格](spec.md) · [决策 ADR](../../docs/adr/0004-v15-platform-baseline.md)

对齐规格已按 `to-spec` 模板发布，状态为 `ready-for-agent`，包含 44 条用户故事、实施与测试决定、范围排除及历史证据引用。测试边界已由用户确认：共享外部命令、真实制品运行与现有证据消费者；依赖保持不变，各票记录实际执行状态。

## 推进顺序

| 序号 | 任务 | Blocked by | 阶段作用 |
| --- | --- | --- | --- |
| 01 | [统一 v3 构建输入锁](issues/01-unified-input-lock.md)（resolved） | 无 | M0-A 增量前置已完成 |
| 02 | [目标 QuickJS 升级与补丁复核](issues/02-quickjs-upgrade.md) | 01 | M0-A 增量前置 |
| 03 | [ESM/Promise 微型 C Host](issues/03-m0-a-esm-promise.md) | 01, 02 | 通过后进入正式 M1 |
| 04 | [新输入的跨宿主与 ABI 回归](issues/04-cross-host-revalidation.md) | 03 | 与 M1 OS 无关开发并行；约束相关冻结/验收 |
| 05 | [Fast/Cross/Full 分级 CI](issues/05-tiered-ci.md) | 03 | 分阶段接入；M1 验收需相应 Fast/Cross 证据 |
| 06 | [原始 ZIP 长期镜像](issues/06-original-archive-retention.md) | 无；需存储目标与权限 | 发布前门禁，不阻断 M1 开发 |

`ready-for-agent` 表示任务合同可供实施；执行时仍检查 Blocked by 和资源前置。06 使用 `needs-info`，待落实存储及保留策略。

正式 M1 按总体计划实现 Engine、最小 CLI 与通用 C Embed SDK；本轮只冻结阶段边界，不提前承诺未实现 API。其具体接口、预算和逐票分解在进入 M1 spec 时细化。04/05 的证据影响 M1 验收及后续 OS ABI 冻结，不要求所有后续产品功能同时完成。

## 存量处理

01 的迁移、Ubuntu/断网验证与输入范围见[验收记录](unified-input-lock-validation.md)。02 的前置已满足；
统一锁当前固定旧 M0 源码，目标引擎升级和 ESM/Promise 成绩仍由 02/03 签发。

旧 [M0 12 张任务](../qaplet-m0/README.md)及其 Answer、失败记录、制品身份和 Core gate 均保留。服务应用相关 probe、测试和 workflow 保留为参考，不新增业务应用里程碑。统一锁已由 01 实施；源码切换、缓存物化及新探针运行由相应后续任务执行，规划记录不作为实现成绩。

## 计划更新检查（2026-11-01，b09b061）

19 个文档/选型文件的本地检查通过：141 个本地链接目标存在，6 张任务的状态、必需章节和前置依赖一致且无环；选型 JSON 与既有 cosmocc 锁的 Run/Artifact/digest/size 一致。v1.4 原计划 SHA-256 未变，旧 12 张任务、实际源码/工具链锁、probe 和 workflow 未改。

`python scripts/dev.py preflight --report out/v15-plan-update/preflight.json` 通过：解析 26 个 Python 文件、无生成物被跟踪、工具 CI 静态合同有效。一次性文档检查结果在 `out/v15-plan-update/docs-check.json`；这些是规划与仓库预检，不代表执行新的 M0-A/B/C 或 M1 验收。

## 规格发布检查（2026-11-01）

`to-spec` 的七个模板章节、44 条连续编号用户故事、19 项实施决定及 26 个本地链接检查通过；实现决定未包含具体文件路径或代码片段。六张任务的状态和 v1.4 原计划摘要未变，仓库 preflight 通过。结果保存在 `out/v15-to-spec/spec-check.json` 和 `out/v15-to-spec/preflight.json`；本轮仅修改规格及本索引。
