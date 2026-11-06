# 14: Cross 合同矩阵与生命周期循环验收

**What to build:** 在主要宿主消费同字节 CLI/C Host APE，取得完整 Linux 合同和 Cross 关键行为、循环次数的实际证据。

**Blocked by:** [13：可分发 SDK 包与完整纯 C 示例](13-sdk-distribution-c-example.md)

**Status:** resolved

**Execution state:** completed；最终产品/工作流源码 `21bd317f4246571f0048789ca97167632c09f924` 的 CI Linux Fast、五个真实 native 及独立 Cross 回放全部通过，每目标实际 1000 循环。原始 provider ZIP 与失败历史保留；Full `41486421618` 的 guest acquisition 503/未执行仍明确失败，不据此声明完整 Full/64K。

**Execution prerequisites:** 匹配的新 SDK/CLI/C Host 供应制品，以及 Windows x64、macOS x64/ARM64、Linux ARM64 和 Linux Fast 的实际执行资源。

**Spec stories:** 58、59、60。

## Acceptance criteria

- [x] Linux Fast 全部本规格正/负合同必需执行，至少 1,000 次创建/关闭/销毁。
- [x] Windows x64、macOS x64/ARM64、Linux ARM64 消费供应制品，分别至少 100 次生命周期及关键 SDK 合同。
- [x] 核对实际 SDK/APE 身份、OS/CPU/loader/page-size/mode，逐例记录期望/实际、退出码、时限和原始日志。
- [x] 各票已有 smoke/敏感变更回归继续运行，本票补齐完整矩阵与声明，不把首次跨平台检查推迟到收尾。
- [x] 缺目标、失败、skip 或未完成明确阻断对应通过，固定引擎循环/TLA 只声明实际通过子集。

## Validation and evidence

通过本票实际提供的公开 SDK/CLI 与纯 C Host 外部边界验证，观察公开状态、稳定错误 code、JS/native 副作用、复制数据、退出码和资源关闭事实。复用已有真实 producer、供应制品、宿主 consumer 与原始证据模式；每票执行自身验收及适用的 Cross/Full 门禁，平台收尾票不替代此前回归。

证据记录当前实现/输入/配方/SDK/APE 身份、实际命令、期望与实际结果、stdout/stderr 原始日志、退出码、时限、宿主与执行方式。外部 supervisor 超时表示用例未完成，不能作为产品正确停止/超时的证明；缺资源、失败、skip 与未完成保持可见，不继承旧 M0 成绩。

## Scope boundary

本票交付一个可单独演示和验收的增量，只声明已经实现并实际验证的范围；未实现能力不以成功 stub 公开。完整 M1 合同以实施规格为准；M2 的 OS waker、I/O 取消、迟到 completion 和完整调度，以及独立发布门禁，继续保持原分界。

## Context

[正式 M1 规格](../spec.md) · [已确认设计](../design.md) · [任务索引与依赖](../README.md) · [开发入口](../../../docs/development.md) · [证据合同](../../../docs/development/evidence.md) · [独立审查材料](../../../docs/agents/review.md)

## Comments

2026-11-02：用户以 `ok` 确认 16 票拆分及阻塞依赖，按 to-tickets 发布为 ready-for-agent。本票尚未执行；满足阻塞依赖后才可领取。

2026-11-02：领取本票；13 已 resolved 且属于本分支祖先。沿用 Q32 已批准的 SDK/CLI/纯 C Host 与现有 producer/native consumer seam，不新增私有测试边界。复用同一 m0.yml 原生 job 和供应 SDK 字节；缺项、skip、timeout 与 TCG 均不冒充必需原生 Cross 成绩。

2026-11-02：完成逐切片 TDD 与 source-ready。公开 matrix 入口缺失、旧 verifier 放过移除 SDK CI、旧 native 报告缺生命周期字段的真实 red，以及四类实际报告 mutation 的 green 拒绝均保留。Ubuntu 98/98 + 1000 次、包外 ZIP 8/8、父代理原生 Windows 97 required + 1000 次暂定通过；三种缺失 native 仍明确 blocked。合并 15 后源码冻结 `0f4f765`，父代理已发布并触发 [Full 41463967070](https://github.com/moorefu/qaplet/actions/runs/41463967070)。本票仍 claimed，待同一新 producer 的完整 native 字节闭包。详见 [14 验证记录](../14-cross-host-validation.md)；不将暂定本地结果与另一 CI producer 拼接。

2026-11-02：原 Full 的两个 macOS 目标均实际完成 98 项及 1000 循环，但两者 `module-aliases` 清理 unlink 失败，Intel 另有 `stop-running` gate 失败；对应 native 不能通过。父代理保留并验证原 provider ZIP 于主工作区 `out/m1/ci/14-15-macos-collection.json`。应用 diagnosing-bugs，先给出四个可证伪假说，再以 `5e4f772` 增加仅失败时的时钟/API/文件身份诊断和每 Mac 五次紧循环，不修改原断言、不跳过失败、不提前认定 fixture 或 Runtime 原因。原 `0f4f765` producer/run 与所有原件保留；修复后须新 producer 和完整重验。

2026-11-03：`41469391051` 五轮重复证实大小写为同目录项（同 dev/ino；先删大写后小写 ENOENT）和 Intel CLOCK_PROCESS CPU 时钟 EINVAL。候选 `d4c7122` 的清理修正在两个 Mac 均通过，ARM64 98/98；Intel 还明确缺 CLOCK_THREAD/RUSAGE_THREAD（均 EINVAL），未把缺测变成功。候选 `2160989` 以 bracketed process CPU 减 encompassing monotonic wall 与分辨率余量取得 owner CPU 保守下界；native barrier 的 owner-held 控制必须不产生伪进度，释放后仍必须实际测得 >=20 ms。Ubuntu 完整 98/98 + 1000、强制两 CPU 十轮通过（20.9–23.4 ms 下界），强制单 CPU 正确保守拒绝（两秒后下界仍零）。等待父代理实际 Mac 证明，再移除临时 DEBUG/probe 并以最终新字节重跑完整 Full；原始失败归档/本地负向证据不删除、不改写。

2026-11-03：`2160989` 的 Cross `41480615619` 实际成功后，移除全部临时诊断/强制模式/重复 probe workflow，保留 CPU 安全控制和原 SDK 断言，形成清洁源码 `21bd317`。该源码的实际 run `41486421618` 中，Ubuntu Fast 98/98 + 1000、包外 SDK 8/8、五 native 各 98 实际用例 + 1000 循环及 SDK Cross 聚合全部通过。Windows 97 required，额外 symlink 用例在该 CI 实际通过；本地普通用户的历史 optional blocked 仍保留。以九份原 provider ZIP、显式路径映射和既有 matrix CLI 独立回放 Cross 通过，见 `out/m1-14/final/{selection,replay,cross,ci-fast-package}.json`。该 run 的整体 Full 因锁定 guest acquisition HTTP503 而失败，未发生 guest 执行；不被 native 成功覆盖。相同源码的 retry `41489263809` 与 SDK64K/完整门禁由 15–16 独立记录，不能混用 producer。父代理已安排 16 修正 evaluator-only loader 权限准备边界；14 回放仅对验证副本准备权限，所有原 provider 字节保留。

## Answer

复用 `sdk.py` 的实际供应包/Host/CLI/完整 C 应用及既有五 native jobs，交付结构化生命周期与 Fast/Cross 聚合，不增建平行测试框架。SDK 上传显式保留隐藏 ARM64 静态库，CI Fast 继续实际包外编译，所有 native 均消费同一 Ubuntu 字节；既有 M0 敏感 Full、只读权限与离线规则保持。

最终验收源码为 `21bd317f4246571f0048789ca97167632c09f924`。实际 CI producer report SHA `13038eac607c28d70c1a0249172a47b38a4cc4f3ffc169e37393bcfe27a1d307`，SDK `7ec19ce8f7dc6eacbe40d650077a60bf463eff77da4baa87552c3b9f81048ed1`，recipe `5a86423264e76e2213d12ca9fbfbc1023fb1360122a81cd28bd00edcf2d068f8`，ZIP `44f2e58fdd96a2346f414bd690faf9b5741ecf720598e276d30bdd0628b63205`。Linux x64/ARM64、Windows x64、macOS x64/ARM64 的原始报告、CPU/OS/kernel/page/mode/loader、98 项期望/实际/命令/退出/时限/raw logs 及各 1000 次生命周期均经匹配原件回放。每目标必需用例无 skip/timeout；循环/TLA 仅声明实际 catalog 的固定引擎子集。

本票如实修正首次 Mac 实测发现的两项 Host fixture 问题：文件清理服从实测目录项关系，仍保留完整模块 URL/硬链接/副作用断言；缺少线程 CPU 查询时用独立受控的保守 CPU 下界证明实际 owner 进度，仍要求 >=20 ms 并拒绝不足观测。没有 Runtime/libc/ABI/锁变更，未用失败、缺项或时间等待伪造成功。全部 red/green/失败归档与清洁/暂定 SDK 保留。

同一清洁源码的 Full retry `41489263809` 随后实际成功，父代理已保留单独的完整 provider 闭包；其 SDK aggregate/recipe 相同但 ZIP 为 `2524580d49e556dc94cbfd51e1a662953279a121f6b051a3b99123a878e7bbb6`。本票 native 结论仍绑定上述 first-run Cross producer，不混用两个包/producer；15–16 以 retry 完整单一集合签发各自 64K/Full/strict 结论。first-run 503 与未执行历史不被改写。

清洁本地 producer 98/98 + 1000、包外 8/8、Fast 回放、230 项仓库检查（149 pass、81 个显式范围 skip）及真实 C bootstrap 通过；CI 原件的 Fast/包外/五 native/Cross 独立回放也通过。完整身份、原 ZIP 选择、命令、原始证据、Mac 红绿与 remote Full 503 限制见 [14-cross-host-validation.md](../14-cross-host-validation.md)。本票只关闭实际已满足的 native Cross 增量；64K、完整 M1 strict 和独立发布镜像门禁分别保留给 15–16 与既定后续范围，`release_ready=false`。

