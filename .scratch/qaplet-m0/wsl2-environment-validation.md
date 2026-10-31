# ENV-01：现有 WSL2 构建环境验收

日期：2026-10-31（Asia/Shanghai）。任务：[02](issues/02-wsl2-build-environment.md)。复用现有 Ubuntu-22.04，没有安装、更新或重建 WSL，没有要求重启。环境通过不代表产品 APE 或完整 M0 通过。

## 环境与准备

普通用户上下文使用 `C:\Program Files\WSL\wsl.exe -d Ubuntu-22.04`，WSL 版本 2.6.3.0，发行版模式 2。受限 agent 的同一路径仍返回“没有发行版”，退出 4294967295；仅代表该进程的可见范围。两次观测分别保存在 `out/env01/restricted-inventory/`、`out/env01/user-inventory/`，没有据此重复安装。

| 项目 | 实测 |
| --- | --- |
| 发行版 | Ubuntu 22.04.5 LTS，普通用户 moore（uid 1000） |
| OS / 执行架构 | Linux / x86_64 |
| kernel | 6.6.87.2-microsoft-standard-WSL2 |
| CPU | Intel Core i9-13900K，Linux 可见 32 CPUs |
| 页大小 | Python OS API、getconf、系统 C sysconf 均为 4096 字节 |
| Make / GCC / Git | 4.3 / Ubuntu 11.4.0 / 2.34.1 |
| HTTPS | curl 7.81.0、OpenSSL 3.0.2，保留证书校验 |
| 解包 / 摘要 | tar 1.34、gzip 1.10、xz 5.2.5、unzip 6.00、zip 3.0、sha256sum 8.32 |
| Python | 3.10.12 |

原有 build-essential、ca-certificates、xz-utils 已安装。补齐 `unzip 6.0-26ubuntu3.2`、`zip 3.0-12build2`，新增约 930 kB；没有升级其他 45 个包或更改软件源。首次 `apt-get update` 在 180 秒超时，日志保留于 `out/env01/setup/apt.*`；检查旧进程已退出后，利用现有索引、有界网络超时安装成功，见 `apt-install-command.json` 与相应原始日志。没有待完成的人工权限或重启步骤。

## 目录与磁盘

Windows 编辑目录 `D:\Workspace\dairn\qaplet` 对应 Linux `/mnt/d/Workspace/dairn/qaplet`（DrvFS/9p）。构建与日志在 `/home/moore/.cache/qaplet/environment/<uuid>/`（ext4），JSON 报告写回源码树 `out/`。两处分别创建、读取和直接执行临时脚本；源码读取、tar/ZIP 往返摘要及新的 login shell 复核均通过。测试不外推 ACL、chmod、大小写或符号链接语义一致。

最终报告测得 D 盘可用 45,537,943,552 字节（约 42.4 GiB）；ext4 可用 1,014,781,485,056 字节是虚拟盘内可用容量。实际 VHD 位于 `C:\Users\moorefu\AppData\Local\Packages\CanonicalGroupLimited.Ubuntu22.04LTS_79rhkp1fndgsc\LocalState\ext4.vhdx`，C 盘可用 22,174,044,160 字节（约 20.7 GiB），也通过 `/mnt/c` 纳入磁盘用例。不能把虚拟盘的近 1 TiB 当成 C 盘物理余量。原始 Windows 存储复核见 `out/env01/user-inventory/storage.json`。

验收阈值为各盘至少 2 GiB，仅适用于环境探针；Nightly/QuickJS/QEMU 后续任务须另核对下载、解包、缓存和构建峰值。持续构建时建议保留额外余量，不能根据本次测量承诺任意构建容量。

## 命令与身份

复跑入口及参数见[开发说明](../../docs/development.md#env-01-环境验收与目录)。本次最终命令：

```sh
make test-environment OUT=out/env01/wsl-final \
  ENV_BACKING_DIR=/mnt/c \
  ENV_DOWNLOAD_URL=https://mirrors.tuna.tsinghua.edu.cn/ubuntu/dists/jammy/InRelease
```

ENV-01 报告 `out/env01/wsl-final/environment.json`：33/33 必需用例 pass，recipe 与 make 均退出 0；schema v2，`m0_core_status=not_run`。报告和独立原始日志都保留在本机，Git 中保存本记录与[本地环境覆盖摘要](local-coverage.json)，干净检出可重新运行。

| 身份 | 值 |
| --- | --- |
| report_id | `d4f2e768-dfed-4595-ae4b-b72fded8606a` |
| 输入集合 SHA-256 | `a4be2aa5b1ff14008e1aee52cbd1b0b2a7f2caa3baa4113f3e4e954e9b1a4507` |
| 报告 SHA-256 | `8299602cd9a533a1cc68070b76946916fca0042be058af252deaceba7c9abf1c` |
| 系统编译器 SHA-256 | `821af3c74506283c179ca413bb33e6b528805a4dd8a5c09df125e5ad560a9e89` |
| 系统 C 制品 SHA-256 | `2f52d13231ec669f99970143e59b33cbff7d4e97f60d4dc6f2ae4f7e3c49ea7f` |
| C 输出与退出码 | `answer=42, execution_arch=x86_64, page_size=4096`，编译/执行均 0 |
| HTTPS 下载 | 270087 字节；SHA-256 `c14060cd8c6d625874dfcb9523a35a395bf4865c28b6b6a82569ef326fe92dc6` |

编译实际执行 `/usr/bin/cc -std=c11 -Wall -Wextra -Werror tests/native/foundation_smoke.c -o <ext4证据目录>/foundation-smoke`（报告保存完整绝对 argv）。这是系统 C 冒烟，不是 APE；既有源代码和制品复用 BOOT-01，不引入假产品制品。

HTTPS 下载独立验证。tar.gz/ZIP 使用本仓库 C 源码真实打包、解包并与原文 SHA-256 比对。GitHub raw 下载曾失败（curl 35，TLS），失败报告 `out/env01/wsl/environment.json` 及其原始日志保持不变；Ubuntu 镜像通过不能证明 GitHub/未来 Nightly 站点可达。镜像 InRelease 会随上游更新，此下载仅为环境探针，不是产品输入锁。

## 验证与审查

`out/env01/validation/final-checks.json` 记录普通 Windows 与 WSL 的 `make check`：CI 静态检查、合同测试、真实 C bootstrap 均通过。初始完整批次各运行 23 项：WSL 22 通过/1 Windows 专用项 skip；Windows 20 通过/3 Linux 专用项 skip。随后补充 bootstrap 报告不能冒充环境通过的合同检查，最终数量见下方审查后验证。所有 skip 都是开发测试的平台边界，不计产品成功。

负向覆盖：缺工具、空间不足、构建目录不可写、下载失败、解包失败；既有编译器缺失、编译/执行错误与超时回归保留。ENV-01 新 scope 扩展报告为 v2，通过清单由验证器消费，旧 v1 仅可用旧验证器读取。独立 `validate-report` 与第二次 WSL login shell 工具/目录复核退出 0，见 `recheck-commands.json`。

审查后最终验证：Windows 与 WSL 的 `make check` 均退出 0，各运行 25 项合同测试，Windows 21 通过/4 Linux 专用项 skip，WSL 24 通过/1 Windows 专用项 skip；真实 noexec 测试在 WSL 实际通过。两端 CI 静态检查、C bootstrap 通过；WSL 再跑 ENV-01 33/33 和 `validate-report` 通过。对应命令及原始日志为 `out/env01/validation/windows-reviewed-command.json`、`wsl-reviewed-command.json`；Python 语法编译和 `git diff --check` 也通过。未推送或触发远程 CI。

## Standards

独立 reviewer 对照 AGENTS、tracker/triage/domain、开发说明、GLOSSARY、相关 ADR 和 Fowler smell baseline 审查：0 项文档标准违反，0 项实质设计异味。

## Spec

独立 reviewer 发现 1 项 P2：进程无法启动时，通用执行 helper 使 noexec 权限缺口被判为 fail，违反父规格的权限缺失应 blocked 合同。已在子进程私有 user/mount namespace 中使用真实 noexec bind mount 复现（`noexec-namespace-red`：CLI 1 != 预期 2），修正为 blocked 并停止依赖目录执行权限的后续探针；`noexec-green` 与完整回归通过。该挂载不改变发行版其他进程的挂载。Spec 复审确认无剩余发现。

最终发现数：Standards 0；Spec 0（修复 1 项 P2）。

## 覆盖与回退

实际覆盖仅为 Linux/x86_64/4096/native 的 WSL2 构建环境。未下载 QuickJS/Nightly，未执行 Core APE、ARM64 4K/64K、macOS、QEMU 或 SCM/Job，完整 M0 仍未完成；任务 04 可以消费这项环境前置，但其自身下载和锁验收仍独立执行。

回退仓库变更可撤销本次提交。只按报告 UUID 手动清理本任务的探针产物，不自动删除发行版、VHD、用户工作区或其他缓存。此次只新增 unzip/zip；如确需撤销，在确认其他项目不再使用后可在该发行版中移除这两个包，不运行 autoremove、不卸载 WSL。
