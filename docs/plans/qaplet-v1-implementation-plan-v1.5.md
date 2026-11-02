# Qaplet v1：全新实施总体架构与工程交付计划

> **文档状态**：当前执行基线，含 2026-11-01 用户确认的实施修订  
> **版本**：v1.5 / 2026-11-01  
> **产品名称**：Qaplet  
> **产品定义**：**An Actually Portable, Node-compatible JavaScript Runtime & Application Platform**  
> **技术基线**：Bellard QuickJS 锁定提交 + Cosmopolitan 固定 GitHub Actions cosmocc Build + Qaplet Host/Event Loop + Node/Web APIs + QJB/App Compiler + C Embed SDK。  
> **实施策略**：Linux x86-64 最小闭环先行；M0-B/C 跨 OS/CPU 基础验证提前并行；之后功能纵向推进、Fast/Cross/Full 持续门禁。  
> **项目范围**：**仅 Qaplet Runtime、SDK、包格式、工具链、通用应用编译、跨平台发布；本计划只覆盖通用平台能力，不包含具体业务应用的开发内容。**  
> **独立实施**：Qaplet 独立实现，复用本仓库已有 M0 基础；`moorefu/quickjs-cosmo` 只作为问题/测试素材，不视为 Qaplet 的现成实现。

## 实施修订与当前状态（2026-11-01）

本版核心路线已获确认；[ADR 0004](../adr/0004-v15-platform-baseline.md)记录以下取舍，[增量规格](../../.scratch/qaplet-v15-alignment/spec.md)和[独立任务](../../.scratch/qaplet-v15-alignment/README.md)给出实施边界。正文涉及的命令、目录和接口若尚未接入，均为目标合同，不能视为现有能力。

- **已有证明保留**：v1.4 M0 12 票及 Core gate 已按原合同完成，旧计划、代码、失败和验收历史继续保留。服务相关工作作为未来应用/系统参考，退出当前平台交付主线。
- **输入事实校正**：现有 cosmocc 原件就是本版指定 Artifact，独立下载/哈希/清单/离线恢复已有证据；不重置为“尚未下载”。目标 QuickJS `2833510432045f72915919cef680ba59e5153b16` 已由任务 02 精确选择并复核 ARM64 overlay；旧源码、输入和成绩继续保留原范围。
- **M1 前置**：统一 v3 锁迁移 → 目标 QuickJS 升级 → M0-A ESM/Promise 微型 C Host；对应任务完成后进入正式 M1。任务 01/02/03 已完成统一入口、目标源码/补丁验证及 [ESM/Promise 微型 Host](../../.scratch/qaplet-v15-alignment/esm-promise-validation.md)，新配方已晋级 pinned，允许进入正式 M1；04/05 约束对应验收与 ABI 冻结。
- **平台门禁**：继续原生页覆盖优先，完整系统 QEMU 可补缺失的 ARM64 4K/64K。固定 guest 输入、实际内核页大小和 `native`/`emulated-system` 必须记录；不增加物理 ARM64 64K 硬前置，不外推物理性能。
- **原件镜像**：长期原 ZIP 镜像及同 SHA 恢复在发布前完成；M1 可先使用逐次校验的已有缓存。原件缺失/损坏仍 fail-closed，不能换 rolling Nightly。
- **SDK 分期**：M1 实现最小 SDK、owner-thread 约束、有界 POD 队列与协作式 stop/interrupt；M2 完成跨 OS waker、I/O 取消、迟到 completion 和完整调度。

## 0. 执行摘要

Qaplet 的目标是交付**真正可携带、可嵌入、具有声明式 Node/Web 兼容能力的 JavaScript 应用运行时**。以 Bellard QuickJS 作为唯一 JS 引擎，Cosmopolitan/APE 作为跨平台 native 基础；支持 ESM/CJS、核心 Node 风格 API、QJB 应用包、独立 fat APE 应用、通用 C Embed SDK、按需读取 ZIP64 大资源。产品不追求 100% Node.js API，也不以 Javascript Context 冒充进程权限隔离。

**开发采用三层早期门槛，而不是两个极端：**

1. **M0-A：Linux x86-64 最小闭环**。从指定 cosmocc 固定 Build 与 QuickJS 锁定源码构建并执行最小 APE；运行微型 C Host + ESM/Promise，记录真实产物 SHA、程序输出和工具链身份，不依赖完整 SDK 或 Node API。
2. **M0-B：同字节 fat APE 跨平台执行**。Windows x64、macOS x64/ARM64、Linux ARM64 4KB 等尽早验证启动、argv/stdio、基础 JS 以及 OS 识别；验证的是**同一个文件**，不是分别构建的文件。
3. **M0-C：高风险 ABI 探针**。在冻结通用 Reactor、Process、TTY、SDK 事件桥之前，验证 Windows Socket/HANDLE/Pipe/Console 与 Linux ARM64 64KB 真实内核中的 loader、pthread TLS、mmap/ZipOS。M1 的纯 JS/Engine 工作允许与 M0-B/C 并行。
4. **M1–M4：宿主与 JS/Node 栈**。完善 QuickJS Host、单逻辑 Loop、Reactor/WorkPool、模块加载、TTY、FS、Process、网络与 TLS，并使用 Node Oracle 差分和真实应用进行校验。
5. **M5–M7：可交付 App 平台**。定义 QJB v1、安全 ZIP/VFS、QJB 与 APE 运行模式、通用 native Host App 编译能力及大资源 mmap。
6. **M8–M9：工程收口与发布**。Fast/Cross/Full 分级 CI、SBOM/许可证/签名、离线 clean rebuild、同字节实测、兼容矩阵与长期稳定性门禁。

**固定 cosmocc 来源（不再自动追最新 Nightly）**：[`jart/cosmopolitan` Run `41169443573`](https://github.com/jart/cosmopolitan/actions/runs/41169443573)、[`cosmocc` Artifact `13031425078`](https://github.com/jart/cosmopolitan/actions/runs/41169443573/artifacts/13031425078)。GitHub 官方记录关联源码提交 `aefad27c09ffda64c6ac64bd69f758e9f9c7b91d`、原始 Artifact ZIP SHA-256 `f2f984d3a9790674fc0da11c1c00a32d4fe3f8dafde99af74b0c6dc24a2a16cf`、大小 `419652152` 字节。**此哈希是 GitHub Artifact ZIP 的摘要，并非另一个 `cosmocc.zip` 的摘要。**

QuickJS 的本版目标为 `2833510432045f72915919cef680ba59e5153b16` / `VERSION=2026-06-04`，已替换旧 `e621135dc542e000e31038bedc52944aa0adaf1a`；Commit/Tree/VERSION/patchset 已由升级任务分别核验，原 ARM64 overlay 经实际 64K guest 保留。固定 cosmocc 原 ZIP 已在既有 M0 独立取得、哈希、编译与离线恢复，证据见[工具链验收](../../.scratch/qaplet-m0/nightly-toolchain-validation.md)。新的统一 v3/目标引擎与微型 Host 配方已在 02/03 实测后晋级 `pinned`；跨宿主回归与 release-ready 仍需各自证据。

**供应链红线**：本地 `.cache/` 与 GitHub Actions cache 仅作可丢弃加速。原始 Artifact 必须按 SHA 长期镜像；缓存命中仍校验；不能自动退回滚动 `cosmo.zip` Nightly；不能靠解压路径存在就认定 compiler 可用。二进制执行权限与链接语义恢复必须经过白名单审计和实际 probe。所有 JS 操作仅在其 owner thread；跨线程 OS 事件要经有界队列/唤醒进入 Loop。

---

## 1. 事实输入、固定 GitHub Actions 构建、工具链锁与缓存治理

### 1.1 已选择的 cosmocc Build（截至 2026-11-01）

**工程选型已固定，不再从滚动 URL 选 Nightly：**

| 字段 | 经 GitHub API / Run 日志核验的事实 |
|---|---|
| Repository | [`jart/cosmopolitan`](https://github.com/jart/cosmopolitan) |
| Workflow | `.github/workflows/nightly-cosmocc.yml`，`Nightly cosmocc` |
| Run | [`41169443573`](https://github.com/jart/cosmopolitan/actions/runs/41169443573)（Run #807、attempt 1、schedule） |
| Run 状态 | `completed` / `success` |
| Run 的 `head_sha` | `aefad27c09ffda64c6ac64bd69f758e9f9c7b91d` |
| 构建脚本 | `tool/cosmocc/package.sh`（在上述 Commit 的 checkout 后执行） |
| Artifact | [`cosmocc` / ID `13031425078`](https://github.com/jart/cosmopolitan/actions/runs/41169443573/artifacts/13031425078) |
| Artifact ZIP `digest` | `sha256:f2f984d3a9790674fc0da11c1c00a32d4fe3f8dafde99af74b0c6dc24a2a16cf` |
| Artifact ZIP 大小 | `419652152` 字节（约 400.2 MiB） |
| 上传内容 | `actions/upload-artifact@v4.3.5`，`path: cosmocc`，日志称 3,043 个文件被上传 |
| 构建环境 | GitHub-hosted Ubuntu 24.04.5 / `ubuntu-24.04`（来自 Run Job 日志） |
| 过期记录 | `2027-01-29T04:39:16Z`（2026-11-01 查询值，不是永久保存承诺） |

**三种身份必须严格区别：**

1. `source_ref`：Run `head_sha` 关联的 **Cosmopolitan 源码 Git Commit**；已从 workflow checkout / job metadata / `tool/cosmocc/package.sh` 的调用链获得构建关联证据。该 Run 在正常可信 GitHub Actions 环境下使用此代码进行构建，但工作流没有提供独立的密码学签名 provenance/attestation，不应写成已取得 SLSA 级证明。
2. `artifact_zip_sha256`：GitHub `upload-artifact` **生成的原始 Artifact ZIP** 的摘要，不是源码压缩包哈希，也不是从 `https://cosmo.zip/pub/cosmocc/cosmocc.zip` 获得的归档摘要。实际 `download` 后必须由 Qaplet 独立重新哈希，匹配 GitHub 元数据才允许解压/执行。
3. `extracted_toolchain_manifest`：从上述 ZIP 安全解压并验证模式/链接、完成编译探针后计算的每文件清单。现有锁已记录完整 file/link 清单和模式；统一 v3 的组件摘要算法及迁入结果见[实施合同](../development/input-lock.md)，不能用 Artifact ZIP 的 SHA-256 代填组件摘要。

本版选择的 QuickJS 输入为 `2833510432045f72915919cef680ba59e5153b16` / `VERSION=2026-06-04`，实际升级和 Commit/Tree/patch 核验由独立任务执行；txiki.js、LLRT、Redis ae、llamafile、zipalign 和 `moorefu/quickjs-cosmo` 沿用前版所注明的参考提交，仅作参考，不把旧参考 SHA 冒充新的 HEAD。

**明确不采用**滚动 [`cosmo.zip` 的 `cosmocc.zip`](https://cosmo.zip/pub/cosmocc/cosmocc.zip) 作为自动 fallback。即使当前该 URL 的内容恰好来自同一 Commit，也不能直接视为与本 Artifact ZIP 字节同一。不得因选定 Artifact 过期/不可访问而静默下载当日 Nightly 或另一 Run。

### 1.2 统一 v3 锁：复用既有工具链证明，验收新输入组合

统一入口为 `toolchains/qaplet.lock.json` / `qaplet.toolchain-lock.v3`，已由任务 01 迁移并验证既有 M0 组合；`third_party/quickjs/source.lock.json`、`toolchains/cosmocc.lock.json` 与 helper 来源锁成为被精确摘要绑定的组件清单。[目标选型样例](qaplet-toolchain-lock-selected.json)保留在独立 JSON 文件中；样例的 `selected_pending_validation` 不代表当前实际锁状态；当前配方已由 02/03 验收晋级 pinned。样例的组件摘要 null 保持未验证目标范围，已实施字段见实际统一锁与迁移合同，不代表既有清单不存在：

目标格式字段以该选型文件为参考；它仍是规划输入，不被当前 builder 消费。现有权威锁和完整组件/源码清单保持不变。

> `local_archive_hash_verified=true` 引用既有相同原 ZIP 的独立测量，不替代未来每台机器的完整性校验。统一格式、目标源码/补丁与新功能探针通过后才能按新合同晋级 `pinned`；长期镜像另为发布门禁。实际来源/恢复记录优先复用现有工具链验收文件。**每次 CI 的缓存验证仍独立执行**；不能仅依赖锁里某个 `true`。

**`lock_state=selected_pending_validation` 仅允许显式获取与受控基础探针，不允许签发普通/发行构建成绩。** 新组合 `pinned` 要求统一入口、源 Commit/Tree/patch、原 ZIP、组件 Manifest、模式/链接、工具链及目标引擎功能探针和审计通过；正式 CI/Release 使用 pinned 输入。pinned 不等于 release-ready，后者还要求长期镜像恢复与对应发布门禁。

升级策略改为 **显式更换官方 Run/Artifact**：提出 `toolchain-bump` PR，记录新的 Run、`head_sha`、Artifact ID、原始 ZIP SHA 与二进制差异，然后重新跑 M0-A/B/C、安全回归。Build/CI 全程禁止自动选取“最新成功 Nightly”。

### 1.3 固定 Artifact 的下载、留存与权限恢复

**分阶段获取与留存**：M0-A/M1 可复用逐次校验的已有原 ZIP 缓存；缺失时只从明确允许的固定 Artifact 或同 SHA 来源恢复。发布前由维护者将已核验原 ZIP 保存到安全可信的长期存储，形成**原样不可变拷贝**并完成恢复验收；这不是首次 pin 的附加前置。后续本地与 GitHub CI 优先从长期存储或本地已验证缓存读取；GitHub Actions Artifact 原始下载作为过期前的明确后备来源。

示例（维护者工作站或**受保护 workflow** 的一次性抓取；令牌必须由密钥库注入，不在日志里打印）：

```bash
set -eu
# 先: export GH_TOKEN=...  (仅在可信终端/Secret 中提供，绝不 commit)
tmp="$(mktemp)"
trap 'rm -f "$tmp"' EXIT
curl --fail --silent --show-error --location --retry 3 \
  -H "Authorization: Bearer ${GH_TOKEN:?GH_TOKEN required}" \
  -H 'Accept: application/vnd.github+json' \
  'https://api.github.com/repos/jart/cosmopolitan/actions/artifacts/13031425078/zip' \
  --output "$tmp"
printf '%s  %s\n' \
  'f2f984d3a9790674fc0da11c1c00a32d4fe3f8dafde99af74b0c6dc24a2a16cf' \
  "$tmp" | sha256sum --check --status
# 另外核对 size=419652152；经 ZIP 安全检查后，原样复制到受控镜像。
```

**两个容易遗漏的问题**：

- **ZIP 根目录与模式/链接**：先审计实际成员、模式与链接，再决定是否恢复。既有相同原 ZIP 的验收记录了 3,043 个 file/link 条目、208 symlinks、77 executable entries，未进行权限修复；不能套用“本归档一定丢失可执行位”的假设。若未来解包流程实际丢失所需语义，仅依审计过的路径白名单恢复并记录规则/Manifest，执行版本、Hello APE 与目标 CPU 探针。原件、恢复后物化及派生 Linux 工具视图各自保持身份。
- **Archive 的身份**：GitHub API 下载的是由 `upload-artifact` 生成的 ZIP。`gh run download`、`download-artifact` 等高层工具可能**自动解压**，不能用解压目录重新打包的 ZIP 去比较上面那个哈希。若需要原样离线镜像，必须下载并保存**原始 ZIP 字节**。若需要对修复权限后的工具链再打包，必须作为**派生制品**另记 SHA/原始来源/权限恢复脚本版本，不能冒充原始 Artifact。

**永久保存与失效策略**：本次 Artifact 查询显示过期时间为 2027-01-29，GitHub 可依保留策略或管理员操作提前删除。发布前必须完成原始 419,652,152 字节在长期受控存储中的留存和同 SHA 恢复验收，带访问审计及保留/覆盖策略；此项不阻断 M0-A/M1 使用已验缓存。具体存储和授权由独立发布任务落实。GitHub `actions/cache` **不是**永久存储。若官方 Artifact 已过期、镜像也缺失，必须阻断构建，并由维护者另行评审“按同一 Commit 自构建的新工具链”为**另一个新制品**，不能声称字节兼容。

`tool/cosmocc/package.sh` 本身还会拉取由脚本中 SHA-256 锁定的 GCC/LLVM 辅助包；这些内容由该 Build 的官方动作产出，仍应在 SBOM/provenance 中记录依赖来源，不能仅记录 Cosmopolitan Git Commit。

### 1.4 本地 `.cache/` 与 GitHub CI 缓存：统一 ZIP 身份、不同信任级

缓存统一位于主工作区物理根目录 `.cache/`，linked worktree 共享该根；整个目录被 Git 忽略，路径选择不能改变输入身份。下列为目标布局示意；统一锁迁移优先兼容现有 `.cache/quickjs/<commit>-<patch-prefix>/source` 与 `.cache/toolchains/cosmocc/<sha>/tree`，不把目录重排作为额外前置。原件/物化以**官方 Artifact 原始 ZIP digest** 寻址：

```text
.cache/                                   # 从不提交 Git
├── downloads/
│   └── cosmocc/
│       └── sha256-f2f984d3...a16cf.zip # 官方 Artifact ZIP 原始字节
├── sources/quickjs/<locked-commit>/      # detached/只读
├── vcs/quickjs.git/                      # 可选可信 Git object mirror
├── toolchains/
│   └── cosmocc/<ARTIFACT_ZIP_SHA>/       # 安全解压 + 受控 mode/link 修复 + manifest
├── build/<host>-<target>-<profile>-<inputs-hash>/
├── probes/
└── tmp/                                   # 失败可删除的 staging
```

- **本地**：原始 ZIP 有效且 digest 匹配才允许解压；解压目录仅在 lock/Manifest、权限修复规则和 probe 匹配时复用。恢复命中仍需验证，Release/受保护 CI 强制深检。校验失败先隔离损坏内容，在受控 staging 中从显式允许的固定 Artifact 或同 digest 镜像重取；仍失败就阻断，不删除唯一原件或换另一 Nightly。
- **GitHub Actions**：默认 `actions/cache` 仅保存**原始 ZIP**，Key `qaplet-cosmocc-artifact-f2f984d3...`（全 64 位摘要）；不默认缓存解压后的可执行组件、构建对象/`.a`、私钥或凭据。PR 只能 Restore，主分支可信 job 可以 Save；命中不代表可信，必须再次执行 SHA、ZIP 内容及解压验证。cache miss 只从该阶段可用的固定字节来源取得原件；fork PR 不被要求访问跨仓库 Artifact 或注入 PAT，所需原件应预热或从已批准来源取得，否则明确 blocked。发布前必须具备已验长期镜像。
- **CI 下载访问**：跨仓库访问 GitHub Actions Artifact 需要适当访问权限；运行在 Qaplet 仓库的默认 `GITHUB_TOKEN` **不能假定**有权读取第三方仓库的 Artifact。维护者在可信渠道一次性复制到组织长期镜像，Release CI 从受审计镜像/内部镜像获取。离线 CI 必须事先预热原始字节。
- **缓存安全**：对下载和 ZIP 提取设置时间/空间上限，拒绝 ZIP 路径逃逸、特殊文件/重名、超限解压、符号链接劫持；临时 staging 文件只有验证后才原子发布。`make cache-clean` 删除 `.cache` 不应影响源码/lock，但离线模式缺失锁定 ZIP 就必须 fail-closed。
- **保留政策**：源码 lock、二进制摘要、权限恢复规则/脚本、GitHub Run URL、镜像路径类型和制品清单进 Git；镜像本体/临时对象/cache 不进入 Git。长期镜像路径可由受保护配置提供，但唯一可接受的内容仍由锁内哈希决定。
- **注意原始 ZIP 的解压路径**：upstream workflow 的 `path: cosmocc` 可能使 ZIP 将 `bin/`、`include/` 等直接放在归档根；Bootstrap 应**验证实际成员布局**，不得硬编码“必有顶层 `cosmocc/`”。解压出的任何二进制首先视为不可信文件，哈希、权限白名单、动态依赖检查和基础 Probe 通过后才执行正式构建。

建议维护者命令合同（**计划实现，非现有命令**）：

```bash
make deps-status
make deps-fetch                 # 官方固定 Artifact 或经批准的原样镜像（同 digest）
make deps-verify VERIFY=deep    # ZIP SHA、大小、源码 Commit/Tree、内容 manifest
make bootstrap                  # 解压、权限/链接恢复、基础 toolchain probe
make m0-a                       # Linux x64 QuickJS/APE/微型 C Host
make m0-b                       # 同字节 fat APE 多 OS/CPU
make m0-c                       # Windows I/O 与 ARM64 64KB 专项
make toolchain-bump             # 显式更换被选 Run/Artifact，走受保护审核
make cache-status
make cache-clean
```

完整步骤的工程证据包括：官方 Run/Job URL、`head_sha`、workflow/脚本路径、Artifact 原始 ZIP 摘要、原始字节保留地址与访问策略、提取后 Manifest、恢复权限的规则摘要、编译器版本/ABI、M0-A/B/C 的真实运行结果。**GitHub Run 证明来源关联，但无法代替独立校验和实际功能验收。**

### 1.5 技术依赖与授权原则

- QuickJS：官方 Bellard `master` 选定 Commit/Tree、最小可审计补丁；不使用 QuickJS-NG 或第二 JS 引擎。
- Cosmopolitan：工具链唯一选型是指定 `jart/cosmopolitan` GitHub Actions Artifact；Run 的 `head_sha` 是源码构建关联，不得混入其他 Nightly/libc 版本。
- Redis：以 `ae` 的 file/time events / beforeSleep / afterSleep 思想实现自有 Reactor；不直接复制当前多许可证 unstable 的源码。
- txiki.js（MIT）：参考工程生命周期、Web API、Worker 和网络模型，不整体移植 libuv。
- LLRT（Apache-2.0）：借鉴 Node/Web API 机器可读兼容矩阵与差分测试。
- llamafile（Apache-2.0）：参考 ZIP64/STORE/原始文件对齐 mmap，维护输入安全边界。
- 加密/网络/压缩等第三方组件：最小静态闭包、license/SBOM、安全审计、独立 feature gate；Cosmocc 自带的 GCC/LLVM 等构建依赖也要纳入 provenance。
## 2. 产品定位、主要场景及非目标

### 2.1 三种对外使用模式

| 使用模式 | 用户入口 | 交付行为 |
|---|---|---|
| JavaScript Runtime | `qaplet run app.mjs` / `.cjs` / `.qjb` | 执行受支持的 ESM/CJS/Node/Web API，明确 unsupported 部分 |
| Portable App Builder | `qaplet pack` / `qaplet compile` | 把精选纯 JS 项目制成可离线运行的 QJB、fat APE 或带外置资源包的应用 |
| C Embed SDK | `libqaplet-embed.a` + `include/qaplet/` | C 应用嵌入 Runtime、注册受控 native module、事件交接和确定性关闭 |

应用构建可包含**通用 Native Host Template**：自定义程序入口与通用 Qaplet SDK 链接，业务 JS/QJB 仍通过标准 Runtime 和资源层加载。它是可复用的应用构建能力，不是为任何特定 OS 行业功能制作的专用 SDK。

### 2.2 首批目标工作负载

独立 CLI、交互 TUI、企业部署/诊断工具、MCP stdio/HTTP 应用、HTTP/SSE 服务、离线数据转换任务、精选纯 JS npm 命令行程序，以及用于验证 SDK 的最小 C Host 样例。主基准包括冷启动、RSS、CPU idle、TTY 与进程正确性、长连接稳定性、跨平台差分、QJB 资源按需读取、构建复现和单 APE 跨 OS 启动。

### 2.3 v1 非目标

完整 Node.js 100% API、动态任意 `.node` 原生插件、V8 Inspector/ABI、替代 npm/pnpm、未经测试的全部 CPU 架构、跨 OS 任意动态 DLL/so、生产 HTTP/3、绕过平台签名/安全策略、把 QJB 声称为沙箱、运行时自动升级、任意业务系统平台集成或应用运维管理工作流。项目本阶段仅交付**通用基础运行时/编译器/SDK**，不将具体业务应用纳入 v1 产品范围。

### 2.4 发行物身份与交付原则

| 制品 | 内容 | 发布验收 |
|---|---|---|
| `qaplet` fat APE | QuickJS、Host、Node/Web 子集、QJB/编译 CLI | 同一文件跨已声明 OS/CPU、真实 Node 差分、可审计依赖 |
| `app.qjb` | 应用 JS/CJS/ESM、manifest、资源 | 严格 ZIP/路径/索引验证、按需读取、无需整体解压 |
| `myapp.com` / `myapp.exe` | 独立应用 APE，平台允许时只改变文件名 | 同一 payload hash、离线运行、签名变体分列 |
| `qaplet-sdk` | C header、静态库、ABI 测试与样例 | 单独的 C Embed API/ABI 生命周期/线程合同测试 |
| `qaplet-tests` | Probe、Node Oracle、ZIP/fuzz 语料 | 运行证据、已认证平台矩阵和可重复复测 |

所有发行制品分别生成来源、lock、build-info、SBOM、license、checksums 和真实运行证据。平台签名/公证/assimilate 之后字节发生改变的变体单独标识，不能宣称和原始 fat APE 具有相同 SHA。

---

## 3. 架构决策记录（ADR v1 冻结候选）

| ADR | 决策 | 验证边界 |
|---|---|---|
| ADR-01 | **APE 同字节发行能力** | 以实际支持 OS/CPU 的相同构建产物为准，不用打包各 OS 二进制冒充 |
| ADR-02 | **Bellard QuickJS 锁具体 Git SHA** | 记录 Git Commit/Tree/VERSION/patchset，不用浮动 master 构建 |
| ADR-03 | **固定 GitHub Actions cosmocc Build** | 只使用 Run 41169443573 / Artifact 13031425078 的原始 ZIP digest，不自动跟随 Nightly |
| ADR-04 | **Cosmopolitan 为唯一通用 Host ABI 基础** | 平台差异由受审计 backend 处理，不引入第二套互相竞争的 native runtime |
| ADR-05 | **一 Runtime、一 owner JS Loop** | 其它线程禁止操作 JSContext/JSValue/GC，事件经 POD completion 投递 |
| ADR-06 | **Redis ae 调度哲学、小 Reactor** | 通用 `poll` 起步，验证后优化；不强行移植 libuv |
| ADR-07 | **有界 WorkPool 与取消合同** | 阻塞 FS/DNS/KDF 等离开 owner Loop，不创造每 socket 一线程模式 |
| ADR-08 | **Node 兼容分级 + 差分证据** | `verified` 只能由真实测试+锁定 Oracle 支持 |
| ADR-09 | **ESM/CJS 单一解析规则** | package exports/imports、QJB/FS provider/cache 路径事实源统一 |
| ADR-10 | **QJB v1 独立定义并安全解析** | 不承诺旧 QJB 字节码/容器向后兼容；不伪称权限沙箱 |
| ADR-11 | **Bundle 只读、状态写外部** | 不允许 ZIP mount 路径逃逸、隐式落盘 |
| ADR-12 | **大资源 STORE + aligned raw-file mmap** | 不将整包复制进 RAM，64KB 与 Windows >4GB 需要独立测试 |
| ADR-13 | **通用 C Embed SDK** | 版本化 ABI、模块注册、线程安全事件传递、生命周期，不露 QuickJS 裸堆作为稳定 ABI |
| ADR-14 | **通用 Native Host App Build** | 任意 C 宿主可与 SDK/JS/QJB 静态链接，至少一个 OS-neutral 示例真机验收 |
| ADR-15 | **单平台主线 + 跨平台早验证** | M0-A Linux 先行，M0-B/C 在 Host ABI 冻结前通过 |
| ADR-16 | **Fast/Cross/Full 分层 CI** | 每提交/PR/每日与 RC 真实验证，不得把 `not_run` 填成 `verified` |
| ADR-17 | **源码身份与编译器身份独立证明** | QuickJS Commit/Tree/VERSION、Run HEAD、Artifact SHA、提取组件 Manifest 分别核验 |
| ADR-18 | **`.cache` 是不可信、可重建加速层** | 本地/CI 恢复后校验 digest，Release 离线 clean rebuild |
| ADR-19 | **原始 Artifact 必须长期保存** | GitHub Artifact 存在有效期，不能依赖 actions/cache 永久可用 |
| ADR-20 | **归档解压后权限/链接恢复有界** | GitHub ZIP x-bit 不保留；受审计修复 + 编译功能 probe |
| ADR-21 | **默认安全不降级** | TLS 校验、路径/输入/ZIP 安全、供应链证明不可因性能便利跳过 |
| ADR-22 | **先可移植正确，再 OS 性能优化** | backend 复用同一行为合同，专用优化必须保留回归/安全门禁 |

### 3.1 关键边界

- **Engine** 负责 QuickJS Context、jobs、模块生命周期；**Host** 负责 Reactor/WorkPool/Process/TTY/Transport/VFS；**JS Node/Web Layer** 负责具体 API 语义；**SDK** 为外部 C Host 提供版本化嵌入合同。
- `app-compiler` 只对已声明可移植的 JS 依赖、资源和静态 native 链接作可验证打包；不能为不支持的 `.node` 插件提供未说明的动态下载 fallback。
- 项目公开 API 只包含已经具备真实实现和证据的能力；没有实装的接口不要放在默认类型定义与能力矩阵中。
- 同一 `toolchains/qaplet.lock.json` 必须决定本地、GitHub Actions 与 Release 的所有输入；不得从缓存位置和滚动下载地址暗中引入另一套依赖版本。

---

## 4. Qaplet 总体技术架构与运行数据流

```text
                      JS / CJS / ESM / QJB / Portable APE
                                      |
                       Node / Web Compatibility APIs
               Buffer | fs | process | stream | net | http | tty
                                      |
                           Qaplet Module Runtime
             Resolver | ESM | CJS | URL / VFS Provider | Cache
                                      |
     +--------------------------- Qaplet Host --------------------------+
     |                 JS owner thread / logical Loop                  |
     |  Scheduler / nextTick / Promise jobs / TimerHeap / Ref-Unref    |
     |                                                                  |
     |  Reactor <----> CompletionQueue <----> Bounded WorkPool          |
     |     |                ^                      |                    |
     |  Socket/TTY*        Wake               FS/DNS/KDF/Compress       |
     |     |                                                            |
     |  Transport: TCP/UDP/TLS --> HTTP1 / SSE / WebSocket             |
     |  Resource Manager: QJB / APE ZIP / mmap / security / cache      |
     |  Process Host: spawn / stdio / exit / signal                    |
     +------------------------------+-----------------------------------+
                                    |
                     Cosmopolitan libc / APE / ZipOS
                                    |
                      Linux / Windows / macOS / *BSD

     C Embed SDK  --> Context Factory / Native Module / Event Bridge
                 --> 同一个 Host / Loader / Resource Provider
```

`TTY*`：Windows Socket 与 Console/Pipe/HANDLE 的等待模型不必相同；Platform Adapter 统一输出 completion，禁止跨线程直接执行 JS callbacks，不能假定同一个 `poll()` 能等待所有 HANDLE。

### 4.1 线程与资源归属

| 实体 | 所有者 | 允许操作 | 禁止事项 |
|---|---|---|---|
| `QapletRuntime` | JS owner thread | Context、JSValue、Promise Jobs、GC | 非 owner 线程调用 `JS_*` |
| Reactor | Host Loop 或受控 I/O backend | socket/file readiness、waker、completion | 执行业务 JS 回调 |
| WorkPool | 有界 native workers | FS/DNS/KDF/压缩的阻塞调用 | 长期独占每个 socket 或访问 JS 堆 |
| QuickJS Worker | 独立 Runtime 与 owner thread | 本 Worker 的 JS 和结构化消息 | 共享其他 Runtime 的 JSContext/GC 堆 |
| C Embed Host | 调用方的主线程和自有原生工作线程 | 生命周期、注册模块、异步发布 POD 消息 | 绕过 SDK 线程/销毁时序 |
| Resource Manager | 资源索引/映射引用计数 | mmap/pread、受限解压、关闭 | 失效映射指针、越界读取或共享已释放数据 |

### 4.2 运行时生命周期

M1 的公开生命周期为 `OPEN / STOP_REQUESTED / STOPPED / FAILED / CLOSED`，执行阶段单独报告 `IDLE / RUNNING / PENDING / DONE`；普通 JS 执行失败的 DONE 不等于 Runtime FAILED。具体合同见[已确认设计](../../.scratch/qaplet-m1/design.md)。stop 接受后立即拒收普通新工作，owner 到检查点后停止新用户 JS 调度；已领取事件协作退出，未领取事件及剩余 jobs 在关闭时释放，不承诺执行完或运行 JS finally。第一次记录的终止原因保留。

owner 可在当前没有执行 JS/宿主回调时 close，包括尚为 PENDING 的执行；close 幂等清理并保留 CLOSED 控制句柄，destroy 只接受 CLOSED。最终释放前宿主负责撤下生产者并等待在途调用结束，SDK 不承诺识别释放后的旧指针。引擎清理期间宿主绑定仍须存活，native finalizer 不执行 JS。M2 承接 I/O 取消、迟到 completion 与完整调度，须使用 token/generation 和 Runtime lifetime 验证。`request_stop` 是**合作式停止请求**，不承诺抢占任意 native syscall 或固定停止/关闭延迟；M1 不公开可恢复的 request_interrupt。

---

## 5. 工程目录结构建议

将可信依赖身份留在已版本化的锁文件中，把所有下载、编译、临时探针放入 `.cache/`；不要求在 Git 仓库内提交大编译器 ZIP 或自动维护随 `master` 漂移的 vendor 快照。

```text
qaplet/
├── README.md
├── LICENSE
├── AGENTS.md
├── Makefile
├── .gitignore                       # /.cache/ /build/ /dist/
├── toolchains/
│   ├── qaplet.lock.json             # 源码 + 固定 Run/Artifact ZIP 权威输入
│   ├── lock.schema.json
│   ├── deps-status.sh
│   ├── deps-fetch.sh                 # 固定 SHA 的原件/不可变镜像
│   ├── deps-verify.sh                # archive/source/deep
│   ├── bootstrap.sh                  # 安全解压、权限/链接、组件 Manifest
│   ├── pin-quickjs.sh                # 人工批准的来源变更
│   ├── pin-cosmocc.sh                # 人工批准的 Run/Artifact 变更
│   ├── cache-prune.sh
│   └── provenance-report.sh
├── patches/{quickjs,cosmopolitan}/   # 审计并锁摘要，不直接改 upstream
├── vendor/third_party/              # 如需要，仅放可审计小依赖源码
├── .cache/                           # 本地或 CI 可重建缓存，不提交 Git
│   ├── downloads/cosmocc/sha256-<sha256>.zip
│   ├── vcs/quickjs.git/
│   ├── sources/quickjs/<commit>/
│   ├── toolchains/cosmocc/<sha256>/
│   ├── build/<host-target-profile-fingerprint>/
│   ├── probes/
│   └── tmp/
├── src/
│   ├── cli/                          # run / pack / compile / doctor / inspect
│   ├── engine/                       # QuickJS runtime/context/worker
│   ├── host/
│   │   ├── loop/
│   │   ├── reactor/
│   │   ├── workpool/
│   │   ├── process/                  # 通用 spawn/wait/pipe
│   │   ├── tty/
│   │   └── resource/                 # QJB/APE/ZipOS/mmap
│   ├── embed/                        # 通用 SDK，native module/event bridge
│   ├── bindings/
│   ├── transport/
│   ├── protocols/
│   ├── modules/{resolver,cjs,esm,json}/
│   └── js/{node,web,internal}/
├── include/qaplet/                  # 版本化 C Embed SDK 头文件
├── packages/{qjb-pack,app-compiler}/
├── probes/
│   ├── m0-a-linux-x64/
│   ├── m0-b-cross-host/
│   ├── m0-c-windows-io/
│   └── m0-c-arm64-64k/
├── tests/{foundation,native,sdk,contracts,ttys,bundles,apps,soak,portability}/
├── examples/{embed-c-host,cli,standalone-qjb,native-host-app}/
├── compat/{matrix.schema.json,matrix.json,test-evidence.json}
├── docs/{adr,specifications,engineering,security,delivery}/
├── test-evidence/                   # 验证结果导出，不依赖本地未上传文件
└── .github/workflows/
    ├── foundation-fast.yml
    ├── foundation-cross.yml
    ├── foundation-full.yml
    ├── deps-bump.yml
    └── release.yml
```

**依赖规则**：`Core -> QuickJS + Cosmocc`；`SDK -> Core`；`App Compiler -> Runtime/SDK + Resource Tooling`；`tests/examples -> Public Runtime/SDK`。Qaplet 自身的构建不需要任何未来业务应用的仓库源码。所有构建入口从同一个 `toolchains/qaplet.lock.json` 决定输入；`QAPLET_CACHE_DIR` 只改存储位置，不改依赖身份。

---

## 6. 从零实现 QuickJS Engine Host

### 6.1 QuickJS 主干接入策略

- 以 Bellard `master` **选型后锁定具体 40 位 Commit**；M0 参考为 `2833510432045f72915919cef680ba59e5153b16`。构建前实际校验 `git rev-parse HEAD`、Tree OID、`VERSION`（核验时为 `2026-06-04`）、干净工作树、patchset SHA；不使用旧 fork 的二进制。
- 将原版上游源码物化为 `.cache/sources/quickjs/<commit>` **只读**，编译期把已版本化的私有补丁叠到独立 build tree 并记录来源；禁止源码目录随当前 `master` 自动更新，禁止通过修改 `vendor/quickjs` 创建第二事实源。
- 原生 API：`JS_NewRuntime`、`JS_NewContext`、`JS_SetModuleLoaderFunc`、`JS_ExecutePendingJob`、`JS_SetMemoryLimit`、`JS_SetMaxStackSize`、`JS_SetInterruptHandler`、`JS_NewPromiseCapability`，以相应头文件当期实际签名验收；如主干有变更，先更新边界探针。
- Runtime Context 创建要经统一 factory，确保主入口、Worker、qjsc 风格独立 APE、SDK 的内建模块注册、权限、资源 provider、异常回传相同。
- 为模块加载实现 `normalize/load` 钩子；`qaplet:`/`qjb:`/`node:`/`file:` 等 Scheme 一律显式规则，不将任意字符串隐式交给 OS 访问。M1 只接受源码文件 ESM、显式注册的 `host:` 模块与已声明的 `qaplet:` 内建模块；文件采用规范绝对 URL、实例内固定缓存及失败记录，见 [ADR 0006](../adr/0006-m1-frozen-module-identity.md)，其余 Scheme 按阶段明确拒绝。
- `JSValue` 的 `dup/free` 由 owner thread 管理；native 资源使用 opaque 句柄和引用计数；强制上下文关闭后的异步任务不再访问 JS 内存。
- 运行时预算：内存上限（默认可调）、最大栈、脚本执行期限（单调时钟 execution deadline）、native handles、任务数、文件大小、计时器数量、Worker 数量，按对应阶段实现。M1 在引擎检查点与 owner 调度边界协作式检查期限，不作精确线程 CPU 计费或任意 native 调用抢占保证；parser/compiler、GC 和 native 清理也不承诺硬超时。同步 CPU 长循环必须有引擎中断机制；异步任务取消属于另一套显式 API。M1 的 Promise jobs 限额针对每轮执行量，公开 QuickJS API 不提供精确的待执行 jobs 条数上限。
- 每次 QuickJS Git Commit、patchset、字节码编译 flags 变更都必须废弃与其绑定的 qjsc/QJB bytecode/object 缓存，以内容指纹而非 `VERSION` 字符串划分。QuickJS 字节码不是跨引擎版本稳定格式，也不是可信输入格式。只允许同一已锁引擎构建/验证的受信任字节码；来自不可信网络或 npm 包时默认以源文本编译，或用强制验证的可信编译清单。
- ES2025/Temporal/Intl 能力依照当前锁定主干和 test262 结果声明；不得从“最新主干”推断所有提案都已经稳定实现。

### 6.2 SDK 与入口合同

| 入口 | 用户调用 | 验收要求 |
|---|---|---|
| 源码 | `qaplet run app.mjs` / `qaplet app.cjs` | argv/cwd/import.meta.main/exit code 正确 |
| QJB | `qaplet run app.qjb -- a b` | 无需解压、模块路径与资产可读、按 mount 生命周期关闭 |
| 独立 App APE | `./app.com -- a b` / `app.exe -- a b` | 无 CLI 选项冲突、载荷指纹一致、路径身份明确 |
| Worker | `new Worker(...)`（声明受支持子集） | Runtime 独立、错误/exit/transfer/terminate 明确 |
| SDK | `create/eval/run_entry/pump/close/destroy`（签名待实施规格） | 重复创建销毁、Opaque 不冲突、证据可复现 |

C Embed SDK 的拟议通用入口为 `qaplet_runtime_create`、`register_host_module`、`eval/run_entry`、`pump/get_result`、`post_event(POD)`、`request_stop`、`close/destroy` 和 POD 状态快照；资源 Provider、Log Sink 按对应阶段扩展，C 原型由后续实施规格落实。M1 用显式 pump 推进 PENDING 入口和事件，不忙等或启动隐藏线程；close 与 destroy 分开，不公开可恢复的 request_interrupt。post_event、request_stop 与 POD 快照可跨线程调用，其余实例操作在 owner 执行；回调重入受限。宿主模块首版使用有限标量/UTF-8/字节桥，稳定 ABI 不公开裸 `JSContext*` 或 `JSValue`。ABI 主版本为 1，结构使用 `struct_size/abi_version`，错误诊断归调用方；M2 的 OS waker、取消及迟到 completion 合同仍由 M0-C/M2 的真实 ABI 探针约束。

### 6.3 Node vs Web API 层的实现原则

**JS 优先，C 辅助**：例如 `node:fs` 的 callback/Promise API、错误归一化、选项处理留给 JS；native 仅提供 `open/read/write/stat` 等可验证原语。网络对象、背压、HTTP Agent、TLS 包装等同理。该分层减少跨平台 C 代码量，提高 Node 差分改进的速度。

---

## 7. Host Kernel：Redis ae 哲学的事件模型

### 7.1 “一个 loop 就是一个 loop”的精确定义

Loop 负责时序、调度、存活和退出，不负责 HTTP/FS/DNS 业务语义。Reactor 只报告 OS fd/handle 的 readiness；TimerHeap 维护单调时钟 deadline；WorkerPool 处理阻塞任务；CompletionQueue 统一把完成事件交给 Loop。不能以给所有 API 包 Promise 的方式掩盖实际阻塞。

```text
                    QapletLoop（每个 JSRuntime 唯一逻辑调度中心）
        +-------------+----------------+--------------+-------------+
        |  JS Jobs    |  Ready Queue   |  Timer Heap  |  Ref/Exit   |
        +------+------+--------+-------+------+-------+-------------+
               |               |              |
          JS owner        CompletionQueue     next_deadline
                               ^                |
                         Wake/Channel          v
                              / \           Reactor.poll(timeout)
                         I/O ready /\ Worker done
                                   /            \
                              socket/TTY*     WorkPool
                                             FS/DNS/KDF
```

**关键接口（C 伪合同）**：

```c
// Handles are generation-tagged; no raw JS callbacks in reactor records.
typedef struct QapletLoop QapletLoop;
typedef struct QapletReactor QapletReactor;
typedef struct QapletWorkItem QapletWorkItem;

typedef enum {
    QAPLET_IO_READ  = 1,
    QAPLET_IO_WRITE = 2,
    QAPLET_IO_ERR   = 4
} QapletIoMask;

int  qaplet_loop_run(QapletLoop *loop);
int  qaplet_loop_run_once(QapletLoop *loop, int64_t max_wait_ms);
void qaplet_loop_request_stop(QapletLoop *loop);
void qaplet_loop_wake(QapletLoop *loop);

int qaplet_reactor_watch(QapletReactor *, int fd, uint32_t mask, uint64_t token);
int qaplet_reactor_modify(QapletReactor *, uint64_t token, uint32_t mask);
int qaplet_reactor_unwatch(QapletReactor *, uint64_t token);

uint64_t qaplet_timer_add(QapletLoop *, uint64_t deadline_ns, uint64_t repeat_ns);
int      qaplet_timer_cancel(QapletLoop *, uint64_t timer_id);
int      qaplet_work_submit(QapletLoop *, QapletWorkItem *);
int      qaplet_work_cancel(QapletLoop *, uint64_t work_id);
```

`token` 必须包含 slot + generation，防止 fd 被关闭后由 OS 复用，同一 fd 上错误的迟到事件被交给新对象。watch/remove/close 要有顺序、幂等和回调竞态测试。

### 7.2 运行步骤（语义伪码）

```c
while (!host->stopping) {
    drain_next_tick_queue_with_budget(host);
    run_quickjs_pending_jobs_with_budget(host);
    dispatch_completions_with_budget(host);
    fire_due_monotonic_timers_with_budget(host);
    run_safe_checkpoints(host);

    if (host_can_exit(host)) {
        if (!emit_before_exit_and_recheck(host))
            break;
    }

    int timeout_ms = compute_min_timeout(host);  // nearest timer / stop / max idle
    reactor_wait_or_wake(host, timeout_ms);       // never hold JS heap locks here
}
close_all_resources_in_order(host);
```

该伪码**不是** Node 事件循环阶段的逐步复制。`process.nextTick`、Promise microtasks、timer、I/O callback、`setImmediate` 的时序以锁定 Node Oracle 的差分结果确定；为防止无限 nextTick/microtask 导致 I/O 饥饿，设执行预算并保留可审计的行为差异，不得无说明改变 Promise 微任务的 ECMAScript 语义。

### 7.3 Reactor backend 选择

- **P0：Cosmopolitan `poll()`**，优先覆盖所有目标 OS；启动时验证 socket readiness、关闭、错误、HUP、非阻塞 connect、半关闭和 wake。禁用固定 `poll(..., 10ms)` 空转。
- **P1：Linux epoll**：通过 Cosmo 可用 syscall / wrapper 运行时选择，与 `poll` 同样的状态机；没有并发基准优势时不默认启用。
- **P1：Darwin/BSD kqueue**：作为可选优化；同样要求运行时探测和负向用例。
- **Windows**：socket 优先依托 Cosmo 现有 poll/WSAPoll 路径验证；Console、子进程 HANDLE、pipe 可能需要专门等待/线程桥，不能假定所有 OS fd 与 Winsock socket 混合 `poll()`；IOCP/wepoll 仅在具体性能瓶颈及 APE 可用性已证明后评估。
- 不准把 libuv/libevent/epoll backend 变成第二份从业务层可见的宿主 ABI。所有 backend 输出统一 `QapletIoCompletion`。

**首选线程形态**：一 Runtime 一个 JS owner thread；初期优先验证同线程 Reactor 轮询的可行性，再按 Windows console/pipe 的实际限制决定是否专用 reactor thread。无论线程配置如何，一个连接绝不独占一个 OS thread。

### 7.4 Timer 与 Ref/Unref

- 二叉最小堆，单调时钟，O(log N) 增删，懒删除要有上限以防堆膨胀；ID + generation 防复用错误。
- `setTimeout`、`setInterval`、`setImmediate`、取消、`ref/unref`、`beforeExit` 与 `exit` 都要形成独立的存活合同。
- 在睡眠前计算最近 timer/deadline；调用系统等待而不是忙循环。系统时间回拨不影响单调计时；时钟跳跃与 suspend/resume 后要有恢复规则。
- 长时间运行时避免无限排队：每 tick 处理有界 jobs/completions，单连接写入高水位触发背压，WorkerPool 队列上限可配置，超限返回结构化 `ERR_RESOURCE_LIMIT`。

### 7.5 WorkerPool / Blocking bridge

- 负责 regular file I/O、`getaddrinfo()`、PBKDF2/KDF、大块压缩、可阻塞的证书/图片处理等；不把 TCP/TLS I/O 长期放在一线程一连接模型。
- 初始默认 2～4 条线程，允许受限配置；有界 `pending_count`/`pending_bytes`/并发控制，任务拥有取消和上下文销毁合同。
- 非 owner 线程只处理 POD 数据、原生 byte buffer、错误码；完成后通过线程安全 queue + `wake()` 让 owner thread 创建/销毁 JSValue，解析 Promise。
- 取消区分 `queued`（可直接取消）、`running`（请求取消但 OS syscall 可能不可中断）、`completed`（拒绝重复完成）。所有任务至多一次完成回调。

### 7.6 Transport、TLS、HTTP 与 FS 的归属边界

- `TCP/UDP/pipe` 归 native transport；`readable/writable` 推进状态机。
- `TLS` 归非阻塞 transport codec，`WANT_READ/WANT_WRITE` 调整 Reactor watch；握手、关闭、重连状态不得创建额外独占长寿命 socket thread。
- `HTTP/1.1`、WebSocket、SSE 都是 transport 消费者，HTTP parser/serializer 不直接调用 OS 等待。
- `FS` 通过同步 `open/read/write` 或异步 WorkPool，普通磁盘文件不强塞进 epoll readiness。
- `DNS lookup` P0 WorkPool + getaddrinfo；P2 可选 c-ares 通过 UDP fd 与 timer 接入同一个 Reactor。
- `child_process`：spawn/wait/stdio/EOF 结果进入 CompletionQueue；不要把 exec 创建逻辑写入 QapletLoop。

### 7.7 Qaplet 运行时可观测性

内核原生维护：`loop_tick`、`lag_ms_p50/p95/p99`、`ready_depth`、`completion_depth`、`worker_active/queued`、`open_handles`、`timer_count`、`socket_count`、`bytes_in/out`、`spurious_wakeups`、`dropped_events`、`gc_count/pause_ms`、`memory_rss/virtual`（受 OS 支持限制）。CLI `qaplet doctor` 与 `qaplet inspect --runtime` 输出 JSON，不依赖 HTTP 管理面；外部 C Host 通过通用 metrics API 获取指标，并自行接入诊断系统；Kernel 只报告运行时指标。生产默认不输出秘钥/请求 body/环境变量全量值。

---

## 8. Node / Web API 能力矩阵（参考 LLRT）

### 8.1 能力声明模型

所有 API 的支持必须有**机器可读事实源** `compat/matrix.json`，字段含 `api`、`profile`、`support` (`verified|partial|experimental|unsupported`)、`since`、`source_tests`、`node_oracle`、`platforms`、`known_differences`、`security_notes`、`owner`、`evidence_sha256`。不能先标 `verified` 再补测试；TypeScript 声明和 `qaplet capabilities --json` 必须从同一事实源产生或校验。

**Node Oracle**：首版以真实的 Node.js `v24.21.0` 已声明合同为基准；通过独立工具锁定其下载包 SHA，不凭经验猜测差异。未来提升基线采用显式 major/minor 合同迁移。要求 Callback/Promise、异常类型、`code/errno/syscall/path`、事件顺序、Buffer 边界、stdio 数据行为、Path/UNC 语义等有差分用例。

### 8.2 v1 模块矩阵（“目标状态”，不是当前实现声明）

| 模块/能力 | v1 目标 | 优先级 | 实现建议与验收说明 |
|---|---|---|---|
| ESM、动态 import、import.meta、TLA | verified | P0 | QuickJS 原生 ESM + 宿主 resolver，URL identity 正确 |
| CJS、require/module.exports/cache | verified 子集 | P0 | Module wrapper、循环依赖、`require.resolve`、JSON CJS |
| `node:module` / `exports/imports` | partial→verified 子集 | P0 | package.json、Node 条件导出、node_modules 查找 |
| `node:buffer` | verified 子集 | P0 | Uint8Array 关系、编码、slice/copy/int/float 读写 |
| `node:process` / `node:os` | verified 子集 | P0 | argv/env/cwd/exitCode/nextTick/platform/arch |
| `node:fs`, `node:fs/promises` | verified 核心集 | P0 | 文件/目录、stat/lstat/junction/UNC/stream、异步非阻塞 |
| `node:path`、`node:url` | verified 核心集 | P0 | win32/posix、file URL、跨 OS 保持 Node 语义 |
| `node:events`、`node:stream` | verified 核心集 | P0 | backpressure、pipeline、destroy、AsyncIterator |
| `node:timers`, `/promises` | verified 核心集 | P0 | event order、ref/unref、AbortSignal |
| `node:tty`, `node:readline`, `/promises` | verified TUI 核心集 | P0 | raw、resize、Unicode、keypress、PTY/ConPTY |
| `node:child_process` | verified 核心集 | P0 | spawn/execFile、shell=false、exit/signal/stdout/stderr；`fork/IPC` P2 |
| `node:dns`, `/promises` | verified lookup 子集 | P0 | getaddrinfo v4/v6 与错误；`resolve*` 后续 |
| `node:net` | verified 客户端+服务端子集 | P1 | TCP connect/listen、backpressure、half-close、timeout |
| `node:tls` | verified 安全客户端子集 | P1 | CA、SNI、hostname、mTLS 分级、TLS1.3 专题 |
| `node:http`, `node:https` | verified HTTP/1.1 子集 | P1 | client/server、chunked、Agent、SSE、Abort |
| 全局 `fetch`、Request/Response/Headers、URL | partial→verified | P1 | WPT subset、stream 取消、redirect、代理策略 |
| WebSocket | verified Client；Server 分级 | P1 | RFC6455、fragmentation、control frames、升级、close |
| `node:crypto` / Web Crypto | verified 精选算法 | P1 | AES-GCM、SHA/HMAC、KDF、Ed25519，正式安全审计 |
| `node:zlib`、压缩扩展 | verified 基础 | P1 | gzip/deflate，zstd/lz4 可选 Full profile |
| `worker_threads` | experimental/unsupported | P2 | 不把 QuickJS Worker 假充 Node worker_threads |
| SQLite、WASM/WAMR | experimental | P2 | 作为可选 profile，严格体积/安全预算 |
| HTTP/2、ALPN、TLS1.3 | planned | P2 | 先安全 backend，再 nghttp2；不阻断 v1.0 |
| HTTP/3 / QUIC、Inspector、完整 V8 API | unsupported | Future | 不公开假承诺 |
| native `.node`、任意 `dlopen` | unsupported | Future | 单 APE/跨 OS ABI 不一致，不做隐式解压执行 |

### 8.3 核心设计：Node 模块解析器

文件提供器和 package resolver 共享一个模块身份规则：

1. 识别 `node:` 内建、`qaplet:` 特有、相对/绝对文件、`file:` URL、裸包名、`#imports`、QJB 虚拟路径。
2. 按上下文和 `package.json` 的 `type` 决定 `.js` 是 CJS/ESM；`.cjs`、`.mjs` 明确，JSON 和 package exports 单独处理。
3. 按 Node 约定解析 `exports`/`imports`/`main`（冲突和自引用要测试），查找 `node_modules`、`node`/`import`/`require` 条件，但不虚构不支持的 Node loader hook。
4. 正确处理循环依赖：CJS module 对象先入缓存再运行；ESM module record 链接和求值按 QuickJS 语义；错误时缓存淘汰规则差分测试。
5. 包内映射不得退回不受控宿主 cwd，也不得允许 `..` 跨 mount；host `require` 与 bundle `require` 指向显式的 provider。
6. 内建模块 C/JS export 名称、default/named export、模块 identity 和 ES/CJS interop 通过 Node Oracle，而不是名字相同就算支持。

CJS 核心伪形态：

```js
function executeCommonJS(source, filename, module, localRequire) {
  // 仅示意：生产实现还要有 scope、cache、source-map、循环依赖与异常处理。
  const wrapped = compileCommonJSWrapper(source, filename);
  wrapped(module.exports, localRequire, module, filename, dirname(filename));
  return module.exports;
}
```

**禁止**把单个全局 `require` 注入所有 ESM realm、禁止以“把 CJS 文本替换成 import”作为通用方案、禁止从远程 npm 下载任意原生可执行资源后静默启动。

### 8.4 TUI 兼容标杆

- **Clack**：P0。检查 `readline.createInterface({terminal:true,output:undefined})`、`rl.terminal` 可变行为、keypress/resize/raw 回收、Unicode 宽度；原版 ESM 依赖或锁定的离线 bundle 必须可运行。
- **Terminal-Kit**：P1。着重 `require.cache`、动态 `require('./termconfig/' + name)`、JSON、Buffer BE/int、ScreenBuffer、TTY/stdin/`execSync`/SIGWINCH。
- **Ink**：P2 技术预研。React/Reconciler 和 Yoga WASM 是额外阻断；考虑通过可选 WAMR 运行上游 yoga-layout，或原生 Yoga+一致 API 适配；不为 Ink 把整个 v1 变成 WebAssembly 优先运行时。
- 终端测试至少覆盖 ASCII/中文/emoji/宽字形、两列布局、颜色、光标、Alt Screen、输入取消、resize、异常/信号退出后完整恢复。

---

## 9. 网络与安全协议方案

### 9.1 传输抽象

`QapletSocket` 管理 `fd`、状态、read/write ring buffer、高低水位、error、read_eof/write_eof、引用计数、超时/取消。`QapletTlsTransport` 在其上运行非阻塞 TLS，暴露相同的 `read/write/flush/shutdown`。任何连接不得新建独占式长期 polling pthread；需要 backpressure 和公平的每轮 byte/callback budget。

### 9.2 HTTP/Fetch/WebSocket

- v1.0 最小网络闭环：DNS、TCP、TLS 安全客户端、HTTP/1.1 request/response、SSE、HTTP Server（限定的 Node 风格 API）、超时/取消/重定向的声明子集。
- HTTP parser 选定维护良好的小型 C parser 或在已有 Cosmopolitan parser 上构建，先做 differential fuzz，不自研不必要的 HPACK/HTTP2 framing。
- WebSocket 可集成纯协议层 `wslay`（MIT），由自己完成握手、TCP/TLS、Origin/证书/代理策略、frame feed、ping/pong/close 和背压。txiki.js 使用 libwebsockets 及自有 event loop 集成作为架构参考，并不强制引入完整 libwebsockets。
- HTTP/2 后续独立工作包使用 `nghttp2`，先实现 ALPN、流控、HPACK、取消和服务端证据；HTTP/3/QUIC 不进入 v1.0 发布门禁。

### 9.3 TLS 安全决策

Cosmopolitan 当前源码的 `third_party/mbedtls/README.cosmo` 仍标识**经大量定制的 Mbed TLS 2.26.0**，不能因为换成 Nightly 就宣称自动获得 TLS 1.3 或所有上游 CVE 修复。

- TLS v1.0 第一安全基线：强制可信 CA/证书链/hostname/SNI 校验、时间验证、弱算法拒绝；禁止“开发用途 insecureSkipVerify”成为默认；私人 CA 和可信根更新均可审计。
- 单独建立 `tls-provider` 私有静态闭包：可在经补丁和审计的 Cosmo TLS1.2 基线和更新的现代 TLS 库之间进行**唯一后端**评估；禁止链接两个不受控的 `mbedtls_*` 符号集。
- TLS1.3 是独立里程碑：Mbed TLS 4.x/TF-PSA-Crypto 的 Cosmopolitan 适配，或其他候选需通过 X509、SNI、ALPN、安全公告、互操作和 ABI 验收后才启用。
- 建立进程共享只读 CA trust store；连接不得默认每个分配巨大固定 arena。内存峰值以并发实测作为准入，不以“连接数限制改大”冒充扩展能力。
- 建立定期安全公告检查的显式升级工作流；SSL 失败绝不自动回退明文 HTTP。

---

## 10. QJB：应用格式、虚拟模块系统与生命周期

### 10.1 从零定义 QJB v1

QJB 保留 `*.qjb` 后缀和“便携 JavaScript 应用包”的含义，但新建完整格式规范、错误码、测试和迁移工具；**不与历史 quickjs-cosmo QJB v1 默认二进制兼容**。初版目的：在同一个运行时内执行受信任的 ESM/CJS/JSON 依赖图，无需先解压到磁盘，并能引用资源。

建议容器采用可审计 ZIP 子集（STORE/DEFLATE、标准 UTF-8 名、确定性排序），配置 manifest 固定路径 `.qaplet/app.json`；避免允许符号链接、特殊文件、绝对路径、路径逃逸、文件名冲突和 ZIP 多盘。文件/中央目录/本地头一致性及 CRC 均需验证。**CRC 检查完整性损坏，不是安全真实性证明**。

```text
app.qjb                       ZIP 数据容器
├── .qaplet/
│   ├── app.json               入口/权限/能力契约/包元数据
│   ├── modules.idx            规范依赖映射（可选）
│   ├── resources.json         成员大小、sha256、compression、用途
│   └── build-info.json        工具链/编译器/Node Oracle/时间戳策略
├── app/main.mjs
├── app/package.json
├── app/node_modules/...
└── app/assets/...
```

**manifest v1 草案**：

```json
{
  "schema": "qaplet.app.v1",
  "name": "sample-agent",
  "version": "1.0.0",
  "entry": "app/main.mjs",
  "moduleType": "module",
  "engine": "quickjs",
  "nodeCompatibility": "node24-subset",
  "requirements": {
    "capabilities": ["node:fs", "node:process", "node:net"],
    "platforms": ["linux-x64", "windows-x64", "darwin-arm64"]
  },
  "assets": { "root": "app/assets" },
  "integrity": { "manifestVersion": 1, "digestAlgorithm": "sha256" }
}
```

`requirements.capabilities` 表示构建期最低运行能力，**不是自动授予的沙箱权限**。不允许 app manifest 以配置文件方式关闭 TLS 验证或绕过安全准入。

### 10.2 QJB 加载流程

1. 打开 package，读取大小及 ZIP 目录，验证格式、成员数、尺寸、文件名和碰撞、entry 合法性与风险上限；**不得先整个包读入匿名内存**。
2. 建立 mmap/按需 pread 视图及紧凑索引；只有被访问的成员才读入/解压；对压缩成员使用带配额的 cache（LRU/引用计数），对 STORE 成员优先直接借用映射内容。
3. 解析 `.qaplet/app.json`，校验内容清单（可按信任等级采取启动关键成员预验证 + 其余成员惰性校验；签名验证另行要求）；不把 `CRC32` 当可信签名。
4. 建立只读虚拟根，主模块 URL 使用 `qjb://<mount-id>/app/main.mjs`；外部 QJB 中 `import.meta.url`/相对资源 API 一致；`require()`、ESM、Worker 共享同一 mount 目录索引，不共享 JSRuntime GC/模块对象。
5. 资源读取为 `read/stream/map` 三种不同内存语义；只有 `read` 才必须返回新的 JS Buffer；外部持有者关闭时映射引用计数回收，悬空 view 必须检测。
6. 应用 `fs` 必须支持已声明的只读 QJB 路径提供器，同时保留宿主真实文件路径；写操作若指向只读 mount 返回 `EROFS`，不能悄悄落到 cwd。

**包内路径规则**：规范化 UTF-8、拒绝绝对根逃逸、`..` 越界、NUL、不同编码大小写别名、非法 ZIP extra、重叠成员、ZIP bomb、畸形 ZIP64、重复记录、权限位冲突。Windows 允许的 UNC/驱动器宿主路径不与 `qjb://` 混淆。`qaplet://app/...` 保留给内嵌 APE 应用的稳定逻辑 scheme，避免将 `/zip/...` 等主机路径直接泄漏给 JS 业务。

### 10.3 QJB 配额分层

- **P0 默认安全配额**：建议最多 8,192 成员、单文本/JS 模块 16 MiB、全部可解压脚本/配置等不超过 256 MiB、单次解压内存 32 MiB、mount 数量默认 16（均需实现前以资源测试调整，非已实现数据）。超限明确 `ERR_QJB_LIMIT`。
- **大型资源**：不纳入“全部解压 JS 模块”的配额，而进入 `QapletLargeAsset` 策略，要求 STORE + 对齐 + 受控 mapping；内存映射本身也有每进程/每应用配额。
- **ZIP64**：作为大资源专题开启。先验证当前 Cosmopolitan ZIP 解析路径对 >4GB 索引与 EoCD 的行为，必要时实现 Qaplet 私有 ZIP64 parser；不因 ZIP64 字段存在就宣称 Windows >4GB `.exe` 可运行。
- **压缩成员缓存**：全局/每应用双预算（bytes + entries），LRU 淘汰必须考虑 JS 借用视图的生存期，避免把 cache 中内存指针交给 JS 后立即释放。
- **格式升级**：新 major 格式应有显式 `schema` 版本，不通过魔法值碰运气读取旧格式。

### 10.4 QJB 构建与升级

- `qaplet pack ./app -o app.qjb`：确定性目录扫描、依赖解析、源文件/资源 manifest、时间戳归一化、版权/许可证收集、原子输出与中断清理。
- `qaplet inspect app.qjb --json`：展示入口、依赖、资源大小、签名/完整性状态、目标 Node 能力、真实解压峰值预算。
- `qaplet verify app.qjb`：校验 ZIP headers、可达依赖、SHA-256、签名/可信根（如配置）、恶意嵌套路径与无效配置。
- `qaplet migrate-qjb <old>`：未来可选；不阻断 v1，全新格式要有误用旧文件时的清晰错误。

---

## 11. 从精选 Node 项目编译出单一 APE 应用

### 11.1 正式产品命令

```bash
qaplet --version
qaplet run app.mjs -- --help
qaplet run app.cjs -- --help
qaplet run ./package-dir -- --help
qaplet run app.qjb -- --help
qaplet pack ./my-app -o my-app.qjb
qaplet compile ./my-app --output my-app.com --profile base
qaplet inspect my-app.com --json
qaplet verify my-app.com
qaplet capabilities --json
qaplet doctor --json
```

以上是**拟议目标 CLI**，不宣称全新 Qaplet 仓库已经实现。允许 `qaplet file.js` 作为 `run` 简写，但必须保持 Runtime 参数与应用参数的边界。通用 Native Host App 的构建接口见 §11.5。

### 11.2 `compile` 工作流与阻断条件

```text
输入 Node 工程
   ↓
读取 package.json、锁文件、入口和依赖图
   ↓
Node resolver 判定 ESM/CJS、内建模块、JSON、资源
   ↓
Node API 能力矩阵校验 + platform availability
   ↓
禁止项检查（.node / native DSO / spawn 外部缺失程序 / 动态无法求解路径）
   ↓
构建 QJB 模块树（可选 minify，必须保持模块语义）
   ↓
用同一锁定 Nightly 工具链产生 Qaplet fat APE
   ↓
将应用内容写入 APE 的 ZIP 区域并生成一致索引
   ↓
复验 APE Loader + ZIP + manifest + 完整性 + 跨 host 实测
   ↓
输出 one-file app + capability report + provenance + checksums
```

对于动态 `require(someVariable)`、动态 glob、读取项目之外资源、运行期下载依赖、含 native addon 的 npm 包，使用显式 denylist/allowlist 与构建错误；可以声明 `--include` 白名单补充资源，但不能在本来缺失的情况下假报完整兼容。禁止把 `eval` 使用动态路径和程序执行行为隐式等价为静态可分析依赖图。

### 11.3 资源层应避免“ZIP 套 ZIP 全量读取”

**目标内嵌结构**：

```text
myapp.com / myapp.exe  （同一个 APE payload，可按平台重命名）
├── APE loader + native code slices
│   ├── QuickJS
│   ├── Qaplet Host Kernel
│   ├── Node API + Transport
│   └── 通用 C Host/SDK 构建支持（按 profile 选择）
└── APE ZIP
    ├── .qaplet/runtime.json
    ├── .qaplet/application.json
    ├── app/main.mjs
    ├── app/node_modules/...  # 精选纯 JS 依赖
    └── app/assets/...        # STORE/DEFLATE 按资源使用分开
```

`compile` 直接把可执行应用模块置于 APE ZIP 中，而不是再在 ZIP 里嵌套一个整体 `app.qjb` 并在运行时完整读取。仍然可以从同一 `QapletVfs` interface 调用已存在的 `QjbReader`/`ZipApeReader`，只要底层索引能复用而不复制整个归档。

### 11.4 单文件与平台安全签名是两个独立门禁

- 生成 APE 后的 ZIP/签名/PE 检验必须有固定处理顺序；不能假设 append 文件后 Windows Authenticode、macOS 公证、ZipOS EOCD 仍有效。**先做签名后可启动的实际 PoC，再承诺企业版单文件签名。**
- 构建输出可以包括可验证的离线附属 `manifest.json`、`.sig`、`.sha256`，不会改变“单一主执行文件”技术能力，但完全离线只交付主文件时的信任根必须有明确方案。
- 不同 OS 可能限制同一 APE 的直接运行：Windows 可执行文件体积、杀软/SmartScreen、macOS 签名和 Gatekeeper、Linux binfmt_misc 与 MZ 误判均需版本化证据；不保证绕开政策限制。

### 11.5 通用 Native Host Template（不依赖具体业务应用）

v1 先证明 `cosmocc + libqaplet-embed.a + 业务 JS/QJB + native/*.c` 可以链接出独立可执行 APE；随后规划 `qaplet compile <app> --host native/host.json`，作为任何原生入口应用都能使用的通用构建能力。不能为了某一类程序引入硬编码模块注册或第二套构建器。

拟议构建描述示例（仅为计划中的 schema，需 M6 正式冻结）：

```json
{
  "schema": "qaplet.native-host.v1",
  "entry": "native/main.c",
  "sources": ["native/main.c", "native/module.c"],
  "moduleSpecifiers": ["example:native"],
  "sdkAbi": 1,
  "linkToolchain": "locked-cosmocc"
}
```

验收应至少覆盖一个完全通用的 C Host 示例：创建 Context、注册原生模块、从 QJB 加载 ESM、异步发送消息、正确关闭资源，且**同一个 APE 字节文件**能在已声明 OS/CPU 上执行。构建器拒绝非受控额外链接参数与隐式 `dlopen`，把所有 native 源码/静态库/flags 的摘要加入 Build Evidence。平台特有扩展只能作为应用的显式额外输入，不得污染所有默认 Runtime。

---

## 12. llamafile 式大型资源与低内存加载

### 12.1 真实问题与目标

APE 几百 MB / 数 GB 不代表内存启动时同量增长。Cosmopolitan `ZipOS` 在初始化时可 `mmap` 整个可执行文件并建立 ZIP 索引；虚拟映射大小不是 RSS。未压缩 STORE 成员可直接引用映射区域；DEFLATE 成员在 `open()` 时可能完整解压该文件。**若对 `/zip/foo` 使用一般 `mmap`，Cosmo 虚拟文件层可能创建匿名映射并复制数据，不能保证零拷贝。**

llamafile 的做法是从原始可执行文件解析 ZIP 条目，定位未压缩 GGUF 数据的真实偏移，按操作系统 granularity 调整后对**原始文件 fd** 执行 `MAP_SHARED` mmap。`zipalign` 支持按给定边界对齐 STORE 成员，默认建议 64 KiB 对齐。

### 12.2 `QapletLargeAsset` API 草案

```c
// Source is an already validated APE file and verified ZIP entry.
typedef struct QapletMappedAsset QapletMappedAsset;
int  qaplet_asset_open(const char *bundle_id, const char *logical_path,
                       const QapletAssetPolicy *policy, QapletMappedAsset **out);
int  qaplet_asset_map(QapletMappedAsset *asset, uint64_t offset, size_t length,
                      QapletMappedSlice *out);
void qaplet_asset_unmap(QapletMappedSlice *view);
void qaplet_asset_close(QapletMappedAsset *asset);
```

- 使用严格 ZIP64/头/成员边界验证求得 `payload_offset`，检查所有 `(offset + length)` 的 64 位溢出、对齐后范围、真实文件大小、条目压缩方法、成员重复和重叠。
- `mmap` 在 Windows 区分 `page size` 与 `allocation granularity`，统一采用 `sysconf(_SC_GRANSIZE)` 决定 mmap offset 的对齐；实际返回 `base + skew`。
- 只读映射时不允许 JS 获得可写且可越界的内存指针；Native 映射对象的生命周期与所有引用它的 TypedArray/JS buffer/Worker lease 绑定；如不能可靠保持引用关系，则向 JS 提供显式 `read(range)` 安全拷贝 API，而不是伪零拷贝。
- CPU/GPU 访问统一记录 mapping/RSS/page fault/IO throughput；未来 GPU backend 如需设备缓冲区不承诺零复制。
- STORE 大资源可使用分段 mmap（例如每次几十至几百 MB 的窗口，按访问模式调整），避免一次申请极大连续用户虚拟地址；支持 `pread` 只读流式 fallback。
- 低频 JS/text/JSON 适度 DEFLATE；频繁随机访问、大型模型/索引/数据库建议 STORE/分块资源；不得对单个 3 GB DEFLATE 成员执行全量解压造成 OOM。

### 12.3 包体积与跨平台限制

- **Windows 超 4 GB PE/APE**：llamafile 官方记录 Windows 4 GB executable size 限制，跨 Windows 的单 APE **不承诺 >4GB**；应切换 `qaplet.exe + assets.qap` 或统一旁路大资源包模式。
- ZIP64 描述资源能力与 Windows PE 可执行能力不同；必须双门禁验证，不允许通过调大 ZIP64 索引间接越过 OS 限制。
- 资源包可带多款模型，但不能保证全部在启动时都进入内存，也不能保证推理时 RSS 很低；工作集由实际访问决定。
- 需验证大量资源条目时 ZIP 目录扫描/排序占用、ZipOS 初次建立索引延迟及 Windows 杀毒扫描放大成本，必要时引入预排序的 `QapletAssetIndex` 并记录与 ZIP 一致性校验。

### 12.4 `assets.qap` 外部标准资源包

对于 `>= 4 GB`、Windows 可执行大小接近上限、需要单独升级模型或只读索引的场景，使用：

```text
qaplet.exe           # 同一代码 APE，跨 OS
assets.qap           # 同一数据 ZIP64，跨 OS
```

资源包可流式 pread/mmap，保留版本、SHA-256/签名 manifest；应用启动时按包身份检查，缺失时明确报错；不隐式访问公网。资源随应用更新而非强制替换 Qaplet binary。

### 12.5 大资源测试计划

0 B、1 B、4 KiB、64 KiB、1 MiB、100 MiB、512 MiB、1 GiB、接近 4 GiB 及超过 4 GiB 数据包；STORE/DEFLATE、ZIP32/ZIP64、非法 size、重复条目、CRC/签名不符、字节截断、数据区与 CD 重叠、故意 64K 不对齐、Windows `MapViewOfFile`、ARM64 64KB 页内核、低内存/大范围随机访问、高并发 view 关闭。MVP 应用数据大于 100 MiB 时 RSS 上限按实测和缓存配置定义，不能预设“RSS 永远不增长”。

---

## 13. 开发工具链、`.cache`、GitHub CI 与构建证据

### 13.1 单平台主线开发，跨平台验证逐层进入

- **主力开发**：Linux x86-64（可用 Linux/WSL2），主分支集中构建、可复现调试、快速迭代；不用让所有平台同时完成相同功能才允许提交。
- **第一道基线**：`cosmocc` 使用已固定 digest 的工具链，在 Linux x64 生成最小 APE 并执行 QuickJS ESM/Promise/纯 C Host 探针（SDK 前置），这就是 **M0-A**。
- **第二道架构基线**：使用**同一字节的 fat APE** 验证 Linux、Windows、macOS 真实宿主（`M0-B`）；至少将 Linux ARM64 4KB/64KB 纳入完整基础验证，并检查某个所谓“可运行”是系统直接启动还是经过 helper/emulation。
- **第三道风险基线**：在通用 Host ABI 冻结前完成 Windows SOCKET/HANDLE/pipe/TTY/process 与 ARM64 64KB loader/pthread/mmap/ZipOS 基础探针（`M0-C`）。M1 的 ESM/模块加载可与 M0-B/C 并行，但 Reactor/Process/SDK OS 语义不可提前宣布冻结。
- **后续功能**：每做一个纵向功能，先在 Linux 快速实现，在 PR 的 Cross CI 运行真实主机差分，核心系统接口变化时提升为 Full 专题；不要积累“Linux 后来再移植”的长尾债。

Cosmopolitan 官方工具链文档说明：`bin/cosmocc` 默认构建 multi-OS/multi-arch APE，也可以分别使用 `x86_64-unknown-cosmo-cc`、`aarch64-unknown-cosmo-cc` 与 `apelink` 合并；还可产生用于调试的 Linux ELF 辅助产物。**不能把构建成功当成已经跨 OS/CPU 执行成功**。参考：[Cosmopolitan Toolchain 官方说明](https://github.com/jart/cosmopolitan/blob/master/tool/cosmocc/README.md)。

### 13.2 选型、锁定、离线构建与真实版本证明

- **唯一选定工具链**：GitHub Actions Run `41169443573`、Artifact `13031425078`、原始 ZIP SHA-256 `f2f984d3...a16cf`；正式构建只读取 `qaplet.toolchain-lock.v3`，不能用 `master`/“latest Nightly”隐式覆盖。以后更换必须走独立 `toolchain-bump` PR。
- QuickJS 的源码 SHA、Git Tree、`VERSION`、补丁 SHA、C/C++ 编译器 flags 都进入 `source-provenance.json`；仅 `quickjs --version` 或 `VERSION` 日期不足以证明身份。
- 本次 **cosmocc 官方 Artifact ZIP 的元数据身份已确定**（GitHub 的 SHA-256、大小、Run HEAD、workflow/path、package.sh 和构建日志），现有 M0 已对原 ZIP、模式/链接及工具链功能形成实测；新入口仍须逐次复核并迁入组件 Manifest，目标 QuickJS 组合另验 Hello/ESM/Promise；官方 Run 关联 `aefad27...` 不等于有独立签名 attestation。
- 离线/受保护 Release：`export QAPLET_OFFLINE=1; make deps-verify VERIFY=deep && make bootstrap && make release`，依赖必须已预热于已锁定缓存或受审计镜像；网络缺失必须给出精确 digest 缺失错误。普通开发的缓存 miss 可以根据 lock 向**固定字节源**抓取一次。
- 对环境差异明确设定 `SOURCE_DATE_EPOCH`、`LC_ALL=C`、明确编译 flags；若 APE 产物因签名时间戳、系统依赖或 Windows 宿主路径不同而不一致，应保存差异并停止“可复现”宣传。

### 13.3 Profile、产品体积与交付边界

| Profile | 内容 | 发布策略 |
|---|---|---|
| `minimal` | QuickJS + Host Loop + ESM + 基础 FS | M0/M1 开发与 ABI 探针起点，不宣称 Node 完整兼容 |
| `base` | CLI、ESM/CJS、主要 Node APIs、FS/TTY/net/http/安全 TLS、QJB、通用 Embed SDK 接入点 | 默认 runtime fat APE |
| `full` | base + 经准入的压缩、SQLite/WASM、HTTP2 等 | 可选；功能声明与静态闭包一致 |
| `sdk` | `libqaplet-embed.a`、版本化 C Headers、POD event/Host module 合同 | SDK 供应用静态链接，目标机器无开发依赖 |
| `tests` | Oracle、可复现 fixtures、格式攻击 corpus、ABI/平台探针 | 测试包与 CI evidence 独立发布 |

各 profile 分别统计体积、cold start、RSS、静态依赖增量；在 M1 有真实基线后再冻结目标，而不是为体积牺牲 TLS 验证、ZIP 安全检查或 SDK ABI 一致性。

### 13.4 `.cache` 用法：本地热缓存与 CI 可重建性

- 目录定义、信任级、数据表和 Git Ignore 已在 §1.3–1.4 固定；开发者 `make deps-verify` 与 CI `make deps-verify` 调用**同一个校验器**，不得 CI 自写捷径。
- 工作区缓存按输入身份分层：`download archive SHA`、`QuickJS Commit/Tree`、`patchset SHA`、`host/target/profile/flags`。仅当身份相同且内容通过验证才可复用。
- 下载操作禁止不受约束地 `curl URL | unzip`；从**固定 Artifact 或其不可变镜像**读取原始 ZIP → 校验 GitHub 给出的 SHA-256/419652152 字节 → 拒绝恶意 ZIP 条目 → 在临时目录解压 → **验证实际模式/链接，确有缺失才按受审计白名单恢复** → 生成 Manifest + 编译/APE Probe → 原子发布到 `.cache/toolchains/<digest>`；仅执行内容、模式/链接与功能验收均满足合同的工具。
- 物化完成并不能跨机器自动证明相同效果：CI 使用新 Runner 每次解压；本地可复用但在正式 release 的 `VERIFY=deep` 校验中必须重新验完整内容。不能只检查 `.cache/toolchains/cosmocc` 目录存在就构建。
- `build/`（或 `.cache/build/`）的 obj/.a 不跨架构默认复用；若后续实现 CCache/sccache，应强制 toolchain digest、compiler flags、source/headers/patch SHA、目标 ABI、profile、重要环境变量参与 key，且 release 另有 clean rebuild 对照。

建议 `.gitignore`：

```gitignore
/.cache/
/build/
/dist/
*.tmp
*.partial
```

推荐 `make` 工作流：

```bash
# 第一次初始化：维护者先将源码/工具链真实摘要记录并审过锁 PR
make deps-status
make deps-fetch                         # 仅固定 GH Artifact/完全相同的长期镜像；cache miss 可联网
make deps-verify                        # 校验原始 ZIP SHA / 大小 + QuickJS Commit/Tree
make bootstrap                          # 解压、恢复工具可执行权限、Manifest 和 Probe
make m0-a                               # Linux x64 Hello APE + QuickJS 微型闭环
make m0-b                               # 构建 fat APE + 导出给跨平台 job
make m0-c                               # 本地可运行的 ABI 探针集合

# 强制全新构建/离线复现
make cache-clean
QAPLET_OFFLINE=1 make deps-verify VERIFY=deep # 删除归档后须报告锁定 Artifact ZIP 缺失
```

### 13.5 GitHub Actions：三个 CI 等级、完整缓存恢复示例

**CI 调度原则**：上游工具链只能使用 Run `41169443573` / Artifact `13031425078` 的原始 ZIP，缓存不存在时只从显式允许的固定 Artifact 或同 SHA 镜像恢复，发布前须完成长期镜像验收；不能使用 `cosmo.zip` 当日 Nightly。`Fast` 每个提交（Linux x64 lint/锁/编译/小测）；`Cross` 每个 PR（Linux + Windows + macOS 的同字节 APE smoke/高风险接口），并对 PR 的 `src/host`、`src/engine`、`include/qaplet`、`toolchains/`、`patches/` 修改提升测试集；`Full` 每日或正式发布（Linux ARM64 原生页覆盖/完整系统 QEMU 补缺、macOS ARM64、必要时 Windows ARM64 仿真、PTy/ConPTY、Node Oracle/24h soak/签名和 ZIP fuzz）。Fast 不能代替 Full，硬件不可用应显示 `not_run`/blocker，不可造假 `verified`。

示例（**工程目标 YAML，命令由对应 M0 issue 实现；Actions 主版本在实际仓库宜锁定为审计过的 commit SHA**）：

```yaml
name: foundation-fast
on:
  push:
    branches: [main]
  pull_request:
  workflow_dispatch:
permissions:
  contents: read
jobs:
  fast:
    runs-on: ubuntu-24.04
    env:
      QAPLET_CACHE_DIR: ${{ github.workspace }}/.cache
    steps:
      - uses: actions/checkout@v6
      - name: Read pinned digest (fail if not pinned)
        id: lock
        shell: bash
        run: |
          python3 -c 'import json,os,re; d=json.load(open("toolchains/qaplet.lock.json")); h=d["cosmocc_nightly"]["archive_sha256"]; assert d["lock_state"]=="pinned" and isinstance(h,str) and re.fullmatch("[0-9a-f]{64}",h); open(os.environ["GITHUB_OUTPUT"],"a").write("cosmocc_sha="+h+"\n")'
      - name: Restore immutable pinned toolchain archive
        id: cached-archive
        uses: actions/cache/restore@v6
        with:
          path: .cache/downloads/cosmocc
          key: qaplet-cosmocc-artifact-${{ steps.lock.outputs.cosmocc_sha }}
      - name: Obtain exact upstream Artifact ZIP / verify / restore x-bit
        shell: bash
        run: |
          make deps-fetch
          make deps-verify VERIFY=archive  # pre-extraction: ZIP SHA/size + source commit/tree
          make bootstrap  # safe extract + declared x-bit/link repairs + toolchain probe
          make deps-verify VERIFY=deep     # post-extraction: component Manifest / ABI
          make m0-a
      - name: Save archive cache only from trusted main push
        if: github.event_name == 'push' && github.ref == 'refs/heads/main' && steps.cached-archive.outputs.cache-hit != 'true'
        uses: actions/cache/save@v6
        with:
          path: .cache/downloads/cosmocc
          key: qaplet-cosmocc-artifact-${{ steps.lock.outputs.cosmocc_sha }}
      - name: Upload verifiable test evidence, not compiled cache
        if: always()
        uses: actions/upload-artifact@v4
        with:
          name: foundation-fast-evidence
          path: test-evidence/foundation-fast/
          if-no-files-found: warn
```

**实现提醒**：示例只有在 `lock_state=pinned` 且固定 Artifact ZIP 已完成首次内容验证后才运行。`make deps-fetch` **必须保证**只把固定 Run `41169443573` / Artifact `13031425078`（或其不变镜像）的原始 ZIP 写入 `sha256-<digest>.zip`；缓存未命中不得自动下载滚动 Nightly。`actions/cache` 的 `cache-hit=true` 不是认证条件；若命中路径被污染也会在 `make deps-verify VERIFY=deep` 失败后重取/失败。`actions/cache@v6` 官方 README 已有该主版本；实际自托管 runner 必须满足对应 Node/runtime/runner 要求，采用固定 Commit SHA 后再在受保护 CI 使用。<https://github.com/actions/cache>

`foundation-cross.yml` 可将 Linux 构建出的同一 `hello-fat.ape` 使用 `upload-artifact` 传给 Windows/macOS runner，**先复核下载后 SHA，再改名/执行并回报 OS/CPU/loader/probe**；不要在每个验证平台重新构建不同字节文件却称之为“同一 APE”。ARM64 原生 runner 先实测页配置，再以固定完整系统 QEMU guest 补缺失的 4K/64K；guest 内再次测量。报告按 native/emulated-system 标识，不以 TCG 成绩声明物理硬件或性能支持。

### 13.6 CI 与发布安全：缓存不是供应链信任根

- **来源**：QuickJS Commit + Tree + VERSION + patch SHA；cosmocc 记录固定 GitHub Run/Artifact、Run HEAD SHA、原始 ZIP digest/size、提取权限/组件 Manifest、工具链 Probe；运行日志标明是否有独立签名 attestation。原始 ZIP 的留存镜像作为可复现条件，不把 rolling 下载 URL 当成可用来源。
- **权限**：`permissions: contents: read` 起步；不允许 `pull_request_target` 检出并执行外部 PR 代码，默认分支的 cache save 只由受保护的可信事件进行；PR 不能带私钥/云凭据构建。GitHub 的 cache 可被可读分支/PR 访问，绝不缓存 Secret。
- **Artifacts**：GitHub `actions/cache` 用于可丢弃的下载加速，`upload-artifact` 与 Release 用于测试数据、manifest、可重现的 APE、SBOM；发布签名私钥应通过单独受控机制提供，不能落入 `.cache/` 或工作目录产物。
- **失效试验**：CI 专门测试错 Artifact ZIP SHA、篡改 ZIP、缺某架构组件、可执行位丢失/符号链接损坏、镜像/Artifact 双双失效、QuickJS VERSION/Commit 不符、补丁漂移、缓存删除/恢复、网络断开、CI worktree 非干净状态；每个都必须明确失败而不是下载“最新”静默绕过。
- **升级后的回归**：锁变化触发 M0-A/B/C、SDK ABI、与改动相关的 Node/QJB/ZIP/安全矩阵；发布候选执行无 cache 的 clean rebuild 对照。夜间 CI 可用缓存加速，但不能以此取代无 cache 的发布验证。

### 13.7 正式可交付证据

- `qaplet-fat.ape`，按需 CPU slice 调试工件、`qaplet-sdk`、能力矩阵、工具链锁、source/patch/build provenance、SBOM、许可证。
- 每个已声明平台实跑的同字节 SHA、日志、CPU/OS/kernel/page-size/loader/ABI；Linux ARM64 64KB 是显式门槛而不是 “aarch64 build exists”。
- 工具链固定 Artifact 原始 ZIP SHA/size、GitHub Run HEAD 与日志关联、权限修复/解压 Manifest、编译/link flags、`SOURCE_DATE_EPOCH`、首次下载 UTC 时间和不可变镜像地址/校验；工作流关联不是独立加密 attestation，没有时如实标 `signed_attestation=false`。
- 在已预热且验证的离线源/归档仓库中，删除编译对象及已解压工具链后执行离线 clean rebuild，并与正常缓存构建产物 SHA 比对；签名、Win/macOS assimilated 格式导致的字节变化独立报告，不与原始 fat APE 共用 SHA 声明。
## 14. 跨平台验收：运行环境、行为合同与真实设备

### 14.1 支持级别不是一个布尔值

| 支持级 | 定义 | 典型宿主 |
|---|---|---|
| Tier-A Release Gate | 每次正式发布执行真实机器/VM、功能/负向/安装/基本性能，全部关键项不跳过 | Linux x64、Windows x64、macOS x64/ARM64、Linux ARM64 4K/64K |
| Tier-B Verified | 定期原生执行、具体功能有对应证据；发布前必须检查高风险回归 | Windows ARM64 上 x64 模拟执行、FreeBSD x64（按资源逐项纳入） |
| Tier-C Experimental | 构建或代码路径研究但未建立完整官方验收 | OpenBSD/NetBSD、更多 Linux CPU 组合、Android/bionic 可能性 |
| Not Supported | 无交付证据；`--capabilities` 不得标为支持 | RISC-V、LoongArch 的直接 APE 承诺，以及未验证旧 Windows 版本 |

**注意**：表中是“计划的验证层级”，并非已经实现的 Qaplet 支持。正式 v1 发布可按资源减少 Tier-A 初期范围，但必须对外如实披露；不能继承旧项目的“七宿主已绿”便自动算新 Qaplet 已绿。

### 14.2 最小真实执行矩阵

| OS / CPU | APE 直接启动 | Node API | QJB / ZIP | TTY | 子进程 | Network/TLS | 大资源 |
|---|---|---|---|---|---|---|---|
| Linux x86-64 | **P0** | P0 | P0 | PTY | P0 | P0 | P1 |
| Linux AArch64 / 4KB | P0 | P0 | P0 | PTY | P0 | P0 | P1 |
| Linux AArch64 / **64KB** | **P0** | P0 | P0 | PTY | P0 | P0 | P1 |
| Windows x86-64 | **P0** | P0 | P0 | ConPTY/Console | P0 | P0 | P1 |
| macOS x86-64 | P0 | P0 | P0 | PTY | P0 | P0 | P1 |
| macOS AArch64 | P0 | P0 | P0 | PTY | P0 | P0 | P1 |
| Windows ARM64 x64 模拟 | P1 | P1 | P1 | Console | P1 | P1 | P1 |
| FreeBSD/OpenBSD/NetBSD | P2 | P2 | P2 | P2 | P2 | P2 | P2 |

对每个可移植 APE，严格区分**同一字节产物**、运行时的 OS/CPU 识别、经过 helper 或仿真执行、平台重命名和签名衍生版本。若某平台需要特殊同化/签名步骤，生成单独的 Managed Distribution 身份，不将其 SHA 与原始 fat APE 混同。

### 14.3 APE 与 OS 兼容故障样本

- Linux 系统将 MZ 文件送进 Wine/WSLInterop/binfmt_misc 的情况；需要实际测试 loader、可执行权限、mount `noexec` 与 shell 路径。
- Windows Defender/企业白名单、受限账户、`.com`/`.exe` 文件名、Win32 Unicode path、UNC、reparse point/junction；`GetProgramExecutableName()`、`argv0`、实际映像路径区分。
- ARM64 ELF 最小段对齐、16K/64K 内核加载、TLS register 和 Windows ARM64 非原生模式；不以 qemu-user 成功替代真实 64KB kernel。
- Darwin PTY 关闭/恢复及 HUP、终端尺寸；进程结束时恢复所有 raw/cursor ownership。
- Windows `poll()` 对 SOCKET/HANDLE/pipe 的差别、非阻塞 connect、连接半关闭、read/write 粒度和 64KB granularity。
- 子进程：APE 调用另一 APE、原生 EXE、带空格/Unicode 命令、shell=false、stdin EPIPE、stdout/stderr 并发、取消和进程树清理。

### 14.4 Node 差分与真实应用验收

每个 API test fixture 必须能通过原版 Node LTS 与 Qaplet 执行；比较语义而不仅是 stdout 文本。Oracle 被固定在 `test-manifest` 的摘要上；对预期差异提交 `known-differences`，不允许大量无说明 skip。

**真实项目阶段选择**：Clack、Terminal-Kit、几款无需原生 addon 的包（如 commander/chalk）、离线诊断 CLI、HTTP/SSE 应用、`fs + child_process` 工具，以及一个通过通用 Embed SDK 运行 JS/QJB 的 C Host 示例。增加复杂性后可尝试 Ink（React/Yoga/WASM 不应作为首发门槛）。所有样本固定源码版本、lockfile、运行命令、报错与修改量。

### 14.5 稳定性测试与优先性能指标

- **首发基础功能**：单 Runtime 运行 24h idle/event-loop、重复 create/destroy 10,000 次、低内存、100～256 TCP 空闲连接（逐步扩展）；GC 压力、取消、异常后再次运行。
- **IO fairness**：多个小/大 HTTP 流并发、SSE 长连接、网络抖动/断网、DNS 超时、慢速 FS、阻塞 CPU 任务；不能出现 nextTick 饥饿或不可取消的无界 queue。
- **运行时间指标**：cold start、steady RSS、process CPU idle、资源映射 VSS/RSS、first-byte latency、p50/p95/p99 Loop lag/HTTP latency、release binary size、hash 一致性。
- **回归基准**：和历史 quickjs-cosmo 的若干可复现场景做 A/B，但不把旧工程的性能测量直接写成新项目成绩；与 Node.js、txiki.js、LLRT 对照相同 JS 任务时要说明 API 差异。
- **观测证据**：发布报告保留 benchmark 使用的主机 OS/kernel/CPU/内存/存储/证书、冷/热运行、重复次数和统计方法。Perf 目标在 M1 测得真实基线后冻结，不凭空承诺具体微秒级启动数字。

---

### 14.6 平台接入时间与强制 CI 层级

| 目标 | 介入阶段 | 最小证据 | 默认回归频率 |
|---|---|---|---|
| Linux x86-64 | **M0-A** | cosmocc 可执行、QuickJS Hello ESM/Promise、微型 C Host、同字节身份 | Fast：每提交 |
| Windows x86-64 | **M0-B → M0-C** | APE 直接/受控 loader 启动、pipe/socket/HANDLE/TTY/child_process、路径/退出 | Cross：每 PR，Host 变更必跑 |
| macOS ARM64 | **M0-B** | APE loader、ESM/Promise/mmap、TTY、OS/CPU/loader 实际识别 | Cross（可用时）+ Full |
| Linux ARM64 4KB | **M0-B** | x64 同一 fat APE 的 arm64 slice 在原生或完整 guest 执行，线程/GC/FS | Full；发布前强制 |
| Linux ARM64 **64KB** | **M0-C** | 真 64KB kernel 的 ELF/APE loader、pthread TLS、mmap、ZipOS/资源对齐 | Full；核心 ABI/Loader 变更必跑 |
| macOS x86-64 | M0-B 后尽快 | 真机/VM APE、stdio/TTY、proc、文件路径 | Cross/Full |
| Windows ARM64 | 后续分级准入 | x64 emulation 与 native ARM64 **明确区分** | Full/Tier-B，未验不声明支持 |
| BSD / 更多 CPU | P2 | 对应 ABI/OS 真机证据 | 实验性，不阻断首发 |

**Fast 与 Cross 的职责不同**：Fast 用 Linux 主线支持快速功能迭代；Cross 的工作是及早否定“底层设计可移植”这一假设，而不是承诺所有 Node API 已完成。M0-C 未通过时可以继续 M1 里独立的 ESModule/语言功能，但不得将通用 Reactor、TTY、Process、SDK OS ABI 标为 `frozen`，更不得将核心 `verified` 发给所有平台。**对 `src/host/`、`src/engine/`、`src/embed/`、`include/qaplet/`、Nightly/QuickJS lock 的 PR 必须升高跨平台门禁**；资源不足时标出待补的目标及执行方式，并按影响面阻断对应能力的 release。
## 15. 安全、权限、供应链与故障隔离

### 15.1 分层信任模型

- **程序与模块**：发布物签名/摘要证明发布身份和完整性，并不自动证明所含 JS 可以无风险执行。
- **运行时权限**：Node 风格 `fs/net/child_process` 的 native 操作遵循当前 OS 用户权限；QJB/QuickJS Context 不提供 OS 级沙箱。需要不可信代码隔离时使用 OS 受限账户或隔离进程，并另行验收。
- **包与资源**：外部 ZIP/QJB/ZIP64 输入必须检查越界、重复名、重叠、畸形偏移、解压炸弹、路径逃逸、整数溢出和签名/hash 可信性；ZIP CRC 不等于签名。
- **网络与密码**：TLS 链/hostname/SNI/证书时间验证、私有 CA、信任根更新、ALPN 与拒绝弱算法都需要显式安全回归；失败不得退回明文。
- **C Embed 边界**：只暴露版本化 ABI 与可控 native module，回调非 owner 线程不能操作 `JS_*`；释放、取消和崩溃后的异步事件不得触及失效 JS 堆。
- **诊断**：环境、Token、私钥、路径中的凭据与裸 native 指针不得被默认日志导出；Debug 仍需脱敏。

### 15.2 供应链控制

建立 `Source Lock -> Archive Verification -> Toolchain Materialization -> Build Evidence -> Test Evidence -> Release Evidence` 链。下载后先检 SHA/size，再审计 ZIP 条目，再执行解压/权限恢复；QuickJS Git Commit/Tree、VERSION 与补丁各自核验。固定工具链更新只能经过 `toolchain-bump` 独立 PR，任何 `make release` 都不能临时自动选择 Nightly。

Release 默认在预热、已验证依赖的离线环境中构建；SBOM（SPDX/CycloneDX）、许可证、CVE 状态、patchset、编译参数及同一 APE 跨宿主测试证据一并保存。若缺少签名构建 provenance，则明确声明仅具备官方 GitHub Run 与 Artifact 摘要关联，不能宣传拥有更高级独立构建证明。

### 15.3 多租户与沙箱不是 v1 自动能力

即使每个 Worker 使用独立 QuickJS Runtime，native binding 仍可能访问同一 OS 用户权限；权限模型必须具备 OS 层实际限制才可宣称 sandbox。编译器的 manifest capabilities 表达最低依赖需求，不自动赋予强隔离。

### 15.4 证据政策

- TLS/CRYPTO/ZIP64/工具链升级/SDK ABI 破坏性调整需要独立审查与负向证据，不以 AI 生成实现等同批准。
- CI 包含来源哈希、静态检查、相关 native sanitizer/fuzz、跨平台 smoke、已锁 Node Oracle 差分及 release 完整性检查。
- 任一 `verified` capability 必须可追溯到 `test_case_id`、Git SHA、toolchain digest、build hash 和真实 host result；缺席为 `not_run`/`partial`。

---

## 16. 历史 quickjs-cosmo 踩坑清单与新项目转化

旧仓库只提供可复现问题样本与回归测试输入，不直接迁移其 runtime 实现或隐式继承“已支持”的产品状态。

| 已知问题/风险领域 | Qaplet 策略 | 验收 |
|---|---|---|
| Windows `os.exec` / `CreateProcess` 出错及返回码差异 | 通用 ProcessHost，shell=false，原生 Win32 错误、路径/argv/环境验证 | M0-C / M2 / M3 |
| `_COSMO_FDS`、子进程重入与 stdio handle 继承 | 隔离的句柄生命周期和 EOF/exit 回归 | M2 |
| Windows junction/UNC、`stat/lstat/realpath` 差异 | OS path/identity 与显示路径分开，跨平台差分 | M3/M5 |
| APE loader 与 ARM64 64KB 页面 | 原生/完整系统 guest 的 loader/pthread TLS/内存与 ZIP 探针 | M0-B/C |
| 每 socket 一线程、10ms polling | 单逻辑 Loop + Reactor + waker + 有界 worker | M2 |
| Cosmopolitan TLS 库版本及补丁状态 | 锁定内容中审计真实版本/安全公告，不以 Nightly 名称当修复证明 | M4 |
| TLS 连接固定大 Arena、每连接重建 CA | 共享只读信任根、内存预算/并发压测 | M4 |
| CA 资源名冲突或资源覆写 | manifest/hash/路径碰撞校验，拒绝 silent fallback | M5 |
| QJB 整包 `malloc` 快照 | 按需索引、mmap/pread/受限解压 | M5 |
| `/zip/` mmap 退化为匿名复制 | 仅对原始 APE STORE 成员进行对齐映射 | M7 |
| TTY raw/resize/PTY 退出时资源所有权 | Native TTY lease、幂等恢复、强制退出负向测试 | M3 |
| `nextTick` / Promise / ref-unref 时序 | Scheduler 与 Node Oracle 差分 | M2/M3 |
| SDK/Worker/QJB 入口上下文漂移 | 统一 Context Factory、模块/资源/能力合同 | M1/M5 |
| 发布 Profile/SHA/动态依赖不一致 | source/toolchain lock、build info、SBOM、跨 host 验证 | M8 |

将上述 case 记录在 `tests/legacy-lessons/README.md`，每条包含 old commit、复现步骤、预期、当前状态、测试结果及支持平台，不能以旧仓库之前的 CI 结果替代当前 Qaplet 对应身份和执行方式的实际运行。

---

## 17. 分阶段实施路线、依赖、平台门槛与正式验收

> **推进原则**：Linux x86-64 先完成第一条最小闭环，但在冻结平台接口之前完成 Windows 和 ARM64 的高风险试验；后续按功能纵向开发，真实 Cross/Full CI 随时检查可移植性。所有阶段采用 `spec -> probe -> implementation -> negative tests -> real host -> evidence`，不写“计划通过”冒充已验收。

### M0-A — 固定工具链与 Linux x86-64 最小 QuickJS APE

**输入与前置**：先完成统一 v3 锁迁移，再受控升级 QuickJS 到 `2833510432045f72915919cef680ba59e5153b16` / VERSION `2026-06-04` 并复核现有 ARM64 overlay；cosmocc 仍为 Run `41169443573` / Artifact `13031425078`，原 ZIP SHA-256 `f2f984d3a9790674fc0da11c1c00a32d4fe3f8dafde99af74b0c6dc24a2a16cf`。原件已测事实复用，使用前仍复核 SHA/size/组件/模式/链接；仅在缺失时审计恢复语义。目标引擎与微型 Host 探针通过后才完成新组合的 pinned 验收。长期原件镜像为发布前门禁。

**任务**：Linux x64 用锁定 cosmocc 编译 Hello APE；链接 QuickJS 原版源码，微型 C main 中创建 Context、加载 ESM、执行 Promise job/退出；记录 flags、真实 loader、SHA、stdout/exit。M0-A **不要求**完整 SDK、CJS resolver、Reactor 或网络。

**通过门槛**：工具链归档本地实算摘要与官方一致；QuickJS Commit/Tree/VERSION/patch 正确；统一 v3 消费者一致；Linux x64 APE 与目标 QuickJS/ESM/Promise 可复现运行。已有缓存可用于开发，长期镜像的同 SHA 恢复在发布前另行验收。若工具链未取得独立签名 provenance，报告使用 `workflow_run_checkout_and_build_log`，如实标 `signed_attestation=false`。

### M0-B — 同一个 Fat APE 的跨平台启动（可与 M1 非风险工作并行）

**任务**：将 M0-A 的 fat APE 原样交付 Windows x64、macOS x64/ARM64、Linux ARM64 4KB；验证相同 SHA 和 JS/argv/env/stdio/clock/exit/fs 基础。区分直接执行、helper、x64 模拟和原生 ARM64。

**通过门槛**：目标宿主有真实 OS/CPU/loader/runtime 输出、同一文件 SHA、错误与限制报告；原生页覆盖优先，固定完整系统 QEMU 可补缺失的 ARM64 页配置；交叉编译和 qemu-user 不算实际内核覆盖，TCG 不外推物理性能。未取得证据的平台保持 `not_run`，不允许宣传 Tier-A。

### M0-C — 高风险 ABI 与通用 Host 接口冻结

**Windows**：Socket/HANDLE/Pipe/Console/waker readiness、`CreateProcess`、进程退出/stdio 继承/Unicode 路径、pthread/TLS、文件路径、ZipOS。

**ARM64 64KB**：原生或固定完整系统 QEMU guest 的真实 64KB Linux kernel 下的 APE loader/ELF segment alignment、pthread TLS、GC、中断、`mmap` granularity、STORE/ZipOS。附带 macOS ARM64 TTY/文件映射探针和各 OS 错误码差分。

**通过门槛**：M2 的 Reactor/Process/TTY/SDK event 接口冻结前必须有真实探针和失败场景收口。M0-C 未完成时仍可做 M1 语言/ESM 工作，但不得将未知 OS 行为写成 verified ABI。

### M1 — QuickJS Engine + 通用 C Embed SDK

**规格状态（2026-11-02）**：用户已确认 [M1 设计 Q1–Q32](../../.scratch/qaplet-m1/design.md)并确认共同理解达成，grilling 完成；[正式 M1 实施规格](../../.scratch/qaplet-m1/spec.md)已发布为 ready-for-agent，沿用 Q32 的测试与证据边界。以下仍为目标合同，正式 M1/SDK 尚未实现或验收；公开 C 声明和枚举数值由规格驱动的实现头文件落实，逐票工作由后续拆票承接。[ADR 0005](../adr/0005-m1-runtime-lifetime-boundary.md)、[0006](../adr/0006-m1-frozen-module-identity.md)、[0007](../adr/0007-m1-cooperative-resource-failure.md)分别记录生命周期、模块缓存及资源终止取舍。

**前置**：统一 v3 锁、目标 QuickJS 升级与新增 M0-A ESM/Promise 已通过；新配方仍须取得自身证据。

- **Runtime/Context**：每 Runtime 一个主 Context，owner 固定为创建线程；全局状态和模块缓存跨求值保留。生命周期与执行阶段分开，显式 pump 推进 PENDING，不隐藏线程或忙等。
- **模块与 CLI**：源码文件 ESM 支持静态/动态 import、TLA；realpath 后的规范 file URL 及失败在实例内缓存，同身份只求值一次。host 模块在首次执行前注册锁定。CLI 提供 `run <file> [-- args]`、`eval --source <text>`、help/version；保留 `qaplet:events` 和只读 `qaplet:runtime` argv/cwd。源码格式、URL/Windows 限制、import.meta 初始值与缓存规则以已确认设计为准；CJS/package/QJB 属于后续阶段。
- **SDK 数据与错误**：ABI 1、结构大小检查、调用方诊断；有限标量/64 位整数/严格 UTF-8/字节桥，参数借用、返回同步复制。get_result 复制完成结果，缓冲区不足可重试。普通 JS 异常在原预算内结算可运行 jobs 后可复用；期限、总调度量及实际分配拒绝永久 FAILED，首因不被清理错误覆盖。
- **队列与停止**：预分配固定槽、复制 POD、FIFO；未注册接收函数返回 NO_HANDLER，满队列立即 QUEUE_FULL，不覆盖或重试。入队成功不保证交付。stop 不占队列容量，立即拒收普通工作；已领取事件协作退出，剩余事件/jobs 在 close 时释放，不运行用户清理 JS。close 幂等保留句柄，destroy 要求 CLOSED 且宿主已撤下生产者并等待在途调用结束。

| 默认预算 | 值 |
|---|---|
| 引擎堆 / 栈 / SDK 原生存储 | 64 MiB / 1 MiB / 16 MiB |
| 单源码 / 模块源码缓存 / 模块数 | 4 MiB / 32 MiB / 256（含内建、host 与失败记录） |
| 事件队列 / 单事件载荷 | 256 槽 / 1 KiB |
| 单桥接字符串或字节值 / 宿主函数参数 | 1 MiB / 32 个 |
| 单次执行期限 / 总调度量 | 30 秒单调时钟 / 100,000 个 jobs 或事件回调，跨 pump 不重置 |
| 单次 pump | 最多 256 jobs、32 事件；jobs 未结算完不插入下一事件 |

这些是可配置的有限工程默认值，不是 RSS 或硬抢占承诺；0 使用默认值，M1 无无限模式。空闲时各事件及其可运行 jobs 单独记账。未处理 rejection 在完整 jobs 检查点判定；SDK 无进展返回 PENDING，CLI 没有外部生产者时立即报告 UNSETTLED_ENTRY。CLI 成功/执行失败/参数错误退出码为 0/1/2。

**门槛**：Ubuntu 构建 CLI、静态 SDK 与纯 C Host 示例，其他宿主验证供应制品；Linux Fast 完整正/负合同，Cross 覆盖 Windows x64、macOS x64/ARM64、Linux ARM64，敏感改动仍按现有规则提升 Full，ARM64 64K 覆盖新输入及 SDK 关键合同。验收模块/TLA/循环实际支持子集、缓存/错误、线程/重入、事件复制、队列饱和/stop 并发、预算/分配拒绝、普通异常后复用、初始化失败清理和 close/destroy 次序。Linux 至少 1,000 次创建关闭销毁，Cross 每目标至少 100 次；缺目标或必需用例失败/跳过不签发通过。M2 的 OS waker、I/O 取消、迟到 completion 与完整调度保持原分界。

### M2 — Loop / Reactor / WorkPool / Process / SDK Event Bridge

实现 Scheduler、TimerHeap/ref-unref、waker、通用 `poll` Reactor/CompletionQueue、有界 WorkPool、基础 ProcessHost 与 TTY/stdio、SDK 通用 POD 消息交接。Windows 根据 M0-C 探针选择 HANDLE/Socket 分路，不得假设单一个 `poll` 语义。

**门槛**：无 socket 独占线程和 busy-spin；跨线程不会调用 `JS_*`；队列满、销毁、取消、fd generation、迟到 completion、重复 close/stop 无 UAF/死锁；Linux/Windows 真机语义差分。

### M3 — Node 核心 API / ESM-CJS / Package Resolver / TTY

`node:*` 能力矩阵、CJS/ESM 互操作、package exports/imports、Buffer/Stream/Events/FS/Path/Process/readline/TTY，使用指定 Node Oracle 版本差分。选择 Clack/Terminal-Kit 等纯 JS 项目做交互和错误路径实测。

**门槛**：至少 8–12 个精选 CLI 核心行为在支持平台通过；未实现 API 显式 `unsupported`；Windows 终端恢复、Unicode、UNC、stdin redirect、`process.nextTick`/Promise 顺序可复验。

### M4 — TCP / TLS / HTTP / Fetch / SSE / WebSocket

非阻塞 TCP/DNS、经验证 TLS 客户端、HTTP/1.1 client/server、Fetch/SSE/WebSocket 子集、流式背压/超时/取消。网络安全后端依赖来源与构建身份进入 SBOM。

**门槛**：CA/hostname/SNI/证书过期/私有 CA/断网/超时/IPv6/高并发负向案例跨宿主通过；TLS1.3/HTTP2 等未达到门槛则标 partial/experimental，不伪称完成。

### M5 — QJB v1 / ZIP VFS / 受信任模块与资源加载

定义并实现 QJB 严格 ZIP 子集、manifest/依赖索引、ESM/CJS/VFS、按需 mmap/pread、pack/inspect/verify、格式 fuzz；SDK Host 也能调用同一 Loader 执行 QJB。

**门槛**：不整包匿名复制，不在运行时偷偷解压 node_modules；畸形 ZIP、越界、重复、路径逃逸一律拒绝；QuickJS 或 Bytecode 编译选项变更触发字节码缓存作废。

### M6 — App Compiler / 通用 Native Host Template

纯 JS Node 工程生成 fat APE；支持依赖能力检查、直接映射 ZIP 资源、静态 SDK Native Host 的通用构建合同。先用 Make + cosmocc 链接 C Host 示例，再规划 `qaplet compile --host`；禁止为某类应用设计特殊链接捷径。

**门槛**：至少三个真实 CLI/TUI App APE 在多 OS 使用同一文件运行；一个纯 C Host + SDK + JS/QJB App APE 也跨宿主验证；不支持原生 `.node`/动态 require 时给出具体构建诊断。

### M7 — ZIP64 / STORE / 原始文件对齐 mmap

研究真实 APE 资源偏移、ZIP64/STORE/64KB 对齐、窗口化 mmap/pread、外置 `assets.qap`，明确 Windows >4GB 可执行文件限制。

**门槛**：1GB/近 4GB、外置 >4GB 测试及原始 APE 映射/RSS 证据；Windows 与 ARM64 64KB 对齐/越界/畸形 ZIP 负向回归。

### M8 — 跨平台质量、安全与供应链发布收口

完成 base/full/sdk/tests profile、Node Oracle、SDK ABI、ZIP/TLS fuzz 与安全、24h soak、SBOM/licenses、源代码及固定 toolchain provenance、Fast/Cross/Full matrix。清空编译/物化缓存后，**保留已验原始 ZIP 与 QuickJS Git 对象**，做离线 clean rebuild，比较 APE SHA 及差异报告。

**门槛**：GitHub Artifact 原始 ZIP 已独立本地哈希并长期存档；权限恢复规则/组件 Manifest/工具链 probe 可重复执行；Linux ARM64 64KB 真实内核及已声明 OS/CPU 的支持报告完整，原生/完整系统 QEMU 分别记录；任何 `not_run`/跳过的高风险门禁不允许隐藏。

### M9 — v1.0.0 发布判定

基础发布要求 M0-A/B/C 在**实际公开承诺的平台集合**内有真实结果、M1–M3/M5–M6 的最小公开合同已验收，以及 M8 完整的工具链、安全/发行证据。M4 可按已经验收的 HTTP/TLS/Fetch 子集交付，M7 未成熟时明确标 `experimental` 并避免以超大资源为默认卖点。版本由测试与能力清单决定，不因阶段编号自动宣称全部完成。

### 17.1 工作并行与门槛依赖图

```text
[统一 v3 锁迁移]
                           |
[目标 QuickJS 升级/补丁复核 + fixed cosmocc Artifact SHA]
                           |
                  M0-A Linux x64
                           |
                 +---------+----------+
                 |                    |
          M1 Engine/ESM/SDK      M0-B Cross-host
            OS-neutral work      same-byte fat APE
                 |                    |
                 |                M0-C OS ABI
                 |              Win I/O, ARM64 64K
                 +----------+---------+
                            |
                  Host ABI Freeze
                            |
                M2 Loop/Process/SDK
                            |
         +------------------+------------------+
         |                  |                  |
      M3 Node/TTY      M4 Network/TLS      M5 QJB/VFS
         |                  |                  |
         +------------------+------------------+
                            |
                 M6 App Compiler / SDK Host
                            |
                   M7 Large Assets
                            |
                 M8 Cross/Full/Release
                            |
                       M9 v1.0
```

**不得提前冻结**：Windows Reactor/Process 与 ARM64 mmap/Loader 基础行为；SDK 异步跨线程回调模型；QJB bytecode engine identity。**允许提前并行**：M1 的纯 ESM/语言层、M5 格式文档/ZIP 攻击语料、M4 依赖库安全调研、跨平台 runner 环境准备与仓库 CI 基础设施。

---

## 18. 首轮工程 Epics / 可验收 Issues

Issue 统一包含 `Goal / Scope / Out-of-Scope / Source Files / Tests / Negative Cases / Evidence / Rollback`；优先可运行的纵向切片与机器证据，不以 API 名称代替实现。

下表是总体 Epic 目录，不是全部重新执行的待办清单。已有 BOOT/CACHE/ABI 等结果按原身份与范围保留，新增差距见[对齐任务](../../.scratch/qaplet-v15-alignment/README.md)；后续阶段进入执行前再细化独立任务。SDK-01 覆盖 M1 最小闭环，SDK-02 承接 M2 的 OS 调度语义。

| ID | 事项 | 前置条件 | 最小完成标准 |
|---|---|---|---|
| BOOT-01 | 新仓库、License、Makefile、AGENTS、基础 CI | 无 | `make doctor` / `make test-foundation` 可执行 |
| LOCK-01 | QuickJS SHA/Tree/VERSION + 固定 cosmocc Artifact 锁 | BOOT-01 | Run/Artifact/原始 ZIP/源 SHA 独立身份，验证错误失败 |
| BOOT-02 | QuickJS 锁定 Git 对象与干净源码物化 | LOCK-01 | 精确 SHA/Tree/VERSION，补丁显式，离线可恢复 |
| BOOT-03 | 固定 cosmocc Artifact 原始 ZIP/组件验证/安全解压 | LOCK-01 | Artifact 13031425078，指定 SHA 和 size，模式/链接审计及必要恢复、Manifest/功能探针 |
| BOOT-04 | **M0-A** Linux x64 微型 QuickJS + APE | BOOT-02/03 | 真实运行 ESM/Promise，记录 APE SHA 和 exit/stdout |
| BOOT-04B | **M0-B** 同字节 fat APE 跨 OS/CPU | BOOT-04 | Windows/macOS/ARM64 SHA 与运行时真实证据 |
| BOOT-06 | **M0-C** Linux ARM64 64KB 内核探针 | BOOT-04B | loader/mmap/pthread/ZipOS 原生或完整系统 guest 正负案例 |
| BOOT-07 | **M0-C** Windows Socket/HANDLE/TTY/Process ABI 探针 | BOOT-04B | readiness/取消/IO/stdin/exit 的真实行为矩阵 |
| CACHE-01 | `.cache` 本地归档、源码、工具链物化、prune | LOCK-01 | 校验/内容寻址/原子安装/损坏/离线缺失失败 |
| CACHE-02 | GitHub Actions cache/恢复策略 | CACHE-01 | 只缓存 SHA 锁定原件；恢复后再验；PR 不写可信 cache |
| CI-FAST | 每提交 Linux x64 源码与编译基础门禁 | BOOT-04 | 验证 lock/最小 Engine/SDK 后续回归 |
| CI-CROSS | 每 PR 跨 OS/CPU smoke | BOOT-04B | 同字节产物运行，宿主身份/结果可审计 |
| CI-FULL | 每日/RC ARM64 64KB/安全/长期稳定性 | BOOT-06/07 | 原生/完整 guest 实际证据，必需资源缺失 `not_run`，发布不跳过 |
| CORE-01 | Context Factory、ESM、GC、Jobs/Promise | BOOT-04 | 多次 init/destroy、正确错误传播 |
| CORE-02 | Timer/Reactor/WorkPool/CompletionQueue | CORE-01 + M0-C | 无 busy-spin、取消/越界/迟到事件安全 |
| CORE-03 | 通用 Process/stdio/TTY | CORE-02 | Windows/Linux/macOS 路径、子进程、PTY/ConPTY 真测 |
| SDK-01 | M1 Versioned C Embed SDK、模块注册、有界 POD 队列与协作式停止 | CORE-01 | owner 驱动纯 C Host 运行 JS，能力/ABI 可查询，跨线程只提交数据/停止请求 |
| SDK-02 | M2 OS waker、I/O 取消、迟到 completion 与完整 Event Bridge | SDK-01 + M0-C | 跨线程唤醒、取消与销毁竞态测试无 UAF/死锁 |
| MOD-01 | Node capability matrix + Oracle | CORE-01 | 每项声明有来源、测试与已知差异 |
| MOD-02 | CJS / ESM/package resolver | CORE-01 | Node exports/imports/循环依赖差分 |
| MOD-03 | FS/Buffer/Stream/Process/TTY | MOD-01/CORE-03 | Clack/Terminal-Kit 真实用例 |
| NET-01 | TCP/经审计 TLS | CORE-02 | CA/hostname/SNI/过期证书/资源预算 |
| NET-02 | HTTP1/Fetch/SSE/WebSocket | NET-01 | 流、取消、keepalive、升级与安全负向 |
| PKG-01 | QJB v1 ZIP Parser / fuzz / limits | CORE-01 | 畸形输入/越界拒绝与资源预算 |
| PKG-02 | QJB/VFS Module/SDK Loader | PKG-01/MOD-02 | 按需加载、销毁/路径/生命周期安全 |
| PKG-03 | pack/inspect/verify 与确定性输出 | PKG-02 | 可重复字节与内容清单校验 |
| PKG-04 | App Compiler fat APE | PKG-03 | 至少三个 CLI/TUI 同字节跨 OS |
| PKG-05 | 通用 Native Host App Template | SDK-01/PKG-03 | 独立 C Host 示例链接 QJB 并运行 |
| LARGE-01 | raw APE STORE/对齐 mmap | PKG-01/BOOT-06 | 1GB/RSS/64KB/畸形索引测试 |
| LARGE-02 | 外置 >4GB `assets.qap` | LARGE-01 | Windows 大资源边界与验证 |
| REL-01 | 原 ZIP 长期镜像、base/full/sdk/tests release、SBOM、evidence | Core/M0–M8 | 完整离线重建/多 OS 运行/签名报告 |

### 18.1 建议首轮并行安排

第一条关键路径：`LOCK-01 -> BOOT-02/03 -> BOOT-04 -> CORE-01/SDK-01`。同时准备 `BOOT-04B/06/07` 的 Windows/macOS/ARM64 Runner，确保在 CORE-02/03 / SDK-02 接口冻结前执行 M0-C。Cache/CI 工作是贯穿全程的支撑线，不允许以“暂时先本地手工下载最新 Nightly”代替锁验证。纯 JS Engine 与 ABI 调查可并行，但发布能力不能提前标绿。

### 18.2 工作量估算（粗略工程人时，非工期承诺）

| 工作域 | 工程人时估算 | 不确定性 |
|---|---:|---|
| M0-A/B/C、工具链/缓存/跨平台基础 | 120–250 | GitHub Artifact 权限、64KB loader、Windows IO |
| M1 QuickJS Engine / SDK 基础 | 100–180 | 主干 API、GC/中断、符号链接/编译器行为 |
| M2 Loop/WorkPool/Process/TTY | 150–300 | Windows HANDLE/Socket、取消与 FD 生命周期 |
| M3 Node CJS/ESM/FS/Stream/TTY | 220–440 | npm 生态差分长尾 |
| M4 TCP/TLS/HTTP | 200–420 | TLS 版本/安全后端与协议互操作 |
| M5/M6 QJB/App Compiler/Native Host | 240–450 | ZIP/签名/Native Host 链接与资源身份 |
| M7 大资源 mmap/ZIP64 | 100–200 | 4GB Windows、64KB alignment、内存映射 |
| M8 CI/安全/构建复现/发行 | 140–300 | 真机资源、SBOM、签名/供应链证据 |

这些数字仅用于工作包拆分/风险排序，应在 M0 实测后更新，不构成日历工期或完成承诺。

---

## 19. 高风险问题登记簿（Risk Register）

| 风险 | 后果 | 预防/纠偏 | 门槛 |
|---|---|---|---|
| 固定 Artifact 在 GitHub 过期/提前删除 | 工具链无法重新取得 | 保存相同 SHA 的原始 ZIP 到长期不可变镜像，不得自动换 Nightly | M0-A/M8 |
| 固定 Artifact ZIP SHA 未独立复验 | 执行错误/被修改 compiler | 下载后校验 419652152 字节与完整 SHA-256，失败阻断 | M0-A |
| Run HEAD 关联被误称独立签名 Attestation | 供应链证明等级被夸大 | 明确官方 Workflow 证据与独立签名 provenance 的差异 | M0-A/M8 |
| Artifact 解包可能缺失 x-bit/链接语义 | compiler 不可运行/执行错误文件 | 受审计白名单恢复、Manifest + Hello APE 探针 | M0-A |
| 自动 fallback 滚动 `cosmo.zip` | 与选定构建不同，难复现 | 仅允许选定 Artifact ID 或字节完全相同的镜像 | 全阶段 |
| `.cache` / GitHub cache 错误命中/投毒 | 执行未受信 compiler | SHA 命名/深校验、PR 不可提交可信 cache | 全阶段 |
| QuickJS `VERSION` 被误当源码身份 | 字节码/ABI/行为漂移 | Commit、Tree、VERSION、patchset 分别核验 | M0-A/M1 |
| QuickJS 与固定 cosmocc 不兼容 | 编译/ABI 崩溃 | 最小 Engine probe、overlay/补丁清单、升级专题 | M0-A/M1 |
| Windows Socket/HANDLE/pipe 不同等待语义 | Reactor/TTY 阻塞或忙等 | M0-C 早期真实 Probe，backend 输出统一 completion | M0-C/M2 |
| ARM64 64KB page 不兼容 | 无法启动/崩溃/资源错误映射 | 原生优先/完整系统 guest 实测 loader/pthread/mmap/ZIP，执行方式分别记录 | M0-C/M8 |
| Reactor owner/thread 边界错误 | GC/UAF/死锁 | non-owner 禁 JS API、代次 token、销毁/迟到完成用例 | M2 |
| Node ESM/CJS 动态依赖不可静态解析 | APE 运行时缺包 | 显式 allowlist/报错/差分，拒绝隐式下载依赖 | M3/M6 |
| TLS 旧版库和安全公告 | 证书误验或 CVE | 锁真实库版本、安全 gate、负向证书用例 | M4/M8 |
| ZIP Bomb、越界、重复名、路径逃逸 | OOM/恶意输入访问宿主 | 严格 ZIP/ZIP64 parser、配额、fuzz | M5 |
| 字节码缓存跨 QuickJS Commit 复用 | 不兼容或不安全执行 | bytecode 绑定 commit+patch+flags，不信任外部字节码 | M1/M5 |
| `mmap(/zip/...)` 实际匿名复制 | 大内存增长 | 原始 APE STORE aligned mmap/pread fallback | M7 |
| Windows 超 4GB APE | 可执行文件运行失败 | 外置 `assets.qap`，ZIP64/PE 约束分开验证 | M7 |
| APE ZIP EOCD 与代码签名冲突 | 打包后验签或加载失败 | 签名/ZIP 改写顺序实测，特殊发行形态分开报告 | M6/M8 |
| QJB 误被宣称 Sandbox | 不可信 JS 具有 OS 权限 | 清晰权限模型和 OS 级隔离要求 | M3/M5 |
| 只靠编译或把模拟结果冒称 native | 虚假的已支持平台 | 同字节真实机器/VM/64KB 内核证据与 `not_run` 状态 | M0-B/C/M8 |
| `actions/cache` 被当作永久存档 | 清理后无法离线重建 | 原始 ZIP 长期镜像，干净 build，分别保存 CI evidence | M8 |

### 19.1 不可接受的捷径

绝不能：把多平台单独构建的不同程序 ZIP 在一起冒充一个 fat APE；未核验锁定身份、组件和模式就执行缓存里的 compiler；在原 Artifact 无法下载时改用滚动 Nightly；用 `VERSION` 文本代替 Git SHA；在 Windows 直接把 Console/Pipe/SOCKET 当成相同 `poll` 对象；用 qemu-user 或未实测页大小的模拟结果宣称 ARM64 64KB 支持，或将完整 TCG guest 冒称物理 native；给未实现的 Node API 返回空值冒充兼容；通过关闭 TLS 验证解决测试；整体解压 QJB 到匿名内存；以已声明能力却没有真实证据的形式发布。

---

## 20. 文档、能力矩阵和质量门禁组织

采用 `docs/INDEX.md` 为唯一入口，将方案/规范与机器生成的锁、能力矩阵和证据对齐，不把上下游 HEAD 或编译器来源写在互相冲突的 Markdown 位置。

```text
docs/
├── INDEX.md
├── PRODUCT.md
├── ARCHITECTURE.md
├── adr/
│   ├── ADR-001-ape-same-byte.md
│   ├── ADR-002-quickjs-locked-source.md
│   ├── ADR-003-fixed-cosmocc-artifact.md
│   ├── ADR-004-embed-sdk.md
│   ├── ADR-005-cross-platform-foundation-gates.md
│   └── ADR-006-cache-as-acceleration.md
├── specifications/
│   ├── host-loop-v1.md
│   ├── embed-sdk-v1.md
│   ├── node-compat-v1.md
│   ├── module-resolution-v1.md
│   ├── qjb-v1.md
│   ├── app-ape-v1.md
│   ├── native-host-app-v1.md
│   └── large-assets-v1.md
├── engineering/
│   ├── toolchain-lock-v3.md
│   ├── cosmocc-artifact-41169443573.md
│   ├── cache-policy-local-ci.md
│   ├── foundation-m0-a-b-c.md
│   ├── github-actions-fast-cross-full.md
│   └── dependency-bump-process.md
├── security/
│   ├── threats.md
│   ├── tls-admission.md
│   ├── source-lock-provenance.md
│   └── release-signing.md
└── delivery/
    ├── supported-platforms.md
    ├── capability-matrix.md
    ├── sdk-abi.md
    └── release-criteria.md
```

`toolchains/qaplet.lock.json` 是代码与工具链输入唯一事实源；`compat/matrix.json` 是 API 支持唯一声明源；SDK header/CLI help/输出 manifest 与文档由 CI 交叉验证。`make deps-verify` 在本地与 Actions 必须是同一规则；任何 GitHub 缓存恢复路径必须在校验失败时 fail-closed。Agent 工作流、自动评审与文档索引可以辅助，但核心构建、测试和发行证明必须由独立 Make/CI 实际执行。

---

## 21. v1 产品验收清单（发行前逐项核对）

### 21.1 固定源码、工具链与三道 M0 门

- [ ] QuickJS Git Commit `2833510432045f72915919cef680ba59e5153b16`、Git Tree、VERSION `2026-06-04`、补丁摘要已经相互独立检查，并有审计证据。
- [ ] Cosmopolitan 工具链**只选择** Run `41169443573` / Artifact `13031425078`；Run HEAD Commit `aefad27c09ffda64c6ac64bd69f758e9f9c7b91d` 与下载源记录一致。
- [ ] 原始 GitHub Artifact ZIP 字节数 `419652152`、独立实算 SHA-256 `f2f984d3a9790674fc0da11c1c00a32d4fe3f8dafde99af74b0c6dc24a2a16cf` 匹配；保存长期不可变镜像。
- [x] `selected_pending_validation -> pinned` 的变更记录包括 ZIP 模式/链接审计及必要恢复、提取组件 Manifest、工具链与目标引擎/APE 运行探针。
- [x] M0-A Linux x64 QuickJS/APE/ESM/Promise 与微型 C Host 能运行，不依赖先完成 SDK；见 [03 验收](../../.scratch/qaplet-v15-alignment/esm-promise-validation.md)。
- [ ] M0-B 同一 fat APE 跨 Windows/macOS/ARM64 运行且 SHA 相同，helper/仿真/原生模式有区分。
- [ ] M0-C Windows I/O/Process 与 ARM64 Linux 64KB 原生/完整系统 guest 的 Loader/TLS/mmap/ZIP ABI 提前验收，M2 OS 合同才被冻结。

### 21.2 通用 Runtime / SDK / Node

- [ ] 单 Runtime 的 JS callbacks 只在 owner 线程，Reactor/WorkPool/CompletionQueue/取消/关闭没有 UAF/死锁。
- [ ] 通用 C Embed SDK 版本化 API、Native Module 注册、POD post/wake/Context 销毁合同按阶段在对应实际目标环境通过测试，执行方式有标识。
- [ ] Node CJS/ESM/resolver、FS/Path/Process/TTY/Buffer/Stream 等目标子集有固定 Oracle 的机器可读差分证据。
- [ ] Clack 与精选纯 JS CLI/TUI 真实运行，任何 `unsupported` 不以空 stub 掩盖。
- [ ] 安全 TLS 默认 CA/hostname/SNI 校验，HTTP/SSE/WebSocket 仅宣称通过的范围。

### 21.3 QJB / App APE / 大资源

- [ ] QJB 严格 ZIP/ZIP64 安全解析、按需读取，无整体匿名快照/临时目录解压。
- [ ] `qaplet pack/compile` 至少三个真实独立 CLI/TUI 可跨已声明 OS 同字节运行。
- [ ] 通用 Native Host C 示例利用 SDK 静态链接一个含 JS/QJB 的可携带 App，没有隐式下载 native addon。
- [ ] 未支持 `.node`/动态 require/超限资源有清晰构建诊断与能力矩阵。
- [ ] 大资源 mmap/外置 ZIP64/Windows 4GB 边界严格实测，未达标标 `experimental`。

### 21.4 `.cache` / GitHub CI / 供应链

- [ ] `.cache/` 被 Git 完整忽略；local/CI 使用同一 lock、下载、SHA、物化/权限修复与验证器。
- [ ] GitHub CI Fast/Cross/Full 三层运行且结果可审计，PR 缓存来源视为不可信，校验不被 `cache-hit` 绕过。
- [ ] Cache 错误 SHA、ZIP 损坏、原件过期且无镜像、解压权限缺失、源码 Commit/Tree 不符等负向测试全部 fail-closed。
- [ ] 预热已验源/原始 ZIP 后能离线清除编译缓存重建；发布有 SBOM、licenses、source/toolchain lock/provenance、SHA、真实宿主报告。
- [ ] 官方 Run 工作流关联不被宣称为独立签名 Attestation；若要求更高级供应链认证需单独获取证据。
- [ ] 真正已声明的 Tier-A 平台全部通过，尤其 Linux ARM64 64KB 真实内核（原生或完整系统 QEMU）；任何 `not_run` 不得写作 verified。

---

## 22. 本计划所依赖的主要源码与规范索引

本版固定了选定的 GitHub Actions Build/Artifact 身份，原 ZIP 与原工具链在既有 M0 已下载、独立校验并运行探针。任务 01–05 已完成：统一 v3 迁移、目标 QuickJS 与 M0-A ESM/Promise 增量已晋级 pinned；[04](../../.scratch/qaplet-v15-alignment/cross-host-revalidation.md)记录所验输入的六目标 Core、页补缺及严格回归，[05](../../.scratch/qaplet-v15-alignment/tiered-ci-validation.md)记录 Fast/Cross/Full 实际执行。后续配方变化须重新验收其新输入。06 原件镜像发布门禁由用户暂缓，release-ready 仍未满足；正式 M1/SDK 尚未实现。下面链接是计划输入/工程参考，不得替代 Release 原始证据。

### QuickJS / Cosmopolitan / APE

1. [Bellard QuickJS](https://github.com/bellard/quickjs)；[本次选定 Commit `2833510...`](https://github.com/bellard/quickjs/commit/2833510432045f72915919cef680ba59e5153b16)，[版本文件 `VERSION`](https://github.com/bellard/quickjs/blob/master/VERSION)。
2. [Cosmopolitan 源码](https://github.com/jart/cosmopolitan)；[选定构建的 Run HEAD Commit](https://github.com/jart/cosmopolitan/commit/aefad27c09ffda64c6ac64bd69f758e9f9c7b91d)。
3. **唯一指定工具链**：[Nightly cosmocc Run `41169443573`](https://github.com/jart/cosmopolitan/actions/runs/41169443573)、[Artifact `13031425078`](https://github.com/jart/cosmopolitan/actions/runs/41169443573/artifacts/13031425078)、[该 Commit 的 workflow](https://github.com/jart/cosmopolitan/blob/aefad27c09ffda64c6ac64bd69f758e9f9c7b91d/.github/workflows/nightly-cosmocc.yml)、[package.sh](https://github.com/jart/cosmopolitan/blob/aefad27c09ffda64c6ac64bd69f758e9f9c7b91d/tool/cosmocc/package.sh)。
4. [Cosmopolitan APE Specification](https://github.com/jart/cosmopolitan/blob/master/ape/specification.md)、[cosmocc Toolchain README](https://github.com/jart/cosmopolitan/blob/master/tool/cosmocc/README.md)。
5. [Cosmopolitan ZipOS get/open/mmap](https://github.com/jart/cosmopolitan/tree/master/libc/runtime)、[定制 Mbed TLS 来源说明](https://github.com/jart/cosmopolitan/blob/master/third_party/mbedtls/README.cosmo)。

### 工程 / 兼容与大资源

6. [txiki.js](https://github.com/saghul/txiki.js)（参考 Host/生命周期/JS-C 桥）；[Redis ae](https://github.com/redis/redis/blob/unstable/src/ae.c)（借鉴哲学，复用代码需核许可）；[AWS LLRT](https://github.com/awslabs/llrt)（Node 能力矩阵）。
7. [历史 quickjs-cosmo](https://github.com/moorefu/quickjs-cosmo)（仅旧问题/测试素材）。
8. [llamafile](https://github.com/mozilla-ai/llamafile)、[zipalign](https://github.com/jart/zipalign)（ZIP64/STORE/资源映射/对齐实践）。
9. [GitHub Actions Cache 原理](https://docs.github.com/en/actions/concepts/workflows-and-actions/dependency-caching)、[actions/cache](https://github.com/actions/cache)、[upload-artifact](https://github.com/actions/upload-artifact)（缓存/ZIP 权限/Artifact 存档限制）。

---

## 23. 最终实施决议（供新仓库冻结）

**产品**：Qaplet = Portable JavaScript Runtime & Application Platform。唯一 JS 引擎为锁定的 Bellard QuickJS 原版主干快照；native 工具链正式选择 **GitHub Actions Run `41169443573` / Artifact `13031425078`**，不随 Cosmopolitan Nightly 滚动发布漂移。目标交付可运行的 fat APE、Node/Web API 分级兼容、ESM/CJS、通用 Loop/Process/TTY/Transport、QJB、应用编译器、大资源安全映射与 C Embed SDK。**当前计划不开发任何业务层应用；既有应用私有 probe/脚本/测试/workflow 保留为参考。**

**工具链身份**：Cosmopolitan Run `head_sha=aefad27c09ffda64c6ac64bd69f758e9f9c7b91d`；原始 Artifact ZIP GitHub 记录的 SHA-256 为 `f2f984d3a9790674fc0da11c1c00a32d4fe3f8dafde99af74b0c6dc24a2a16cf`、大小 `419652152` 字节。QuickJS commit `2833510432045f72915919cef680ba59e5153b16`、`VERSION=2026-06-04`；Commit/Tree/VERSION/patch 及 Archive SHA/组件 Manifest 独立核验。未经实测前锁状态是 `selected_pending_validation`，不能写成已完成的 release build。

**实施顺序**：统一 v3 输入锁 → 目标 QuickJS 升级/补丁复核 → M0-A Linux x64 Hello APE + ESM/Promise 微型 Host；与 M1 独立语言层开发并行推进 M0-B Windows/macOS/ARM64 同字节启动以及 M0-C Windows IO/ARM64 64KB ABI 探针；之后冻结 Host/SDK OS 接口，逐层完成 Runtime/Node/Network/QJB/Compiler/Assets，并持续运行分级 Fast/Cross/Full CI。

**缓存/发行**：原始 Artifact 在过期前保存不可变镜像，发布前完成恢复验收；M1 可先使用逐次校验的已有缓存。`.cache/` 本地/CI 只作可再生成性能加速；解压后审计实际权限/链接，仅在确有缺失时按受审计规则恢复，再做真实编译探针；缺失/哈希不符阻断构建，严禁换滚动 Nightly 冒充原件。Release 交付 source/toolchain identity、能力矩阵、SBOM、许可证、实际 OS/CPU/64KB 内核与 native/emulated-system 结果、离线 clean rebuild 和 APE 同字节证据。

**发布标准**：以**真实平台可执行性、声明 API 的语义正确性、资源/安全边界和构建可验证性**为准；不通过“功能条目数”“QuickJS VERSION 字符串”“一次交叉编译成功”或“缓存命中”来判断 Qaplet v1 已完成。
