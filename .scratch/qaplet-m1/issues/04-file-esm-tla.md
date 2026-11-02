# 04: 文件 ESM、动态 import 与 TLA 闭环

**What to build:** C Host 用 run_entry 运行真实文件模块图，相对依赖、动态 import 和 TLA 经统一 loader/pump 完成，并具有规范身份和首次元信息。

**Blocked by:** [03：Promise 与 jobs 的显式分步执行](03-promise-job-pump.md)

**Status:** resolved

**Execution state:** completed；2026-11-02 在 `codex/m1-04` 实现真实文件 ESM、统一规范注册表、首次元信息、TLA/pump 与有限源码计量；合入 02/07 后，最终同字节 SDK/C Host 的 22 项在 Ubuntu、Windows x64 与 ARM64 64K 完整 guest 均通过。[验收与原始证据](../04-file-modules-validation.md)。

**Execution prerequisites:** Ubuntu 构建环境与固定输入可用，且已合入本票所列阻塞任务；使用仓库约定的共享输入/缓存与现有构建、证据入口。

**Spec stories:** 12、13、14、17。

## Acceptance criteria

- [x] 通过真实源码文件运行静态依赖、动态 import、可完成 TLA 及已支持循环用例，结果与副作用可观察。
- [x] 入口相对路径用创建时 cwd，依赖相对路径用导入模块；内存动态 import 使用显式 base URL。
- [x] file 规范身份和统一注册表保证成功模块只求值一次，不能用重复原始 Eval 冒充缓存。
- [x] import.meta.url/main 按首次实例化规则初始化，模块 run 不桥接整个 namespace。
- [x] 有限单源码、源码缓存及模块计量接入既有存储基础，新能力有实际 C Host 证据。

## Validation and evidence

通过本票实际提供的公开 SDK/CLI 与纯 C Host 外部边界验证，观察公开状态、稳定错误 code、JS/native 副作用、复制数据、退出码和资源关闭事实。复用已有真实 producer、供应制品、宿主 consumer 与原始证据模式；每票执行自身验收及适用的 Cross/Full 门禁，平台收尾票不替代此前回归。

证据记录当前实现/输入/配方/SDK/APE 身份、实际命令、期望与实际结果、stdout/stderr 原始日志、退出码、时限、宿主与执行方式。外部 supervisor 超时表示用例未完成，不能作为产品正确停止/超时的证明；缺资源、失败、skip 与未完成保持可见，不继承旧 M0 成绩。

## Scope boundary

本票交付一个可单独演示和验收的增量，只声明已经实现并实际验证的范围；未实现能力不以成功 stub 公开。完整 M1 合同以实施规格为准；M2 的 OS waker、I/O 取消、迟到 completion 和完整调度，以及独立发布门禁，继续保持原分界。

## Context

[正式 M1 规格](../spec.md) · [已确认设计](../design.md) · [任务索引与依赖](../README.md) · [开发入口](../../../docs/development.md) · [证据合同](../../../docs/development/evidence.md) · [独立审查材料](../../../docs/agents/review.md)

## Comments

2026-11-02：用户以 `ok` 确认 16 票拆分及阻塞依赖，按 to-tickets 发布为 ready-for-agent。本票尚未执行；满足阻塞依赖后才可领取。

2026-11-02：先记录公开 C Host red/green 文件模块切片；随后合入 02 的严格值/诊断与 07 的共享执行账本。最终 source `2f15dd6aa90e7466bf89e1a6b81c3fba064c0da8` 已在构建前提交；`out/m1-04/combined/build.json` 为最终 Ubuntu producer，`run.json` 与 `check.json` 通过。父任务收到 READY 后，以相同 SDK/APE/真实模块文件在普通用户 Windows 和 ARM64 64K guest 各运行全部 22 项并通过。扩展失败/路径拒绝及 NUL overlay 由 05 验证；本票不声明完整 M1 Cross/Full 或发布就绪。

## Answer

新增公开 `qaplet_run_entry` 与 `qaplet_eval_with_base`，文件模块能力位为 8。`src/modules.c` 在递归 compile-only 解析前保留规范身份，持有真实源码和编译模块值；显式 run 复用模块求值 Promise，不重复原始 Eval、不桥接 namespace。首次 `import.meta.url/main`、创建 cwd、依赖基准、支持的函数循环、动态 import/TLA 与 PENDING/close 由供应静态 SDK 的纯 C Host 实际观察。默认单源码 4 MiB、源码缓存 32 MiB、256 模块，并公开计量；新 ESM 预算用例验证跨 pump 配额、宿主等待期限、模块循环中断和原生存储失败首因。

最终 22 项在 Ubuntu x64/4K、Windows x64/4K 与完整系统仿真 ARM64/64K 全部通过，无本票用例 skip。准确 producer、SDK/APE/recipe 身份、原始 red/green、命令和各宿主记录见[验收记录](../04-file-modules-validation.md)。其他 M1 票、物理 ARM64/macOS 和发布门禁保持各自范围。

