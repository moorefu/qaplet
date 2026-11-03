# 06 · 等待、外部事件、宿主保活与停止

本票只交付显式 M2 Runtime 的等待、私有 connected TCP waker、宿主令牌、
外部业务事件和 stop。timer/immediate、work、process、stdio、HostNotify
仍由后续票实现；没有成功占位或隐式 JS 线程。最终 T03 全部资源混合负载、
所有目标 Cross/Full 及 64K 由 integration 统一签发，本地通过不外推这些范围。

工作树 `.cache/worktrees/m2-06` / `codex/m2-06-wait` 从干净的
`78cce5cac8a79d8f81ad28c2ba38e65759c7e198` 开始。采用已确认 T01/T02 的
供应 SDK/包外纯 C Host 与最小 CLI 边界，未重复访谈。

## 公开合同与实现

- `QAPLET_CAP_M2_WAIT` 与实例模式分别查询。M1 创建不取得新能力、令牌或
  waker；旧头文件、旧错误数值、保留字段、非等待 pump 和结果槽语义保留。
- `qaplet_keepalive_create` 是 owner、非驱动/回调操作；
  `qaplet_keepalive_release` 与 `qaplet_wake` 可任意线程使用。opaque 值含
  Runtime 身份和代次，旧代次、重复或跨实例释放为 `INVALID_HANDLE`；代次
  耗尽不复用。令牌从创建起预分配、计原生字节；数量饱和为 `RESOURCE_LIMIT`，
  不改变调用方输出或已有结果。
- M2 的私有 waker 无 helper/JS 线程，不导出 fd/HANDLE。创建中同时最多三个
  socket，常驻两个 socket/一个 owner poll 注册，全部设置 CLOEXEC/nonblock。
  创建先检查有限句柄/注册额度；失败回滚，普通创建不启动未来 worker。
- 接受事件、stop、令牌释放与通知共用 mutex。状态提交之后 send 失败仍返回
  已接受，独立锁存 `CONTROL_FAILURE`、errno 域/数值并保留终止首因；必要时
  shutdown 发送端使正在等待的 peer 观察控制故障。提示可合并，不当作工作数。
- owner 在同一锁内排空提示、清 pending、再检查业务队列、存活与终止状态；
  解锁之后的生产者发布留下可读字节。没有周期性短轮询。
- run_once 先推进有界就绪工作，0 不睡眠；真正等待取累计剩余 max_wait 和
  活跃入口绝对期限的最小值，wake/EINTR 不重置预算。run 持续到自然空闲、
  普通错误或终止。Promise 本身不保活；末个令牌释放后仍处理已接受事件与
  完整 jobs，不能推进的 PENDING 返回 UNSETTLED_ENTRY。当前没有 unref 资源
  API，因此不将这些测试外推为后续 timer/work/process 的 unref 验收。
- 完整 jobs 检查点、跨 pump 执行账本、普通错误可复用、无进展最近结果保留
  沿用 M1。整个 pump/run/run_once/quiesce 有驱动保护，包含 PENDING/等待。
  M2 owner 可观察已 FAILED 的首因并立即启动原生清理，不再运行用户 JS。
- 终止确认即撤销令牌、关闭 waker；close 与生产者控制路径按锁序交接。
  每个 FD 在唯一一次 close 前退休。close 错误独立报告；确定仍持有与无法
  证明已关闭的句柄分别计量，后者持续 CLEANING/destroy BUSY，quiesce 不
  按旧数值重新关闭。保留 cleanup_reason，CLOSED 不复活。
  宿主仍须撤下并 join 外部生产者后才 destroy。
- 扩展 POD 快照公开句柄、注册、令牌当前/峰值，wake/send/wait 饱和累计计数，
  原生字节及控制错误。CLI 使用 M2 驱动，先复制入口结果再继续自然存活，并
  在 close 后有限 quiesce；信号/终端等完整 CLI 合同仍在后续票。

## TDD 与真实故障

1. 公开等待合同在缺少令牌接口/资源快照时，Ubuntu `cc -fsyntax-only`
   实际编译失败；第一段实现后，同一供应 SDK Host `--m2-wait` 通过。
2. `out/m2-06/slice1/` 是第一段供应物，106 项公开合同通过。扩展的四个
   公开 tracer 用其真实头/archive 验证事件、释放、stop、身份和等待期限。
3. 受控 send 故障先得到红灯 `code=BAD_STATE, reason=IDLE`：已接受投递在
   生产者把 Runtime 置 FAILED 后，旧驱动预检拒绝 owner 确认。修正 M2 驱动
   与非等待 pump 的终止观察范围后，独立系统夹具通过。
4. `out/m2-06/final/` 保留修正后的 111 项普通合同和独立 fault-consume
   通过原件。它还不包含最后的 close 诊断修正，不能混为最终供应字节。
5. `out/m2-06/close-fault-red.log` 实际红灯：close syscall 被明确拒绝且
   fd 仍存在时，close 仍返回 OK。修正为独立清理诊断；新增受控合同要求
   CLOSED/CLEANING、destroy BUSY 及独立清理诊断；下一步进一步核对 FD 复用安全。
6. root 的关闭边界检查指出 fcntl 看到同号有效也不能证明仍是原资源。
   供应报告 `libc_source.status=unproven`，没有 archive→源码 commit 的已验映射，
   因而不能用任一上游源码推断本供应库的各 OS close 行为。
   [Linux close 手册](https://man7.org/linux/man-pages/man2/close.2.html)
   也明确指出错误后重试可能关闭其他线程复用的 FD。该文档只支持保守策略，
   不作为固定 Cosmopolitan archive/所有 OS 的源码映射证明。
   最终实现采用一次性退休与 `native_uncertain_handles`。受控 fixture 同时
   覆盖“明确拒绝、原 FD 未释放”和“实际释放后立即复用同号 FD”两种事实；
   后续 quiesce 不碰复用对象，Runtime 保持 CLOSED/CLEANING，destroy BUSY。
   fixture 自行关闭其对象，两个未证明清理的 SDK 控制句柄有意保留到进程退出。
   不把这些负向用例称为原生清理成功；真实 close 错误仍可能永久不能 destroy。
7. 最终原件选择 `out/m2-06/verified/`，正常产品与
   `controlled_system_fixture=true` 的故障包分别绑定源、ZIP、archive、
   链接 wrap 参数和执行日志。故障覆盖初始化、socket/accept 回滚、精确
   pre-sleep 交接、整段重入、send/recv/poll/时钟失败、时钟倒退、首因及 FD 复用。
   中间 `closure/` 保留为诊断修改历史，不作为最终选择。

所有构建使用 Ubuntu-22.04 与固定输入，主物理 `.cache` 保持原路径。
Windows linked-worktree 的 Git 路径只在 ignored `out/m2-06/bin/git` 中为
当前树的只读查询映射；fixture 写操作与其它仓库透传，不设置全局 GIT_DIR
或共享 core.worktree。首次未识别 `git -c ... -C` 的只读 shim 失败留在会话，
修正后供应构建通过；没有更改共享配置。

因 D 盘触发既有 4 GiB 门槛，按 root 的统一恢复指令对本票闲置派生缓存
执行标准 NTFS `compact /C /S /I /Q`（无 `/EXE`），未改布局/阈值。
`out/m2-06/cache-compact.json` 记录 87 个既有文件前后 SHA/长度完全相同、
0 reparse；50,471,144 字节存为 26,770,165 字节。后续新派生文件继承压缩。

## 可重放入口

以下均从本工作树执行；Windows 用同一供应 APE/SDK，不编译产品。
`SUPPLIED_BIN` 是主缓存已核验工具链的 `tree/bin`。

```sh
python3 scripts/sdk.py build \
  --source-cache /mnt/d/Workspace/dairn/qaplet/.cache/quickjs \
  --toolchain-cache /mnt/d/Workspace/dairn/qaplet/.cache/toolchains/cosmocc \
  --helper-source-cache /mnt/d/Workspace/dairn/qaplet/.cache/sources/ape-loader \
  --build-dir /mnt/d/Workspace/dairn/qaplet/.cache/build/m2-06 \
  --output-dir out/m2-06/verified/artifacts --report out/m2-06/verified/build.json
python3 scripts/sdk.py run --build-report out/m2-06/verified/build.json \
  --artifact out/m2-06/verified/artifacts/qaplet-host.com --loader-dir SUPPLIED_BIN \
  --report out/m2-06/verified/linux.json
python3 scripts/sdk.py package-consume --build-report out/m2-06/verified/build.json \
  --artifact out/m2-06/verified/artifacts/qaplet-host.com --loader-dir SUPPLIED_BIN \
  --source-cache /mnt/d/Workspace/dairn/qaplet/.cache/quickjs \
  --toolchain-cache /mnt/d/Workspace/dairn/qaplet/.cache/toolchains/cosmocc \
  --helper-source-cache /mnt/d/Workspace/dairn/qaplet/.cache/sources/ape-loader \
  --output-dir out/m2-06/verified/package-artifacts --report out/m2-06/verified/package-consume.json
python3 scripts/sdk.py fault-consume --build-report out/m2-06/verified/build.json \
  --artifact out/m2-06/verified/artifacts/qaplet-host.com --loader-dir SUPPLIED_BIN \
  --source-cache /mnt/d/Workspace/dairn/qaplet/.cache/quickjs \
  --toolchain-cache /mnt/d/Workspace/dairn/qaplet/.cache/toolchains/cosmocc \
  --helper-source-cache /mnt/d/Workspace/dairn/qaplet/.cache/sources/ape-loader \
  --output-dir out/m2-06/verified/fault-artifacts --report out/m2-06/verified/fault-consume.json
```

本票独立重复范围使用同一供应 Host：

```sh
python3 out/m2-06/repeat_wait.py --build-report out/m2-06/verified/build.json \
  --artifact out/m2-06/verified/artifacts/qaplet-host.com \
  --loader SUPPLIED_BIN/ape-x86_64.elf --tokens 500 --concurrent 100 \
  --output-dir out/m2-06/verified/repeat-linux
```

500 次 `--m2-wait-tokens` 每次创建两个 M2 实例，核对公开令牌容量、
Runtime/代次、句柄当前 2/峰值 3、close 后资源零和实际 destroy；共 1000
个此类生命周期。100 次 `--m2-wait-concurrent` 每次实际启动/join 三个 producer，
分别覆盖等待中释放末令牌、复制事件后释放、等待中 stop；共 300 个附加实例和
300 次 join，每次 close/quiesce 核对真实句柄/注册/令牌归零。每次 stdout 必须
精确匹配 `sdk.py` 的公开 marker、stderr 为空、退出 0，前后供应 ZIP/SDK/APE/
loader 摘要一致。它们仅是 06 的资源重复范围，不能称为最终 T03 全混合负载。

## 当前验收状态

产品提交 `921f6f6`；合并当前 integration `0e4ba21` 后为
`2a9ab078415956a6f3c207d3f84f738209bde684`。最终供应物绑定后者。
构建前 Windows Git 为 clean，63 个 RECIPE 输入逐一与该提交 Git blob 的原字节
相同，0 mismatch、全部 LF；原件为 `out/m2-06/recipe-blobs.json`。

最终 producer 的 `working_tree_dirty=true` 如实保留：Ubuntu Git 不继承
Windows 全局 `core.autocrlf=true`，把原有 CRLF Markdown 等非 RECIPE 文档观测
为 modified。没有改这些文件、全局配置或旧 JSON；本地产物通过 63 项配方原字节
与固定提交绑定。后续 clean Ubuntu CI 重新构建签发准确提交。

| 选择 | 实际结果与范围 |
|---|---|
| `verified/build.json` | Ubuntu 固定输入 producer pass；commit 为上述 2a9ab07，dirty=true |
| `verified/linux.json` | Linux x86-64 native / WSL2 6.6.87.2 / 4096，111 pass，0 fail/skip |
| `verified/windows.json` | Windows 10.0.22000 x86-64 native / 4096，110 pass，1 optional blocked；全部必需项 pass |
| `verified/package-consume.json` | 密封 ZIP 包外纯 C 应用仅使用公开头/供应 archive，8 pass |
| `verified/fault-consume.json` | 独立受控系统边界，1 case pass；关闭不确定的两个负向实例明确不能 quiescent/destroy，不算清理成功 |
| `verified/repeat-linux/report.json` | 600 次公开调用全部 pass，41.783 秒；1000 token 实例 + 300 并发实例、300 join |
| `verified/repeat-windows/report.json` | 同一 SHA Host，600 次公开调用全部 pass，26.358 秒；同等本票资源范围 |
| `check-merged.json` | 合并后 Ubuntu dev.py check pass；364 tests / 87.611 秒，188 skipped；实际系统 C bootstrap pass |

Windows 唯一 optional blocked 是既有 `qaplet-host.module-symlinks`，
`required=false`、`winerror=1314`，未获得实际文件 symlink fixture 权限。
不把该用例计为通过；没有必需用例未执行。普通仓库检查中未供应的专门输入
产生 skips，不将其当成本票 111 项实际产品消费的成功替代。

最终 SDK 配方 SHA-256：
`a0d03eb529e15041af852ccaadf4f273a32c097106e8320577c1059427906265`。
SDK 集合 SHA-256：
`9cc4c0bd74c4f13b601d88e3dac98dfcc90b6c0f49e0d5dc776ac169a816ce93`。
供应原 ZIP 为 14,268,119 bytes，SHA-256：
`14189215ac6bf6be81d2c705d3af29a09e26888e2dd8b46a7f5d71e89247035e`。

| 最终 APE | bytes | SHA-256 |
|---|---:|---|
| Host | 5,115,065 | `d107e6ac9d8e25fabc401f29d72caf41d4b0ff94341ecb1c562ec0655df5db8e` |
| CLI | 2,428,307 | `fb073b446cc05b531e15a36bcbf14a8f0e857cf7f9be19801bdad2247beadeae` |
| 完整 C 应用 | 2,435,174 | `2b5d3c7472a6bbc32c6bfa5d13f22c8cb549194fe2fb7d3b6be05ba00b29c503` |

全部精确 argv、时刻、实际执行架构/页大小、原始 stdout/stderr/退出及供应输入
前后摘要在选定 JSON 和 evidence/ 下。loader 是固定主缓存原件
`/mnt/d/Workspace/dairn/qaplet/.cache/toolchains/cosmocc/f2f984d3a9790674fc0da11c1c00a32d4fe3f8dafde99af74b0c6dc24a2a16cf/tree/bin`。
Windows 通过 require_escalated 的正常用户上下文直接执行同一 APE，未编译产品。

重复的 Windows 精确入口（不提供 loader）：

```powershell
python out/m2-06/repeat_wait.py --build-report out/m2-06/verified/build.json `
  --artifact out/m2-06/verified/artifacts/qaplet-host.com `
  --tokens 500 --concurrent 100 --output-dir out/m2-06/verified/repeat-windows
```

## 保留的失败与限制

- `retired/fault-consume.json` 首次复用夹具 fail：新 socket 自动占用了 listener
  留下的更低空槽，没有自动复用刚关闭的数值。改为显式 dup2 形成确定同号复用，
  随后包外预跑与最终供应 fault-consume 均 pass；未放宽产品判据。
- ignored 重复 runner 首次遇到 catalog 的无参数 scalar 项触发 IndexError，尚未
  执行任何重复；原目录 `retired/repeat-linux`、`repeat-windows` 保留。过滤无参数项
  后，两平台 `retired/*-1` 的本票重复通过；最终 Host SHA 不同，因此 verified 两平台
  又实际各执行 600 次，未混用旧 SHA 的重复成绩。
- 发现公开 C fixture 的追加段有 CRLF 后统一为 LF，并先提交产品再构建；
  `accepted/` 保留旧原字节封装，不作为最终供应选择。
- 不能证明的 close 失败保持原生清理未完成，不重试可能复用的 fd、不伪造 destroy。
  该保守负向结果是当前明确边界；未验映射的 libc 源码不用于宣称各 OS 错误后必然关闭。
- macOS 两架构、Linux ARM64、实际 64K 及当前 integration Full 的本票新增合同
  尚待 root 统一重建/分发/严格消费；旧 01/02/04/05、M0/M1 Full 成绩不代替本票。

本票保持 `claimed / awaiting_cross`，不标 resolved，不签发 release-ready。
