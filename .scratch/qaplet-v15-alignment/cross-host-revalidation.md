# BOOT-04B/06/07 · 新输入跨宿主回归进度

任务 04 保持 claimed。2026-11-02 的合同实现基于 `b17bba2`；完整新 CI 矩阵和最终配方的断网 pair 尚待组合实现与实际运行，本文不签发完整 Core、Host/SDK OS ABI 冻结或 release-ready。

既有共享 startup/I/O/deep/Core 入口继续使用当前 v3、原制品和独立 helper。startup 新增 `matrix --profile cross`，按五个实际 native OS/架构汇总，Linux ARM64 保留实际页大小；默认 Full 保持六个页目标。重复 report ID、另一 producer 的引用/上下文及 CI producer/consumer commit 漂移或 CI 缺字段现在直接拒绝。pages64k producer 记录实际 CI；guest 导入保留原执行 CI 和 validation_ci。

红绿日志保存在主工作区 `out/v15/04/`：`cross-{red,green}.log`、`identity-{red,green}.log`、`producer-edge-{red,green}.log`、`pages-ci-{red,green}.log`、`duplicate-{red,green}.log`。数据合同显式选择 03 的普通 producer 和真实 Ubuntu startup；CI 字段变异只用于拒绝合同，不冒充实际远程成绩。尚未取得新输入 macOS/Windows/native ARM64 的实际完整组合；旧 M0/02/03 报告保留原范围。

第一轮普通 Ubuntu `dev.py check` 通过，202 tests、0 failures/errors、51 个明确数据/平台 opt-in skips，系统 C bootstrap 通过；原始入口报告为 `out/v15/04/check-workflow.json`，日志在 `out/v15/04/check/`。此次选择 03 pinned producer、官方 Linux loader 和真实 startup；后续 05 recipe 改动合入后须重新选择最终 producer 和执行所需验收。
