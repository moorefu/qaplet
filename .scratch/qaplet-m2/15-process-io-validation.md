# 15 · C Host owned Process pipe I/O

状态：`local_complete / cross_deferred`。本地 Linux/Windows 实现与供应验收完成，
支持后续本地开发；保持 claimed，不签发 Cross/Full、实际 64K 或 release-ready。

原件物理根为
`D:/Workspace/dairn/qaplet/.cache/worktrees/m2-15-process-io/out/m2-15-process-io/`。
以下文件名均相对该目录；旧报告、失败、编译失败及对应 raw logs 不改写。

## 实现与共同主干

分支 `codex/m2-15-process-io` 从实际 integration `d2c86d7` 的干净 LF 树开始。
首个公开默认三 PIPE red 链接原密封 13 SDK，真实返回 UNSUPPORTED=6。red helper
误写期望值 3，故其聚合状态保留 unexpected；真实 C 失败与退出 1 不改标。

`ce75a4c` 保存公开默认 PIPE 测试；`c75304b` 是 14 初版 `4d08aa0` 的明确
cherry-pick 来源，非 14 最终验证。`048ca9c` 是首个默认三 PIPE 与 binary I/O
Linux/Windows green 纵向片。`4626a5f`、`ee2c425`、`61e7520` 逐步加入
权限、共享额度、背压、EOF/exit、失败及包外 consumer。`1dc3444` 实际融合 12 与
14 exact `5724f4e`，保留完整 JS Work/Promise/jobs/ref 语义与 14 所有 native cases。
Windows 已退出 child 的 TerminateProcess 竞争修复唯一归属 14 的 `5724f4e`；
15 没有另存第二套终止/reap 责任。

`2ccfe3e` 补齐 helper I/O wait 错误的终态和真实完成责任。最终 supplier 的产品
源码是 `74906ff4f955bc984471756436bc259d90addddd`，已融合实际 d3a7305 的 CI P0。
随后自然融合 root `ce9cd7e` 得到 `1c2bf24`；新增 CI gate/合同/文档没有改变
这份 SDK 的 80 个 RECIPE 文件，逐文件当前 raw 与 supplier 摘要已核对。

新增独立版本化端点/operation/status/result/snapshot，cap bit 23
`QAPLET_CAP_PROCESS_IO`、error 30 `QAPLET_IO_ERROR`；M1 布局、reserved 和旧错误值
不变。`qaplet_operation_reserve_locked/retire_locked` 是 Work 与 I/O 共同的
128 operations / 8MiB data 私有 seam，字节仍在 16MiB native 总额内。Work 的
worker/JS 状态保持私有；完成和取消不 malloc、不占业务事件槽。

POSIX owned pipe 使用 nonblocking owner poll。Windows 使用真实 named pipe 的
overlapped parent、同步 inherited child ends、有限 helper/native event 与
CancelIoEx；不将 pipe HANDLE 交给 socket poll，也不短轮询。受管理 stdio 的
结束与 handoff 纳入 process ref/release 条件，child exit/reap 不丢剩余输出。

## 关键真实 red/green 与限制

- `red-defaults.json` 保存原 SDK 默认 PIPE 拒绝；`defaults-linux/windows.json`
  与 `bytes-initial-linux/windows.json` 保存首供应三 PIPE、NUL/0xff、独立短读、
  BUSY、先复制写入、finish、退出后 EOF/ref 的真实 green。
- `probe-windows-after-issue.json` 保留 65536-byte pending write 的真实反例：
  peer 在 issue 后读到新 payload 2048，取消后 GetResult=995/0，不能把 0 当无副作用。
  quota probe 显示 outbound server query error5，而仍 owned 的 read client 查询到
  server output quota=4096。产品据实际查询值分块；parent 的临时 child ends 在
  spawn 继承完成后关闭，不多留 read 副本抑制 broken pipe/EOF。
- `probe-windows-partial-pending.json` 证明 quota-sized 4096 请求 pending 后，peer
  再读旧 payload 1024、未整空时取消，GetResult995/0，剩余旧 payload1024；另一个
  4096 请求实际完成后已读2048加剩余2048与 native count4096一致。
- 初始失败写入夹具误以为 64KiB 必有部分写；该失败保留。128KiB 本地旧 SDK 实际
  red 于 Host SIGPIPE handler 被触发，修复 thread-local mask/consume/restore 后
  原 handler、原 mask、调用前 blocked+pending SIGPIPE 均保留。永久普通失败夹具
  改为 child 实际 close stdin 后再发布 R，不依赖 Linux pipe 容量；精确 partial
  prefix 由独立系统 seam 真实写1024后注入 native error 验收。
- `fault-gated-red-windows.json` 保存真实 child GetExit37 已返回、helper 发布前
  gate 的旧 SDK close27/domain2/error5；14 exact 修复与最终 I/O 配方都保留实际
  exit/reap/close 唯一责任。实际分配拒绝后 child 未在 owner 调用处取消的 red 则
  由 15 修复：失败入口立即开始 native shutdown，不等下一次 pump/close。
- `fault-wait-red-windows.json`、`fault-wait-notify-red-windows.json` 保存当前旧供应
  event wait 失败仍 PENDING、外部 Host 收不到稳定完成的 red。最终 helper 保留
  ordinary IO_FAILED/native error，取消后继续等实际 kernel completion 并通知 Host；
  event/data 在完成证明和 join 前不退。外部 manual event 的 reset/POD-recheck/
  wait 交接不靠 sleep 或 owner 周期轮询。
- Windows 自然循环32次未必复用 HANDLE 的夹具失败保留。最终采用 14 同类真实
  close 后立即持有 OS 复用 event 的协议；POSIX 则真实复用该 fd 为 Host pipe，
  预置 Z 并在旧 token、迟到 cleanup、helper join 后读回。新 child 的 R 也独立
  验证，不能用只改 generation 代替真实数字复用。
- 背压 consumer 曾假定 release 即归还 Win helper；该失败保留。最终按公开物理
  保留合同对 RESOURCE_LIMIT 做有界 owner collection/yield 重试，不释放仍活的
  helper/data。1MiB pull、独立 stderr、每 pump 4096-byte 收紧额度及内存界均实跑。
- 首个完整 supplier 的新 POD 负例误期望 INVALID_ARGUMENT，单项
  `abi-fixture-mismatch.json` 保存失败；按既定 ABI_MISMATCH 修正为 `61e7520`。

## 最终选择

只选择 `build-final-3.json`、`final-3-artifacts/` 及对应 normal/package/fault-3
报告。此前 `build-final.json`、`build-final-2.json` 的真实成功和失败仍保留原范围，
不替代新的 wait-error/native-reuse 配方。

| 原件 | 实际结果与范围 |
|---|---|
| build-final-3.json | Ubuntu-22.04 producer、完整密封 ZIP/双 archive/三 fat APE |
| normal-3-linux/windows.json | Linux 186 pass；同字节 Windows 185 pass、既有 optional symlink 1 blocked，required gaps 0；两端各生命周期 1000 次 |
| package-3.json / package-3-windows.json | 两端各 72 pass；ZIP 源树外仅公开 include/archive/source 编译与同字节消费 |
| fault-3.json / fault-3-windows.json | Linux 16 pass、Windows 17 pass；独立 core/work/process/io 系统配方，不混入普通成绩 |
| devcheck-3.json | 规定 check：402 项中 202 实际通过、200 条件 skip，真实 C bootstrap pass；不与产品平台成绩相加 |
| handoff.json | pass；80 RECIPE raw/product blob/current blob/LF、ZIP 80 成员、双 CPU archive 各 16 ELF64 REL、全部 scope 父 Python/helper 前后一致及 1448 raw logs |

供应身份：产品提交 `74906ff4f955bc984471756436bc259d90addddd`；SDK recipe
`15add7c17ef0ab707c130182b877eaa11832503ca4fa7b82e8d749d555ded22c`；SDK
`5a0930e176892c3282fbaaedd43c0a57171f95b7b762932b8b5f6cb237a3d06d`；原 ZIP
`f55934e1ae773f95b4215860381fe6d6c3ed7ca2c4be7cf3c281cd0fabc842a9`。
Linux 实际为 Ubuntu-22.04/WSL kernel `6.6.87.2-microsoft-standard-WSL2`、x86_64、
4096 页、uid 1000；Windows 为本机 x64。两端均正常原生执行，不是模拟平台成绩。

`handoff-pre-doc.json` 保留交接文档提交前的首份审计；最终 `handoff.json` 只对
文档提交后的实际 HEAD 再核同一供应及原报告身份，不重复编译、运行或改写旧 scope。

每次 scope 先冻结 helper，静态列表来自当前 repo 的全 tracked Python，加实际本地
helpers，运行前后逐文件记录；另记父进程真实 imports/called code。没有对子 Python
动态 imports 作 profile，不冒称该范围。Windows 只消费 Ubuntu 供应字节；原件
保留实际 OS/kernel/CPU/页大小/argv/退出/timeout/清理事实。

后续缺口保持：其他必需原生目标、当前实际64K产品、CI/Full、跨资源最终组合长测。
本票不实现 JS Process/stdio/TTY、不签发父 M2 或 release-ready，不 push/触发 CI。

## 独立本地合并接收

2026-11-05：独立merger审查source `37bbde4470f1bf3418ce663f491700d591eb93ba`
相对实际root `ce9cd7e41fed5361c54a9112d6fccccd15ee1f21`的21文件增量，未发现阻止
本地接收的产品问题。以每命令`core.autocrlf=false`执行no-ff合并，得到
`65de84e2cd86104f4249211ec06bcc7a352eafd1`；其合并树与source tip完全相同。

复用supplier原审计（重定向输出以保留原件）、`sdk.check_package`、
`sdk_matrix.validate_native`和`build_ape.headers`独立核对：80配方raw/Git/LF、
80原ZIP成员、双架构各16个ELF64 REL、三fat APE头、1448份raw日志及当前普通/
包外/故障catalog均通过。合前与合后均通过，未重建SDK或重跑全suite。
合后实际Ubuntu-22.04仓库preflight及共享`.cache/quickjs`锁定源码verify通过。
主交接与检查原件在`out/qaplet-m2/merge-15-process-io/`；重放helper曾因两类既有
case结构的字段假设失败，两次失败stdout/stderr保留，修正只涉及主out helper。

接收范围仍为`Local development: complete / Status: claimed / cross_deferred`。
其他原生目标、当前实际64K、CI/Full、最终跨资源组合长测和发布就绪均保持待验；
源worktree、全部旧输出与失败保留，未启动16/17、push或触发CI。
