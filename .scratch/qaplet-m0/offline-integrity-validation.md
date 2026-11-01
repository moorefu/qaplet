# BOOT-08：断网完整性入口与实际拒绝基线

2026-11-01，`codex/m0-09-offline-integrity` / `out/implement-spec/09`，从 integration `ab254628` 开始且 ancestry 检查通过。按 implement-spec / TDD 沿用已确认的公共 CLI、真实 OS 和结构化 evidence seams。

**09 已 resolved，最终断网执行与当前集成检查均通过。** 本次实际执行使用包含 07 最终 64K loader / packer 和 QuickJS ARM64 overlay 的 integration `42fb679` 加本票 runner 内容。没有使用旧 16K producer 的相等字节代替最终验收。Core 完整门禁仍由 10 汇总。

## 已实现入口

`scripts/offline_integrity.py run` / `make test-offline` 创建专用网络 namespace，实际工作降为非 root UID/GID；preflight、两个独立构建、精确物理 cache inventory、strace 网络/进程审计、六个二进制比较和复制负向 fixture 组成一次有界运行。缓存、HOME/XDG/TMP、构建中间件均在主仓库 `.cache/`，报告/原始日志/最终制品在 `out/`。没有下载或更新 fallback，不改宿主网络、binfmt、WSL 或 mount。

`preflight` / `refusals` 可独立执行；其 reason 明确不声明产品重构。`inventory` / `compare` / `audit-network` 为独立证据消费 CLI，不自行声明实际断网执行。完整 `run` 已真实消费 07 的 `third_party/ape-helpers/source.lock.json`、`scripts/ape_helpers.py` 和 `--helper-source-cache`；本票没有复制或改写其 producer。

## 已实际运行

所有编译和 aggregate check 均在 Ubuntu-22.04 / WSL2 Linux x86_64 / 4096 字节页执行。Windows 只编排 WSL、读取报告和写源码。

- `out/implement-spec/09/out/ticket09/preflight.json`：真实 fresh network namespace 通过，父 `net:[4026531840]`，实际 child `net:[4026532227]`（逐次 namespace 身份不同）；UID/GID 1000，`socket.if_nameindex()` 只有 `lo`，无 IPv4 route，literal `203.0.113.1:443` connect 被内核以 errno 101 / `ENETUNREACH` 拒绝。sysfs 共享网卡目录没有被当作隔离证明。
- `out/implement-spec/09/out/ticket09/refusals.json`：10/10 实际复制负向用例通过。缺源码、改 `quickjs.c`、改 patch manifest、未锁 HEAD、缺原 ZIP、坏 ZIP、缺工具树文件、占位 compiler digest、冲突 GitHub provenance、缺 Zig 分别得到对应非零退出和明确原因。每个 supplier report 的 executable commands 为零，独立 strace 只有 initial Python `execve` 一次，internet attempts 为零。fixture 全在主仓库 `.cache/validation/offline/`，没有修改实际 source/toolchain cache。
- `out/implement-spec/09/out/ticket09/refusals-final-baseline.json`：同组最终基线增加每个复制 fixture 的精确内容 inventory，并将 HOME/XDG/TMP 全部限定到主仓库 `.cache/`；保留前一次报告/trace，不覆盖历史原始目录。
- `out/implement-spec/09/out/ticket09/implicit-network.strace.log` 和 `implicit-network-refused.json`：单独的诊断 fixture 在 fresh namespace 真正发起外部 connect，实际被内核 errno 101 拒绝；公共 `audit-network` 仍退出 1 / fail。内核阻断不能掩盖 build 的隐式联网企图。该 fixture 不属于产品 build。
- `out/implement-spec/09/out/ticket09/check/`：Ubuntu `make check` 通过，72 个合同、0 失败、6 个显式平台/重型入口 skip，静态 CI 检查和真实系统 C bootstrap 通过；这不是尚未执行的双构建通过。输出为 `check.stdout.log` / `check.stderr.log`。
- 四个新 CLI 合同按红绿逐项执行：普通 worker 拒绝且不碰 cache；相同 fixture 二进制比较通过、改 ARM sidecar 则失败并保存首个差异 offset；outward cache link 拒绝；已被内核拒绝的 implicit connect trace 也拒绝。focused 4/4 通过，无私有函数测试或内部 collaborator mock。

## 最终实际断网双构建

2026-11-01 11:34:33–11:58:07 +08:00，在 Ubuntu-22.04 / WSL2 Linux x86_64 / 4096 字节页实际完成 [完整报告](../../out/implement-spec/09/out/ticket09/final/offline-integrity.json)，状态 `pass`、16/16 cases。命令如下；Windows 只编排 WSL，没有编译。

```sh
python3 scripts/offline_integrity.py run \
  --cache-root /mnt/d/Workspace/dairn/qaplet/.cache \
  --uid 1000 --gid 1000 \
  --report out/ticket09/final/offline-integrity.json
```

入口由 Ubuntu root 启动，只有 namespace setup 保留 root；真实 worker 和全部构建为 UID/GID 1000。父 namespace `net:[4026531840]`、实际子 `net:[4026532227]`，只有 `lo`，IPv4 route 为空，literal 外连 errno 101。两份完整 build trace 各有 192 次实际 `execve`、零 IPv4/IPv6 外连企图；没有依赖“连接最终失败”放过隐式联网。各 build 的七项 native runtime case 都通过，包含 pointer roundtrip 和 object/Map 生命周期。

两次工作目录分别位于主物理 `.cache/build/offline/6c4426f6-af9e-4139-8ab0-289d7074aa92/{first,second}/`，均为此前不存在的独立目录。两个 producer report、四份每次构建的 before/after supplier report、原始 compile/link/runtime stdout/stderr 和 strace 均保留于完整报告引用的 evidence 目录。没有 seed、下载、重试或替换锁。

精确 cache inventory 前后逐项相同：21,180 个文件、23,034 个文件/目录/link entries、2,815,491,333 字节，集合 SHA-256 `6f5de3bf2e21ca2932d684ef467a609983b0c8a467874fffbf8b548ff3c9d531`。它覆盖原始 QuickJS（包括实际 `.git/HEAD`）、原 Nightly ZIP/tree、完整 derived native view、锁定 helper sources、Zig archive/tree，并记录真实权限及 link targets。所列锁、两个有序 overlay 文件及 producer recipe 文件的前后身份也相同。HOME/XDG/TMP、构建和负向副本全部位于主物理 `.cache/`。

以下六个二进制均直接逐字节比较为相同，没有差异 offset。Hello/eval APE SHA 也与 07 最终 producer 相同。

| 文件 | 字节 | 两次共同 SHA-256 |
| --- | ---: | --- |
| `hello.com` | 754446 | `467f8889db7a671cd238a7cc56df933951fea686c7c524480f4112870da3695d` |
| `hello.com.dbg` | 1831502 | `4fe1964ed895731826b4be9552cca262f0003b8967f79f73760bacdb07e2a55d` |
| `hello.aarch64.elf` | 1400718 | `d0e1c9c6b1551822af20343f4be1be8681857f9970dfa291ee784ea773ee38e4` |
| `quickjs-eval.com` | 2273408 | `426a7b05367fc6fd8d4831eebb2439782b3b66141af115094cc1efa766b05cd4` |
| `quickjs-eval.com.dbg` | 3135532 | `1bf58403bc81c9d633a2cfbbc3d32da73a465dfead6a325642a3468c7eee9dd2` |
| `quickjs-eval.aarch64.elf` | 2657844 | `f4292843596df7228d71ea32ac5db3cb67ee438118d0cce6248438aaa89ca7f2` |

额外直接比较 source-built helper：ARM64 Linux loader `255583cb792109859388afc600366c8e048a07c68cb81a23e97846cd0837a74d`、packer `ae823b16816908e6dc6add65e772ab77479f6dd8447145cd55667e6b2070d26d`，均字节相同。这不包括独立 macOS ARM64 helper 的重构，不声明其可复现。

## 最终拒绝与输入安全

完整 `run` 的 14/14 copy-only negative cases 全部通过。原十项之外，新增缺 helper cache、缺 loader source、改 loader source、改 packer source；最后一项在 ordered overlay identity 检查即失败。每份 supplier report 的 executable commands 都为零，每份独立 trace 只有初始 Python `execve` 一次且零 internet attempts。各副本在执行前保存精确 inventory，没有修改实际供应缓存。先行同组独立 [14-case 报告](../../out/implement-spec/09/out/ticket09/final/refusals.json) 也通过。

先前的实际 implicit-network fixture 和公共 CLI 比较/外链拒绝合同保留；内核拒绝 connect 仍被 `audit-network` 判为失败。最终没有差异被解释文字豁免，`m0_core_status` 保持 `not_run`。

## 测试时配方与当前集成的区别

断网运行结束后才合入最新 integration `1129cd8`。其 06 修改了 `build_ape.py` 的 `check_ci()`；11 是独立 SCM 文件，不改变 Core producer。不能把本次已执行的原始 recipe fingerprint 改写成合入后的新值。

[配方比对报告](../../out/implement-spec/09/out/ticket09/final/recipe-current-vs-tested.json) 保存完整 old/new inventories 和 [原始 diff](../../out/implement-spec/09/out/ticket09/final/producer-ci-only.diff)：

- 实际双构建的 recipe inventory SHA-256：`dbf05fbe593bf0bfcf253b86f0bea7edf299cdf372349c6f868bbc92d6cb9a71`。
- 当前 raw recipe inventory SHA-256：`930102f78a6fed1da01000750a48fe56e1e5e3b1e5ac2756af6f756308551afb`；没有宣称对这个新 raw fingerprint 执行双构建。
- 唯一变化输入为 `scripts/build_ape.py`，SHA 从 `f75c655b5eb29e6d608c212bfa2496db095d46fd3a85b14366ea85fc9102e6cb` 变为 `cf71af2b9381423e6be53a6c4253932a0b8ee5bf2d3d315c1cd814e82d08ba2f`。去掉 `check_ci()` 后完整 AST 相同，实际 build/verify/pack/run 路径未改变。门禁应绑定保存的实际 producer、输入与制品身份。

## 当前集成检查

合入 `1129cd8` 后的 Ubuntu `make check OUT=out/ticket09/current-check-complete` 通过：107 tests、0 failures、15 个显式平台/重型入口 skip；两个 CI 静态合同和实际系统 C bootstrap 通过。原始输出为 `out/ticket09/current-check-complete.{stdout,stderr}.log`。实际 startup 测试消费本次 first producer，含七项 native startup 和 0644 传输后权限恢复；实际 I/O 合同消费 07 已构建的 `io-portable` 和 06 `combined-current/loaders`，没有再编译供应制品。四个 offline CLI 合同也全部通过。

保留了两份较早失败原件：强制 DrvFS TMPDIR 的 `out/ticket09/final/check.stderr.log`（94 tests，12 个已诊断候选目录 rename EACCES），以及开启实际 consumer 却未给 loader bundle 的 `out/ticket09/current-check.stderr.log`（102 tests，8 个明确缺 loader 的 blocked 结果）。后者已补显式 loader 输入再检查，不修改 probe 或跳过断言。临时 POSIX 权限 fixture 使用 `/tmp`，没有可复用缓存移出主 `.cache/`。根级 VS Code 文件占用诊断与 watcher 修正见[独立记录](drvfs-publication-validation.md)。
