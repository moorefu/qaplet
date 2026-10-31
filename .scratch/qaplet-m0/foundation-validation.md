# BOOT-01 本地验证与 code-review

日期：2026-10-31（Asia/Shanghai）。任务：[01 工程骨架与环境诊断](issues/01-foundation.md)。本记录只确认 BOOT-01；M0 Core 产品验收未完成。

初始批次在首次提交前完成；之后发生的远程 CI 与修复另记于文末，初始批次的“未执行”不代表后续 CI 状态。

## 本地结果

最终批次的命令、预期/实际退出码与原始输出定位在 `out/validation/local-checks.json`；11 项检查均符合预期。生成物在 `.gitignore` 的 `out/` 内，当前机器保留，干净检出可按 README 重建。

| 检查 | Windows 普通用户上下文 | WSL Ubuntu-22.04 |
| --- | --- | --- |
| `make test` | 15 项通过，退出 0 | 14 项通过；1 项仅限 Windows 的 exe 选择用例 skip，退出 0 |
| `make test-bootstrap` | 实际 C 编译/执行，退出 0 | 实际 C 编译/执行，退出 0 |
| `make doctor` | 工具及 WSL 可测；产品前置未齐，退出 2 | 原生 Linux x64 可测；产品前置未齐，退出 2 |
| `make test-foundation` | 缺产品输入/制品/必需用例，退出 2 | 同左，退出 2 |
| `make check-ci` | JSON/YAML 和编排合同通过，退出 0 | 同一检查也被合同测试执行 |
| `py_compile` / `validate-report` | 通过，退出 0 | Python 3.10 通过整套适用合同 |

所有 skip 只属于开发测试中无关平台的 Windows 入口选择，未被记成产品 probe 成功。缺编译器、源文件、必需用例清单、编译错误、错误输出/退出码、挂起/超时、报告写入失败、WSL 不可用、错误 schema/CI 配置与凭据不泄露均有命令级回归覆盖。

两端 bootstrap 的内容输入集合 SHA-256 均为 `8eadeba3715141f8e2d5da29fcadbd4d90b221c99d4a2822512a615586cb0dd4`。这是 Makefile、共享脚本、C 用例、清单、schema 和 workflow 的实际字节身份；不是 QuickJS/Nightly 锁。首次提交前执行时 Git HEAD 不存在，报告如实保留 git_commit=null。

| 证据 | Windows | WSL |
| --- | --- | --- |
| bootstrap 报告 | `out/windows-final/bootstrap.json` | `out/wsl-final/bootstrap.json` |
| report_id | `c684e93c-fd6f-4d73-9697-5ba6062bfa48` | `1fc97bca-fb86-4d28-a864-5c19758b5871` |
| kernel / 架构 / 页大小 | 10.0.22000 / x86_64 / 4096 | 6.6.87.2-microsoft-standard-WSL2 / x86_64 / 4096 |
| 实际 C 输出 | answer=42 / x86_64 / 4096，退出 0 | answer=42 / x86_64 / 4096，退出 0 |
| 系统编译器 | MSYS2 GCC 14.2.0 | Ubuntu GCC 11.4.0 |
| Python / Make | 3.14.7 / 4.4.1 | 3.10.12 / 4.3 |
| 原生制品 SHA-256 | `613cad9887f138b60ec4c7a22abb80d7a84d6bbe38aca43c4664473f490a3a10` | `2f52d13231ec669f99970143e59b33cbff7d4e97f60d4dc6f2ae4f7e3c49ea7f` |

这两个系统原生二进制是不同 artifact，均不是 APE，不比较它们是否字节一致。构建 argv、编译器二进制摘要、制品大小、时限、逐命令 stdout/stderr 日志路径在报告内。最终 doctor 报告为 `out/windows-final/doctor.json` / `out/wsl-final/doctor.json`，其中 Linux 环境检查本身 pass，报告整体因 M0 产品前置未齐保持 blocked。

暂存内容通过 `git checkout-index` 导出到不含未跟踪源码的 `out/clean-checkout/`，已从该目录运行真实 C bootstrap、合同回归及 CI 静态检查。实现后仍保留 AGENTS/ADR/既有文档；总体计划原文件 SHA-256 仍为 `C1A16D76C2453EF42BAFE9DCACE9673CCEA0FC1C95FBF6A9CFAA77C93A0E8237`。

## Standards

独立 reviewer 按 AGENTS、tracker/triage/domain 文档、GLOSSARY、ADR 0001–0003 和 Fowler smell baseline 审查：无可操作发现；0 项硬性违反、0 项实质设计异味。

## Spec

独立 reviewer 按 01 任务、M0 spec 和 ADR 审查，发现 1 项 P2：原诊断测试清空 PATH 后无条件期望 WSL blocked，忽略了新实现优先使用绝对路径的已安装 WSL。该错误只在真实普通用户上下文暴露，受限沙箱测试会掩盖它。

已在普通用户上下文复现 `pass != blocked`；修正后主机盘点接受真实观测并校验就绪 Linux 的架构/页大小，另设 Windows 专用负向用例，显式传入不存在的 `--wsl` 并断言 blocked。沙箱和普通用户的聚焦回归、最终 Windows/WSL 全套回归均符合预期。reviewer 复审确认无剩余发现，无 BOOT-01 范围遗漏或越界。

两轴最终发现数：Standards 0；Spec 0（已修复 1 项 P2）。

## 未执行项

未获取 QuickJS/Nightly、未构建或运行产品 APE/QuickJS、未执行完整六类产品矩阵、ARM64 深探针、QEMU、离线重构及 SCM/Job。已有 origin，但没有 push 或触发远程 workflow；CI 仅本地静态检查，未运行 actionlint 或 Actions 服务端校验。任务 02 仍须独立验收其完整工具/磁盘等合同。

## 首次远程 CI 与 Windows 配置修复

用户重新关联 GitHub 后，已读取 [run 41202382602](https://github.com/moorefu/qaplet/actions/runs/41202382602) 的实际状态，执行提交为 `ad509353948f8cef6032da497192f77a50d51824`：

| Bootstrap job | 实际结论 |
| --- | --- |
| macos-14 | success |
| ubuntu-24.04-arm | success |
| ubuntu-24.04 | success |
| windows-2022 | failure；C bootstrap blocked，后续合同/静态检查步骤 skipped |

[Windows job 124540572197](https://github.com/moorefu/qaplet/actions/runs/41202382602/job/124540572197) 上传的 artifact `13042059802` 内，`bootstrap.json` 明确报告 `C compiler not found: C:\msys64\mingw64\bin\gcc.exe`，没有执行任何 C 命令。原始 ZIP 位于本机 `out/ci-failure/bootstrap-windows-2022.zip`，SHA-256 为 `48fdc6c5a63c6e6c453dba745289ad256b89aa6a94784bbd83295fe06b20c0ff`。无需把允许失败的 doctor blocked 误当成本次阻断点。

原因是将本机已有的 MSYS2 MinGW 编译器路径写进了托管矩阵。[官方 Windows 2022 镜像清单](https://github.com/actions/runner-images/blob/main/images/windows/Windows2022-Readme.md)记录系统 GCC；预装 MSYS2 本身不保证其 `mingw64/bin/gcc.exe` 存在。修复为 Windows 行使用 PATH 中的 `gcc`，继续由共享脚本采集实际编译器路径、版本和摘要，不新增 MSYS2 安装步骤或依赖锁。

本地反馈循环先红后绿：新增回归读取实际 Windows matrix 的 CC 并调用真实 bootstrap；修复前 WSL 返回同样的 compiler-not-found，修复后真实 C 编译/执行通过。另一个回归注入原绝对路径，确认 `check-ci` 拒绝这种托管配置。本地绝对 `CC` 路径仍被支持。

最终 Windows 普通用户上下文 17/17 测试通过；WSL 16 项通过、1 项 Windows 专用用例 skip；CI 静态检查和 Python 编译检查通过。日志及退出码在 `out/ci-fix/validation/`。修改后的远程 job 尚未运行，此处不声称远程已修复通过，也不把三个成功的系统 C bootstrap 计为产品 APE/QuickJS 或完整 M0 成绩。

本次修复另行按 `ad50935` 基准执行两路 code-review：Standards 0 项发现，Spec 0 项发现；待远程复跑的验证限制已明确保留。
