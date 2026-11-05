# 23: 完整 SDK 分发与包外 C Host 示例

**What to build:** Ubuntu 产生完整 M2 CLI/静态 SDK 和包外纯 C Host 示例，示例同时演示命名工作、宿主循环通知、进程/stdio 和安全关闭。

**Blocked by:** [19 · 跨资源预算、POD 快照与终止首因](19-aggregate-budgets-metrics-failures.md)、[22 · CLI 中断交接与清理超时报告](22-cli-signal-stop-cleanup-budget.md)。

**Status:** claimed

**Execution state:** local_complete / cross_deferred

Local development: complete

[本地验证](../23-sdk-delivery-validation.md)与专属worktree
`out/m2-23-sdk-delivery/HANDOFF.md`、`handoff.json`保存最终SKU3、包外纯C组合例、
Windows公开spawn→CLI修复、两端实际消费及原件/限制。产品配方冻结`144d5e18`，
其后`d6d1d16`只修两处测试读取编码，未改产品RECIPE。已独立no-ff合入`6ab8b73c`；主接收原件见
`out/qaplet-m2/merge-23-sdk-delivery/HANDOFF.md`、`handoff.json`。本地开发可放行24–26，
merger未领取后继。正式Cross/Full及父AC组仍待验，下列验收项未因本地通过勾选。

**Execution prerequisites:** 19已本地完成并独立合入`597020eb`（接收`2b3035f7`），22亦已独立合入`b8b9f572`（接收`6cca6f9b`）；当前全部开发前置已满足，已领取。正式Cross/Full仍待验；验证范围见[本地开发方式](../local-development.md#2026-11-05剩余开发前置与验证范围)。

**Spec stories:** 1、2、3、4、5、29、35、37、38、50、51、66、74、75、90、91、92、94、95、99、105、108、109。

**Confirmed decisions:** Q1、Q2、Q4、Q7、Q11、Q14、Q20、Q25、Q26、Q33、Q34、Q39、Q41、Q43、Q45、Q48、Q53。

**Spec acceptance groups:** AC-01、AC-02、AC-08、AC-12、AC-13、AC-15、AC-19、AC-24、AC-25、AC-28。这些是本票涉及的父规格验收组；复杂组由多票分别完成子合同，再由完整矩阵重放，不把局部成功冒充整组完成。

## Acceptance criteria

- [ ] 封装公开材料足以独立编译链接 C Host，不需私有引擎头；旧头文件消费者仍可使用旧创建模式。
- [ ] 示例真实组合 M2 创建、命名文件工作、事件/通知、异步进程/stdio、POD 查询及 close/quiesce/destroy。
- [ ] 创建/失败/取消/清理示例遵守全部数据保留期，不把接受或 CLOSED 当成全部退出。
- [ ] 同一 APE 字节跨宿主消费；各目标原生 SDK archive/slice 单独标识，头文件、库、CLI 帮助和实例能力一致。
- [ ] 供应包/配方/源码/loader/原 ZIP/外部编译命令及原始日志可审计，未实现或未经验证能力不能写入已支持清单。

## Blocking details

- [19 · 跨资源预算、POD 快照与终止首因](19-aggregate-budgets-metrics-failures.md)：完整发行物需要完整默认值、快照和错误合同。
- [22 · CLI 中断交接与清理超时报告](22-cli-signal-stop-cleanup-budget.md)：发行物包含最终 CLI 中断和清理行为。

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

2026-11-06：19/22均已实际独立接收，root领取23，以当前integration创建专属
`codex/m2-23-sdk-delivery`分支/worktree，fresh实现者按公开行为TDD完成M2组合示例、
SDK分发与消费工具收口。先诊断Windows公开SDK启动CLI child的可复现CONTROL_FAILURE：
旧/新parent与旧/新child组合均已观察到child exit1/reaped、parent exit0，直接无Console
启动同一供件则正常；原因待定位，不把它改成支持例外。保留22信号/安全合同和实际限制。
只有23本地完成及独立合入才放行24/25/26本地开发；27仍需其正式全范围验收。

2026-11-06：23本地实现完成，已融合两条内部职责分支和真实integration祖先。
SKU3正常Linux229/Windows228+optional1、ZIP包外两例14、CLI67/72、sender2/1及
强执行态2/1通过；只读两native slot接受，其余三个仍blocked。公开Windows
CONTROL_FAILURE已用单项SystemRoot与socket10106真实OS对照定位并最小修复，
explicit replacement/空env/句柄隔离合同保留。所有输入及packing原件可审计，原tmp
已不可达且原因unknown照实记录。状态claimed/local_complete/cross_deferred；
父M2、24–27正式矩阵/64K/Full长测及最终两轴Review未完成。


2026-11-06：fresh merger独立接收最终clean source `2c856c85`，no-ff `6ab8b73c`
的tree与source逐字相等。13份最终选定报告、617实际case记录（616pass及既有
optional symlink blocked1）、6696本票保留文件和两份内部原件inventory合前/合后
完整只读重放通过；57解析/55编码的合后Ubuntu root preflight通过。6项typed
cleanup=false、6项metadata/arguments null和3项strong整SDK清理未直接证实的null
分别保留；原native pair总体blocked、guest仅export、旧M1 lifetime1000不外推。
Status仍claimed/local_complete/cross_deferred，原Blocked by/AC及正式欠项不变。
