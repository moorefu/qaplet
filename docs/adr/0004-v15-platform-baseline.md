# v1.5 平台主线与既有验收连续性

2026-11-01，用户确认以 [v1.5](../plans/qaplet-v1-implementation-plan-v1.5.md) 为当前路线，只交付通用平台；现有 M0、服务探针及计划外成果保留原始结论与引用。ADR 0001 的 Core/Application 技术边界继续有效，其第一方服务应用交付安排退为参考；ADR 0002 的固定输入原则继续有效，工具链来源固定到已选 Run/Artifact，不再从移动 Nightly 重新选型。

为在准备 M1 时保持可追溯的输入，先迁移统一 v3 锁，再受控升级 QuickJS 到 `2833510432045f72915919cef680ba59e5153b16`，复核现有 ARM64 补丁，补齐 M0-A 的 ESM/Promise 微型 C Host。cosmocc 继续使用现有已验的 Run `41169443573` / Artifact `13031425078` 原 ZIP；旧输入和成绩保留，新引擎的成绩按新身份重新签发。计划层对齐不修改实际构建锁或缓存。

用户明确继续采用 ADR 0003 的原生优先、完整系统 QEMU 补缺政策，取代 v1.5 提案中的物理 ARM64 64K 硬前置。完整 guest 必须固定 QEMU/TCG、machine/CPU、kernel/config/rootfs，并在 guest 内实测 ARM64/65536；报告区分 `native` 与 `emulated-system`，不外推物理硬件或性能。qemu-user 不满足该合同；新输入的相关 OS ABI 仍须重新验证。

长期原始 ZIP 镜像作为发布前门禁，M1 可先使用每次重新校验的现有缓存。缓存缺失或损坏时仍只接受固定原件/同 SHA 来源，离线缺失立即失败，不能切换滚动 Nightly；未验收长期镜像时不签发 release-ready。

M1 交付版本化最小 SDK、owner-thread 约束、有界 POD 队列和协作式 stop/interrupt，由 owner 主动处理；M2 完成跨 OS waker、I/O 取消、迟到 completion 和完整调度。这样可先形成可嵌入闭环，同时避免提前冻结尚未实现的 OS 调度合同。任务与适用证据见[对齐规格](../../.scratch/qaplet-v15-alignment/spec.md)。
