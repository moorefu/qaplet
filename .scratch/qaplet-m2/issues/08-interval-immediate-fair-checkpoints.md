# 08: 重复 timer、immediate 与检查点轮转

**What to build:** 重复 timer、immediate 和业务事件有界轮转，完整 jobs 检查点、稳定顺序、ref/unref 与跨 pump 账本共同成立。

**Blocked by:** [07 · 一次 timer 推进 TLA 与自然退出](07-one-shot-timer-tla.md)。

**Status:** claimed

**Execution state:** awaiting_cross

**Execution prerequisites:** 已满足本票直接 blocker；Ubuntu 构建环境及固定输入可用，复用仓库约定的输入、缓存和证据方式。ready-for-agent 表示需求已就绪，不绕过 blocker。

**Spec stories:** 13、14、15、16、17、18、19、20、22、23、24、25、77、107。

**Confirmed decisions:** Q3、Q5、Q6、Q7、Q16、Q17、Q18、Q24、Q31、Q32、Q36、Q40、Q47、Q51。

**Spec acceptance groups:** AC-03、AC-04、AC-05、AC-06、AC-21、AC-22。这些是本票涉及的父规格验收组；复杂组由多票分别完成子合同，再由完整矩阵重放，不把局部成功冒充整组完成。

## Acceptance criteria

- [x] interval 至少 1ms，按原计划跳过错过周期且最多补一次；回调返回 Promise 不阻塞后续触发，取消阻止重排。
- [x] 相同 deadline 接受顺序、immediate FIFO 和回调中新 immediate 下一轮可复验，丢弃 JS 句柄不隐式取消。
- [x] 完整 jobs 检查点前不插入其他回调、不提前判 rejection；配额 yield 保留检查点和轮转位置。
- [x] 活跃入口共享账本，空闲交付及其 jobs 使用独立跨 pump 账本；无限 jobs 终止，普通回调错误结算后可继续驱动。
- [x] 每轮 jobs/交付/I/O 推进额度与已接受业务事件、最后 ref 释放及 unref-only ready 的边界一致，不增加 Node 钩子语义。

## Blocking details

- [07 · 一次 timer 推进 TLA 与自然退出](07-one-shot-timer-tla.md)：复用 timer 注册、最近 deadline 与交付。

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

2026-11-04：07 已取得当前五原生/实际64K严格闭包并 resolved，领取 `codex/m2-08-scheduling` / `.cache/worktrees/m2-08`。当前集成已含10的真实C WorkPool候选，需在已有 work/timer/event 持久轮转上加入immediate，保留完整jobs检查点、累计账本和统一timer/immediate额度；10本身仍等待独立新Full，不能将其合入当作完成。

2026-11-04：本地实现完成，合入实际integration `166ab50` 的票10文件夹具修复后，以产品 `bc5fc49` 重新正式供应。Linux153、Windows152+旧optional1、双宿主包外39/故障2、Ubuntu完整check369及bootstrap全部通过。八项新增公开合同和受控clock证明interval原计划跳过、FIFO/下一pump、真实cycle GC不取消、单pump/跨pump四类32单元轮转、完整rejection检查点和累计账本。70项当前recipe raw/Git blob/LF、双archive14成员及包成员身份统一，producer dirty=true原样保留。详[本票验证](../08-scheduling-validation.md)与本树`out/m2-08/final-filefix/handoff.json`。以上勾选表示已实现并完成局部正负验收；保持claimed/awaiting_cross，当前五原生/实际64K/新Full由root签发后再resolved，不外推最终T03或父M2。

