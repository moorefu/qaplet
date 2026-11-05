# 22：CLI 中断交接与清理等待，本地验证

2026-11-06，**Local development: complete**，等待独立接收。票 22 保持
`claimed / local_complete / cross_deferred`；本记录不签发父 AC 组、正式 Cross/Full、
M2 完成或发布。实际入口为 integration `97cc8f15f23a51e2c3cb13257a3bab9cf208e7a3`，
已含 21 独立合并 `13abf9ac` 与接收 `fe8a17d`；产品/配方冻结
`1343e6cb569b2245a03d141109da9be8ca0f457a`。专属分支 `codex/m2-22-cli-signal`。

完整交接在 `.cache/worktrees/m2-22-cli-signal/out/m2-22-cli-signal/HANDOFF.md`、
`handoff.json`；同目录保存生产/消费报告、完整输入、原始命令与日志、失败及 scope。
独立合入前不放行 23，本实现者不领取后继。

## 实现与生存期

CLI 在 main 开始建立原生入口，早于 Runtime 创建；Runtime 经原子发布交给普通
pthread relay。Unix handler 只保留 errno、发布锁自由 latch，并向固定 nonblocking
pipe 写 hint；不调用 SDK、mutex、malloc 或 stdio。Windows callback 明确为 MS x64
ABI，仅使用锁自由发布和预解析的 raw Win32 SetEvent，两个独立 hint 均尝试。
实际最终 PE callback 的反汇编及真实 Ctrl+C 验证在 `signal-build-1.json` 与 Windows
报告，未见 TLS、stack protector、外部 atomic helper。SDK 本身不安装进程 handler；
双架构发行 archive 的引用清单另存 `sdk-native-handler-audit.json`。

撤下 handler 不用作 join 证明：CLI 保留 handler 代码、静态状态、两个 Unix fd 或两个
Windows event 与 DLL 引用到进程结束，避免迟到入口触及复用数字。异步入口从不读取
Runtime。owner 停止 relay 的未来 SDK 调用，使用真实 pthread tryjoin 确认物理退休，
随后才允许 Runtime destroy；未 join、SDK 未 QUIESCENT 或未知原生完成均保留并退出 1。
普通运行期 relay 使用阻塞 waker，无固定短轮询。罕见 done 通知早于物理退出时，在剩余
固定 deadline 阻塞后再确认，不隐藏无界 join。普通 eval 的历史 tracer 两端各 20 次均为
毫秒级；最终混合/native 收口可约 5 秒，不解释为新的总退出硬 deadline。

5 秒是共享的累计主动原生等待预算，覆盖 SDK quiesce、relay 退休及中断后的 Host 输出
退休等待。重复信号不重置。账本保守扣除 SDK quiesce 调用经过的单调时间；close 在其前，
不强抢 native 回调、析构或任意系统调用，亦不承诺整个函数、同步诊断写入或进程退出的
硬耗时。时钟失败或倒退保守失败，不延长额度。未完成报告不等成功 destroy。

21 最后 Host 输出责任已收口：POSIX 最终写临时设 nonblocking，按真实可写/中断 waker
等待，退出恢复 flags，并保留线程局部 SIGPIPE 规则。Windows overlapped pipe 对独立
owned 副本/OVERLAPPED/event 等实际完成；中断取消后只用剩余预算等待，未知保留。
同步 pipe 最终 Host 写用 owned pthread、原生 thread HANDLE 和副本，CancelSynchronousIo
即使错过 syscall 接受，也不提前释放仍可能使用的数据；必须真实原生 thread 退出及
pthread join 后才能释放。同步普通文件的有界 stderr 诊断直接 WriteFile，避免预算已空
时另起尚未调度的诊断 helper。Console 保持严格 UTF-8→UTF-16 与既定路由。
同步 Windows pipe 的 SDK attach 仍 UNSUPPORTED；异步文件不新增支持。

Host 不提高 SDK 默认额度。每个未知输出调用至多保留一个 64KiB owned record，固定调用
图最多 7 个（payload 448KiB），元数据/allocator 另计；sync 每条最多两个 event 与一个
native thread HANDLE，另有 pthread 管理数据。relay 与每个 sync writer 请求 1MiB 栈，
最多 8MiB 请求栈（保护页、pthread/OS 元数据另计）；独立入口副本/render/OS 缓冲另计。
这不是 SDK 默认预算，亦不把进程结束时 OS 回收改称 SDK 已物理清理。

## 最终供应与实际范围

Ubuntu x86_64 / WSL2 Linux 6.6.87.2，页 4096；Windows 11 10.0.22000 / AMD64，页 4096。
所有构建均在 Ubuntu，复用主物理 `.cache/`；Windows 消费完全相同 APE 字节。

| 身份 | SHA-256 |
| --- | --- |
| SDK | `8f9db148ba9a5702cf434e945a33db1242ae48ba480708c816163cc641d50202` |
| recipe（109 输入，producer dirty=false） | `2f9b6716e895241efe1ba4fca78bf2e095971fd4926de905c1754e87c18f0d48` |
| 原 ZIP（95 成员） | `01ea1a8cc3abf3a085bb3c3b66c2031ee0da65d7986bb98db5253e3c73fc5145` |
| 普通 CLI APE | `89d0467572aa1b7c3f6e62f08040bd75ade47b780ccea1435c982fa425f720cc` |

109 个真实配方输入在构建前保存 `frozen-source-inputs/`，受控供应又独立保存
`signal-1-artifacts/inputs/`；均与 producer 逐 SHA 核对。头文件和双架构 SDK archive 与
21 供应 6 同字节，旧执行报告仍保留其原身份，不改贴本供应。

| 最终报告 | Linux | Windows | 声明范围 |
| --- | ---: | ---: | --- |
| `signals-final-*` | 8 | 4 | 真 SIGINT/SIGTERM 等待、READY 后中断、最终 pipe 输出、无 Runtime help 满 pipe；真 Ctrl+C 等待、READY 后中断、overlapped/sync 最终 pipe 输出；原 await execute 是弱门，不单独证明执行态 |
| `strong-owned-final-*` | 2 | 1 | 同一非 async JS 调用栈直接 spawn 后 for(;;)，没有 await/挂起；已验证身份的独立 native child 才能发 READY，随后真实 OS 中断；不依赖私有状态、随机 sleep 或 PC 调试 |
| `queue-final-*` | 2 | 1 | 单独公共 SDK C Host：已实际 QUEUE_FULL，OS relay 接受 stop；owner 后无 JS marks，丢弃两事件、QUIESCENT/destroy；不是 CLI 满队列端到端 |
| `budget-final-*` | 1 | 1 | 独立 OS pthread 退休故障：首次中断后，native exit held 门内再送五次中断；两段共享预算、incomplete/1，未 destroy |
| `late-final-*` | 1 | 1 | 独立 native hint 延迟：捕获 fd/HANDLE 后暂停，真实 Runtime destroy，创建替代对象，放行旧入口及真实 future 信号；原目标身份保留、替代对象未被触发 |
| `mixed-final-*` | 2 | 1 | 无故障原生观察供应：公共 TTY read 接受、timer/stdio/直接 child；owner stop、完整模式恢复、真实强制终止和唯一 native reap、QUIESCENT/destroy；finally 的 forbidden child 未出现 |
| `cli-final-*` | 67 | 72 | 既有结果、ESM/Script、公开负例、真实字节/stdio/TTY、overlapped/未知完成保留的受影响回归；包外 ZIP C consumer 在 Ubuntu 编译 |

以上均 pass（本票 Linux16/Windows9，连同既有回归共 164 个行为 case）。其中两端 budget，以及 Windows CLI 的未知
GetOverlappedResult 负例，明确保持 `native_cleanup_complete=false`；它们通过的是诚实
保留/未完成/退出 1，不能汇总成全部原生清理成功。工具合同为 1 pass、2 skip，另列。

三项 strong 输入及原生 arm 属独立 out 证据，不改产品或 109 配方。arm APE 在 Ubuntu
构建、两端同字节、不链接 SDK；真实 PID 文件后阻塞在 Host 身份验证门，之后才输出
READY 并持续存活。Linux 初始 PPid 必须是 CLI，driver 已实际设置/读取 subreaper，
owner 退出后 waitpid ECHILD 且 child proc 缺失，区别于 SDK 未 reap 而被 driver 收养。
Windows 在 READY 前用真实 process snapshot、完整 image、child→CLI→controller 父链
核验并保留 child HANDLE，之后真实 wait=0/GetExitCode=1。strong 的
`native_cleanup_complete=null`：只声明该 scope 的执行态和可观测 child 退出；不把
Windows 独立 Host wait 冒称 SDK 内部 reap 或全 Runtime QUIESCENT，后者由 mixed 原生
观察 scope 单独证明。无未追踪 child。

Linux driver 首批 final 记录了实际 target PID/signum，但未落盘 sender PID；未回填或猜测。
独立 `observe-os-run.py` 在真实 os.kill 边界补录 sender/target PID、owner wchan、原生返回
及单调时间，另以 `*-os-final-linux.json/ndjson` 对同一冻结字节执行上述 14 项。最终 scope
manifest 选择这批补录执行；此前 final 原件仍有自己的实际身份和范围。

首批最终供应 budget 的实际条件等待 Linux 为 3.200033322s + 1.799270366s；Windows 为
3.2007198s + 1.8008259s，后者实际 OS 返回略超过目标 deadline，非绝对墙钟承诺。
Windows 私有 Console initial=1/shared=2，group=0、CTRL_C_EVENT，sender 实收六次；
不以原始字节 0x03、Ctrl+Break 或非零 group 的 API 成功代替 Ctrl+C。

late Windows 用公开 1500ms 执行期限让 owner 在首个 callback hint 仍持有原 HANDLE 时
终止；首次原因保留 DEADLINE_EXCEEDED，另有 OS interrupt 诊断。受控门后确认的是实际
最后 hint 已完成与销毁后的新 callback 进入，不声称 Windows OS callback thread 已 join。
目标/代码继续保持进程生存期；控制设施清理不替代 SDK native QUIESCENT。

Windows mixed 原件保留 `[503,3,3] → [511,7,7] → [504,7,7] → [511,7,7]`。
SDK 租约恢复的是实际 entry 基线 511，不声明恢复启动前 503，不猜测 bit8 或重写 before。
固定 libc source mapping 仍 unproven。queue Windows 的 Unix disposition 字段不作测量，
Unix 实际前后读取与双 SDK archive 静态审计单列。

## 历史失败及未完成责任

所有旧 red、夹具/编译失败、受限 Windows timeout、真实 native 失败保留。最初 Windows
native=null/launcher_exit256 只记夹具未闭合；原生 PID 与 Cosmopolitan getpid 不同，
改用真实 GetCurrentProcessId 后才取得共享 Console/实际投递/旧 CLI native exit2 的产品红。
旧 Host 输出红的 supervisor timeout 只表示未完成。独立 fault 的初次包装崩溃、诊断未能
写出的失败均不改绿，最终输入、制品与新执行另存。

原 await READY execute 不据此重标成强执行态。后补 direct stdio.write→for(;;) 的输入未
产生 READY，外层 timeout/kill 保持未完成；同 CLI child 的中间 arm 选择也保留实际
Linux 通过/Windows 无 READY，Windows 该 SDK child 场景原因仍 unknown，不先归为产品
成功或单纯夹具问题。root 独立对照 21/22 已供应 CLI 的 CREATE_NO_WINDOW + 真实普通
文件 stdio，分别 exit0/READY/空 stderr（17.1/16.2ms），排除了纯无 Console 启动回归；
原件复制于 `independent-root-no-console/`，保留 root 原身份/路径，不外推 SDK child 场景。
后继 23 SDK 收口可保留 stderr 并观察 exited/closed 继续诊断，本票不扩展或猜测其原因。
两次 Windows 失败残留 owner 均经原生 parent、精确 image/输入核实后才
被 fixture 强制清理，不计为 SDK 清理。最终强门改用有真实身份与退休证据的 native arm。

部分早期 tracer 只保留源 SHA、对象/APE、命令/日志，未完整保存当轮 C 输入，标为中间
输入不可完整取回；不倒推补造。后续输入快照及最终 109 输入完整。实际 parent Python
imports 由既有报告记录，子进程动态 import 保持 unknown，不用静态 helper inventory 替代。

正式五原生、当前实际 64K、CI/Full、10000 mixed 生命周期、1 小时组合长测、M2 与最终
Standards/Spec Review 仍 deferred。原父 spec、27 票、38 条直接阻塞边、135 AC、默认
限额与 SDK ABI 保留；23 仍需本票独立合入后才满足其 22 开发前置。

## 独立本地接收

2026-11-06，fresh merger 从 clean integration `97cc8f15f23a51e2c3cb13257a3bab9cf208e7a3`
实际 no-ff 合入最终 clean source `d52371a370efa0e6a777e9ccea50f8834a42a3fa`，
合并 `b8b9f572cd25a43eb649ced4538616c2eddada2f` 的 tree
`ad22f50d3e091bc7de4bdd5479e1c18de84ebbbb` 与 source 完全相等。
主接收原件为 `out/qaplet-m2/merge-22-cli-signal/HANDOFF.md`、`handoff.json`。

合前与合后独立只读重放均通过：20 份选定报告、164 个行为 case、2283 个保留文件、
2519 个实际证据文件；109 个真实 recipe 输入与两份快照、95 ZIP 成员、公开/M1头、
双目标 archive 各19个 ELF64 REL 对象、3个 fat APE/sidecar 已逐 raw/Git/producer 核对。
原producer dirty=false不改，普通回归67/72与专项16/9分别计数；辅助无Console两项不混入164。
原始callback机器码与双SDK archive无handler引用另核。实际sender/target、私有Console
Ctrl+C、native entry与模式恢复、mixed唯一reap/QUIESCENT/destroy、共享累计等待及迟到
hint目标身份闭合。三个expected incomplete继续false，三个strong的全SDK native清理继续null。
Windows strong child的实际终态为53261（0xD00D）；GetExitCodeProcess成功BOOL和CLI exit1
分别记录，不互相替代。queue仍只签公共C Host共享relay边界。

最终包外消费者的临时目录 `/tmp/qaplet-sdk-consumer-uqawyama` 已不存在，实际Ubuntu只读
取件失败原件保留。其三个SDK输入按明确原ZIP成员逐SHA闭合，两个repacked输出按明确
最终artifact逐SHA闭合；两个pre-repack中间物不可回取，只保留记录身份，不作最终产品
或源字节证明，不以宽泛路径/hash搜索放行。早期tracer缺失输入、强门旧失败与same-CLI
Windows child原因unknown、21启动/租约范围、原dirty和动态child imports限制继续保留。
merger解析helper预核1/2原失败与源版本均保存，最终premerge3/postmerge exit0。

root另存的 `out/qaplet-m2/sdk-cli-child-22/109a5dcd-e343-48c4-988d-da996d4453b8/report.json`
是后续独立诊断，不属于20/164：最终22 CLI经公开spawn并await exited/closed，启动21/22
child均返回code26 CONTROL_FAILURE、child exit1/reaped=true，parent exit0且无supervisor
timeout。结合两版直接无Console均READY成功，问题收窄到SDK启动路径，原因仍unknown；
交23诊断/修复，不作为支持例外，不称已修复或22独有回归。

合后 Ubuntu root preflight pass：54 Python解析、52文本编码、pinned v3、主物理.cache。
merger新产品执行0，无SDK重建/重复suite；本记录只改三份Markdown。22
**Local development: complete**，仍claimed/local_complete/cross_deferred；本地可放行23，
merger未领取。父spec、原Blocked by/AC及27票/38边/135AC保持不变，正式欠项不提前完成。
