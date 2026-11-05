# Qaplet v1.5 执行与交付优化

2026-11-05。依据用户工程效率任务书实施，覆盖旧计划中关于开发调度的过期描述；不修改 M2 Q1–Q53、M0/M1 已验收合同或已声明平台。当前开发以本地 Ubuntu 构建、Linux/Windows 消费同一供应字节为主。远程 CI 因用户确认的付款前置问题暂不执行；本地通过不能代替远端通过。

## 当前基线

[工程审计](../reviews/qaplet-development-efficiency-review.md)以 integration `3d03f855` 为锚点：01–10 resolved；11/12/13 已本地完成，跨平台欠项保留；14/15 实施中；16–27 未完成。M1 的成功 Full 属于其原产品配方，不能签给新增 M2。`main` 仍是旧环境历史分支，本次不强行合并、改 main、打标签或发布。

## 已落地的验证分层

| 层级 | 入口及触发 | 实际范围与结论边界 |
|---|---|---|
| L0 Local | `make check-fast`；`make test-engine/test-loop/test-work/test-process/test-tty/test-ci`；`dev.py check-fast --profile …` | 预检、选中公开 SDK/CLI 行为、ESM/Promise、生命周期、Ubuntu 最小公开头 C 编译；只从已验固定缓存构建，不 fetch/QEMU/全项目历史 probe。TTY 尚未实现时明确 blocked |
| L1 Fast | 普通 Engine/JS/应用变更 push，包括 M2 integration；`m0.yml` 的 `fast` | Ubuntu 真实编译当前 SDK、运行选中 Runtime/SDK 行为。移出历史 M0/四类 M2 probe、包外故障总回归和 offline pair；不是仅语法检查 |
| L2 Targeted Cross | 原生 OS/资源改动、公开 SDK 头、集成 PR | 一个 Ubuntu portable producer；选定真实 native runner 消费相同 APE/SDK；现有完整目标内合同与日志保留。公共 ABI/共享原生代码仍选五个声明目标 |
| L3 Full | 每日 03:17 UTC、RC tag、显式 full、工具链/构建/验证关键路径或未知风险 | 五原生、实际 ARM64 页补缺/64K、完整 SDK/M0、断网 pair/refusal、原 ZIP/Core consumer。未来 Node/soak/network 合同保持未实现状态 |
| Release | 正式 RC 的独立发布判定 | Full 加声明 API/平台、长测、供应链、原件长期保存及发布合同；未完成 06 镜像与后续产品门禁前 `release_ready=false` |

L0 争取 30–90 秒、L1 争取 2–4 分钟是测量目标。首次物化当前 SDK 与热供应检查分开计时；冷构建不保证落在 90 秒内。`--plan` 永远不运行产品。旧 `make check`/完整合同入口保留，CI 分层变化需完整本地回归，普通功能票只运行受影响合同。

原 `Makefile` 属于 pinned v3 输入，因此其字节保持不变；GNU make 默认读取新增 `GNUmakefile`，由它 include 原 Makefile 并提供快捷目标。`dev.py` 是同一公开入口，不需要调用者了解 wrapper。SDK/锁坏缓存拒绝，不自动重写原失败；显式 `--supplier-report`/`--supplier-dir` 可选择新的匹配供应。

### 风险和工作流选择

`scripts/ci_tiers.py` 对实际 push before/after、PR merge-base diff 选择；完整历史用于 diff。初始 push 枚举当前 tree；无法取得实际变更即失败，不降级。手工指定层级只能提高已知风险。

| 代表性变更 | M0 层级/目标 | 相关独立工作流 |
|---|---|---|
| `docs/`、`.scratch/`、已知根说明 | scope only，无产品执行 | 仅记录选择，各原检查 job 仍创建 |
| `src/values.c`、execution、模块应用 fixture | push Fast；集成 PR Cross | 不重跑 SCM/Job/Foundation |
| `include/qaplet.h`、共享 Process/Loop/Work 原生代码 | Cross，五 native | 公共头合同不能只验 Linux |
| `sdk_process_winchild.c`、`m2_process_pe.py` | Cross，真实 Windows x64 | 不拿 Linux 结果替代 Windows ABI |
| SCM 或 Job 私有应用 | 对应独立 producer/Windows consumer | 不因应用变更重跑整个 Runtime |
| 共享构建/输入锁/原件辅助依赖 | Full，并传递选择依赖它的服务应用 | 不仅看文件名是否包含服务名 |
| 新未知位置、验证合同或 CI 关键脚本 | Full；未知路径选所有工作流 | 默认保持保守，后续用证据细化规则 |

所有五个现有 workflow 身份和既有检查 job 名保留。复用单一 `.github/actions/ci-scope` 调用相同选择器；不使用 workflow 级 paths 来隐藏检查。无关范围上传 `scope.json`，表示没有选中产品执行，不伪装测试通过。SCM/Job 的 Windows consumer 只在对应 producer 真正被选中时运行。保护设置细项不可读，未改仓库设置。

### 关键路径、并发与复用

Fast 单独生产当前 SDK，不等待 Full。L0 的最小 C 冒烟只用 Ubuntu PATH `cc` 编译公开头调用者为普通 ELF 对象，核对实际 source/header/compiler 闭包前后字节；它不构建 Runtime/portable SDK，不签发包外链接成绩。Full 的 `offline-rebuild` 在独立 Ubuntu job 中运行，和 native consumer 并行，原件使用独立 `out/full-offline/` 命名空间，经 `m0-offline-SHA` 交给同一 Core consumer，避免与 portable producer 的原路径映射冲突。Full Core 仍等待 offline 成功，不删除其 pair、供应拒绝或 recipe receipt。其他 consumer 不编译产品。

Fast/Targeted 的同分支、同 job/平台旧运行可被新提交取消。Full/RC 使用 run ID 隔离且不自动取消；独立工作流的手工运行也使用独立 ID。效率报告 job 不取消证据运行。

本地 supplier 按当前产品 recipe 选择、逐件校验 distribution/ZIP/统一锁后复用；目录存在不代表通过。每次普通 L0 仍实际执行所选行为。`ci_evidence.py reuse` 另提供显式成功结果复用：同 SHA、当前 recipe、当前选中测试合同、明确平台、未过期成功执行，重核原报告、SDK 和原始进程日志。它引用原 producer/report/CI/时间，不签发新 OS、Cross 或 Full 成绩；旧失败、未执行、合同变更、坏字节或陈旧结果拒绝。现有成功 Actions run 则先用 `runs` 收据核对 SHA，原 ZIP 使用既有 `verify` 重放；没有有效证据时不能跳过必需作业。本次不宣称已验证跨 run 自动远端跳过。

## 效率测量与改造前后

`ci_evidence.py efficiency` 离线读取已保存 run/jobs；`efficiency-actions` 只读当前 run 的两份有界 API 原件，m0 收尾 job 自动保留。每个 job 记录已知执行、可测队列、步骤阶段、继承结果、cache/复用未知值及失败分类。步骤名划分的 build 包含校验；阶段合计不是关键路径可直接相减的 CPU 时间。自身收尾未结束时报告明确是快照。

| 项目 | 改造前实证 | 当前代码行为/待测 |
|---|---|---|
| M2 普通 push | 分支名强制 Full | 实际 diff 选择 Fast/Cross/Full |
| 每提交工作流 | 可观察 19 个普通 push SHA 都创建五个 workflow | 仍创建五个 scope 检查；只执行相关重作业，不能称只触发一个 workflow |
| Fast 生产入口 | 9 个直接产品/loader/probe build，加包外/fault consumer | 当前 SDK 一个 producer，加最小公开 C consumer；不再构建无关历史 probe |
| Full native 等待 | producer 包含断网 pair/refusal | supplier 完成即可消费；offline 单独并行，Core 继续等待 |
| 实际 Fast | 两样本 Build 238/321 秒，流程 250/336 秒 | 远端改后未测；不能报告提速百分比 |
| 实际成功 Full | 四样本 Build 350–521 秒、流程 671–789 秒 | 远端改后未测；覆盖合同未减少 |
| p50/p95/cache hit/重复执行率 | 样本不足或缺字段 | null，待同口径采样；不是 0 |

复测命令：

```sh
python scripts/dev.py check-fast --profile ci
python scripts/dev.py check-fast --profile engine
python scripts/ci_tiers.py plan --event-name push --ref refs/heads/codex/m2-integration \
  --event-json out/benchmark/event.json --changed-paths out/benchmark/paths.json --report out/benchmark/plan.json
python scripts/ci_evidence.py efficiency --input-response out/saved-run.json \
  --jobs-response out/saved-jobs.json --tier fast --report out/fast-efficiency.json
```

基准路径集为 docs、values.c、public header、Win fixture、shared Process、SCM、共享构建、未知路径；同一组正负合同验证调度差异。恢复 CI 后分别收集成功 Fast/Cross/Full、失败/排队样本与原始 step 数据，区分冷/热缓存，样本足够前不发布分位数。账单前置失败归 Environment/Runner，缺日志的取消根因保持 Unknown；不归 Runtime regression。

## M2 执行顺序

保持五个 triage label；不另造一套冲突状态。票据 `Status` 表示正式验收，既有 `Local development: complete`、`cross_deferred` 表示本地实现与平台待验两个轴。后继只可消费已稳定的开发接口，不绕过安全、资源和生命周期依赖。

当前先完成 14/15 的实际退出、取消、pipe/backpressure/部分字节及回收；再 16 JS Process → 17 stdio → 18 TTY，19/20 混合预算与关闭贯穿每项首次接入；21–23 收 CLI/SDK。24–27 集中最终 native/64K/soak/独立审查。每票保存本票与受影响回归；不因辅助报告变化重复全部历史工程验证，也不把旧产品证据签到新 recipe。

14/15 本次进行中的工作可完成；P0 效率改造收口前不再扩开新功能票。完整跨平台不作为所有本地后继的调度障碍，正式 resolved/ABI 冻结仍须原验收条件。

## 交付里程碑与真实应用

| 里程碑 | 用户可得到什么 | 退出条件 |
|---|---|---|
| A Runtime Foundation | SDK/CLI、ESM/Promise、有限 Loop/Work/Process/stdio/TTY | M2 资源安全与 Linux/Windows 当前产品合同；最终欠项可见 |
| B Useful CLI | 真实 npm 参数/文件 CLI；Clack 交互向导 | 精选 Node 兼容矩阵，真实包与传递依赖固定，正负输入/重定向/取消恢复 |
| C Portable App Alpha | Linux/Windows 运行同一 APE 应用，技术预览能力声明 | QJB/ZIP VFS/编译器最小闭环、包身份与重定位、真实应用验收 |
| D Expanded Platforms | 扩到 macOS/ARM64/实际 64K | 各目标真实执行、TTY/输入/动态行为，不用架构标签推断 |
| E Qaplet v1 | 可正式发布的 Runtime/Application Platform | 声明范围全通过、长测、安全/供应链/镜像/发布合同，独立 review |

[npm 兼容路线](qaplet-npm-compatibility-roadmap.md)先 `@clack/prompts`，再 `@earendil-works/pi-tui`。复用通用 Host/Loop/Process/TTY，不建 TUI 专用 Runtime；支持范围外 API 明确报错。Alpha 不宣称完整 Node、pi coding agent 或全部终端功能。

### 条件工期（从本轮 P0 收口后计算）

下表是粗粒度工程工作日预算，不是日期承诺；单元格为该段增量。现有仅前十票约 28 小时、JS Work 单次约 49 分钟提交窗口，且有大量跨平台阻塞，不能据此线性外推吞吐。

| 段落 | 乐观 | 基准 | 保守 |
|---|---|---|---|
| M2 剩余本地实现（含混合清理） | 3–5 日 | 1–2 周 | 3–5 周 |
| M2 全阶段验收 | 本地后 1–3 日 | 1 周 | 2–4 周或外部资源等待 |
| M3 精选 Node + Useful CLI/Clack | 1–2 周 | 3–5 周 | 6–10 周 |
| pi-tui 受限场景 | Clack 后 1–2 周 | 3–5 周 | 6–10 周 |
| QJB/APE 最小应用与 Linux/Windows Alpha | Useful CLI 后 2–3 周 | 4–7 周 | 8–12 周 |
| Expanded Platforms/正式 v1 | Alpha 后 3–5 周 | 6–10 周 | 12 周以上，外部阻塞无上限 |

乐观条件：14/15 当前修复稳定、无新增底层 ABI 缺陷、固定输入持续可用、npm 必需面较小、CI 恢复。基准给资源组合、Node 流/解析及终端恢复留返工余量；保守包括 Windows 部分 I/O、原生 Mac/64K、native addon/模块长尾及长测返工。路线包含可并行段，不能简单把所有行相加；每个里程碑后按实际反馈重新估算。计费/runner 不恢复时，本地实现可继续，全平台/正式发布不能承诺完成日期。
