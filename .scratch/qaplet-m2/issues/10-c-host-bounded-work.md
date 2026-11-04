# 10: 纯 C Host 的有界原生工作闭环

**What to build:** C Host 注册命名处理器，提交真实有界文件读取任务，等待并复制结果；运行中的任务可安全跨 close 保留到原生退出。

**Blocked by:** [06 · 等待中的 Runtime 接收事件与停止](06-wait-events-keepalive-stop.md)。

**Status:** claimed

**Execution state:** awaiting_cross

**Execution prerequisites:** 已满足本票直接 blocker；Ubuntu 构建环境及固定输入可用，复用仓库约定的输入、缓存和证据方式。ready-for-agent 表示需求已就绪，不绕过 blocker。

**Spec stories:** 3、5、31、35、36、38、39、40、41、42、47、50、79、80、86、87、88、89、91、108、110。

**Confirmed decisions:** Q4、Q10、Q11、Q19、Q20、Q26、Q27、Q35、Q36、Q37、Q38、Q45、Q52。

**Spec acceptance groups:** AC-01、AC-10、AC-11、AC-12、AC-13、AC-21、AC-24。这些是本票涉及的父规格验收组；复杂组由多票分别完成子合同，再由完整矩阵重放，不把局部成功冒充整组完成。

## Acceptance criteria

- [x] 执行前注册冻结且复制声明；重复/替换/运行中注册拒绝，函数与 userdata 的 native quiescent 保留期明确。
- [x] 输入成功接受前复制、输出事先预留，内部完成不依赖业务 onEvent 或队列空位；超额无部分接受。
- [x] 每 Runtime 独立按需 worker，默认池 2、处理器并发 1；不同可运行处理器不被额度阻塞的队首任务饿死。
- [x] 纯 C 消费者实际完成有界文件读取、原生错误和结果复制，不访问 JSValue/Context。
- [x] 随首个 worker 实现安全 stop/close、native pending、quiesce 和 destroy BUSY，关闭时不得提前释放代码、数据或输出。

## Blocking details

- [06 · 等待中的 Runtime 接收事件与停止](06-wait-events-keepalive-stop.md)：原生完成需要 owner 等待/唤醒及实例控制屏障。

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

2026-11-03：06 自身当前五原生及实际 64K 严格验收已完成，领取 `codex/m2-10-work` / `.cache/worktrees/m2-10`，fresh implementer 执行公开 C SDK/真实有界文件读取的 TDD。并行能力位协调：C work 使用 bit 21，既有 ABI 数值不变；首个 worker 即实现容量、原生完成保留及安全关闭，不延后到 19/20。

2026-11-04：07/09 融合后冻结产品 `9c69877`，提交原字节、70项 RECIPE、双 CPU archive、SDK metadata 已核对。完整 Linux 142/142 pass；Windows 原生同字节141 pass / 1既有 optional symlink blocked，必需缺口0；密封ZIP包外普通8+work13=21及受控系统故障2项均在Linux与Windows通过。本地check通过；新增三类持久轮转、work允许unref timer存活、真实HostNotify lease和物理pthread退出的失败原件及最终身份见[10验证记录](../10-work-validation.md)。保持 claimed / awaiting_cross，待root当前配方五原生/实际64K及组合Full签发，不自行resolved，不声明11任务取消公开API或12 JS work Promise。

