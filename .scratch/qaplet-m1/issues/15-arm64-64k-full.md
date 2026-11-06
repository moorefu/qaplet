# 15: ARM64 64K 与相关 Full 门禁验收

**What to build:** 在真实 ARM64 64K 内核验证新输入的 SDK 关键合同，按既有 Full 规则补齐受影响的 Core/配方回归与原始证明。

**Blocked by:** [13：可分发 SDK 包与完整纯 C 示例](13-sdk-distribution-c-example.md)

**Status:** resolved

**Execution state:** resolved；固定源码 `21bd317f4246571f0048789ca97167632c09f924` 的 Full `41489263809` 成功，匹配原 producer/SDK/ZIP/三 APE 的 ARM64 64K 98 项、当前 M0 Full 235 项及八项真实 mutation 回归通过。解决提交仅更新本票与验收记录；长期镜像和 M1 严格完成仍由各自门禁判定。

**Execution prerequisites:** 匹配的新制品、ARM64 64K 原生环境或固定完整系统 guest，以及当前 Full 所需的受影响输入和实际运行资源。

**Spec stories:** 58、59、60。

## Acceptance criteria

- [x] 原生页覆盖优先，固定完整系统 QEMU 可补缺，guest 内实测 CPU/page-size 并标 native/emulated-system。
- [x] 变化后的实际 SDK/C Host 制品在 64K 执行必要 ESM/jobs/GC/interrupt/线程事件/关闭合同，记录真实身份。
- [x] 保留现有敏感改动 Full 选择及受影响的 Core/loader/配方/offline 验证规则，不继承旧输入成绩。
- [x] 供应输入/guest/日志/摘要缺失或错误拒绝；qemu-user/交叉编译不能替代实际内核覆盖。
- [x] 长期原件镜像仍为独立发布门禁，M1/Full 证明不自动签发 release-ready。

## Validation and evidence

通过本票实际提供的公开 SDK/CLI 与纯 C Host 外部边界验证，观察公开状态、稳定错误 code、JS/native 副作用、复制数据、退出码和资源关闭事实。复用已有真实 producer、供应制品、宿主 consumer 与原始证据模式；每票执行自身验收及适用的 Cross/Full 门禁，平台收尾票不替代此前回归。

证据记录当前实现/输入/配方/SDK/APE 身份、实际命令、期望与实际结果、stdout/stderr 原始日志、退出码、时限、宿主与执行方式。外部 supervisor 超时表示用例未完成，不能作为产品正确停止/超时的证明；缺资源、失败、skip 与未完成保持可见，不继承旧 M0 成绩。

## Scope boundary

本票交付一个可单独演示和验收的增量，只声明已经实现并实际验证的范围；未实现能力不以成功 stub 公开。完整 M1 合同以实施规格为准；M2 的 OS waker、I/O 取消、迟到 completion 和完整调度，以及独立发布门禁，继续保持原分界。

## Context

[正式 M1 规格](../spec.md) · [已确认设计](../design.md) · [任务索引与依赖](../README.md) · [开发入口](../../../docs/development.md) · [证据合同](../../../docs/development/evidence.md) · [独立审查材料](../../../docs/agents/review.md)

## Comments

2026-11-02：用户以 `ok` 确认 16 票拆分及阻塞依赖，按 to-tickets 发布为 ready-for-agent。本票尚未执行；满足阻塞依赖后才可领取。

2026-11-03：原生 ARM64 实测 4K 后，由固定完整系统补 64K；先前原始失败、限定范围 tracer 及 `ci=null` 本地观察均保留，未重标为 CI 成功。最终只选择同一次成功重试 `41489263809` 的原 provider 闭包。解决前已吸收集成 `dd28a861c782a4c1844d7a5cd4f4e0d27d7a3f1b`；产品与 evaluator 未改变。

## Answer

实现公开 `sdk_guest.py export/import/verify`，复用原锁定完整系统 runner 和 `core_gate.Evidence`，绑定当前 SDK 配方、完整 ZIP/组件、三个 APE、loader、源码和原始执行字节。共享 guest transport 校验保留原 M0 精确 catalog，并拒绝 qemu-user、附加磁盘、缺失/错误供应输入、guest/page/kernel、日志、摘要、必需用例、时限和 cleanup。既有敏感改动 Full 及 Core/loader/offline 规则未放宽。

[最终验收记录](../15-arm64-full-validation.md)列出精确身份、命令与映射；[成功 Full 41489263809](https://github.com/moorefu/qaplet/actions/runs/41489263809) 在 `21bd317...` 上取得实际 64K 全 98 项 SDK/CLI/C Host/application 及当前完整 M0 235 项。只读官方重放再次通过，八项公开 CLI 回归（含 16 个 mutation 子例）全通过、无 skip。最终 SDK 为 `7ec19ce8...`，只接受该重试的 ZIP `2524580d...`、producer 和 native/guest 报告，未混用相同 aggregate 的先前 ZIP。

原件长期镜像继续独立，`release_ready=false`。本票不替代任务 16 的完整 M1 严格证据门禁，M2 OS waker/I/O 取消/完整调度边界保持不变。

