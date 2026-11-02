# 11: 并发关闭与最终释放的完整合同

**What to build:** 完整应用可在普通失败、资源 FAILED 或 PENDING 时 close；生产者安全撤下后 destroy，native 清理期间绑定保持有效。

**Blocked by:** [09：完整资源配置、计量与永久分配失败](09-resource-limits-failures.md)、[10：饱和与运行中的协作式停止](10-cooperative-stop.md)

**Status:** ready-for-agent

**Execution state:** not_started；尚未领取、实现或验收。

**Execution prerequisites:** Ubuntu 构建环境与固定输入可用，且已合入本票所列阻塞任务；使用仓库约定的共享输入/缓存与现有构建、证据入口。

**Spec stories:** 5、45、46、47、48、49。

## Acceptance criteria

- [ ] owner 未执行 JS/宿主回调时可 close PENDING/STOP_REQUESTED/STOPPED/FAILED；重入 BUSY，非 CLOSED destroy 为 BAD_STATE。
- [ ] 先拒收/停止调度并释放事件与 JS 引用，保留宿主绑定到引擎析构完成，最后发布 CLOSED。
- [ ] 控制句柄与同步存储活到 destroy，生产者在 close 前后查询/投递得到稳定拒绝，不触及已释放队列。
- [ ] 宿主撤下生产者并等在途调用结束后最终释放；SDK 不承诺检测未来调用或悬空指针。
- [ ] 重复 close 不重复清理，可检测关闭错误单独报告不覆盖首因；部分初始化失败及 stop/分配失败竞争的收尾有实际 C Host 验证。

## Validation and evidence

通过本票实际提供的公开 SDK/CLI 与纯 C Host 外部边界验证，观察公开状态、稳定错误 code、JS/native 副作用、复制数据、退出码和资源关闭事实。复用已有真实 producer、供应制品、宿主 consumer 与原始证据模式；每票执行自身验收及适用的 Cross/Full 门禁，平台收尾票不替代此前回归。

证据记录当前实现/输入/配方/SDK/APE 身份、实际命令、期望与实际结果、stdout/stderr 原始日志、退出码、时限、宿主与执行方式。外部 supervisor 超时表示用例未完成，不能作为产品正确停止/超时的证明；缺资源、失败、skip 与未完成保持可见，不继承旧 M0 成绩。

## Scope boundary

本票交付一个可单独演示和验收的增量，只声明已经实现并实际验证的范围；未实现能力不以成功 stub 公开。完整 M1 合同以实施规格为准；M2 的 OS waker、I/O 取消、迟到 completion 和完整调度，以及独立发布门禁，继续保持原分界。

## Context

[正式 M1 规格](../spec.md) · [已确认设计](../design.md) · [任务索引与依赖](../README.md) · [开发入口](../../../docs/development.md) · [证据合同](../../../docs/development/evidence.md) · [独立审查材料](../../../docs/agents/review.md)

## Comments

2026-11-02：用户以 `ok` 确认 16 票拆分及阻塞依赖，按 to-tickets 发布为 ready-for-agent。本票尚未执行；满足阻塞依赖后才可领取。

