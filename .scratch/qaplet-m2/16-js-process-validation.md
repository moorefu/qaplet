# 16 · JS Process、字节 IO 与存活

本地实现分支 `codex/m2-16-js-process`，起点为实际 integration
`2d538a1507468cb275e8b63b995f47bf58932a8e`。12、14、15 均是已合入祖先。
当前记录尚在补齐最终供应与交接，不把候选执行或构建成功写成正式 Cross/Full。
票 16 继续 `claimed`，验收组及父规格不改。

原件根为 `D:/Workspace/dairn/qaplet/.cache/worktrees/m2-16-js-process/out/m2-16-js-process/`。
所有旧候选、编译失败、夹具失败、产品失败、原始日志及源配方摘要保留。

## 实现边界

`qaplet:process` 使用公开 C spawn、cancel、stdio token、POD 状态、release，
JS IO 位于独立 `src/js_io.c`，仅消费公开 IO 接口，不读取 ProcessHost 布局。
`src/js_async.c` 保存 Process 的 owner Promise 和 token 身份。
两者复用已有完成类别、provider 轮转、完整 jobs 检查点与累计执行账本。
IO 的 native handoff=2、Process 的私有 JS handoff 保留到完整 jobs 检查点。
原生计数、8 MiB 操作数据子额、16 MiB 总额、物理 helper/OVERLAPPED/join
和两阶段关闭继续由 C 公共实现负责；未提前释放，也未推迟到 19/20。

M1 的公开布局、reserved、ABI 版本和既有错误数值不变；16 复用 PROCESS 和
PROCESS_IO。STDIO 常量和状态/ref 基础由 17 共享，未完成 17 时不启用其能力位。
`55e65f1` 是最小 resource-owner guard/可选 observer 共享提交；
`052c314`、`d45e04a` 是 17 的端点 POD/ref/IO handoff 共享来源，16 对应
`d290d88`、`6ac1136`。`83d9f70`、`e547875`、`93ca6f5` 的通用 JS IO
适配、finish 关闭观察和读错误字节保留已共享给 17；没有新增 17→16 票依赖。

## 红绿与执行上下文

- `red-01.json`：旧供应 CLI 与其锁定 loader 实际导入 `qaplet:process`，
  exit 1 / UNSUPPORTED。`green-01.json`：首轮新供应同一表达式输出 function。
  最初缺 `--source` 的 CLI 参数误用只属夹具错误，不作产品红。
- `slice1-linux`：spawn/reap/closed 实际完成后，测试误把 JS Number 42
  断言为 INT64；修正为 DOUBLE，旧失败保留。`slice2-green-linux` 首次公开
  复制写、双通道、BUSY、EOF、退出与 closed、额度释放均绿。
- `slice2-debug-linux`：写结果复制误填非零 capacity、NULL buffer，公开 C
  返回 INVALID_ARGUMENT。修正成写结果 capacity=0 后同场景转绿。
- Windows 受限默认执行上下文的 main 前超时，与正常宿主实际产品结果分开。
  `slice2-green-windows-unrestricted` 中同字节 JS basic 和 C 默认 pipe 通过；
  `slice2-green-windows-cio` 中原 C pipe bytes 通过。
- `slice2-wintrace-windows` 实际 JS 双通道出现产品 DEADLINE_EXCEEDED，公开
  Host/POD 观测为 accepted=3、completed=0，仅到 write-await。随后缩为单写、
  1000ms 产品期限的 `issue-red-windows`，约 1.139s 失败，且先真实 finish 清理。
  17 的 `abbd559`（16 为 `b96e9b9`）在 drain/recheck 中加入可 issue 判断，
  只重驱动下一有界 pump；POSIX pipe EAGAIN 不被当成即时工作。最小和原场景
  在 `issue-green-{linux,windows}` 均绿，无辅助 timer。调试前缀已从产品/测试删除。
- `finish-after-red-linux`：await write 后再 finish，入口未真正 DONE。
  `e547875` 在当前 JS 执行中按已交付 IO handoff 观察闭合；不在清理检查点
  新建 JS 执行。`finish-after-green-{linux,windows}` 两种 finish 顺序均绿。
- `lifetime-red-{linux,windows}`：取消错误缺 actual reap 字段。保留实际
  exited/reaped/exitCode/signal/domain/error 后，两种 EOF/退出顺序和未读二进制
  输出均绿。Linux 最初完整接受 64K 写入属于有效 OS 行为，不能证明部分错误；
  最终受控 child 显式将 Linux pipe 缩至 4096（实测 UAPI 1024+7），Windows
  使用实际有限 pipe quota；`lifetime-green-*` 记录真实部分写字节和原生错误。
- `control-red-*`：读取消错误缺 copied bytes。`93ca6f5` 修正后
  `control-green-*` 通过严格参数、确定 cancel-before-issue、管道数据未丢、
  小额度及 stop/dispatch/caught OOM 清理。
- `liveness-{linux,windows}`：unref 自然 idle、ref 恢复、1 MiB 引擎堆下
  100,000 循环引用压力后丢弃 Process 仍自然退出；IO 完成的 active-entry 和
  independent ledger 均在 pump_jobs=1 下精确累计 64 dispatch 后失败。
  每平台还实际执行 1000 次 create/spawn/reap/close/quiesce/destroy 和
  100 次混合 IO/cancel/错误循环，每次核对 native 资源回零；Windows 循环约
  14.98s，Linux约数秒，具体原件保留实际时长。
- `combined-quick-*`：JS Work 与 IO 共用 2 个 operation / 8 个预留字节，
  BUSY 优先，拒绝无部分接受，完整 checkpoint 后额度归还。
- `final-smoke-*` 中新增普通完成回调 Error 后，Runtime 仍 OPEN 却丢失了
  现有 Process/IO；原 `pump`/`run` 在任何错误上无条件 shutdown。`ba7c030`
  仅对非 OPEN 的终止状态取消，`ordinary-green-*` 与后续 run/pump 两路径
  均能继续求值 42，原 C terminal/stop/dispatch/OOM 清理保持通过。
- `finish-direction-red-*`：关闭后的读端点 finish 被误当成幂等成功。
  `54f9d31` 将方向校验置于 closed 幂等前，双平台 green 与公开 C BAD_STATE 一致。
- 17 的 `4bf3fa4`（本分支 `91b3598`）为 C owner 闲置关闭增加预留的
  control 完成记录：owner 只读公开 POD，按既有 IO provider/32 交付单元和
  完整 jobs 检查点交付 closed；`1ced9ec`（本分支 `49a24f4`）将其计入既有
  IO pending_handoffs，无新公开布局。`control-fusion-*` 全重编后的 13 个
  JS Process 用例通过，循环也再次通过；这些仍保留自己的候选源码范围。
- `io-start-red-windows`：公开 JS spawn 后由 Host 精确令下一次 pthread_create
  返回 EAGAIN，JS 旧错误为 code 30 / domain 0 / native 0。`86abf4f` 仅在本次
  IO_ERROR 时读取公开 start-error snapshot，green 保留 ERRNO/EAGAIN，失败未
  计 accepted、下一次 IO 恢复并实际完成清理。参数等拒绝不携带旧 OS 错误。
  此夹具随 host-process.c 的独立条件分支分发，fault consumer 单独链接受控
  pthread 边界；正常 SDK/CLI/Host 不含该 wrap。
- `reap-red-linux`：复用真实 sys_waitid 观察后由夹具实际 waitpid 偷走 reap 的
  OS 边界，SDK 正确报 ECHILD/reaped=false，但 JS closed 曾错误成功、默认 ref
  在完整 checkpoint 后被撤。`reap-green-linux` 分别保存实际 reaped 位，并把
  collect/handoff 的自动解除 ref 和显式 set_ref 的有效期都绑定实际 reap。
  closed 保持未决，显式 unref 可解除自然等待；CLOSED 后 engine=0，未知原生
  记录/容量保留，quiesce 仍报 PROCESS_ERROR/WAIT_TIMEOUT，destroy BUSY。
  本用例的通过表示安全保留这一负向合同，绝不表示 native cleanup 完成；
  有界保留随受控 Host 进程结束收口。17 的 stdio 供应不依赖此 Process 专属增量。

部分写最终夹具使用公开 io_block_bytes=1 MiB 的有限覆盖配置，默认值仍是
64 KiB；Linux pipe 显式缩为一个实际内核页，不能把页大小固定为 4096。
child 确认消费 4096 字节后关闭，实际已写量严格在消费量与 1 MiB 请求量之间。
这修正了在实际 64K 内核上“一个 pipe 页就是整个 64K 请求”的夹具假设；
当前未执行 64K 产品，不据此宣称该目标通过。

候选 `iterate.py` 每次布局/Runtime 源变化都重编全部 Runtime 源单元，
只复用不变 QuickJS 对象；仅改 Host 时逐字核对所有 Runtime/header 和旧 archive。
这些输出明确标记 exploratory-candidate，不能替代最终供应 ZIP/SDK 成绩。
早期 Windows 候选 receipt 的 page_size=4096 是字面占位值，不是实测，不能
作为平台验收；后续候选改为 mmap.PAGESIZE。最终验收以 SDK native facts 和
实际 Host 测量为准。带 DEBUG 前缀的旧候选仅验证 C 断言，最终 catalog 仍要求
精确 stdout、空 stderr；旧调试日志不改写。

## 最终验证与保留范围

待融合当时实际 integration tip 后，以完整 `sdk.py build` 供应当前源码，保存
Linux/Windows 同字节正常 catalog、包外 public SDK consumer、受控故障及本地
开发检查的原件。交接时补齐确切源码、recipe、SDK、ZIP、OS、命令和清理摘要。

其他必需原生宿主、当前实际 64K、远端 CI/Cross/Full、最终混合资源长测和发布
门禁保持 deferred。预检、候选、供应构建、平台产品执行分别报告；外部超时、
必需 skip/not_run/失败不记通过，不清理或覆写旧证据，不 push。
