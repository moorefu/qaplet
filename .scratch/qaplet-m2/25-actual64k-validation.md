# 25 · 实际 ARM64 / 65536：本地验证与交接

当前产品 SKU4 的全部本票适用合同已实际执行。状态 `local_complete / cross_deferred`，
待独立 merger 接收；不签发五原生、小时 soak、最终两轴 review、27或 release-ready。
源 worktree 为 `.cache/worktrees/m2-25-64k-matrix`，分支 `codex/m2-25-64k-matrix`。
本票全部原件位于该 worktree 的 `out/m2-25/`，没有删除旧 out 或 worktree。

## 当前固定供件与结果

SKU4 producer 位于主物理 `.cache/worktrees/m2-validation-supply-4/out/m2-validation-supply-4/`：
`build-4.json` SHA256 `b14bcdccb854d9a4004ea181b584189b29b514c05e49e08eea03483a20aa13e9`，
源 `8a0badb36c03baed79f74b88f651e7e095ea0dc4`，SDK
`324192be6af08779e8ce60eff4aac2a74d222bd376456bb54a6115846b726c38`，
recipe `4a59fdcf6cc545e87fd11033dcafafe4afb064537652ef6d8cce435d26996136`，
ZIP `ef7a4790e8f49d290ca58505f9275a039b13ed0a42351b3d66d7f4563423d36e`。
root 的 `out/qaplet-m2/sdk4-static-verification.json` 独立核过210实际原件、包与4 APE。

| 当前实际组 | 原始 guest 报告 | 严格 consumer | 结果 |
| --- | --- | --- | --- |
| 普通 Host162 / CLI53 / 示例14 | s4-ordinary-guest.json | s4-ordinary-import.json | 229/229 |
| fault25 / TTY13 / mixed12 | s4-special-guest.json | s4-special-verified-002.json | 50/50 |
| CLI IO10 / 真实PTY4 / signal16 | s4-cli-guest.json | s4-cli-verified-002.json | 30/30 |
| waker19 / process13 / stdio19 / TTY19 / pages64k12 | s4-backend-guest.json | s4-backend-verified.json | 82/82观察，7条guest命令 |
| waitid相关ABI | s4-abi-guest.json | s4-abi-verified.json | 1/1 |
| C9 lifecycle1000 / mixed100 | s4-c9-cycles-guest.json | s4-c9-cycles-verified.json | 1100实际循环，2条guest命令 |

只读总索引 `s4-matrix-001.json` 重核319条完整 guest 命令、394条语义记录及全部显式
原件索引；循环1100次另计。没有本票必需 fail、skip、not_run、缺项或超时。
四 backend 是独立 OS/libc 探针，不是产品源码二进制，也没有链接 SDK archive。

实际 guest：Linux `6.8.0-147-generic-64k` / aarch64，getpagesize 与别名65536、smaps64KiB；
QEMU6.2.0，`virt-6.2,gic-version=3`、`cortex-a72`、`tcg,thread=single`、一CPU、512MiB。
每份报告分别保留 QEMU可执行文件、kernel/config/压缩kernel、RAM rootfs/init、签名包输入，
serial原件、virtio journal+长度/SHA256 seal、准确argv/cwd/deadline/START/END/EXIT，
以及 payload传输前后SHA256。RAM-only、无磁盘、无网络、QEMU正常退出、无宿主全局修改。
这是真实64K完整系统内核，不是qemu-user；不填native ARM64槽位，不外推TCG物理性能。

默认权限fixture仍按原源码实际断言FD>256（可用hardlimit允许时>=8192），
`qaplet-host.m2-process-inherit` 实际通过；没有按低soft limit跳过高FD隔离。
既有034 nofile准备及06 RAM journal修复沿用原scope，没有修改runner或降级证据。

## CLI 与清理责任映射

普通229中 `qaplet.*`53就是完整 `sdk.CLI_CASES`：eval/run、argv/module、预算和基本错误。
新增14是文件entry复制、文件/管道二进制后台IO、200001字节最终输出、broken/invalid输出、
真实child双通道131072字节、普通process拒绝、分配故障、公开SDK结果对照10项与
normal/error/dispatch-budget/unicode真实PTY4项。53+14覆盖POSIX普通67，不重复运行53。

signal16为 wait/execute/output/help × SIGINT/SIGTERM八项、queue2、strong2、mixed2、
累计5秒native退休budget1、destroy后迟到native hint1。queue2只声明公开SDK relay Host，
不是CLI满队列端到端。output/help先有实际pipe容量和 `/proc` wait-channel观察才发信号；
原始sender/targetPID/signal/ret/errno逐次保留。strong/mixed先读实际native PID/PPid/exe与
live身份，mixed还保留原始termios/flags、唯一child SIGKILL9/reap、owner-close23、
quiesce6和destroy0。终端4项保留三个stream各before/modified/restored完整字段。

预算原件6次实际信号与多个cond-wait合计约5秒；故意保留native线程，
`native_cleanup_complete=false`。strong2的全部SDK内部native清理未直接观察到，字段为null。
special8项故意uncertain/nonreturning/lost-pid等也保留false：m2-faults、work-faults、lost-pid、
JS reap-fault、mixed nonreturning/tty-nonreturning/uncertain/tty-uncertain。成功返回不提升这些字段。
late原件证明destroy0、旧target identity保存、replacement未复用/未触及、late完成、未来entry可送达。

## 实际 C9 循环

共享独立C9供件来自24，producer `cycles-build-9.json` SHA256
`e6cb58e65f9f16bcc18c6d53936370a1926a51ff31e781aa01f305f5c546aeac`，
同字节 fatAPE `261baef841dbb4bda7686328978660a870fedfff00f4e4983c6f432b27706b19`。
该public consumer首次编译前保存实际C/application-m2.c/公开头/双archive与helper，
严格核实际compile/link/object/archive、packing和loader，不借产品源码二进制身份。

lifecycle1000：create/close/quiesce/destroy各1000，失败0，4566.942272ms；
mixed100：work/events/timer/borrowedstdio/真实TTY/child-reap/EOF/producer-join各100，
34正常、33取消、33错误，5520.374528ms。三次warmup每组另计。
最终helper/worker/keepalive均0，1000/200次基线检查；OS FD/thread生命周期5/1→5/1，
mixed6/1→6/1（首次5/1与持久fixture基线另列）。实际执行不是用空Runtime替代混合循环。

## 保留的红绿、SKU与工具迭代

旧SKU3 `ordinary-guest.json` 为228/229，`special-guest.json` 为46/50；
`diagnostic-repeated-guest.json`20轮6失败均SDK已reap且native exit0，证明旧POSIX
child在stdin EOF自然退出，尚未发布的SDK状态不等于原生仍活。未据此认定core取消bug。
共享单文件修正 `847426d` 保留原断言与5秒预算，使force-stop child生命周期独立于stdio，
case-owned READY+PID/PPid+WNOHANG0先证明活着，Stop后只由SDK reap。
ret0时raw status初始化占位不当作exit0；`wait_status_valid=(ret>0)`明确是审计派生解释。

旧/新EOF红绿原件 `red-mixed-child-eof.log`、`green-mixed-child-eof-2.log`，
调用身份在 `eof-invocations.json`（事后整理元数据，非冒充同步日志）；
fixed2 native12与guest32/32在 `fixed-mixed-build-run-2.json`、`fixed-mixed-guest-2.json`、
`fixed-mixed-verified-003.json`、`shared-mixed-fix-handoff.json`。旧输入与报告均未retag。
TTY另一shared fixture已由24修真实helper退休，root统一一次SKU4构建，不改产品API/core。

auxiliary origin3可对target4作精确component bridge，仅限实际用C/递归include/公开头/
双archive/helper/toolchain/loader与真实compile/link参数全部相等。原SDK/producer/packing不变，
随后SKU4新实际执行；stock Host已变则完整229新跑。TTY含变更include，重新编译正常与fault
两组：`s4-tty-build-run.json`，同字节供24原生消费。桥接总收据
`s4-component-bindings.json` SHA256 `f1a06e745a19bc4f3df82499723e797bf6917b9ed101fcefea5a02a479191bc6`。
旧C8的1000/100保留在 `cycles-guest.json`、`cycles-verified.json`，不当C9/SKU4成绩。

一次派生工具输出失误明确保留：`audit-s4-components.py` 曾重复使用
`s4-component-bindings.json`，首次exit1原因 `Unexpected auxiliary compiler executable`
（把portable-helper原生gcc当consumer cosmocc）被后续pass覆盖；首次终端退出/原因仍在会话，
原失败JSON不可回取，不从最终pass反造历史内容。`s4-component-bindings-red-1.json`
因复制过晚实际也是pass，文件名不当红证据。最终源码按实际cosmocc筛选consumer并独立核helper；
最终binding与所有产品/native/guest原件独立保留。后续工具输出均用唯一目录/已存在拒绝覆盖。

早期capture脚本演进时引用过可变source路径。实际历史字节从已有SHA/size核过的原RAM
initrd按明确member提取保存，非SHA搜索或重建：`preserve-capture-payloads.py`、
`capture-payload-preservation.json`、`guest-payloads/<report-id>/m2-guest-capture.sh`和精确path mapping。
原manifest/report未改。当前SKU4各新export在执行前保存唯一fixture/exporter快照。

CLI controller C1/C2对procfs size0和64K pipe容量的失败、C3–C7修正、原生原件均在
`red-cli-controller-native.log`、`cli-signals-guest-2.json`、`cli-controller-*/`、
`cli-routes-native-5.json`、`cli-budget-native-6.json`、`cli-mixed-native-7.json`。
当前C7字节固定后30项guest一次全通过。`s4-cli-verified-001.json` 是只读工具误期待TIMEOUT，
原始PTY明确code18 DISPATCH_LIMIT；修解释器后002只重读既有原件，不重跑产品。

工具红绿/负向：`red-cli-signal-evidence.log`→`green-cli-signal-evidence.log`；
`tool-negative-run-001.json`及其独立日志记录15测试全通过、产品重执行0，含缺signal、
完整恢复、累计预算、late replacement、strong null、缺seal、有效reseal后缺START、
afterhash改变、timeout清理拒绝。共享输入闭包还拒绝旧TTY transitive include和
额外.o/.a/脚本/异常link flag。Ubuntu仓库检查与最终清洁交接身份另见out的HANDOFF。
