# 16: M1 严格证据入口与最终集成判定

**What to build:** 以一个可重放的严格入口汇总完整 M1 证据，正确接受实际合格结果，拒绝缺项、错身份、篡改、skip 和旧 M0 冒充。

**Blocked by:** [14：Cross 合同矩阵与生命周期循环验收](14-cross-host-contracts.md)、[15：ARM64 64K 与相关 Full 门禁验收](15-arm64-64k-full.md)

**Status:** resolved

**Execution state:** completed；2026-11-03 在 codex/m1-16 完成。最终严格入口 85/85、零 failures/errors/skips，M1 与独立当前 M0 Full 均 pass；Ubuntu 完整检查、当前原件 preflight 和 POSIX 只读回归通过。固定产品 21bd317 的 57 个 recipe blobs/工作字节不变；父规格保持 claimed 等待最终独立审查。

**Execution prerequisites:** 14、15 已完成且提供身份匹配的当前实现、供应原件、原始日志和报告；必需证据缺失不得改成空成功。

**Spec stories:** 59、60。

## Acceptance criteria

- [x] 只消费显式选定、身份匹配的 producer/SDK/APE/native/Full 证据，不把预检或计划当执行。
- [x] 完整规格合同、Linux 1,000 次和 Cross 每目标 100 次及 64K 关键覆盖都有可审计必需项。
- [x] 实际红绿及变异拒绝覆盖缺/重复/冲突项、错提交/SDK/制品哈希、改日志、错页和 skip。
- [x] 既有共享入口和证据工具复用，缺必需资源保持失败/未完成，不创建空成功或平行框架。
- [x] 最终集成输出明确 M1 实现/验收范围、能力及未覆盖项，保留旧 M0 原结论和 M2/发布门禁分界。

## Validation and evidence

通过本票实际提供的公开 SDK/CLI 与纯 C Host 外部边界验证，观察公开状态、稳定错误 code、JS/native 副作用、复制数据、退出码和资源关闭事实。复用已有真实 producer、供应制品、宿主 consumer 与原始证据模式；每票执行自身验收及适用的 Cross/Full 门禁，平台收尾票不替代此前回归。

证据记录当前实现/输入/配方/SDK/APE 身份、实际命令、期望与实际结果、stdout/stderr 原始日志、退出码、时限、宿主与执行方式。外部 supervisor 超时表示用例未完成，不能作为产品正确停止/超时的证明；缺资源、失败、skip 与未完成保持可见，不继承旧 M0 成绩。

## Scope boundary

本票交付一个可单独演示和验收的增量，只声明已经实现并实际验证的范围；未实现能力不以成功 stub 公开。完整 M1 合同以实施规格为准；M2 的 OS waker、I/O 取消、迟到 completion 和完整调度，以及独立发布门禁，继续保持原分界。

## Context

[正式 M1 规格](../spec.md) · [已确认设计](../design.md) · [任务索引与依赖](../README.md) · [开发入口](../../../docs/development.md) · [证据合同](../../../docs/development/evidence.md) · [独立审查材料](../../../docs/agents/review.md)

## Comments

2026-11-02：用户以 `ok` 确认 16 票拆分及阻塞依赖，按 to-tickets 发布为 ready-for-agent。本票尚未执行；满足阻塞依赖后才可领取。

## Answer

复用 `check_evidence.py` 增加显式 `m1-evidence` 严格 profile；`m1_gate.py` 使用同一个 Evidence/native/guest/Core 语义边界，按原 API/provider ZIP 绑定当前源码、封装 SDK、三 APE、包外纯 C 编译、五 native、64K 和独立当前 M0 Full。没有下载、产品进程执行、平行 guest/ZIP 框架、CI 伪造或旧 M0 替代。

最终产品固定为 `21bd317f4246571f0048789ca97167632c09f924` / 成功重试 Full `41489263809`。五 native 实际各 1000 循环；Windows 97 required + 1 optional（本次也实际 pass），其他每目标 98 required；64K 98 项、包外应用 8 项、当前 M0 Full 235 项及原离线证明分别通过。

最终 `out/m1-16/accepted-strict-windows.json` 为 85/85、零 failure/error/skip，包含重新打包的语义变异和原 ZIP 不变的 transport 变异；最终 Ubuntu check 310 项及 bootstrap、完整原件 preflight、3 项 POSIX byte/mode 回归通过。loader chmod、deadline、不可覆盖输出、中断计数、审计目标、缺 producer 命令和 timezone 正/负控制均保留实际红绿。所有失败尝试和原始 closure 保留，不以最终摘要覆盖。

详见[16 验收](../16-evidence-validation.md)和[严格入口合同](../../../docs/development/evidence.md#正式-m1-严格入口)。评估器另记源码身份，57 个产品 recipe blob/index/工作字节差异为空。父规格不在本票关闭，M2、物理 64K/性能和长期镜像发布门禁保持独立；`release_ready=false`。

