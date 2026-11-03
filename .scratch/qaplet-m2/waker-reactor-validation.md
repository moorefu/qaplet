# 02 · waker / Socket 真实 OS 探针

2026-11-03：独立探针及 producer/consumer 已实现；本地三个实际目标通过，完整 Cross 尚待集成分支运行。本记录只证明 OS 可实施性，不签发 Qaplet Runtime、SDK、generation 过滤或网络产品能力。

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
