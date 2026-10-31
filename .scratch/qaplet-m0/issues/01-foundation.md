# 01: 工程骨架与环境诊断（BOOT-01）

**What to build:** 维护者可以从干净检出运行环境诊断和基础工程检查，并准确区分工程入口可用与 M0 产品验收完成。

**Blocked by:** None (can start immediately)。

**Status:** resolved

**Execution prerequisites:** 现有 Windows x64 开发环境即可开始；不要求先具备 Nightly 或外部测试机。

## Goal

维护者可以从干净检出运行环境诊断和基础工程检查，并准确区分工程入口可用与 M0 产品验收完成。

## Scope

保留现有 main 仓库、AGENTS 和用户文档；建立 MIT 许可、最小构建/CI 入口、基础报告契约和一条使用系统 C 编译器的真实冒烟路径。make doctor 与 make test-foundation 是统一外部入口；早期报告必须列出尚未具备的工具链、制品和宿主。共享脚本先支持本地探索/调试，GitHub Actions仅编排持续集成：预留构建后artifact分发、原生架构/页大小探测、QEMU补缺及独立SCM/Job job；不把测试逻辑只写进workflow。没有probe的骨架不能显示产品验收成功。远程执行须有实际仓库/runner。

## Out of Scope

下载或锁定 Nightly、产品 APE、QuickJS Runtime、对外发布、完整目录树占位。

## Source Files

根级构建与工程说明、许可、CI skeleton、环境诊断器、foundation runner、报告 schema。具体代码路径在实现时确定；沿用总体计划的目录边界。

## Acceptance criteria

- [x] 自有代码声明 MIT，第三方许可边界单列；现有文档与 AGENTS 内容被保留。
- [x] doctor 记录工具位置/版本和实际宿主，明确报告 WSL2/Linux 未就绪等前置；不打印凭据。
- [x] 基础检查至少编译并执行一个可断言结果的 C 冒烟用例，不以空测试集或 echo 成功代替。
- [x] test-foundation 对缺少必要输入、制品或未执行的必需用例给出非通过状态和非零退出；bootstrap 检查单独标为 bootstrap，不能被汇总成完整 M0 通过。
- [x] CI skeleton 可解析；本地检查、远程未运行状态与实际测试结果有清晰区别。

## Tests

以真实工具调用验证诊断器和 C 冒烟结果；核查 CI 配置与入口；在可控 PATH 下重放依赖缺失。

## Negative Cases

编译器缺失、编译失败、错误退出码、空测试集、报告写入失败、WSL 不可用。

## Evidence

环境报告、bootstrap 用例输出/退出码、CI 静态检查结果；所有未执行项都有原因。报告契约供后续任务复用。

所有结果遵循[父规格](../spec.md)的身份与状态合同。尚未执行的探针不得填造成功结果。

## Rollback

移除本任务新增的工程骨架即可回退；不重置用户仓库、不覆盖既有文档，不引入系统配置副作用。

## Context

- [M0 执行规格](../spec.md)
- [任务索引、资源前置与里程碑](../README.md)

## Comments

2026-10-31：开始按 implement 工作流实施。测试边界沿用父规格已确认的外部命令、退出状态、实际 C 执行和结构化报告；本地验证优先。

2026-10-31：完成。Windows 普通用户上下文 15/15 合同测试通过；WSL Linux 14 项通过，另 1 项仅限 Windows 的 WSL exe 选择测试明确 skip。两端真实 C bootstrap 均执行成功，answer=42、x86_64、page_size=4096；doctor 和完整 test-foundation 因产品前置/用例缺失按预期退出非零。暂存内容导出的干净目录通过 bootstrap、合同测试和本地 CI 静态检查。

code-review 分 Standards / Spec 两路执行：Standards 无发现；Spec 发现依赖受限沙箱假设的 WSL 测试，经普通用户上下文复现、修正和复审后无剩余发现。完整证据定位、摘要及审查记录见[BOOT-01 验证记录](../foundation-validation.md)。

保留并纳入首次提交的既有 AGENTS、术语、ADR 和总体计划；总体计划原文件 SHA-256 仍为父规格固定值。用户提供的 WSL 入口经沙箱外核验实际可用，[环境记录](../environment-check.md)已补充新事实；不关闭任务 02，不声明产品 APE/QuickJS、ARM64/macOS 矩阵或远程 CI 已验证。
