# 18: TTY 租约、resize 与 Console UTF-8

**What to build:** JS/C 在明确所有权下修改终端模式、观察尺寸变化并使用已验证的 Console 文本/VT 子集，退出路径恢复原模式。

**Blocked by:** [05 · 真实终端租约、resize 与 Console 文本探针](05-tty-console-os-probes.md)、[17 · 标准 stdio 的显式安全接入](17-standard-stdio-opt-in.md)。

**Status:** ready-for-agent

**Execution state:** not_started

**Execution prerequisites:** 05已resolved；等待17本地完成并独立合并，当前未满足全部开发前置。验证范围及正式欠项见[本地开发方式](../local-development.md#2026-11-05剩余开发前置与验证范围)。

**Spec stories:** 13、14、15、66、67、68、69、70、71、72、73、77、86、87。

**Confirmed decisions:** Q7、Q13、Q19、Q23、Q26、Q27、Q35、Q41、Q42、Q45、Q47。

**Spec acceptance groups:** AC-04、AC-11、AC-17、AC-19、AC-20、AC-21、AC-24、AC-26。这些是本票涉及的父规格验收组；复杂组由多票分别完成子合同，再由完整矩阵重放，不把局部成功冒充整组完成。

## Acceptance criteria

- [ ] 显式独占模式租约保存原状态并拒绝冲突，借用/拥有端点责任明确，宿主绕过 SDK 的修改由宿主协调。
- [ ] 正常释放、错误、stop 原生清理和 close 幂等恢复且读回核对，资源句柄正确关闭；强杀限制如实记录。
- [ ] 尺寸查询和 resize 可合并为最新值，不因通知数产生业务计数或无限积压。
- [ ] Console UTF-8/VT 子集、Unicode/分块/非法数据与原始二进制 pipe 分开验收，不替换解码掩盖错误。
- [ ] 终端 I/O、租约和取消受 generation、容量与 native quiescent 屏障约束；不公开完整 PTY 创建或 readline/keypress。

## Blocking details

- [05 · 真实终端租约、resize 与 Console 文本探针](05-tty-console-os-probes.md)：租约和 Console 子集只能基于真实终端证据。
- [17 · 标准 stdio 的显式安全接入](17-standard-stdio-opt-in.md)：需要标准端点的类型识别、所有权与安全 I/O 接入。

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

