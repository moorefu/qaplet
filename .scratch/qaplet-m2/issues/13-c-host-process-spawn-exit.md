# 13: 纯 C Host 的显式 spawn 与退出观察

**What to build:** C Host 以显式路径和复制的 argv/env/cwd 启动直接子进程，等待实际退出并回收；先完成明确 stdio 模式下的最小闭环。

**Blocked by:** [04 · 子进程参数、stdio 与继承隔离探针](04-process-inheritance-os-probes.md)、[06 · 等待中的 Runtime 接收事件与停止](06-wait-events-keepalive-stop.md)。

**Status:** claimed

**Execution state:** awaiting_cross；低 NOFILE fixture 修复已独立合并，等待新五原生与实际 64K 原件。

**External blocker:** GitHub 账号付款或支出限额拦截 job 启动，用户已提供页面原文；见[CI账单阻塞](../ci-billing-blocker.md)。当前平台缺口保持未验收。

**Local development:** complete；按用户2026-11-05的[本地开发指示](../local-development.md)，实现、Linux/Windows与本地实际64K已支持后续开发。完整Cross仍deferred，Status不提前resolved。

**Execution prerequisites:** 已满足本票直接 blocker；Ubuntu 构建环境及固定输入可用，复用仓库约定的输入、缓存和证据方式。ready-for-agent 表示需求已就绪，不绕过 blocker。

**Spec stories:** 51、52、53、54、55、56、57、58、65、77、81、83。

**Confirmed decisions:** Q12、Q19、Q22、Q24、Q25、Q29、Q36、Q41。

**Spec acceptance groups:** AC-01、AC-15、AC-16、AC-18、AC-21、AC-24。这些是本票涉及的父规格验收组；复杂组由多票分别完成子合同，再由完整矩阵重放，不把局部成功冒充整组完成。

## Acceptance criteria

- [x] 显式路径、Unicode/空格/特殊 argv、Runtime cwd 和创建时环境快照/完整替换成立，不搜索 PATH/PATHEXT 或隐式 shell。
- [x] 输入 UTF-8/NUL、项数/字节、缺程序及创建中失败有稳定拒绝和清理，旧入口结果不被未接受调用擦除。
- [x] ignore/inherit 等已完成子集有真实 C SDK 闭环；尚未完成的 pipe 行为不得被默认为成功或悄悄改成 ignore。
- [x] 直接子进程退出/reap 与实际 native 事实可观察，默认 ref 与进程/句柄额度从资源首次出现起有效。
- [x] 随资源引入实现 stop/close 的最小安全终止回收与保留，继承只含授权 stdio，不能泄漏 waker 或临时句柄。

## Blocking details

- [04 · 子进程参数、stdio 与继承隔离探针](04-process-inheritance-os-probes.md)：spawn、参数和句柄继承必须有真实 OS 事实。
- [06 · 等待中的 Runtime 接收事件与停止](06-wait-events-keepalive-stop.md)：退出观察和停止控制使用统一 owner 等待/唤醒。

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

2026-11-04：04、06 已按各自范围完成，领取 `codex/m2-13-process` / `.cache/worktrees/m2-13`，从当前集成点实施公开 C spawn、退出/reap 与资源首次接入的预算和关闭安全。能力位协调为 bit 22；尚未实现的 pipe 模式必须明确拒绝，不能回退为 ignore。最终状态仍以新供应制品和适用平台证据为准。

2026-11-04：显式 C spawn/ignore/inherit、实际 exit/reap、资源额度及两阶段清理候选已实现；
已融合 timer 和真实 Work 集成 `01e72a6`，并验证 PID 身份失效保留、共享 completion ledger/
32 项配额、默认环境合计预算。详见 [13 验证记录](../13-process-validation.md)。
按 root 调度先候选交接，等待 filefix/08 实际集成后 followup 最终重供；当前保持
`claimed / in_progress`，尚未标 awaiting_cross 或 resolved。

2026-11-04：恢复后已融合实际 filefix/08/02 集成，产品冻结 `6583ed3`，最新 SDK recipe
77 项。Linux normal162 / Windows161+optional；package39、原fault2均双宿主通过，
Process 包外公开9及 Linux系统8/Windows系统4通过。真实archive各15个 ELF成员、
Raw/Git blob/LF/ZIP/SDK/loader和实际驱动 imports已核验。详见[13验证记录](../13-process-validation.md)
及 `out/m2-13/closure/handoff.json`。本地交付转 `claimed / awaiting_cross`；独立 merger
与新的 Full/实际64K 由 root 安排，不以当前局部成功关闭父验收组或后续14–16能力。

2026-11-04（独立合并）：`a5c8b10` 已集成源tip `1b0886a` / 产品 `6583ed3`；77项RECIPE、双archive15、供应ZIP、595条原始命令/1422闭包文件、raw/Git blob/LF/合并树与两窄UbuntuCI通过，详主 `out/qaplet-m2/merge-13/final-handoff-001.json`。原std-route执行前hash不足限定与checker失败历史均保留。当前仍claimed/awaiting_cross，新的Full及实际64K验收后才放行14。

2026-11-04（当前Full失败与定向修复）：`453fe76` / Full `41817613432` completed/failure，11 jobs为8success/3failure；两Mac各154pass/8fail，三其他native各162pass、实际64K162pass。16个必需Process失败无timeout：其中Intel limits原exit0但stderr非空，其他15项exit1，不能以exit0或旧errno115推断成功/唯一根因。严格原件见主 `out/qaplet-m2/full-453fe76/verify/native-raw-observations.json`、`HANDOFF.md`；本轮未执行Process包外9/系统8/4，保留此前局部范围。另以旧密封SDK包外真实Host在Linux/Win证实B02缺口：单Process POD查询返回WRONG_THREAD3，而owner/aggregate/lifecycle均OK0；child37/reap/release/quiesce/destroy全成。见主 `out/13-b02-query-audit-001/conclusion.json`，Windows原runner将exit256归unexpected的限定保留，判断依据是公开返回。转in_progress，由fresh `codex/m2-13-cross-fix` / 新worktree修复纯POD查询、提供Mac真实code/domain/error及limit诊断，旧冻结m2-13源码/out不改。先证具体拒绝边界再改Mac行为，14仍未放行。

2026-11-04（纯POD与诊断阶段本地交付）：新树实际融合 `6b0de319`，产品 `0b725cc`。
新增任意线程 `qaplet_get_process_status` 的纯锁内 token POD 复制，保留旧 owner
观察/collect，不扩 POD；首次 slots 发布同步。旧 SDK 实际双宿主 red、缺符号 red 与
永久公开 query 的新密封ZIP包外双宿主绿均保留。最终 normal Linux170/Windows169+
旧optional1，package46/fault2各双宿主，Process公开10各双宿主及系统8/4、check370/
177实际193skip/bootstrap通过。77 raw/Gitblob/LF、ZIP79、三APE、双archive各15及
原命令/身份已核，见[新增验证范围](../13-process-validation.md)和新树
`out/m2-13-cross-fix/final/handoff-002.json`。Mac仅增强失败时实际code/domain/error/
helper/原生rlimit诊断；Full453两Mac16失败仍未关闭，等新原生Full事实后定向修复。
本票继续claimed/in_progress，不放行14或派发整体Review。

2026-11-04（当前Mac unlimited NOFILE定向修复本地交付）：fresh新树从实际4d13c20
实施，产品54b03c8。先旧密封SDK公开hard无限red，再固定双arch thunk/原对象审计，
临时实验aa76dc7 / run41834422457两Mac真实验证FD8192、soft256、188项/6页、父不变
和真实reap；ARM实验loader与早报摘要不同明确记录。Darwin改为额度内CLOEXEC目录
FD、fork child实际FD表枚举关闭非stdio并保留exec-error ack；不截断上界、不用soft/
私有cap、不改PID身份或物理join。永久inherit加强高FD/降soft/父状态并修复诊断被
重定向隐藏的问题；临时workflow只留实验分支。新正式供应normal Linux170、Win169+
旧optional1、package46/fault2各双宿主、Process公开10+系统8/4、同scoped hard无限绿
和check370/177实际193skip/bootstrap通过；77 raw/Gitblob/LF、ZIP79、三APE、双archive
各15及原日志/输入闭包已核，详[13验证记录](../13-process-validation.md)和新树
`out/m2-13-macos-fix/final/handoff.json`。Mac SDK完整原生链/实际64K仍等新Full；保持
claimed/in_progress，旧失败不改标，14与整体Review未放行。

2026-11-05：独立合并后的 `91b9a51` / Full `41848288564` 已严格确认五原生850/850，
其中Process50/query5，Mac产品spawn/退出/reap完整链已通过；但实际64K因固定输入
HTTP503仍为0，保持claimed/in_progress。固定输入供给独立合入94c，新Full
`41859852472` 的两Mac尚排队、page未开始。另以冻结公开SDK最小实测发现inherit
fixture在合法soft1024时于SDK调用前创建高FD失败；高soft和低soft inputs正对照通过，
未推断guest限额或产品失败。fresh `codex/m2-13-nofile-fixture` 只修复fixture准备，
原始red与全部身份见主 `out/qaplet-m2/nofile-fixture-probe/README.md`。14仍未放行。

2026-11-05（低soft NOFILE fixture准备修复）：fresh分支从94cda36实施，仅测试配方
034f3c8，融合integration0c8d7da。真实原SDK低soft red先于SDK调用；新fixture临时
提高soft准备合法高FD后立即恢复，hard允许仍8192、hard4096最低4095，再降soft256。
子三FD的fcntl/read EBADF、二进制stdio、exit37/reap及父flags/limits恢复不弱化。
新旧供应的公开低限额green、normal Linux170/Win169+optional1、包外46含Work20/
Process10/fault2及原件边界见[新增验证记录](../13-process-validation.md#低-soft-nofile-的-inherit-fixture-准备修复)
和主out/qaplet-m2/13-nofile-fixture-fix/handoff.json。原fault产品pass但unused audit.py
静态superset fail保留，同故障APE新scope纯消费补证，不重写原件。本票保持in_progress，
新actual64K/Mac矩阵仍由新Full验证；未关闭11/13、放行14、提前Review或push。
