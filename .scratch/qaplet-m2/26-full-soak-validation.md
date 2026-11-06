# 26 · Linux/Windows 本地 Full 与资源稳定性

**Local development:** complete

2026-11-06，两种原生 x86_64/4096 目标各完成 10000 次真实混合 Runtime
生命周期及至少一小时持久 OPEN Runtime 长测。当前只读 consumer `8e7b9db`
在 Windows、Ubuntu 分别严格汇合四份原件通过；14 项实际合同测试全部通过、
零 skip。票仍 claimed/local_complete/cross_deferred，交独立 merger 接收；父 M2、
27 的原正式前置、三个缺失 native、CI、最终两轴 Review 和发布门禁保持原范围。

## 固定输入与身份

先实际融合共同 `8a0badb3`，再接收独立 C9 slice；实际执行评估器为 clean
`17e8347`（mixed01、Windows soak02）及 clean `f44a9fb`（Linux soak03）。
`477a41f` 已把最终 root `1b8f617` 融入本分支；`8e7b9db` 只补严读取保存原件的
consumer 与负向合同。执行 source 与当前 consumer 身份分别保留，不重标旧报告。

供应 SDK4 的产品 source 为 clean `8a0badb3`，SDK SHA
`324192be6af08779e8ce60eff4aac2a74d222bd376456bb54a6115846b726c38`，
115 RECIPE SHA `4a59fdcf6cc545e87fd11033dcafafe4afb064537652ef6d8cce435d26996136`，
原 ZIP SHA `ef7a4790e8f49d290ca58505f9275a039b13ed0a42351b3d66d7f4563423d36e`。
供应原件为主工作区 `.cache/worktrees/m2-validation-supply-4/out/m2-validation-supply-4/`
的 `build-4.json`、`supply-4-artifacts/`；build report SHA `b14bcdccb854d9a4004ea181b584189b29b514c05e49e08eea03483a20aa13e9`。

公共 C9 fixture 由 24 独占实现，在 Ubuntu 对实际 SDK4 公开 header、双 archive、
公开 application-m2.c 编译。其 `cycles-build-9.json` SHA
`e6cb58e65f9f16bcc18c6d53936370a1926a51ff31e781aa01f305f5c546aeac`，
两端消费同字节 `qaplet-m2-cycles.com` SHA
`261baef841dbb4bda7686328978660a870fedfff00f4e4983c6f432b27706b19`。
首次 compiler 输入、原 packing 与发布制品均保留；未改 sdk_link packing、115 配方、
SDK/API 或默认 helper4。Linux 使用固定官方 native x86_64 loader；Windows 实际 PE
经私有 ConPTY 获得真实 Console。没有新增 24→26 正式依赖边。

## 四份实际原件

实现者原件根为主工作区 `.cache/worktrees/m2-26-full-soak/out/m2-26-full-soak/`。
只选择 `full-selection-{windows,linux}-3.json` 指定的原件。

| 目标/原件 | 实际完成数 | fixture 毫秒 | 外层实际进程秒 | close/quiescent/destroy |
| --- | ---: | ---: | ---: | --- |
| Linux mixed01 | 10000 个 Runtime | 78209.639135 | 78.216966 | 各10000 |
| Windows mixed01 | 10000 个 Runtime | 220146.435600 | 220.237337 | 各10000 |
| Linux soak03 | 1个持续 OPEN Runtime；466624 个混合轮次 | 3603417.142643 | 3603.453636 | 各1 |
| Windows soak02 | 1个持续 OPEN Runtime；109641 个混合轮次 | 3604869.730900 | 3604.974419 | 各1 |

四原件 failed=0、timeout=false、supervisor_terminated=false。mixed 的三次独立
warmup Runtime 已实际销毁，未计入10000。每个 soak 另有三个独立 OS warmup
Runtime，随后测量 Runtime 内三轮 OPEN warmup 计入 completed；因此实际测量阶段
混合轮次分别为 Linux466621、Windows109638。idle/load/cancel 的实际时长总和
分别为3603204.408900ms、3604276.166401ms，独立排除 warmup 凑一小时。

每轮真实覆盖 work/file、event producer、timer 完成/取消、借用 stdio、适用
PTY/Console 及直接 child+三 pipe；accepted result/data/operation/endpoint/token
均释放，producer 实际 join，direct child 实际 reap，双路 EOF/排空另核。三类预期
normal/cancelled/errors outcome 总和与 completed 一致；errors 不是 fixture failed。
没有将故意 pending 负向在同进程累计10000次。

## 基线、系统等待与性能观察

持久 Runtime 三类 warmup 完成后只冻结一次 OPEN POD 基线；operation/data bytes/
timer/process/io endpoint/keepalive token 均精确为0，Linux worker2/helper0/native
handle2、Windows worker2/helper1/native handle3 是可解释驻留职责，不滚动重基线。
每轮基线检查、代次推进与共享容量重用通过；末尾 QUIESCENT、helper/worker/token0、
真实 destroy 与 direct-child/producer join 分别核验。

| 原件 | OS FD/HANDLE 固定 warm baseline→post-destroy | OS线程 baseline→post-destroy |
| --- | --- | --- |
| Linux mixed01 / soak03 | 6→6 / 6→6 | 1→1 / 1→1 |
| Windows mixed01 / soak02 | 87→87 / 87→86 | 7→4 / 7→3 |

OS总计按 after<=一次 warm baseline 表达无增长，真实减少原样保留；未归属的其它
TID/Unknown wait reason 保留在原始样本，不按总数减少单独宣称所有 SDK 线程退出。
OPEN 驻留职责与最终 native join/destroy 是不同事实；未声称进程 RSS/native bytes0。

Linux 是 WSL2 kernel6.6.87.2、Windows kernel10.0.22000；均实际 native x86_64、
4096页。mixed01起初四路并行，Windows soak02与已拒绝的Linux02并行，Linux03单独
补跑；仍在同一物理主机实际其它负载下，未把结果当作隔离 benchmark。

Linux03 的3584条核心 OS 样本均保留实际 `/proc` status/stat/FD/task 原文，另1条
明确 pre-ready startup 不当作 RSS0；96个非owner单TID ENOENT/ESRCH 事件有实际
枚举、errno/path/class，core CPU/RSS/FD/thread 与 owner wait 仍完整，live core/
owner gap0。Windows02保留3527条真实 Process/ThreadState/WaitReason样本。

两端各1200个实际 SDK idle wait 调用均在调用前有 current/ref timer>=1，requested
2000ms，drive reason=PROGRESS；Linux idle owner1196次 do_sys_poll，Windows1178次
UserRequest 等待，未知 reason 仍保留。whole run_once 时长中位数/最大值分别为
Linux1000.755527/1083.345532ms、Windows1010.042749/1016.395199ms，来源是调用前后
fixture monotonic clock；这些是整次调用时长，不是 timer lateness。

同相位相邻 OS 样本观测的 idle process CPU 分别为0.23s/1083.494566s及
3.3125s/1092.213327s；边界间隔未归入该统计。idle RSS 真实范围分别为
1048576–2883584、6688768–7376896 bytes，首末20条中位数分别为
1441792→2490368、7032832→7278592 bytes。增长、波动及采样来源均保留，没有预造
统一性能阈值。完整描述数据为 `final-resource-observations-4.json`。

## 原始失败与严格收口

两条 soak01 在约32分钟中断，无 native final/退出码/清理证明。原件、mtime、
原执行者 session 不可达及 root 独立 PID 不存在查询均保留；context/session寿命
只是原因假设，native cleanup unknown，不判为 SDK 成功或 SDK 失败。

Linux soak02 实际完成 SDK一小时/POD/native cleanup，但旧采样器丢了15条运行中
核心资源行，误记 process_exited；原始异常 path/errno 未保存，不能事后追认为线程
退休。旧 reader 忽略该分支，旧 pass/四join/11测试保留原范围；当前 reader 明确拒绝，
不采用其作为本票 Full 通过。实际 Python native-thread race 复现及真实 zombie 缺
VmRSS 负向后，collector 仅容纳已证明的非owner单TID ENOENT/ESRCH，保存完整 core/
owner 与结构化错误；owner/core/运行中整行缺测必拒绝，缺 VmRSS 不默认0。

Linux03从0完整补跑，未重跑已正确的Windows02/两mixed。完成前后九项冻结输入原
字节一致；其后 reader-only 补严错误/退出标志与resources矛盾、TID枚举归属、原始
PID/kB单位、startup只在首次有效观测前适用，以及跨OS POSIX路径比对。实际完整
Linux03正向与六类反例在两宿主通过；14项总合同全部实际执行、零skip。

`tdd/24`、`25` 的前缀正向实际ERROR，并非有效红绿，保留原内容；`26/27`才是Linux前缀
真红绿，`28/29/30`为完整原件正式边界红绿。外围汇总器另有CRLF误判：实际11项
unittest exit0/OK，原错误wrapper记录与原stderr保留；最终14项按原始逐行结果解析。

最终严格结果为 `full-verified-{windows,linux}-3.json`、
`acceptance-{windows-verify,linux-verify,tests}-3.json`；
执行前 catalog、最终 consumer source、命令/环境及 stdout/stderr 各自保存。
`full-freeze-1/3.json`、`post-run-freeze-3-binding.json`分别锁定旧/新执行输入与
实际不变证明。所有 progress、wait、OS raw、ConPTY report 与旧失败不改写。
