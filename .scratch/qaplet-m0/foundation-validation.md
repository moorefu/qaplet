# BOOT-01 本地验证与 code-review

日期：2026-10-31（Asia/Shanghai）。任务：[01 工程骨架与环境诊断](issues/01-foundation.md)。本记录只确认 BOOT-01；M0 Core 产品验收未完成。

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
