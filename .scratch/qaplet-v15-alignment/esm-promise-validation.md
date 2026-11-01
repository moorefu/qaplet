# BOOT-04 · M0-A ESM/Promise 微型 C Host 验收

2026-11-02，任务 03 完成目标 QuickJS 下的 Ubuntu x64 ESM/Promise 闭环；统一锁已晋级
`pinned`，允许进入正式 M1。本文不签发 M1 实现、新完整跨宿主矩阵或 release-ready。
02 的候选报告、旧 M0 与旧离线成绩保留各自原输入和合同，未被改写或重标。

## 实现与实际运行

保留 `hello` / `quickjs-eval` 两个 artifact family。在既有 C probe 内增加固定 ESM 来源和显式
模式；`m0:dependency` 只有内嵌 `export const base = 40`，没有文件、网络或产品 resolver。
模块通过公开 QuickJS API 编译/求值，从 module namespace 读取导出。
Promise 模式实际执行 pending jobs，再检查导出的 completion Promise；pending 队列为空不等于
Promise 完成。独立 rejection tracker 保留未处理 rejection；本固定 fixture 最多同时一个。
每个进程仍创建并销毁独立 Runtime/Context，没有形成 SDK ABI。

| 新用例 | 实际断言 |
| --- | --- |
| `esm-export`、`esm-dependency` | 模块导出或依赖求值输出 `42`、空 stderr、exit 0 |
| `jobs-complete` | `Promise.resolve(40).then(n => n + 2)` 被驱动并输出 `42` |
| `esm-missing`、`esm-syntax`、`esm-exception` | 缺模块、解析和求值异常分别写对应错误，空 stdout、非零退出 |
| `jobs-exception` | 真正入队的 C JS job 抛出 `InternalError: m0-job-exception` |
| `jobs-rejection` | Promise 回调抛错成为未处理 rejection，即使导出的 completion 已完成也失败 |
| `jobs-unfinished` | 无 pending jobs 但 completion 仍 pending，报告 unfinished 并失败 |
| `jobs-budget` | 持续追加的真实 microtasks 达 256-job 上限，报告 budget exceeded 并失败 |

共享 builder 的用例清单同时供 native/guest startup 与 Core 消费；新合同为原七项加十项，
恰好 17 项，不能以空结果或只有 global eval 的报告签发新成绩。跨宿主错误要求语义匹配及非零
退出，不把 Ubuntu 的 exit 1 强加给 Windows。guest manifest/import 已接入同一清单，03 未
声明在其他 OS/CPU 执行新增合同；04 需使用新的 producer、helper 和真实执行证据。

## 候选、晋级与普通 producer

[完整候选构建](../../out/v15/03/candidate/build-info.json)在 Ubuntu 22.04.5 LTS、WSL2 Linux
`6.6.87.2-microsoft-standard-WSL2`、x86_64、4096 页、`native` 模式真实编译并运行 17/17。
[受控 startup](../../out/v15/03/candidate-startup.json)独立执行同批原字节并通过。
构建前后 source/toolchain、原 ZIP、完整内容/类型/链接、派生 ELF 工具视图与独立源码 helper
均由原入口重新验证；DrvFS 模式限制继续如实记录，没有修复权限、移动缓存或下载新 Nightly。

[晋级收据](../../out/v15/03/promotion.json)分别绑定 02 的 exact source/ARM64 64K 证据与 03
的新 Host/recipe 证据。02 不证明新增 C Host；03 不继承 02 的旧 recipe 身份。
晋级只改变 state，`input_id` 仍为
`ba5f300846fa80ee131d01592298e8da964c6ce3d6b6566856b76c7124ea1a8b`。
当前 pinned 锁为 8051 字节、SHA-256
`3edb402d03eb788464bbbd68386f58cbd43512148be4439852e9c1d743f1a4e3`；候选锁原字节另存
`out/v15/03/candidate.lock.json`，没有修改候选 producer 的身份。

晋级后从 `dev.py build-ape` [重新执行普通构建](../../out/v15/03/pinned-workflow.json)，
[新 producer](../../out/v15/03/pinned/build-info.json)为 `controlled_probe=false`、17/17；
[普通 startup](../../out/v15/03/pinned-startup.json)也为 17/17，并保留产品和 loader 前后摘要。
构建命令、flags、超时、原始 stdout/stderr、双 CPU header/map/import 与 helper 身份均在
各报告的 `commands`、`build`、`input_reports` 和原 evidence 目录中。
使用 `-Os -fwrapv -D_GNU_SOURCE -DCONFIG_VERSION="2026-06-04"`，关闭 Temporal/Intl 和 LTO。

| 当前普通制品 | SHA-256 |
| --- | --- |
| `hello.com`（754446 字节） | `467f8889db7a671cd238a7cc56df933951fea686c7c524480f4112870da3695d` |
| `quickjs-eval.com`（2274662 字节） | `b7c7e0c6692dc221574e55937003be3dd1c81314db99a89b37ad96715c18598d` |

Hello 字节与旧版相同，但这里重新构建/执行并绑定新 producer；不沿用旧运行成绩。
固定 QuickJS Commit/Tree、ordered ARM64 overlay、cosmocc 原 ZIP 和 `libc_source: unproven`
不变。所有可复用输入及构建目录均在主工作区物理 `.cache/`；新实际报告与交付物均在主工作区
`D:\Workspace\dairn\qaplet\out\v15\03`，不依赖实施 worktree 的临时输出。

## 红绿与拒绝

第一项真实 APE [红测试](../../out/v15/03/esm-export-red.log)证明旧 eval Host 不支持 ESM 模式。
随后用 Ubuntu 系统 C 编译器运行 native C Host 的逐项 red/green：export/dependency、模块错误、
jobs 完成、job/rejection 错误、unfinished/budget。每次实际源码 SHA、编译/链接命令、进程
结果和原始 unittest 日志在 `out/v15/03/{dependency,module-errors,jobs,job-errors,unfinished}-*.json`
及同名日志目录；native 诊断不是 cosmocc fat APE 产品成绩，最终产品由上述完整 producer 签发。
[清单红测试](../../out/v15/03/catalog-red.log)另外拒绝只有旧七项的实际 producer。

[六项实际拒绝与 deadline](../../out/v15/03/refusals/summary.json)通过：缺制品、复制品改字节、
02 旧 producer、候选报告冒充普通报告均在执行前拒绝；report 目标为目录时 CLI 非零并保留
原始错误；当前 APE 的不终止表达式在 1 秒 deadline 被共享 process-group kill/reap 收回，
`timed_out=true` / exit -9，原制品摘要不变。没有删除或损坏唯一原 ZIP/源码缓存。

Windows 创建的 linked worktree 原本含绝对 `D:/...` Git 指针；WSL 预检失败被保留，实施时
仅将本 worktree 的 `.git` 指针改为相对路径。未改公共 Git metadata、系统设置或缓存布局。
修正后的 selected 预检按 gate 拒绝；[pinned 预检](../../out/v15/03/preflight-pinned.json)通过。

[最终普通 `dev.py check`](../../out/v15/03/check-final-workflow.json)通过：197 tests、0 failures/errors、
51 个明确平台/数据 opt-in skips；真实系统 C bootstrap 通过，`m0_core_status` 保持 `not_run`。
本次显式选择新 producer、官方 Linux loader 和普通 startup，八项新增 Host/consumer 合同全部
执行，包括缺失十项、导出错误、job 错误 exit 0 和错误路径 stdout 污染的拒绝，原始结果见
[完整日志](../../out/v15/03/check-final/command-0.stderr.log)。此前受 selected gate 阻断的普通
startup/输入合同现已运行。首次完整检查暴露两条旧 guest 用例数预期（8/11）；仅将预期更新为
新增十项后的 18/21，然后完整重跑通过；首次失败日志保留在 `out/v15/03/check/`。

新的完整 Core/offline pair、跨宿主与 ABI 验收由 04 负责，分级 CI 由 05 负责；不能把此前旧
recipe 的断网构建自动用于新 C Host。06 仍为 `needs-info`，没有存储上传、发布或远程 push。
