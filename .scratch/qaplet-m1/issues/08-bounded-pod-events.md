# 08: 跨线程 POD 到 JS 的有界事件桥

**What to build:** 工作线程投递复制事件，owner pump 经 qaplet:events 接收；演示 FIFO、数据隔离、未注册、超大和满队列行为。

**Blocked by:** [06：Qaplet Host Module 的同步调用闭环](06-host-module-bridge.md)、[07：跨 pump 的执行期限与总调度量](07-execution-budgets.md)

**Status:** resolved

**Execution state:** completed；公开 SDK / supplied C Host 已在 Ubuntu、normal-user Windows x64 与 ARM64 64K 完整系统逐一实跑，29/29 用例通过。

**Execution prerequisites:** Ubuntu 构建环境与固定输入可用，且已合入本票所列阻塞任务；使用仓库约定的共享输入/缓存与现有构建、证据入口。

**Spec stories:** 27、28、29、30、31、32、33、34。

## Acceptance criteria

- [x] 固定槽默认 256、载荷 1 KiB，type 为 uint32/Number、token 为 uint64/BigInt 且 0 合法，不保存指针或 JS 引用。
- [x] onEvent 一次注册，未注册 NO_HANDLER；注册窗口独立于已锁定的 C 宿主模块声明。
- [x] 成功完整复制；超大/QUEUE_FULL 完全拒绝，不等待、不覆盖、不重试，多生产者按接受顺序 FIFO。
- [x] JS 收到独立 Uint8Array；修改宿主原缓冲区不影响数据，事件返回 Promise 不等待且 rejection 遵循检查点。
- [x] 有入口时合用账本，空闲事件及其 jobs 使用跨 pump 独立账本；默认每 pump 最多 32 事件，jobs 未结算不领取下一事件。

## Validation and evidence

通过本票实际提供的公开 SDK/CLI 与纯 C Host 外部边界验证，观察公开状态、稳定错误 code、JS/native 副作用、复制数据、退出码和资源关闭事实。复用已有真实 producer、供应制品、宿主 consumer 与原始证据模式；每票执行自身验收及适用的 Cross/Full 门禁，平台收尾票不替代此前回归。

证据记录当前实现/输入/配方/SDK/APE 身份、实际命令、期望与实际结果、stdout/stderr 原始日志、退出码、时限、宿主与执行方式。外部 supervisor 超时表示用例未完成，不能作为产品正确停止/超时的证明；缺资源、失败、skip 与未完成保持可见，不继承旧 M0 成绩。

## Scope boundary

本票交付一个可单独演示和验收的增量，只声明已经实现并实际验证的范围；未实现能力不以成功 stub 公开。完整 M1 合同以实施规格为准；M2 的 OS waker、I/O 取消、迟到 completion 和完整调度，以及独立发布门禁，继续保持原分界。

## Context

[正式 M1 规格](../spec.md) · [已确认设计](../design.md) · [任务索引与依赖](../README.md) · [开发入口](../../../docs/development.md) · [证据合同](../../../docs/development/evidence.md) · [独立审查材料](../../../docs/agents/review.md)

## Comments

2026-11-02：implement_08 已确认工作树包含集成基线 33ca5439，读取 implement-spec/TDD、规格 Q23–Q27、ADR 0005–0007 及开发合同；按已确认 Q32 seam 执行公开 SDK / supplied C Host red→green。

2026-11-02：用户以 `ok` 确认 16 票拆分及阻塞依赖，按 to-tickets 发布为 ready-for-agent。本票尚未执行；满足阻塞依赖后才可领取。

## Answer

2026-11-02：在 `codex/m1-08` 实现固定槽复制事件、公开 `qaplet_post_event`、
一次注册 `qaplet:events.onEvent` 及 owner-only pump 交付；源码提交
`f07ed1ad42375a2cefe8f1181108844318e26bca`。默认 256×1024 字节内联槽和 owner scratch
从创建起计入 native storage，builtin 从创建起占用一个模块身份。稳定新增 code 为
NO_HANDLER=20、QUEUE_FULL=21、PAYLOAD_TOO_LARGE=22，能力 bit12；此前枚举值未变。
投递完整复制/原子拒绝，受控多生产者与 owner/Host callback 交接验证 FIFO 与线程边界；
交付独立 Uint8Array 与无损 token（含 0），返回 Promise 不等待而 jobs/rejection 正常结算。
事件使用已有入口账本或跨 pump 空闲事件账本，普通异常后可复用，真正资源失败保持首因；
close 关闭接受并丢弃原始队列，外部 C 计数证明不执行用户清理 JS。

现有 `scripts/sdk.py` producer/catalog 分发并构建公开 SDK、纯 C Host 与实际 ESM/TLA fixture。
`out/m1-08/final/build.json`、`final/run.json` 和 normal-user `final/windows-user-run.json`
通过；主工作区 `out/m1/08-guest-final/run.json` 的实际 ARM64 65536 页完整系统也全 29 用例通过。
源码冻结贯穿三个匹配 SDK/APE consumer；红绿输出与原制品保留。
`out/m1-08/final-check.json` 仓库检查通过（219 tests，75 个既有 scope skips 单独保留）。
最后重新合并当前 integration `33ca5439` 为 already up to date；未等待或换标独立 05 的新输入。
精确身份、实际命令、原始证据和范围见[验收记录](../08-events-validation.md)。
本票不实现 stop、不签发完整 M1 Cross/Full 或 release-ready。

