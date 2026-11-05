# 12 · JS named work local validation

当前：`Local development: complete`（本分支，待独立合并），正式
`Status: claimed`；完整Cross/CI `cross_deferred`。
基线 `d2c86d7539792d1aef983031ef034b7d63d32794`，独立分支
`codex/m2-12-js-work`。按已确认 Q49/Q53 的供应公开 SDK + JS/包外纯 C
Host 边界实施，未新增私有测试接口、错误数值或 capability bit。

## 公共接口与生命周期

`qaplet:work` 的 `submit(name, Uint8Array)` 返回 Task，拥有只读 `promise`
属性以及 `cancel/ref/unref/hasRef` 方法。成功结果为复制的 Uint8Array；错误
保留数字 `code/errorDomain/error`。JS 与 C 共享接受、复制、取消同步顺序、
有限输出和结果/物理容量。公开 C owner/reentry 约束保持原合同，JS 使用专用
owner 内部接受入口。

Task wrapper 仅持有带引用计数的 POD token；原生任务记录在 owner 上持有
resolve/reject，worker 不读写 JS 字段。完整 jobs 检查点后解除 ref、释放这些
JS 引用并自动 release 原生结果；取消调用仍物理运行时只退休逻辑结果，数据
继续收费直到调用返回。停止时仅丢弃 JS 责任，不执行 then/catch/finally。

## 逐段验证原件

全部原件在主工作区 `out/qaplet-m2/12-js-work/`，不可覆盖的失败也在原位。

| 范围 | 原件与实际事实 |
|---|---|
| 模块/真实有界文件/复制/自动 release | `red-file-002.json` 公开旧 SDK red；`build.json` + `green-file.json` green，源码 `bc4d439` |
| ref/unref、ready unref 显式驱动、GC | `red-liveness.json` red；`liveness/build.json` + `green-liveness-002.json` green，源码 `2153c1e` |
| callback 内默认 ref、完整 jobs/类别轮转、普通 throw | `red-checkpoints.json` red；`checkpoints/build.json` + `green-checkpoints.json` green，源码 `52e16d2` |
| 文件取消三种顺序与物理保留 | `check-cancel.json`：既有 native 同步/容量路径从 JS 公开边界直接 green，无人为改坏产品制造 red |
| 严格输入、ENOENT 与输出越界 | `check-errors.json`：普通错误、容量恢复从现有路径直接 green |
| stop 无晚 JS、userdata 到物理退出 | `check-stop.json` 直接 green |
| 活跃入口/独立完成的累计 ledger | `check-ledgers.json`：两组无限 jobs 均精确 50 dispatch 永久失败 |
| 实际 SDK 分配拒绝 | `check-js-allocation.json`：独立 controlled 系统配方，真实 calloc 单次拒绝；JS catch 不清除永久 MEMORY_LIMIT，非额度预检或 OS ENOMEM 冒充 |
| other-ref 与悬挂 Promise | `check-unref-shared-002.json` green；ref interval 保留到 work owner 交付，随后取消；unref-only entry 为 UNSETTLED_ENTRY，显式驱动完成 |
| 孤立 surrogate 名称 | `red-name-utf8.json` red，最小复用严格字符串校验，修复提交 `bb220ce`；最终 Linux/Windows normal 中同一 errors 用例 green |

外围失败分类：`red-file` 首次误用 qaplet_eval 的参数数量，仅编译失败，不算产品
red；修复后 `red-file-002` 才是有效模块 red。`green-liveness` 在 producer 尚未
写出报告时误启动消费者，FileNotFoundError，无产品用例执行；后续
`green-liveness-002` 才是完整 green。`check-unref-shared` 把一个已先执行并结束的
单次 timer 当成持续 ref，错误要求 ready unref work 必须先交付；产品允许此时
自然返回，已修为明确持续的 ref interval，保留原失败。

每个中间 probe 在源树外临时目录解包供应 ZIP，仅使用供应公开头和 archive
编译当前公开 C fixture，另记录新 fixture 的真实输入身份；它不冒充该 fixture
已经包含在旧供应 ZIP。最终供应将封装全部新增公开用例。

## 最终本地验收

Ubuntu 最终供件 `final/build.json` 已通过，产品源码
`bb220ce7d726ba9c5a46eb7c7d5b8f699214075d`，SDK
`184a679cd6f100c768c23800c41e4af64e878a0f66011154483f7827403aa351`，recipe
`a7dc0386023cb361bc346c19d18b8bf42923853afdb6987c2726453c3cf95155`。
producer 实测 `working_tree_dirty=false`。`final/linux.json` 为177/177 pass，
`final/windows.json` 为176 pass + 原有 optional file-symlink blocked 1；新增7个
JS work 用例均通过，两宿主消费同一 APE 与 SDK 字节。

最终 `final/package.json` / `final/package-windows.json` 各53/53 pass，含新增
7项的Work27、应用8与timer18；包外源码仅从密封ZIP解出，Ubuntu编译，Windows
消费同一组APE。`final/fault.json` / `final/fault-windows.json` 各2/2 pass，
包含本票 caught JS submit 实际 calloc 拒绝。一次规定入口 `final/devcheck.json`
通过：380项=180pass+200条件skip，真实C bootstrap通过；原件直接保存在主
`final/devcheck-artifacts/`，无额外复制或重跑。

最终供应、三APE及sidecars、两目标archive、密封ZIP、loader、命令与raw logs由
主 `out/qaplet-m2/12-js-work/handoff.json` 审计绑定。当前未 push、未触发 CI、未运行新的 Mac/ARM64/
actual64K/Full/长测；这些保留 `cross_deferred`，不关闭父规格或完整验收组。

Python执行闭包：中间 probe 与全部 build 原 scope 的静态 superset 为
`scripts/*.py` + 本 scope helpers，另记录实际 parent imports/called code；
`tests/contracts` 的 before 字节没有独立捕获，不倒填为已捕获。
最终 normal/package/fault/devcheck 开始前 guard 已升级为全部 tracked Python +
scope helpers 的前后静态快照，并继续捕获实际 parent imports/called code。
所有 scope 均明确不声明 Python 子进程动态 imports 已 profile。
