# Ubuntu、WSL 与物理缓存

从[开发入口](../development.md)进入本页；环境诊断、compiler/cache 选择和权限问题在此核对执行上下文。现有真实失败与 A/B 记录继续保留在 `.scratch/qaplet-m0/`。

## 当前构建与缓存约定

2026-11-01 用户确认：构建统一在 Ubuntu 完成，其他平台验证已生成的同一摘要制品与 SDK。包含编译的 `make check`、合同回归及 bootstrap CI 因而在 Ubuntu 执行；Windows/macOS 使用对应验证入口消费制品。

可复用源码、工具链原归档/解包树、guest 输入和构建中间文件统一保存在仓库根目录 `.cache/`，报告与最终制品使用 `out/`。当前主工作区对应 `D:\Workspace\dairn\qaplet\.cache` / `/mnt/d/Workspace/dairn/qaplet/.cache`；多个实施 worktree 的实际运行应显式使用这一个主工作区缓存。CI 使用其检出目录下的 `.cache/`。缓存目录已被 Git 忽略；默认构建离线校验已锁输入，显式获取操作负责填充缓存。

本机该目录位于没有 `metadata` 选项的 DrvFS。实际执行、读写和内部符号链接可用；文件可设为无写权限，但非执行位和目录只读位不能按 POSIX 原样表示。工具链验证须在报告中记录实测权限限制，并持续核对原归档 SHA-256、所有文件内容/类型和链接目标；在支持 POSIX 权限的 Ubuntu 文件系统保留完整权限校验。没有更改全局 WSL/mount 设置。下文与历史验收记录中的 ext4/home 缓存、Windows 编译和旧 CI 矩阵是此前观测，当前默认与调度以本节及配置为准。

缓存候选目录改名的另一次 `EACCES` 已通过现场 Windows Restart Manager 查询定位到 VS Code 的 Pylance 文件占用。仓库 `.vscode/settings.json` 排除 `.cache/`、`out/`、`toolchains/evidence/` 的递归监听；同一 CLI 的加入/移除/恢复对照及原始完整检查已验证，见[诊断记录](../../.scratch/qaplet-m0/drvfs-publication-validation.md)。这不改变缓存位置、权限或输入验证，也不增加改名重试；其他程序主动占用文件仍可能导致 OS 拒绝操作。

## WSL 入口与权限上下文

`dev.py` 在执行前探测实际临时根可写和可用空间，默认最低 64 MiB，可用
`--min-temp-free-bytes` 提高。`--temp-root` 仅设置此次子进程的 TMPDIR/TEMP/TMP，
不修改全局环境。Windows C 盘临时空间不足时可显式选择 D 盘的 `out/.../temp`。
该测量不推断 WSL VHD backing 盘空间，也不是完整构建峰值容量证明。

`check --native-fs --temp-root /tmp --output-dir out/check-NAME` 先验证 POSIX 写权限位，
再将当前已跟踪和未忽略的源文件逐字快照到一次性目录，执行原 `make check`，
退出时复制报告并清理快照。源字节清单和日志留在新 out 目录；这不是 SDK 供应源。
缓存参数仍指向主工作区物理 .cache。原始 DrvFS checkout 和挂载设置保持不变。

长构建先收口源文件、提交并固定 head，再用
`build-sdk --source-ref FULL_SHA --output-dir out/sdk-NAME`。入口在新目录独立检出
该提交，保存并前后核对源文件清单；工作区后续编辑不进入供应。独立检出和产物均
保留在 out，工具链/源码/中间缓存仍显式使用主工作区 .cache。需要构建的未提交改动
应先提交；不能用 dirty 工作区配方或旧供应报告替代。输出目录和报告必须是新路径。

Windows 默认优先使用已安装的 `%ProgramFiles%\WSL\wsl.exe`，再回退 PATH；`--wsl` 或 `make doctor WSL=...` 可显式指定入口。只读取快捷方式目标不足以判断发行版是否可用：MSI 广告快捷方式可能先指向 Installer 图标资源。

诊断时先记录所用 exe、执行上下文、命令退出码和原始日志。查询失败的结论限定于当前进程可见范围。用户提供另一入口或说明已安装时，用该入口在可访问发行版的用户上下文只读复核，并保留两次观测。环境就绪的证据是实际 Linux kernel、执行架构和页大小；系统 C bootstrap 另行验证编译/执行。

2026-10-31 的实际核验发现，受限 agent 进程与普通用户进程对 WSL 的可见结果不同：前者可能返回无发行版/拒绝访问，后者可进入 Ubuntu-22.04。doctor 因而只报告当前执行上下文的观测，保留每个入口的日志；不因 `wsl.exe` 存在就判定环境就绪，也不将受限查询失败外推为系统未安装。具体复核见[环境记录](../../.scratch/qaplet-m0/environment-check.md)。

Linux 下可直接在仓库根目录运行相同 make 入口。Windows 工作目录 `D:\Workspace\dairn\qaplet` 在此次 WSL 中映射为 `/mnt/d/Workspace/dairn/qaplet`，不假定其他机器盘符相同。ARM64 4K/64K 与此 WSL x64 4K 分开计数。

### 既有 Markdown 换行与跨 OS Git 审计

2026-11-06 的 M2-23 独立接收发现：主工作区103份历史Markdown保留CRLF，Ubuntu以
`core.autocrlf=false`检查时标记修改，Windows缓存状态显示clean。逐文件原始字节核验
证明这103份仅CRLF转LF后与HEAD blob逐字相等，合前/合后/接收后的原始字节均未改变；
父M2规格和产品配方保持原字节。原件与失败观测见主`out/qaplet-m2/root-eol-23/`及
`out/qaplet-m2/merge-23-sdk-delivery/`，不能把Windows缓存clean解释为原始字节相等。
确认上述精确差异后，可用单次`git --no-optional-locks -c core.autocrlf=input status`
检查文本语义状态；Python入口的子Git可用该次进程的`GIT_CONFIG_COUNT/KEY_0/VALUE_0`
传入同值并记录实际命令。不写全局或仓库配置、不重写历史文档，也不将该处理泛化为
忽略其它内容变化。专属worktree以LF检出，产品输入仍须raw/Git/供应快照逐字核验。

## ENV-01 环境验收与目录

复用现有 Ubuntu-22.04；不需要再次安装 WSL。以普通 Linux 用户在上述源码目录运行：

```sh
make test-environment OUT=out/env01/wsl-final \
  ENV_DOWNLOAD_URL=https://mirrors.tuna.tsinghua.edu.cn/ubuntu/dists/jammy/InRelease
python3 scripts/foundation.py validate-report \
  --input-report out/env01/wsl-final/environment.json \
  --report out/env01/wsl-final/validated.json
```

此 URL 仅作 HTTPS 下载探针，保存实际长度/摘要，不作为产品依赖锁。省略 URL 会得到 blocked；不自动联网。GitHub TLS 失败和软件源超时等原始记录也保留，不能由另一端点通过推断所有下载站点可达。

默认构建与逐命令证据放在仓库 `.cache/environment/<uuid>/`，JSON 报告放在 `OUT` 下。`ENV_BUILD_DIR` 可用于 worktree 显式选择主工作区 `.cache/environment`；构建目录不可创建时在报告旁保留 blocked 证据。tar.gz 与 ZIP 对仓库 C 源码做真实创建、解包和 SHA-256 往返验证；HTTPS 下载另作独立用例。系统 C 输出的架构与 sysconf 页大小须与宿主测量一致。新 login shell 重新检查工具与两个目录；这不要求终止发行版或重启机器。

验收默认要求源码盘、构建文件系统以及显式提供的 backing 盘各至少 2 GiB 可用空间；CLI `--min-free-bytes` 可提高阈值。这是小型环境探针的下限，后续 Nightly/QuickJS/QEMU 任务应按实际归档、解包、缓存和构建峰值另算空间。当前源码与缓存都在 D 盘；历史 ext4 缓存的 VHD 在 C 盘，其 `/mnt/c` 测量不能当作当前缓存盘。仅当确有该存储依赖时显式提供 `ENV_BACKING_DIR`，不能从 ext4 显示的虚拟容量反推物理空间。

当前源码与缓存都在 DrvFS（`/mnt/d`）。历史 ENV-01 另测过 ext4 构建目录，其通过不表示 Windows ACL、chmod、符号链接或大小写语义完全一致；当前权限限制见本节开头。历史环境指纹见[ENV-01 验收记录](../../.scratch/qaplet-m0/wsl2-environment-validation.md)及[本地覆盖记录](../../.scratch/qaplet-m0/local-coverage.json)。环境验收与产品 APE/ARM64 成绩分别判定。
