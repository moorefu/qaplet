# 13: 可分发 SDK 包与完整纯 C 示例

**What to build:** 从供应 SDK 包的公开头文件/静态库构建独立 C 应用，演示模块、异步事件、结果、停止与关闭全路径，并形成可核验分发清单。

**Blocked by:** [11：并发关闭与最终释放的完整合同](11-close-destroy-lifetime.md)、[12：共用 Runtime 的最小 CLI](12-minimal-cli.md)

**Status:** resolved

**Execution state:** completed；最终产品源码 `b636f15426b4f4deba4e878ff036843ca2581ffd` 已吸收 integration `ee84e7e`。Ubuntu supplied 98/98、真实 ZIP 包外编译 8/8、Windows 97/97 required 与 ARM64 64K 完整 guest 98/98 实际通过。见 [验收记录](../13-sdk-validation.md)。

**Execution prerequisites:** Ubuntu 构建环境与固定输入可用，且已合入本票所列阻塞任务；使用仓库约定的共享输入/缓存与现有构建、证据入口。

**Spec stories:** 50、57、58。

## Acceptance criteria

- [x] Ubuntu 产生实际可消费 SDK 包及 CLI/C Host APE，头文件、库、目标组件、能力和有效默认值一致。
- [x] 包外纯 C 消费者只使用供应公开材料即可构建完整应用，不依赖私有引擎或工作树内部头文件。
- [x] ABI 1 的前缀、短结构、兼容零尾部、未知非零 flags/reserved 及独立诊断在真实包消费边界验证。
- [x] 完整示例经公开操作演示注册/ESM/TLA/线程事件/pump/get_result/stop/close/join/destroy，失败路径也可复现。
- [x] SDK 包/目标组件与 APE 摘要、source/recipe/toolchain、命令及日志纳入现有 producer/分发协议，旧 M0 身份不改写。

## Validation and evidence

通过本票实际提供的公开 SDK/CLI 与纯 C Host 外部边界验证，观察公开状态、稳定错误 code、JS/native 副作用、复制数据、退出码和资源关闭事实。复用已有真实 producer、供应制品、宿主 consumer 与原始证据模式；每票执行自身验收及适用的 Cross/Full 门禁，平台收尾票不替代此前回归。

证据记录当前实现/输入/配方/SDK/APE 身份、实际命令、期望与实际结果、stdout/stderr 原始日志、退出码、时限、宿主与执行方式。外部 supervisor 超时表示用例未完成，不能作为产品正确停止/超时的证明；缺资源、失败、skip 与未完成保持可见，不继承旧 M0 成绩。

## Scope boundary

本票交付一个可单独演示和验收的增量，只声明已经实现并实际验证的范围；未实现能力不以成功 stub 公开。完整 M1 合同以实施规格为准；M2 的 OS waker、I/O 取消、迟到 completion 和完整调度，以及独立发布门禁，继续保持原分界。

## Context

[正式 M1 规格](../spec.md) · [已确认设计](../design.md) · [任务索引与依赖](../README.md) · [开发入口](../../../docs/development.md) · [证据合同](../../../docs/development/evidence.md) · [独立审查材料](../../../docs/agents/review.md)

## Comments

2026-11-02：用户以 `ok` 确认 16 票拆分及阻塞依赖，按 to-tickets 发布为 ready-for-agent。本票尚未执行；满足阻塞依赖后才可领取。

2026-11-02：领取本票。测试 seam 沿用 Q32 已批准的公开 SDK、供应静态包 public-header-only C 应用、CLI 与现有 producer/consumer；不新增私有引擎测试入口。按 TDD 实际保存外部包消费红绿证据。

2026-11-02：逐切片保留缺失 sealed package、缺失应用 ABI workload 及坏 ZIP 未生成结构化失败报告的实际 red/green。前两项交付分发与公开消费能力，既有 Runtime ABI 行为原已满足；坏 ZIP 实际缺陷由既有 consumer 协议捕获 BadZipFile 修复。所有早期包、superseded producer 与六类真实包拒绝证据保留，不改写旧成绩。

## Answer

交付 `qaplet-sdk.zip`：65 个显式成员、43 个 SDK 组件，保留真正的 x86_64 与隐藏 `.aarch64` 静态库、公开头文件、完整 `sdk/application.c`、源码 ESM、README、许可证，以及三套经既有 APE linker/packer/publication/audit 验证的 Host、CLI、完整应用与 sidecars/maps/loader/notices。`sdk/manifest.json` 给出 ABI、实际能力、全部有效有限默认值、目标库摘要、源码/输入/工具链/配方与三套 APE 身份。

沿用 `scripts/sdk.py` 增加 `package-consume`，实际解包供应 ZIP 至源码树之外 `/tmp` 应用目录，仅以供应 `sdk/include` 与静态 SDK 编译完整纯 C 应用，复用既有双 CPU 64K 链接/审计及同一 `EXAMPLE_CASES`。包外产出的应用 APE 与供应应用逐字节相同。ABI workload 在此真实边界验证短前缀/结构、兼容零尾部、非零尾部、版本、flags/reserved、截断/零容量诊断及真实工作线程的独立诊断；没有私有引擎头文件、私有分配入口或新测试框架。

完整应用实际执行 Host Module 注册、静态/动态 ESM、TLA、工作线程深复制 POD 事件、owner pump/get_result、worker stop 与 owner STOPPED 确认、close、CLOSED 句柄调用、join、Host FILE 读取/关闭/删除，最后 destroy。错误模式可复现普通 rejection、queue-full、deadline、缺模块与参数错误，完成 native 清理并返回 raw 1/2。临时 FILE 使用 ticket 11 已测的 native TEMP 或 POSIX `/tmp` + mkstemp/fdopen，不重新引入裸 tmpfile 依赖。

最终 producer 为 `out/m1-13/final-v2/build.json`，SHA `4e0d95ca42a4f6fced34dae2a4f55bbeccc952c41201217dadc8ad63d51e9811`；READY 为 `out/m1-13/READY.json`。SDK `778434131e9cfa1448963a1bd507d08f42c501d4decd25297c2788b9f0b569a2`，recipe `89bd63717e3f4134b15096dda379a36c065580c93a1fe3f7d3f45661f81d7c8b`，ZIP `4d3a1802ff41e7d2a655bb800083d87a0b41c938e625bfd3758a4b03c5ef5723`，完整应用 `dc1e572109699b0f6837e14c4ede715468ca9ea907d164fc342b72604af79719`。57 个 recipe 输入均与已提交 blob 相同；source/SDK/APE 保持冻结，之后仅提交本票结票/验收文档。

实际验收：Ubuntu x64 supplied 98/98，外部 ZIP 编译/运行 8/8；父代理独立普通用户 Windows x64 native PE 97/97 required，既有 optional symlink fixture 继续明确 blocked；ARM64 完整系统 guest 的实测 65536-byte pages 上 98/98。独立核验同 SDK/recipe、完整 guest catalog/argv/退出/原始日志与不变 payload。Ubuntu 219 个仓库检查（75 个既有 optional scope skip 单列）及真实系统 C bootstrap 通过。所有 required 产品用例无 skip/timeout。

完整原始 red/green、拒绝、superseded 包、两个目标库及 APE 摘要、命令/日志、平台身份与 replay 见 [13-sdk-validation.md](../13-sdk-validation.md)。完整原生 Cross、M1 Full/strict 聚合及发布门禁仍由 14–16 保留，本票不将 guest 记为 native，也不扩入 M2 waker/I/O cancellation。

