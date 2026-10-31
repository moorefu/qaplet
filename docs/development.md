# 本地开发与 BOOT-01 报告

共享执行脚本位于 `scripts/`，系统 C 冒烟位于 `tests/native/`，命令合同测试位于 `tests/contracts/`；没有创建未实现的 Runtime、SDK 或应用目录占位。

## 命令与 scope

| 入口 | 报告 | 成功含义 |
| --- | --- | --- |
| `make check` | 复用下列检查及其报告 | 提交前串联 CI 静态检查、合同回归、真实 C bootstrap |
| `make doctor` | `out/doctor.json` | 环境盘点；当前因 M0 前置未齐整体 blocked |
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

报告 v1 的公共上下文由根级 `source`、`host`、`ci`、`qemu` 供每个 case 继承；不同 case 的编译器、构建与制品身份在各自记录中保存。

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

## CI skeleton

`.github/workflows/foundation.yml` 用 JSON 表达 YAML，便于在无第三方 Python 包的本地环境解析。`make check-ci` 检查触发器、只读权限、共享命令、失败时上传证据及禁用的产品槽位，并验证静态检查先于编译和合同测试；没有执行 Actions 服务端校验或 actionlint。

提交前运行 `make check`。涉及 Windows/WSL 执行差异时，分别在普通 Windows 用户上下文和现有 WSL 运行同一入口，分别保存 `OUT`。受限上下文测试可补充失败证据，真实用户上下文负责核验可用环境。WSL 不可用负向用例用显式不存在的 `--wsl` 隔离；其回归已经位于 `tests/contracts/test_foundation.py`。

托管 runner 的 compiler 配置使用 PATH 中的可执行文件名：Windows 为 `gcc`，其他 bootstrap 行为 `cc`。静态检查拒绝把开发机的编译器绝对路径放入该矩阵；本地 `CC` 仍可显式使用绝对路径。Windows 镜像预装 MSYS2 不代表已安装其 MinGW GCC；本任务使用 runner 提供的系统 GCC，不依赖 MSYS2 目录或安装步骤。

启用的 `bootstrap` job 在已配置 runner 上做环境盘点、真实 C 冒烟和合同回归；doctor 的 blocked 允许继续，但测试失败不允许继续。报告与原始日志即使失败也上传。runner 页大小来自实际 OS 查询，不能从镜像名推断。该 workflow 名称和 artifact 均标为 bootstrap。

禁用的 `m0-build-distribute`、`m0-native-pages`、`m0-qemu-page-gaps` 分别为锁定构建/同摘要制品分发、原生 ARM64 页探测和缺失页配置完整系统 TCG guest 预留位置。SCM 和 Job 分属 `svc-scm`/`svc-job`，使用独立应用制品，不进入 Core 门禁。启用这些位置之前须由对应任务接入可运行的共享脚本、输入与制品；当前不能从 disabled/skip 推出通过。首次远程 bootstrap 的实际结果及 Windows 配置修复见[验证记录](../.scratch/qaplet-m0/foundation-validation.md#首次远程-ci-与-windows-配置修复)。

## CI 查询与故障定位

1. 先按待验证的提交 SHA 找到对应 run，读取 status/conclusion。run 已 completed/success 即完成成功核验；将 SHA、run 链接和结果写回任务证据。
2. run 失败时读取 job 摘要，定位失败步骤，再读该 job 的日志。日志只有 blocked/摘要时，从已上传的 artifact 读取具体 reason 和原始命令日志。
3. 插件传输失败先做一次有限重试，仍失败就报告查询暂不可用。GitHub 404 与账号登录状态分别核验；读取当前身份和仓库可见范围后再判断是否需要用户重新授权。

每次只返回判定所需字段和失败片段。保留已取得的 run/报告对象供后续解析；只有重新运行、状态尚未结束或用户要求刷新时才再次查询远程。
