# 05: 模块失败固定与路径拒绝合同

**What to build:** 证明改文件、修复失败和别名路径不会悄悄改变原 Runtime；用新 Runtime 验证修复，并明确拒绝 M1 不支持的 URL 与格式。

**Blocked by:** [04：文件 ESM、动态 import 与 TLA 闭环](04-file-esm-tla.md)

**Status:** resolved

**Execution state:** resolved；已合入 `d792f4b`，combined SDK Ubuntu 34/34、Windows 33 required pass（file-symlink 明确 blocked）、ARM64 64K 34/34；新输入 clean M0 Full/Core 235 项、deep/offline 已实际通过。

**Execution prerequisites:** Ubuntu 构建环境与固定输入可用，且已合入本票所列阻塞任务；使用仓库约定的共享输入/缓存与现有构建、证据入口。

**Spec stories:** 14、15、16、17。

## Acceptance criteria

- [x] 已有规范身份的加载/链接/求值失败统一保存；同实例不热更新或重试，新的 Runtime 可读修正内容。
- [x] 未能规范化的请求不创建失败模块记录；已成功部分图和应用修改不回滚。
- [x] 覆盖 symlink 规范化、硬链接不去重、普通文件限制、Windows 本地盘符/大小写的实际行为。
- [x] query/fragment、authority、编码分隔符、NUL、UNC/设备路径、CJS/JSON/WASM/bytecode/native 库及未知 scheme 明确拒绝。
- [x] 命中缓存不重新收费，失败记录纳入模块计数；每类路径/失败由实际文件和 C Host 验证。

## Validation and evidence

通过本票实际提供的公开 SDK/CLI 与纯 C Host 外部边界验证，观察公开状态、稳定错误 code、JS/native 副作用、复制数据、退出码和资源关闭事实。复用已有真实 producer、供应制品、宿主 consumer 与原始证据模式；每票执行自身验收及适用的 Cross/Full 门禁，平台收尾票不替代此前回归。

证据记录当前实现/输入/配方/SDK/APE 身份、实际命令、期望与实际结果、stdout/stderr 原始日志、退出码、时限、宿主与执行方式。外部 supervisor 超时表示用例未完成，不能作为产品正确停止/超时的证明；缺资源、失败、skip 与未完成保持可见，不继承旧 M0 成绩。

## Scope boundary

本票交付一个可单独演示和验收的增量，只声明已经实现并实际验证的范围；未实现能力不以成功 stub 公开。完整 M1 合同以实施规格为准；M2 的 OS waker、I/O 取消、迟到 completion 和完整调度，以及独立发布门禁，继续保持原分界。

## Context

[正式 M1 规格](../spec.md) · [已确认设计](../design.md) · [任务索引与依赖](../README.md) · [开发入口](../../../docs/development.md) · [证据合同](../../../docs/development/evidence.md) · [独立审查材料](../../../docs/agents/review.md)

## Comments

2026-11-02：用户以 `ok` 确认 16 票拆分及阻塞依赖，按 to-tickets 发布为 ready-for-agent。本票尚未执行；满足阻塞依赖后才可领取。


2026-11-02：`codex/m1-05` 已合入 `33ca543` 的 Host modules，最终实现源码
`eeaadc933344185248dd940493ca3e7672f809d6`。公开 Host 的冻结失败、实际路径与新
Runtime 修复均已执行；窄 NUL overlay 走 isolated actual source、selected controlled probes、
pinned、fresh ordinary producer 路线，保留 upstream Commit、首项 ARM64 patch、固定原 ZIP
和旧缓存。证据与未完成的 Full gate 见 [05 验收记录](../05-module-policy-validation.md)。
全新 clean Full run `41431796320` 完成前保持 claimed；此前两次 Full 失败不重标为 pass。

## Answer

2026-11-02：完成。实现来源 `c6d41f253914ec46e58375cf59d497c2bbff54b2`，已合入
最新 integration `d792f4bd0d0c9bdca8da9f84f013827aa553f889`。文件策略、规范失败固定、
source/alias 冻结、新 Runtime 修复、实际硬链接/软链接与 Windows native drive/case 合同
均由公开 SDK + 真实 C Host 验证；失败身份占模块数，缓存命中不重复收费。当前公开内建
`qaplet:events` 的模块数通过 POD snapshot 计入断言。

NUL 通过窄 module-name overlay 在长度丢失前拒绝，普通 bridge NUL 字符串继续成功；
保留原 upstream Commit/Tree、首项 ARM64 patch、固定 cosmocc 原 ZIP 与旧已验缓存。
独立 candidate、actual controlled Ubuntu/Windows/ARM64 probes、pinned 与 fresh ordinary
producer 的全过程及失败原件均保留。稳定模块错误沿用 14–16 与 common codes，能力 bit10。

最终供应 producer `out/m1-05/combined-events-v1/build.json`、SDK/APE 同目录；Ubuntu
34/34、Windows 33 required、ARM64 64K 34/34 通过。Windows file-symlink 资源实际缺少
权限，以 blocked/非 required 和原始 exit19712、WinError1314 明确记录，不计入 pass。
新输入 M0 clean Full run `41431796320` 实际通过；原 Core 报告 235 项、deep/offline 通过。
详细身份、命令、原始 stdout/stderr、关闭事实、原件与范围见
[05 验收记录](../05-module-policy-validation.md)。不声明整个 M1 Full 或 release-ready。
