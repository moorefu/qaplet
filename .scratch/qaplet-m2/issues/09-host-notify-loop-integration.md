# 09: 宿主自有事件循环的原生通知接入

**What to build:** C Host 通过可选 HostNotify 唤醒自己的循环，再在 owner 分步驱动 Runtime；内部等待仍由独立 waker 保证。

**Blocked by:** [06 · 等待中的 Runtime 接收事件与停止](06-wait-events-keepalive-stop.md)。

**Status:** resolved

**Execution state:** completed；当前配方五原生及实际 64K 的本票 SDK/HostNotify 范围严格验收通过，整体 Full 的独立 TTY 失败保留。

**Execution prerequisites:** 已满足本票直接 blocker；Ubuntu 构建环境及固定输入可用，复用仓库约定的输入、缓存和证据方式。ready-for-agent 表示需求已就绪，不绕过 blocker。

**Spec stories:** 29、74、92、108、109。

**Confirmed decisions:** Q9、Q34、Q39、Q45、Q48。

**Spec acceptance groups:** AC-02、AC-07、AC-08、AC-24。这些是本票涉及的父规格验收组；复杂组由多票分别完成子合同，再由完整矩阵重放，不把局部成功冒充整组完成。

## Acceptance criteria

- [x] 原生通知可来自任意生产者/完成线程，宿主用自己的真实等待机制收取提示，不导出 SDK 等待句柄。
- [x] 合并与多余通知不改变业务数量或交付结果，内部 run_once 不依赖宿主钩子完成唤醒。
- [x] fixture 中钩子快速返回、不运行 JS、不重入 SDK，SDK 不以回调阻塞替代内部控制交接。
- [x] 内部清理与外部在途通知均结束前，代码和 userdata 不可卸载；确定性交接覆盖 close 重叠。
- [x] 投递、stop、令牌释放和清理进度的通知只报告实际支持范围，POD 查询及线程权限不变。

## Blocking details

- [06 · 等待中的 Runtime 接收事件与停止](06-wait-events-keepalive-stop.md)：复用状态发布、唤醒和 owner 分步驱动。

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

2026-11-03：06 自身当前五原生及实际 64K 严格验收已完成，领取 `codex/m2-09-host-notify` / `.cache/worktrees/m2-09`，fresh implementer 执行公开 SDK/真实宿主等待接入的 TDD。并行能力位协调：HostNotify 使用 bit 20，保留既有 ABI 数值；内部 waker、原生在途和 userdata 保留期不得延后实现。

2026-11-03：本地产品 `f0feb5c` 已完成，保持 claimed / awaiting_cross。独立通知声明/POD、锁下 lease 与锁外 hook、内部 waker 独立、close 重叠、finite quiesce 与原生/外部双重保留期均经公开 SDK 及包外 Host 验证；不虚构后续 work/process 完成。64 项 RECIPE Git blob/LF 相同，最终 Linux 115 pass、Windows 114 pass + 1 可选 symlink blocked（必需缺口 0），包外应用 8、受控 fixture Linux/Windows 各 1、独立 Notify Host 各 4 全通过，两平台各实际 100 组进程内/12,800 业务事件。当前完整本地 check 通过。受限 Windows pre-main timeout、错误 loader 选择和一次性报告 adapter 失败原件保留；最终五原生/实际 64K 与全资源 T03 待 integration 新 Full。详见[09 独立验证记录](../09-host-notify-validation.md)，冻结原件为本树 `out/m2-09/final/`。

2026-11-04：当前 `563ff33` 的 [Full 41788055889](https://github.com/moorefu/qaplet/actions/runs/41788055889) 整体 completed/failure，唯一 Windows TTY resize 失败另行调查，不能称全 run 通过。09 自身范围由 fresh 独立核验及 root 二次核验完成：五原生 SDK 各 115（575）、实际 64K 115、包外应用 8、独立 controlled fixture 1、当前 M0 Full 235 全部通过，必需缺口 0；本次 Windows optional symlink 也实际通过。29 provider 原 ZIP、12 嵌套供应物、11 job 日志、64 RECIPE 与精确提交/冻结 evaluator、密封 SDK/双 archive 各 12 ELF64 REL、原生 loader、guest journal seal/argv/清理均核验。`out/qaplet-m2/full-563ff33/verify/scope-summary.json`、`HANDOFF.md` 与 `09-acceptance-check.json` 保存选择、边界和失败历史。按本票 scope resolved，不关闭父 M2、最终 T03 或发布门禁；旧整体失败不改标。

