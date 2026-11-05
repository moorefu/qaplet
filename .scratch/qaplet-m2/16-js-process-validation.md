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

候选 `iterate.py` 每次布局/Runtime 源变化都重编全部 Runtime 源单元，
只复用不变 QuickJS 对象；仅改 Host 时逐字核对所有 Runtime/header 和旧 archive。
这些输出明确标记 exploratory-candidate，不能替代最终供应 ZIP/SDK 成绩。

## 最终验证与保留范围

待融合当时实际 integration tip 后，以完整 `sdk.py build` 供应当前源码，保存
Linux/Windows 同字节正常 catalog、包外 public SDK consumer、受控故障及本地
开发检查的原件。交接时补齐确切源码、recipe、SDK、ZIP、OS、命令和清理摘要。

其他必需原生宿主、当前实际 64K、远端 CI/Cross/Full、最终混合资源长测和发布
门禁保持 deferred。预检、候选、供应构建、平台产品执行分别报告；外部超时、
必需 skip/not_run/失败不记通过，不清理或覆写旧证据，不 push。
