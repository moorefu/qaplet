# 04: Nightly 工具链锁与离线缓存（BOOT-03）

**What to build:** 维护者可以固定某一次真实 Nightly 输入，并在后续构建中只使用该摘要对应的工具链。

**Blocked by:** [01 · 工程骨架与环境诊断](01-foundation.md)、[02 · WSL2 Linux x64 构建环境就绪](02-wsl2-build-environment.md)。

**Status:** resolved

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

- [x] 原归档 SHA-256、长度、获取时间、URL、版本输出和编译器二进制摘要均来自实际测量。
- [x] 若无法证明 libc 对应源码修订，明确记为未证明，不用当前 Cosmopolitan HEAD 冒充。
- [x] 已锁输入在命中缓存时可以断网校验和使用；摘要不符或缓存缺失必须失败。
- [x] 普通构建不隐式下载或升级；首次获取和 bump 都是独立显式动作。
- [x] 工具链版本/来源声明可追溯；占位摘要或未知编译器身份不能被标为已验证。

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

2026-11-01: ticket 04 implementer claimed this work on `codex/m0-04-nightly`, based on integration `efeed76`. CLI, real external commands, and structured evidence are the confirmed seams. Implementation uses vertical red-green slices; source HEAD and bundled libc provenance remain separate facts.

2026-11-01 progress: explicit acquisition, exact-lock cache restore, deliberate bump, immutable offline verification/path, acquisition receipts and 11 WSL CLI contracts implemented. Required WSL aggregate check passed (36 cases, one Windows-only skip). Local direct/proxy/Python TLS and the first hosted acquisition attempt failed to reach the official rolling archive; no production lock or compiler identity has been fabricated. The scoped acquisition workflow is an input transport fallback using the same CLI. [Validation and next acceptance step](../nightly-toolchain-validation.md).

## Answer

2026-11-01: implemented and validated against the original artifact from official Cosmopolitan Nightly run `41169443573`, artifact `13031425078`. Its original ZIP SHA-256 is `f2f984d3a9790674fc0da11c1c00a32d4fe3f8dafde99af74b0c6dc24a2a16cf`, length 419,652,152 bytes. This is a distinct official GitHub Nightly source; canonical cosmo.zip packaging identity is not assumed. All three measured compilers report GCC 14.1.0. The complete content/link/compiler lock and byte-preserved acquisition receipt are tracked under `toolchains/`; generated raw evidence under `toolchains/evidence/` remains local and Git-ignored per the user's later instruction.

WSL true disconnected-network verification, exact empty-cache restoration, repeat verification and same-length actual archive corruption/missing-cache refusal passed without changing the lock or primary cache. Safe internal symlinks are recorded; escapes, cycles, link-parent writes, damaged inputs, writable caches and conflicting origin metadata are refused. Fourteen WSL Nightly contracts pass; final shared checks pass on Windows and WSL (50 total contracts plus each system-C bootstrap). The workflow run commit is recorded separately; bundled libc source mapping remains unproven/null. Product construction and full M0 acceptance are not claimed. See [complete validation and limitations](../nightly-toolchain-validation.md).
