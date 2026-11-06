# 12: 共用 Runtime 的最小 CLI

**What to build:** qaplet run/eval 使用与 C SDK 相同的 loader、结果和预算，提供 argv/cwd 快照、help/version 和正确退出码。

**Blocked by:** [02：复制结果、严格值类型与安全诊断](02-result-values-diagnostics.md)、[05：模块失败固定与路径拒绝合同](05-module-cache-path-policy.md)、[07：跨 pump 的执行期限与总调度量](07-execution-budgets.md)

**Status:** resolved

**Execution state:** resolved；2026-11-02 已实现并通过同字节 Ubuntu / Windows x64 / ARM64 64K 公开 SDK、纯 C Host 与实际 CLI 验收；完整原生 Cross / M1 Full 另计。

**Execution prerequisites:** Ubuntu 构建环境与固定输入可用，且已合入本票所列阻塞任务；使用仓库约定的共享输入/缓存与现有构建、证据入口。

**Spec stories:** 18、55、56。

## Acceptance criteria

- [x] run 源码文件/应用参数、eval 显式 source 及 help/version 实际可用；eval 为 Script，run 为 ESM。
- [x] qaplet:runtime 提供创建时只读 argv/cwd，SDK 与 CLI 同一 factory、身份与错误行为。
- [x] run 不打印 namespace，eval 只打印支持类型，不隐式转换对象或执行诊断 getter。
- [x] CLI 未完成且无可推进工作时立即 UNSETTLED_ENTRY，退出码成功/执行失败/参数错误为 0/1/2。
- [x] 实际 CLI 进程和纯 C 消费者对照成功、缓存/模块错误、rejection、结果/预算行为，保留原始日志与新制品身份。

## Validation and evidence

通过本票实际提供的公开 SDK/CLI 与纯 C Host 外部边界验证，观察公开状态、稳定错误 code、JS/native 副作用、复制数据、退出码和资源关闭事实。复用已有真实 producer、供应制品、宿主 consumer 与原始证据模式；每票执行自身验收及适用的 Cross/Full 门禁，平台收尾票不替代此前回归。

证据记录当前实现/输入/配方/SDK/APE 身份、实际命令、期望与实际结果、stdout/stderr 原始日志、退出码、时限、宿主与执行方式。外部 supervisor 超时表示用例未完成，不能作为产品正确停止/超时的证明；缺资源、失败、skip 与未完成保持可见，不继承旧 M0 成绩。

## Scope boundary

本票交付一个可单独演示和验收的增量，只声明已经实现并实际验证的范围；未实现能力不以成功 stub 公开。完整 M1 合同以实施规格为准；M2 的 OS waker、I/O 取消、迟到 completion 和完整调度，以及独立发布门禁，继续保持原分界。

## Context

[正式 M1 规格](../spec.md) · [已确认设计](../design.md) · [任务索引与依赖](../README.md) · [开发入口](../../../docs/development.md) · [证据合同](../../../docs/development/evidence.md) · [独立审查材料](../../../docs/agents/review.md)

## Comments

2026-11-02：用户以 `ok` 确认 16 票拆分及阻塞依赖，按 to-tickets 发布为 ready-for-agent。本票尚未执行；满足阻塞依赖后才可领取。

2026-11-02：implement_12 领取并确认集成祖先 9174bfad；读取 implement-spec/TDD、规格/设计、ADR 0005–0007、开发与证据合同，沿用 Q32 已批准外部 seam。先后合入 stop 与资源额度集成，最终源码冻结至三个匹配平台执行完成。

## Answer

2026-11-02：`codex/m1-12` 实现公开 SDK factory 复制 argv/cwd、只读
`qaplet:runtime`（能力 bit16、两个 builtin 从创建起计数）及公共头文件驱动的
真实最小 CLI。`run <file> [-- args...]` 为 ESM，不打印 namespace；
`eval --source <text>` 为 Script，只输出 SDK 支持的复制值。Promise/rejection、
文件身份/缓存/错误和有限预算全部调用同一 SDK 路径，不执行用户转换/诊断 getter。
CLI 无外部生产者，PENDING/no-progress 立即 UNSETTLED_ENTRY；SDK 保留 PENDING 合同。
原始 OS 退出为 0/1/2，Windows 在 SDK 清理与 flush 后显式 native ExitProcess，
实际 PE 证据证明 raw1/raw2，没有沿用 Host 的位移规则或占用 stop code23。

最终产品源码 `41b9a89f4b46499d3a7f9209eacce93b84da4a41` 包含当前集成
`32d455e55522ba5b8fdfb3b600d013435e6fb6f0`。现有 `scripts/sdk.py` 扩展为
静态 SDK、审计 Host 与 CLI 两个 APE、完整 sidecar/map/notices 身份和 87 个真实用例。
`out/m1-12/final/{build,run}.json` 的 Ubuntu 87/87 通过；normal-user
`final/windows-user-run.json` 的 86 个必需用例全通过，既有可选 symlink 限制明确保留；
主工作区 `out/m1/12-guest-final/run.json` 的实际 ARM64 65536 页完整系统 87/87 通过。
三个目标使用同一 SDK/APE 字节，所有 42 个 CLI 用例验证 raw OS 退出与 NUL UTF-8 原始日志。
`final-check.json` 仓库检查与真实 C bootstrap 通过；原有 scope skips 不算产品 pass。
独立比对 SDK recipe 的已提交字节和 261 个 guest 原日志，SDK/recipe 身份及 payload
before/after 摘要相同。源码、红绿、中间制品和所有原始执行证据保留。

精确身份、实际命令、红绿证据与范围见[验收记录](../12-cli-validation.md)。
本票不签发完整原生 Cross、完整 M1 Full、release-ready 或 M2 调度能力。

