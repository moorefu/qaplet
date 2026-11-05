# 16: JS Process 与 stdio 的完整异步闭环

**What to build:** JS 经 qaplet:process 启动、读写、取消并观察直接子进程，分别等待退出与 stdio 结束，在自然退出中保留剩余数据。

**Blocked by:** [12 · JS 命名工作、Promise 与取消闭环](12-js-named-work-promises.md)、[14 · 直接子进程取消、回收与关闭竞争](14-process-cancel-reap.md)、[15 · 子进程三路有界字节 I/O 与 EOF](15-c-host-process-pipe-io.md)。

**Status:** claimed

**Execution state:** local_complete / cross_deferred；独立分支 `codex/m2-16-js-process`。

**Local development:** complete；公开 JS/C、封存包外 SDK 和受控系统故障在本地 Linux/Windows 同字节验收完成。详细范围、失败历史与清理事实见[验收记录](../16-js-process-validation.md)；原件与最终收据位于该 worktree 的 `out/m2-16-js-process/HANDOFF.md` 和 `handoff.json`。其他必需原生目标、当前实际 64K、CI/Cross/Full、最终混合资源长测与发布就绪保持待验，不改变 claimed 状态或下列正式验收合同。

**Execution prerequisites:** 12、14、15本地完成并已独立合并（`3d03f855`、`25d3eb12`、`65de84e2`），实际集成祖先已核验；可与17并行。验证范围及正式欠项见[本地开发方式](../local-development.md#2026-11-05剩余开发前置与验证范围)。

**Spec stories:** 13、14、15、17、18、19、20、51、52、53、54、56、57、58、59、60、61、62、63、64、65、81、83。

**Confirmed decisions:** Q5、Q6、Q7、Q12、Q16、Q17、Q19、Q21、Q22、Q24、Q28、Q29、Q30、Q32、Q34、Q40、Q41、Q47、Q52。

**Spec acceptance groups:** AC-04、AC-05、AC-14、AC-15、AC-16、AC-17、AC-18、AC-22、AC-23、AC-24。这些是本票涉及的父规格验收组；复杂组由多票分别完成子合同，再由完整矩阵重放，不把局部成功冒充整组完成。

## Acceptance criteria

- [ ] JS 与 C 共用显式路径、参数/环境快照、三种 stdio 模式及错误，不增加 Node child_process/stream 兼容承诺。
- [ ] 任务/进程对象以 owner Promise 交付；退出/reap、EOF、输出读取和受管理 stdio 关闭分别可观察。
- [ ] ref 进程保持到其直接回收与受管理 stdio 结束；unref/GC 不等于取消，后代持管道只影响 EOF，不扩大管理范围。
- [ ] 并发双通道、部分写、BUSY、背压、取消/正常完成竞争和稳定错误从 JS 可复验。
- [ ] 完成、timer、事件与 jobs 共用轮转及账本；stop/close 后不再用户 JS，取消和输出记录保持到安全退出。

## Blocking details

- [12 · JS 命名工作、Promise 与取消闭环](12-js-named-work-promises.md)：复用 JS 异步任务/Promise 与 owner 完成交接。
- [14 · 直接子进程取消、回收与关闭竞争](14-process-cancel-reap.md)：JS 取消需要已实现的直接 child 终止/reap。
- [15 · 子进程三路有界字节 I/O 与 EOF](15-c-host-process-pipe-io.md)：JS stdio 依赖完整的原生字节 I/O/EOF/背压。

## Validation and evidence

本票通过实际提供的公开 SDK、JS/最小 CLI 或包外纯 C Host 边界验收，观察公开状态、稳定错误 code、复制数据、JS/native 副作用、输出/退出和资源回收事实。不得以私有布局、内部调用次数或完整异常字符串代替合同。

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

2026-11-05：本地完成；融合已独立合入的17及当时实际 integration `a5067a9` 后交接。当前最终供应产品 `13282ae`、全部原始日志与受控负向保留；尚未由本实现者合入根分支或 push，正式验收欠项不清零。

2026-11-05：按用户指示领取本地实现；保留全部产品、安全和正式验收合同。本地验收并独立合并后放行开发，正式跨平台验收仍保留最终收口。

2026-11-03：用户明确回复“按 27 张发布”，确认 27 张纵向任务及 38 条直接阻塞边；本票按已确认提案独立发布为 ready-for-agent，尚未领取或实施。父规格保持不变。

2026-11-05（独立本地接收）：source `353ee949` 经独立原件核验后实际no-ff合入
`015cdb9f`，合并树同源，17既有产品与18领取记录保留。86 recipe/raw LF/Git、82 ZIP、
双archive各19、normal208/207+既有optional1、package94/94、fault24/25、1722 raw logs、
原本票1000+100循环及devcheck/bootstrap均复核，合后Ubuntu preflight通过。
lost-reap负向保留未知原生责任，不计为清理成功。详[独立接收记录](../16-js-process-validation.md#独立本地接收--2026-11-05)
与主`out/qaplet-m2/merge-16-js-process/HANDOFF.md`。claimed及local_complete/cross_deferred、
原Blocked-by和五条正式AC不变；19/20仍需18独立合入，其他平台/64K/Cross/Full与发布继续待验。

