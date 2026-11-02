# 11: 并发关闭与最终释放的完整合同

**What to build:** 完整应用可在普通失败、资源 FAILED 或 PENDING 时 close；生产者安全撤下后 destroy，native 清理期间绑定保持有效。

**Blocked by:** [09：完整资源配置、计量与永久分配失败](09-resource-limits-failures.md)、[10：饱和与运行中的协作式停止](10-cooperative-stop.md)

**Status:** resolved

**Execution state:** completed；最终源码 `51e342f73a54e8dd295bc4bdf1c1a6890bfdb030`，已合入 09、10、12 及最新 integration 文档；Ubuntu 90/90、Windows 89/89 required、ARM64 64K 完整 guest 90/90 实际通过。详见 [验收记录](../11-close-validation.md)。

**Execution prerequisites:** Ubuntu 构建环境与固定输入可用，且已合入本票所列阻塞任务；使用仓库约定的共享输入/缓存与现有构建、证据入口。

**Spec stories:** 5、45、46、47、48、49。

## Acceptance criteria

- [x] owner 未执行 JS/宿主回调时可 close PENDING/STOP_REQUESTED/STOPPED/FAILED；重入 BUSY，非 CLOSED destroy 为 BAD_STATE。
- [x] 先拒收/停止调度并释放事件与 JS 引用，保留宿主绑定到引擎析构完成，最后发布 CLOSED。
- [x] 控制句柄与同步存储活到 destroy，生产者在 close 前后查询/投递得到稳定拒绝，不触及已释放队列。
- [x] 宿主撤下生产者并等在途调用结束后最终释放；SDK 不承诺检测未来调用或悬空指针。
- [x] 重复 close 不重复清理，可检测关闭错误单独报告不覆盖首因；部分初始化失败及 stop/分配失败竞争的收尾有实际 C Host 验证。

## Validation and evidence

通过本票实际提供的公开 SDK/CLI 与纯 C Host 外部边界验证，观察公开状态、稳定错误 code、JS/native 副作用、复制数据、退出码和资源关闭事实。复用已有真实 producer、供应制品、宿主 consumer 与原始证据模式；每票执行自身验收及适用的 Cross/Full 门禁，平台收尾票不替代此前回归。

证据记录当前实现/输入/配方/SDK/APE 身份、实际命令、期望与实际结果、stdout/stderr 原始日志、退出码、时限、宿主与执行方式。外部 supervisor 超时表示用例未完成，不能作为产品正确停止/超时的证明；缺资源、失败、skip 与未完成保持可见，不继承旧 M0 成绩。

## Scope boundary

本票交付一个可单独演示和验收的增量，只声明已经实现并实际验证的范围；未实现能力不以成功 stub 公开。完整 M1 合同以实施规格为准；M2 的 OS waker、I/O 取消、迟到 completion 和完整调度，以及独立发布门禁，继续保持原分界。

## Context

[正式 M1 规格](../spec.md) · [已确认设计](../design.md) · [任务索引与依赖](../README.md) · [开发入口](../../../docs/development.md) · [证据合同](../../../docs/development/evidence.md) · [独立审查材料](../../../docs/agents/review.md)

## Comments

2026-11-02：用户以 `ok` 确认 16 票拆分及阻塞依赖，按 to-tickets 发布为 ready-for-agent。本票尚未执行；满足阻塞依赖后才可领取。

2026-11-02：`codex/m1-11` 已确认继承当前 integration；09、10 均已 resolved 并合入，领取本票。测试 seam 沿用 Q32：公开 C SDK、供应静态 SDK 的 public-header-only C Host 及既有 producer/consumer，覆盖关闭期间生产者、绑定存活、半初始化回滚及终止竞争。

2026-11-02：按三次实际公开边界切片交付 close-states、close-concurrent、close-competition；现有 Runtime 机制原已满足所测生命周期行为，保留 pre-existing-green 事实，不制造 Runtime red。Windows 的 `tmpfile()` fixture 失败按 diagnosing-bugs 定位为 relative-backslash 启动路径交互，改用原有 native TEMP / mkstemp / fdopen 模式后，原始完整 consumer 转绿。初始失败、各路径对照及两套原始 producer 均保留。

## Answer

交付 `sdk/host-close.c` 与既有 `scripts/sdk.py` required case catalog，纯 C Host 只消费公开头文件和供应静态 SDK；公开头文件与 SDK 文档明确完整关闭顺序、宿主绑定保留期、CLOSED 控制句柄/同步存储及 destroy 的生产者撤下/在途 join 责任。本票未新增 native cleanup hook，也未改变 Runtime 或未发布 factory 的分配回滚边界。

实际覆盖正常结束、普通异常、OPEN/PENDING、STOP_REQUESTED/PENDING、STOPPED、MEMORY_LIMIT FAILED；回调重入 close/destroy BUSY 与非 CLOSED destroy BAD_STATE；无效诊断不关闭、重复关闭不重复 drop/清理；jobs、events、finally、FinalizationRegistry 不执行额外用户回调。真实 Host FILE/userdata 活至 close 返回，宿主自行关闭和移除资源。

并发生产者通过公开 dropped/lifecycle 快照实际观察 native teardown 重叠；close 返回后每个 worker 仍完成至少 100 次稳定 CLOSED 调用，随后由宿主撤下并 join，最后 destroy，释放后不使用旧指针。真实引擎 byte-return 分配拒绝与工作线程 stop 通过条件变量分别固定两种记录先后，公开返回、诊断、进度与关闭后快照保留首因。已有 required C Host 还实际执行 128 个引擎 factory 配置、32 个 native factory 配置及失败后恢复和 1,000 次生命周期循环。当前释放 API 不返回可恢复 cleanup 错误；未伪造该错误路径，未来可检测 cleanup 错误仍须独立于首因报告。

最终 Ubuntu producer 为 `out/m1-11/combined-final/build.json`，原始 SHA-256 `3cbedee072e721cacdafa556e11ff25c9f98df8589194e18f44d53a392fc5027`；READY 指针同目录。SDK 为 `0329f0be8e4cd3bc745a43839ddee8969baa2777ca5c0891896cd67efed690ec`，配方为 `27f21aaac7843f398618863424f593cc1723aa5c4388f1983b6d6c04b6e719ec`，Host APE 为 `f7a9ffa684d4e48680253ad39c1ba027389ec5102c8853a08e72bf795d88c2b3`。最终源码已合入 integration `ef0c80a2e59de20bba5c33fc54abca212eb9aa26`，源码/配方/制品保持冻结，随后仅提交本验收与结票文档。

同字节 supplied consumer：Ubuntu x64 90/90；父代理独立普通用户 Windows x64 native PE 89/89 required，既有 optional symlink fixture 仍明确 blocked；ARM64 完整系统 64K guest 90/90，Linux aarch64、`6.8.0-147-generic-64k`、实测 65536-byte pages。所有 required 输出/退出符合预期且无 supervisor timeout。Ubuntu `dev.py check` 实际通过 219 测试，75 个既有 optional scope skip 与真实系统 C bootstrap；不将 skip 计作产品通过。

完整输入、两套 producer、红绿 fixture 事实、原始 stdout/stderr/exit/时限、宿主执行方式及 replay 见 [11-close-validation.md](../11-close-validation.md)。原生 macOS/Linux ARM64 Cross、完整 M1 Full/strict 聚合和发布门禁仍由 14–16 保留；本票不替代那些尚未完成的范围，M2 waking/cancellation/recovery 不扩入。

