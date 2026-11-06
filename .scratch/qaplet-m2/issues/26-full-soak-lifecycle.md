# 26: Linux/Windows Full 长测与资源稳定性

**What to build:** Linux/Windows 各实际运行 10,000 次混合生命周期及 1 小时 idle/负载/取消交替长测，证明资源有限且退出责任不累积。

**Blocked by:** [23 · 完整 SDK 分发与包外 C Host 示例](23-sdk-package-c-host-example.md)。

**Status:** claimed

**Execution state:** local_complete / cross_deferred

**Local development:** complete

**Execution prerequisites:** 23已本地完成并独立合入`6ab8b73c`，接收`a5439c9d`；本票本地前置已满足。当前SKU4两端各10000真实混合及至少一小时持久RT已实际完成，双平台严格四原件重读与14实际合同零skip；已由独立merger以no-ff `d4dca9d2`完成本地接收。旧23兼容循环与已拒绝的Linux02不替代当前Full，正式边与27前置保持。详见[26本地验证](../26-full-soak-validation.md)。

**Spec stories:** 5、15、42、47、49、58、63、64、70、76、77、79、80、84、85、86、87、88、89、90、91、92、93、100、101、102、110。

**Confirmed decisions:** Q4、Q14、Q16、Q24、Q26、Q27、Q35、Q36、Q37、Q40、Q45、Q48、Q49、Q50、Q51、Q52。

**Spec acceptance groups:** AC-04、AC-07、AC-10、AC-14、AC-17、AC-18、AC-20、AC-21、AC-23、AC-24、AC-27、AC-28。这些是本票涉及的父规格验收组；复杂组由多票分别完成子合同，再由完整矩阵重放，不把局部成功冒充整组完成。

## Acceptance criteria

- [x] 两种原生目标分别完成并记录 10,000 次混合生命周期，不能以空实例循环替代真实资源创建/完成/取消/关闭。
- [x] 每目标至少 1 小时 idle、负载和取消交替长测，任务、timer、stdio/TTY 及直接子进程覆盖适用场景。
- [x] 操作资源计数回到基线、共享容量不随次数累积，无 UAF/死锁/无界队列；计数与实际 OS 清理事实对应。
- [x] 无工作时进入真实系统等待，记录 idle CPU/RSS/延迟和宿主来源，不先编造统一性能阈值。
- [x] 原始进度、实际时长/次数、失败和超时完整，24 小时及网络发布长测仍归 M8/后续阶段。

## Blocking details

- [23 · 完整 SDK 分发与包外 C Host 示例](23-sdk-package-c-host-example.md)：长测必须运行当前完整产品工作负载。

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

2026-11-06：23独立接收后领取本票，fresh实现者以独立分支/worktree实现并执行两端
Full。先协调公共fixture及固定SDK/APE身份，再开始昂贵验证；实际资源覆盖、进度、
次数、时长、OS/POD基线与安全清理均保留原件。与24/25并行开发，不新增依赖边；
不以空实例、outer timeout、进程退出或强制设施清理冒充SDK原生quiescent。



2026-11-06：本地Full实际完成，公开SDK4/C9原字节不变，执行source17e8347/f44a9fb与
consumer8e7b9db分别绑定。当前两端各10000混合、一个OPEN RT至少1h、真实系统等待/
POD/OS/native cleanup通过，四原件双OS严格重读及14项实际合同零skip。Linux总466624、
测量phase466621；Windows总109641、测量phase109638；各3轮OPEN warmup不凑小时。
两soak01中断的native退出/清理unknown，Linux02旧observer缺15条live core被拒绝，
新03全部3584核心样本及96个已分类非owner查询事件可取；原失败和未知不改写。
详见[26验证](../26-full-soak-validation.md)。本票claimed/local_complete/cross_deferred，
等待独立merger接收；父M2、27原正式前置与发布/最终Review门禁保持原范围。


2026-11-06（独立接收）：最终source `b2223ef5` 从actualroot `1b8f6178` 独立no-ff合入
`d4dca9d2`，候选tree `2fd7e23426d7cc4be427a09a350b871cd0a30d5c`与source完全相同。
合前/合后1245个唯一文件、372355168字节及封闭out的523项原件逐项通过；SDK4原ZIP、
C9/77编译快照、115配方/4冻结文件、15实际consumer导入和103历史root raw不变。
四当前原件只选mixed01两份、Winsoak02、Linuxsoak03；执行17e/f44、consumer8e和最终
b222分别绑定，旧Linux02的15条live缺测仍被拒绝，旧01退出/清理unknown不累加。
合后普通用户Ubuntu预检71解析/64编码通过，独立strict重放通过；完整14项Windows
合同零skip、正式guard单方法两OS六类变异按原日志核。merger无产品/编译/重复suite/
guest/长测或peer写入/会话控制。主收据为`out/qaplet-m2/merge-26-full-soak/handoff.json`。
仍claimed/local_complete/cross_deferred；缺三native、正式CI/Cross、27、父M2、整体
两轴Review与release门禁不由本地Full外推。
