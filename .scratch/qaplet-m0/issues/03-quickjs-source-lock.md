# 03: QuickJS 源码锁与来源记录（BOOT-02）

**What to build:** 维护者可以取得和验证唯一、可追溯、干净的 QuickJS 源码输入。

**Blocked by:** [01 · 工程骨架与环境诊断](01-foundation.md)。

**Status:** claimed

**Execution prerequisites:** 首次显式获取原版 Bellard QuickJS 上游的网络访问。

## Goal

维护者可以取得和验证唯一、可追溯、干净的 QuickJS 源码输入。

## Scope

实施时重新解析 Bellard master，记录仓库、ref、commit、获取时间、源码内容身份和原始许可；建立最小补丁清单及验证入口，初始无补丁也要显式记录。此任务只锁源码，不依赖 Nightly。

## Out of Scope

采用 QuickJS-NG、迁移旧 fork、修改引擎、实现 Runtime/ESM/SDK、宣称已与 cosmocc 编译兼容。

## Source Files

上游源码供应模块、源码锁与 provenance、原始许可和补丁清单。

## Acceptance criteria

- [ ] 提交是从官方原版上游实际解析得到；不把总体计划中的参考 SHA 当作当期最新事实。
- [ ] 锁定后可以重新取得同一提交并验证干净状态；源码快照可追溯到实际内容。
- [ ] 空补丁集或每个补丁的来源/摘要都可核验，不能隐含本地引擎修改。
- [ ] 普通构建不再解析移动 HEAD 或静默 git pull；更新有独立显式流程。

## Tests

从锁重新获取并比对源码；重复校验应得到同一身份；验证干净树和补丁清单。

## Negative Cases

错误提交、上游访问失败、源码被改、补丁内容/顺序变化、缺失原始许可。

## Evidence

源码锁、获取与校验日志、上游许可、补丁报告；尚无编译证据时如实记录。

所有结果遵循[父规格](../spec.md)的身份与状态合同。尚未执行的探针不得填造成功结果。

## Rollback

恢复前一个经核验的锁与源码缓存；保留失败候选的诊断，不自动回退到计划示例或旧 fork。

## Context

- [M0 执行规格](../spec.md)
- [任务索引、资源前置与里程碑](../README.md)

## Comments

2026-11-01: Claimed on `codex/m0-03-quickjs`; implementation uses the confirmed external CLI/source-identity seam.
