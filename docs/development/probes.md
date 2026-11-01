# 固定输入与产品探针

当前输入版本由[统一 v3 锁](input-lock.md)决定。下文的 source/Nightly/helper 分锁是被该入口精确摘要绑定的组件清单；生产 `bump`/rolling `download` 已禁用，显式更新先生成并验证 v3 候选。

共享执行脚本位于 `scripts/`，系统 C 冒烟位于 `tests/native/`，命令合同测试位于 `tests/contracts/`；没有创建未实现的 Runtime、SDK 或应用目录占位。

M0-SVC 的应用私有原生 SCM 探针现位于 `apps/svc/probes/`；该目录不是正式 Runtime/SDK 或 qaplet-svc 实现。`scripts/svc_scm.py build` 只在 Ubuntu 构建独立制品，`run --input-report <producer> --artifact <same-bytes>` 在 Windows 验证 SCM 生命周期，在 Linux 另需显式 `--loader <distributed-loader>` 验证普通/SCM 拒绝入口。`cleanup --plan <fixture-plan>` 是独立恢复入口，`check-ci` 检查独立 `.github/workflows/svc-scm.yml` 的 Ubuntu 构建、Windows 消费及 always 清理/保留合同。Windows driver 不自动提权；普通权限不足保留 blocked，当前开发宿主的真实临时服务操作依据用户本轮明确授权并经过必要 OS UAC。详细输入、时限、精确清理范围及实际本地结果见[SCM 记录](../../.scratch/qaplet-m0/scm-abi-validation.md)。

## 命令与 scope

| 入口 | 报告 | 成功含义 |
| --- | --- | --- |
| `make check` | 复用下列检查及其报告 | 提交前串联 CI 静态检查、合同回归、真实 C bootstrap |
| `make doctor` | `out/doctor.json` | 当前执行上下文的环境盘点；不签发完整 Core 成绩 |
| `make test-environment` | `out/environment.json` | ENV-01 Linux x64 工具、磁盘、目录、HTTPS/解包/摘要、系统 C 验收；须显式指定下载 URL |
| `make test-bootstrap` | `out/bootstrap.json` | 系统 C 编译/执行通过，不是 APE 或 QuickJS 成绩 |
| `make test-foundation` | `out/m0.json` | BOOT-01 保留的非通过缺口报告；完整 Core 改用独立 consumer gate |
| `make test` | unittest 输出 | 命令合同回归通过，不是产品矩阵 |
| `make check-ci` | `out/check-ci.json` | JSON/YAML 语法及项目编排合同通过，远程未运行 |
| `make build-ape` | `out/build-info.json` / `out/artifacts/` | QuickJS/Nightly/helper source 固定输入离线校验、Hello / QuickJS fat APE、双 CPU headers/link map/import 检查及 Linux x64 实际值/错误路径 |
| `python scripts/core_gate.py evaluate --input-manifest INPUT --report OUTPUT` | 显式 OUTPUT | 完整实际证据消费门禁；Core 通过才退出 0，独立 SVC 不改变退出码 |

这些 make recipe 与 CI 复用同一 Python CLI。也可直接执行：

```sh
python scripts/foundation.py test-foundation --profile bootstrap --cc cc --timeout 30 --report out/bootstrap.json
python scripts/foundation.py validate-report --input-report out/bootstrap.json
```

`validate-report` 消费 `compat/foundation-report.schema.json`，验证结构、版本、非空/唯一用例和基础状态不变量；它不验证外来报告是否真的执行，也不能代替任务 10 的完整证据门禁。`scripts/evidence.py` 只实现本 schema 使用的有限 JSON Schema 词汇，遇到不支持的词汇拒绝验证。扩展报告合同应同步版本和验证器。

`tests/contracts/bootstrap-cases.json` 是当前唯一必需 bootstrap 用例清单。清单缺失、空、重复或含未知用例都失败；缺 C 源码或编译器为 blocked，编译失败、输出不符、非零退出或超时为 fail。冒烟编译/执行每条命令默认限时 30 秒，可设为 1–300 秒；doctor 版本查询限时 10 秒，WSL 查询限时 15 秒。Windows 超时尝试终止进程树并兜底终止直接子进程；Unix 使用独立进程组清理。BOOT-01 不运行安装服务、SCM 或 Job 的测试。

## 固定输入入口（BOOT-02 / BOOT-03）

QuickJS 的权威锁、原始许可和有序补丁清单位于 `third_party/quickjs/`。普通使用先显式填充固定提交缓存，再离线校验：

```sh
make quickjs-fetch OUT=out/inputs/quickjs-fetch
make quickjs-verify OUT=out/inputs/quickjs-verify
```

`quickjs-fetch` 使用已提交的固定 commit，不解析移动 master；`quickjs-bump` 才显式重新解析官方 master 并更新锁。默认源码缓存位于 `.cache/quickjs/`，worktree 可用 `QUICKJS_CACHE` 指定主工作区缓存。构建先校验所选缓存，并在 `.cache/build/` 的独立目录生成中间文件。源码锁验证不是编译或跨宿主验收。

Nightly 入口是 `make toolchain-fetch`、`make toolchain-bump`、`make toolchain-verify`。权威锁为 `toolchains/cosmocc.lock.json`，默认缓存为 `.cache/toolchains/cosmocc/`，可用 `TOOLCHAIN_CACHE` 指定主工作区缓存。`verify` 和 `path` 完全离线；缺失缓存或身份不符时失败，不自动下载。当前 DrvFS 缓存直接测其物理卷空间；仅在实际使用 VHD 存储时另提供对应 backing 卷，不假定为 C 盘。

需要分开获取与 Linux 测量时，显式运行：

```sh
python scripts/nightly_toolchain.py download \
  --download-to .cache/downloads/cosmocc.zip \
  --report out/nightly-download/acquisition.json
python3 scripts/nightly_toolchain.py fetch \
  --archive /path/to/cosmocc.zip \
  --acquisition-report /path/to/acquisition.json \
  --report out/inputs/nightly-fetch.json
```

接收端重新比对获取报告里的原始长度与 SHA-256，再安全解包、测量编译器和完整内容。没有成功官方获取记录的本地 ZIP 仅属于 `local-import`，默认不能作为产品工具链使用。工具链 libc 与源码 HEAD 的对应关系无法证明时保持 `unproven`，不能把下载成功当成 APE 成功。

本轮官方发布站点在本地及 GitHub runner 都超时，最终从[官方 Nightly run 41169443573](https://github.com/jart/cosmopolitan/actions/runs/41169443573) 取得原始 `cosmocc` artifact。权威锁如实记录 `github-actions` 来源及制品摘要，获取记录保存在 `toolchains/cosmocc.acquisition.json`。新缓存可在取得锁中对应的原始 ZIP 后运行：

```sh
make toolchain-fetch TOOLCHAIN_ARCHIVE=/path/to/original-artifact.zip \
  TOOLCHAIN_ACQUISITION_REPORT=toolchains/cosmocc.acquisition.json \
  OUT=out/inputs/nightly-fetch
```

此 GitHub 来源的锁不会因缓存缺失而切换到滚动发布站点。已有权威锁时，精确匹配的 ZIP 副本也可独立用于恢复；首次建立新的官方来源必须提供可核查获取记录。`acquire-nightly.yml` 的一次性 integration push 获取已结束，后续仅显式手动触发；它保留官方发布站点下载的成功/失败日志和归档 7 天，不更新任何锁。取回 artifact 后仍须本地校验和 Linux 工具测量。

两种输入报告各自使用独立的版本化合同；现有 foundation `validate-report` 不验证它们。BOOT-09 的 `core_gate.py` 现消费 producer 前后 input reports、实际交付字节及完整执行证据；单独供应验证不关闭 M0。

## BOOT-04A fat APE 构建和报告

所有产品构建在 Ubuntu 里运行；其他平台只消费同摘要制品和验证 SDK。运行 `make build-ape OUT=out/m0` 默认读取仓库 `.cache/quickjs/` / `.cache/toolchains/cosmocc/` 实体缓存；可设 `QUICKJS_CACHE` / `TOOLCHAIN_CACHE` 指向主仓库 `.cache/`，缺失或损坏时保留 blocked/fail 报告，不获取网络输入。构建前、构建后分别执行两个供应脚本的离线 `verify`，四份输入报告及其摘要保存在同次 evidence 目录。输出目录必须为空或不存在；复跑指定新 `OUT`。

默认工作目录为仓库 `.cache/build/<report-id>/`，worktree 可用 `APE_BUILD_DIR` 指向主仓库 `.cache/build/`。该目录保留对象、失败现场和临时文件；输入目录不写入，不通过符号链接将缓存转移到仓库外。实际 DrvFS 权限/工具行为以迁移后的验证结果为准。`cosmocc` 本身不支持含空格的参数路径，构建在编译前检查这一限制。默认每个 compile/link 上限 300 秒，单次执行/审计 30 秒；CLI 可显式调整。超时终止进程组并保留日志。

实际诊断证明：WSLInterop 的 `MZ` 注册会把 Nightly 的 APE helper 送到 `/init` / Windows，Linux `/mnt/d/...` 路径因而失败；原生 Windows 错误退出实测 256，bridge 观测 0，与 8 位截断一致。构建不依赖这项注册的偶然存在与否，而在已校验原缓存之外，使用官方 `assimilate -e -x -o` 创建独立 Linux ELF 工具视图。视图位于工具链 cache root 下 `official-assimilate-linux-x86_64-v1/<archive-sha>/tree`；全部 62 个可执行 MZ 程序（含 `libexec/gcc` 内部程序）被转换，shell driver 复制为相同字节，数据/库和原 archive links 保持安全的目录相对查找。原归档、原 tree、锁、libc 来源声明均不改动。

`native_toolchain` 单列派生原始/结果 SHA、官方转换命令和 provenance receipt，不能把派生 helper 当成官方原 ZIP 字节。缓存复用仍从原程序内编码的 x86_64 ELF64 header（Linux OSABI 0）和完整未变 body 独立计算预期 SHA，重新验证输出、只读文件和严格 links/layout；receipt 不作为完整性权威。构建后再检查原输入及派生视图。没有 seeded fallback、静默重试或全局 binfmt 改动；正常 native view 不需要自行解包 loader。`prepare-toolchain` CLI 可独立预备视图，仍完全离线。

`scripts/build_ape.py` 的 `build` / `inspect` / `check-ci` 使用独立的 `ape-build` v1 报告，BOOT-01 的 `validate-report` 不验证此合同。报告记录 source/toolchain lock 和实际内容身份、完整 argv/cwd/时间/原始日志、所用 flags、原版 QuickJS C 文件顺序、Temporal/Intl 关闭状态、APE 资源顺序及实际 BUILDLOG、两种 CPU 的 ELF header/LOAD 边界和片段摘要、原制品/sidecar/map 摘要、链接符号与 PE 导入检查、本地架构/页大小及 eval 错误路径。`m0_core_status` 保持 `not_run`，libc 源码映射保留输入锁的 `unproven`；后续门禁仍须消费完整矩阵证据。

生成文件为 `artifacts/{hello,quickjs-eval}.com`、`.com.dbg`、`.aarch64.elf`、`.{x86_64,aarch64}.map` 和 `.build-commands.log`。Hello 输出包含 `probe`、编译执行架构和 `sysconf` 页大小的 JSON；eval 的 `--facts` 输出同样事实，默认求值 `6 * 7` 输出 `42`，一个参数作为 JS 表达式；异常和语法错误向 stderr 写出异常，Ubuntu 实测退出 `1`。跨宿主应断言错误语义及非零状态，并记录各宿主实际值（Windows 预检观测 256）。两探针通过锁定的原生 ELF APE loader 运行；WSLInterop/binding 状态记录在 build-info，不修改全局注册。

03 在同一 `quickjs-eval` 增加 `--esm-export`、`--esm-dependency`、`--jobs-complete`，均从真实模块/Promise 取得 `42`。固定模块 loader 只提供内嵌 `m0:dependency`，不读取文件或网络。`--esm-missing`、`--esm-syntax`、`--esm-exception`、`--jobs-exception`、`--jobs-rejection`、`--jobs-unfinished`、`--jobs-budget` 分别验证加载、解析、求值、pending job、未处理 rejection、无可运行 job 的 pending Promise 与 256-job 上限，均须空 stdout、对应 stderr 和非零退出。外层进程时限继续由共享命令入口执行。该 fixture 不是产品 resolver、完整 TLA 合同或 SDK；[实际范围](../../.scratch/qaplet-v15-alignment/esm-promise-validation.md)限于已执行环境。

最小引擎使用 `quickjs.c dtoa.c libregexp.c libunicode.c cutils.c`，`-Os -fwrapv -D_GNU_SOURCE` 和 VERSION 中的 `CONFIG_VERSION`；不定义 `CONFIG_TEMPORAL_INTL`，不启用 LTO。05 初次构建的空 patch 清单是历史输入；07 实际 ARM64 运行发现 47-bit pointer truncation 后，在同一 Bellard pin 上追加有序 header overlay，改用上游已有的完整指针表示，见下节。自有探针是 M0 验证程序，不是 Runtime/Embed SDK。所有服务专属导入（SCM / Job Object 等）均由两个 CPU 的符号、map 和最终 PE imports 拒绝；常规 libc 进程/线程能力可出现。

独立审计：`python3 scripts/build_ape.py inspect --artifact out/m0/artifacts/hello.com --expected-sha256 <expected> --report out/inspect.json`。预期摘要先于工具执行比较；之后读取相邻的两个 sidecar 和 CPU map，缺失 slice/文件、坏边界或服务符号均失败。完整实际构建合同可显式运行 `QAPLET_TEST_SOURCE_CACHE=/path/to/cache make check OUT=out/check-native`；普通 `make check` 不会下载或强制每次重编大输入。

独立 `.github/workflows/m0.yml` 只构建一次并上传 `out/m0/`。它先运行 `make check`，再显式执行 `fetch_locked_toolchain.py` 从锁定官方 artifact URL 取原始 ZIP（GH_TOKEN 只经环境和 curl stdin，不写入 argv/报告），SHA/长度必须匹配；已有正确原 ZIP 可直接传给 `make toolchain-fetch`。bootstrap 失败、归档过期、鉴权或网络错误均不回退滚动 Nightly，也不改锁。CI 随后运行与本地相同的输入 fetch 和 `build-ape`；跨宿主测试 job 由后续任务消费上传字节和摘要，不重新编译。本地实际验收见 [fat APE 记录](../../.scratch/qaplet-m0/fat-ape-build-validation.md)。

## BOOT-04B 原生启动入口

`scripts/startup_loaders.py fetch` 显式获取独立锁定的 Zig Linux 归档；`verify` 与 `build` 离线校验全部内容。`build` 仅允许 Ubuntu x86_64，生成独立 loader bundle（包括 Ubuntu 交叉构建的 Mac ARM64 Mach-O），不更改产品 APE。worktree 使用 `--zig-cache`、`--toolchain-cache`、`--build-dir` 指向主工作区 `.cache/`。原生验证不调用宿主编译器：

```sh
python scripts/startup.py run --build-report out/m0/build-info.json \
  --artifact-dir out/m0/artifacts --loader-dir out/m0/loaders \
  --report out/startup/startup.json
```

`--expected-os` / `--expected-arch` 可把调度声明与实测身份比对。每个宿主执行 17 项：原七项 Hello/eval facts、42、JSValue pointer roundtrip、object/Map、异常/语法错误，加上上述十项 ESM/jobs。builder、startup 和 Core 消费同一用例清单；旧七项成绩不签发新增合同。helper `build --producer-report BUILD` 将 producer 的独立 source-built ARM64 loader 绑定到分发 bundle。

`page-gaps --input-report NATIVE --github-output FILE --report PLAN` 只根据实测 native Linux ARM64 页大小调度：4K 补 64K、64K 补 4K、16K 或缺报告补两项；失败的 native startup 保留失败，不用 guest 成功覆盖。`guest-manifest --page-size 65536 --manifest out/guest.json` 仅导出 07 generic guest payload，不声明运行。共享 `merge-manifests` 合并 startup 与 I/O，64K 另含 deep probe；实际 `arm64_guest.py run` 后，`startup.py import-guest --guest-report GUEST` 独立复核 17 项语义、producer/loader/传输前后摘要、TCG/guest 身份、原始日志和清理。原始日志随 report 的 `evidence/<report-id>/` 迁移后仍可导入。

`matrix` 用重复的 `--input-report` 或 `--input-dir` 汇总六目标，保留同目标的失败和非目标页大小观测；缺项保持 blocked。五 native CI、仅补缺页的完整系统 guest 和最终 matrix 均消费同一 Ubuntu producer 字节；失败仍上传原始证据。完整 CI `41271366261` 已通过，native Linux ARM64 实测 4096，完整系统 guest 仅补 65536；见 [06 验收](../../.scratch/qaplet-m0/cross-host-startup-validation.md)及 [Core gate](../../.scratch/qaplet-m0/core-evidence-gate-validation.md)。

`matrix --profile cross` 只汇总五种实际 native OS/架构，Linux ARM64 保留实测页大小；guest 不替代 native。默认 `--profile full` 仍要求六个页配置目标。两种汇总均保留缺项和失败，拒绝重复 report ID、错误 producer 引用/上下文、旧输入及缺失 ESM/jobs 清单；成功不签发完整 Core。CI producer/实际执行 consumer 必须同时有完整 run/commit/runner/image 身份且 commit 相同；本地只读导入不改写实际执行 CI。

## BOOT-07 native I/O

`python scripts/native_io.py build --output-dir out/m0/io/artifacts --report out/m0/io/build-info.json` 在 Ubuntu 生成独立 fat APE，复用已锁定工具链的前后校验与派生 native helper 审计。worktree 显式传 `--toolchain-cache /mnt/d/Workspace/dairn/qaplet/.cache/toolchains/cosmocc --build-dir /mnt/d/Workspace/dairn/qaplet/.cache/build`，输出仍在 `out/`。此 native 基线使用当前官方布局；ARM64 64K loader/packer 修复和运行由 07 继续验收，不能将 headers 中存在 ARM64 当作运行成绩。

`python scripts/native_io.py run --build-report out/m0/io/build-info.json --artifact out/m0/io/artifacts/native-io.com --loader-dir out/m0/loaders --report out/io.json` 只消费 producer 摘要对应字节；Linux/macOS 使用 06 的 helpers.json 和 native loader，Windows 直接执行 PE。共享 measured-host 检查保留实际 CPU、执行架构、内核页大小及 macOS translation 观测。20 个必需 case 覆盖 socket/half-close、pipe/EOF/EPIPE、私有 PTY/ConPTY 的检测/修改/恢复/关闭、Unicode/空格路径/argv 的无隐式 shell spawn/wait、子进程 timeout/回收、pthread/TLS 和只读 ZipOS；Windows 另测独立 CONIN$ handle 的 CloseHandle/无效句柄。

schema v1 report_kind 为 native-io，scope 为 build/host/manifest/guest；缺项保留 not_run 且非通过，缺实际交互设施为 blocked，异常为 fail，limitation 必须有实测返回值和明确原因。`--probe-mode hang --timeout 1` 是实际负向 deadline fixture，成功清理后仍报告 fail。所有命令/原始日志和 fixture 身份保留在独立 evidence UUID。共享 M0 workflow 的 Ubuntu producer 一次生成并分发；五个 native job 只验证，startup 失败后仍尝试 I/O 并分别保留证据。最新六目标 I/O 已通过，完整记录和历史缺口/失败见 [I/O 基线](../../.scratch/qaplet-m0/native-io-validation.md)。

`guest-manifest --build-report BUILD --artifact FILE --loader ARM64_ELF --page-size 65536 --manifest OUTPUT --report REPORT` 导出经过 SHA/LOAD 对齐验证的 Unicode/空格路径 payload（scope manifest，不运行 guest）；由 07 的 `merge-manifests` 和完整 guest runner 统一处理镜像、锁和启动。`import-guest --build-report BUILD --artifact FILE --guest-report GUEST_JSON --report IO_JSON` 复核实际 QEMU/guest/前后摘要/原始日志，再用 native 共用的 20 个必需值断言生成 scope guest 结果。host 保留真实 QEMU 物理宿主，execution/guest 明确 emulated-system 和实际页大小；不能作为物理 ARM64 或原生性能成绩。

## BOOT-06 · 64K 构建与实际 guest

07 的实测及各阶段失败见 [64K 验收](../../.scratch/qaplet-m0/arm64-64k-validation.md)。
主工作区新增 `.cache/sources/ape-loader/<pin>/` 固定 official source；显式执行
`python3 scripts/ape_helpers.py fetch --report out/ape-helpers-fetch.json` 后普通 build
仅离线验证。worktree 的 `make build-ape` 可用 `APE_HELPER_SOURCE_CACHE` 指向主仓库
`.cache/sources/ape-loader`。`portable_helpers` 单列 independently source-built 的
ARM64 64K Linux loader、patched packer、有效 headers、来源/patch/result SHA 与实际
命令。原 Nightly、原 helpers、原缓存和 libc `unproven` 声明不改。

共享 `prepare_portable_helpers` / `link_portable` 供 Hello/eval、pages64k 与 I/O 三种
独立 producer 使用：official cosmocc 保持原 driver，仅 MAXPAGESIZE=65536，再由
源码 packer 保留 incoming LOAD alignment 重新打包，嵌入新 ARM64 loader。禁止把
全部 COMMONPAGESIZE 也改成 64K（实测 x86 PC16 overflow）。新原生消费必须匹配
各自新 SHA，不能复用旧 16K artifact 的报告。

独立深度入口为 `scripts/pages64k.py build`、`guest-manifest`、`verify-guest` 和
`run-native`。后者仅接受实际 native Linux/AArch64/65536。共享
`scripts/arm64_guest.py fetch/verify/prepare/merge-manifests/run` 使用
`guests/arm64.lock.json` 的 immutable Ubuntu snapshot，`prepare` 和 `run` 不联网。
命令支持 Unicode/空格路径、前后 guest SHA、独立 TMPDIR、actual cwd、原始 stdout/
stderr/串口、时限与 cleanup；宿主/guest 及 native/emulated-system 分开记录。
guest kernel/config/rootfs 和 QEMU binary/version/TCG/machine/CPU 均有实际身份。
完整 CI 的 page-gap 选择与 native startup importer 由 06 接入同一入口。
