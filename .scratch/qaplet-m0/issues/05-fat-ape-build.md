# 05: 构建 fat APE 与最小 QuickJS 探针（BOOT-04A）

**What to build:** 维护者可以从固定输入构建含目标 CPU slices 的 APE，以及验证原版 QuickJS 可被该工具链编译并求值的独立探针。

**Blocked by:** [03 · QuickJS 源码锁与来源记录](03-quickjs-source-lock.md)、[04 · Nightly 工具链锁与离线缓存](04-nightly-toolchain-lock.md)。

**Status:** ready-for-agent

**Execution prerequisites:** 已验证的源码与工具链、Linux x64 构建环境。

## Goal

维护者可以从固定输入构建含目标 CPU slices 的 APE，以及验证原版 QuickJS 可被该工具链编译并求值的独立探针。

## Scope

生成 C Hello fat APE 和最小 QuickJS C eval 探针；记录真实编译/链接命令、flags、资源顺序、headers/slices、产物大小和摘要。使用所锁 Nightly 实际支持的 fat-APE 流程。若原版不兼容，记录失败；需要补丁时保持最小 overlay 和独立回归。

## Out of Scope

正式 qaplet run、ESM/TLA/Promise 调度、Runtime factory、稳定 Embed SDK、Node API、服务链接、发布支持声明。

## Source Files

foundation C/QuickJS 探针、APE 构建规则、私有补丁 overlay 和 build-info。

## Acceptance criteria

- [ ] 两个制品均有可重放构建命令、输入身份、CPU slice/header 检查和独立 SHA-256。
- [ ] QuickJS 探针验证确定性表达式的结果和异常/错误退出路径；最小 create/eval/destroy 只服务于探针。
- [ ] 构建与可执行入口不依赖旧项目代码或预编译运行时；所有必要补丁都通过源码清单。
- [ ] Core 探针及其链接记录不含 SCM、Job Object 或服务配置实现。
- [ ] 本地可执行用例有真实日志；ARM64 slices 存在不等同 ARM64 运行通过，native/补缺QEMU跨宿主证据交给后续任务；先本地探索构建失败，CI复用相同配方重现并分发同一摘要制品，不在测试job重新编译冒充同一文件。

## Tests

锁定工具链下构建与本地运行，检查值/退出码、slice、headers、link map 和 artifact hash。

## Negative Cases

错误表达式、编译/链接失败、缺失 slice、损坏资源、未经登记补丁、意外服务导入。

## Evidence

Hello 与 eval 产物、build-info、源码/工具链/补丁身份、编译日志、本地结果与链接检查。

所有结果遵循[父规格](../spec.md)的身份与状态合同。尚未执行的探针不得填造成功结果。

## Rollback

保留锁定输入与失败日志，回退本任务构建规则或独立 overlay；不切换引擎或用 OS 专用二进制替代 fat APE。

## Context

- [M0 执行规格](../spec.md)
- [任务索引、资源前置与里程碑](../README.md)

## Comments

尚未实施。
