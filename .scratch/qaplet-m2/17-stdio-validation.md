# 17 · 标准流显式安全接入

实施起点 `2d538a1507468cb275e8b63b995f47bf58932a8e`，分支
`codex/m2-17-standard-stdio`。12/15 的本地独立合并为实际祖先。

沿用父规格 Q14/Q49/Q53/T01 已确认 seam：公开 SDK、包外纯 C Host、
真实标准流重定向与 JS 模块。系统故障夹具独立记录。使用 implement-spec 和
TDD skill 的纵向 red/green，不测试私有布局或内部调用次数。

接入与分类分开；dup 不隔离共享 flags。Host 负责协调借用标准流的替换、
共享 flags 和并发使用。未授权改变共享 flags，不能安全执行的方式拒绝。
默认创建不接管；文件 helper 的物理责任不因逻辑取消或 close 消失。

本记录随真实 slice 和验收更新；正式 Cross/Full/其他原生/64K/长测保持待验。

## 产品边界

独立版本化 `qaplet_stdio_info`、inspect/attach/get/detach 接口，返回既有
`qaplet_io_endpoint`。capability 24 为 STDIO，25 仍预留给未实现的 TTY。
M1 结构、reserved、旧错误值和创建行为不变。分类与是否支持接入分别表达。

POSIX 标准 fd 仅复制为 SDK 自持 fd，不设置共享 O_NONBLOCK。阻塞 pipe、
TTY 和其他设备明确 UNSUPPORTED；由 Host 预先设置的非阻塞 pipe 可安全接入。
Windows 查询真实 GetStdHandle/GetFileType/GetConsoleMode，并通过固定工具链
的 NtQueryInformationFile/FileModeInformation 核验同步/overlapped 模式。
只接入已证实的 overlapped byte pipe，写入还须实际可查询 quota；不把15私有
pipe 的属性套用于宿主句柄。普通同步文件经有限 helper 执行。默认创建不查询
或接管标准流，SDK 只关闭自持 duplicate。

模式查询依据 [Microsoft NtQueryInformationFile](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/ntifs/nf-ntifs-ntqueryinformationfile)
及锁定工具链结构。读取类型、模式及原始 OS 错误不需要修改共享状态。实际支持
仍由当前两宿主的真实产品输入证明，不把文档存在当作 OS ABI 成绩。

`qaplet:stdio` 只包装 Host 在首次导入前显式接入的端点。未接入导出 null，
字节适配器复用16独立共享的 `83d9f70`、closed 修复 `e547875`，不依赖 JS
Process 实现。read/write 返回可取消的 Promise 任务，采用公开 C IO 复制结果；
Promise jobs 完整检查点后才释放结果/handoff。空闲端点不保活，默认 ref 操作
保活到 owner 交接；逻辑取消但物理未退出的调用继续计费而不保活。

## 红绿及保留的反例

全部本票原件位于 worktree 的 `out/m2-17-stdio/`，未覆盖旧报告或失败。

- `red-capability/`：以15供应 SDK 编译公开 C Host，真实能力断言退出1；
  `green-capability/` 首片通过。为让16先独立复用基础，曾暂撤 library 查询位；
  STDIO 完整实现后才重新发布该位。
- `red-inspect/`、`red-attach/` 保存新公开接口未实现时的实际链接拒绝。
  `green-inspect/` 首次 runtime 增量替换崩溃保留：共享 runtime 字段在中间新增，
  旧 archive 其余对象仍使用旧 offsets。`green-inspect-2/` 起重编所有 runtime
  单元，通过真实 stdin pipe/file/invalid 分类；最终 SDK 从完整配方重新构建。
- `file-initial-windows/`、`pipe-initial-{linux,windows}/`：二进制 NUL/0xff、
  当前可用短读、EOF、BUSY、取消、idle/ref 区分和 Host 原句柄/共享 flags 保留。
- `green-attach-pipe/` 保存测试误用不存在的 m2 snapshot helper 字段的编译失败，
  改用公开 IO snapshot 后通过；没有增加私有布局断言。
- `output-initial-{linux,windows}/`：两路有序二进制复制写、每次3字节推进，
  retained C result 保留 count/data，饱和拒绝并在实际回收后归还。
- `faults-initial-{linux,windows}/`：受控真实 syscall gate，逻辑 cancel 后
  run 自然返回，copy BUSY；close/quiesce(0)/destroy 保留物理责任；释放 gate 后
  actual native read/exit/join 才归还容量。另有实际2字节前缀后 native error 和
  helper EAGAIN；这些是独立故障配方，不混入普通产品身份。
- `red-js-defaults/` 保存本地 helper 把 defaults 字符串误配 fault 的错误选择。
  `red-js-defaults-2/` 才是实际模块缺失红；`green-js-defaults/` 为默认 null 绿。
- `red-js-bytes-issue/` 保存独立 await 文件读取无辅助 timer 的失败；JS 在本轮
  issue 后提交 IO，drive 原先 drain 了 wake 却未 issue 请求就等待。共享修复
  `abbd559` 按已有 helper/pending/!physical 判断可立即 issue，POSIX EAGAIN
  pipe 仍等 fd readiness。`green-js-bytes-issue/`、`js-initial-windows/`
  两端通过。16独立单 write 的1秒产品 deadline 红绿由其原件另行保留。
- `terminal-backpressure-linux/` 保存65536字节可整写、不能凭请求大小假定背压
  的真实反例。改为 Host 自己实际填到 EAGAIN，保留已有前缀后再核对 SDK payload；
  `backpressure-fixed-{linux,windows}/` 均通过。没有固定 Linux pipe 容量。
- `terminal-initial-windows*` 证明 ConPTY 存在不等于实际 GetStdHandle stdin
  为 Console。fixture 显式 SetStdHandle 后，产品仍只读取实际标准句柄。
  `terminal-fixed-windows*` C 断言/退出/清理已成功，但首 consumer 仅看 ConPTY
  stdout 而遗漏 APE 继承的 libc stdout；`terminal-fixed-route-windows*` 分别
  保存两条真实路由，未把继承 C stdout 伪装成 Console text 支持。
- `local-contract-red*` 保存 all 计划遗漏已实现 stdio 的真实公开 CLI 断言失败；
  `local-contract-green*` 纳入现有 stdio catalog 后12测试通过、1条件 skip。
  计划不签发产品执行成绩，tty profile 当前选择 stdio 而不宣告 TTY capability。

普通产品、受控系统输入、工具合同和历史失败具有各自身份。最终选择及原件摘要以
最终 HANDOFF/handoff.json 为准；早期增量 archive 不替代最终密封 SDK。

## 最终组合补充

`build-final`、`build-final-2` 是各自真实成功的旧供应，保留原范围。随后共享的
read-error bytes (`93ca6f5`)、普通 JS error 不关闭 OPEN 资源 (`ba7c030`)、关闭
read endpoint 的 finish 方向检查 (`54f9d31`)、同步 IO start OS error
(`86abf4f`) 都已吸收；最新供应使用新的配方、报告及原 ZIP，旧报告不改标。

`normal-2-linux` 193/193 pass；`normal-2-windows` 保留旧15共享额度 fixture
的 required fail。失败点在 release 后立即要求 Work 使用同一共享容量，没有等待
Windows IO helper 实际 join。修正只在公开 IO snapshot + 非阻塞 pump/yield
观察 count/data 真实归零后再要求恢复，不改变产品保留合同。该失败的产品清理
仍为 not_proved；`failure-survivor-audit.json` 的精确创建窗只读审计匹配0个
存活 fixture，没有终止任何无身份匹配进程，不把外部观察冒充 SDK 清理成功。

`devcheck` 在共享源仍变化时出现 reuse 工具单项拒绝（411项、201条件skip）；
冻结后 `ci-reuse-focused` 原单项通过，`devcheck-2` 完整通过，原失败不覆盖。
所有拒绝 guard 原样保留。`package-2` 79/79、`fault-2` 22/22 Linux 已通过；
它们绑定其旧供应，不能替代后续新源。

`close-control-red-{linux,windows}` 在同字节2 SDK公开复现 C 关闭无 IO 任务的
端点后 JS closed 永不交付；红的 Runtime close 屏障另行通过。
共享 `4bf3fa4` 以 owner-only 公共 status 观察、每端点预留字段和既有 IO
completion 轮转解决，`1ced9ec` 将其计入公共 pending_handoffs。没有在 C close、
native cleanup 或 POD poll 中运行用户 JS，没有新增无界队列或假 I/O 操作。
`mixed-final-slice-{linux,windows}` 验证 pump_jobs=1 时 handoff/ref 留到完整
jobs、unref 等显式 pump、Runtime close 丢弃尚未运行 mark、ordinary error 后
端点继续读到 EOF，以及 JS/C finish 方向一致。操作/data维持0时的控制保留可由
公共 snapshot 说明。

`red-fault-js-start` 与 `js-start-red-windows` 保存真实 JS code30/domain0/native0
的缺口；`green-fault-js-start` 和 `js-start-green-windows` 修复为 errno/EAGAIN，
后续 bad argument 的 domain/error 仍0，接受重试后复制文件原字节并清理。

[Microsoft IoIsOperationSynchronous](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/ntifs/nf-ntifs-ioisoperationsynchronous)
说明 QueryInformationFile 类查询本身采用同步 API 语义，即使被查询句柄用于
异步 I/O；这补充 FileModeInformation 查询语义，不替代本地真实类型和回收证据。

`build-final-3` 与 `devcheck-3` 是 source `159bd900` 的成功冻结范围。交接前只读
核查发现相邻 detach 路径：`detach-red-{linux,windows}` 以同一公开 close seam
单变量切换 Host 动作，真实返回 OK 后旧 token 已 INVALID_HANDLE，JS closed 的
责任被提前撤去。`d5019b8` 选择 BUSY 无副作用拒绝：JS wrapper 仍承担关闭或
完整 jobs 交付时，不关闭、不失效、不提前退休。C close + owner full checkpoint
之后可重试 detach。`detach-green-{linux,windows}` 覆盖交付前、跨 jobs 中 BUSY、
完整交付后成功失效，以及原 default-ref/unref/Runtime close 屏障和数据/错误回归。
不是仅通过文档排除该组合。

最后修复只改变 C/header/adapter 及对应 C fixture；`devcheck-3` 的完整工具检查
保持其原 source 范围，最终审计逐个证明全部 tracked Python 字节与该次冻结一致。
当前源另执行 preflight，最新 SDK 的完整 native/package/fault/终端边界重新验收。
只有第四份供应及最终 receipt 被选为本地交接；此前成功和失败均保留各自范围。

## 本地完成与最终选择

最终产品 source 为 `61603f2a006301b60d2575587a906638369db13a`，完整继承
实际 integration `8212c0551c5981963e9ffd33201e0c8a78338ef1`。最后票状态提交
只修改本记录和 tracker，不改变任何 SDK 配方输入。Ubuntu 生产原件为
`out/m2-17-stdio/build-final-4.json` 与 `final-4-artifacts/`：

- recipe：`4c19ec6cbb5d70a15157f1b05b733c81098cba46f185e1cd6c5b6fb165f96639`
- SDK：`541b17915c251e59ae03760451c3e483c6cc2d4062bc4eb938ffd4dbccaf786a`
- 原 ZIP：`e065e47ed69e3f4370ae55863280082302b4e7c76e21409c50bc86d55a1cb1c7`

| 公开边界 | Linux x86_64 | Windows x86_64，同一供应字节 |
|---|---|---|
| `normal-4` 全部 SDK/CLI/应用 catalog | 195 pass | 194 pass，旧 optional symlink 1 blocked，required gaps 0 |
| `package-4` 包外 SDK 消费 | 81 pass | 81 pass |
| `fault-4` 独立系统故障配方 | 23 pass | 24 pass |
| `terminal-4` 实际 stdin 分类及无副作用拒绝 | 真实 PTY 1 pass | 明确安装的原生 Console 1 pass |
| 原有 SDK lifecycle | 实际 1000 次 | 实际 1000 次 |

两个实际宿主均为 native、4K 页；完整 OS/kernel/CPU/API 测量保存在各报告。
Windows 无编译；普通产品和包外应用、受控故障分别消费 Ubuntu 供应的原件。
终端只验分类与安全拒绝，原 fd/HANDLE/mode 保留；不宣告18的 lease、resize、
Console UTF-8/文字能力。ConPTY 完整 reaped/closed/readers joined，继承的
APE libc stdout 与 Console transport 各自保存，报告没有混淆路由。

`devcheck-3` 按指定入口通过411项（210执行、201显式条件skip）及实际 C
bootstrap；`preflight-4` 在最终产品源通过。审计逐个核验全部 tracked Python
与 `devcheck-3` 的 before 快照、其原 source `159bd900` Git blob 及当前 raw
字节一致。它证明未变工具合同；不将旧工具检查重新标为最后 C 改动的产品成绩。

`handoff-audit-pre-doc` 保留审计 helper 缺少 loader_manifest 参数的原始失败，
没有产品失败或数据改写。修正本地只读审计参数后 `handoff-audit-pre-doc-2`
通过：85 recipe 文件分别对照生产身份、产品 Git blob、当前 Git blob 及 raw LF；
原 ZIP 82 项；x86_64/aarch64 archives 各18个真实 ELF64 REL；完整 native
catalog、命令、必需/可选项与1000次 lifecycle；11个 guarded scopes 的 SDK/
recipe joins、1608份原始日志。`handoff-audit-final` 增加分支检查时使用了只读
WSL Git adapter 不支持的 branch 命令，保留原失败；改用现有允许的等价
rev-parse 后，最终 `handoff-audit-final-2` 在文档提交后生成 `handoff.json`，
并再次独立绑定产品与交接提交。全部父进程实际 Python 调用、
静态 Python/helper 前后字节分开记录；子进程动态 imports 保持 unknown。
非配方 EOL 只分类，不整体正规化。

五项票内 AC 均有当前公开用例：默认/分类由 js-defaults、classify、实际 terminal；
借用与安全拒绝由 file/pipe/classify；可靠字节、NUL、EOF、BUSY、finish/closed、
取消与错误由 file/pipe/js-bytes/js-errors/c-close-js 和 fault partial/js-start；
共享额度与背压由 output-limits/backpressure；helper 物理保留与 stop/close
由独立 cancel/close/stop/allocation/start 故障配方。正式 AC 的文字和跨平台
义务保持原样，状态为 `claimed`、`local_complete / cross_deferred`。

18可复用 `src/stdio.c` 的 `qaplet_stdio_classify` 真实分类与 `src/io.c` 中
`qaplet_io_endpoint` 的所有权事实；当前 attach 只接受已支持类型，没有预先提供
终端成功 stub。
与16共享的所有已知 adapter 修复已纳最终配方（包括 read error bytes、closed
顺序、普通错误 OPEN 保留、finish 方向、精确 start OS 错误和 detach guard）。
19/20的组合验收不替代本票首次资源预留与真实 close/quiesce/destroy 安全。

其他必需原生平台、当前实际64K产品、CI/Full、最终混合资源长测和 release-ready
仍 deferred。没有 root 自合、push 或 CI；保留全部 out 原件、旧失败和 worktree
供独立 merger 核验。

## 独立本地接收

2026-11-05：独立merger以最终source `8b9e389c8c0ef3d70d847776108ad86284cc033a`
接收产品 `61603f2a006301b60d2575587a906638369db13a` 的第四份供应。确认source
包含当时actual root `8212c0551c5981963e9ffd33201e0c8a78338ef1` 与12/15独立
合并祖先、两树clean后，no-ff merge `caa439aeaf384e9db85a4e2e06c51d8ad40fa659`
树与source完全相同。

合前及合后均使用现有SDK package/native typed consumer和独立raw/catalog重放，
重核85个recipe输入的供应摘要、产品/交接/合后Git blob与raw LF，公开header、82个
原ZIP成员、完整分发文件、双CPU各18个ELF64 REL对象、三fat APE headers和1608份
原日志。normal Linux195、Windows194+既有optional symlink1/required gaps0，
package各81，controlled fault23/24，真实PTY/显式Console分类及清理各1，原生
lifecycle各1000，均保持原实际执行身份。STDIO24与未实现TTY25边界未扩大。

独立核验84个tracked Python与devcheck-3 before快照、159bd900 Git和当前字节相同；
411项/210实际/201条件skip及C bootstrap保留其旧source范围。最后C/header/adapter
由供应4完整公开边界覆盖；merger新实际执行仅Ubuntu仓库preflight，缓存仍为主
`.cache`。未重建SDK、未重复长测、未启动QEMU、未push或CI、未执行整体双轴Review。
独立helper初次对combined fault输出形状的假设失败保留于主out/premerge日志，修正
仅在忽略的接收helper，后继premerge-2及postmerge通过；没有新的产品失败。

供应HANDOFF/handoff.json摘要前后相同，原supplier dirty字段和全部旧失败保持原样。
本票claimed/local_complete/cross_deferred支持18本地开发；其他必需原生、当前实际
64K、CI/Full、最终组合长测及发布就绪未验收。16尚未集成的Process专属lost-reap
负向不由本票签发。完整选择、命令与摘要见主`out/qaplet-m2/merge-17-stdio/HANDOFF.md`
及`handoff.json`。
