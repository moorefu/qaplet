# 05: 构建 fat APE 与最小 QuickJS 探针（BOOT-04A）

**What to build:** 维护者可以从固定输入构建含目标 CPU slices 的 APE，以及验证原版 QuickJS 可被该工具链编译并求值的独立探针。

**Blocked by:** [03 · QuickJS 源码锁与来源记录](03-quickjs-source-lock.md)、[04 · Nightly 工具链锁与离线缓存](04-nightly-toolchain-lock.md)。

**Status:** resolved

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

- [x] 两个制品均有可重放构建命令、输入身份、CPU slice/header 检查和独立 SHA-256。
- [x] QuickJS 探针验证确定性表达式的结果和异常/错误退出路径；最小 create/eval/destroy 只服务于探针。
- [x] 构建与可执行入口不依赖旧项目代码或预编译运行时；所有必要补丁都通过源码清单。
- [x] Core 探针及其链接记录不含 SCM、Job Object 或服务配置实现。
- [x] 本地可执行用例有真实日志；ARM64 slices 存在不等同 ARM64 运行通过，native/补缺QEMU跨宿主证据交给后续任务；先本地探索构建失败，CI复用相同配方重现并分发同一摘要制品，不在测试job重新编译冒充同一文件。

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

2026-11-01：ticket 05 implementer 已领取；分支 `codex/m0-05-fat-ape`，基于 integration `cf0d125`。按已确认的 CLI / 原生行为 / evidence seam 执行 TDD。

2026-11-01：本地实现完成；最终 Ubuntu 构建使用主仓库 `.cache/{quickjs,toolchains/cosmocc,build}`，两个 fat APE 和五项实际运行/错误路径通过，迁移前后字节一致。完整身份、headers/双 CPU maps、符号/PE 导入与原始日志见[BOOT-04A 验收记录](../fat-ape-build-validation.md)。`make build-ape` 和独立 `m0.yml` 共用配方，测试 job 再编译由静态合同拒绝。保持 claimed，等待 integration 实际 Ubuntu CI 完成锁定原归档获取、构建与原字节 artifact 上传后由 coordinator 关闭；本地成功不冒充 CI 成功。

2026-11-01：WSLInterop 误将原工具链 MZ helper 启动为 Windows 的错误已由独立进程/路径对照证明。消费层现由锁定官方转换工具建立独立 Linux ELF 视图，62 个 helper 和 GNU 内部程序均覆盖，原缓存与锁不变，派生结果与完整 body 独立校验。同一启用 WSLInterop 的 invocation 原 driver 3/3 失败、派生 driver 3/3 成功，派生 route 无 `/init` / Windows helper。最终 fat 产品 SHA 不变、真正 invalid-flag 路径和 Ubuntu `make check`（65 / 0 fail）通过。保持 claimed，CI 获取/构建/上传的实际结果仍待 coordinator 核验。

## Answer

2026-11-01：集成提交 `1b06a7914c34f41909341f495b044c99f8127a00` 的 [M0 Ubuntu run 41263354111](https://github.com/moorefu/qaplet/actions/runs/41263354111) 已 completed/success。build job `124720949222` 实际完成只读 GH_TOKEN 跨仓库精确归档获取、缓存填充、共享构建/运行和原字节上传；不是仅静态 workflow 检查。相同提交的 [bootstrap run 41263354057](https://github.com/moorefu/qaplet/actions/runs/41263354057) 也成功。

已取回 artifact `13066340529`，ZIP 校验及 172 份构建命令原始日志定位通过，五项真实运行结果通过；两个 fat APE 的长度和 SHA-256 均匹配 producer report，并与本地 canonical 完全一致。构建宿主为原生 Ubuntu 24.04.5 x86_64 / kernel `7.0.0-1017-azure` / 4096 字节页，image `ubuntu24` / `20261026.354.1`。原 ZIP 下载缓存位于主仓库 `.cache/downloads/ci/`，解包的完整报告/制品和独立核对位于 `out/m0-integration/ci05/`。详细身份和限界见[验收记录](../fat-ape-build-validation.md)。06/07/08/09 及 11/12 的任务前置现已满足；跨宿主与完整 Core 门禁仍待各自实际验收。
