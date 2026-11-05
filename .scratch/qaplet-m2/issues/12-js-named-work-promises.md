# 12: JS 命名工作、Promise 与取消闭环

**What to build:** JS 经 qaplet:work 调用 C Host 处理器，获得任务 Promise、取消与结果；完成交付加入统一检查点调度。

**Blocked by:** [08 · 重复 timer、immediate 与检查点轮转](08-interval-immediate-fair-checkpoints.md)、[11 · 任务取消竞争、结果 release 与容量保留](11-work-cancel-result-retention.md)。

**Status:** claimed

**Execution state:** local_complete；本地Linux/Windows实现与验收已完成，待独立合并；完整Cross `cross_deferred`。

**Local development:** complete（本分支）；[实现、红绿与供应验收](../12-js-work-validation.md)。正式 `Status: claimed` 保留，不冒称完整跨平台完成。

**Execution prerequisites:** 08已完成、11的Local development已完成；按用户[本地开发指示](../local-development.md)放行本地实施，保留未完成的跨平台验收。

**Spec stories:** 17、18、19、20、35、37、39、40、43、44、45、46、49、50、81、83、91、107。

**Confirmed decisions:** Q6、Q7、Q10、Q11、Q17、Q20、Q21、Q24、Q32、Q34、Q38、Q40、Q52。

**Spec acceptance groups:** AC-04、AC-05、AC-12、AC-13、AC-14、AC-22、AC-23、AC-24。这些是本票涉及的父规格验收组；复杂组由多票分别完成子合同，再由完整矩阵重放，不把局部成功冒充整组完成。

## Acceptance criteria

- [x] 本地Linux/Windows：真实有界文件读取从命名 JS 调用走复制输入、worker、owner Promise 结算，C 与 JS 共用任务行为。
- [x] 本地Linux/Windows：任务对象取消、ref/unref 和结果映射正确；取消/完成竞争不重复结算，丢弃句柄不隐式取消。
- [x] 本地Linux/Windows：owner 交接前保持默认 ref，Promise 结算及 jobs 处理后解除；JS 原生结果可回收但物理未退出数据仍保留。
- [x] 本地Linux/Windows：完成与 timer/immediate/事件按同一轮转和执行账本推进；jobs quota yield 不提前交付其他类别或判 rejection。
- [x] 本地Linux/Windows：普通 native/JS 错误、有限输出、实际分配拒绝、stop 后不再运行 JS 和 userdata 保留期均有公开 C Host/JS 验收。

## Blocking details

- [08 · 重复 timer、immediate 与检查点轮转](08-interval-immediate-fair-checkpoints.md)：JS 异步交付需要完整 jobs 检查点、轮转和账本。
- [11 · 任务取消竞争、结果 release 与容量保留](11-work-cancel-result-retention.md)：JS 任务复用可靠终态、取消、结果和容量保留。

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

2026-11-05：完成本地纵向实现及Linux/Windows验收，供应产品源码 `bb220ce`。
normal 为177pass / 176pass+既有optional symlink1，包外消费者两端各53pass，
controlled fault各2pass。一次devcheck为380项=180pass+200条件skip，真实C bootstrap通过。
红绿、真实原件、测试夹具修正和静态Python捕获范围详[本票记录](../12-js-work-validation.md)。
完整Cross/CI/Mac/ARM64及长测未执行，继续claimed/cross_deferred，等待独立merger。

2026-11-03：用户明确回复“按 27 张发布”，确认 27 张纵向任务及 38 条直接阻塞边；本票按已确认提案独立发布为 ready-for-agent，尚未领取或实施。父规格保持不变。

