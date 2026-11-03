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
