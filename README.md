# Qaplet

可移植 JavaScript 应用运行时与应用交付平台。当前仅实现 M0 的 BOOT-01 工程骨架；正式 Runtime、Core APE、QuickJS 与 Nightly 构建仍待后续任务。

本地开发需要 Python 3.10+、GNU Make，以及可执行的 GCC 或 Clang 系统 C 编译器。无需安装 Python 包；这些仅是开发依赖。

```sh
make doctor             # 环境报告；前置未齐时非零退出
make test-bootstrap     # 编译并执行真实 C 程序，成功仅代表 bootstrap
make test-foundation    # 完整 M0 检查入口；当前因产品输入/探针未实现而退出非零
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

报告和逐次运行的制品/原始日志保存在 `out/`，不提交到 Git。退出码：`0` 是当前 scope 通过，`1` 是失败或证据写入失败，`2` 是 blocked/not_run。GNU Make 会将 recipe 的非零状态转为自身的非零状态；精确检查 CLI 退出码可直接调用 `python scripts/foundation.py ...`。

入口、报告字段、WSL 权限上下文和 CI 预留位置见[本地开发说明](docs/development.md)。[M0 spec](.scratch/qaplet-m0/spec.md)、[任务索引](.scratch/qaplet-m0/README.md)和[ADR](docs/adr/)定义产品验收；bootstrap 成功不关闭 M0。自有代码采用 [MIT](LICENSE)，[第三方许可](THIRD_PARTY_NOTICES.md)单列。
