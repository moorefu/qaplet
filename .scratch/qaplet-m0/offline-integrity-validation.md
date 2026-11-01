# BOOT-08：断网完整性入口与实际拒绝基线

2026-11-01，`codex/m0-09-offline-integrity` / `out/implement-spec/09`，从 integration `ab254628` 开始且 ancestry 检查通过。按 implement-spec / TDD 沿用已确认的公共 CLI、真实 OS 和结构化 evidence seams。

**当前状态：09 仍为 claimed。尚未执行最终两个实际断网重构，不能关闭 BOOT-08 或 Core。** 07 正在修正 64K loader / packer；此票必须合入该统一 producer 后，再用最终配方和新 artifact 身份执行双构建。没有用旧 16K producer 的相等字节代替最终验收。

## 已实现入口

`scripts/offline_integrity.py run` / `make test-offline` 创建专用网络 namespace，实际工作降为非 root UID/GID；preflight、两个独立构建、精确物理 cache inventory、strace 网络/进程审计、六个二进制比较和复制负向 fixture 组成一次有界运行。缓存、HOME/XDG/TMP、构建中间件均在主仓库 `.cache/`，报告/原始日志/最终制品在 `out/`。没有下载或更新 fallback，不改宿主网络、binfmt、WSL 或 mount。

`preflight` / `refusals` 可独立执行；其 reason 明确不声明产品重构。`inventory` / `compare` / `audit-network` 为独立证据消费 CLI，不自行声明实际断网执行。`run` 完整 producer 编排尚待 07 合入后的第一次真实执行；源码预期读取 07 的 `third_party/ape-helpers/source.lock.json`、`scripts/ape_helpers.py` 和 `--helper-source-cache`，这些文件不由 09 复制或另改。

## 已实际运行

所有编译和 aggregate check 均在 Ubuntu-22.04 / WSL2 Linux x86_64 / 4096 字节页执行。Windows 只编排 WSL、读取报告和写源码。

- `out/implement-spec/09/out/ticket09/preflight.json`：真实 fresh network namespace 通过，父 `net:[4026531840]`，实际 child `net:[4026532227]`（逐次 namespace 身份不同）；UID/GID 1000，`socket.if_nameindex()` 只有 `lo`，无 IPv4 route，literal `203.0.113.1:443` connect 被内核以 errno 101 / `ENETUNREACH` 拒绝。sysfs 共享网卡目录没有被当作隔离证明。
- `out/implement-spec/09/out/ticket09/refusals.json`：10/10 实际复制负向用例通过。缺源码、改 `quickjs.c`、改 patch manifest、未锁 HEAD、缺原 ZIP、坏 ZIP、缺工具树文件、占位 compiler digest、冲突 GitHub provenance、缺 Zig 分别得到对应非零退出和明确原因。每个 supplier report 的 executable commands 为零，独立 strace 只有 initial Python `execve` 一次，internet attempts 为零。fixture 全在主仓库 `.cache/validation/offline/`，没有修改实际 source/toolchain cache。
- `out/implement-spec/09/out/ticket09/refusals-final-baseline.json`：同组最终基线增加每个复制 fixture 的精确内容 inventory，并将 HOME/XDG/TMP 全部限定到主仓库 `.cache/`；保留前一次报告/trace，不覆盖历史原始目录。
- `out/implement-spec/09/out/ticket09/implicit-network.strace.log` 和 `implicit-network-refused.json`：单独的诊断 fixture 在 fresh namespace 真正发起外部 connect，实际被内核 errno 101 拒绝；公共 `audit-network` 仍退出 1 / fail。内核阻断不能掩盖 build 的隐式联网企图。该 fixture 不属于产品 build。
- `out/implement-spec/09/out/ticket09/check/`：Ubuntu `make check` 通过，72 个合同、0 失败、6 个显式平台/重型入口 skip，静态 CI 检查和真实系统 C bootstrap 通过；这不是尚未执行的双构建通过。输出为 `check.stdout.log` / `check.stderr.log`。
- 四个新 CLI 合同按红绿逐项执行：普通 worker 拒绝且不碰 cache；相同 fixture 二进制比较通过、改 ARM sidecar 则失败并保存首个差异 offset；outward cache link 拒绝；已被内核拒绝的 implicit connect trace 也拒绝。focused 4/4 通过，无私有函数测试或内部 collaborator mock。

## 续作所需

1. 合并包含最终 07 producer 的最新 integration；复读其 helper 输入/配方接口，补实际缺失/损坏 helper source 拒绝用例。
2. 在 Ubuntu root 入口运行 `scripts/offline_integrity.py run --cache-root /mnt/d/Workspace/dairn/qaplet/.cache --uid 1000 --gid 1000 --report out/ticket09/final/offline-integrity.json`。实际命令应通过 namespace 后降到普通 UID；每个 build 默认 1200 秒上限。
3. 独立核对两次原生 case、最终 Hello/eval SHA、两个 CPU sidecar、来源/配方、真实网络 trace 和 before/after inventory；修复所有差异，未解释差异保持 fail。便宜时额外比较 07 每次生成的 ARM loader；06 单独 macOS ARM64 helper 的重复构建目前未纳入，也不声明其可复现。
4. 最终 `make check`、最新 integration merge、证据文档/票据状态更新后才 resolve 09。当前没有最终 artifact SHA、没有填造双构建通过状态。
