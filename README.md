# Qaplet

可移植 JavaScript 应用运行时与应用交付平台。当前 M0 已有工程/环境验收、固定 QuickJS 与官方 Nightly 输入，以及 Hello / 最小 QuickJS eval fat APE 构建和本地 Linux x64 执行证据。正式 Runtime、SDK、完整跨宿主矩阵及 Core 门禁仍待后续任务。

本地开发需要 Python 3.10+、GNU Make，以及可执行的 GCC 或 Clang 系统 C 编译器。无需安装 Python 包；这些仅是开发依赖。

```sh
make check              # 提交前：静态 CI 检查、合同测试和真实 C bootstrap
make doctor             # 环境报告；前置未齐时非零退出
make test-bootstrap     # 编译并执行真实 C 程序，成功仅代表 bootstrap
make test-foundation    # 完整 M0 检查入口；当前必需矩阵/聚合探针未齐，退出非零
make test               # 命令、报告和负向路径合同测试
make check-ci           # 本地 CI 语法和编排合同检查，不触发远程运行
```

Windows 若没有 `make` 别名，可使用现有 `gmake`，或显式运行 `C:\msys64\usr\bin\make.exe`。示例：

```powershell
& 'C:\msys64\usr\bin\make.exe' test-bootstrap
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
# WSL 首次填充工具链另加 TOOLCHAIN_BACKING_DIR=/mnt/c（本机 VHD 所在盘）。
make build-ape OUT=out/m0
```

`build-ape` 完全离线，先后校验 `.cache/quickjs/` 和 `.cache/toolchains/cosmocc/`，在 `.cache/build/<report-id>/` 生成 `hello.com` / `quickjs-eval.com`，将原始字节、两种 CPU 的 ELF sidecar、link map、真实底层命令日志和 `build-info.json` 保存到 `OUT`。输出目录须为空或不存在。QuickJS 只验证 create/eval/destroy；Temporal/Intl 显式不启用，补丁清单为空。默认表达式输出 `42`，单个表达式参数可验证异常退出 `1`；`--facts` 输出实际架构与页大小。CPU slice 存在不代表 ARM64 已运行。

`QUICKJS_CACHE`、`TOOLCHAIN_CACHE`、`APE_BUILD_DIR` 可指向当前主仓库 `.cache/` 下的缓存和构建根目录（例如 worktree 共享主仓库输入）。复跑使用新的 `OUT`。独立检查已有制品时运行 `python3 scripts/build_ape.py inspect --artifact out/m0/artifacts/hello.com --expected-sha256 <build-info中的摘要> --report out/inspect.json`；它检查 fat headers、两个 sidecar、link map、链接符号和最终 PE 导入边界。完整合同的真实构建用例通过 `QAPLET_TEST_SOURCE_CACHE=/repo/.cache/quickjs make check` 显式启用，跨 worktree 可另设 `QAPLET_TEST_TOOLCHAIN_CACHE` 和 `QAPLET_TEST_BUILD_DIR` 指向主仓库 `.cache/`。

`.github/workflows/m0.yml` 先运行本地检查，显式填充固定输入，再复用 `make build-ape` 并上传原制品与证据。后续测试 job 消费这些字节；完整矩阵由任务 06 接入。实际本地结果见 [BOOT-04A 验收记录](.scratch/qaplet-m0/fat-ape-build-validation.md)。

普通报告和逐次运行的制品/原始日志保存在 `out/`，不提交到 Git；ENV-01 的 Linux 构建证据默认放在 `~/.cache/qaplet/environment/`，JSON 报告仍写入 `OUT`。其复跑命令和显式 HTTPS 下载参数见[本地开发说明](docs/development.md#env-01-环境验收与目录)。退出码：`0` 是当前 scope 通过，`1` 是失败或证据写入失败，`2` 是 blocked/not_run。GNU Make 会将 recipe 的非零状态转为自身的非零状态；精确检查 CLI 退出码可直接调用 `python scripts/foundation.py ...`。

入口、报告字段、WSL 权限上下文和 CI 预留位置见[本地开发说明](docs/development.md)。[M0 spec](.scratch/qaplet-m0/spec.md)、[任务索引](.scratch/qaplet-m0/README.md)和[ADR](docs/adr/)定义产品验收；bootstrap 成功不关闭 M0。自有代码采用 [MIT](LICENSE)，[第三方许可](THIRD_PARTY_NOTICES.md)单列。
