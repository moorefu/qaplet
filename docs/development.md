# 开发与验证入口

当前规划和迁移范围见[文档入口](INDEX.md)。本页描述已实现的操作路径；v1.5 的统一 v3 锁、目标 QuickJS 和新 M0-A 由[对齐任务](../.scratch/qaplet-v15-alignment/README.md)执行后再更新这些入口，计划样例不作为当前构建输入。

构建制品和SDK发行物统一在Ubuntu执行；其他平台消费同一字节制品和SDK。可复用输入和构建缓存使用主工作区物理 `.cache/`，报告与最终制品使用 `out/`；`toolchains/evidence/` 保持Git忽略。

## 当前构建与缓存约定

`python3 scripts/dev.py context --report out/context.json` 记录实际checkout、Git主工作区、缓存和宿主身份。linked worktree 的 `check`、`build-ape`、`offline` 入口自动传入主工作区缓存参数；构建入口要求Ubuntu。`--plan` 只生成命令计划，不能作为执行成绩。一次性POSIX权限fixture使用适合该行为的临时文件系统，默认临时目录与可复用缓存分开。

## 选择入口

| 任务 | 入口与含义 |
| --- | --- |
| 日常检查 | `python3 scripts/dev.py check`，先预检，再复用现有 `make check` |
| 快速仓库预检 | `python3 scripts/dev.py preflight --report out/preflight.json` |
| 完整证据回归 | `python3 scripts/check_evidence.py --config FILE --report out/evidence.json`，所需输入和测试均必须执行 |
| 构建Hello/eval | `python3 scripts/dev.py build-ape`，复用已锁定构建脚本 |
| 断网重构 | `sudo python3 scripts/dev.py offline`，先实际权限/namespace预检再运行双构建 |
| 审查材料 | [按轴生成、验证和派发](agents/review.md) |
| CI证据 | `python3 scripts/ci_evidence.py --help`，显式fetch、离线verify、typed summary |

`make check` 仍为原入口：CI合同、普通回归和真实系统C bootstrap；完整Core判定由显式consumer证据入口签发。CLI `--help` 是参数的权威列表。

## 按任务查阅

| 当前工作 | 先读 |
| --- | --- |
| 环境诊断、compiler/cache选择、WSL/DrvFS权限 | [Ubuntu与缓存](development/environment.md) |
| CI调整、查询或制品收集 | [CI与证据工具](development/ci.md) |
| 报告路径、身份、严格回归或长验证 | [证据合同](development/evidence.md) |
| 输入锁、APE、启动/I/O、ARM64 guest | [输入与产品探针](development/probes.md) |
| 实施历史与已测限制 | [M0验收索引](../.scratch/qaplet-m0/README.md) |

## ENV-01 环境验收与目录

详细命令、执行上下文和历史测量见[环境验收](development/environment.md#env-01-环境验收与目录)。

## BOOT-09 · 完整 Core evidence consumer

完整manifest、路径映射、只读消费与scope边界见[Core evidence合同](development/evidence.md#boot-09--完整-core-evidence-consumer)。
