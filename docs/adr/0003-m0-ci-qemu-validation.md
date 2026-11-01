# M0 本地优先、原生页覆盖与 QEMU 补充

2026-11-01：用户在 v1.5 对齐中再次确认完整系统 QEMU 可补缺失 ARM64 页配置，[ADR 0004](0004-v15-platform-baseline.md)保留此决策。下文“当前 WSL 缺内核/发行版”是规划时观测，实际环境与现有六目标成绩见[当前 M0 索引](../../.scratch/qaplet-m0/README.md)。

用户于2026-10-31进一步确定：代码探索、调试与失败复现优先本地，GitHub Actions承担持续集成；先利用原生Linux环境的实际架构/页大小，再由完整系统QEMU补缺失组合。此前“两种页配置都固定放QEMU”的方案由此替代，Core/App独立门禁和12张任务依赖不变。

CI使用GitHub原生ARM64 Linux runner，job开始实测架构/kernel/页大小。若原生为4K则只补64K QEMU，若为64K则只补4K；其他页配置明确报告缺口。覆盖按OS、架构、页大小和执行方式识别。WSL x64 4K不能计入ARM64 4K。

本地先准备WSL并实测，再用Windows + WSL + 一个ARM64 64K完整guest形成快速profile；需要ARM64 4K专属复现时可选完整profile。快速profile不宣称完成全部架构矩阵。测试脚本在本地可运行，CI仅编排同一脚本，不将日常探索转移到反复提交workflow。

QEMU补充必须启动真实guest内核，以TCG和固定QEMU/machine/CPU/kernel/config/rootfs运行。qemu-user不能替代4K/64K内核验证；报告区分native与emulated-system。SCM/Job先本地隔离VM探索，CI托管Windows VM持续回归；重启/交互/策略等按实际用例声明覆盖。

当前WSL缺内核和发行版，页大小无法实测。GitHub官方ARM64镜像清单未列页大小，本轮未触发CI，不能把4K猜测记成测量结果。运行前探测和镜像/页配置漂移检查是实施要求。详见[环境记录](../../.scratch/qaplet-m0/environment-check.md)。

依据：[GitHub原生ARM64 runner](https://docs.github.com/en/actions/reference/runners/github-hosted-runners)、[官方Ubuntu ARM64镜像清单](https://github.com/actions/runner-images/blob/main/images/ubuntu/Ubuntu2404-Arm64-Readme.md)、[完整ARM系统QEMU](https://www.qemu.org/docs/master/system/target-arm.html)、[QEMU用户态模拟](https://www.qemu.org/docs/master/user/main.html)。
