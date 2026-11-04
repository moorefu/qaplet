# 08 · interval、immediate 与完整检查点轮转

本票在 `codex/m2-08-scheduling` / `.cache/worktrees/m2-08` 实施，先从
`09527d1` 快进到当时 integration `4375333`。沿用父规格确认的最小 CLI、
公开 SDK、包外纯 C Host 和受控系统边界；读取 implement-spec、tdd 及其
tests/mocking 指引，使用既有 Q14/Q49/Q53 授权，不新增接口访谈。

## 产品边界

- 保持 M1 和既有 M2 公开结构的大小、布局、错误编码和 reserved 语义。
  timer 能力继续使用 bit19，不新增 capability 或错误码。
- 增加 `qaplet_interval_create`、`qaplet_immediate_create`，复用现有 timer
  token、cancel/ref/unref 和公开快照。JS `qaplet:timers` 增加
  setInterval/clearInterval、setImmediate/clearImmediate，句柄拥有同一组方法。
- timeout、interval、immediate 共用实例 `m2_limits.timers` 固定槽表，默认
  1,024。元数据自创建起计入 native 总额；正在执行的回调即使自取消仍占槽，
  直到返回后安全回收。丢弃 JS 句柄只释放句柄 token，不取消已注册资源。
- interval 至少 1ms；原计划 deadline 加整周期，跳过已经错过的时段，每次
  领取只调用一次。回调返回 Promise 不参与重排，runnable jobs/rejection
  仍遵循完整检查点；回调内取消阻止再次排程。
- 每个 pump 内固定 immediate 接受序边界；本轮回调或 jobs 新注册的
  immediate 最早在后一次 pump 领取。各类别的 cursor 跨 pump 保留。
- 完成、timer、immediate、业务 event 共用每 pump 32 单元，event 仍受旧
  `pump_events` 更小配额限制。无就绪类别跳过；回调之后先结算完整 jobs。
  纯 C work 完成交接沿用既有路径，不启动新的 JS 执行账本。
- 保留活跃入口共享账本、独立回调及其 jobs 跨 pump 账本、普通异常恢复、
  unref-only 自然返回和显式 pump/run_once 的区别。CLI 私有 entry-only
  驱动仍先保存入口结果；没有增加 Node 钩子或兼容层。

## 红绿与保留的失败

原件位于本树 `out/m2-08/`，不覆盖失败报告：

1. `interval-red/` 首次候选适配器只复制了 x86-64 供应 archive，ARM64
   链接缺引擎对象；属于适配器失败，未运行产品。补齐同供应 ARM64 archive
   后的 `interval-red2/report.json` 实际运行并在缺失 setInterval 处失败。
   `interval-green/report.json` 新接口和既有 timer 回归通过。
2. `immediate-red/report.json` 实际在缺失 setImmediate 的注册处失败；
   `immediate-green/report.json` 验证注册不重入、FIFO、自取消以及嵌套
   immediate 第一次 pump 只见 `ab`、第二次才见 `abc`。
3. `rotation-contract/` 新夹具把 work 输出额度留为 0，按公开规则选择
   1 MiB 默认，无法接受 32 项；未放宽产品预算。显式声明 1 字节后，
   `rotation-contract2/report.json` 的单 pump 和跨 pump 两项通过。
4. `budgets-contract/report.json` 共享单槽容量、普通异常/晚处理 rejection、
   活跃入口及独立 idle 回调无限 jobs 的累计账本全部通过。
5. `clock-contract/report.json` 受控 `clock_gettime` 系统边界通过：同 deadline
   的 `IAB` 接受序；首次到期读到 47ms，回调推进到 74ms 后下次仍为原计划
   80ms，快照精确剩余 6ms；79ms 不执行、80ms 执行并自取消，没有追赶 burst。
6. `gc-contract/report.json` 在公开 4 MiB 引擎预算下成功制造 64 MiB 的
   不可达循环 ArrayBuffer；普通引用计数不能释放这些环，运行成功要求实际
   cycle GC。丢弃的 timer/immediate 句柄之后仍按 TI 交付，验证 GC 不取消。

四类调度的断言并非只看完整 run 最终次数：公共 Host callback 在每次 JS
副作用中查询公开 work pending_handoffs。单次 pump 公开 trace 为八组 TIE，
首次 timer 观察完成已从 32 降为 31，第八组 event 观察 24，之后公开队列与
timer 记录分别剩余 24/48。`pump_jobs=1` 时第一 pump 为 `T1`，第二 pump
仅 `T12` 且没有新的 handoff/event；第三 pump 为 `T123IET`，证明检查点
完成后 cursor 从 immediate、event、完成继续到 timer。

新的完整检查点用例先制造 rejected Promise，再通过分跨 pump 的 jobs 链
安装 catch，不提前报告 rejection。普通 throw 分支在 jobs 结算为 `T123H`
后才向 Host 返回 JS_EXCEPTION；再次驱动才见 B，公开快照仍 OPEN，B 的
独立账本恰好 1。活跃入口及 idle 独立交付的无限 jobs 均跨 pump 累计到
精确 50 dispatch 后永久 FAILED；不通过插入其他回调维持表面公平。

## 完整供应与验收

当前产品首提交为 `bcf9d64`，`pre-process/` 为该提交的完整首供应，原件保持
独立：build pass；Linux 152/152、Windows 151 pass 和 1 既有 optional
symlink blocked，必需缺口0；包外应用/work/timer 两宿主38/38；受控故障
两宿主2/2；dev check pass（91.376秒，含真实系统C bootstrap）。首个 Linux
consumer 未给显式 loader-dir 而 blocked，保存为 `linux.json`；补齐后的
实际结果为 `linux-fixed.json`。GC 新用例不包含在该首供应，最终重新供应。
所有产品和 SDK 构建仅在 Ubuntu；
Windows 只消费同字节。一次性 driver 对自身及仓库导入前后源码作 SHA-256
核对并嵌入报告；工作树 dirty 状态原样记录。

本票尚未完成当前五原生、实际64K和新 Full 严格集成验收。票10已知 Windows
文件夹具及票02探针缺口由独立修复处理，不能把旧 Full 的失败归因或改写成
本票调度成绩。当前仍为 claimed / in_progress，不关闭本票、父 M2 或发布门禁。
