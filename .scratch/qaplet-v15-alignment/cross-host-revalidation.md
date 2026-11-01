# BOOT-04B/06/07 · 新输入跨宿主回归进度

任务 04 保持 claimed。2026-11-02 的合同实现基于 `b17bba2`；完整新 CI 矩阵和最终配方的断网 pair 尚待组合实现与实际运行，本文不签发完整 Core、Host/SDK OS ABI 冻结或 release-ready。

既有共享 startup/I/O/deep/Core 入口继续使用当前 v3、原制品和独立 helper。startup 新增 `matrix --profile cross`，按五个实际 native OS/架构汇总，Linux ARM64 保留实际页大小；默认 Full 保持六个页目标。重复 report ID、另一 producer 的引用/上下文及 CI producer/consumer commit 漂移或 CI 缺字段现在直接拒绝。pages64k producer 记录实际 CI；guest 导入保留原执行 CI 和 validation_ci。

红绿日志保存在主工作区 `out/v15/04/`：`cross-{red,green}.log`、`identity-{red,green}.log`、`producer-edge-{red,green}.log`、`pages-ci-{red,green}.log`、`duplicate-{red,green}.log`。数据合同显式选择 03 的普通 producer 和真实 Ubuntu startup；CI 字段变异只用于拒绝合同，不冒充实际远程成绩。尚未取得新输入 macOS/Windows/native ARM64 的实际完整组合；旧 M0/02/03 报告保留原范围。

第一轮普通 Ubuntu `dev.py check` 通过，202 tests、0 failures/errors、51 个明确数据/平台 opt-in skips，系统 C bootstrap 通过；原始入口报告为 `out/v15/04/check-workflow.json`，日志在 `out/v15/04/check/`。此次选择 03 pinned producer、官方 Linux loader 和真实 startup；后续 05 recipe 改动合入后须重新选择最终 producer 和执行所需验收。

05 冻结后的集成 `1b68a38` 使用新 input_id `e41f53e2fe3176ce9cdabaeed6a99de2744acfab34669c8db73f9e14f0baf47c`。最终普通 Ubuntu check 通过 208 tests、0 failures/errors、69 个显式 opt-in skips；报告为 `out/v15/04/final-check-workflow.json`。实际 root namespace setup 降权至 UID/GID 1000 的 offline preflight 通过；双构建及精确 before/after inventory 仍在运行，不能从预检或单个 producer 推出完整 offline pass。

真实远程 Cross `41343504925` / `a1845855688e3bfcbe9eaae0e9bba989986e0767` 的五宿主 startup 和 Cross startup matrix 通过，但非 Windows I/O 因共享 loader 访问不存在的 `args.controlled_probe` 而失败。原 CI archive `13097833073` 经独立长度/SHA/CRC 验证后，以原 I/O 字节和 helpers.json 复现 exact AttributeError；`8b3f556` 将未提供该选项的 shared caller 默认保持普通 pinned 模式。`io-loader-directory-{red,green}.log` 明确只是模拟记录 CI 环境的本地回放，不计为新的远程成绩。真正本地新 producer 为 `out/v15/04/io-consumer-regression/build-info.json`，其 CI=null 的 Linux 20 项实际执行在 `out/v15/04/io-loader-local.json` 通过。新远程 run 仍由集成流程取得。
