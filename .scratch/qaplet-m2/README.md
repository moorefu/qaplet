# 正式 M2 任务与依赖

**Approval:** 2026-11-03，用户明确确认“按 27 张发布”。
**Execution state:** in_progress；2026-11-03 用户调用 `/implement-spec`，在 `codex/m2-integration` 上实施。逐票验收状态以任务及[实施记录](implementation.md)为准。
**Parent:** [正式 M2 规格](spec.md)，Q1–Q53、110 条用户故事和 28 组验收合同；发布子票不修改或关闭父规格。
**Coverage:** 27 票、38 条直接阻塞边、135 条逐票验收条件。主要责任及完整追踪见[已确认拆票方案](ticket-proposal.md#覆盖映射)。

**Current development mode:** 用户于2026-11-05明确要求先在本地开发、主要推进Linux/Windows；
本阶段以本地实现验收完成作为开发依赖，跨平台/CI欠项保留最终收口。详[本地开发方式](local-development.md)。

## 任务与直接阻塞

ready-for-agent 表示本票需求可实施；领取前确认直接blocker已满足当前阶段的本地开发前置。
`Local development: complete`可放行本地开发，跨平台未验收仍保留claimed/cross_deferred。
编号按blocker在前排列，独立任务可并行，不按编号强行串行。

| 票号 | 独立任务 | 直接 blocker | Status |
|---|---|---|---|
| 01 | [M2 Runtime 的可选创建与兼容生命周期](issues/01-runtime-opt-in-compatibility.md) | 无 | resolved |
| 02 | [唤醒与 Socket 等待的真实 OS 探针](issues/02-waker-reactor-os-probes.md) | 无 | resolved |
| 03 | [Pipe 与借用标准流的可取消接入探针](issues/03-pipe-stdio-cancel-os-probes.md) | 无 | resolved |
| 04 | [子进程参数、stdio 与继承隔离探针](issues/04-process-inheritance-os-probes.md) | 无 | resolved |
| 05 | [真实终端租约、resize 与 Console 文本探针](issues/05-tty-console-os-probes.md) | 无 | resolved |
| 06 | [等待中的 Runtime 接收事件与停止](issues/06-wait-events-keepalive-stop.md) | [01](issues/01-runtime-opt-in-compatibility.md)、[02](issues/02-waker-reactor-os-probes.md) | resolved |
| 07 | [一次 timer 推进 TLA 与自然退出](issues/07-one-shot-timer-tla.md) | [06](issues/06-wait-events-keepalive-stop.md) | resolved |
| 08 | [重复 timer、immediate 与检查点轮转](issues/08-interval-immediate-fair-checkpoints.md) | [07](issues/07-one-shot-timer-tla.md) | resolved |
| 09 | [宿主自有事件循环的原生通知接入](issues/09-host-notify-loop-integration.md) | [06](issues/06-wait-events-keepalive-stop.md) | resolved |
| 10 | [纯 C Host 的有界原生工作闭环](issues/10-c-host-bounded-work.md) | [06](issues/06-wait-events-keepalive-stop.md) | resolved |
| 11 | [任务取消竞争、结果 release 与容量保留](issues/11-work-cancel-result-retention.md) | [10](issues/10-c-host-bounded-work.md) | claimed |
| 12 | [JS 命名工作、Promise 与取消闭环](issues/12-js-named-work-promises.md) | [08](issues/08-interval-immediate-fair-checkpoints.md)、[11](issues/11-work-cancel-result-retention.md) | claimed |
| 13 | [纯 C Host 的显式 spawn 与退出观察](issues/13-c-host-process-spawn-exit.md) | [04](issues/04-process-inheritance-os-probes.md)、[06](issues/06-wait-events-keepalive-stop.md) | claimed |
| 14 | [直接子进程取消、回收与关闭竞争](issues/14-process-cancel-reap.md) | [13](issues/13-c-host-process-spawn-exit.md) | claimed |
| 15 | [子进程三路有界字节 I/O 与 EOF](issues/15-c-host-process-pipe-io.md) | [03](issues/03-pipe-stdio-cancel-os-probes.md)、[11](issues/11-work-cancel-result-retention.md)、[13](issues/13-c-host-process-spawn-exit.md) | claimed |
| 16 | [JS Process 与 stdio 的完整异步闭环](issues/16-js-process-stdio-liveness.md) | [12](issues/12-js-named-work-promises.md)、[14](issues/14-process-cancel-reap.md)、[15](issues/15-c-host-process-pipe-io.md) | claimed |
| 17 | [标准 stdio 的显式安全接入](issues/17-standard-stdio-opt-in.md) | [12](issues/12-js-named-work-promises.md)、[15](issues/15-c-host-process-pipe-io.md) | claimed |
| 18 | [TTY 租约、resize 与 Console UTF-8](issues/18-tty-console-leases.md) | [05](issues/05-tty-console-os-probes.md)、[17](issues/17-standard-stdio-opt-in.md) | claimed |
| 19 | [跨资源预算、POD 快照与终止首因](issues/19-aggregate-budgets-metrics-failures.md) | [09](issues/09-host-notify-loop-integration.md)、[16](issues/16-js-process-stdio-liveness.md)、[18](issues/18-tty-console-leases.md) | claimed |
| 20 | [组合资源的停止与两阶段清理](issues/20-mixed-stop-close-quiesce.md) | [09](issues/09-host-notify-loop-integration.md)、[16](issues/16-js-process-stdio-liveness.md)、[18](issues/18-tty-console-leases.md) | claimed |
| 21 | [CLI 异步运行与入口结果保存](issues/21-cli-async-run-results.md) | [20](issues/20-mixed-stop-close-quiesce.md) | claimed |
| 22 | [CLI 中断交接与清理超时报告](issues/22-cli-signal-stop-cleanup-budget.md) | [21](issues/21-cli-async-run-results.md) | ready-for-agent |
| 23 | [完整 SDK 分发与包外 C Host 示例](issues/23-sdk-package-c-host-example.md) | [19](issues/19-aggregate-budgets-metrics-failures.md)、[22](issues/22-cli-signal-stop-cleanup-budget.md) | ready-for-agent |
| 24 | [五原生目标的合同矩阵与生命周期](issues/24-native-cross-contract-matrix.md) | [23](issues/23-sdk-package-c-host-example.md) | ready-for-agent |
| 25 | [实际 ARM64 64K 的新合同与清理验收](issues/25-arm64-64k-contract-matrix.md) | [23](issues/23-sdk-package-c-host-example.md) | ready-for-agent |
| 26 | [Linux/Windows Full 长测与资源稳定性](issues/26-full-soak-lifecycle.md) | [23](issues/23-sdk-package-c-host-example.md) | ready-for-agent |
| 27 | [M2 严格证据入口与最终集成判定](issues/27-strict-m2-evidence-gate.md) | [24](issues/24-native-cross-contract-matrix.md)、[25](issues/25-arm64-64k-contract-matrix.md)、[26](issues/26-full-soak-lifecycle.md) | ready-for-agent |

## 当前可领取项

2026-11-05：16、17、18均已本地完成并独立合并，实际合并点分别为 `015cdb9f`、
`caa439ae`、`3e85a272`（接收收据 `71a5db50`、`5bc82e1c`、`64624ddd`）。
19、20随后均已本地完成并独立合入`597020eb`、`943c1962`，接收分别为`2b3035f7`、
`776e50cb`；21据此领取实施，22仍等待21本地完成及独立合入。19/20正式Cross/Full仍待验。
23的19前置已满足，仍等待22。完整开发前置和各票验证范围见
[本地开发方式](local-development.md#2026-11-05剩余开发前置与验证范围)，原阻塞边不变。

## 初始 frontier（历史记录）

初始 frontier 为 01、02、03、04、05（现均已resolved）：

- [01 · M2 Runtime 的可选创建与兼容生命周期](issues/01-runtime-opt-in-compatibility.md)：承接必要重构，以可运行 M2 空实例和旧 M1 兼容性验收。
- [02 · 唤醒与 Socket 等待的真实 OS 探针](issues/02-waker-reactor-os-probes.md)。
- [03 · Pipe 与借用标准流的可取消接入探针](issues/03-pipe-stdio-cancel-os-probes.md)。
- [04 · 子进程参数、stdio 与继承隔离探针](issues/04-process-inheritance-os-probes.md)。
- [05 · 真实终端租约、resize 与 Console 文本探针](issues/05-tty-console-os-probes.md)。

02–05 是独立探针闭环，可与 01 并行；产品资源接入均经由 01/06，必要的驱动和原生保留边界先落地。探针提供 backend 事实，不代表 M2 产品已经通过。

## 后续 frontier

- 01、02 完成后可推进 06，随后分出 timer（07→08）、HostNotify（09）、C 原生任务（10→11）；取得 04 的事实后也可推进 Process（13→14）。
- 12 汇合 JS 调度与任务终态；15 汇合 pipe 事实、操作记录和 Process。它们分别支撑 JS Process（16）与标准 stdio（17），18 再接入终端。
- 19 与 20 可并行核对跨资源预算/错误/观测和组合退出屏障。
- 23 完成后，24、25、26 可并行取得五原生、实际 64K 和 Full 长测原件；27 据此独立判定。
- 以上不是额外固定波次，只按直接 blocker 和实际完成状态领取；不以共享文件产生虚构前置。

## 每票的完成方式

每票有五条自身验收，并列出相关父规格故事、Q 和 AC 组。C Host 的原生任务/进程子集也是完整产品边界，JS wrapper 后续复用同一合同；每次只公开已经完成的能力。

新增资源从第一票就实施有限容量、结果保留和安全 stop/close/quiesce/destroy。19/20 是跨资源联立及竞态收口，不是允许早期票延后预算或首次清理安全的理由。

各票随交付执行公开正负验收与适用 Cross/Full；最终矩阵票不替代首次平台检查。构建与 SDK 分发在 Ubuntu，其他宿主只消费供应制品；复用固定输入、主工作区缓存及既有 producer/consumer。真实 backend 与受控系统故障夹具保持各自身份，外部 timeout 不能证明产品正确停止。

最终 M2 需五原生目标及实际 64K 的适用合同、各至少 1,000 次生命周期及 100 次混合异步；Linux/Windows Full 各另做 10,000 次混合生命周期和 1 小时交替长测。24 小时、网络产品能力及发布镜像保持原阶段边界，当前不签发 release-ready。

## 覆盖与父规格连续性

- [已确认方案的故事责任表](ticket-proposal.md#用户故事的主要落地责任)：110 条均有实际实施或验证责任。
- [决策责任表](ticket-proposal.md#决策的主要责任)：Q1–Q53 全部映射。
- [验收责任表](ticket-proposal.md#规格验收组责任)：AC-01–AC-28 的复杂组按子合同落地，24/25 全量重放，27 核验原件。
- [直接边及其原因](ticket-proposal.md#直接阻塞边及原因)：38 条直接边，无循环和冗余传递依赖。
- 父规格发布前 SHA-256：5255BA862F0B39176EDAB7552B94EFC9D3505017AADE3C059DC9B48BEC869105；本次发布保持其原字节和状态。

## Context

[正式规格](spec.md) · [已确认拆票方案](ticket-proposal.md) · [M1 已完成基线](../qaplet-m1/final-review-validation.md) · [开发入口](../../docs/development.md) · [证据合同](../../docs/development/evidence.md) · [独立审查材料](../../docs/agents/review.md)

## Comments

2026-11-03：按用户确认发布 27 份独立任务，均为 ready-for-agent/not_started。仅发布任务与索引，没有开始产品实现、关闭父规格或继承旧 M1 成绩为 M2 通过。

2026-11-03：开始实施。先以 `5685bb6` 保存已批准规格、任务及现有文档修改；01–03 已分配独立 worktree，04/05 在并发容量释放后领取。尚未签发任何 M2 验收结论。

2026-11-03：01–03 的实现及本地闭环已经独立机械合并，当前均等待统一 Cross/Full；04/05 已开始。父规格只同步执行状态及入口，已确认合同不变。

