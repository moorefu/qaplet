# 10: M0 Core 证据汇总与门禁（BOOT-09）

**What to build:** 维护者有一个可执行、不可被缺失结果绕过的M0 Core完成判定，并能独立追踪服务探针。

**Blocked by:** [06 · 同一 APE 的跨宿主启动验证](06-cross-host-startup.md)、[07 · Linux ARM64 64K 深度探针](07-arm64-64k-probes.md)、[08 · Windows/Unix 原生 I/O 行为基线](08-native-io-baseline.md)、[09 · 离线重构与输入篡改拒绝验证](09-offline-integrity.md)。

**Status:** ready-for-agent

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

- [ ] 必需宿主/用例缺失、失败、制品身份不符或离线验证不满足时，Core gate非通过且退出非零。
- [ ] 每项通过追溯到source/toolchain/build/artifact/host/case及CI run/image；按(OS/架构/页大小/execution_mode)计算覆盖，原生ARM64记录真实页配置，QEMU仅补缺并追溯kernel/config/rootfs、QEMU/TCG和guest页大小；本地快速profile不能冒充完整矩阵；证据字段合法但没有实际执行不能通过。
- [ ] Core链接记录无SCM/Job/service专有导入；服务探针有独立artifact、结果集合及状态。
- [ ] 模拟服务探针失败不改变Core判定；真实Core探针失败必须改变Core判定。
- [ ] 依赖清单和TLS相关历史风险以实际输入身份为准，未知项公开记录，不复制计划旧版本为新事实。

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

尚未实施。
