# 13 · C Host 显式 spawn 与实际退出/reap

2026-11-04。票保持 `claimed / in_progress`。这是中间候选记录，尚未最终交付；
root 将先合入 10 的 Windows 文件夹具修复及 08 的四类调度，再 followup 本工作树完成
最终 SDK 重供、完整检查与 `awaiting_cross` handoff。不能用本页局部结果关闭父验收组。

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
  当前 completion/timer/event 共 32 项；completion 内 work/process 通过独立持久 cursor
  公平轮转。08 合入时须保留这一 provider 层，不把 process 再分一个阶段或额外 32 项。

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
没有改产品或 SDK 迁就夹具。此分路目前绑定旧候选供应，最终重供后还须重新绑定。

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

## 旧候选供应与仍需完成的交付

`out/m2-13/final/` 是合入 Work 之前的独立正常 producer：74 项 recipe，dirty=true 原样
保留；Linux 134 pass，Windows 133 pass + 1 既有可选 symlink blocked；密封 ZIP 包外
两平台各 5 条正常 C 合同，另有 Linux 6 / Windows 4 条受控系统合同通过，各项 scope
分别记录。原 M2 四个初始化故障阶段/uncertain fd/Notify 夹具通过。
Ubuntu check 为 368 总、191 条按其显式输入条件 skip，真实 C bootstrap 通过。
上述供应不能证明后加的 Work 融合/PID/环境预算/新公开目录。

恢复本票后必须合入 root 实际 filefix/08 tip，保留 completion provider 和四类主 cursor，
重新生成普通 SDK/双 archive/ZIP/三 APE，执行完整目录、包外正常/故障、原故障及 std-route。
逐项核验 Raw/Git blob/LF recipe 与供应字节，执行当前 Ubuntu check，追加最终身份和
handoff 后才标为 `claimed / awaiting_cross`。新的 Cross/Full/64K 由 root 独立安排；
未完成项、旧失败、旧源码/配方及先前真实平台成绩各保留原范围，不签发 release-ready。
