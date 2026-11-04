# 02 · Windows failed-connect 分路回归

2026-11-04。实施分支 `codex/m2-02-win-error-fix`，独立 worktree
`.cache/worktrees/m2-02-win-error-fix`。开始时将 `4375333` 快进到实际 integration
`166ab50b1f0d4ef6bfb17aa856791047cbdb421c`。本记录只涉及 OS 探针，未更改 Runtime、SDK、CI 或支持能力位。
本地修复验证已完成；新的五原生 Full 由 integration 独立验收后决定，02 保持 claimed / awaiting_cross。

## 原始失败与范围

Full [41799595408](https://github.com/moorefu/qaplet/actions/runs/41799595408)，attempt 1，
提交 `867356c15a2ad0b9206b733b4441942f3aa83bd2`，真实 completed/failure。
Windows waker 唯一失败为 `socket.connect-error`：
`connect=-1/errno=115, poll=0/2000ms, revents=0, select=-2, SO_ERROR=10061, connection_failed=false`。
`reserve_closed_before_connect=true`，目标端口 50630。其余 18 项通过。

原 ZIP、provider API、逐 job 原始日志、producer、供应 APE、native 报告与 raw stdout
保留在主树 `out/qaplet-m2/full-867356c/`；完整指针见其 `HANDOFF.md` 和
`verify/windows-failures-002.json`。本次没有改写原件，也没有将独立 SDK file fixture
失败混入探针修复。`196cd75` 原六目标各 19 项验收、Runtime 06 和 Timer 07 的既有范围保留。

以下新输出均在本 worktree 的 `out/m2-02-win-error/`。所有构建在 Ubuntu；正常 Windows
用户上下文仅执行同字节 APE。受限上下文未用于此次产品通过结论；无输出不推断 pre-main。

## 已执行的最小反馈与排序假设

1. 原 CI producer 直接配本地非 CI consumer 的公开 Python `run` 正确拒绝身份不完整，
   没有通过伪造 CI 环境绕过；该失败保存为 `original-001.json`。
2. 正常 Windows 直接消费原供应 APE 的公开 `--suite` 三次，均 19/19。
   每次 `poll=0`，原分路随后 `select=1, writable=true, exceptional=false, SO_ERROR=10061`。
   本机三次绿灯不能否定 Windows Server 的原始失败。
3. 从原源码移除其它 waker、成功连接、复用等步骤，只保留单个 nonblocking loopback
   failed-connect、关闭预留端口和清理。诊断副本在 poll 后、首次 SO_ERROR 前用真实 select
   固定“连接已完成”的系统调用顺序，不读取 SO_ERROR、不构造 readiness。
   `minimal-native2/feedback.json` 三次均红，约 2 秒/次，原字段与 CI 同形：
   `poll=0, socket_error=10061, select=-2, connection_failed=false`；闸门 select 实际为 1。
   去掉该闸门即回到原字节在本机三次绿灯的时序。

一条已执行的 unattended 反馈命令（正常 Windows 用户上下文）为：

```powershell
python out/m2-02-win-error/driver.py out/m2-02-win-error/minimal-native2.driver.json python out/m2-02-win-error/feedback.py out/m2-02-win-error/minimal-red2/connect.com out/m2-02-win-error/minimal-native2 3
```

实际外层 exit 1、三次 APE exit 256、三次 `passed=false`、清理通过，driver
`unchanged=true`。这是新真实 OS 受控顺序执行；它复现错误状态组合，不声称重放了 CI
Server 的具体调度。最初将 gate 放在 poll 之前的候选使 poll=1，未复现，原件仍保留于
`minimal-red/`，不算红灯。首个反馈 driver 因诊断行无 status 的 KeyError 也保留。

在变更生产分路前，向用户列出并依次检查四个假设：

| 排序 | 可证伪预测 | 实际结果 |
|---|---|---|
| 1 | 首次 SO_ERROR 已非零会跳过 select；独立于该值选择等待应消除同形红灯 | 只去除旧条件的 `!got && !socket_error`，三次均真实 select=1、绿灯 |
| 2 | SO_ERROR 读取清除或改变后续 readiness；交换先后会改变集合或错误值 | 两个 APE 顺序变体均三次绿灯；本机两次错误读取仍 10061，未证明“清除导致该回归” |
| 3 | 包装 select 与原生 Winsock select 的集合不同；独立相同 fixture 应呈现差异 | APE 为 writable=true / exceptional=false；CPython 原生 Windows socket/select 为 false / true |
| 4 | 其它 listener 抢占释放端口；应观测成功连接或零 SO_ERROR | 所选控制执行均非零真实拒绝错误，不支持该原因；仍保留端口与释放事实，抢占不能伪造通过 |

两个顺序变体及其源码/构建/raw 在 `select-after-read/`、`select-before-read/`；原生对照在
`native-order.json`。该对照使用独立 socket，不冒称与 APE 是同一个描述符。

## API 与源码边界

[Microsoft Winsock select](https://learn.microsoft.com/en-us/windows/win32/api/winsock2/nf-winsock2-select)
规定 nonblocking connect 失败应在 exceptfds 观察，随后读取 SO_ERROR。
[Microsoft getsockopt](https://learn.microsoft.com/en-us/windows/win32/api/winsock2/nf-winsock2-getsockopt)
将 SO_ERROR 与 WSAGetLastError 的线程错误明确区分，并描述了返回/清除 socket 错误的合同。
本机实际重复读取保留 10061；没有以文档描述覆盖实际结果或据此外推其他 Windows 版本。

固定上游提交 `aefad27c09ffda64c6ac64bd69f758e9f9c7b91d` 的
[select-nt.c](https://github.com/jart/cosmopolitan/blob/aefad27c09ffda64c6ac64bd69f758e9f9c7b91d/libc/calls/select-nt.c)
经 sys_poll_nt 转换 fd 集合；
[poll-nt.c](https://github.com/jart/cosmopolitan/blob/aefad27c09ffda64c6ac64bd69f758e9f9c7b91d/libc/calls/poll-nt.c)
的 socket 路由使用 WSAPoll；
[getsockopt-nt.c](https://github.com/jart/cosmopolitan/blob/aefad27c09ffda64c6ac64bd69f758e9f9c7b91d/libc/sock/getsockopt-nt.c)
还涉及 Windows 错误转换。三份原文、Git blob SHA 和 URL 保存在 `primary/`。
锁定 Nightly 与完整 libc 源提交的映射仍为 unproven；这些源码只作解释线索，
不签发“锁定 Nightly 必然执行了该源码”的结论。

因此本次记录实际 `Cosmopolitan select` 包装入口，不称之为 Winsock 原生 select。
`connect_errno`、poll/select/getsockopt 调用 errno 与 `SO_ERROR` 的 raw 输出分列，
不按 10061 等数字猜测 POSIX errno，也不归并到 SDK 错误域。

## 修复与先红后绿入口

根因是探针分路错误：poll 返回 0 后，首次 SO_ERROR 若已非零，旧条件会跳过 select，
而相同用例仍要求真实等待就绪才能通过。错误已经到达的时序反而使探针失败。
修复使 poll=0 时始终执行原显式 select 路由，然后单次读取 SO_ERROR；不让错误值决定
是否执行等待，不扩大 2000ms/5s 时限，不直接设置 connection_failed。
判据仍要求 connect=-1、非零真实 SO_ERROR 及实际 poll/select readiness；预留端口仍先关闭。

公开 `--probe-mode connect-error-ready` 只增加真实完成闸门，输出明确的 gate 原始集合，
case 标记 `controlled-system-order`；正常 `--suite` 不启用它，仍为原 19 项。
新增测试从公开 Python CLI 消费 Ubuntu producer，检查错误、等待、gate 和描述符清理，
不访问内部布局。正常 suite 的 `waker.control-failure` 仍保留独立系统故障标记。

修复前，仅新增 gate 与回归测试的公开 producer 位于 `public-red/`。
公开 `run --probe-mode connect-error-ready` 与新增测试均实际红灯：gate_select=1，
原 select=-2、SO_ERROR=10061、connection_failed=false。测试 raw 在
`public-red-test.driver.{json,stdout.log,stderr.log}`，完整 consumer 原件在 `public-red/windows.json`。
不是通过改造报告来强制 red。

## 最终验证

正式 producer 为 `final/build-info.json`，构建时观察到的 HEAD 为 `166ab50`，
source/build 字段记录实际修改字节而不声称 clean checkout；没有重写这个观察值。
APE 为 977602 bytes，SHA-256
`c03fb4f5109ec271fd987ac2b070141b8ac2017567f591d855ffe3989880bd89`；
source 摘要 `33528c9d4ccef995a8a04d4dffee4ded256844a638fcc536d87cfaa0bab898c4`；
build 配方摘要 `0b9ebbaf1bff295165b12a498fa64859f4b35ef2cdd6e2a43c0489a5a287309a`。
构建前后原锁、原 ZIP、完整工具链、官方 assimilate 派生树和独立 helper 均由既有 producer 实际核验。

| 原件 | 实际成绩 |
|---|---|
| `final/linux.json` | Ubuntu/WSL2 x86-64，4096，正常 suite 19/19 |
| `final/windows.json` | 正常 Windows 11 `10.0.22000` x86-64，4096，正常 suite 19/19 |
| `final/windows-gate.json` | 同一正式 APE，控制完成先于错误读取：19/19；gate_select=1，生产 select=1，SO_ERROR=10061 |
| `final/guest-64k.json` / `final/arm64-64k.json` | 完整系统 Linux ARM64 `6.8.0-147-generic-64k`，实际65536页，19/19；公开 consumer 重核通过 |
| `final-contracts-windows.driver.*` | Windows 7 项公开 CLI 合同全部通过，0 skipped |
| `final/dev-check.json` | 初次 Ubuntu `dev.py check`，370 tests、188 既有 skip；本票7项均实际通过，CI合同和真实C bootstrap通过 |
| `final/dev-check-integration.json` | 合入 `6c8c0eaffa058f59131be35cb540206bde1be582` 的08新源码后再执行必需 check，370 tests、188既有skip、本票7项及bootstrap通过 |

64K 固定 QEMU 6.2.0、`virt-6.2,gic-version=3`、`cortex-a72`、`tcg,thread=single`。
guest 实测 ARM64/65536、内核、页大小别名与 smaps 一致，payload 前后相同；QEMU 正常退出、
无 timeout、RAM-only、无磁盘与外部网络，未改 host global 状态。全部正常 suite 的 socket 清理、
waker 7 个 helper join、live_helpers=0 均实际通过。TCG 不外推物理 ARM64 性能。

08 合并只影响 SDK/调度文件，没有改变本票九个 probe recipe/source 文件；其后再合入
`e742c7694928b1d7e35d373531ff94b8008f0265` 的单一 implementation 文档记录。
本票没有修改公共 README 或 implementation。源码与公开测试的最终提交、当前 Git blob、
producer 原始字节、LF、全部仓库 Python、执行 driver 前后身份和 final 原件清单由
`final-closure.json` 独立核对；没有在旧报告中补写新签名。

`driver-v1.py` 保留前期 driver 原字节；后续 driver 还显式纳入诊断脚本与只读 linked-Git shim。
各 driver 在执行前记录仓库源文件，执行后核对；前期诊断输入身份另由各 build/feedback 保存。
throwaway 变体均明确留在忽略的 `out/m2-02-win-error/` 诊断目录，未进入产品配方；没有遗留
`[DEBUG-...]` instrumentation。`git diff --check` 通过。

新五原生 Full 尚未执行，本机 Windows 11 不外推 Windows Server 的唯一调度。
独立 merger 应先核验本树交接与同字节配方，再由 integration 新 Full 原件决定本票重新 resolved。
