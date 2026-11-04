# 10 · 纯 C 有界原生工作与两阶段退出

本票从干净的 `4d86901` / `codex/m2-10-work` / `.cache/worktrees/m2-10`
开始。沿用已经确认的公开 SDK、包外纯 C Host 与真实/受控系统 seam；
读取父规格、GLOSSARY、ADR 0005/0007、06 及两修复证据，使用 tdd 与
tests/mocking 指引，不重复接口访谈，不声明 JS work Promise 或完整 task cancel。

## 公开增量

- 能力 bit 21 `QAPLET_CAP_M2_WORK`，独立版本化声明、结果、work POD 快照。
  M1 布局/数值不变；M1 实例不取得 work 能力，调用返回 UNSUPPORTED。
- owner 注册复制命名声明；默认并发 1，显式不超过实例池；重复、替换和
  首次执行/有效 pump/接受 work 之后的注册拒绝。函数代码和 userdata 仍为
  Host 所有，保留到 native quiescent，不能套用同步 Host Module 的较短边界。
- 每 Runtime 按需 worker，默认 2，OS 请求栈默认各 1 MiB 单独计量。
  worker 从可运行 handler 中按接受顺序选择，受 handler 并发阻塞的队首
  不占用一个执行配额，也不挡住其他可运行 handler。
- submit 在接受之前同时预检 operation count、8 MiB 数据子额、任务/线程
  元数据及 16 MiB 原生总额；输入复制，输出全部预留，再在接受 mutex 下
  发布 token。实际 SDK calloc 拒绝永久失败，容量预检只拒绝操作。
- 三元 Runtime/slot/generation token，不暴露 JSValue、Context、fd 或内部
  存储指针；代次耗尽不复用。owner 复制终态结果，BUFFER_TOO_SMALL 只报告
  长度、无部分复制、不重跑；结果保留仍收费，release 后 token 失效。
- 完成只写接受前预留的原生记录，不 malloc、不占业务事件槽、不要求 onEvent。
  worker 发布不解除默认 ref；owner 在完整 jobs 检查点之间交接。无活跃
  JS 入口的 C 完成不创建 JS ledger、不改最近 JS 结果；有活跃入口则扣同一份
  累计 dispatch/期限。work/event 在有限交付预算中轮转，cursor 跨 pump 保留。
- stop 请求、worker 领取、完成按同一接受锁排序；请求线程不遍历清理。
  worker 协作标记可观察已接受 stop，非 OPEN 后不再领取排队任务。
  owner 确认终止立即取消并启动原生退出，不等 close。
- STOPPED 保留 C 终态结果；逻辑 release 可使 token 失效，但仍在物理调用中
  的输入/输出/记录继续收费。close 不等不可中断调用、不执行用户 JS，保留
  原生数据直到物理退出。pool、worker 及结果保留责任独立于 loop ref。
- callback/worker_main 返回不证明 OS 线程退出，Host TLS 析构也可能继续。
  只用实际供应的 `pthread_tryjoin_np` 成功证明 join 后回收；EBUSY 保留等待，
  其他错误记录独立 cleanup 并停止重试该线程身份。0/有限 quiesce 不做阻塞
  join；仍在运行时 CLOSED/CLEANING、destroy BUSY。不强杀 pthread。

## 保留的红绿与失败

原件在本工作树 `out/m2-10/`，旧报告不改写：

1. `file-red.log`：公开新 C tracer 首次实际编译失败，旧头没有 work 类型/入口。
   `file-green.json`：真实 mkstemp/read/close、精确二进制、有界输出、ENOENT、
   复制输入/声明、结果重读和 join 实际通过。
2. `slice1/build.json`：构建期间追加了测试源码，producer 正确报告
   `SDK recipe/source changed during build`；不是最终供应。其已生成候选 APE
   只作逐片 tracer，后续都用新供应重新验收。
3. `extra-results.json`：fairness/close 实际通过，registration 因测试把短结构
   的既有 ABI_MISMATCH 写为 INVALID_ARGUMENT 而失败；修正预期后的
   `extra-results-2.json` registration/capacity 通过，未放宽产品 ABI。
4. `ledger-red.json`：活跃 PENDING 入口的小累计 dispatch 与 native 交接组合
   实际失败；`ledger-green.json` 修复后同时验证 dispatch 与绝对等待期限。
5. `physical-red.json`：真实阻塞 TLS 析构导致旧阻塞 join 被外层 supervisor
   5 秒终止，明确是未完成/失败，不能算产品已退出。`physical-green.json`
   同一 seam 验证 0/有限 quiesce 返回 pending、BUSY、安全保留，解除析构后
   真正 join。无退出信号之后的保守复核可到有限等待预算末尾才完成回收；
   不用周期短轮询，不承诺立即回收或整个调用硬耗时。
6. `retention-red.json`：STOPPED 的 C 结果被 loop shutdown 过早失效；
   `retention-green.json` 验证 STOPPED 终态复制、逻辑 release 与物理占额分离，
   并复验其余 work 合同。
7. `scheduling-red.json`：32 个 native 完成的批量交接挡住已就绪业务事件；
   `scheduling-green.json` 验证共享有限预算下的轮转与完整检查点。
8. `request-red.json`：stop 已接受而 owner 尚未驱动时，worker 协作标记仍为假；
   `request-green.json` 验证标记和 worker 领取共享 stop 的接受顺序，不启动
   排队的迟到 callback。
9. 一次 inline Python runner 因 shell 引号产生 SyntaxError，未运行产品；
   随后改为保存的 `tracer.py`。首次 Git 写入被受限文件系统拒绝，没有写入
   或重置数据；普通用户上下文提交成功。一次 recipe 检查临时命令未设置
   scripts import 路径，ImportError 后补齐路径，实际逐文件检查见下。

## 首冻结供应（保留原范围，非最终选择）

首产品 `276a880ba110c7626e68c756c558550421eb85eb`，66 个 RECIPE 原字节
全部等于该提交 Git blob、全部 LF，`recipe-blobs.json`。Windows Git clean。
Ubuntu producer `working_tree_dirty=true` 原样保留，未改写为 clean。
后续 `4c7c29e` 加包外 work 消费，`8180eec` 加停止/调度红绿及可叠加 ABI
基线断言；最终供应必须对最终合并源码重新生成。

| 原件 | 实际结果 |
|---|---|
| `frozen/build.json` | Ubuntu 固定输入 SDK ZIP/双 archive/三 APE 构建 pass，绑定 276a880、dirty=true |
| `frozen/dev-check.json` | Ubuntu完整检查 pass；368 tests，177 实际执行、191 条件 skip；真实系统 C bootstrap pass |
| `frozen/linux.json` | Linux x86-64 native / WSL2 6.6.87.2 / 4096；122 中121 pass、1 fail；10个 work 新用例全部 pass |
| `frozen/windows.json` | Windows x86-64 native / 4096，同一 APE；120 pass、1 fail、1 optional symlink blocked；10个 work全部 pass |
| `frozen/fault-consume.json` | 真正供应 ZIP 在 `/tmp/qaplet-sdk-consumer-*` 包外编译；既有 M2 系统夹具及新 work 系统夹具各1必需 case，2/2 pass |

两宿主的唯一 fail 是完整示例 `--abi` 把旧全部 capability 魔数 491519 当作
精确新全集。修复保留这份旧必需位的 subset 断言和 tail/flags/结构合同；
新 work bit 由 work 公开用例独立验证，metadata 仍严格核对真实全部位。
Windows 唯一 optional blocked 是旧 symlink fixture 的 winerror 1314，未计 pass。

受控 work fixture 通过公开结果/快照观察：pthread_create EAGAIN 原样保留、
操作原子拒绝且 Runtime 可重试；实际 calloc 拒绝永久 MEMORY_LIMIT，stop 不
覆盖首因；tryjoin EIO 独立 cleanup，CLOSED 不复活，存储/线程身份保留，
反复 quiesce 不伪造清零/destroy。最后的异常实例刻意保留到进程结束，
该负向结果不是“SDK 已清理成功”。夹具不代替正常 native backend 证明。

## 待最终集成验收

07 合入 integration `fa4d768` 后，本票融合公开能力19/20/21、SDK目录和声明，
保持旧必需 ABI subset 与真实全集 metadata 分开验证。公开 work scheduling
用例增加32个 work、32个JS timer、32个事件，在 `pump_jobs=1` 下以三次 pump
观察公开队列/保留结果/timer 快照：首轮 work/timer 后 jobs 未结算，第二轮只做
jobs，第三轮先交付事件再轮转 work/timer。机械合并候选
`timer-mixed-red.json` 实际在第三轮事件计数断言失败，持久三类 cursor 修复后
`timer-mixed-green.json` 通过，同时通过原 ledger/capacity 用例。

公开 completion 用例新增由 unref timer 释放阻塞 work 的可控交接。原07自动
timer资格在 work 仍 ref 时忽略 timer，`timer-work-live-red.json` 被5秒外层
supervisor终止，保留失败。自动资格和 driver 存活同时包含 pending handoff 后，
当前 ref work 允许 ready unref timer，最后 handoff 后剩余远期 unref timer
不再保活。`timer-final-green.json` 的13个 work 用例全部 pass，无 stderr 或
timeout；这些仍为增量候选，最终密封供应另行绑定。

接续实施保留前代理未提交的公开 `--m2-work-notify` tracer。合入真实 09 后，
`out/m2-10/notify-red.json` 编译成功，但 Host 自有条件变量等不到 native 完成
提示，被外层 5 秒 supervisor 终止，明确为失败。worker 完成、worker 退出、
成功 submit 与逻辑 release 改为锁内取得真实通知 lease、锁外调用 hook；
reaper 的 quiescent 判定同时检查 notify inflight。
`notify-green.json` 同一公开 seam 通过；增强后的 `notify-lease-green.json`
实测完成 hook 在途期间 owner 可交接结果、close，0 quiesce 返回 pending、
destroy BUSY，解除 hook 后实际 join、销毁 userdata 同步原语。
`notify-work-regression.json` 的13个 work tracer 全部通过。上述仍是显式增量
候选 SDK，不冒充最终密封供应。

`notify-candidate-fault.json` 在相同增量公开 SDK 下实际通过 work 系统故障
夹具；`notify-candidate-windows.json` 在普通用户 Windows native
10.0.22000 / x86-64 / 4096 上消费 Ubuntu 生成的相同 APE 字节，通知在途关闭
与 work 系统故障两项 2/2 pass。候选 Windows 不构建产品。
`recipe-current.json` 确认产品 `f431a44` 的67个 RECIPE 文件 raw 字节全部等于
Git blob 且全部 LF。最终尚待07集成后的三类调度融合、重新冻结与完整供应；
本轮交接不改变 claimed / in_progress，不声明 awaiting_cross 或已完成票10。

一次性旧 runner 直接选 raw toolchain，编译子调用误返0但无对象、cosmoar失败，
保留 `notify-red/candidate.json` 及各原日志；改选仓库已验的
`official-assimilate-linux-x86_64-v1` 后实际产生对象。一次空对象列表 runner
把目录当文件读取导致 IsADirectoryError，未运行产品；修正 runner 后保存了
上述真实红绿。未改产品以绕过环境失败，未改全局 Git 或共享 core.worktree。

## 最终本地供应与交接

最终产品 `9c69877e9c6f6d4e02401e18ecf8a26c132aaeac`，
`out/m2-10/final/` 保存原件。SDK recipe
`1b4993c4bfa330f48712910c4a27de22af349d50da8ad87eba8eebe283e868f2`；
SDK identity `372afc6c243f92fd2b7072aa2894075825b6c6362eae3be389fee84b7e62e678`；
供应原 ZIP `f655c4d2f620136749c4ffeb6dc9697de4117f1f3a767ab0a8d7bbf51500beb5`
（15,556,957 bytes）。Ubuntu producer 的 `working_tree_dirty=true` 原样保存；
Windows Git clean 和逐文件 raw/Git blob/LF 核验独立报告，不把 producer 改写为 clean。

| 最终原件 | 实际结果 |
|---|---|
| `build.json` | Ubuntu SDK ZIP、x86-64/AArch64 archives、CLI/Host/application APE 构建与审计 pass |
| `linux.json` | Linux native x86-64 / WSL2 6.6.87.2 / 4096；142/142 pass |
| `windows.json` | Windows native x86-64 / 10.0.22000 / 4096；同字节141 pass、1旧 optional symlink blocked；必需缺口0 |
| `package.json` | 真正供应 ZIP 在 `/tmp/qaplet-sdk-consumer-501482qz`，只用公开 include/archive 包外编译；应用8 + work13 = 21/21 pass |
| `package-windows.json` | Windows 再抽取核验同一 ZIP，在树外临时目录提供模块；执行相同包外 APE 字节，21/21 pass |
| `fault.json` | 同一 ZIP 在 `/tmp/qaplet-sdk-consumer-0_okdyer` 包外编译，既有 M2 与 work 系统边界2/2 pass |
| `fault-windows.json` | 相同包外故障 APE 的 Windows native回放，2/2 pass |
| `dev-check.json` | 冻结产品的完整本地check pass；368 tests，177实际执行、191条件skip；真实系统 C bootstrap pass |
| `handoff.json` | 70个当前RECIPE逐一 raw==冻结产品Git blob、全LF；双archive各14个ELF64 REL成员，machine62/183；全部包成员身份与供应一致；能力mask4161535及21个名称一致 |

normal、package、fault 六份报告分别保存实际命令、原始 stdout/stderr、OS/kernel/
页大小与制品身份。所有普通 work 的物理退出、通知 lease、累计预算和数据复制
均在最终主 Host 与包外 Host 重新执行。通知在途关闭部分刻意阻塞 hook，是
保留期负向夹具；不据此承诺合规 HostNotify 可以阻塞。故障 join 不可证明回收
的实例仍刻意保留到进程终止，绝不以进程退出冒充 SDK 已清理成功。

双 archive 分别为 x86-64
`fc72f8696ea7b2aad164305d71f52078faeb17ee865b93939a9aa9c1658d0292`
（1,045,236 bytes），AArch64
`743f7b6e5bffcdb49440b5328122b720dab9857e09d8019bff46ae36699e9c23`
（1,056,084 bytes）。只在 Ubuntu 构建；Windows 在已确认的普通用户上下文原生
消费相同字节，不对受限上下文的假挂或无WSL发行版观测修改产品。

最终审计首次在 integration 刚更新为 `46e5cf4` 时因祖先检查失败而停止；
随后 `83e3b105` 合入该最新 integration。该增量仅05探针/合同/文档，70个SDK
RECIPE均未改变；审计重新实际通过，无需重新供应SDK。同步后的本地check另存
`dev-check-post-sync.json`，保留冻结前原check，不覆盖原件。
该次同步后check实际通过369项（177实际执行、192条件skip），真实系统 C
bootstrap亦通过；新增条件skip不计作实际平台或05探针成绩。

本票保持 claimed / awaiting_cross，未 resolved、未 push、未做整体 Standards/Spec
review。其余五原生/实际64K与最终全资源 T03/Full由 root 的新供应签发；上述
双宿主与局部回放不外推这些范围，不签发 release-ready，也不声明11任务取消
公开API或12 JS work Promise。后续process尚未合入，不含能力bit22或新错误码。
