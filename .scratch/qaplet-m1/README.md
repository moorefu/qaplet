# 正式 M1 任务索引

**Approved:** 2026-11-02，用户以 `ok` 确认 16 个纵向切片及阻塞依赖；23 条直接阻塞边，无环、无传递冗余边。
**Publication baseline:** 每票独立文件；发布时均为 ready-for-agent，当前状态见各票。
**Spec:** [正式 M1 实施规格](spec.md)。[设计记录](design.md)保留 Q1–Q32，规格与 ADR 是共同合同。

**Implementation:** 集成分支 `codex/m1-integration`；01–08、10 已解决并合入，09、12 正在实施，其余按依赖推进。当前状态以各票的 Status、Execution state 和验收记录为准，完整 M1 尚未验收。已合入的闭环包括 [Runtime](01-runtime-validation.md)、[严格值/诊断](02-values-validation.md)、[Promise](03-promise-validation.md)、[文件 ESM/TLA](04-file-modules-validation.md)、[模块策略](05-module-policy-validation.md)、[Host Module](06-host-modules-validation.md)、[预算](07-budgets-validation.md)、[事件桥](08-events-validation.md)及[协作停止](10-stop-validation.md)。工作树清理前保留的逐票原始证据位于主工作区 `out/m1/tickets/NN/out/`，各目录的 `path-mappings.json` 记录原路径及逐文件复制校验结果；01 另记录 ARM64 4K/64K 创建失败修复的红绿证据。

## 任务与阻塞依赖

| 任务 | Blocked by | 发布时状态 |
|---|---|---|
| [01 · 纯 C Host 的最小 Runtime 闭环](issues/01-runtime-c-host.md) | 无 | ready-for-agent |
| [02 · 复制结果、严格值类型与安全诊断](issues/02-result-values-diagnostics.md) | [01](issues/01-runtime-c-host.md) | ready-for-agent |
| [03 · Promise 与 jobs 的显式分步执行](issues/03-promise-job-pump.md) | [01](issues/01-runtime-c-host.md) | ready-for-agent |
| [04 · 文件 ESM、动态 import 与 TLA 闭环](issues/04-file-esm-tla.md) | [03](issues/03-promise-job-pump.md) | ready-for-agent |
| [05 · 模块失败固定与路径拒绝合同](issues/05-module-cache-path-policy.md) | [04](issues/04-file-esm-tla.md) | ready-for-agent |
| [06 · Qaplet Host Module 的同步调用闭环](issues/06-host-module-bridge.md) | [02](issues/02-result-values-diagnostics.md)、[04](issues/04-file-esm-tla.md) | ready-for-agent |
| [07 · 跨 pump 的执行期限与总调度量](issues/07-execution-budgets.md) | [03](issues/03-promise-job-pump.md) | ready-for-agent |
| [08 · 跨线程 POD 到 JS 的有界事件桥](issues/08-bounded-pod-events.md) | [06](issues/06-host-module-bridge.md)、[07](issues/07-execution-budgets.md) | ready-for-agent |
| [09 · 完整资源配置、计量与永久分配失败](issues/09-resource-limits-failures.md) | [05](issues/05-module-cache-path-policy.md)、[08](issues/08-bounded-pod-events.md) | ready-for-agent |
| [10 · 饱和与运行中的协作式停止](issues/10-cooperative-stop.md) | [08](issues/08-bounded-pod-events.md) | ready-for-agent |
| [11 · 并发关闭与最终释放的完整合同](issues/11-close-destroy-lifetime.md) | [09](issues/09-resource-limits-failures.md)、[10](issues/10-cooperative-stop.md) | ready-for-agent |
| [12 · 共用 Runtime 的最小 CLI](issues/12-minimal-cli.md) | [02](issues/02-result-values-diagnostics.md)、[05](issues/05-module-cache-path-policy.md)、[07](issues/07-execution-budgets.md) | ready-for-agent |
| [13 · 可分发 SDK 包与完整纯 C 示例](issues/13-sdk-distribution-c-example.md) | [11](issues/11-close-destroy-lifetime.md)、[12](issues/12-minimal-cli.md) | ready-for-agent |
| [14 · Cross 合同矩阵与生命周期循环验收](issues/14-cross-host-contracts.md) | [13](issues/13-sdk-distribution-c-example.md) | ready-for-agent |
| [15 · ARM64 64K 与相关 Full 门禁验收](issues/15-arm64-64k-full.md) | [13](issues/13-sdk-distribution-c-example.md) | ready-for-agent |
| [16 · M1 严格证据入口与最终集成判定](issues/16-m1-evidence-gate.md) | [14](issues/14-cross-host-contracts.md)、[15](issues/15-arm64-64k-full.md) | ready-for-agent |

状态以各票文件为准，表中记录发布时状态。ready-for-agent 表示票本身已具备实施说明，不表示阻塞票已经完成或产品已通过验收。

## 当前可领取项

- 初始 frontier 只有 [01](issues/01-runtime-c-host.md)。
- 01 完成后，[02](issues/02-result-values-diagnostics.md)、[03](issues/03-promise-job-pump.md)可并行；后续始终按各票真实阻塞边推进。
- 13 完成后，[14](issues/14-cross-host-contracts.md)、[15](issues/15-arm64-64k-full.md)可并行，16 消费两者完整证据。
- 编号是依赖优先的阅读顺序，不要求把前一批所有票都做完再开始下一票；只有明确列出的阻塞票才是实施前置。

## 每票的完成方式

每票都交付可独立演示的产品行为，并在公开 SDK/最小 CLI/纯 C Host 边界做实际验收。Linux 实跑、适用的敏感变更 Cross/Full 和原始证据随票推进；14–16 补齐完整平台矩阵、相关 Full 和严格聚合，不把首次跨平台检查推迟到项目末尾。

Ubuntu 构建 CLI、SDK 分发物与纯 C 示例，其他宿主消费供应制品。各票记录其当前 source/recipe/toolchain/SDK/APE 身份和实际运行事实，旧 M0 通过不能代替新增合同。缺目标、失败、skip、外部 supervisor 超时和未完成都不能签发对应产品通过。

票中验收项目只在实际通过后勾选；实施完成须在本票追加结果、原始证据指针与适用限制，并按本地 tracker 将其解决。依赖未解决的 ready-for-agent 票不提前领取；本次发布不修改父规格或关闭任何既有任务。

## 规格覆盖

16 票共有 80 条验收条件，覆盖规格的全部 60 条用户故事。各票的 Spec stories 记录追溯关系，详细 Q1–Q32 到外部验收的映射沿用规格；既定默认限额、线程/重入、失败缓存、stop/close/destroy 和 Q32 平台门槛不因拆票改变。

M1 设计与规格已确认，本次仅发布任务。M2 的 OS waker/I/O 取消/迟到 completion/完整调度不前移；长期原 ZIP 镜像仍为独立发布门禁，任务发布不签发 release-ready。

## Context

[术语](../../GLOSSARY.md) · [生命周期 ADR](../../docs/adr/0005-m1-runtime-lifetime-boundary.md) · [模块缓存 ADR](../../docs/adr/0006-m1-frozen-module-identity.md) · [资源终止 ADR](../../docs/adr/0007-m1-cooperative-resource-failure.md) · [开发入口](../../docs/development.md) · [证据合同](../../docs/development/evidence.md) · [审查材料](../../docs/agents/review.md)

## Comments

2026-11-02：按用户确认的拆分发布 01–16，一票一文件；初始仅 01 无阻塞。规格和已完成 M0/对齐任务状态保持原范围。

