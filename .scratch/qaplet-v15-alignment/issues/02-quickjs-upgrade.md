# 02：QuickJS 目标提交升级与 ARM64 补丁复核（BOOT-02）

**Status:** resolved
**Blocked by:** [01：统一 v3 输入锁](01-unified-input-lock.md)。
**Execution prerequisites:** Ubuntu；精确目标 Git 对象可从已验缓存或官方上游取得。获取为显式动作。

## Goal

把 M1 的引擎输入受控升级到 `2833510432045f72915919cef680ba59e5153b16`，保留旧 `e621135dc542e000e31038bedc52944aa0adaf1a` 基线及补丁证据。

## Scope

验证目标 Commit、Tree、VERSION、许可、原始内容与干净物化。比较旧源码和目标源码，针对现有 ARM64 full-pointer overlay 作出可证明的保留、适配或不再需要结论，记录补丁顺序、摘要与来源。使用统一 v3 状态和消费者生成新身份；同一 VERSION 文本不表示同一引擎。更新字节码/对象/构建指纹，隔离旧缓存，新输入成绩不得继承旧产物摘要。

## Out of Scope

跟随最新 master、修改固定 cosmocc 原 ZIP、正式 Runtime/SDK、完整 Node API、以版本字符串推断 Temporal/Intl 支持。

## Source Files

统一输入锁、`third_party/quickjs/` 来源/补丁/许可材料、`scripts/quickjs_source.py`、`scripts/build_ape.py`、现有 eval/ARM64 合同测试及对应证据记录。

## Acceptance criteria

- [x] 目标精确 Commit/Tree/VERSION 与官方对象一致，源码可离线重新物化。
- [x] 原始与叠加后内容身份分开；ARM64 补丁结论由源码差异和实际运行支持。
- [x] 使用新引擎构建最小 eval/对象/指针/错误路径探针，至少在 Ubuntu x64 与实际 ARM64（可用锁定完整 guest）验证关键修复。
- [x] 新输入/补丁改变使不兼容字节码、对象及相关构建缓存失效；旧内容仍可追溯。
- [x] 不删除旧锁/补丁历史，不把旧六目标成绩自动标成新引擎的覆盖；完整新跨宿主回归由 04 完成。

## Tests

精确获取与离线恢复、干净源码校验、补丁应用/顺序、现有可观察 JS 行为与错误/指针回归；记录实际两种 CPU 执行方式及产物 SHA。

## Negative Cases

错 Commit/Tree、同 VERSION 不同源码、补丁冲突/漂移、dirty 源码、错缓存命中、旧 bytecode 被复用、目标许可缺失、ARM64 既有崩溃复现。

## Evidence

源码差异、补丁审计、新 source/toolchain/build/artifact 身份、真实运行及拒绝日志。无法取得的宿主保持 not_run。

## Rollback

回退到可追溯旧输入及对应构建入口；保留新候选和失败报告，互不覆盖缓存。

## Context

[对齐规格](../spec.md) · [现有补丁原因](../../qaplet-m0/arm64-64k-validation.md) · [源码验收](../../qaplet-m0/quickjs-source-validation.md)

## Comments

2026-11-02：按已确认的共享 CLI、真实制品与证据消费者边界实施；隔离分支 codex/v15-02-quickjs 已同步 integration 788a2aa。保留目标候选状态至 02/03 实际探针共同满足晋级条件。

## Answer

已选择目标 `283351…` / Tree `b2763aa…`，分别锁定 pristine 与 overlay 内容，保留旧 manifest、
patch 和缓存。目标 quickjs.h 与旧 upstream 相同；未补丁 public JSValue 指针检查在实际锁定
ARM64 64K guest 仍 exit 3，保留 unchanged full-pointer overlay。最终受控 Ubuntu x64 与相同
字节 ARM64 完整 guest 的七项 eval/对象/指针/错误路径全部通过，源码可从已验本地 Git 对象离线
重新物化；九项 source 拒绝及候选门禁合同已执行。

精确身份、真实命令/报告/原始日志、红绿、实际 skips 与非通过范围见
[任务 02 验收](../quickjs-upgrade-validation.md)。锁仍为 selected，普通 make check/consumer 按设计
拒绝，03 完成 ESM/Promise 后晋级并重跑完整适用合同。此票不签发新六目标、M1 或 release-ready。
