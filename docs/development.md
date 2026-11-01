# 本地开发与 BOOT-01 报告

共享执行脚本位于 `scripts/`，系统 C 冒烟位于 `tests/native/`，命令合同测试位于 `tests/contracts/`；没有创建未实现的 Runtime、SDK 或应用目录占位。

## 当前构建与缓存约定

2026-11-01 用户确认：构建统一在 Ubuntu 完成，其他平台验证已生成的同一摘要制品与 SDK。包含编译的 `make check`、合同回归及 bootstrap CI 因而在 Ubuntu 执行；Windows/macOS 使用对应验证入口消费制品。

可复用源码、工具链原归档/解包树、guest 输入和构建中间文件统一保存在仓库根目录 `.cache/`，报告与最终制品使用 `out/`。当前主工作区对应 `D:\Workspace\dairn\qaplet\.cache` / `/mnt/d/Workspace/dairn/qaplet/.cache`；多个实施 worktree 的实际运行应显式使用这一个主工作区缓存。CI 使用其检出目录下的 `.cache/`。缓存目录已被 Git 忽略；默认构建离线校验已锁输入，显式获取操作负责填充缓存。

本机该目录位于没有 `metadata` 选项的 DrvFS。实际执行、读写和内部符号链接可用；文件可设为无写权限，但非执行位和目录只读位不能按 POSIX 原样表示。工具链验证须在报告中记录实测权限限制，并持续核对原归档 SHA-256、所有文件内容/类型和链接目标；在支持 POSIX 权限的 Ubuntu 文件系统保留完整权限校验。没有更改全局 WSL/mount 设置。下文与历史验收记录中的 ext4/home 缓存、Windows 编译和旧 CI 矩阵是此前观测，当前默认与调度以本节及配置为准。

## 命令与 scope

| 入口 | 报告 | 成功含义 |
| --- | --- | --- |
| `make check` | 复用下列检查及其报告 | 提交前串联 CI 静态检查、合同回归、真实 C bootstrap |
| `make doctor` | `out/doctor.json` | 环境盘点；当前因 M0 前置未齐整体 blocked |
| `make test-environment` | `out/environment.json` | ENV-01 Linux x64 工具、磁盘、目录、HTTPS/解包/摘要、系统 C 验收；须显式指定下载 URL |
| `make test-bootstrap` | `out/bootstrap.json` | 系统 C 编译/执行通过，不是 APE 或 QuickJS 成绩 |
| `make test-foundation` | `out/m0.json` | 当前始终非通过，列出输入、制品、六类宿主与必需探针缺口 |
| `make test` | unittest 输出 | 命令合同回归通过，不是产品矩阵 |
| `make check-ci` | `out/check-ci.json` | JSON/YAML 语法及项目编排合同通过，远程未运行 |

这些 make recipe 与 CI 复用同一 Python CLI。也可直接执行：

```sh
python scripts/foundation.py test-foundation --profile bootstrap --cc cc --timeout 30 --report out/bootstrap.json
python scripts/foundation.py validate-report --input-report out/bootstrap.json
```

`validate-report` 消费 `compat/foundation-report.schema.json`，验证结构、版本、非空/唯一用例和基础状态不变量；它不验证外来报告是否真的执行，也不能代替任务 10 的完整证据门禁。`scripts/evidence.py` 只实现本 schema 使用的有限 JSON Schema 词汇，遇到不支持的词汇拒绝验证。扩展报告合同应同步版本和验证器。

`tests/contracts/bootstrap-cases.json` 是当前唯一必需 bootstrap 用例清单。清单缺失、空、重复或含未知用例都失败；缺 C 源码或编译器为 blocked，编译失败、输出不符、非零退出或超时为 fail。冒烟编译/执行每条命令默认限时 30 秒，可设为 1–300 秒；doctor 版本查询限时 10 秒，WSL 查询限时 15 秒。Windows 超时尝试终止进程树并兜底终止直接子进程；Unix 使用独立进程组清理。BOOT-01 不运行安装服务、SCM 或 Job 的测试。

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

## WSL 入口与权限上下文

Windows 默认优先使用已安装的 `%ProgramFiles%\WSL\wsl.exe`，再回退 PATH；`--wsl` 或 `make doctor WSL=...` 可显式指定入口。只读取快捷方式目标不足以判断发行版是否可用：MSI 广告快捷方式可能先指向 Installer 图标资源。

诊断时先记录所用 exe、执行上下文、命令退出码和原始日志。查询失败的结论限定于当前进程可见范围。用户提供另一入口或说明已安装时，用该入口在可访问发行版的用户上下文只读复核，并保留两次观测。环境就绪的证据是实际 Linux kernel、执行架构和页大小；系统 C bootstrap 另行验证编译/执行。

2026-10-31 的实际核验发现，受限 agent 进程与普通用户进程对 WSL 的可见结果不同：前者可能返回无发行版/拒绝访问，后者可进入 Ubuntu-22.04。doctor 因而只报告当前执行上下文的观测，保留每个入口的日志；不因 `wsl.exe` 存在就判定环境就绪，也不将受限查询失败外推为系统未安装。具体复核见[环境记录](../.scratch/qaplet-m0/environment-check.md)。

Linux 下可直接在仓库根目录运行相同 make 入口。Windows 工作目录 `D:\Workspace\dairn\qaplet` 在此次 WSL 中映射为 `/mnt/d/Workspace/dairn/qaplet`，不假定其他机器盘符相同。ARM64 4K/64K 与此 WSL x64 4K 分开计数。

## ENV-01 环境验收与目录

复用现有 Ubuntu-22.04；不需要再次安装 WSL。以普通 Linux 用户在上述源码目录运行：

```sh
make test-environment OUT=out/env01/wsl-final \
  ENV_DOWNLOAD_URL=https://mirrors.tuna.tsinghua.edu.cn/ubuntu/dists/jammy/InRelease
python3 scripts/foundation.py validate-report \
  --input-report out/env01/wsl-final/environment.json \
  --report out/env01/wsl-final/validated.json
```

此 URL 仅作 HTTPS 下载探针，保存实际长度/摘要，不作为产品依赖锁。省略 URL 会得到 blocked；不自动联网。GitHub TLS 失败和软件源超时等原始记录也保留，不能由另一端点通过推断所有下载站点可达。

默认构建与逐命令证据放在仓库 `.cache/environment/<uuid>/`，JSON 报告放在 `OUT` 下。`ENV_BUILD_DIR` 可用于 worktree 显式选择主工作区 `.cache/environment`；构建目录不可创建时在报告旁保留 blocked 证据。tar.gz 与 ZIP 对仓库 C 源码做真实创建、解包和 SHA-256 往返验证；HTTPS 下载另作独立用例。系统 C 输出的架构与 sysconf 页大小须与宿主测量一致。新 login shell 重新检查工具与两个目录；这不要求终止发行版或重启机器。

验收默认要求源码盘、构建文件系统以及显式提供的 backing 盘各至少 2 GiB 可用空间；CLI `--min-free-bytes` 可提高阈值。这是小型环境探针的下限，后续 Nightly/QuickJS/QEMU 任务应按实际归档、解包、缓存和构建峰值另算空间。当前源码与缓存都在 D 盘；历史 ext4 缓存的 VHD 在 C 盘，其 `/mnt/c` 测量不能当作当前缓存盘。仅当确有该存储依赖时显式提供 `ENV_BACKING_DIR`，不能从 ext4 显示的虚拟容量反推物理空间。

当前源码与缓存都在 DrvFS（`/mnt/d`）。历史 ENV-01 另测过 ext4 构建目录，其通过不表示 Windows ACL、chmod、符号链接或大小写语义完全一致；当前权限限制见本节开头。历史环境指纹见[ENV-01 验收记录](../.scratch/qaplet-m0/wsl2-environment-validation.md)及[本地覆盖记录](../.scratch/qaplet-m0/local-coverage.json)。环境验收与产品 APE/ARM64 成绩分别判定。

## 固定输入入口（BOOT-02 / BOOT-03）

QuickJS 的权威锁、原始许可和有序补丁清单位于 `third_party/quickjs/`。普通使用先显式填充固定提交缓存，再离线校验：

```sh
make quickjs-fetch OUT=out/inputs/quickjs-fetch
make quickjs-verify OUT=out/inputs/quickjs-verify
```

`quickjs-fetch` 使用已提交的固定 commit，不解析移动 master；`quickjs-bump` 才显式重新解析官方 master 并更新锁。默认源码缓存位于 `.cache/quickjs/`，worktree 可用 `QUICKJS_CACHE` 指定主工作区缓存。构建先校验所选缓存，并在 `.cache/build/` 的独立目录生成中间文件。源码锁验证不是编译或跨宿主验收。

Nightly 入口是 `make toolchain-fetch`、`make toolchain-bump`、`make toolchain-verify`。权威锁为 `toolchains/cosmocc.lock.json`，默认缓存为 `.cache/toolchains/cosmocc/`，可用 `TOOLCHAIN_CACHE` 指定主工作区缓存。`verify` 和 `path` 完全离线；缺失缓存或身份不符时失败，不自动下载。当前 DrvFS 缓存直接测其物理卷空间；仅在实际使用 VHD 存储时另提供对应 backing 卷，不假定为 C 盘。

需要分开获取与 Linux 测量时，显式运行：

```sh
python scripts/nightly_toolchain.py download \
  --download-to .cache/downloads/cosmocc.zip \
  --report out/nightly-download/acquisition.json
python3 scripts/nightly_toolchain.py fetch \
  --archive /path/to/cosmocc.zip \
  --acquisition-report /path/to/acquisition.json \
  --report out/inputs/nightly-fetch.json
```

接收端重新比对获取报告里的原始长度与 SHA-256，再安全解包、测量编译器和完整内容。没有成功官方获取记录的本地 ZIP 仅属于 `local-import`，默认不能作为产品工具链使用。工具链 libc 与源码 HEAD 的对应关系无法证明时保持 `unproven`，不能把下载成功当成 APE 成功。

本轮官方发布站点在本地及 GitHub runner 都超时，最终从[官方 Nightly run 41169443573](https://github.com/jart/cosmopolitan/actions/runs/41169443573) 取得原始 `cosmocc` artifact。权威锁如实记录 `github-actions` 来源及制品摘要，获取记录保存在 `toolchains/cosmocc.acquisition.json`。新缓存可在取得锁中对应的原始 ZIP 后运行：

```sh
make toolchain-fetch TOOLCHAIN_ARCHIVE=/path/to/original-artifact.zip \
  TOOLCHAIN_ACQUISITION_REPORT=toolchains/cosmocc.acquisition.json \
  OUT=out/inputs/nightly-fetch
```

此 GitHub 来源的锁不会因缓存缺失而切换到滚动发布站点。已有权威锁时，精确匹配的 ZIP 副本也可独立用于恢复；首次建立新的官方来源必须提供可核查获取记录。`acquire-nightly.yml` 的一次性 integration push 获取已结束，后续仅显式手动触发；它保留官方发布站点下载的成功/失败日志和归档 7 天，不更新任何锁。取回 artifact 后仍须本地校验和 Linux 工具测量。

两种输入报告各自使用独立的版本化合同；现有 foundation `validate-report` 不验证它们，也不汇总这些结果关闭 M0。BOOT-09 仍须接入全部 Core 实际证据。

## CI skeleton

`.github/workflows/foundation.yml` 用 JSON 表达 YAML，便于在无第三方 Python 包的本地环境解析。`make check-ci` 检查触发器、只读权限、共享命令、失败时上传证据及禁用的产品槽位，并验证静态检查先于编译和合同测试；没有执行 Actions 服务端校验或 actionlint。

提交前在 Ubuntu 运行 `make check`。Windows/WSL 差异由普通用户上下文的对应验证命令采集，并分别保存报告；其他平台消费 Ubuntu 已构建制品。受限上下文仅补充可见范围内的证据。WSL 不可用负向用例用显式不存在的 `--wsl` 隔离；其回归位于 `tests/contracts/test_foundation.py`。

bootstrap 编译矩阵仅含 Ubuntu runner，compiler 使用 PATH 中的 `cc`。静态检查拒绝非 Ubuntu 编译行及开发机编译器绝对路径；Ubuntu 本地 `CC` 仍可显式使用绝对路径。历史 Windows GCC 配置修复保留在验收记录中，当前不再调度 Windows/macOS bootstrap 编译。

启用的 `bootstrap` job 在 Ubuntu 上做环境盘点、真实 C 冒烟和合同回归；doctor 的 blocked 允许继续，测试失败则阻断。报告与原始日志即使失败也上传。runner 页大小来自实际 OS 查询，不能从镜像名推断。该 workflow 名称和 artifact 均标为 bootstrap；其他平台的产品/SDK 验证由对应 workflow 消费同摘要制品。

禁用的 `m0-build-distribute`、`m0-native-pages`、`m0-qemu-page-gaps` 分别为锁定构建/同摘要制品分发、原生 ARM64 页探测和缺失页配置完整系统 TCG guest 预留位置。SCM 和 Job 分属 `svc-scm`/`svc-job`，使用独立应用制品，不进入 Core 门禁。启用这些位置之前须由对应任务接入可运行的共享脚本、输入与制品；当前不能从 disabled/skip 推出通过。首次远程 bootstrap 的实际结果及 Windows 配置修复见[验证记录](../.scratch/qaplet-m0/foundation-validation.md#首次远程-ci-与-windows-配置修复)。

## CI 查询与故障定位

1. 先按待验证的提交 SHA 找到对应 run，读取 status/conclusion。run 已 completed/success 即完成成功核验；将 SHA、run 链接和结果写回任务证据。
2. run 失败时读取 job 摘要，定位失败步骤，再读该 job 的日志。日志只有 blocked/摘要时，从已上传的 artifact 读取具体 reason 和原始命令日志。
3. 插件传输失败先做一次有限重试，仍失败就报告查询暂不可用。GitHub 404 与账号登录状态分别核验；读取当前身份和仓库可见范围后再判断是否需要用户重新授权。

每次只返回判定所需字段和失败片段。保留已取得的 run/报告对象供后续解析；只有重新运行、状态尚未结束或用户要求刷新时才再次查询远程。
