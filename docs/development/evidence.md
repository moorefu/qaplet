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
  "guest_manifest": "local-4k-inputs.json",
  "guest_report": "guest4096/guest.json",
  "publication_inputs": "publications.json"
}
```

可选 `guest_manifest` 为实际 4K guest 选择其原 producer 与传输证据，并通过 `QAPLET_GATE_4K_MANIFEST` 传给既有 4K 回归；省略时沿用 `input_manifest`，兼容同一 bundle。它不替代必需的完整 Core manifest、4K guest 或五 producer publication selection。Full CI 与本地 4K 可分别使用真实的 CI/本地身份，不能改写或模拟 CI 字段让不同 producer 混用。

入口先检查三个必需文件、五种 producer 分发声明、实际完整 Core 和4K guest，再执行已有 Core/4K/publication/recipe consumer regressions。本 profile 每项测试都必需：缺数据、失败、error、skip或未完成均非通过。普通 `make check` 的平台/显式数据 skips 保留原含义。`--preflight-only` 只签发 evidence-preflight，明确没有执行 mutation regressions。

报告旁 `evidence/<uuid>/` 保存 preflight 报告、完整 regression log 与逐测试更新的 progress.json。该入口不下载、编译、启动新guest或安装服务；Windows系统操作继续由独立受控入口验证。当前已验收数据的位置见[实施记录](../../.scratch/qaplet-m0/final-review-validation.md)。

## 长验证之前

仓库预检同时检查证据工具和 `tests/contracts/*.py` 的显式文本编码。`scripts/text_encoding.py` 给出首批工具范围，所有 `evidence_*.py` 自动纳入。`read_text`、`write_text`、常规文本 `open` 使用显式 `encoding`；原始进程输出使用二进制 I/O。生产者配方中的其他脚本留待单独审计，变更其字节仍需匹配产品验收。小型 Unicode fixture 回归在关闭 Python UTF-8/locale coercion 的进程执行，不依赖完整 CI 制品。

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

## 证据保存与路径派生

`scripts/evidence_store.py` 管理显式关闭的输出树；不提供删除 source/worktree 的操作。先 `plan --source-root CLOSED --backup NEW --report NEW_REPORT` 查看去重内容、保守归档上界、可用字节及 reserve。`--expansion-budget` 为下一次回归/恢复另留峰值空间，`--reserve-bytes` 默认 512 MiB；计划不等于保存完成。

```sh
python3 scripts/evidence_store.py create --closed-source --source-root CLOSED --backup NEW --report out/store-created.json
python3 scripts/evidence_store.py verify --backup NEW --source-root CLOSED --report out/store-verified.json
python3 scripts/evidence_store.py restore --backup NEW --report out/store-restored.json
python3 scripts/evidence_store.py materialize --backup NEW --member relative/original.log --report out/store-expanded.json
```

每次报告使用新路径，放在 source/backup 之外。新归档使用 Python 3.13 标准库可读的去重 tar.gz；历史 M1 tar.zst 通过 Python 3.14+ 标准库读取。inventory 保留每条路径的 SHA、长度、模式、mtime 和源物理身份；完整流读取核验压缩 footer。恢复在 `backup/out` 创建独立文件，按 inventory 恢复每条路径元数据；已有不同文件拒绝覆盖。source/backup 重叠、reparse/目录别名、越界成员、坏归档及空间不足均失败，源保留。归档、恢复与校验收据始终 `cleanup_authorized=false`；worktree 清理仍需独立的 Git/路径边界/遗漏输出审计。新工具只操作本次显式备份，不修改 M1 既有保存收据或快照计数。

`scripts/evidence_selection.py --config ACCEPTED_CONFIG --provider-catalog ORIGINAL_CATALOG --backup BACKUP --output-dir NEW_SELECTION` 从原选择和保存收据派生新的 M1/M0 配置。`--backup` 可重复；收据及 inventory/归档摘要先核对，再验证 provider ID/name/commit/digest 与解包字节。`--mount D:=/mnt/d` 显式增加 WSL 变体，不假定本机盘符适用于其他机器。

工具只变更位置字段和显式保存映射，原报告与原 provider catalog 不改写；`derivation.json` 记录源摘要和输出摘要，状态为 `prepared`、`evidence_verified=false`。先对新 `config-native.json` / `config-wsl.json` 执行原 `check_evidence.py --preflight-only`，再执行完整严格回归；派生成功不代表新的 OS 执行或严格通过。

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

`core_gate.py`、`sdk_guest.py`、`sdk.py` 消费操作与严格入口在创建输出前，按各选择文件自己的基目录和最长前缀映射保护显式选择的文件、原 ZIP 和供应目录；路径的大小写别名、hardlink 和目录别名也属于同一输入。SDK matrix 的显式 native input-report 还保护该报告目录下的 `evidence/<report_id>/`，覆盖尚未读取的原始日志。映射根本身只是解析规则，不禁止在无关兄弟路径写报告。写最终报告前再核对已实际读取的 evidence_index，包括原始 serial/stdout/stderr 和嵌套 M0 索引。SDK build 保持独立的生产者输出行为。

验证失败或未完成时，尚未打开的后续原件也必须保留。因此上述入口不会替换运行前已经存在的报告目标；终端返回非零、`status: fail`、`report_written: false` 和保留原因，既有文件不代表本次结果。使用新的报告路径可保存本次失败及已解析索引。新路径仍在读取配置前提供 running/progress，兼容阻塞输入；已有无关报告只有在完整所选验证成功且与实际输入无冲突时才可替换。

新 CI 选择还将三个 producer 及其 native/完整 guest consumer 的 commit 逐边比较：任一方有 CI 身份时，另一方也必须有完整身份；不同 run 可消费同 commit 的已验原归档，不能用缺字段或另一 commit 绕过。64K producer 直接记录 CI，guest 导入保留实际 guest CI 与独立 validation_ci；历史缺 CI 的报告不能补写成为当前 CI 成绩。各报告仍须匹配原归档 provider ID、commit 后缀和实际字节。

完整实际 manifest、通过报告、红绿及负向回归见 [10 验收](../../.scratch/qaplet-m0/core-evidence-gate-validation.md)。标准 `make check` 不自动寻找历史 `out/`；真实 consumer 回归须显式设置 `QAPLET_GATE_MANIFEST`。供应/build 配方不因新增 consumer 而改变。

guest catalog 按实际目标页大小选择：4096 仅需要 Hello/eval + I/O、17 项 startup 和一个 I/O suite；65536 另需 deep payload、facts/suite/shell-header 三条入口。`arm64_page_gaps` 只对已测页集合实施补缺规则：native 65536 可由4096 guest补缺，native 16384 需4096和65536两个guest；该纯调度模型不声明这些硬件已实际执行。

`python scripts/core_gate.py validate-guest --input-manifest FILE --guest-report GUEST --report OUTPUT` 是独立 guest transport/原始语义检查入口。它只消费该页配置所需的 producer，成功时 `status: pass`、`scope: guest-transport`，但 `core.status` 和 `m0_core_status` 都为 `not_run`，不能代替完整 `evaluate`。4K profile 原 lock 只固定 Debian packages；补充 `guests/arm64-derived.lock.json` 将独立从这些 SHA-verified package tar members 解出的 Image/config/compressed kernel 身份固定，并绑定原 lock 摘要/精确 package IDs；错误派生摘要仍拒绝，不从报告任意非空字段恢复通过。原 canonical lock 和已有65536证据不改写。

## 正式 M1 严格入口

仍使用 `scripts/check_evidence.py`，显式选择 `m1-evidence`；原 `m0-evidence` 合同不变：

```json
{"schema_version": 1, "profile": "m1-evidence", "input_manifest": "m1-inputs.json"}
```

```sh
python3 scripts/check_evidence.py --config out/m1-selection.json --report out/m1-evidence.json --preflight-only
python3 scripts/check_evidence.py --config out/m1-selection.json --report out/m1-evidence.json
```

`m1-inputs.json` 的完整字段如下；引用和迁移规则沿用上文 `Evidence` 合同。相对选择路径以该 manifest 的父目录解释，原报告路径和日志均不改写。

| 字段 | 必需的真实选择 |
| --- | --- |
| `schema_version` / `profile` | `1` / `m1-full` |
| `product_commit` | 完整 Qaplet 源码 SHA；与 QuickJS `source.commit` 分开 |
| `provider_catalog` | 原 provider artifacts API JSON，含 ID、真实 name、run/head SHA、原 ZIP digest/size |
| `transports` / `path_mappings` | 显式原 ZIP 与当前已解包根目录；不能把短本地缓存别名当 provider name |
| `producer_report` / `distribution_root` | 原 Ubuntu SDK build 报告及其完整供应目录 |
| `loader_manifest` | 同闭包的 `ape-loaders` helpers.json |
| `package_consumer` | 实际包外公开 C 编译、链接、审计和八项应用运行报告 |
| `native_reports` | 恰好五项：Linux x64/ARM64、Windows x64、macOS x64/ARM64 |
| `sdk_guest` | schema 1 / profile `sdk-guest`，含 `selection` 与 `guest_report`；原生 ARM64 已测 64K 时为 null |
| `m0_manifest` / `m0_original_manifest` | 只改当前路径的 M0 Full 选择和原 Full provider 内不可改写的 manifest |

入口复用 `ci_evidence` 完整 ZIP/CRC/解包 inventory、`core_gate.Evidence`、SDK producer/package、native、锁定 guest 和原 `core_gate.evaluate`。每条 CI 边必须匹配选定产品提交和 provider ID/name/run；所有执行报告、原始日志、SDK/APE/loader 字节都必须在选定 transport 内。它不下载、构建或运行产品。SDK 的完整公开文件集、隐藏 ARM64 archive、内外两层 ZIP、配方及逐 case SDK/APE、实际命令与 raw output 均重新核对；包外临时源码目录可以已消失，其公开输入仍由封装 ZIP 和实际编译命令审计。

每个 native 重放 98 项 Host/CLI/应用合同和实际 1000 次生命周期；Linux x64 门槛为 1000，其余为 100。Windows 仅 file-symlink fixture 可以按原协议成为可选 blocked，当前成功重试中该可选项也实际通过。原生 ARM64 优先，缺失的 64K 由固定完整系统 TCG 的 98 项补齐。独立当前 M0 Full 保留原六目标、235 项、离线二构建/六二进制/两 helper/14 refusals 语义；旧 M0 或首次失败 Full 不替代本次产品。

`--preflight-only` 明确签发 `evidence-preflight`，不声称运行变异。完整入口必须运行其选定的真实闭包回归；required skip/error/failure、空 catalog、未完成和中断均失败，原始 log、逐项 progress 和实际计数保留。失败时保留已解析 evidence_index；输出路径不得覆盖选择或 provider 原件。评估器与测试源码身份独立于冻结产品的 57 个 recipe 文件，并在严格运行结束重核。

初次严格入口的红绿见 [16 验收](../../.scratch/qaplet-m1/16-evidence-validation.md)；最终选择、独立审查和清理后 98 项实际重放见[最终集成记录](../../.scratch/qaplet-m1/final-review-validation.md)。当前产品源码固定 `43193d6e9163aaccece5e3c30d96bcbabd3cd260`，成功 Full 为 `41542426189`；报告分开给出 `m1`、`m0`/`m0_core_status`、required/optional gaps。M2 OS waker、I/O 取消、迟到 completion、完整调度以及物理 64K/性能均不外推；长期原 ZIP 镜像仍未验收，`release_ready=false`。
