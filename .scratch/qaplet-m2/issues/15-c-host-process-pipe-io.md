# 15: 子进程三路有界字节 I/O 与 EOF

**What to build:** C Host 使用默认 pipe stdio 并发读 stdout/stderr、写 stdin，取得短读写、背压、EOF 和退出后的剩余输出。

**Blocked by:** [03 · Pipe 与借用标准流的可取消接入探针](03-pipe-stdio-cancel-os-probes.md)、[11 · 任务取消竞争、结果 release 与容量保留](11-work-cancel-result-retention.md)、[13 · 纯 C Host 的显式 spawn 与退出观察](13-c-host-process-spawn-exit.md)。

**Status:** claimed

**Execution state:** in_progress；本地Linux/Windows阶段，完整Cross deferred。

**Execution prerequisites:** 03已完成、11/13的Local development已完成；按用户[本地开发指示](../local-development.md)放行本地实施，保留未完成的跨平台验收。

**Spec stories:** 33、34、39、40、42、54、55、57、58、59、60、61、62、63、73、79、81、83。

**Confirmed decisions:** Q8、Q10、Q19、Q20、Q24、Q27、Q28、Q29、Q35、Q36、Q41、Q47。

**Spec acceptance groups:** AC-10、AC-11、AC-16、AC-17、AC-18、AC-21、AC-22、AC-24、AC-26。这些是本票涉及的父规格验收组；复杂组由多票分别完成子合同，再由完整矩阵重放，不把局部成功冒充整组完成。

## Acceptance criteria

- [ ] pipe/inherit/ignore 及默认三路 pipe 全部可用；读写操作复用可靠完成与共享操作/字节额度，不挤占业务事件。
- [ ] 每方向一个未完成操作，额外请求 BUSY；读当前可用字节并区分暂不可用/EOF，保留任意字节和 NUL。
- [ ] 写入先复制，短写和失败都保留实际已写量；正常结束写入与立即取消/关闭分开。
- [ ] stdout/stderr 独立拉取保序、慢消费者背压和每轮 I/O 额度有效；退出/reap 与 EOF 分开，未读数据仍可消费。
- [ ] 真实 fd/HANDLE 复用、取消中 close、迟到完成和继承泄漏无误投/UAF；受管理 stdio 未结束时 ref 责任仍在。

## Blocking details

- [03 · Pipe 与借用标准流的可取消接入探针](03-pipe-stdio-cancel-os-probes.md)：pipe/标准流 backend 的取消和共享状态边界必须实测。
- [11 · 任务取消竞争、结果 release 与容量保留](11-work-cancel-result-retention.md)：复用有界异步操作、结果记录与取消/release 协议。
- [13 · 纯 C Host 的显式 spawn 与退出观察](13-c-host-process-spawn-exit.md)：使用受管理子进程的原生 stdio 端点及继承合同。

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

2026-11-03：用户明确回复“按 27 张发布”，确认 27 张纵向任务及 38 条直接阻塞边；本票按已确认提案独立发布为 ready-for-agent，尚未领取或实施。父规格保持不变。

