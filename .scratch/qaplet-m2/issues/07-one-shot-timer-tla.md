# 07: 一次 timer 推进 TLA 与自然退出

**What to build:** JS/C 创建一次 timer，run_once 按最近 deadline 等待，入口 await 被推进；timer ref/unref 和取消参与自然退出及执行期限。

**Blocked by:** [06 · 等待中的 Runtime 接收事件与停止](06-wait-events-keepalive-stop.md)。

**Status:** claimed

**Execution state:** awaiting_cross

**Execution prerequisites:** 已满足本票直接 blocker；Ubuntu 构建环境及固定输入可用，复用仓库约定的输入、缓存和证据方式。ready-for-agent 表示需求已就绪，不绕过 blocker。

**Spec stories:** 8、10、11、13、14、16、21、25、33、77、107。

**Confirmed decisions:** Q3、Q5、Q15、Q16、Q18、Q19、Q31、Q32、Q36、Q40、Q51。

**Spec acceptance groups:** AC-03、AC-04、AC-06、AC-11、AC-21、AC-22。这些是本票涉及的父规格验收组；复杂组由多票分别完成子合同，再由完整矩阵重放，不把局部成功冒充整组完成。

## Acceptance criteria

- [x] 公开最小 timer 接口从注册到 owner 回调可真实运行，零延迟不在注册栈同步执行。
- [x] 严格拒绝非安全整数、隐式转换和 deadline 溢出，资源身份校验 Runtime 与 generation。
- [x] 入口 DONE 后 ref timer 仍可驱动；仅 Promise 或 unref-only 资源不构成持续等待，显式 pump 可推进就绪工作。
- [x] 最近 timer、max_wait 和活跃入口期限共同限制等待；TLA、重复 pump/wake 不重置执行账本。
- [x] timer 取消、stop 和 close 不留下回调或资源；单调时钟失败/倒退的受控结果不延长期限。

## Blocking details

- [06 · 等待中的 Runtime 接收事件与停止](06-wait-events-keepalive-stop.md)：复用有限等待、入口/保活和停止唤醒。

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

2026-11-03：06 自身当前五原生及实际 64K 严格验收已完成，领取 `codex/m2-07-timers` / `.cache/worktrees/m2-07`，使用 fresh implementer 按已确认公开 SDK/JS/CLI seam 做 TDD。并行能力位协调：timer 使用 bit 19，保留全部既有 ABI 数值；需求及父规格不变。

2026-11-03：本地纵向红绿实现与合同记录见 [07 验证记录](../07-timer-validation.md)。独立 timer snapshot 不扩大既有快照；CLI 私有 entry-drive 在完整入口检查点交还并保存入口结果。当前保持 claimed / awaiting_cross，由 root 合并后统一签发当前 Cross/Full/实际 64K，不据本地通过 resolved 本票或父规格。


2026-11-04：已完成 HostNotify 融合及当前 `563ff33` integration 同步；最终 RECIPE 源码 `ddf00aa`。本地同字节 Linux 129 项、Windows 128 pass / 1 optional symlink blocked，必需缺口 0；各 1,000 次 timer 生命周期和 100 次 timer 混合、密封 ZIP 包外 timer 10 项及受控 fault 的双宿主重放、普通包外 C 应用 8 项、完整 dev check 均通过。元数据能力名称与累计等待预算夹具追加红绿、失败原件、最终供应身份见 [07 验证记录](../07-timer-validation.md)。保持 claimed / awaiting_cross，等待 root 新配方集成 Full/实际 64K 后关闭，不以本地成功 resolved。
