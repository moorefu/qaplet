# BOOT-04B/06/07 · 新输入跨宿主回归进度

任务 04 保持 claimed。2026-11-02 的合同实现基于 `b17bba2`；最终配方的本地断网 pair、native 与完整 guest 已通过，完整新 CI 矩阵仍待组合远程验收。本文不签发完整 Core、Host/SDK OS ABI 冻结或 release-ready。

既有共享 startup/I/O/deep/Core 入口继续使用当前 v3、原制品和独立 helper。startup 新增 `matrix --profile cross`，按五个实际 native OS/架构汇总，Linux ARM64 保留实际页大小；默认 Full 保持六个页目标。重复 report ID、另一 producer 的引用/上下文及 CI producer/consumer commit 漂移或 CI 缺字段现在直接拒绝。pages64k producer 记录实际 CI；guest 导入保留原执行 CI 和 validation_ci。

红绿日志保存在主工作区 `out/v15/04/`：`cross-{red,green}.log`、`identity-{red,green}.log`、`producer-edge-{red,green}.log`、`pages-ci-{red,green}.log`、`duplicate-{red,green}.log`。数据合同显式选择 03 的普通 producer 和真实 Ubuntu startup；CI 字段变异只用于拒绝合同，不冒充实际远程成绩。尚未取得新输入 macOS/Windows/native ARM64 的实际完整组合；旧 M0/02/03 报告保留原范围。

第一轮普通 Ubuntu `dev.py check` 通过，202 tests、0 failures/errors、51 个明确数据/平台 opt-in skips，系统 C bootstrap 通过；原始入口报告为 `out/v15/04/check-workflow.json`，日志在 `out/v15/04/check/`。此次选择 03 pinned producer、官方 Linux loader 和真实 startup；后续 05 recipe 改动合入后须重新选择最终 producer 和执行所需验收。

05 冻结后的集成 `1b68a38` 使用新 input_id `e41f53e2fe3176ce9cdabaeed6a99de2744acfab34669c8db73f9e14f0baf47c`。最终普通 Ubuntu check 通过 208 tests、0 failures/errors、69 个显式 opt-in skips；报告为 `out/v15/04/final-check-workflow.json`。实际 root namespace setup 降权至 UID/GID 1000 的 offline preflight 通过；随后最终双构建、六个交付二进制与两 helper 逐字节比较、14 个复制输入拒绝、精确 before/after inventory 和断网 trace 均通过。实际报告为 `out/v15/04/final-offline/offline-integrity.json`，23 分钟执行，cache unchanged=true；05 已实际封装 helper 并由既有 preflight 证明 recipe-identical（24 项）。

## 最终本地执行与消费者

同一 first offline producer 与独立 I/O/pages producer 的 CI 均为 null。Ubuntu x64/4096 的 17 项 startup 和 20 项 I/O 通过；锁定完整系统 QEMU/TCG 在 Linux/AArch64/4096 与 65536 分别执行 18/21 条入口，startup、I/O 和 64K 的 12 项 deep 全通过。报告与完整串口位于 `out/v15/04/local/guest-{4096,65536}/`，两页均通过只读 Core `validate-guest`，scope=guest-transport、core=not_run。`local/guest-inputs.json` 是局部实际输入选择，未声明完整 native ARM64 或六目标 Core。

Windows 初次受限工具上下文的 startup/I/O 超时保留在 `local/{startup-windows,io-windows}.json`；独立同字节/同路径对照定位到该执行上下文。直接 Windows 普通用户上下文复验为 startup 17/17 与 I/O 21/21，报告为 `local/{startup-windows-normal,io-windows-normal}.json`，没有更改产品、系统或安全设置。旧进程已结束，intervention 记录未发现残留目标。

实际数据选中的 startup、guest startup、I/O 和 4K Core 五组消费者/拒绝回归均通过；普通平台/数据 opt-in skips 仍明确保留。严格 runner 现在允许独立 `guest_manifest` 绑定本地 4K producer，完整 Core 仍必须消费远程同 commit producer/matrix；不模拟 CI 字段迁就验收。SCM/Job 仅在 Ubuntu 构建分发制品，capability=not_run；五类实际 producer 的 notices/maps/HACL publication 回归为 3/3 通过，选择在 `out/v15/04/publication/inputs.json`。

Full CI 的固定 snapshot 入口一次 HTTP 503 保留原失败；只对显式 guest fetch 增加原 URL/摘要的至多三次瞬时 HTTP 重试，共用原 190 秒预算。以实际签名输入和本地外部 HTTP fixture 先红后绿验证 503→成功及重试耗尽；`guest-retry-{red,green}.log` 不冒充上游或新远程成绩。

真实远程 Cross `41343504925` / `a1845855688e3bfcbe9eaae0e9bba989986e0767` 的五宿主 startup 和 Cross startup matrix 通过，但非 Windows I/O 因共享 loader 访问不存在的 `args.controlled_probe` 而失败。原 CI archive `13097833073` 经独立长度/SHA/CRC 验证后，以原 I/O 字节和 helpers.json 复现 exact AttributeError；`8b3f556` 将未提供该选项的 shared caller 默认保持普通 pinned 模式。`io-loader-directory-{red,green}.log` 明确只是模拟记录 CI 环境的本地回放，不计为新的远程成绩。真正本地新 producer 为 `out/v15/04/io-consumer-regression/build-info.json`，其 CI=null 的 Linux 20 项实际执行在 `out/v15/04/io-loader-local.json` 通过。新远程 run 仍由集成流程取得。
