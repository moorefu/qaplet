# 07: Linux ARM64 64K 页深度探针（BOOT-06）

**What to build:** 维护者能证明所锁工具链生成的原生 APE 在实际64K Linux内核上满足最小系统合同。

**Blocked by:** [05 · 构建 fat APE 与最小 QuickJS 探针](05-fat-ape-build.md)。

**Status:** claimed

**Execution prerequisites:** 优先本地完整系统QEMU 64K guest进行探索；持续集成时，原生Linux ARM64 runner若实测64K则直接测试，否则以QEMU补64K。锁定kernel/config/rootfs及QEMU/TCG/machine/CPU；qemu-user或WSL x64的页查询不能满足ARM64 64K前置。

## Goal

维护者能证明所锁工具链生成的原生 APE 在实际64K Linux内核上满足最小系统合同。

## Scope

针对 loader/ELF 段和启动头、pthread 与线程局部存储、分配/mmap/munmap、文件与 ZIP 资源读取设计独立 C 探针。新增探针制品逐个记录构建与摘要，不能和第5项产物混为一个身份。

## Out of Scope

1GB/4GB 大资源产品功能、正式 QJB 解析器、SDK 生命周期、物理ARM64验证和性能声明；qemu-user或未启动64K内核的模拟不能满足本任务。

## Source Files

ARM64 64K portability 探针、页大小与 header 检查、ZIP 测试资源及报告。

## Acceptance criteria

- [ ] 报告证明执行环境为Linux/AArch64且实际page size=65536，原生环境记native，完整QEMU guest记emulated-system并记录宿主架构；Windows 映射粒度或 MSYS 查询结果不能充数。
- [ ] loader、pthread/thread-local storage、分配/映射、资源读取用例有可断言结果。
- [ ] 每个新增 APE 的源码/工具链/构建摘要可追溯，并在实际64K内核执行；QEMU情况下保留kernel/config/rootfs摘要和串口日志，原生情况下保留runner/kernel身份。
- [ ] 所选原生宿主或guest未就绪、内核未启动、页大小不符或用例失败时保留阻断项；保留64K内核/用例门槛，不将模拟通过标为物理硬件通过。

## Tests

先在本地64K完整guest探索并复现，核验uname/sysconf/getconf后执行。CI按原生ARM64实测页配置选native 64K或一个补充64K guest；不无条件启动两套guest，4K差分由跨宿主任务复用同一runner记录。

## Negative Cases

错误对齐、越界资源、无效映射参数、线程局部数据交叉污染、错误架构或页大小报告。

## Evidence

实际内核/架构/页大小证明、各探针构建身份和原始结果、失败复现命令。

所有结果遵循[父规格](../spec.md)的身份与状态合同。尚未执行的探针不得填造成功结果。

## Rollback

隔离最小修补，保存异常样本；回退工具链/overlay需重新跑对应证据，不把64K状态改成默认通过。

## Context

- [M0 执行规格](../spec.md)
- [任务索引、资源前置与里程碑](../README.md)

## Comments

2026-11-01：ticket07 在 `codex/m0-07-arm64-64k` 独立 worktree 开始实施；基于 integration `dc5c0c3`，已确认祖先关系。TDD seam 沿父规格已确认的外部 CLI、实际 OS 与结构化报告；共享完整系统 guest runner 由本任务维护，06/08 复用。
