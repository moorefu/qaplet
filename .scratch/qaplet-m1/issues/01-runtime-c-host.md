# 01: 纯 C Host 的最小 Runtime 闭环

**What to build:** Ubuntu 构建最小静态 SDK 与纯 C Host APE；经公开接口创建、求值标量、取结果、close/destroy，并纳入现有制品分发的基础 smoke。

**Blocked by:** None (can start immediately)。

**Status:** resolved

**Execution state:** completed；2026-11-02 实现并实际验收；分支 codex/m1-01，产品及限额回滚修正 3c0d682，头文件跨 checkout 字节策略 d70162c。

**Execution prerequisites:** Ubuntu 构建环境与固定输入可用，且已合入本票所列阻塞任务；使用仓库约定的共享输入/缓存与现有构建、证据入口。

**Spec stories:** 1、2、3、4、5、47、48、49、50、52。

## Acceptance criteria

- [x] 公开 C 消费者无需私有引擎头文件即可构建运行，实例只有一个主 Context，创建线程固定为 owner。
- [x] create/eval/get_result/POD 快照/close/destroy 具有实际版本前缀、基本错误和状态合同；普通同步求值保留状态，新实例为新环境。
- [x] 创建失败返回空输出；非 owner 受限操作拒绝；重复 close 安全，destroy 只接受 CLOSED。
- [x] 从起点使用既定有限堆/栈及受计量存储基础，保留原 M0 制品和证据原范围。
- [x] 真实 Ubuntu C Host 执行结果及新增 APE 身份可通过现有分发/宿主 smoke 消费；不标记尚未实现能力。

## Validation and evidence

通过本票实际提供的公开 SDK/CLI 与纯 C Host 外部边界验证，观察公开状态、稳定错误 code、JS/native 副作用、复制数据、退出码和资源关闭事实。复用已有真实 producer、供应制品、宿主 consumer 与原始证据模式；每票执行自身验收及适用的 Cross/Full 门禁，平台收尾票不替代此前回归。

证据记录当前实现/输入/配方/SDK/APE 身份、实际命令、期望与实际结果、stdout/stderr 原始日志、退出码、时限、宿主与执行方式。外部 supervisor 超时表示用例未完成，不能作为产品正确停止/超时的证明；缺资源、失败、skip 与未完成保持可见，不继承旧 M0 成绩。

## Scope boundary

本票交付一个可单独演示和验收的增量，只声明已经实现并实际验证的范围；未实现能力不以成功 stub 公开。完整 M1 合同以实施规格为准；M2 的 OS waker、I/O 取消、迟到 completion 和完整调度，以及独立发布门禁，继续保持原分界。

## Context

[正式 M1 规格](../spec.md) · [已确认设计](../design.md) · [任务索引与依赖](../README.md) · [开发入口](../../../docs/development.md) · [证据合同](../../../docs/development/evidence.md) · [独立审查材料](../../../docs/agents/review.md)

## Comments

2026-11-02：已确认 worktree 基于 integration；测试沿用规格 Q32 已批准的公开 SDK / 纯 C Host / 现有制品消费者边界。按 TDD 逐个纵向切片执行，未实现能力不进入能力声明。

2026-11-02：用户以 `ok` 确认 16 票拆分及阻塞依赖，按 to-tickets 发布为 ready-for-agent。本票尚未执行；满足阻塞依赖后才可领取。

2026-11-02：Ubuntu 产出公开头文件、双 CPU 静态 SDK 与纯 C Host APE，八项真实用例在 Ubuntu x64、Windows x64、ARM64 完整系统 4K/64K guest 均通过，每处含 1,000 次生命周期。ARM64 创建失败回滚的初次 segfault 已最小化、修正并重跑；旧失败制品与日志保留。SDK/APE 身份、执行命令、仓库检查和适用限制见下方 Answer；未将后续 M1 能力或完整 Cross/Full 标记为通过。

## Answer

已交付并验收本票全部条件。见[实施与验证记录](../01-runtime-validation.md)及[SDK 开发入口](../../../docs/development/sdk.md)。最终 producer / Ubuntu consumer 为 worktree `out/m1-01/verified/{build,run}.json`；同字节 Windows 为 `windows-user-run.json`；主 checkout ARM64 guest 为 `out/m1/01-guest-verified/run.json` 和 `out/m1/01-guest-4k-verified/run.json`。M0 pinned 输入及原证据范围未改变；后续 M1 合同仍由各阻塞票负责。

