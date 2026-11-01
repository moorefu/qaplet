# 12: 应用私有 Job Object 可行性探针（BOOT-05B）

**What to build:** 维护者知道所锁工具链下应用私有Job桥接能否提供最小进程归属和终止能力，并明确其限制。

**Blocked by:** [04 · Nightly 工具链锁与离线缓存](04-nightly-toolchain-lock.md)、[05 · 构建 fat APE 与最小 QuickJS 探针](05-fat-ape-build.md)。

**Status:** resolved

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

- [x] 按本轮用户 override，在当前 Windows 宿主真实创建受控进程并查询/验证 Job 归属、基本终止和 owned handle 释放结果。
- [x] 测试明确列出实际覆盖的父子/两层嵌套场景；不由单 PID 退出推断任意进程树可安全回收。
- [x] ABI/OS 值和首用 handle-count 失败可重放；首用 +2 未归因及其他未验证能力不标 verified。
- [x] Core 构建不新增 Job 私有导入；fixture accounting active 0、owned process 已 wait、handles 已关闭。

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

2026-11-01：ticket 12 implementer 已在 `codex/m0-12-job` / `out/implement-spec/12` 认领，从 integration `8fb7c39` 开始。采用已确认的 CLI、实际 OS 行为和结构化 evidence seam；构建仅在 Ubuntu，Windows 只消费同摘要应用私有制品。

## Answer

2026-11-01：完成应用私有 Job APE、Ubuntu producer、同摘要 Windows consumer、独立 kill-on-close launcher fence 和独立 CI workflow。三个完整 Windows cycle 验证 owned parent+child、nested leaf、assignment access denied、重复 close、early exit、150ms leaf deadline 和全部 fixture 清理；实际 1 秒 launcher timeout 正确 fail 且 active 0。Windows 8/8 公开合同、Ubuntu 8/1 skip 及并入最新 integration 后最终 `make check` 115/25 explicit skips 通过；Core Hello/eval 独立 imports audit 均通过。远程 Job CI 未执行。

能力只对上述 owned fixture、实际 close 和初始化后的稳定计数 verified。首版两次真实 handle-count failure 保留；最终 staged 结果将首次 +2 定位到 CreateProcessW 路径，但 handle 身份仍未归因，不声明所有进程 handle 完全无泄漏或任意树安全。完整身份、命令、实际值、失败复现、限制与后续方向见[验收](../job-object-validation.md)及[共享入口](../../../docs/svc-job-probe.md)。

Fresh merge review 发现 first launch 的 fence 事实可能被后续 suite 覆盖；现已用统一的每次 launch gate 修复并分别保存全部 facts。13 种损坏真实记录的负向 module/report 回归先 red 后 green；无正向 OS success mock。C 和制品身份不变，当前 consumer 单独记录 source identity；同字节 Windows 重跑和真实 timeout 均保留两次 fence verdict/零残留。并入 `f24787b` 后 Windows 9/9、Ubuntu 9/1 skip、完整 `make check` 116/25 explicit skips 通过，详见验收的 consumer 修复段。远程 Job CI 仍未执行。
