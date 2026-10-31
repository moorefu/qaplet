# Qaplet M0 执行索引

**Status:** implementation-in-progress / BOOT-01-resolved / ENV-01-resolved
**Confirmed:** 2026-10-31；MIT、WSL2 Linux x64、命令行/结构化证据验收、完整Core矩阵与12张任务均已确认；最新调整为本地探索优先、CI持续回归；Linux ARM64先测原生runner页配置，QEMU仅补缺失组合。  
**Baseline:** [总体计划 v1.4（App-first / 本地优先与原生页覆盖）](../../docs/plans/qaplet-v1-implementation-plan.md)，SHA-256 `C1A16D76C2453EF42BAFE9DCACE9673CCEA0FC1C95FBF6A9CFAA77C93A0E8237`。

## 入口

- [M0 spec](spec.md)：范围、验收、测试、资源与失败合同。
- [术语](../../GLOSSARY.md)
- [Core/App 边界 ADR](../../docs/adr/0001-core-app-boundary.md)
- [固定输入 ADR](../../docs/adr/0002-pinned-build-inputs.md)
- [本地优先 / 原生页覆盖 / QEMU补充 ADR](../../docs/adr/0003-m0-ci-qemu-validation.md)
- [环境核查与测量命令](environment-check.md)
- [本地 issue tracker 约定](../../docs/agents/issue-tracker.md)

## 任务图

| 序号 | 任务 | Blocked by | 归属 |
| --- | --- | --- | --- |
| 01 | [BOOT-01 · 工程骨架与环境诊断](issues/01-foundation.md) | 无 | M0 Core |
| 02 | [ENV-01 · WSL2 Linux x64 构建环境就绪](issues/02-wsl2-build-environment.md) | 无 | M0 Core |
| 03 | [BOOT-02 · QuickJS 源码锁与来源记录](issues/03-quickjs-source-lock.md) | 01 | M0 Core |
| 04 | [BOOT-03 · Nightly 工具链锁与离线缓存](issues/04-nightly-toolchain-lock.md) | 01, 02 | M0 Core |
| 05 | [BOOT-04A · 构建 fat APE 与最小 QuickJS 探针](issues/05-fat-ape-build.md) | 03, 04 | M0 Core |
| 06 | [BOOT-04B · 同一 APE 的跨宿主启动验证](issues/06-cross-host-startup.md) | 05 | M0 Core |
| 07 | [BOOT-06 · Linux ARM64 64K 页深度探针](issues/07-arm64-64k-probes.md) | 05 | M0 Core |
| 08 | [BOOT-07 · Windows/Unix 原生 I/O 行为基线](issues/08-native-io-baseline.md) | 05 | M0 Core |
| 09 | [BOOT-08 · 离线重构与输入篡改拒绝验证](issues/09-offline-integrity.md) | 03, 04, 05 | M0 Core |
| 10 | [BOOT-09 · M0 Core 证据汇总与门禁](issues/10-core-evidence-gate.md) | 06, 07, 08, 09 | M0 Core |
| 11 | [BOOT-05A · 应用私有 SCM ABI 探针](issues/11-svc-scm-abi-probe.md) | 04, 05 | M0-SVC |
| 12 | [BOOT-05B · 应用私有 Job Object 可行性探针](issues/12-svc-job-object-probe.md) | 04, 05 | M0-SVC |

任务状态ready-for-agent表示规格足以交给agent；真正可开工还要检查Blocked by与Execution prerequisites。有资源未就绪的任务不能宣称已执行。每张任务独立保存，完成时在该文件记录结果、证据和状态。

初始无任务前置的入口是01工程骨架和02环境准备。建议先从01开始实施，02可并行准备；本轮没有执行安装或系统变更。

输入锁03与04满足后构建05。05产物就绪后，06/07/08/09及11/12分别按各自资源展开；10只汇总Core结果。11与12互不依赖，也不阻断10。总体计划BOOT-04在这里拆为构建与跨宿主验收；BOOT-05拆为SCM与Job可行性。

## 资源前置与当前事实

| 资源 | 规划时状态 | 使用方与完成条件 |
| --- | --- | --- |
| 本机Windows x64 | 有C/构建工具，未跑产品探针 | 日常本地开发、普通原生探针 |
| WSL2 Linux x64 | 缺kernel和发行版，实际页大小unknown | 02就绪后读取架构/页大小，承担日常构建与探索 |
| 本地完整系统QEMU | 尚未准备镜像 | 本地快速profile通常只补ARM64 64K；guest需锁定并实测 |
| GitHub原生Linux ARM64 | 官方提供ubuntu-24.04-arm等；本次未实测页大小 | 06/08；若4K则只补64K guest，若64K则只补4K |
| CI Linux x64 / Windows / macOS | 拟用对应架构托管runner，未实跑 | 持续集成/全矩阵，复用本地脚本 |
| 本地可丢弃Windows VM | 未确认接入 | 11/12先在此做真实探索；CI托管Windows VM做基础持续回归 |

页大小和架构必须一起记录。WSL x64 4K不等于ARM64 4K；本地快速profile成功不是完整M0矩阵完成。详细只读核查与后续测量入口见[环境记录](environment-check.md)。

## GitHub Actions 实施安排

探索、调试和小范围回归先本地完成；CI用于提交后的持续集成和跨平台回归。本地与CI共用构建/探针/采集脚本，workflow只编排，不以反复提交CI作为日常调试循环。

Linux ARM64优先原生runner，job开始读取架构与页大小，QEMU只补缺失的ARM64 4K/64K组合。常见预期是原生4K + QEMU 64K，但4K未实测前不能写为事实。实际若是64K则补4K；如是其他页配置，明确缺口，不能冒充已覆盖。

本地WSL就绪后同样先测页大小。本机x64的快速profile可用WSL原生路径 + 一个ARM64 64K guest；需要本地完整ARM64矩阵时再显式增加4K guest。QEMU必须完整启动相应guest内核，qemu-user不能满足页大小验收。

01负责共享入口与CI骨架，02落实本地WSL/探测，05本地构建并支持CI重现，06接原生+补缺矩阵，07做64K深探针，08复用矩阵，11/12本地隔离VM探索并在CI回归。12张任务和Blocked by不变。

当前无GitHub remote/workflow/probe实现；本轮只核查并修订方案，未触发CI或安装WSL。CI构建后分发同一摘要制品，失败也保留日志/串口/报告；Core/SVC继续独立判定。

### BOOT-01 实施状态（2026-10-31）

已建立 MIT/第三方许可边界、共享 make/Python 入口、真实系统 C bootstrap、报告 schema 和可本地解析的 CI skeleton。原有规划表格和“当前事实”为规划时快照，最新结果以[环境复核](environment-check.md#boot-01-实施复核2026-10-31)及 [01 的实施记录](issues/01-foundation.md)为准。

01 已完成实现、Windows/WSL 本地验证和两路 code-review，详见[验证与审查记录](foundation-validation.md)。02 的现有 WSL2 完整环境验收已执行，见[ENV-01 记录](wsl2-environment-validation.md)和[本地覆盖](local-coverage.json)；其余 10 项任务仍待各自验收。

实际发现现有 WSL2 Ubuntu-22.04 可在普通用户上下文运行，Linux x64/4096 字节页已测量；受限进程看不到同样的发行版。origin 已配置为 `moorefu/qaplet`，但本任务没有 push 或执行远程 CI。产品输入锁、APE、ARM64/macOS 产品宿主、QEMU 和 SCM/Job 探针仍未执行，完整 M0 保持未完成。

后续首次远程 bootstrap 已执行：[run 41202382602](https://github.com/moorefu/qaplet/actions/runs/41202382602)。Linux x64、Linux ARM64、macOS bootstrap 成功；Windows 因 compiler 路径不存在失败，现已修正并通过本地回归。此结果仅更新 bootstrap 状态，未关闭完整 M0；详见[CI 结果与修复](foundation-validation.md#首次远程-ci-与-windows-配置修复)。

## 状态和证据

Core完成由10的完整门禁判定。SCM/Job有独立artifact和结果；失败只阻断相应App能力。原始日志不能只保留成功项。必需用例blocked/not_run/skip时完整Core门禁不得通过。

BOOT-01/ENV-01 命令、schema 和环境验收已实现；产品 probe 和完整 Core 门禁仍待对应任务。具体锁值、下载摘要、主机结果和产品能力必须来自实施测量，不在规划中填造。

## 后续里程碑（本轮不生成详细spec或任务）

| 阶段 | 目标与交接条件 |
| --- | --- |
| M1 | QuickJS Engine、基础CLI与通用Embed SDK；依据BOOT-04结果进入 |
| M2 | Loop/Reactor/WorkPool/Process及SDK事件桥；消费M0原生行为基线 |
| M3 | Node基础兼容与真实CLI；并行启动服务应用JS配置/systemd Direct原型 |
| M4 | 网络、TLS、HTTP/Fetch/SSE/WebSocket；独立安全验收 |
| M5 | QJB格式、严格解析与按需资源加载 |
| M6 | 普通App Compiler及通用自定义Native Host构建合同 |
| M7 | 大资源ZIP64/STORE/对齐映射 |
| M8 | qaplet-svc的JS业务、应用私有Native Host与系统适配完整交付 |
| M9 | Core/App各自的兼容、安全、性能、供应链和发布证据 |
| M10 | Runtime/SDK与Service App分别按已声明范围判定发布 |

详细内容继续引用总体计划；只有依赖事实就绪、即将实施的阶段再细化。
