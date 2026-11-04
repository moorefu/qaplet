# 07 · 一次 timer、TLA 与自然退出

工作树 `.cache/worktrees/m2-07`，分支 `codex/m2-07-timers`，基线 `4d86901`。
沿用 Q14/Q49/Q53 的公开 SDK、JS/最小 CLI 与受控系统边界；不重新访谈。
使用 TDD 的纵向切片，实际原件统一保存在本树 `out/m2-07/`。
本票保持 awaiting_cross；完整当前 Cross/Full 与实际 64K 由 root 在集成后签发。

## 公开合同与实现

新增 bit19、timer Runtime/generation 值句柄、owner C 注册/取消/ref、独立 POD
snapshot 和 M2 `qaplet:timers` 的 setTimeout/clearTimeout 与 handle 方法。
M1 布局、旧错误数值、既有 M2 snapshot 布局及 SDK 最近执行结果槽保持不变。
完整接口和借用期见 [一次 timer](../../docs/development/timers.md)。

固定槽表从首次创建计入原生总存储，接受前检查 count/安全整数/deadline，记录在
回调返回前持续占额；generation 与接受序号不回绕复用。取消、stop、故障和 close
释放根，不运行 JS。复用 06 整段驱动保护、原始控制/cleanup 分离、单调时钟、
完整 jobs 检查点与累计账本。未更改不确定句柄的计费或一次关闭规则。
只实现一次 timer，没有 interval/immediate 成功占位。

## 真实切片

- `slice1-real-red.*`：供应旧 SDK 的公开 C 编译缺 timer 类型/入口，exit 1。
  `slice1/build.json` 的 Ubuntu 首片供应构建通过；`slice1-green.*` 证实零延迟
  注册不同步回调、owner 恰好一次及 close/quiesce/destroy。
- `slice2-red.*`：公开 JS import/Promise 运行红灯；`slice2-green/report.json`
  转绿。`slice4-tla-green/` 使用真实 ESM 顶层 await，恢复后公开结果 42。
- `slice3-red/`：独立 POD 查询尚不存在的编译红灯；`slice3-green/` 证实
  Runtime/generation、取消/复用/完成失效、ready/ref/deadline。
- `cli-red/` 与 `cli-contract-red/`：旧 CLI 把 unref-only 入口作为成功；同轮
  后台 timer 把入口 42 擦成 undefined；50ms TLA 没有系统等待。
  `cli-contract-green/` 同 SDK candidate 下分别转为 UNSETTLED_ENTRY、42、
  实际一次 poll(50ms) 与 42。私有 entry-drive 复用自动 driver，不新增 SDK ABI。
- `slice6-liveness/`：DONE/ref、unref-only 自动退出与显式 pump/run_once、最近
  等待、stop 后资源清退。`slice6-limits/`、`slice10-reserved-green/`：count
  预检、槽复用、自取消回调仍收费、超大元数据总存储拒绝和完整创建回滚。
- `slice7-jobs/`：pump_jobs=1 下完整检查点优先，普通错误先结算 jobs，后续
  驱动继续。`slice7-deadline/` 与 `slice11-budget/`：活跃入口绝对期限及跨
  零等待驱动累计 dispatch 到永久 FAILED，终止前不重置账本。
- `slice8-faults/`：供应 archive 的独立系统 wrapper，确定的 deadline 溢出、
  max_wait/timer/entry 三种最小等待、核心时钟失败/倒退、终止资源清退；既有
  初始化/原始诊断/不确定关闭/同号 FD 复用负向合同一并实际通过。
- `slice9-abi-red/`：短 diagnostic 被错误写入的真实运行红灯；
  `slice9-abi-green/` 修复为验证失败直接返回，canary 保持。
- `slice10-strict-green/`：严格 JS Number、无转换钩子、非有限/负数/小数/
  字符串/对象/BigInt/Symbol/不安全整数及 deadline 溢出拒绝，handle 控制。

candidate.py 的每次反馈都复制首片供应 archive、在 Ubuntu 重新编译自己的 Core
单元；各自保存源码 SHA、完整命令/退出/原始输出和供应 archive 前后不变收据。
这些显式标为 controlled_candidate_build，不当作最终 SDK 或原生平台签发。

保留过程入口错误：第一次直接调用原工具树的 compiler 误入 Windows helper，
只产生 open failed，exit 0；`slice1-red.*` 不算产品红或成功。改用已核验的
Ubuntu native tool view 后取得真正 `slice1-real-red.*`。candidate 首次缺少
argparse 的 `--` 分隔在编译前拒绝；严格 JS fixture 最初误读 Boolean 的 number
字段，`slice6-strict/` 保留失败，改读公开 boolean 字段后通过。

## 最终供应与范围

最终提交、配方逐文件 Git blob/LF 核验、Ubuntu SDK、Linux/Windows 同字节消费、
包外纯 C/系统故障、普通 dev check 的原件与结果将在执行后追加，不预填成功。
没有 push、整体 Standards/Spec 审查、父规格 resolved 或 release-ready 声明。

## 冻结后边界与保留候选

`7233e24` 的 66 项 RECIPE 与 Git blob 原字节相同、全 LF；首个完整 dev check
通过。冻结核对发现 `UINT64_MAX` 合法 deadline 被当作空集合 sentinel。
`exact-deadline-real-red/` 在公开 snapshot 的最近毫秒断言真实失败；
`exact-deadline-green/` 将内部查询改为独立存在标志后通过。最初的
`exact-deadline-red/` 夹具先调到更大时刻再往回调，实际命中了倒退保护，
不算这个缺陷的独立红灯。`006508e` 提交修复后再次核验全部 66 输入，
`recipe-blobs.json` 保存该轮，早期收据另存 `recipe-blobs-7233e24.json`。

`final/build.json` 是 `7233e24` 的候选；`verified/build.json` 是 `006508e` 的
候选，不是最终选择。后者两宿主实际执行完整 126 项，timer/CLI 新项全部通过，
共同在旧 `m2-creation` 断言失败：新增 timer 元数据原生总预算预检正确返回
RESOURCE_LIMIT，旧 fixture 只允许 MEMORY_LIMIT。Linux 为 125 pass/1 fail；
Windows 为 124 pass/1 fail/1 optional blocked（真实 symlink 权限不足）。
该轮 `verified/package-consume.json` 包外 C 应用 8 项通过，仍只保留原供应范围。

`c00d7ec` 精确扩展该 fixture：只在 native 分支允许并单独计数元数据预检
RESOURCE_LIMIT，实际 heap/native 分配拒绝仍必须 MEMORY_LIMIT，三类拒绝
及成功都必须出现；逐失败用公开诊断验证空句柄、原始 primary_reason、完整
回滚、QUIESCENT、无 cleanup/uncertain 句柄。`creation-metadata-green-fixed/`
通过；此前 `creation-metadata-green/` 反馈 runner 给 standalone main 多传了
fixture 参数，在产品执行前以 exit 2 拒绝，保留原件且不算产品红灯。

`dev-check-final.json` 对 `006508e` 的普通检查通过：368 tests / 89.194 秒，
191 条件 skip，真实系统 C bootstrap pass。两次 dev 的默认原日志在 out/dev；
`dev-evidence/` 保存全部 36 文件逐字节副本，`dev-evidence-map.json` 记录
原路径、保存路径、SHA/长度与一致收据；没有改写原报告的真实日志路径。

## 2026-11-04 · HostNotify 融合与追加红绿

接续时保留 `814290f` 及在途 `2c44fc3` merge，核实 MERGE_HEAD 后完成
`097885a`，再合入仅文档集成至 `563ff33`。timer bit19 与 HostNotify bit20
同时保留；能力掩码、公开元数据、Host 单元和受控输出累积，不以一方覆盖另一方。
通知 TLS guard 对 timer create/cancel/ref/snapshot 同样返回 BUSY；固定通知控制字段
通过 Runtime 本体计费，与 timer 槽表继续累积计入原生总存储。

`integrated-b481efc/` 保留首轮融合供应和通过的 dev check；其密封 ZIP 的公开
metadata 断言真实失败：能力整数含 bit19，但 capability_names 缺少 m2-timers。
增加该名称后转绿。编辑时 Windows 默认文本解码曾误改两条 Unicode 夹具，
`2794342` 已按前一提交 Git blob 恢复原字节，最终净差异只有能力名称；没有供应
误编码版本。`final-a37c4bc/` 仅保留该中间提交的配方检查，不作为 SDK 供应。

`final-2794342/` 的完整 Linux 129 项及 Windows 128 pass / 1 optional blocked、
两宿主各 1,000 次 timer 生命周期和 100 次 timer 混合序列、包外应用 8 项、
Linux/Windows 同字节受控 fault 均通过。额外密封 ZIP 包外 timer Host 的 Linux
10 项通过，Windows 在旧 liveness 夹具中真实失败 1 项：注册 15ms timer 后要求
run_once(1ms) 必须 WAIT_TIMEOUT。该次命令约 23ms；原始复合断言没有分别记录
reason/calls/回调时刻，因此不能仅凭总耗时断言具体在哪个调度位置暂停。
该红原件 `timer-package-windows.json` 保持 fail，没有用重跑覆盖。

公开 C 最小观察探针 `timing-observer-build/` 与 `timing-observer-windows/`
各实际 102 例，记录注册前/驱动前后/实际回调 CLOCK_MONOTONIC。确定性注册后
暂停 30ms 时，旧超时断言在两宿主均 false，而实际回调分别约 30.0806ms、
30.3626ms，符合已接受 timer 的最早 deadline；远期 5,000ms timer 的 1ms
等待均返回 WAIT_TIMEOUT。它证明旧夹具排除了合法调度，不声称捕获原始自然红的
具体暂停位置。B05/Q51 的累计主动等待预算不是整个调用的硬耗时上限。

`ddf00aa` 仅修改验收 C：正常 liveness 用实际回调单调时钟约束，不早于注册前
时刻 + 15ms；WAIT_TIMEOUT 必须仍保留一个 ref timer，PROGRESS 必须恰好交付
一次。受控时钟另设 1,000ms timer / 1ms max_wait 必须超时，再保留原 5ms timer
与 max_wait/entry 最小值断言。`liveness-fixture-green-build/` 与 Windows 原件
对单一 30ms 暂停注入均通过；候选明确单独标记，不当作最终供应。Core 与等待
实现没有因这次夹具修复改变，未增加错误码或延长 timeout。

一次性 timer 包外 adapter 首次在筛选空参数列表时 IndexError，Windows fault
adapter 首次在 Python 解析阶段有多余括号；均发生于产品执行前。对应
`timer-package-adapter-error.json`、`fault-windows-adapter-error.json` 保存失败阶段，
原工具输出留在工作记录；修正使用分别命名的 fixed 报告，产品失败未被改写。

## 最终供应 ddf00aa 与本地交接

最终 RECIPE 源码为 `ddf00aa3ad67547ed3f874e13cfc42d69b95d3ea`，已包含
`563ff33` integration。全部新原件在本树 `out/m2-07/final-ddf00aa/`；
[handoff.json](../../out/m2-07/final-ddf00aa/handoff.json) 汇总原件身份与范围，
不替代原报告、命令、stdout/stderr。Windows 与 Ubuntu 的 67 项 RECIPE
逐文件原字节 = Git blob、CRLF 为零；Ubuntu producer 的
`working_tree_dirty=true` 原样保留，没有改写为 clean。

| 原件 | 实际结果与范围 |
|---|---|
| `build.json` | Ubuntu 新版供应 pass；密封 ZIP、公开 SDK 双目标 archive、Host/CLI/完整应用 APE 审计通过 |
| `metadata-green.json` | 密封 ZIP 的 bit19/bit20 和 m2-timers/host-notify 名称同时存在 |
| `linux.json` | Linux x86-64 / WSL2 6.6.87.2 / 4096，129 pass，必需缺口 0 |
| `windows.json` | Windows 10.0.22000 x86-64 / 4096，128 pass / 1 optional symlink blocked，必需缺口 0 |
| `repeat-linux/report.json` | 实际 1,000 次 timer 生命周期、100 次 timer 混合序列，约 25.323 秒，供应字节前后相同 |
| `repeat-windows/report.json` | 同字节 Host，实际相同次数，约 10.918 秒，供应字节前后相同 |
| `package-consume.json` | 最终 ZIP 在 `/tmp/qaplet-sdk-consumer-1xvf6i0v` 包外仅用公开头/供应 archive 编译普通 C 应用，8/8 pass |
| `fault-consume.json` | 最终 ZIP 在 `/tmp/qaplet-sdk-consumer-duij2mij` 包外编译受控系统 fixture，1/1 pass；包括确定性远期 timer 等待、HostNotify 与 timer BUSY guard、原始首因和 uncertain fd 保留 |
| `fault-windows.json` | Windows 执行同字节 Ubuntu 包外故障 APE，1/1 pass，非普通随机 OS 故障覆盖 |
| `timer-package-consume.json` | 最终 ZIP 在 `/tmp/qaplet-sdk-consumer-bnp_84v6` 包外编译独立 timer C Host，10/10 pass；仅公开头、原供应 archive，不重编 SDK |
| `timer-package-windows.json` | Windows 执行同字节包外 timer APE，10/10 pass，包含按实际回调时钟验证的 liveness |
| `dev-check.json` | Ubuntu 完整 check pass：368 总、177 实际通过、191 显式输入条件 skip、87.114 秒；真实系统 C bootstrap pass |

普通完整目录还分别实际执行了既有 M1 1,000 次生命周期；新增 1,000/100 只证明
本票 timer 资源范围，不冒充最终 T03 全资源混合。全部最终必需报告均无 timeout、
必需 skip 或 fail。`dev-evidence/` 保存本轮 18 文件逐字节副本，
`dev-evidence-map.json` 保存 SHA/长度/一致收据；原报告的原日志路径未被修改。

最终 SDK recipe SHA-256 为
`1715c5a60616841c680576efc8334c823e82d19d7871d172a0a3aabbfc41c1ca`，
SDK aggregate 为
`4d56905b72a9d9e559b52e8e27bf3bf0241f8293082b9badf2462156c149ea5a`，
密封 ZIP 为
`84b24406d2efecc15e55abe225fc7c9f74049aea5be947ed9aeb8ab8a2740de5`。
Host APE 为
`bce26b62ee6ded78b7314526370a04ed9251f236a932d562928fa4a6252835dd`，
CLI APE 为
`3e2c0b422187084e89ef2486894b2df2f2884c15a1889f71b9200c69093ffa49`。
两 archive 分别为 x86-64
`c50bfad9d1e4b298d665783895d7fc6fd4cc08be672556761e7fe2e5402cacb5`、
ARM64 `a212b002199ecc867e61422b0d409ddce38e623930760f44491df58a280a7915`，
没有声称不同架构共享一份原生 archive。

一次性 `timer_consume.py`、`consume_timers_windows.py` 和
`consume_fault_windows.py` 位于 `out/m2-07/`，身份/源文件/完整命令由对应报告保留；
辅助入口自身身份另见 `adapter-identities.json`。
Windows 只重放 Ubuntu 已编译的同字节 APE。包外 timer 的 modules 显式映射到
同一密封 SDK 的 modules，前后整包逐项核对未变，不包含引擎或私有头。

本票本地交付完成，仍为 **claimed / awaiting_cross**。新的五原生、实际 64K、
集成 Full 和最终父 M2 全资源长测由 root 继续签发；旧 09 Full 不覆盖本配方。
没有触发 CI、push、合并到主树、关闭本票或父规格，也没有签发 release-ready。
