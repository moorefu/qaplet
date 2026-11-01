# Qaplet

可移植 JavaScript 应用运行时与应用交付平台。当前路线见[文档入口](docs/INDEX.md)和 [v1.5 执行计划](docs/plans/qaplet-v1-implementation-plan-v1.5.md)。既有 v1.4 M0 已完成固定输入、Ubuntu fat APE、六目标启动/I/O、ARM64 64K 和断网合同；原结论由 [Core gate](.scratch/qaplet-m0/core-evidence-gate-validation.md) 保留。

正式 M1 前先完成[统一 v3 锁、QuickJS 目标升级和 M0-A ESM/Promise 增量](.scratch/qaplet-v15-alignment/README.md)。当前构建仍使用既有分锁；新计划和选型样例不代表已经切换输入。具体业务应用退出当前交付主线，已完成的服务相关工作保留作参考。

Ubuntu 本地构建需要 Python 3.10+、GNU Make，以及可执行的 GCC 或 Clang 系统 C 编译器。无需安装 Python 包；其他平台使用已生成制品的验证入口。

```sh
make check              # 提交前：静态 CI 检查、合同测试和真实 C bootstrap
make doctor             # 环境报告；前置未齐时非零退出
make test-bootstrap     # 编译并执行真实 C 程序，成功仅代表 bootstrap
make test-foundation    # BOOT-01 保留的缺口报告；不能签发完整 Core 通过
make test               # 命令、报告和负向路径合同测试
make check-ci           # 本地 CI 语法和编排合同检查，不触发远程运行
```

Windows 可用现有 `gmake` 或 `C:\msys64\usr\bin\make.exe` 做静态检查和环境盘点；构建与包含编译的合同回归进入 Ubuntu。示例：

```powershell
& 'C:\msys64\usr\bin\make.exe' check-ci
& 'C:\msys64\usr\bin\make.exe' doctor WSL='C:\Program Files\WSL\wsl.exe'
```

`PYTHON`、`CC` 和 `WSL` 都表示单个可执行文件路径，不能包含 shell 命令或 flags。`OUT` 可分别保存 Windows/WSL 的报告：

```sh
make test-bootstrap OUT=out/wsl
make doctor OUT=out/wsl
```

产品构建统一在 Ubuntu 中执行；其他平台消费同一制品并验证 SDK。所需输入和构建中间文件统一保存在本仓库 `.cache/` 实体目录，报告和分发制品保存在 `out/`。首次先显式获取锁定输入；Nightly 原 ZIP 必须匹配 `toolchains/cosmocc.lock.json`，不能换成任意最新版本：

```sh
make quickjs-fetch OUT=out/inputs
# 已有匹配 ZIP 时直接使用；也可显式用下列 helper 获取锁中 GitHub artifact。
# GH_TOKEN 仅从进程环境读取；归档过期/不可访问会失败，不切换滚动通道。
python3 scripts/fetch_locked_toolchain.py --archive .cache/downloads/cosmocc.zip --report out/inputs/acquisition.json
make toolchain-fetch TOOLCHAIN_ARCHIVE=.cache/downloads/cosmocc.zip OUT=out/inputs
# 只有缓存实际依赖 VHD 时才显式提供对应 backing 卷；当前缓存位于仓库 .cache。
make build-ape OUT=out/m0
```

`build-ape` 完全离线，先后校验 `.cache/quickjs/` 和 `.cache/toolchains/cosmocc/`，在 `.cache/build/<report-id>/` 生成 `hello.com` / `quickjs-eval.com`，将原始字节、两种 CPU 的 ELF sidecar、link map、真实底层命令日志和 `build-info.json` 保存到 `OUT`。输出目录须为空或不存在。QuickJS 验证 create/eval/destroy、pointer roundtrip、对象/Map 生命周期和错误路径；Temporal/Intl 显式不启用，ARM64 full-pointer overlay 独立锁定。默认表达式输出 `42`，异常保留实际非零状态；`--facts` 输出实际架构与页大小。

构建使用由锁定官方 `assimilate -e -x` 从原工具副本派生的 Linux ELF 工具视图，位于 `.cache/toolchains/cosmocc/official-assimilate-linux-x86_64-v1/<archive-sha>/tree`。它覆盖全部可执行 MZ helper 和 GNU 内部程序，防止 WSLInterop 将编译器 helper 当成 Windows 程序运行；不修改全局 WSL/binfmt 配置。原归档、原缓存与锁保持不变，派生工具身份单列于 `native_toolchain`，每次从原始 header/完整 body 独立重算预期摘要，不能用旁边的可写 manifest 代替验证。最终交付仍是 fat APE，实际字节与原构建一致。可用 `python3 scripts/build_ape.py prepare-toolchain --report out/native-toolchain.json` 单独预备并验证此视图。

`QUICKJS_CACHE`、`TOOLCHAIN_CACHE`、`APE_BUILD_DIR` 可指向当前主仓库 `.cache/` 下的缓存和构建根目录（例如 worktree 共享主仓库输入）。复跑使用新的 `OUT`。独立检查已有制品时运行 `python3 scripts/build_ape.py inspect --artifact out/m0/artifacts/hello.com --expected-sha256 <build-info中的摘要> --report out/inspect.json`；它检查 fat headers、两个 sidecar、link map、链接符号和最终 PE 导入边界。完整合同的真实构建用例通过 `QAPLET_TEST_SOURCE_CACHE=/repo/.cache/quickjs make check` 显式启用，跨 worktree 可另设 `QAPLET_TEST_TOOLCHAIN_CACHE` 和 `QAPLET_TEST_BUILD_DIR` 指向主仓库 `.cache/`。

`.github/workflows/m0.yml` 先运行本地检查，显式填充固定输入，再复用 Ubuntu producer 并上传原制品与证据。五个 native job 和只补缺页的完整 QEMU guest 消费相同 producer 字节；完整 CI `41271366261` 已成功，见 [Core 验收](.scratch/qaplet-m0/core-evidence-gate-validation.md)。

普通报告和逐次运行的制品/原始日志保存在 `out/`，可复用输入与中间构建保存在仓库 `.cache/`，均不提交到 Git。ENV-01 命令见[本地开发说明](docs/development.md#env-01-环境验收与目录)。foundation 退出码 `0` 是当前 scope 通过，`1` 是失败，`2` 是 blocked/not_run；GNU Make 保留非零失败含义。完整 Core 使用独立只读消费入口：

```sh
python3 scripts/core_gate.py evaluate --input-manifest out/core-inputs.json --report out/core-gate.json
```

它按显式 manifest 选择证据与原路径/迁移根映射，校验归档、实际原始日志、语义、身份、六目标、64K、离线 pair 和 link/import 边界；完整通过才退出 `0`。输入合同、实际复跑路径和独立 SVC 状态见 [Core gate 记录](.scratch/qaplet-m0/core-evidence-gate-validation.md)。

入口、报告字段、WSL 权限上下文和 CI 预留位置见[本地开发说明](docs/development.md)。[M0 spec](.scratch/qaplet-m0/spec.md)、[任务索引](.scratch/qaplet-m0/README.md)和[ADR](docs/adr/)定义产品验收；bootstrap 成功不关闭 M0。自有代码采用 [MIT](LICENSE)，[第三方许可](THIRD_PARTY_NOTICES.md)单列。

完整缓存填齐后，Ubuntu 用 `sudo make test-offline OFFLINE_CACHE_ROOT=/repo/.cache OUT=out/offline` 创建独立断网 namespace，再降为非 root 用户实际重构两次并逐字节比较。`OFFLINE_UID` / `OFFLINE_GID` 默认 1000；worktree 指向主仓库物理 `.cache/`。入口保留 exact cache inventory、实际网络/进程 trace、复制负向 fixture 和拒绝结果，缺工具/输入时失败且不联网修复。准备步骤、时限及独立 helper 范围见[断网重构说明](docs/development.md#boot-08-断网重构)。
