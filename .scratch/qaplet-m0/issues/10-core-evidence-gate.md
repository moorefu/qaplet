# 10: M0 Core 证据汇总与门禁（BOOT-09）

**What to build:** 维护者有一个可执行、不可被缺失结果绕过的M0 Core完成判定，并能独立追踪服务探针。

**Blocked by:** [06 · 同一 APE 的跨宿主启动验证](06-cross-host-startup.md)、[07 · Linux ARM64 64K 深度探针](07-arm64-64k-probes.md)、[08 · Windows/Unix 原生 I/O 行为基线](08-native-io-baseline.md)、[09 · 离线重构与输入篡改拒绝验证](09-offline-integrity.md)。

**Status:** resolved

**Execution prerequisites:** 六类宿主及全部Core必需用例的结果齐备。

## Goal

维护者有一个可执行、不可被缺失结果绕过的M0 Core完成判定，并能独立追踪服务探针。

## Scope

聚合输入/构建/测试证据、用例覆盖、离线结果和Core link map/import检查。记录实际纳入的依赖许可、工具链来源及TLS依赖身份/风险待办；不在M0选定或实现网络TLS后端。生成只包含实测状态的实施报告。

## Out of Scope

将M0-SVC结果加入Core阻断集合、正式v1发布、完整SBOM发行流程、声称未实现API或未验证平台受支持。

## Source Files

M0报告聚合器、必需用例/宿主清单、依赖许可与来源记录、实施状态文档。

## Acceptance criteria

- [x] 必需宿主/用例缺失、失败、制品身份不符或离线验证不满足时，Core gate非通过且退出非零。
- [x] 每项通过追溯到source/toolchain/build/artifact/host/case及CI run/image；按(OS/架构/页大小/execution_mode)计算覆盖，原生ARM64记录真实页配置，QEMU仅补缺并追溯kernel/config/rootfs、QEMU/TCG和guest页大小；本地快速profile不能冒充完整矩阵；证据字段合法但没有实际执行不能通过。
- [x] Core链接记录无SCM/Job/service专有导入；服务探针有独立artifact、结果集合及状态。
- [x] 模拟服务探针失败不改变Core判定；真实Core探针失败必须改变Core判定。
- [x] 依赖清单和TLS相关历史风险以实际输入身份为准，未知项公开记录，不复制计划旧版本为新事实。

## Tests

对真实报告做聚合，再注入缺项、错摘要、失败、重复/冲突报告，验证判定；测试Core/SVC结果隔离。

## Negative Cases

空结果集、未知宿主、x64同页大小冒充ARM64、本地快速profile冒充完整矩阵、原生页变化、emulated冒充native、4K/64K guest不符、缺失串口日志、过期摘要、缺失原始日志、失败被skip、任意SVC失败误阻Core、Core失败误标通过。

## Evidence

Core汇总报告、可追溯索引、link/import审计、许可/来源清单、未决风险和独立SVC状态指针。

所有结果遵循[父规格](../spec.md)的身份与状态合同。尚未执行的探针不得填造成功结果。

## Rollback

回退聚合规则时保留原始日志；撤回不成立的完成声明，不能删除失败证据恢复绿色。

## Context

- [M0 执行规格](../spec.md)
- [任务索引、资源前置与里程碑](../README.md)

## Comments

2026-11-01：ticket 10 由独立 worktree `out/implement-spec/10` / branch `codex/m0-10-core-gate` 领取，起点为 integration `d516a94`；01–09 前置已 resolved。公共 CLI、实际 OS 行为及结构化 evidence 为已确认测试 seam。消费已保存的完整 CI `41271366261` 和真实断网重构证据，不重新查询已完成 run。

## Answer

完整 `scripts/core_gate.py evaluate` consumer 已实现并以实际 CI1129 / offline09 数据通过，Core pass / exit 0。主工作区保留 `out/m0-integration/core-gate/{inputs,inputs-linux,core-gate,core-gate-linux}.json`、425个 offline snapshot files 和仅两对被比较的 helper 产品；原报告与失败日志不改写。三类 producer、175项 startup/I/O/deep、617项 rehashed evidence、14个 ZIP transport、16项实际 offline cases 可追溯。SCM 与 Job 本地/独立CI状态指针各自保留，不进入Core阻断集合。

TDD 真实 consumer 19/19 通过，含超过40种副本注入及重新打包/更新摘要后的语义拒绝。Ubuntu 最终 `make check` 135 tests / 0 failures / 43 explicit skips，static checks 和实际 C bootstrap 均 pass；Ubuntu 另行消费同一原始证据 pass，Windows 全量 consumer pass。原始 stdout/stderr、测试 red/green 和最终日志位于 `out/implement-spec/10/out/`；有真实数据的 integration suite 由 `QAPLET_GATE_MANIFEST` 显式启用，没有模拟正向 Core 验收。已并入最新 integration `31cb3b9`；未更改任何 Core producer 配方、工具链或 workflow。

测试/当前 recipe inventory、每个 build 自己的 helper SHA、旧/新 I/O/deep 身份分别保留，原始真实断网 pair 不被冒称为新 fingerprint 的重构。实际许可清单、libc source unproven、network TLS unknown/unverified 与 M4 待办公开。完整输入合同、原始身份、已测宿主及限制见[验收记录](../core-evidence-gate-validation.md)和[开发入口](../../../docs/development.md#boot-09--完整-core-evidence-consumer)。正式 Runtime/SDK/API 与服务业务不在本票范围。
