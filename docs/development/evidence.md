# 证据身份与严格回归

当前消费者要求[统一 v3 身份](input-lock.md)。历史报告保留原成绩；缺少当前身份的报告不能在
新入口重标为当前组合通过。完整新矩阵由相应对齐任务取得，下面的严格回归仍需显式选择匹配的
实际证据，普通 `make check` 与本地单宿主/断网成绩不替代它。

`python3 scripts/check_evidence.py --config <selection.json> --report out/evidence-check.json` 是只读的严格回归入口。配置 schema_version 为 1、profile 为 `m0-evidence`，并显式选择 `input_manifest`、`guest_report`（实际4096 guest）和 `publication_inputs`（实际五 producer）三个文件；相对路径以配置文件所在目录解释。publication selection 内保留原 producer 路径，按该既有合同提供当前平台可访问的路径。

```json
{
  "schema_version": 1,
  "profile": "m0-evidence",
  "input_manifest": "inputs.json",
  "guest_report": "guest4096/guest.json",
  "publication_inputs": "publications.json"
}
```

入口先检查三个必需文件、五种 producer 分发声明、实际完整 Core 和4K guest，再执行已有 Core/4K/publication/recipe consumer regressions。本 profile 每项测试都必需：缺数据、失败、error、skip或未完成均非通过。普通 `make check` 的平台/显式数据 skips 保留原含义。`--preflight-only` 只签发 evidence-preflight，明确没有执行 mutation regressions。

报告旁 `evidence/<uuid>/` 保存 preflight 报告、完整 regression log 与逐测试更新的 progress.json。该入口不下载、编译、启动新guest或安装服务；Windows系统操作继续由独立受控入口验证。当前已验收数据的位置见[实施记录](../../.scratch/qaplet-m0/final-review-validation.md)。

## 长验证之前

先运行 `scripts/dev.py preflight`。有已锁定 recipe receipt 时增加 `--recipe-receipt <file>`，同时核对当前工作字节和暂存 Git blobs；结果保留原输入闭包，不宣称不同配方等价。再运行严格证据的 `--preflight-only`，最后执行完整回归。`scripts/dev.py offline` 先执行便宜的仓库预检及实际 nonroot namespace preflight，成功后才运行原断网 pair；`--preflight-only` 到权限/namespace检查即结束。配方变化后的完整证明仍由原 producer/consumer 合同判定。

## 身份与原始证据

报告 v2 的公共上下文由根级 `source`、`host`、`ci`、`qemu` 供每个 case 继承；不同 case 的编译器、构建与制品身份在各自记录中保存。v2 新增 `environment` scope 及其通过时的必需验收清单；历史 v1 报告须用对应旧版本验证器验证，当前验证器拒绝混用版本。

| 字段 | 内容 |
| --- | --- |
| `schema_version` / `report_id` / `scope` | 合同版本、唯一运行身份、验收范围 |
| `started_at` / `finished_at` | UTC 时间，含时区；外部命令及用例也记录实际时间 |
| `source` | 可查询时的 Git HEAD，以及当前构建/验证输入逐文件 SHA-256 和集合摘要；HEAD 不保证工作树干净 |
| `host` | OS、CPU、Python 实际执行架构、kernel、OS API 实测页大小与执行方式；C 输出另行校验架构/页大小 |
| `ci` | 本地为 null；CI 仅记录 run/attempt/workflow/commit、runner OS/架构/名称与 image 版本的允许列表 |
| `qemu` | 本任务未运行 QEMU，为 null；后续真实 guest 需记录 host/guest 架构、QEMU/TCG/machine/CPU、kernel/config/rootfs 摘要 |
| `cases[]` | case 身份、期望/实际值、退出码、时限、状态、原因与开始/结束时间 |
| `commands[]` | argv 数组、cwd、时间、退出码、超时标记及 stdout/stderr 原始日志绝对路径 |
| `toolchain` / `build` / `artifact` | 实测系统编译器路径/版本/摘要、构建命令/source 摘要、制品路径/大小/摘要；非构建用例为 null |

缺失的产品身份保持 null，并通过 case 的原因说明缺口，不填假 SHA 或成功结果。每次运行使用独立 `evidence/<uuid>/`，同名报告原子替换时保留旧日志。原始日志保持进程输出的字节编码；Windows WSL 可能使用 UTF-16LE。报告本身是 UTF-8 JSON，不序列化环境变量全集、凭据或 Git remote。

## BOOT-08 断网重构

先在联网环境显式填齐锁定 QuickJS、原 Nightly/派生 Linux 工具视图、APE helper 源码及单独启动 helper 所用 Zig 缓存，再在 Ubuntu 执行：

```sh
sudo python3 scripts/offline_integrity.py run \
  --cache-root /mnt/d/Workspace/dairn/qaplet/.cache \
  --uid 1000 --gid 1000 --report out/offline/offline-integrity.json
# 等价入口：sudo make test-offline OFFLINE_CACHE_ROOT=/repo/.cache OUT=out/offline
```

root 仅创建独立 `unshare --net` namespace，`setpriv` 将实际探针和构建降到给定非零 UID/GID。报告记录父/子 namespace、实际 UID、`socket.if_nameindex()`、路由和 literal `203.0.113.1:443` 的 `ENETUNREACH`；共享 sysfs 的网卡目录不作为隔离证明。不更改宿主网络、WSL/binfmt 或 mount。`preflight` 可单独测此环境，成功不代表构建通过。

setup 只将本次新建的 evidence UUID 目录交给目标 UID/GID（0750），供降权 worker 写入子报告；不递归修改仓库、缓存或已有报告目录。调用者提供的报告祖先目录须允许目标身份遍历。真实 POSIX `/tmp` 权限 fixture 与 DrvFS 构建缓存分开验证。

`run` 在两个全新 `.cache/build/offline/<id>/{first,second}/` 根目录运行实际统一 producer；每次命令有 1200 秒上限，内部 compile/link 另有已有时限。`strace -f` 单独记录 build 的网络/exec 系统调用；setup 的预期被拒连接位于 trace 之外，build 出现任何 IPv4/IPv6 connect/send 尝试都会失败，即使内核已拒绝。没有安装工具、下载、更新或修复 fallback。

精确 before/after inventory 逐文件保存类型、大小、SHA-256、权限和 link target，并拒绝缓存外重定向。它覆盖所选 QuickJS、原 Nightly archive/tree、完整 native 派生视图、锁定 loader/packer 源码和独立启动 helper 的 Zig archive/tree；build 中间件、复制负向 fixture、HOME/XDG/TMP 全在同一物理 `.cache/`。Zig 的可变 global cache 不作为锁定输入。两次 `.com`、`.com.dbg`、`.aarch64.elf` 共六个交付二进制逐字节比较；差异保存 offset/长度/SHA 并失败，不能用解释文字恢复通过。map/命令日志保留实际不同路径。此票不重构或声明单独 macOS ARM64 helper 可复现。

`refusals` 单独在相同隔离条件运行复制用例；缺源/归档、改源码/补丁/HEAD、坏归档/工具内容、占位摘要、冲突 provenance 和缺启动 helper 工具须在 executable 使用前失败，`strace` 和供应报告共同证明没有启动供应 executable。实际输入不被修改。`compare`、`inventory`、`audit-network` 只消费相应本地证据，不独立声称执行过断网构建。完整报告和原始日志保留在 `out/`，历史报告同名替换时原 evidence 目录仍保留；这些原 scope 的 `m0_core_status` 继续 `not_run`，独立 `core_gate.py` 汇总完整 Core，不改写原报告。

## BOOT-09 · 完整 Core evidence consumer

`python scripts/core_gate.py evaluate --input-manifest FILE --report FILE` 不下载、不编译、不重跑探针、不安装服务。manifest v1 必须使用 `profile: "full"`，显式给出三类 `producers`（startup/io/pages64k）、`startup_reports` / `io_reports` / `pages64k_reports` 数组、`offline_report` 和 `offline_recipe_comparison`。manifest 相对路径以其父目录解释；报告原路径不重写。

直接 CLI 参数 `--input-manifest`、`--report` 和 `--guest-report` 的相对路径均以调用者当前工作目录解释；manifest 内字段仍以上述 manifest 父目录解释。

`path_mappings` 为 `{source, root}` 数组，表示原 POSIX/Windows 前缀和当前物理目录；使用最长前缀，拒绝重复前缀、`..`、ZIP 绝对路径/symlink、重复 member 和越界。`transports` 为 `{id, name, root, archive: {path, sha256, size_bytes}}` 数组；重新比对整个 ZIP 摘要/长度、每项 CRC 及其解包字节。索引记录 original/resolved path 和新测 SHA，没有 basename 搜索或缺日志 fallback。`svc_reports` 是可选独立状态指针；SCM/Job 缺项或失败只改变 `svc`。

gate 从既有权威 case catalog 检查 17 项 startup（含 ESM/jobs）、20/21 项 I/O、12 项 deep，按实际 OS/CPU/执行架构/页大小/mode 重算六目标。native Linux ARM64 必须先有实测，QEMU 仅补未覆盖的 4K/64K。完整 serial token、命令参数/时间/退出/deadline、原始字节、before/after payload、kernel/config/rootfs/QEMU 身份和 cleanup 均有对应检查；native Windows 控制台消费独立 console-results.ndjson。

同一 report ID 重复即拒绝，包括完全相同副本；选中失败、blocked、skip 或未知 scope 均不能被后续成功覆盖。报告保存 `selected_inputs`，失败时保留原证据及已解析索引。重新哈希 Core APE、两个 sidecar/map 和 nm/PE-import 原始日志后重跑禁止服务符号合同。离线项必须是实际隔离/nonroot 的两次独立 build、六个二进制与两 helper 比较、14 refusal、真实 strace 和精确 cache/recipe before/after。recipe 对比保留测试/当前两套原始 fingerprint：输入闭包和实际当前字节相同时接受 `changed_inputs: []`；非空差异仅接受原有 `build_ape.py` 的 `check_ci` AST 证明，不冒称该差异后的配方已额外断网执行。声明的差异必须与两份 inventory 精确一致。

完整实际 manifest、通过报告、红绿及负向回归见 [10 验收](../../.scratch/qaplet-m0/core-evidence-gate-validation.md)。标准 `make check` 不自动寻找历史 `out/`；真实 consumer 回归须显式设置 `QAPLET_GATE_MANIFEST`。供应/build 配方不因新增 consumer 而改变。

guest catalog 按实际目标页大小选择：4096 仅需要 Hello/eval + I/O、17 项 startup 和一个 I/O suite；65536 另需 deep payload、facts/suite/shell-header 三条入口。`arm64_page_gaps` 只对已测页集合实施补缺规则：native 65536 可由4096 guest补缺，native 16384 需4096和65536两个guest；该纯调度模型不声明这些硬件已实际执行。

`python scripts/core_gate.py validate-guest --input-manifest FILE --guest-report GUEST --report OUTPUT` 是独立 guest transport/原始语义检查入口。它只消费该页配置所需的 producer，成功时 `status: pass`、`scope: guest-transport`，但 `core.status` 和 `m0_core_status` 都为 `not_run`，不能代替完整 `evaluate`。4K profile 原 lock 只固定 Debian packages；补充 `guests/arm64-derived.lock.json` 将独立从这些 SHA-verified package tar members 解出的 Image/config/compressed kernel 身份固定，并绑定原 lock 摘要/精确 package IDs；错误派生摘要仍拒绝，不从报告任意非空字段恢复通过。原 canonical lock 和已有65536证据不改写。
