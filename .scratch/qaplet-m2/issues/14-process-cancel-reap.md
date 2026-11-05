# 14: 直接子进程取消、回收与关闭竞争

**What to build:** C Host 明确取消直接子进程并观察实际终止/reap，正常退出、重复取消、stop 和 close 竞争均保留唯一责任。

**Blocked by:** [13 · 纯 C Host 的显式 spawn 与退出观察](13-c-host-process-spawn-exit.md)。

**Status:** claimed

**Execution state:** local_complete_pending_merge；本地Linux/Windows实施与验证完成，待独立合并；完整Cross deferred。

**Local implementation/validation:** complete；正式 Local development 放行由独立 merger 合入后登记。

**Execution prerequisites:** 13的Local development已完成；按用户[本地开发指示](../local-development.md)放行本地实施，保留未完成的跨平台验收。

**Spec stories:** 33、34、56、57、58、64、65、84、85、86、88、89。

**Confirmed decisions:** Q12、Q19、Q21、Q26、Q27、Q29、Q30、Q35、Q45、Q50。

**Spec acceptance groups:** AC-11、AC-14、AC-18、AC-23、AC-24。这些是本票涉及的父规格验收组；复杂组由多票分别完成子合同，再由完整矩阵重放，不把局部成功冒充整组完成。

## Acceptance criteria

以下勾选限定于已执行的本地 Linux/Windows；其他原生平台、完整 Cross/Full 与父规格组合长测仍未验收。

- [x] 取消使用已验证强制终止并回收，保留实际 exit/signal/error，不承诺温和信号、宽限期或整树清理。
- [x] 取消与正常退出、创建完成及重复回收有确定顺序，不把 logical CANCELLED 当成已 reap。
- [x] owner 确认 stop/FAILED 即启动终止，不等待 close 才开始；未回收期间 native pending/destroy BUSY 可见。
- [x] 跨 Runtime/代次、迟到退出和复用原生句柄不误投新进程，必要记录和容量保留到真实回收。实际 HANDLE 复用在 Windows 的受控 OS 交接执行；POSIX 另验证失去 PID 身份后不再发送信号。
- [x] 正常、错误、STOPPED/FAILED、PENDING close 及 quiesce 重试不运行用户 JS 或提前释放原生数据。

## Blocking details

- [13 · 纯 C Host 的显式 spawn 与退出观察](13-c-host-process-spawn-exit.md)：必须已有受管理的直接子进程和退出记录。

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

2026-11-05：本地公开取消、终止即回收、确定性 helper/TLS 物理保留、真实 native error/wake 故障与 Windows HANDLE 复用均通过。正常供应 Linux 172pass、Windows 171pass+既有可选 symlink 限制1；包外两端各58pass；独立故障 Linux12/Windows9pass；devcheck380=180pass+200条件skip，C bootstrap通过。详见[本票验证记录](../14-process-cancel-validation.md)。保持 claimed，不提前关闭 full ticket/父M2，不 push/触发 CI；待独立 merger 合入后登记本地依赖放行。

2026-11-05（融合12）：已融合实际integration 3d03f85，保留JS Work7项及Process cancel/terminal/包外路径。15真实IO gate发现的Windows child已exit/helper未pub竞争由14唯一提交5724f4e修复；旧供应cancel/close两序实际red，新组合正式fault均green，活child真实native5仍保留。新normal Linux179pass/Win178pass+optional1，package两端65pass，fault Linux12/Win10pass，相关SDKNative三项pass/0skip；不重复380。旧sourceout冻结，新原件独占主out/qaplet-m2/14-fusion-js12，见[融合验证](../14-fusion-js12-validation.md)。继续待独立merger放行本地开发，不改变claimed/fullCross deferred。

