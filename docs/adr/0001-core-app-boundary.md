# Core 与第一方服务应用独立交付和验收

2026-11-01：[ADR 0004](0004-v15-platform-baseline.md)将具体业务应用交付移出当前主线；下文的 Core/Application 技术边界继续有效，第一方服务应用安排保留为历史/未来参考。

沿用总体计划 v1.2 的 App-first 决策：Qaplet Core 与通用 Embed SDK 服务于所有应用，qaplet-svc 是第一方 Qaplet Application。服务业务属于应用，SCM/Job 等必要原生桥接属于该应用的私有 Native Host，默认 Core/SDK 不链接服务专有代码。这样可以用真实系统应用验证平台，同时避免将应用 ABI 风险和维护负担引入每个 Runtime 发行物。

M0-SVC 独立采集 ABI/系统行为证据；失败只阻断对应应用平台能力，不阻断已满足自身合同的 Core。应用暴露的通用 SDK/构建回归仍按通用合同修复。

来源：[总体计划 §3、§19](../plans/qaplet-v1-implementation-plan.md)；本轮 M0 边界已由用户确认。
