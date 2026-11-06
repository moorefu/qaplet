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

`5e7a44e` 的 `out/m2-08/final/` 完整候选已经验收：Linux153/153、Windows
152 pass和1既有optional symlink blocked、两宿主包外39/39、两宿主受控
故障2/2、dev check369（177实际执行、192条件skip）及bootstrap通过。
`handoff.json` 核对70项当前RECIPE raw==该提交Git blob、全部LF；两archive
各14个ELF64 REL成员，machine62/183；密封包全部当前成员逐一核对；能力mask
4161535及21个名称一致。SDK recipe为
`ccc2f6dfe0e76a626cc179722a741d937458b0a3fc95fbae387037f88e932ea2`，
SDK identity为 `8c2ec660feba4c3e64f3f60fdf0e3a7ffba60dc0168b478348721ace497f07b5`，
原ZIP为 `54f7a41eb5e0a3db6acd0d39e4a69585d016f96b456939135b0335984c88b171`。
producer的dirty=true保持原样。

该候选在root准备合入独立票10文件夹具修复时封存，不覆盖或改写为修复后
成绩。票08不将13作为新blocker；仅合入实际最新integration，随后用新目录
重新供应当前统一配方。后续Process会复用同一完成类别与32单元预算。

## 当前统一供应与交接

已合入 root 实际 integration `166ab50b1f0d4ef6bfb17aa856791047cbdb421c`，
产品提交为 `bc5fc49d64a5cbfc6a5463e0d107daa7a7bf8066`。差异仅为票10的
三个供应 fixture/script 文件，Core 调度源码未改变。新原件全部在
`out/m2-08/final-filefix/`，前述两个供应目录、红绿和失败原件均保留。

| 原件 | 实际范围与结果 |
|---|---|
| `build.json` | Ubuntu正式SDK/双archive/Host/CLI/application审计及密封ZIP通过 |
| `linux.json` | Linux x86-64 / WSL2 6.6.87.2 / 4096，153/153通过 |
| `windows.json` | Windows native x86-64 / 10.0.22000 / 4096，152通过、1既有optional symlink blocked；必需缺口0 |
| `package.json` / `package-windows.json` | 真正密封ZIP在树外抽取，只用公开头及供应archive编译的应用8+work13+timer/scheduler18，两宿主各39/39通过 |
| `fault.json` / `fault-windows.json` | 同一ZIP树外受控系统夹具，两宿主各2/2通过；含真实文件消费者和本票时钟验收，不外推正常OS覆盖 |
| `dev-check.json` | Ubuntu完整check通过，369总/177实际执行/192条件skip，98.829秒；真实系统C bootstrap通过 |
| `handoff.json` | 当前70项RECIPE原字节等于产品Git blob、全部LF；密封包全部成员与供应一致；双archive各14个ELF64 REL成员，目标machine62/183；能力mask4161535与21名称一致 |

当前SDK recipe为
`0eceff82077e1ab21ae04b3a77d1d33c2007ba84c519291dd5e53a2f955bd684`，
SDK identity为 `dbfd03bb6b869d3e855cb9d2f9c98c55a4f7e9744f863a04d3a78d73fc83f0f1`，
密封ZIP为 `5840be70cbeb83c4cb31d9f6496c7330888f6db1a8ff0fe86a9141e4ed3981bc`
（15,580,718 bytes）。Ubuntu producer的dirty=true原样保留，不以Windows
Git clean替换。每次driver自身及仓库导入前后哈希、实际命令、stdout/stderr、
供应字节和宿主身份保存在报告与原日志中。

两static archives与5e7a44e候选逐字节完全相同，独立于变化后的Host和ZIP：
x86-64 `de1d671fea44fea394d68a6e4dd704a4c8e14361a1c7fbea3345dd8a85a0a320`；
ARM64 `e5e91aa0e1625f892075513e8971368c0e6b6a46528f45c7aaa8aa8adf7dc683`。
`static_archives_identical`收据绑定前候选handoff，不把两个目标混为同一份archive。

本票本地实现与全部正负合同完成，保持 **claimed / awaiting_cross**。当前
五原生、实际64K和新Full严格验收由root继续；当前两宿主成绩不替代它们，
不关闭本票、父M2或发布门禁。没有push、合并主树、发起整体Standards/Spec
review或签发最终T03全资源混合/长测。票02原探针失败和旧Full失败保留原范围；
票10修复当前配方已统一，13仍未合入、不构成本票新blocker。
