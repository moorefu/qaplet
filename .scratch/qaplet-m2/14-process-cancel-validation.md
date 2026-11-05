# 14 · 直接子进程取消本地验证

2026-11-05，按[本地开发安排](local-development.md)完成 Linux/Windows 实施与验证，待独立 merger 合入后登记 Local development complete。票 14 保持 claimed，完整 Cross/Full、其他原生宿主、组合长测与父 M2 未完成；没有 push、触发 CI 或整体 Review。

## 产品合同

新增公开 `qaplet_process_cancel`：任意线程、不进入 JS，接受锁排序取消与原生结果发布。取消先接受后 status.code 为 `QAPLET_CANCELLED`，重复为 OK；结果先发布返回 `QAPLET_ALREADY_COMPLETED`。实际 exit/signal/native error、reap、owner 交付和 helper 退出仍独立报告。保留旧结构、能力位22及原错误数值。

显式 cancel、owner 确认 STOPPED/FAILED、close 共用原有 force termination / WNOWAIT 保持 PID 身份 / 锁内 waitpid / 真实 tryjoin 责任。取消不关闭输出管道、不承诺整树或温和信号。release 在 reap 前 BUSY；helper 尚在 TLS 析构时，release 已使 token 失效，槽、helper、栈与原生数据仍保留。失去 PID 身份后禁止再次向该数字 PID 发送信号。

`sdk.py package-consume` 现在实际从密封 ZIP 在源树外编译并运行 Process consumer；`fault-consume` 单独编译带 syscall wrappers 的 Process fixture。没有把打包源码算成已执行。

## 正式本地原件

全部输出位于独立 worktree：

`D:/Workspace/dairn/qaplet/.cache/worktrees/m2-14-process-cancel/out/m2-14-process-cancel/`

Ubuntu 映射是 `/mnt/d/Workspace/dairn/qaplet/.cache/worktrees/m2-14-process-cancel/out/m2-14-process-cancel/`。可复用缓存保持主仓库 `.cache/`；既有 `out/qaplet-m2/13-nofile-fixture-fix/` 原件只读消费，未改写。

产品/配方源码提交 `3462fe3ec18b2e3ee0072a436a4ff7d8fc2da03a`，首个实现提交 `4d08aa0`，基于 integration `d2c86d7539792d1aef983031ef034b7d63d32794`。SDK aggregate 为 `b4547345ebce13f35726ac3f9349e02baf71f032040ccf403ca2cc7bbde6d831`；配方为 `3ed0765686e2fbace407112251ac41775b0ec0ab5a3e3eec1e377609ff785c98`。77项 RECIPE、79项 ZIP成员、两个目标 archive、供应 APE/sidecar/loader 的实际身份保留在 build/report/manifest 与交接审计；不将成员数写成通用常量。

| 原件 | 实际范围与结果 |
| --- | --- |
| `red.json`、`red-compile.*.log` | 在 Ubuntu 仅用旧密封 SDK 的公开头/archive编译新消费者，实测缺 `qaplet_process_cancel`；预期红，不是产品成功 |
| `build-first.json`、`first-green.json` | 首条实现正式供应后，公开 any-thread cancel / completion-first / generation与Runtime隔离 / 原生force与reap 转绿 |
| `preflight-final.json`、`build.json` | 最终预检与 Ubuntu 正式供应通过；源字节与 Git blob/LF 在交接审计核对 |
| `linux.json` / `windows.json` | 172pass / 171pass+既有 optional symlink blocked1；含新 cancel 与 terminal 两个公开 catalog 用例 |
| `package.json` / `package-windows.json` | 各58pass，包含实际包外 Process 12项；Windows只消费Ubuntu同字节APE/child，无构建 |
| `fault.json` / `fault-windows.json` | Linux12pass / Windows9pass；其中Process为10/7，另有既有M2/Work两项；fork/read/wait身份故障3项是显式POSIX-only系统fixture |
| `devcheck.json` | 规定的一次 `dev.py check` 通过；380项=180pass+200条件skip，真实系统C bootstrap通过 |
| `handoff.json`、`merge-final.json` | 源/配方/原ZIP/公开archive、报告与raw日志、Python静态与实际父进程闭包、最终融合实际integration的只读核验及真实merge命令 |

公开 terminal 用例在 PENDING、普通 JS 错误、STOPPED、FAILED 四种顺序下核对原生清理；STOPPED/FAILED 必须在 close 前观察实际强制退出/reap，首因保持；已排队 jobs/finally 的 Host 副作用始终为零。

独立系统 fixture 用真实 pthread 入场 gate 扣住 helper，观察已接受 CANCELLED 但尚未 reap、release BUSY；再让真实 wait/reap 完成并扣住 TLS 析构，观察 reaped但helper_active、release后token失效但容量仍占用、0/有限quiesce可重试与destroy BUSY；最后放行析构，以真实tryjoin和零原生量确认退出。native terminate error保留在操作状态及清理诊断，实际自然退出37不被伪造成kill；真实send故障发生在取消接受后，返回仍OK，CONTROL_FAILURE首因独立且不被stop/close覆盖。

Windows实际 HANDLE 复用在 `__wrap_CloseHandle` 的真实 close 成功之后、helper发布及TLS分配之前取得并持有同值真实事件HANDLE；旧公开token取消为INVALID_HANDLE，新事件仍能等待/置位，迟到helper与quiesce正常完成。最多8192次有界分配，未伪造HANDLE或改Runtime私有布局。POSIX另运行真实丢失PID身份/禁止再信号fixture；该故障特意保留CLOSED/BUSY ownership到fixture进程终止，不声称成功destroy。

## 输入闭包与历史失败

最终 build、normal、package、fault、devcheck scope开始前已固定 helper。guard 使用相对 checkout 的排除规则，静态集合包含全部79项tracked Python（其中35项tests/contracts）和scope helpers，最终各scope静态集合101项；前后摘要与实际父解释器导入/调用文件独立记录。以 `git ls-files '*.py'` 再核均无遗漏。父解释器实际执行闭包为SDK相关14项、devcheck9项；Python子进程动态imports未profile，明确不声称该范围。全部tracked Python的当前raw/Git blob另作干净来源限定核验。

原始失败均保留，不覆盖成成功：

- 首 `preflight.json` 产品pass，但外围guard误把祖先 `.cache` 当checkout内排除，遗漏实际imports而exit1；`guard-initial.py`与原命令/日志/导入证明保留，`preflight-002`起修复为relative路径，并在最终全部scope前纳入完整tracked Python。
- 初 `trial` 因fixture缺固定供应 `SetLastError` 声明而编译失败。该最初试验driver尚无finally保存，因此没有 `trial.json`；原driver、command/import证明及raw编译日志完整保留，不倒填当时未保存的C输入身份。之后每个trial有独立最终report。
- `trial-002` 的terminal setup把namespace作为Promise结果返回，触发现有严格值桥；改成undefined返回后 `trial-003`通过。取消physical/native-error两项当时已经通过。
- `trial-004` 缺事件API供应头声明，原编译失败保留；后续按固定供应 `libc/nt/events.h` 修正，没有猜ABI。
- `trial-006` 的fixture错把永久FAILED后的spawn预期写为RESOURCE_LIMIT；按公开生命周期优先级改为BAD_STATE后 `trial-007`转绿。
- `trial-windows-002` 的128次事件分配未观察到同值HANDLE；`trial-windows-003` 的wake分支即使8192次也未取到已被native分配占用的旧值（原实际值/次数保留）。没有将它们算复用pass；改成真实CloseHandle后的同步OS交接后，`trial-windows-004`的wake/physical/native-error三项全部通过，正式fault消费再次通过。

所有正式supplied运行的supervisor timed_out均为false。上述探索/fixture失败与正式产品通过分开记录；未测平台、完整Cross/Full及1小时组合长测保持deferred，不外推历史M0/M1或其他输入成绩。
