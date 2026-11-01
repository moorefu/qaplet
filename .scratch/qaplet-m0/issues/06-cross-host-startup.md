# 06: 同一 APE 的跨宿主启动验证（BOOT-04B）

**What to build:** 维护者可以证明每个 foundation APE 的同一份字节在计划要求的六类宿主执行，且native与emulated-system执行方式均如实标明。

**Blocked by:** [05 · 构建 fat APE 与最小 QuickJS 探针](05-fat-ape-build.md)。

**Status:** resolved

**Execution prerequisites:** 本地WSL/Windows与本地完整系统QEMU先探索；CI采用对应原生runner，ARM64 runner先测页大小，再仅补缺失ARM64页配置的QEMU guest。kernel/rootfs需锁定，当前无运行证据。

## Goal

维护者可以证明每个 foundation APE 的同一份字节在计划要求的六类宿主执行，且native与emulated-system执行方式均如实标明。

## Scope

分发第5项制品并验证传输前后摘要；运行 Hello 与最小 eval，采集宿主身份、实际架构、kernel/page size、命令、stdout/stderr、退出码。64K 此处只负责启动/eval，深入系统合同由第7项承担。

## Out of Scope

完整 Node/TTY/TLS/QJB 支持、性能承诺、用重新编译/assimilate/签名后的文件冒充原始同一产物、将QEMU结果冒称物理ARM64已验证，或用qemu-user代替完整4K/64K guest。

## Source Files

跨宿主 runner、宿主清单、结果导入与校验器；复用 foundation evidence 契约。

## Acceptance criteria

- [x] 六类宿主都有实际执行报告，每个制品跨宿主 SHA-256 恒等；Hello 和 eval 分别维持自身身份。
- [x] macOS ARM64以native执行；Linux ARM64优先原生runner并实测页大小，缺失的4K/64K组合再由完整QEMU补充，报告native/emulated-system。
- [x] 结果注明OS/kernel、页大小、host/guest架构、execution_mode、QEMU/TCG/machine/CPU和guest镜像摘要、runner image/run身份、命令与退出码；QEMU专属字段仅在guest必填，原生结果注明not_applicable；文件重命名不能改变内容身份。
- [x] 主机或权限未就绪时允许逐宿主采集，但整张任务保持未完成；缺失/失败不得被 skip 成通过。

## Tests

先本地执行共享runner验证失败和输出；CI ARM64原生runner测量架构/页大小后运行本机覆盖组合，仅为缺失组合以TCG启动锁定guest；回收原生日志与guest串口输出；比对分发摘要、预期输出、返回码和架构声明。

## Negative Cases

制品被替换、摘要不符、非可执行挂载、错误 loader 路由、WSL x64冒充ARM64、runner页大小变化/非4K或64K、模拟冒充native、缺少宿主、报告缺字段。

## Evidence

每个制品的六宿主原始日志与结构化结果、传输摘要、宿主指纹和启动异常记录。

所有结果遵循[父规格](../spec.md)的身份与状态合同。尚未执行的探针不得填造成功结果。

## Rollback

保留失败报告并将对应宿主标为未验/失败；修复构建后对新摘要重跑，不能复用旧摘要的成功证据。

## Context

- [M0 执行规格](../spec.md)
- [任务索引、资源前置与里程碑](../README.md)

## Answer

2026-11-01：集成 `1129cd8` 的真实 [M0 run 41271366261](https://github.com/moorefu/qaplet/actions/runs/41271366261) 已 completed/success。五个 native 与一个完整系统 65536 字节页 TCG guest 各自通过七项启动；native ARM64 实测 4096，因此只补 65536。Mac ARM64 为 native/16384。两个 producer 摘要与最终本地 Hello/eval 相同，所有目标分发前后恒等。14 个原 ZIP 的服务端长度/SHA/CRC、438 个唯一原始命令日志路径和 guest 串口摘要已独立复核，六目标 matrix 全 pass；详见[当前验收](../cross-host-startup-validation.md)。本票 resolved，完整 Core 仍由 10 聚合。

## Comments

2026-11-01：在 `codex/m0-06-cross-host` / `out/implement-spec/06` 开工，已确认基于 integration `dc5c0c3`。沿用用户确认的 CLI / 实际 OS / evidence seam；独立实现原生 runner 与 Ubuntu 构建的 loader bundle，ARM64 完整 guest 复用 07。六宿主实际证据齐备前保持 claimed。

原生 baseline 已完成本地 Ubuntu / Windows 五项执行、独立 Mac ARM64 helper 的 Ubuntu 构建、五原生 CI 编排及缺宿主拒绝汇总；见[验收与 64K 交接](../cross-host-startup-validation.md)。Mac/原生 ARM64 CI 仍待实际运行，64K loader/packer 修正与 generic guest 由 07 接续；不能关闭本票或完整 Core。

2026-11-01 后续：已合入 07 final producer/source-built loader 与 08 native I/O，启动合同升级为七项，增加 JSValue pointer roundtrip 和 object/Map 生命周期。06 接通实际 generic guest 语义导入、可迁移 raw logs、按 native 实测页大小生成 QEMU 缺口与六目标 matrix；16K native 保留观测并补两种目标页，失败不能被后来的 blocked 报告覆盖。新 SHA 的 Windows 七项实际通过；完整本地/guest checkpoint 见[当前验收](../cross-host-startup-validation.md)。本票保持 claimed，等待新完整 CI 对这批 producer 字节的全部 native 与补缺 guest 验收；旧摘要 CI 不计入本轮通过。
