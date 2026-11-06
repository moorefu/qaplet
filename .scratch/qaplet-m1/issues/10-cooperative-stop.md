# 10: 饱和与运行中的协作式停止

**What to build:** 工作线程在队列满、JS 循环或入口 PENDING 时仍可 request_stop；验证拒收、已领取/未领取边界、幂等与首因。

**Blocked by:** [08：跨线程 POD 到 JS 的有界事件桥](08-bounded-pod-events.md)

**Status:** resolved

**Execution state:** complete；2026-11-02，codex/m1-10 已合入 integration 9174bfad；组合源码 dfd0c7a 的 Ubuntu/Windows/ARM64 64K 供应制品验收完成，范围见本票验收记录。

**Execution prerequisites:** Ubuntu 构建环境与固定输入可用，且已合入本票所列阻塞任务；使用仓库约定的共享输入/缓存与现有构建、证据入口。

**Spec stories:** 41、42、43、44、45。

## Acceptance criteria

- [x] request_stop 不消耗队列槽，成功代表接受，进入 STOP_REQUESTED 后拒收普通工作。
- [x] 统一接受顺序决定 stop/入队/领取；已领取事件协作退出，未领取事件停止执行，不自动重试。
- [x] 同步 CPU 循环、Promise/async 流程、无就绪工作的 PENDING 和回调阶段停止均由 owner 明确确认。
- [x] 重复 stop 在停止/FAILED/CLOSED 状态无副作用成功；期限与 stop 并发保留首次记录，不依赖异常文字。
- [x] 剩余事件/jobs 在关闭时释放，不额外执行用户 finally/清理 JS；每个竞态由可控交接和真实结果证明。

## Validation and evidence

通过本票实际提供的公开 SDK/CLI 与纯 C Host 外部边界验证，观察公开状态、稳定错误 code、JS/native 副作用、复制数据、退出码和资源关闭事实。复用已有真实 producer、供应制品、宿主 consumer 与原始证据模式；每票执行自身验收及适用的 Cross/Full 门禁，平台收尾票不替代此前回归。

证据记录当前实现/输入/配方/SDK/APE 身份、实际命令、期望与实际结果、stdout/stderr 原始日志、退出码、时限、宿主与执行方式。外部 supervisor 超时表示用例未完成，不能作为产品正确停止/超时的证明；缺资源、失败、skip 与未完成保持可见，不继承旧 M0 成绩。

## Scope boundary

本票交付一个可单独演示和验收的增量，只声明已经实现并实际验证的范围；未实现能力不以成功 stub 公开。完整 M1 合同以实施规格为准；M2 的 OS waker、I/O 取消、迟到 completion 和完整调度，以及独立发布门禁，继续保持原分界。

## Context

[正式 M1 规格](../spec.md) · [已确认设计](../design.md) · [任务索引与依赖](../README.md) · [开发入口](../../../docs/development.md) · [证据合同](../../../docs/development/evidence.md) · [独立审查材料](../../../docs/agents/review.md)

## Comments

2026-11-02：用户以 `ok` 确认 16 票拆分及阻塞依赖，按 to-tickets 发布为 ready-for-agent。本票尚未执行；满足阻塞依赖后才可领取。

## Answer

2026-11-02：公开 any-thread `qaplet_request_stop`，capability bit14 与稳定原因
`QAPLET_EXECUTION_STOPPED=23`。停止、投递与 owner 领取共用已有 Runtime mutex；
饱和队列不阻挡停止，STOP_REQUESTED 立即拒收，owner interrupt/边界或后续 pump
确认 STOPPED/DONE。保留第一次记录的 stop/期限/调度量/实际分配失败，停止后未领取
事件与 jobs 仅在 close 释放，不额外调度用户 finally/清理 JS，不提供恢复或 M2 waker。

已确认 Q32 的 public SDK / supplied static SDK / public-header-only C Host seam 上，
受控 C callback、worker/owner 交接与公开接受/消费/丢弃事实证明领取边界；CPU 循环由
实际 owner CPU 进展确认后才请求 stop，不用随机 sleep 推断。首次 Windows 用例真实失败
保留：合法零 CPU 初值被 fixture 误判为 clock failure。先复现、最小化、展示排名假设和
tagged 诊断，再分离 clock 成功状态与数值；最小例 20/20 与原例 10/10 通过，无 SDK 缺陷
推断，所有原失败和制品仍保留。

组合源提交 `dfd0c7ac76d51f317ad4869e340bdf1d7eaed892` 已实际合入 05 的新 pinned
input f07 与 integration `9174bfad`，重新签发 `out/m1-10/combined-final/build.json`。
源码冻结贯穿匹配消费者：Ubuntu 38/38，normal-user Windows 37 个必需项全通过（另一个
optional symlink fixture 保持 blocked），主工作区 `out/m1/10-guest-combined-final/run.json`
的 ARM64 完整系统 65536 页全 38 用例通过，无 supervisor timeout。
`out/m1-10/combined-check.json` 通过 219 tests（75 个既有 optional skips 单列）与系统 C
bootstrap。精确身份、命令、原始红绿/诊断/供应制品证据、平台事实及未执行范围见
[验收记录](../10-stop-validation.md)。不签发完整 M1 native Cross/Full、完整 11 生命周期或
release-ready；不等待尚未合入的 09/12。

