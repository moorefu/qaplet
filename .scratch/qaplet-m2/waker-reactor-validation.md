# 02 · waker / Socket 真实 OS 探针

2026-11-03：独立探针及 producer/consumer 已实现；本地三个实际目标通过，完整 Cross 尚待集成分支运行。本记录只证明 OS 可实施性，不签发 Qaplet Runtime、SDK、generation 过滤或网络产品能力。

最新状态：首次 Full 的两 macOS 在 `socket.connect-error` 失败；下文保留旧验收，新增最小夹具修复和新本地证据。新字节尚待原生 macOS Cross，本票仍为 awaiting_cross。

## 实现与输入身份

- 实施分支 `codex/m2-02-waker`，确认起点祖先为 `5685bb6e26b9b181ccf68208c73f69e355ce7275`。实现提交 `e4bd0e6`、`804f521`；报告绑定的源提交为 `804f5216602562bf906630979d47a771f3ecb01d`。报告同时绑定实际源文件和配方字节，提交号不暗示整个 worktree 干净。
- 新入口为 `scripts/m2_waker.py`，探针源为 `tests/native/m2_waker.c`，公开 CLI 合同为 `tests/contracts/test_m2_waker.py`。未修改旧 M0 producer 或其配方。
- v3 锁为 `pinned`，锁 SHA-256 `eb0c7dcdb401ca3739f9e3ce863604e486b526d534579417d17b3e05b9da32f9`；input_id `f07f23c64bfcfb4a92722723e3e673213534a6fa81e690c401bd9fa10e0fd178`。
- 固定 cosmocc 原 ZIP：419652152 bytes，SHA-256 `f2f984d3a9790674fc0da11c1c00a32d4fe3f8dafde99af74b0c6dc24a2a16cf`。构建前后均离线核验原件、完整工具链内容与派生 helper；DrvFS 权限限制保留在原报告，未放宽字节检查。
- Ubuntu 正式产物 `m2-waker.com`：977339 bytes，SHA-256 `35bf4ffb75ec41476daa99d84ae145995a202eaa789ed91f91f2906c8a5642f9`。三个目标消费该同一摘要 APE；ARM64 loader 单独绑定。
- source 摘要 `c2cd706e417adfe080628921427b83cbc2c662e288fd47fbe4c50a834b39fd6a`；build 配方摘要 `2a2275255dff382e28958e1bb966f2ba7a4aed8bcf3ad444f8f58503b40dc80a`。构建中源/配方变化会拒绝签发 producer。

## 真实执行与 backend 结论

| 目标 | 执行方式 / 实测内核 | 页大小 | 结果 |
|---|---|---:|---|
| Linux x86-64 | native，WSL2 `6.6.87.2-microsoft-standard-WSL2` | 4096 | 19 / 19 pass |
| Windows x86-64 | native，正常用户执行上下文，`10.0.22000` | 4096 | 19 / 19 pass |
| Linux ARM64 | emulated-system，`6.8.0-147-generic-64k` | 65536 | 19 / 19 pass |
| Linux ARM64 原生 | 未执行 | 未测 | not_run |
| macOS x86-64 / ARM64 | 未执行 | 未测 | not_run |

64K 使用固定 QEMU `6.2.0`、machine `virt-6.2,gic-version=3`、CPU `cortex-a72`、`tcg,thread=single`。kernel/config/rootfs/QEMU 可执行文件/serial 原件均绑定；guest 实测页大小、别名、smaps 相符，payload 前后摘要一致。QEMU 正常退出、未超时，RAM-only、无磁盘、无外部网络；不外推物理 ARM64 性能。

探针使用 nonblocking loopback TCP pair 作为 socket waker；owner 同线程 `poll`，状态在 mutex 内先发布再发信号。helper 上限为实例 1 个，每个线程栈 262144 bytes，逐个创建/加入；7 个 helper 全部 join，最终 live_helpers=0。关闭与通知在同一 mutex 下确认先后顺序，不跨仍在使用的端点释放资源。取消是状态发布加 socket 通知，再 join；不强杀线程。

三种交接分别覆盖通知先于状态检查、空检查后准备睡眠、已经进入一次无限 `poll`。后者由 condition 交接后经过明确的 100ms 通知闸门，并要求本次单个 `poll(timeout=-1)` 实测至少 80ms；没有随机 sleep 或周期性短轮询。实测等待分别为 Linux 100.133ms、Windows 105.610ms、64K guest 102.315ms；对应进程 CPU 为 49 / 0 / 2344 微秒，来源 `getrusage(RUSAGE_SELF)`。maxrss 原始返回为 28028 / 4824 / 0，单位及支持程度按 OS 保留；guest 的 0 不宣称实际 RSS 为零。这些是探针观察值，不是统一性能门槛。

1000 次接受合并为 1 个 signal byte，状态计数仍为 1000；额外独立用例只注入通知、不发布业务状态，owner 观察到 0 项工作，排空后不再就绪。受控故障用真实关闭的 notify fd 触发 syscall 失败，保留 accepted=1、observed=1、独立控制错误；该行标识为 controlled-system-fault，不能替代正常产品故障合同。

Socket 覆盖非阻塞 connect、空/就绪读、半关闭与反向读写、对端关闭/EOF、拒绝连接、无效描述符和实际数字 fd 复用。全部保留原始 `revents` 及运行平台的 `POLL*` 符号值，HUP 不作为跨平台统一位值。

失败 connect 的实测分路尤其需要保留：

- Linux / 64K：`poll=1`，raw revents=28，SO_ERROR=111，符号解释含 error/HUP。
- Windows：`poll(2000ms)=0`、raw revents=0；随后显式 `select` 写集合返回 1，SO_ERROR=10061。因此当前 Windows 证据只支持已连接 socket / waker 使用 owner poll；失败 connect 必须采用已实测的 select 路由，不能宣称 poll 本身完整报告连接错误。探针中的分路和两个等待结果均可见，不是隐藏 fallback。

数值 fd 复用只证明 OS 会复用，过时代次过滤仍由后续 Reactor / 资源身份票从产品公开接口验收。

## CLI 与证据原件

本地原件位于 worktree 的 `out/m2/waker/accepted/`；清理 worktree 前应保存该目录及其 `evidence/`，不能只保留此笔记。

| 文件 | 含义 |
|---|---|
| `build-info.json`、`artifacts/` | Ubuntu producer、输入/配方/APE 双架构审计与供应 helper |
| `linux.json`、`windows.json` | 实际 native OS consumer，逐项结果及原始命令日志 |
| `payload-64k.json`、`export-64k.json` | 显式 guest 清单；导出本身不代表执行 |
| `guest-64k.json`、`arm64-64k.json` | 完整系统实际传输/执行与只读 M2 consumer |
| `contracts-linux.json`、`contracts-windows.json` | 两平台各 6 项 CLI 合同全部通过，0 skipped，保留外层原始日志 |

构建命令在 Ubuntu worktree 执行；共享输入/缓存始终指向主仓库物理 `.cache/`：

```sh
python3 scripts/m2_waker.py build \
  --toolchain-cache /mnt/d/Workspace/dairn/qaplet/.cache/toolchains/cosmocc \
  --helper-source-cache /mnt/d/Workspace/dairn/qaplet/.cache/sources/ape-loader \
  --build-dir /mnt/d/Workspace/dairn/qaplet/.cache/build \
  --output-dir out/m2/waker/accepted/artifacts \
  --report out/m2/waker/accepted/build-info.json
```

`run` 必须传 `--build-report` 和 `--artifact`；Linux/macOS 还传供应 `--loader-dir` 或固定官方 `--loader`。Windows 直接以 PE 执行，未构建。`guest-manifest` 使用供应 `ape-aarch64.elf` 与 `--page-size 65536`，随后 `arm64_guest.py run --cache /mnt/d/Workspace/dairn/qaplet/.cache/guests` 实际执行，最后 `import-guest` 只读核验。报告 scope 分别为 build / host / manifest / guest，均保留 `product_support_verified=false`。

CLI 验收执行：

```sh
QAPLET_TEST_M2_WAKER_PRODUCER=.../accepted/build-info.json \
QAPLET_TEST_M2_WAKER_LOADER=.../tree/bin/ape-x86_64.elf \
QAPLET_TEST_M2_WAKER_GUEST_REPORT=.../accepted/guest-64k.json \
python3 -m unittest discover -s tests/contracts -p test_m2_waker.py -v
```

Windows 设置 producer / guest 两项并用正常用户上下文执行同一测试，省略 loader。6 项合同包含真实供应物正常执行、改字节在启动前拒绝、输入缺失 blocked、报告不能覆盖供应原件、外层 deadline 确认进程回收且仍为 fail，以及真实 guest 原件的 4 个拒绝子用例：缺项、重复、将 observed 改为 0 但保留 pass、payload 执行后摘要变化。guest 变体复制原始证据并重算变更日志摘要；没有 synthetic success。

## 失败历史与未完成范围

- TDD 红灯：入口不存在时不能写阻塞证据；首次缺探针源的 Ubuntu 构建 fail；首份 Windows 探针因失败 connect 尚无 select 分路 fail；多余通知用例未实现时公开 CLI 测试红灯。后续变更和正式三目标结果分开保留。
- 受限 Windows agent 上下文中，新的 APE 与已有旧 M0 native-io / Hello 对照都在 main 前无输出挂起；正常用户上下文的新 APE可执行。`out/m2/waker/first/windows.json` 保留受限超时，`windows-user-context.json` 保留真实失败 connect 差异。该受限上下文不外推为系统未支持，也不签发通过。
- `out/m2/waker/first/`、`final/` 和 `diagnosis-windows.ndjson` 是历史/探索成绩，不与 `accepted/` 混合；隔离 no-pause / minimal 变体不作为验收来源。
- 本票尚不能标记 resolved：macOS 两目标、Linux ARM64 native 与适用 Full 回归待集成分支统一执行。M2 生命周期/长测、SDK 公共合同、生成代次过滤、继承/Console/Pipe 等仍由各自票验收，当前 OS 探针不替代它们。

## 首次 Full macOS 失败与最小夹具修复

首次 Full [41599033541](https://github.com/moorefu/qaplet/actions/runs/41599033541) 绑定 `5fb999fda5b671bbf559ea66db2d91741355123b`。Darwin 24.6.0 的 x86_64 / 4096 与原生 aarch64 / 16384 各 18 pass、1 fail，唯一失败为 `socket.connect-error`；suite 退出 1、未超时、stderr 为空。原始观测均为 `connect=-1, errno=115, poll(2000ms)=0, select(5s)=0, SO_ERROR=0, connection_failed=false`。旧 Linux x64、Linux ARM64 native 和 Windows x64 各 19/19；它们不能覆盖 macOS 的失败。

完整交接及原件位于主树 `out/qaplet-m2/initial-cross/HANDOFF.md`。修复树为 `.cache/worktrees/m2-02-fix`、分支 `codex/m2-02-macos-fix`，起点 `c338ca9`；新输出独立位于修复树 `out/m2-02-fix/`，没有写回旧原件。

### 红灯、收敛与可证伪假设

先实际执行原件重放：

```powershell
python D:/Workspace/dairn/qaplet/out/qaplet-m2/initial-cross/replay_mac_waker.py --report out/m2-02-fix/macos-failure-red-replay.json
```

实际退出 1，`failure_reproduced=true, targets=2, failed_case=socket.connect-error, new_native_execution=false`。收敛后的诊断信号是唯一失败记录：连接持续 pending，等待未报告就绪，也未取得 OS 错误；其余 18 项不失败。这里最小化的是保存的观测，不是新的 Darwin syscall 实验；本机无原生 Mac，不能把秒级旧 trace 重放称为新夹具的绿灯。

在改变夹具前给出以下排序假设及预测：

1. bound、未 listen 的 reserve 在 Darwin 使连接保持 pending；释放该 reserve 后同地址的真实 nonblocking connect 应取得错误。
2. Cosmopolitan 的 Darwin poll/select 连接错误通知均有缺口；释放 reserve 后仍 pending，需新原生对照才能区分。
3. 首次 SO_ERROR 读取清除了迟到错误；若先 select 后读 SO_ERROR 改变结果则支持它，但旧 trace 首次已读 0 使此项较弱。
4. 地址或套接字设置错误；其它相同 loopback 设置的成功 connect 会使此项较弱。

Apple 的 [XNU 固定源码](https://github.com/apple-oss-distributions/xnu/blob/xnu-11417.140.69/bsd/netinet/tcp_input.c#L2321)将存在且为 TCPS_CLOSED 的端点送入 drop，而[无对应 PCB 的路径](https://github.com/apple-oss-distributions/xnu/blob/xnu-11417.140.69/bsd/netinet/tcp_input.c#L2177)可进入 reset 路径。这支持假设 1，不证明该 CI 内核二进制的具体执行路径；新的 macOS 原生结果仍是最终验证。

公开 CLI 原有真实供应物测试先增加端口释放事实、真实错误和等待就绪断言；使用旧正式本地 producer 执行该单测，实际 1 test / 0.815s 失败，`reserve_closed_before_connect` 缺失，日志为 `connect-refusal-red.log`。这是候选夹具合同的红灯，与前述真实 macOS 失败分开记录。

### 最小修改与新本地结果

仅修改 `tests/native/m2_waker.c` 与公开 CLI 回归：在原 bind/getname、client nonblock 设置后，调用既有 `closed` 实测 close 成功且 F_GETFD 返回 EBADF，再对原 loopback 地址发起 connect。原始记录新增 `reserve_closed_before_connect` 和 `target_port`。端口释放至 connect 之间存在别的本地进程抢占端口的可能；若连接成功或错误没有实际就绪及非零 SO_ERROR，既有 consumer 仍失败，不重试或伪造拒绝。

consumer `scripts/m2_waker.py` 字节不变，SHA-256 为 `bf34d114f6dc62d08ea871fdb665c31fbf7a899907b610659c217d550fa90fb5`。未扩大 2 秒 poll / 5 秒 select 时限，未把 pending 当成功，未改宿主网络、安全策略或 sysctl。`macos-failure-preserved-after-fix.json` 再次以同一不变 consumer 重放旧原件，仍为两目标同一失败，退出 1。

Ubuntu 使用既有固定输入和主树物理 `.cache/` 构建，新 APE 为 977325 bytes、SHA-256 `c1507c60f6800e45847fef05bb7a5cc1d4a5a110fb65dda221d93746beb630b7`；source 集合 SHA 为 `5bb85bcce1c84bc7da4d3333834d516dbdc1da80a057e6ca1ef56d23748da33a`，build 配方 SHA 为 `610a3afb16dce19a17302fe3ea3c8e08ae06b7c3c7812debed2c15086381b383`。报告记录构建时 HEAD `c338ca9` 及实际未提交源字节，不把 HEAD 外推为工作树干净。

| 新供应字节目标 | 实测执行 | 结果 | connect-error 原始事实 |
|---|---|---|---|
| Linux x64 | native，WSL2 6.6.87.2，4096 | 19/19 | errno 115，poll 1，revents 28，SO_ERROR 111 |
| Windows x64 | native，正常用户上下文，10.0.22000，4096 | 19/19 | errno 115，poll 0，select 1 / writable，SO_ERROR 10061 |
| Linux ARM64 64K | emulated-system，6.8.0-147-generic-64k，65536 | 19/19 | errno 115，poll 1，revents 28，SO_ERROR 111 |
| macOS x64 / ARM64，Linux ARM64 native | 新字节尚未执行 | not_run | 等待新原生 Cross |

三个已测目标的 `reserve_closed_before_connect=true`，端口均非零；socket 资源全部关闭，7 个 helper 全部 join，live_helpers=0。64K 仍使用固定 QEMU 6.2.0 / virt-6.2,gic-version=3 / cortex-a72 / TCG 单线程，guest 实测页大小及 smaps 相符，payload 前后摘要相同；QEMU 正常退出、未超时，RAM-only、无网络和磁盘，无 host global changes。

新 Linux / Windows 公开 CLI 各 6 tests 通过、0 skipped，耗时 4.197s / 5.415s；含真实供应物正常执行及新增拒绝夹具事实、改字节启动前拒绝、缺输入 blocked、输出碰撞拒绝、外层 deadline 真实回收、真实 guest 证据的四类拒绝变体。原件为 `contracts-linux.log`、`contracts-windows.log`；`build-info.json`、`artifacts/`、`linux.json`、`windows.json`、`payload-64k.json`、`export-64k.json`、`guest-64k.json`、`arm64-64k.json` 与全部 `evidence/` 保留在新输出目录。

纯只读 Windows `preflight-windows.json` 通过；WSL 直接读取 Windows worktree `.git` 路径失败的首次预检单独保存在 `preflight.json`，不是探针失败。提交 `ec180f1` 后合入 integration tip `af6c0ff`，merge 为 `445763b`；仅在新 ignored 输出目录放置当前 worktree 的只读 Git 路径映射，fixture 的 init/config/写操作仍使用真实 Git，不设全局 GIT_DIR、不改共享配置。

合入后的第一次 `dev.py check` 真实失败，原件 `check.json` / `check/command-0.stderr.log` 保留：345 tests / 92.270s、169 skipped，唯一失败为真实供应物 CLI 单测，consumer 原因为 `Real OS probe did not complete every required case`。旧单测的 TemporaryDirectory 已自动清理当次原始 report，笼统断言没有保留具体失败 case；因此不能归因为 connect 夹具、计时或宿主负载，也不能宣称这次未归因失败已修复。

为避免再次丢失诊断，CLI 单测现在在失败消息中保留所有非 pass 的 case_id / actual，没有改任何通过判据。相同环境立即重放通过；诊断循环 `repeat_probe.py` 保留 `repeat/000.json` 至 `099.json` 及各自 raw evidence，100 次实际公开 CLI 全通过，waker.blocked 的实测等待为 100.068–105.964ms。这些重放没有把首次失败改写成绿灯或已确诊原因。

带诊断的第二次完整 Ubuntu `dev.py check` 通过，原件为 `check-diagnosed.json` 与 `check-diagnosed/`：input-lock / CI 合同通过，345 tests / 85.320s、169 skipped，随后实际系统 C bootstrap 通过。普通检查未供应范围的 skips 不签发完整产品验收；02 的六项专门 CLI 合同均执行。本票仍待新 macOS 原生及适用 Full，保留上述未归因本地失败历史，尚不能 resolved。
