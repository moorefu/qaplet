# 13: 可分发 SDK 包与完整纯 C 示例

**What to build:** 从供应 SDK 包的公开头文件/静态库构建独立 C 应用，演示模块、异步事件、结果、停止与关闭全路径，并形成可核验分发清单。

**Blocked by:** [11：并发关闭与最终释放的完整合同](11-close-destroy-lifetime.md)、[12：共用 Runtime 的最小 CLI](12-minimal-cli.md)

**Status:** claimed

**Execution state:** in_progress；`codex/m1-13` 已确认继承 integration `0c9eb272ea1133222c89df2f88a475d8747e4af1`，11、12 均已 resolved 并合入，开始公开分发包与外部纯 C 消费切片。

**Execution prerequisites:** Ubuntu 构建环境与固定输入可用，且已合入本票所列阻塞任务；使用仓库约定的共享输入/缓存与现有构建、证据入口。

**Spec stories:** 50、57、58。

## Acceptance criteria

- [ ] Ubuntu 产生实际可消费 SDK 包及 CLI/C Host APE，头文件、库、目标组件、能力和有效默认值一致。
- [ ] 包外纯 C 消费者只使用供应公开材料即可构建完整应用，不依赖私有引擎或工作树内部头文件。
- [ ] ABI 1 的前缀、短结构、兼容零尾部、未知非零 flags/reserved 及独立诊断在真实包消费边界验证。
- [ ] 完整示例经公开操作演示注册/ESM/TLA/线程事件/pump/get_result/stop/close/join/destroy，失败路径也可复现。
- [ ] SDK 包/目标组件与 APE 摘要、source/recipe/toolchain、命令及日志纳入现有 producer/分发协议，旧 M0 身份不改写。

## Validation and evidence

通过本票实际提供的公开 SDK/CLI 与纯 C Host 外部边界验证，观察公开状态、稳定错误 code、JS/native 副作用、复制数据、退出码和资源关闭事实。复用已有真实 producer、供应制品、宿主 consumer 与原始证据模式；每票执行自身验收及适用的 Cross/Full 门禁，平台收尾票不替代此前回归。

证据记录当前实现/输入/配方/SDK/APE 身份、实际命令、期望与实际结果、stdout/stderr 原始日志、退出码、时限、宿主与执行方式。外部 supervisor 超时表示用例未完成，不能作为产品正确停止/超时的证明；缺资源、失败、skip 与未完成保持可见，不继承旧 M0 成绩。

## Scope boundary

本票交付一个可单独演示和验收的增量，只声明已经实现并实际验证的范围；未实现能力不以成功 stub 公开。完整 M1 合同以实施规格为准；M2 的 OS waker、I/O 取消、迟到 completion 和完整调度，以及独立发布门禁，继续保持原分界。

## Context

[正式 M1 规格](../spec.md) · [已确认设计](../design.md) · [任务索引与依赖](../README.md) · [开发入口](../../../docs/development.md) · [证据合同](../../../docs/development/evidence.md) · [独立审查材料](../../../docs/agents/review.md)

## Comments

2026-11-02：用户以 `ok` 确认 16 票拆分及阻塞依赖，按 to-tickets 发布为 ready-for-agent。本票尚未执行；满足阻塞依赖后才可领取。

2026-11-02：领取本票。测试 seam 沿用 Q32 已批准的公开 SDK、供应静态包 public-header-only C 应用、CLI 与现有 producer/consumer；不新增私有引擎测试入口。按 TDD 实际保存外部包消费红绿证据。

