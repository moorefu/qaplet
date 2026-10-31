# 04: Nightly 工具链锁与离线缓存（BOOT-03）

**What to build:** 维护者可以固定某一次真实 Nightly 输入，并在后续构建中只使用该摘要对应的工具链。

**Blocked by:** [01 · 工程骨架与环境诊断](01-foundation.md)、[02 · WSL2 Linux x64 构建环境就绪](02-wsl2-build-environment.md)。

**Status:** ready-for-agent

**Execution prerequisites:** 已就绪 WSL2 Linux x64；首次显式访问官方 Nightly 通道所需网络与缓存空间。

## Goal

维护者可以固定某一次真实 Nightly 输入，并在后续构建中只使用该摘要对应的工具链。

## Scope

显式下载官方 cosmocc Nightly，先记录并核验原始归档 SHA-256/长度再解包；采集版本输出、编译器摘要、来源与获取时间。建立内容校验缓存和显式 bump 流程。Cosmopolitan source-head 与工具链内 libc 身份分别记录，不猜测绑定。

## Out of Scope

固定旧正式 Release 代替 Nightly、无锁自动升级、伪造工具链对应源码、产品运行时联网下载。

## Source Files

工具链 bootstrap/verify、权威锁、只读缓存规则和来源报告；沿用总体计划 toolchains 布局。

## Acceptance criteria

- [ ] 原归档 SHA-256、长度、获取时间、URL、版本输出和编译器二进制摘要均来自实际测量。
- [ ] 若无法证明 libc 对应源码修订，明确记为未证明，不用当前 Cosmopolitan HEAD 冒充。
- [ ] 已锁输入在命中缓存时可以断网校验和使用；摘要不符或缓存缺失必须失败。
- [ ] 普通构建不隐式下载或升级；首次获取和 bump 都是独立显式动作。
- [ ] 工具链版本/来源声明可追溯；占位摘要或未知编译器身份不能被标为已验证。

## Tests

首次获取、缓存命中、重复校验及工具版本调用；构造损坏副本检验拒绝路径。

## Negative Cases

截断归档、摘要不符、缓存缺失、解包越界、工具不可执行、空间不足、网络失败。

## Evidence

实际锁值、原归档与编译器摘要、校验日志、工具版本和来源不确定性说明。

所有结果遵循[父规格](../spec.md)的身份与状态合同。尚未执行的探针不得填造成功结果。

## Rollback

切回已验证工具链锁/缓存；保留旧内容地址，不覆盖现有缓存，不以静默下载救场。

## Context

- [M0 执行规格](../spec.md)
- [任务索引、资源前置与里程碑](../README.md)

## Comments

尚未实施。
