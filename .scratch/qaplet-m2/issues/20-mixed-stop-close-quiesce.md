# 20: 组合资源的停止与两阶段清理

**What to build:** 同一 Runtime 同时持有任务、timer、进程、stdio/TTY 和宿主通知时，stop 到 close/quiesce/destroy 的所有权屏障一致且可证明。

**Blocked by:** [09 · 宿主自有事件循环的原生通知接入](09-host-notify-loop-integration.md)、[16 · JS Process 与 stdio 的完整异步闭环](16-js-process-stdio-liveness.md)、[18 · TTY 租约、resize 与 Console UTF-8](18-tty-console-leases.md)。

**Status:** claimed

**Execution state:** local_complete / cross_deferred；已独立本地接收，实际no-ff `943c1962`。

**Local development:** complete；[本地组合验证](../20-mixed-cleanup-validation.md)及主
`out/qaplet-m2/merge-20-mixed/HANDOFF.md`、`handoff.json`保存独立接收；源worktree的
`out/m2-20-mixed/`保留不同供应/配方的实际执行身份。正式Cross/Full及最终矩阵仍deferred。

**Execution prerequisites:** 09已resolved，16本地完成并独立合并`015cdb9f`（接收`71a5db50`），18本地完成并独立合并`3e85a272`（接收`64624ddd`）；全部当前开发前置已满足，已领取，与19并行。正式Cross/Full欠项不替代也不取消，验证范围见[本地开发方式](../local-development.md#2026-11-05剩余开发前置与验证范围)。

**Spec stories:** 3、4、5、19、20、31、36、45、47、48、49、55、64、68、70、84、85、86、87、88、89、90、91、92、93、101、109、110。

**Confirmed decisions:** Q2、Q4、Q19、Q21、Q23、Q24、Q26、Q27、Q34、Q35、Q39、Q44、Q45、Q50、Q51、Q52。

**Spec acceptance groups:** AC-02、AC-10、AC-11、AC-14、AC-18、AC-20、AC-22、AC-23、AC-24。这些是本票涉及的父规格验收组；复杂组由多票分别完成子合同，再由完整矩阵重放，不把局部成功冒充整组完成。

## Acceptance criteria

- [ ] 请求线程只记录终止并唤醒，owner 确认 STOPPED/FAILED 就撤销调度、取消工作并启动直接 child 回收，不等 close。
- [ ] STOPPED、入口 DONE、循环空闲、JS CLOSED 与 native quiescent 分别可见；关闭后无用户 jobs/finally，清理不使 CLOSED 复活。
- [ ] 混合资源下 quiesce(0)/有限累计等待可重试，destroy 在原生未退出时 BUSY，等待预算不宣称强抢 native/析构。
- [ ] 同步 Host binding 到 close、新 work 到 native quiescent、通知钩子及外部生产者到全部在途结束的保留期分别成立。
- [ ] 确定性覆盖取消/完成/close/复用和首因竞争、初始化/清理错误及不退出任务；保留数据而非假销毁，其他资源继续可行回收。

## Blocking details

- [09 · 宿主自有事件循环的原生通知接入](09-host-notify-loop-integration.md)：混合清理必须覆盖通知 userdata 与外部在途调用。
- [16 · JS Process 与 stdio 的完整异步闭环](16-js-process-stdio-liveness.md)：必须已有任务、进程及 stdio 的真实退出责任。
- [18 · TTY 租约、resize 与 Console UTF-8](18-tty-console-leases.md)：混合清理包括终端恢复和租约所有权。

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

2026-11-05：18独立接收后由root实际领取；分支`codex/m2-20-mixed-cleanup`、worktree
`.cache/worktrees/m2-20-mixed-cleanup`从当前实际integration起步，fresh实现者TDD，与19并行。
沿用全部资源既有有限预算、物理保留及唯一回收责任，验证混合场景而非延后首次安全。
本票本地完成及独立合入后可放行21开发，正式Cross/Full与最终验收仍待验。

2026-11-05：五项本地组合合同完成。SDK2产品`e3b4bc4`，包外纯C/JS混合资源两端各12/12、
相关Host选定回归各23/23，独立包外stop/waker首因双序各2/2通过。真实TTY/ConPTY、同步binding
close释放、work物理保留、通知在途与外部未来调用屏障分别验证。nonreturning/unknown明确保留
pending/BUSY，其他资源仍安全退出/恢复，不声称最终native清理。SDK1完整normal与所有早期
夹具/环境失败保留原身份，未重贴成绩。当前仍claimed、local_complete/cross_deferred，待独立接收；
原Blocked by/AC/父spec及正式Cross/Full欠项不变，详本地验证和最终HANDOFF。

2026-11-05（实际集成收口）：已融合19真实no-ff `597020eb`和后续MD-only实际root `a4e5fec1`。
最终供应3产品`0916ff72`，Core/header与19产品逐文件同源；原ZIP包外mixed两端各12/12、
当前相关Host/aggregate旧头回归各28/28、独立包外首因双序各2/2通过，完整ConPTY模式与
实际原生回收/有意保留分别可见。旧供应1/2、所有失败和source/dirty均保留原身份，未重贴为
新完整矩阵。最终清洁HEAD及原件摘要见HANDOFF，仍待独立接收后放行21；正式Cross/Full未完成。

2026-11-05（独立本地接收）：fresh merger从clean root `a4e5fec1`实际no-ff合入最终
clean source `ec93a4ca`，合并`943c1962`与source tree完全一致。复用供应3静态预核并合前/
合后独立只读重核20scope/1852raw；当前mixed各12、selected各28、首因双序各2通过，四个
安全保留负向/端仍为native_cleanup_complete=false，ConPTY设施清理另核。供应1完整normal
212/211+optional1、供应2旧范围及失败均保留原身份；47脚本解析/52编码文件的合后Ubuntu
preflight通过，主物理.cache。主接收原件见`out/qaplet-m2/merge-20-mixed/`；merger产品执行0，
无重建/重复suite。原Blocked by/五AC/父spec不变，保持claimed/local_complete/cross_deferred；
本地开发前置可放行21，merger未领取后继，正式Cross/Full与最终矩阵仍待验。

