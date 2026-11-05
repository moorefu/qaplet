# 19: 跨资源预算、POD 快照与终止首因

**What to build:** 宿主用小公开额度混合提交不同资源，能看到真实拒绝、占用和峰值，并区分操作错误、接受后 wake 故障和永久终止。

**Blocked by:** [09 · 宿主自有事件循环的原生通知接入](09-host-notify-loop-integration.md)、[16 · JS Process 与 stdio 的完整异步闭环](16-js-process-stdio-liveness.md)、[18 · TTY 租约、resize 与 Console UTF-8](18-tty-console-leases.md)。

**Status:** claimed

**Execution state:** implementing_local

**Execution prerequisites:** 09已resolved，16本地完成并独立合并`015cdb9f`（接收`71a5db50`），18本地完成并独立合并`3e85a272`（接收`64624ddd`）；全部当前开发前置已满足，已领取，与20并行。正式Cross/Full欠项不替代也不取消，验证范围见[本地开发方式](../local-development.md#2026-11-05剩余开发前置与验证范围)。

**Spec stories:** 2、3、5、20、31、32、42、47、74、75、76、77、78、79、80、81、82、83、93、106、107、110。

**Confirmed decisions:** Q4、Q10、Q19、Q24、Q25、Q31、Q34、Q35、Q36、Q37、Q46、Q48、Q51、Q52。

**Spec acceptance groups:** AC-01、AC-10、AC-11、AC-21、AC-22。这些是本票涉及的父规格验收组；复杂组由多票分别完成子合同，再由完整矩阵重放，不把局部成功冒充整组完成。

## Acceptance criteria

- [ ] 完整默认表及有效配置可查询；timer、注册、句柄、进程、令牌、处理器和共享操作额度从实际资源生命周期计量。
- [ ] 8 MiB 操作数据包含在 16 MiB 原生总额，元数据/环境也计费；线程请求栈各 1 MiB 单列，不能冒充 RSS 硬上限。
- [ ] 逻辑取消、C 保留结果与物理退出共同决定容量，提交前预留/拒绝无部分接受，不靠私有分配次数验收。
- [ ] 实际 SDK/引擎分配拒绝、时钟/等待/waker 控制故障永久终止；已发布接受后不变成假拒绝，先记录原因不被后错覆盖。
- [ ] POD 当前量/峰值/任务深度/预留/I/O/wake/cancel/drop 计数及安全有界诊断齐全，计数饱和且不输出内部指针或完整敏感数据。

## Blocking details

- [09 · 宿主自有事件循环的原生通知接入](09-host-notify-loop-integration.md)：跨资源观测/控制故障须覆盖已实现的宿主通知路径。
- [16 · JS Process 与 stdio 的完整异步闭环](16-js-process-stdio-liveness.md)：预算/错误/快照覆盖 work、进程和全部 stdio 使用者。
- [18 · TTY 租约、resize 与 Console UTF-8](18-tty-console-leases.md)：预算和故障核对还须包括终端租约/Console。

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

2026-11-05：18独立接收后由root实际领取；分支`codex/m2-19-aggregate`、worktree
`.cache/worktrees/m2-19-aggregate`从当前实际integration起步，fresh实现者TDD。
旧普通IO诊断拒绝样式仅为待公开证伪风险，先核公开POD/canary再决定修复；不改原AC。
完成本地验收及独立合入后才标local_complete，正式Cross/Full仍单列待验。

