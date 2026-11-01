# v1.5 对齐与 M1 入口

**Status:** ready-for-agent
**Execution state:** 任务 01–05 与三条审查修正已完成。当前 pinned 输入 `f0b8dbcf…` 在精确集成提交 `b3adfb8…` 的 Fast/Cross/Full、六目标 Core/64K/offline 及独立本地 4K/publication 严格回归 34/34、无 skip 均已实际验收，见[审查修正验收](review-fixes-validation.md)。旧 e41/1b 的 33/33 与既有 M0 保留原完成状态和输入范围。正式 M1 可进入但尚未实现 M1/SDK；06 保持 needs-info、用户暂缓，不声明 release-ready。

[当前总体计划](../../docs/plans/qaplet-v1-implementation-plan-v1.5.md) · [正式对齐规格](spec.md) · [决策 ADR](../../docs/adr/0004-v15-platform-baseline.md)

对齐规格已按 `to-spec` 模板发布，状态为 `ready-for-agent`，包含 44 条用户故事、实施与测试决定、范围排除及历史证据引用。测试边界已由用户确认：共享外部命令、真实制品运行与现有证据消费者；依赖保持不变，各票记录实际执行状态。

## 推进顺序

| 序号 | 任务 | Blocked by | 阶段作用 |
| --- | --- | --- | --- |
| 01 | [统一 v3 构建输入锁](issues/01-unified-input-lock.md)（resolved） | 无 | M0-A 增量前置已完成 |
| 02 | [目标 QuickJS 升级与补丁复核](issues/02-quickjs-upgrade.md)（resolved） | 01 | M0-A 增量前置已完成 |
| 03 | [ESM/Promise 微型 C Host](issues/03-m0-a-esm-promise.md)（resolved） | 01, 02 | M0-A 通过，正式 M1 可进入 |
| 04 | [新输入的跨宿主与 ABI 回归](issues/04-cross-host-revalidation.md)（resolved） | 03 | 新输入六目标 Core 与严格回归通过；供相关 M1 验收消费 |
| 05 | [Fast/Cross/Full 分级 CI](issues/05-tiered-ci.md)（resolved） | 03 | 分级共享入口与实际 Fast/Cross/Full 通过；事件实测范围见验收 |
| 06 | [原始 ZIP 长期镜像](issues/06-original-archive-retention.md) | 无；需存储目标与权限 | 发布前门禁，不阻断 M1 开发 |

`ready-for-agent` 表示任务合同可供实施；执行时仍检查 Blocked by 和资源前置。06 使用 `needs-info`，待落实存储及保留策略。

正式 M1 按总体计划实现 Engine、最小 CLI 与通用 C Embed SDK；本轮只冻结阶段边界，不提前承诺未实现 API。其具体接口、预算和逐票分解在进入 M1 spec 时细化。04/05 的证据影响 M1 验收及后续 OS ABI 冻结，不要求所有后续产品功能同时完成。

## 存量处理

01 的迁移、Ubuntu/断网验证与输入范围见[验收记录](unified-input-lock-validation.md)。02 的前置已满足；
02 的[精确源码、补丁与 Ubuntu/ARM64 验证](quickjs-upgrade-validation.md)已完成；统一锁当前选择目标
引擎；03 的 [ESM/Promise、晋级与普通构建验收](esm-promise-validation.md)已完成，候选和普通
报告保留各自身份。[04 的同字节跨宿主/完整 Core 与 33/33 严格回归](cross-host-revalidation.md)、
[05 的分级 CI](tiered-ci-validation.md)已完成，实际实现提交为 `1b2f5393e7e7097e9529f72fc68ee52cd491a3a9`。
M1 的 OS 无关工作可进入；这些基础成绩供后续相应验收消费，不代替正式 Runtime/SDK 合同。

旧 [M0 12 张任务](../qaplet-m0/README.md)及其 Answer、失败记录、制品身份和 Core gate 均保留。服务应用相关 probe、测试和 workflow 保留为参考，不新增业务应用里程碑。统一锁已由 01 实施；源码切换、缓存物化及新探针运行由相应后续任务执行，规划记录不作为实现成绩。

## 计划更新检查（2026-11-01，b09b061）

19 个文档/选型文件的本地检查通过：141 个本地链接目标存在，6 张任务的状态、必需章节和前置依赖一致且无环；选型 JSON 与既有 cosmocc 锁的 Run/Artifact/digest/size 一致。v1.4 原计划 SHA-256 未变，旧 12 张任务、实际源码/工具链锁、probe 和 workflow 未改。

`python scripts/dev.py preflight --report out/v15-plan-update/preflight.json` 通过：解析 26 个 Python 文件、无生成物被跟踪、工具 CI 静态合同有效。一次性文档检查结果在 `out/v15-plan-update/docs-check.json`；这些是规划与仓库预检，不代表执行新的 M0-A/B/C 或 M1 验收。

## 规格发布检查（2026-11-01）

`to-spec` 的七个模板章节、44 条连续编号用户故事、19 项实施决定及 26 个本地链接检查通过；实现决定未包含具体文件路径或代码片段。六张任务的状态和 v1.4 原计划摘要未变，仓库 preflight 通过。结果保存在 `out/v15-to-spec/spec-check.json` 和 `out/v15-to-spec/preflight.json`；本轮仅修改规格及本索引。
