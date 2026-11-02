# 05: 模块失败固定与路径拒绝合同

**What to build:** 证明改文件、修复失败和别名路径不会悄悄改变原 Runtime；用新 Runtime 验证修复，并明确拒绝 M1 不支持的 URL 与格式。

**Blocked by:** [04：文件 ESM、动态 import 与 TLA 闭环](04-file-esm-tla.md)

**Status:** claimed

**Execution state:** claimed；2026-11-02 在 `codex/m1-05` 独立 worktree 开始公开 SDK/Host 红绿验证。

**Execution prerequisites:** Ubuntu 构建环境与固定输入可用，且已合入本票所列阻塞任务；使用仓库约定的共享输入/缓存与现有构建、证据入口。

**Spec stories:** 14、15、16、17。

## Acceptance criteria

- [ ] 已有规范身份的加载/链接/求值失败统一保存；同实例不热更新或重试，新的 Runtime 可读修正内容。
- [ ] 未能规范化的请求不创建失败模块记录；已成功部分图和应用修改不回滚。
- [ ] 覆盖 symlink 规范化、硬链接不去重、普通文件限制、Windows 本地盘符/大小写的实际行为。
- [ ] query/fragment、authority、编码分隔符、NUL、UNC/设备路径、CJS/JSON/WASM/bytecode/native 库及未知 scheme 明确拒绝。
- [ ] 命中缓存不重新收费，失败记录纳入模块计数；每类路径/失败由实际文件和 C Host 验证。

## Validation and evidence

通过本票实际提供的公开 SDK/CLI 与纯 C Host 外部边界验证，观察公开状态、稳定错误 code、JS/native 副作用、复制数据、退出码和资源关闭事实。复用已有真实 producer、供应制品、宿主 consumer 与原始证据模式；每票执行自身验收及适用的 Cross/Full 门禁，平台收尾票不替代此前回归。

证据记录当前实现/输入/配方/SDK/APE 身份、实际命令、期望与实际结果、stdout/stderr 原始日志、退出码、时限、宿主与执行方式。外部 supervisor 超时表示用例未完成，不能作为产品正确停止/超时的证明；缺资源、失败、skip 与未完成保持可见，不继承旧 M0 成绩。

## Scope boundary

本票交付一个可单独演示和验收的增量，只声明已经实现并实际验证的范围；未实现能力不以成功 stub 公开。完整 M1 合同以实施规格为准；M2 的 OS waker、I/O 取消、迟到 completion 和完整调度，以及独立发布门禁，继续保持原分界。

## Context

[正式 M1 规格](../spec.md) · [已确认设计](../design.md) · [任务索引与依赖](../README.md) · [开发入口](../../../docs/development.md) · [证据合同](../../../docs/development/evidence.md) · [独立审查材料](../../../docs/agents/review.md)

## Comments

2026-11-02：用户以 `ok` 确认 16 票拆分及阻塞依赖，按 to-tickets 发布为 ready-for-agent。本票尚未执行；满足阻塞依赖后才可领取。

