# 06: 等待中的 Runtime 接收事件与停止

**What to build:** C Host 使用 run_once/run 等待外部 Qaplet Event；生产者投递、释放保活令牌或请求 stop 后 owner 可靠醒来，旧 pump 继续不主动等待。

**Blocked by:** [01 · M2 Runtime 的可选创建与兼容生命周期](01-runtime-opt-in-compatibility.md)、[02 · 唤醒与 Socket 等待的真实 OS 探针](02-waker-reactor-os-probes.md)。

**Status:** claimed

**Execution state:** in_progress

**Execution prerequisites:** 已满足本票直接 blocker；Ubuntu 构建环境及固定输入可用，复用仓库约定的输入、缓存和证据方式。ready-for-agent 表示需求已就绪，不绕过 blocker。

**Spec stories:** 6、7、8、9、10、11、12、26、27、28、30、32、84、85、109。

**Confirmed decisions:** Q2、Q3、Q5、Q9、Q15、Q16、Q24、Q34、Q40、Q46、Q50、Q51。

**Spec acceptance groups:** AC-02、AC-03、AC-04、AC-07、AC-09、AC-10、AC-23、AC-26。这些是本票涉及的父规格验收组；复杂组由多票分别完成子合同，再由完整矩阵重放，不把局部成功冒充整组完成。

## Acceptance criteria

- [ ] run_once 的 0/有限等待、进度、执行 phase 和返回原因分别表达；多次 wake 不重置累计等待预算。
- [ ] owner 创建保活令牌，任意线程释放；入口 DONE、空闲、PENDING 和 UNSETTLED_ENTRY 的公开行为可区分。
- [ ] 业务事件维持复制、NO_HANDLER、超大拒绝、QUEUE_FULL 和 FIFO；满队列仍接受 stop。
- [ ] 检查到入睡的各确定顺序无丢唤醒；不隐藏 JS 线程、不固定短轮询，stop 接受端只发布与唤醒。
- [ ] 整个驱动范围保护危险重入，原有执行期限跨等待累计；原生控制和空实例清理保持安全。

## Blocking details

- [01 · M2 Runtime 的可选创建与兼容生命周期](01-runtime-opt-in-compatibility.md)：使用可选 M2 实例、整段驱动保护及原生保留边界。
- [02 · 唤醒与 Socket 等待的真实 OS 探针](02-waker-reactor-os-probes.md)：阻塞等待与唤醒采用真实验证的 OS backend。

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

