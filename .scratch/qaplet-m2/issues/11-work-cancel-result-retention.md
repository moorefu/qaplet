# 11: 任务取消竞争、结果 release 与容量保留

**What to build:** C Host 对排队及运行任务取消，按确定顺序取得唯一终态；复制/释放结果后，物理未退出任务仍安全占额。

**Blocked by:** [10 · 纯 C Host 的有界原生工作闭环](10-c-host-bounded-work.md)。

**Status:** claimed

**Execution state:** local_complete；本地 C work 取消/释放与物理保留已验证且独立合并，当前组合 Cross/实际64K/Full仍为 `cross_deferred`。

**External blocker:** GitHub 账号付款或支出限额拦截 job 启动，用户已提供页面原文；见[CI账单阻塞](../ci-billing-blocker.md)。独立merger已完成，当前缺口仍为完整平台证据。

**Local development:** complete；按用户2026-11-05的[本地开发指示](../local-development.md)，实现与Linux/Windows验收可支持下游开发。完整Cross仍deferred，Status不提前resolved。

**Execution prerequisites:** 已满足本票直接 blocker；Ubuntu 构建环境及固定输入可用，复用仓库约定的输入、缓存和证据方式。ready-for-agent 表示需求已就绪，不绕过 blocker。

**Spec stories:** 38、42、43、44、45、46、47、48、49、79、81、83、88、89、91、107、110。

**Confirmed decisions:** Q10、Q19、Q20、Q21、Q24、Q26、Q27、Q35、Q37、Q44、Q45、Q52。

**Spec acceptance groups:** AC-10、AC-11、AC-13、AC-14、AC-21、AC-22、AC-24。这些是本票涉及的父规格验收组；复杂组由多票分别完成子合同，再由完整矩阵重放，不把局部成功冒充整组完成。

## Acceptance criteria

- [x] 排队取消不启动；运行中只记录协作取消，不强杀线程，不把取消当成 syscall 已退出或副作用回滚。
- [x] 取消先与完成先各有确定性交接，至多一次终态；保留记录的重复取消幂等，完成先发布报告已完成。
- [x] BUFFER_TOO_SMALL 可重读且不重跑，非终态 release 为 BAD_STATE，release 后旧 token 查询/取消/结果明确失效。
- [x] 128 操作和 8 MiB 数据子额与物理运行/C 结果保留共同计量；logical cancel/release 不提前归还仍使用的数据。
- [x] owner 完成交接才解除默认 ref；过时代次/跨 Runtime 和取消后 close/quiesce 重试无误投或 UAF。

## Blocking details

- [10 · 纯 C Host 的有界原生工作闭环](10-c-host-bounded-work.md)：必须已有真实任务、缓冲和 worker 才能定义取消及 release 竞争。

## Validation and evidence

本票通过实际提供的公开 SDK、JS/最小 CLI 或包外纯 C Host 边界验收，观察公开状态、稳定错误 code、复制数据、JS/native 副作用、输出/退出和资源回收事实。不得以私有布局、内部调用次数或完整异常字符串代替合同。

使用明确接受结果和可控 producer/owner 交接复现竞态，不用随机 sleep 猜顺序。受控系统故障夹具与正常产品分别记录输入及配方；外部 supervisor 超时表示未完成，不能证明产品已成功停止或清理。

每票执行自身正负验收与适用 Cross/Full；24–27 的最终矩阵和长测不替代此前平台检查。全部产品与 SDK 构建在 Ubuntu，其他宿主消费供应制品；同一 APE 字节跨宿主验证，目标 SDK archive/slice 分别标识。

证据绑定当前产品源码、配方、SDK/APE/loader/供应原 ZIP、实际命令与日志、OS/kernel/CPU/页大小/执行方式、实际次数/时长及清理结果。缺项、fail、必需 skip/not_run 或中断保持可见，不能外推旧 M0/M1 成绩。

## Scope boundary

本票交付一个可独立演示和验收的增量，只声明已经完成并实际验证的能力；不提供成功 stub、隐式 fallback 或未经验证的 OS ABI。

资源从首次接入起就受父规格有限默认值、容量预留和安全 stop/close/quiesce/destroy 约束。19/20 负责跨资源联立与组合故障验收，不允许此前延后硬限额或首次关闭安全。完整默认表、线程/重入、结果保留和原生 lifetime 以父规格为准。

保留 M1 旧创建与 ABI 合同；完整 Node 兼容、网络产品 API、整树清理、24 小时发布 soak 和独立发布镜像门禁继续保持原阶段边界。当前任务发布不等于实现或通过。

## Context

[父规格与全部合同](../spec.md) · [任务索引与依赖](../README.md) · [已确认拆分及覆盖映射](../ticket-proposal.md) · [开发入口](../../../docs/development.md) · [SDK 入口](../../../docs/development/sdk.md) · [证据合同](../../../docs/development/evidence.md) · [独立审查材料](../../../docs/agents/review.md)

## Comments

2026-11-03：用户明确回复“按 27 张发布”，确认 27 张纵向任务及 38 条直接阻塞边；本票按已确认提案独立发布为 ready-for-agent，尚未领取或实施。父规格保持不变。

2026-11-04：10当前严格Cross/实际64K闭包完成，领取codex/m2-11-cancel / .cache/worktrees/m2-11，从已含08四类scheduler的当前集成开展公开取消竞争、C结果release与物理保留的TDD。13在并行候选预留PROCESS_ERROR27/domainWIN32=2/cap22；本票如需CANCELLED/ALREADY_COMPLETED新code可用28/29，避免冲突。所有直接blocker已满足，不为共享文件虚构前置。

2026-11-04：本地交付候选产品 `96e8eb4`，先融合实际 integration 的 Process `a5c8b10` 与 tracker `453fe76`，交接前再融合纯 tracker `5c2f019`。公开任意线程 cancel/版本化 status、28 CANCELLED/29 ALREADY_COMPLETED 与 Process27 并存；release 立即失效 token，记录/ref 保留到完整 jobs owner 交接与物理调用完成，STOPPED 终止交接则退休已 release 且物理完成的记录。初始接口红、检查点 release 红及额外 STOPPED 真实滞留红均保留并有永久绿；真实 file 三接受顺序、默认128/8MiB/16MiB、权限/HostNotify TLS guard、不可返回与真实 TLS exit 安全保留完成。最终 Ubuntu 全供应、Linux169/169、Windows168+1既有optional symlink且必需缺口0；密封ZIP包外两宿主各46/46、受控故障各2/2、devcheck370(177实际/193skip)+C bootstrap通过。77 RECIPE raw/产品及当前Git blob/LF、完整原ZIP/SDK/三APE、双archive实际各15 ELF64 REL machine62/183及实际driver/import闭包身份通过。详见[11验证记录](../11-work-cancel-validation.md)与本工作树`out/m2-11/fusion-fixed/handoff.json`；保持claimed/awaiting_cross，不自行主树合并、push、resolved或声明本票当前Full/终审/父M2完成。

2026-11-04（独立合并）：`d8a9ba987d59a19caa085756cd6529914f152da5` 已no-ff合入源tip `d8b5e826` / 供应产品 `96e8eb4`。77项raw/Git blob/LF、完整ZIP79成员/SDK/三APE/双archive15、normal169/package46/fault2原始case和实际driver/import身份及历史红绿独立核验；原3579文件/632,688,371逻辑字节内容及mtime前后不变。post树同源、双树clean/diffcheck、Ubuntu input-lock/两窄CI/真实C bootstrap通过。父Python profile未覆盖make/test子进程的限定如实保留，不伪称实际子进程imports已被profile捕获。详主 `out/qaplet-m2/merge-11/AUDIT.md`、`post.json`。保持claimed/awaiting_cross，后续与13查询及Mac诊断增量一起取得当前五原生/实际64K原件；未放行12/15或完成父M2。
