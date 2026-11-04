# 13 · C Host 显式 spawn 与实际退出/reap

2026-11-04。本地实现与适用双宿主验证完成，票保持 `claimed / awaiting_cross`。
当前供应为 `out/m2-13/closure/`，不能与下面保留的旧候选 `final/` 混用。
独立 merger 及新的 Cross/Full/实际 64K 由 root 安排；本页不能关闭父验收组。

工作树为 `.cache/worktrees/m2-13`，分支 `codex/m2-13-process`。起点 `8167dc1`；
实现提交 `628b11d`，timer 集成合并 `768eb56`，完成交接账本修复 `98b52c2`；
已合入真实 Work 集成 `01e72a6`，本分支对应合并提交 `0bb529e`。
后续 PID/清理首因/环境预算修复和公开回归以本分支最新候选提交为准。

## 已实现的 C 子集

- 独立版本化 process options/token/status/snapshot，能力 bit 22；追加
  `PROCESS_ERROR=27`、`ERROR_DOMAIN_WIN32=2`，既有枚举值和 M1/M2 旧 POD 布局不动。
- 显式程序路径；相对程序/cwd 基于 Runtime cwd。严格 UTF-8/NUL、参数数量和合计
  argv/env/path 字节预检。M2 创建时复制 Host C 环境；默认快照和显式完整替换均计入
  单次输入上限。复用不可变快照只避免重复存储，不免除该输入额度。
- ignore/inherit 真实闭环。默认三路 pipe 当前明确 `UNSUPPORTED`，没有 ignore fallback。
  Windows 原生 CreateProcess 使用独立 Unicode environment block 和三 HANDLE 白名单；
  POSIX exec 前清理额外描述符。内部 waker 和临时 fd 均移出 0–2，复制瞬间也计句柄峰值。
- 每个 child 使用有限 helper 阻塞等待，受实例共同 helper/请求栈、native handle 和
  completion registration 额度控制。默认 process=32、helper=4 不保证同时接受 32 个
  运行 child。进程终态和物理线程 join 分开；TLS 未退出时不返还栈/线程/槽额度。
- POSIX 先 WNOWAIT，mutex 内最终 waitpid，避免正常 reap 与 stop 之间的 PID 复用。
  Windows 保留实际 GetExitCodeProcess 值；没有将 Cosmopolitan wait status 归一化成 Win32。
- stop/close 从首次资源起强制结束直接 child 并保留 reap/join 责任。拒绝调用若已经创建
  child，也立即启动该 child 的回滚，不等后续 close。quiesce(0) 不阻塞 join，未知身份
  不重碰数值；不能证明原生责任结束时 destroy 保持 BUSY。
- get_status 只观察原生事实并执行非阻塞 housekeeping；handoff 在 owner drive 完成。
  活跃入口沿用累计 dispatch/deadline，pure C 完成不创建新 JS ledger、不擦除最近结果。
  completion/timer/immediate/event 共 32 项；completion 内 work/process 通过独立持久
  cursor 公平轮转。已融合 08，保留完整 jobs 检查点、immediate cutoff、跨 pump cursor
  和 entry-only 完成即返回的边界；process 没有第五阶段或额外 32 项。

没有公开本票范围外的完整 cancel、pipe/EOF、JS process 模块、整树/温和信号能力。

## 真实边界与红绿

公开 C 源码为 `tests/native/sdk_process.c`；受控系统源为
`tests/native/sdk_process_faults.c`。没有包含引擎/Runtime 私有头或断言私有布局。
全部编译在 Ubuntu，Windows 仅消费相同 APE/SDK/PE 字节。可复用输入与工具链在主工作区
物理 `.cache/`，报告与中间候选在本树 `out/m2-13/`。

| 原件目录 | 实际事实 |
|---|---|
| `red/` | 首条 default-pipe 合同缺类型/符号真红，最小明确拒绝转绿；实际 spawn/reap 缺符号红色另存 |
| `slice4-invalid-stdio-red/` | 关闭 Host fd0 后，旧 waker 占用 fd0 导致错误接受 inherit；保留实际断言失败 |
| `slice7-stop-create-red/` | Linux/Windows 在 OS 创建调用处接受 stop，旧 SDK 随后仍成功发布 token；两平台真红 |
| `slice8-inherit-*` | 保留新增夹具 argv 索引错误及诊断；修正夹具后，任意字节 stdin/stdout/stderr 与 fd197/Win 文件对象隔离通过 |
| `ledger-red/` | 两平台 get_status 过早交接/解除 ref 的真红 |
| `ledger-green/`、`work-process-ledger-green/` | 累计 dispatch、原生观察/交接分离、pure C 保留最近 JS 结果通过 |
| `lost-pid-red/` | 真 WNOWAIT 后夹具抢先 waitpid，旧 SDK 随 stop 尝试 kill 已退休 PID；wrapper 阻止实际危险信号，断言真红 |
| `lost-pid-green/` | 不再 signal 旧 PID，helper 实际 join、进程槽继续保留；CLOSED 清理错误和 BUSY destroy 通过 |
| `read-fail-red/` | fork 成功后 exec 错误通道 read 注入 EIO；旧调用拒绝却未在 close 前结束 child，清理后仍保留红色断言 |
| `cleanup-cause-red/` | 已有进程清理错误后，quiesce 的 clock 故障覆盖首个 cleanup reason；真红 |
| `merged-faults-green/` | 失去 PID、清理首因、拒绝后 child 回滚、TLS、stop/create 全部通过 |
| `environment-budget-red/` | 默认环境快照与新 argv 合计超过公开 process_data_bytes，旧实现仍接受；真红 |
| `environment-budget-green/` | 合计输入预算修复；Linux/Windows 当前同字节 8 项公开 C 候选合同均 pass，原日志与 `*-cases.json` 保留 |
| `fourway-red/`、`fourway-red-corrected-fixture/` | 对保留融合前对象新增 immediate 场景，两场景真红；最终修正夹具仍红，排除夹具返回 token 对象的混淆 |
| `fourway-green/` | 首次融合夹具直接返回不支持桥接的 timer 对象，保留失败；只把安装函数改为返回 undefined |
| `fourway-green-2/` | 最终公开九模式同 APE Linux/Windows 全绿；四类调度及 ref child/unref timer/immediate 通过 |

当前 8 项候选覆盖：输入/复制、OS 拒绝与旧结果、独立 count/helper/handle/registration/
native storage 与默认环境合计预算、ignore/inherit、stop/unref、Runtime/generation、
累计 ledger，以及 40 个 process + 16 个 work ready completion 与 timer/event 的同轮
32 项公平交接。这些是增量链接候选，不是最终普通 SDK/ZIP 的新身份或完整 M2 长测。

受控 TLS 场景让 child 先实际退出/reap，helper 停留在 pthread TLS destructor。
0/25ms quiesce pending、destroy BUSY、helper=1/请求栈=1MiB/进程槽保留；放行后真实 join。
失去 PID 场景则是 helper=0/栈=0、进程槽=1，CLOSED 后 fd/HANDLE=0 仍不能声称 quiescent。
两种保留事实分别验收。Host 抢 reap/自动 reap 属外部干预限制；该受控用例故意保留一个
有限 Runtime 控制记录到夹具进程退出，不计入正常零积累生命周期通过。

## Windows 与 POSIX 的实测边界

Windows 完整替换使用供应的无 CRT `process-child.exe` 观察真实 Unicode OS block，
并验证 argv/cwd。没有用 Cosmopolitan child 的启动转换伪造环境一致；04 的差分仍有效。
inherit fixture 以文件 volume/index 身份判定额外 HANDLE，不按数值是否有效误判复用。

`final/std-route-verified/windows.json` 是额外 Windows 原生分路：Host C fd0 保持可用，
GetStdHandle 输入槽暂设 INVALID_HANDLE（真实 error 6），SDK 克隆 C fd0；child 实测为
FILE_TYPE_PIPE=3，随后原标准槽恢复、reap/quiesce/destroy 成功。`final/std-route/` 保留
最初极小 PE 夹具自身节布局空洞导致 error193 的失败；只给独立 PE 填齐节跨度后重验，
没有改产品或 SDK 迁就夹具。此分路绑定旧候选供应。
新 `closure/std-route-verified/windows.json` 已重新绑定当前供应：本次 parent/child 实测
FILE_TYPE_CHAR=2，一致；raw error 6、C fd0 仍有效、原标准槽恢复和完整清理通过。
两次实际类型分别保留，不把旧 PIPE 结果套用到新命令。

固定 archive 有 sys_waitid thunk，没有 public waitid wrapper。`waitid-abi/` 保存原始
双架构对象、archive/object SHA、实际反汇编：x64 分派到 systemfive，ARM64 保留 Linux
95 和 Darwin 173 选择。原 libc 源提交映射继续 `unproven`，不拿最新上游源码冒充固定版本。
调用只使用原生输出作为不解码的 opaque siginfo；Linux 第五个 rusage 参数为 NULL，
Darwin 原型是四参数。P_PID=1/参数依据 [Linux UAPI](https://raw.githubusercontent.com/torvalds/linux/master/include/uapi/linux/wait.h)、
[Linux wait 系统调用说明](https://man7.org/linux/man-pages/man2/waitid.2.html)、
[Darwin syscall 声明](https://raw.githubusercontent.com/apple-oss-distributions/xnu/main/bsd/kern/syscalls.master)
和 [Darwin wait 类型](https://raw.githubusercontent.com/apple-oss-distributions/xnu/main/bsd/sys/wait.h)。
Linux 真实系统夹具证明 WNOWAIT 两次后仍可 reap、EINTR/EINVAL/ECHILD 和 exit/kill 两序。
这些结果不外推为已执行的 macOS/ARM64/64K。

## 旧候选供应的保留范围

`out/m2-13/final/` 是合入 Work 之前的独立正常 producer：74 项 recipe，dirty=true 原样
保留；Linux 134 pass，Windows 133 pass + 1 既有可选 symlink blocked；密封 ZIP 包外
两平台各 5 条正常 C 合同，另有 Linux 6 / Windows 4 条受控系统合同通过，各项 scope
分别记录。原 M2 四个初始化故障阶段/uncertain fd/Notify 夹具通过。
Ubuntu check 为 368 总、191 条按其显式输入条件 skip，真实 C bootstrap 通过。
上述供应不能证明后加的 Work 融合/PID/环境预算/新公开目录。

## 当前最终本地供应与交接

实际合入 filefix `166ab50`、08 `6c8c0ea`，产品冻结 `6583ed37158f9a33039dc8aa1915fa76005dd72f`。
之后合入 root 明确的 `codex/m2-integration`：`5bf82a4`（文档）、`620b996`（独立 02 探针），
77 项 SDK 输入逐项不变。Git 名为 `main` 的旧工作线不属于此次集成，从未合入。

| 当前原件（均在 `out/m2-13/closure/`） | 适用实测范围 |
|---|---|
| `build.json` | 普通 Ubuntu producer，77 项权威 recipe，dirty=true 原样保留；SDK、双 archive、79 成员密封 ZIP、三 fat APE |
| `linux.json` / `windows.json` | Linux 162 pass；Windows 161 pass + 1 既有可选 file-symlink blocked；无必需缺项 |
| `package.json` / `package-windows.json` | 两平台各 39 pass；只从实际 ZIP 在源树外编译应用/Work/timer/immediate，Windows 不重编 |
| `fault.json` / `fault-windows.json` | 原 M2 和 Work 两组系统故障，两平台各 2 pass，保留四个初始化 stage/uncertain fd/notify/timer/work 范围 |
| `process-package-build.json` | ZIP 在 POSIX `/tmp` 两源树外解包，只用供应公开头和两架构 archive；编译 Process 正常/系统夹具，消费前后原字节相等 |
| `process-package-linux.json` | 公开 9 pass + 受控系统 8 pass；包含真实 WNOWAIT/EINTR/EINVAL/ECHILD、exit/kill 两序、PID 退休、拒绝后回滚、TLS gate |
| `process-package-windows.json` | 同字节公开 9 pass + 适用系统 4 pass；无未执行 POSIX 系统夹具的 Win 成绩 |
| `std-route-verified/windows.json` | 当前供应的 C fd0 与原生 GetStdHandle 分路、恢复、reap 和完整清理 |
| `devcheck.json` | Ubuntu 权威 check：369 总，177 执行通过、192 按显式输入条件 skip；真实 C bootstrap 通过；M0 Core 保持 not_run |
| `02-narrow.json` | 新合入 02 探针的 Python 窄检查：7 总，2 执行通过、5 按显式输入条件 skip；不冒充 02 的原生平台成绩 |
| `imports-*.json` | 实际消费/包外/系统/std-route/check 驱动的 repo imports 前后身份一致；原 producer 的 77 项 recipe 自身前后复核；额外 ci_identity.py 与产品/tip blob 一致 |
| `source-supply-verified.json` | Raw/Git product blob/Git tip blob/LF 与供应逐项一致；ZIP/SDK/loader 前后核验；两 archive 各实际 15 个 ELF64 little-endian REL 成员，机器 62/183 |

九个公开 Process 模式为 scheduling/liveness/identity/ledger/limits/inherit/inputs/errors/cleanup。
共享调度首 pump 恰为 14 Process + 15 Work + timer + immediate + event = 32，下一 pump
交完其余；事件槽饱和不丢 native 完成。ref child 允许 ready unref timer/immediate，child
unref 后自然 IDLE 不交付，显式 pump 再交付；本票没有 JS process API。

当前 recipe `4b8e39bad12fb43e70745950b4d519e673ac746435160d6d38dd6a9945fe9040`，
SDK `7526066d584e4564a15b80c3e732735a77beb49702e31221e7789783c815418e`，
ZIP `7d9cabfc64f5cbaab32a806d09da4b825c6b638dde293289d7ed3deafb684e49`（16,247,317 bytes）。
双 archive 与全部 APE/loader 的完整 SHA、原命令、实际宿主及日志由上述报告保存。

当前本地 handoff 为 `closure/handoff.json`。本票不签发新的 macOS/ARM64/64K、完整
cancel/pipe/JS process、父验收组或 release-ready；旧红灯、旧供应与候选映射各保留其原范围。

## 2026-11-04 · B02 纯 POD 查询与 Mac 失败诊断阶段

新树 `.cache/worktrees/m2-13-cross-fix` / `codex/m2-13-cross-fix` 从实际集成
`5c2f019` 建立，旧 `m2-13` 源码和输出冻结。实现 `6e34aeb` 先供应阶段候选；
随后实际融合含 11 的集成 `6b0de319`（merge `f6159b6`），最终产品冻结
`0b725ccf4e413cf55f6250c428ff4b5aab1af829`。没有使用旧 `main`。

新增 `qaplet_get_process_status`，任意普通线程只在 mutex 内检查 Runtime/slot/
generation/released 并复制既有 status POD；不执行 collect、join、owner handoff、JS
或通知。旧 `qaplet_process_get_status` 保留 owner 原生观察/非阻塞退休语义。
首次 slots 发布移到同一 mutex 下；POD 布局、既有 code/domain/能力位不变。
HostNotify TLS 仍先拒绝 BUSY，CLOSED 的 token 已失效；destroy 前宿主仍须 join 调用方。

旧密封 SDK 的原最小场景在正常 Ubuntu/Windows 重新真实执行：worker 查询返回
WRONG_THREAD3，owner/aggregate/lifecycle 都是 OK0；真实 child exit37/reap/release/
quiesce/destroy 完成。`out/m2-13-cross-fix/b02-red-{linux,windows}.json` 保留 Linux
exit1 与 Windows exit256，不改其原始退出值。新永久 C 合同先以旧 archive 实际链接，
缺少新查询符号的 red 保存在 `query-api-red.json`。B02 的 owner guard 原因已有公开
真实红与明确源码，沿用该已证诊断；没有再造不相关候选假设。

永久 `tests/native/sdk_process.c` query 模式加入 HOST_CASES 并随 ZIP 供应，验证任意
线程/交叉 Runtime/旧 generation、release 竞争、无 collect/交付/JS、HostNotify BUSY
和 CLOSED 失效。阶段 `6e34aeb` 的 ZIP 在 `/tmp` 包外编译后，同 APE 两宿主各 1/1
绿；`stage1/` 原件保留。最终新夹具在无 collect 断言之后用既有 owner 观察入口确认
helper 物理退休，再测 one-slot 复用，避免合法 tryjoin 调度差异成为夹具误判。

最终原件均在新树 `out/m2-13-cross-fix/final/`：

| 报告 | 本阶段实际范围 |
|---|---|
| `build.json` | Ubuntu 普通供应；77 RECIPE、79 ZIP 成员、三 fat APE、双 archive |
| `linux.json` / `windows.json` | normal170 pass / 169 pass + 1 既有可选 symlink blocked |
| `package.json` / `package-windows.json` | 两宿主各46条密封 ZIP 包外 application/work/timer 合同 |
| `fault.json` / `fault-windows.json` | 两宿主各2条受控 core/work 系统故障组 |
| `process-package-build.json` | ZIP 在 `/tmp` 两源树外解包，只用供应头/archive/source 编译 |
| `process-package-linux.json` / `process-package-windows.json` | 永久公开 Process 各10；独立系统 seam Linux8/Windows4；同 APE 字节 |
| `devcheck.json` | Ubuntu check370；177实际通过、193按显式输入条件 skip；真实 C bootstrap pass |
| `handoff-002.json` | 77 raw/Git blob/LF、三 APE、双 archive各15个ELF64 REL机器62/183；623唯一命令、1246 raw logs、18父解释器实际repo Python闭包 |

配方 `39db413fbce0b034c8ff790065b6625025b146896e8e43ece96597a80f4c39bb`，SDK
`552288cdfe56a2eaf44d5d327e11e7f5c58b20b4daef589884af909726a256e0`，ZIP
`fdc87eeb691a6fe45f2e714659f5f128aa36f7dd0ad661a4047fb563d60127a9`。
报告嵌实际 driver/父解释器 imports 执行前后身份。子 Python 的 argv/raw 日志与 RECIPE
来源另核；父 profile 不声称捕获子进程动态 imports。`execution-inputs-before/after.json`
对最终消费者前后的370项全部 tracked 源码与本任务 helpers 静态 superset 核验相同，
与实际动态观测分开。首次收口脚本误用 `files.file` 字段的 KeyError 保留在
`handoff.imports.json`，旧脚本为 `audit-001.py`；改读真实 `files.path` 后独立002通过。

Mac 的 `453fe76` / Full41817613432 仍为两原生目标共16个必需 Process 失败，
不是本地通过范围。已在修改 Mac 行为之前向 root 公布可证伪排序：hard RLIMIT_NOFILE
大于 INT_MAX 的提前拒绝、fork/exec 原生拒绝、helper pthread 建立失败、已接受 child 的
loader/waitid 失败。当前只给原来预期成功的 spawn 增加失败诊断：实际 return code、
status code/domain/error/helper_active 与原生 getrlimit soft/hard/result/error。
不再从断言时的旧 errno115 猜错误域；没有改 spawn_posix/继承上界/白名单或等待判据。
下一次原生 Full 取得这些事实后才选择 Mac 产品修复。13保持 claimed/in_progress，14
未放行，整体 Review 未派发，也未签发父 M2、最终 T03 或发布完成。
