# 03: Pipe 与借用标准流的可取消接入探针

**What to build:** 以真实 pipe、文件、标准流及别名 fixture 验证有界读写、取消、关闭和共享状态影响，为 stdio backend 提供可复验接入矩阵。

**Blocked by:** None (can start immediately)。

**Status:** resolved

**Execution state:** completed；2026-11-03 五原生及实际 64K 的当前探针原件已独立重放，保留 Windows 写入能力限制；见[实施记录](../implementation.md)。

**Execution prerequisites:** 已满足本票直接 blocker；Ubuntu 构建环境及固定输入可用，复用仓库约定的输入、缓存和证据方式。ready-for-agent 表示需求已就绪，不绕过 blocker。

**Spec stories:** 59、61、62、63、67、68、73、100、101、103。

**Confirmed decisions:** Q8、Q14、Q27、Q28、Q41、Q42、Q47、Q49。

**Spec acceptance groups:** AC-17、AC-19、AC-26。这些是本票涉及的父规格验收组；复杂组由多票分别完成子合同，再由完整矩阵重放，不把局部成功冒充整组完成。

## Acceptance criteria

- [x] 覆盖真实 stdin 的 pipe、文件、无效句柄及适用终端类型，区分借用、拥有和描述符别名。
- [x] 验证任意字节/NUL、短读写、暂不可用、EOF/EPIPE、部分写和 readiness；结果保留原始字节。
- [x] 实际观察 nonblocking/模式修改是否影响别名，不假设 dup 自动隔离共享状态。
- [x] 验证等待中取消、端点关闭及迟到完成的原生退出边界；不能安全接入的方式有实测原因。
- [x] 探针经 Ubuntu 构建与供应、适用 Cross/Full 实跑和原始报告消费，不能用旧 M0 pipe 单字节成绩替代。

## Blocking details

无实施票前置，可立即领取；仍须遵守已完成 M1 基线与开发约定。

## Validation and evidence

本票的纵向边界是 Ubuntu producer、供应原生探针、真实 OS 操作与结构化 consumer 报告。先实测再选择 backend，保留成功、失败、能力限制和实际资源清理；探针结果不自动成为产品支持声明。

使用明确接受结果和可控 producer/owner 交接复现竞态，不用随机 sleep 猜顺序。受控系统故障夹具与正常产品分别记录输入及配方；外部 supervisor 超时表示未完成，不能证明产品已成功停止或清理。

每票执行自身正负验收与适用 Cross/Full；24–27 的最终矩阵和长测不替代此前平台检查。全部产品与 SDK 构建在 Ubuntu，其他宿主消费供应制品；同一 APE 字节跨宿主验证，目标 SDK archive/slice 分别标识。

证据绑定当前产品源码、配方、SDK/APE/loader/供应原 ZIP、实际命令与日志、OS/kernel/CPU/页大小/执行方式、实际次数/时长及清理结果。缺项、fail、必需 skip/not_run 或中断保持可见，不能外推旧 M0/M1 成绩。

## Scope boundary

本票交付一个可独立演示和验收的增量，只声明已经完成并实际验证的能力；不提供成功 stub、隐式 fallback 或未经验证的 OS ABI。

资源从首次接入起就受父规格有限默认值、容量预留和安全 stop/close/quiesce/destroy 约束。19/20 负责跨资源联立与组合故障验收，不允许此前延后硬限额或首次关闭安全。完整默认表、线程/重入、结果保留和原生 lifetime 以父规格为准。

保留 M1 旧创建与 ABI 合同；完整 Node 兼容、网络产品 API、整树清理、24 小时发布 soak 和独立发布镜像门禁继续保持原阶段边界。当前任务发布不等于实现或通过。

## Context

[父规格与全部合同](../spec.md) · [任务索引与依赖](../README.md) · [已确认拆分及覆盖映射](../ticket-proposal.md) · [开发入口](../../../docs/development.md) · [SDK 入口](../../../docs/development/sdk.md) · [证据合同](../../../docs/development/evidence.md) · [独立审查材料](../../../docs/agents/review.md)

## Comments


2026-11-03：用户明确回复“按 27 张发布”，确认 27 张纵向任务及 38 条直接阻塞边；本票按已确认提案独立发布为 ready-for-agent，尚未领取或实施。父规格保持不变。

2026-11-03：实现与本地验证见 [03 探针记录](../03-stdio-validation.md)。独立 Ubuntu producer 供应同一 APE，Linux x64、Windows x64 和实际 ARM64 64K 完整系统 guest 各 19/19 pass；两平台供应 CLI 合同各 4/4 pass。Windows NOWAIT 大写入 TRUE/0 是明确观测，原始字节及已写量/回收通过，能力约束独列，不冒充短写支持。保留编译、受限上下文超时及 64K fixture 页假设的红记录。普通 Ubuntu check 通过。首四项记录本票实现及已测宿主范围；最后一项所需当前 Cross/Full 及其他必需原生宿主尚待集成验证，状态保持 claimed，不放行依赖或宣称 M2 产品通过。


2026-11-03（集成验收）：`5fb999f` / run `41599033541` 的五原生各 19 项及实际 64K 19 项，以原始 stdout、Windows 独立 terminal.ndjson、payload/serial、供应摘要及退出/清理事实重算通过；当前 M0 Full 235 项回归也已重放。原件及命令见主 `out/qaplet-m2/initial-cross/verify/VERIFICATION.md`。Windows NOWAIT 大块写入 TRUE/0 的限制继续保留。02 两台 macOS 的 waker 失败单独保留，不将本票完成外推为整个 Full 或父 M2 完成。
