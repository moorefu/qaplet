# 03: Promise 与 jobs 的显式分步执行

**What to build:** C Host 通过 eval/pump 将 Promise 从 PENDING 推进到 DONE，区分未推进与完成，正确结算 rejection 并在普通异常后复用实例。

**Blocked by:** [01：纯 C Host 的最小 Runtime 闭环](01-runtime-c-host.md)

**Status:** claimed

**Execution state:** implementing；2026-11-02 在 `codex/m1-03` 独立 worktree 领取，基于已验任务 01 集成提交 `e07c227`，按公开 SDK / 纯 C Host 边界执行 TDD。

**Execution prerequisites:** Ubuntu 构建环境与固定输入可用，且已合入本票所列阻塞任务；使用仓库约定的共享输入/缓存与现有构建、证据入口。

**Spec stories:** 19、20、21、22、25、26、39。

## Acceptance criteria

- [ ] 只自动等待真实 Promise，同实例仅一个活动执行，PENDING 时新 eval/run 拒绝为 BUSY。
- [ ] pump jobs 默认配额 256，配额提前返回不忙等；无就绪工作仍未完成时返回 PENDING 和未推进。
- [ ] 完整检查点才判定未处理 rejection，检查点内补处理器可消除记录，入口失败不重复报告。
- [ ] 同步抛错/job 失败的剩余可运行 jobs 得到结算，普通失败完成后可再次执行并保留状态修改。
- [ ] 纯 C Host 用实际 Promise 链演示跨多次 pump 完成与无进展，关闭 pending 结构不泄漏执行引用。

## Validation and evidence

通过本票实际提供的公开 SDK/CLI 与纯 C Host 外部边界验证，观察公开状态、稳定错误 code、JS/native 副作用、复制数据、退出码和资源关闭事实。复用已有真实 producer、供应制品、宿主 consumer 与原始证据模式；每票执行自身验收及适用的 Cross/Full 门禁，平台收尾票不替代此前回归。

证据记录当前实现/输入/配方/SDK/APE 身份、实际命令、期望与实际结果、stdout/stderr 原始日志、退出码、时限、宿主与执行方式。外部 supervisor 超时表示用例未完成，不能作为产品正确停止/超时的证明；缺资源、失败、skip 与未完成保持可见，不继承旧 M0 成绩。

## Scope boundary

本票交付一个可单独演示和验收的增量，只声明已经实现并实际验证的范围；未实现能力不以成功 stub 公开。完整 M1 合同以实施规格为准；M2 的 OS waker、I/O 取消、迟到 completion 和完整调度，以及独立发布门禁，继续保持原分界。

## Context

[正式 M1 规格](../spec.md) · [已确认设计](../design.md) · [任务索引与依赖](../README.md) · [开发入口](../../../docs/development.md) · [证据合同](../../../docs/development/evidence.md) · [独立审查材料](../../../docs/agents/review.md)

## Comments

2026-11-02：用户以 `ok` 确认 16 票拆分及阻塞依赖，按 to-tickets 发布为 ready-for-agent。本票尚未执行；满足阻塞依赖后才可领取。

