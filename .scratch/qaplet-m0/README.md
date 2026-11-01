# Qaplet M0 执行索引

**Status:** implementation-in-progress / tickets-01-through-12-resolved / Core-gate-passed / final-review-pending
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

2026-11-01 实施更新：03 已完成实际 QuickJS 提交锁定、重新获取及 Windows / WSL 离线校验，见[源码验收](quickjs-source-validation.md)。04 已锁定官方 Nightly run `41169443573` 的原始 artifact（419652152 字节，SHA-256 `f2f984d3a9790674fc0da11c1c00a32d4fe3f8dafde99af74b0c6dc24a2a16cf`），完成编译器测量、断网校验、缓存恢复和损坏拒绝，见[工具链验收](nightly-toolchain-validation.md)。WSL 已安装 QEMU 6.2.0，并探索启动实际 64K ARM64 guest；持久 guest 锁及产品探针仍由 06/07 实施。服务探针按用户确认改用当前 Windows 宿主，实际 SCM 创建权限不足，既有 Job 归属已测；详见父规格执行更新。下表保留规划时历史状态，不替代新观测。

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

本段最初记录“无 GitHub remote/workflow/probe”的规划快照已被实际实现取代：完整 M0 CI `41271366261` 与独立 SCM CI `41271366311` 成功；Job 完成本地测量，远程仍未执行。CI 构建后分发同一摘要制品，失败也保留日志/串口/报告，Core/SVC 独立判定。

### BOOT-01 实施状态（2026-10-31）

已建立 MIT/第三方许可边界、共享 make/Python 入口、真实系统 C bootstrap、报告 schema 和可本地解析的 CI skeleton。原有规划表格和“当前事实”为规划时快照，最新结果以[环境复核](environment-check.md#boot-01-实施复核2026-10-31)及 [01 的实施记录](issues/01-foundation.md)为准。

01 已完成实现、Windows/WSL 本地验证和两路 code-review，详见[验证与审查记录](foundation-validation.md)。02 的现有 WSL2 完整环境验收已执行，见[ENV-01 记录](wsl2-environment-validation.md)和[本地覆盖](local-coverage.json)；其余 10 项任务仍待各自验收。

实际发现现有 WSL2 Ubuntu-22.04 可在普通用户上下文运行，Linux x64/4096 字节页已测量；受限进程看不到同样的发行版。origin 已配置为 `moorefu/qaplet`，但本任务没有 push 或执行远程 CI。产品输入锁、APE、ARM64/macOS 产品宿主、QEMU 和 SCM/Job 探针仍未执行，完整 M0 保持未完成。

后续首次远程 bootstrap 已执行：[run 41202382602](https://github.com/moorefu/qaplet/actions/runs/41202382602)。Linux x64、Linux ARM64、macOS bootstrap 成功；Windows 因 compiler 路径不存在失败，现已修正并通过本地回归。此结果仅更新 bootstrap 状态，未关闭完整 M0；详见[CI 结果与修复](foundation-validation.md#首次远程-ci-与-windows-配置修复)。

## 状态和证据

2026-11-01：12 的应用私有 Job 探针已在用户指定的当前 Windows 宿主完成受控父子/嵌套/拒绝/超时/清理验收，Ubuntu 构建并按同摘要消费；见[验收](job-object-validation.md)。首用 CreateProcessW +2 handle 增长未归因，owned handles 均关闭且重复基线稳定；不声明任意树安全或所有进程 handle 完全无泄漏。独立 Job CI 已接线但尚未远程执行，不改变 Core 门禁。

2026-11-01：05 的固定输入 fat Hello/eval 构建、五项本地运行和双 CPU/map/import 审计已通过。WSLInterop 路由修正经原工具失败/派生 Linux 工具成功的同上下文对照验证。集成提交 `1b06a79` 的[真实 Ubuntu CI](https://github.com/moorefu/qaplet/actions/runs/41263354111) 完成精确工具链下载、共享构建及原字节分发，取回文件与本地 canonical SHA 完全一致；见[05 验收](fat-ape-build-validation.md)。06/07/08/09 和 11/12 的前置在该时已满足；后续完整 Core/跨宿主已通过10，正式SDK不在M0范围。

Core完成由10的完整门禁判定。SCM/Job有独立artifact和结果；失败只阻断相应App能力。原始日志不能只保留成功项。必需用例blocked/not_run/skip时完整Core门禁不得通过。

BOOT-01/ENV-01、固定输入、产品 probe 和完整 Core gate 均已有实现及真实测量。当前状态见[10 验收](core-evidence-gate-validation.md)，历史缺口/失败保留；不把 M0 探针外推为正式 Runtime/SDK。

07 的实际 Linux/AArch64/65536 完整 guest、12 项独立深度合同、source-built loader/
packer 修正和 ARM64 full-pointer QuickJS overlay 已完成，见[64K 记录](arm64-64k-validation.md)。
所有新 APE 有各自摘要；06/08/10 必须消费这些新身份重新验证，旧 16K/旧 QuickJS
成功或失败均只属于历史记录。完整 Core 仍由 10 的矩阵门禁判定。

11 的应用私有 SCM ABI 已在用户授权的当前 Windows 宿主完成真实生命周期，
见[SCM 记录](scm-abi-validation.md)。Ubuntu 构建的独立 fat APE 在 Linux 普通入口、
Windows export/layout、管理员 ServiceMain/HandlerEx/status 与 SCM 查询/重复 stop/删除
均通过；普通 token 的 error 5 保留为 blocked。服务、受控进程与句柄清理及独立
post-delete 查询通过；Core map/import 边界再审计通过。独立 SCM workflow run
`41271366311` 已成功且原始字节/日志已核验；当前完整 Core 由10独立通过。

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

2026-11-01：10 的完整 Core gate 通过六目标 startup/I/O、12项 deep、实际断网 pair/14 refusal 和 Core link/import 边界。三类 producer 与各自产品/derived helper 身份分别保留；许可/TLS未知项公开。独立 Job CI `41273469604` 在 `e17981c` 成功，首次 +2 handle 身份仍未归因，能力只限已测 owned fixtures。完整 manifest、报告、负向回归和边界见[10 验收](core-evidence-gate-validation.md)。全部12票已有 Answer，最终integration review另行执行。

2026-11-01：09 已完成最终 producer 的两个真实断网 Ubuntu builds，六个二进制与额外 loader/packer 字节相同；14 个副本输入拒绝和精确 cache before/after inventory 通过。合入 `1129cd8` 后的 `make check` 107 tests / 0 failures / 15 explicit skips。测试 recipe 与随后 CI-only checker 变化的 raw fingerprint 分别保留，不宣称新 fingerprint 已重构。09 resolved，Core 完整门禁仍由 10 判定；见[断网最终验收](offline-integrity-validation.md)。
