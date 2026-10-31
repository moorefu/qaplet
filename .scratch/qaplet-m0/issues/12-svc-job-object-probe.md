# 12: 应用私有 Job Object 可行性探针（BOOT-05B）

**What to build:** 维护者知道所锁工具链下应用私有Job桥接能否提供最小进程归属和终止能力，并明确其限制。

**Blocked by:** [04 · Nightly 工具链锁与离线缓存](04-nightly-toolchain-lock.md)、[05 · 构建 fat APE 与最小 QuickJS 探针](05-fat-ape-build.md)。

**Status:** ready-for-agent

**Execution prerequisites:** 本地可丢弃Windows VM先探索，CI托管Windows临时VM持续回归；使用受控fixture并先测既有Job归属/限制，与SCM独立执行。

**Execution override（2026-11-01）：** 用户明确要求“直接使用本机”，本轮改用当前 Windows 开发宿主和受控 fixture；先测实际权限及既有 Job 归属，只终止本探针创建的进程并记录清理结果。保留实际宿主身份，见[父规格执行更新](../spec.md)。

## Goal

维护者知道所锁工具链下应用私有Job桥接能否提供最小进程归属和终止能力，并明确其限制。

## Scope

独立C探针验证Job导入/调用、创建/分配、受控子进程归属、退出/终止和句柄清理；记录runner既有Job归属、嵌套或拒绝分配等实际限制，区分CI环境限制与产品ABI失败。使用独立artifact与App结果，不把Job提升为默认Runtime接口。

## Out of Scope

正式Managed Process API、全量进程树安全保证、JS恢复策略、watchdog、权限逃逸方案、Core专有Job导入。

## Source Files

服务应用私有Job C探针、受控进程fixture、VM runner及独立证据。

## Acceptance criteria

- [ ] VM中真实创建受控进程并查询/验证Job归属、基本终止和句柄释放结果。
- [ ] 测试明确列出实际覆盖的子树/嵌套场景；不由单PID退出推断任意进程树可安全回收。
- [ ] ABI缺口或OS限制以可重放失败和修复方向记录；未验证能力不标verified。
- [ ] Core构建不新增Job私有导入；测试后无本fixture遗留进程/对象。

## Tests

受控父子进程正向路径、分配拒绝/重复关闭、受控嵌套情形和资源清理。

## Negative Cases

导入缺失、无权限、分配失败、提前退出、嵌套限制、超时和句柄泄漏。

## Evidence

独立制品/VM身份、API/错误码与进程归属结果、已验证限制、失败复现和清理记录。

所有结果遵循[父规格](../spec.md)的身份与状态合同。尚未执行的探针不得填造成功结果。

## Rollback

清理本fixture创建的进程与Job，由CI回收临时runner；不终止runner自身或无关Job；只回退应用私有探针，不影响Core门禁。

## Context

- [M0 执行规格](../spec.md)
- [任务索引、资源前置与里程碑](../README.md)

## Comments

尚未实施。
