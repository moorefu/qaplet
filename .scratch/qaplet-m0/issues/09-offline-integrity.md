# 09: 离线重构与输入篡改拒绝验证（BOOT-08）

**What to build:** 维护者能断网重构相同输入，并看到输入丢失或被篡改时构建可靠失败。

**Blocked by:** [03 · QuickJS 源码锁与来源记录](03-quickjs-source-lock.md)、[04 · Nightly 工具链锁与离线缓存](04-nightly-toolchain-lock.md)、[05 · 构建 fat APE 与最小 QuickJS 探针](05-fat-ape-build.md)。

**Status:** resolved

**Execution prerequisites:** 完整且已验证的源码、工具链与资源缓存；可验证禁止联网的构建运行条件。

## Goal

维护者能断网重构相同输入，并看到输入丢失或被篡改时构建可靠失败。

## Scope

在独立构建目录从锁定缓存重复构建foundation制品，记录离线约束与产物对比。将缺失缓存、损坏归档、改动源码/补丁、占位锁值等拒绝路径自动化。优先达到字节相同；差异必须定位来源并在门禁中明确显示。

## Out of Scope

自动下载修复缓存、忽略摘要、正式发行签名、公证、全面可复现构建服务。

## Source Files

离线验证入口、完整性负向fixtures、构建身份比对与差异报告。

## Acceptance criteria

- [x] 断网重构实际运行，保留缓存清单与无隐式网络的约束/记录。
- [x] 确定性foundation制品字节相同；若有差异必须解释并给可验证对比，未解释差异阻断任务。
- [x] 缺失或篡改输入、来源冲突、占位摘要会在使用前失败，且不能回退到新Nightly或旧源码。
- [x] 负向测试使用缓存副本，不污染正常已锁缓存。

## Tests

两个干净目录重构并比对，配套执行输入损坏/丢失等负向用例。

## Negative Cases

断网cache miss、SHA不符、源码或patch变化、未锁HEAD、占位字段、隐式联网、不可解释的字节差异。

## Evidence

两次构建日志与产物SHA、离线约束、差异定位、负向用例及退出码。

所有结果遵循[父规格](../spec.md)的身份与状态合同。尚未执行的探针不得填造成功结果。

## Rollback

恢复已验证缓存和构建配方；保存差异报告，不用更新锁值掩盖原输入损坏。

## Context

- [M0 执行规格](../spec.md)
- [任务索引、资源前置与里程碑](../README.md)

## Comments

2026-11-01：`codex/m0-09-offline-integrity` / `out/implement-spec/09` 从 integration `ab254628` 开始，已核验 ancestry。沿用已确认的 CLI、真实 OS 与结构化 evidence seams；最终断网双构建等待 07 的统一 64K producer 合入。

实现了隔离 namespace / UID、精确缓存 inventory、strace 审计、字节比较和复制负向入口；实际 preflight、10 个 before-use 拒绝、隐式网络 negative 与 Ubuntu `make check` 已通过。最终两个真实构建及新 helper source 拒绝仍待 07 producer 合入，状态保持 claimed。续作与原始证据指针见[验收记录](../offline-integrity-validation.md)。

## Answer

2026-11-01：合入 07 最终 producer 后实际完成两个全新目录的 disconnected Ubuntu builds，16/16 cases 通过；六个交付二进制与额外 loader/packer 均逐字节相同。两份 build trace 各 192 次实际 execve、零 internet attempts；14 个缺失/篡改副本全部在 supplier executable 使用前失败，原缓存 21,180 文件 / 2,815,491,333 字节精确 before/after inventory 不变。完整报告及六个 SHA 见[最终验收](../offline-integrity-validation.md#最终实际断网双构建)。

准备就绪前合入最新 integration `1129cd8`，Ubuntu `make check` 107 tests / 0 failures / 15 explicit skips、CI 静态合同、真实 C bootstrap 通过，包含已提供最终制品的 native startup / I/O 和四个 offline CLI 合同。实际双构建测试 recipe inventory 为 `dbf05fbe593bf0bfcf253b86f0bea7edf299cdf372349c6f868bbc92d6cb9a71`；合入后的 `930102f78a6fed1da01000750a48fe56e1e5e3b1e5ac2756af6f756308551afb` 单独记录，只改 builder 的 `check_ci()`，不宣称新 raw fingerprint 执行过双构建。历史 DrvFS 和缺 loader 失败原件仍保留。Core 及独立 macOS ARM64 helper reproducibility 不由此票关闭。
