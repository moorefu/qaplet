# 02: 唤醒与 Socket 等待的真实 OS 探针

**What to build:** Ubuntu 生成并分发原生探针，在真实目标验证跨线程唤醒、Socket readiness/错误/关闭及无丢唤醒所需事实，产出可消费的 backend 选择证据。

**Blocked by:** None (can start immediately)。

**Status:** claimed

**Execution state:** in_progress；旧 `196cd75` 六目标各19项验收保留；当前 `867356c` Windows socket.connect-error 回归另行调查。

**Execution prerequisites:** 已满足本票直接 blocker；Ubuntu 构建环境及固定输入可用，复用仓库约定的输入、缓存和证据方式。ready-for-agent 表示需求已就绪，不绕过 blocker。

**Spec stories:** 26、27、28、34、100、101、103、104。

**Confirmed decisions:** Q8、Q9、Q14、Q19、Q27、Q49。

**Spec acceptance groups:** AC-07、AC-11、AC-26、AC-28。这些是本票涉及的父规格验收组；复杂组由多票分别完成子合同，再由完整矩阵重放，不把局部成功冒充整组完成。

## Acceptance criteria

- [x] 使用固定输入和供应探针字节，记录各适用目标的 OS、CPU、页大小、执行方式和逐项原始结果。
- [x] 真实阻塞等待能被跨线程通知打断；覆盖合并/多余通知、关闭竞争和控制失败，不用固定短轮询冒充 wake。五原生及实际 64K 均有当前供应字节的逐项结果。
- [x] 验证 Socket readiness、错误、HUP/EOF、非阻塞 connect、半关闭及复用，不把平台 revents 常量当成统一值。Windows poll 限制及显式 select 分路保留；首次 Mac 失败原件不改写，修复后的新 Mac 原生结果已严格验收。
- [x] 用明确交接覆盖检查前、准备睡眠及已等待顺序；辅助线程方案有实例可实施的有限数量和取消边界。
- [x] 未完成、失败或头文件存在均不能签发已验；探针与产品支持范围及输入身份分开记录。

## Blocking details

无实施票前置，可立即领取；仍须遵守已完成 M1 基线与开发约定。

## Validation and evidence

本票的纵向边界是 Ubuntu producer、供应原生探针、真实 OS 操作与结构化 consumer 报告。先实测再选择 backend，保留成功、失败、能力限制和实际资源清理；探针结果不自动成为产品支持声明。

实施及逐目标原件见 [02 验收记录](../waker-reactor-validation.md)。正式源提交 `804f521`，同一 APE `35bf4ffb75ec41476daa99d84ae145995a202eaa789ed91f91f2906c8a5642f9` 在 Linux x64 / Windows x64 / ARM64 实际 64K guest 各 19 项通过；Linux / Windows 各 6 项公开 CLI 合同全部通过，0 skipped。macOS 两目标及 Linux ARM64 native 缺口保持可见。

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


2026-11-03：实施分支 `codex/m2-02-waker`，基于 integration 基线 `5685bb6`。采用独立 `scripts/m2_waker.py` / `tests/native/m2_waker.c` 与公开 CLI 合同；Ubuntu 构建、供应同字节消费。初测 Linux 18 项通过；Windows 正常用户上下文发现失败 connect 的 poll 差异，已以显式 select 分路实测，正式重建及其他目标验证进行中。受限 Windows main 前超时、旧 M0 对照与正常用户执行分开保留，不计为产品失败或通过。

2026-11-03：正式补齐 19 项并完成三个本地实际目标及两平台 CLI 全验收；合并 integration tip `791af3d`，将本票保留为 awaiting_cross。验收记录包含全部输入/配方/供应物身份、原始日志位置、失败历史、Windows backend 差异与尚未执行目标，不将局部通过冒充整个父验收组完成。

2026-11-03：首次集成 Full `41599033541` 的两 macOS 均为 18 pass / 1 fail，唯一失败为 `socket.connect-error`；保存原件通过不变 consumer 重放仍红。修复分支 `codex/m2-02-macos-fix` 仅在真实 nonblocking connect 前实测释放 loopback 端口预留，记录关闭和目标端口，未改 consumer 或等待时限。Apple XNU 源码支持 bound 未 listen 端点会丢弃报文的假设，尚未由新 macOS 原生执行确认因果。新 APE `c1507c60f6800e45847fef05bb7a5cc1d4a5a110fb65dda221d93746beb630b7` 在本地 Linux/Windows/实际 ARM64 64K 各 19/19、两平台各 6 项公开 CLI 合同通过；原件及候选修复边界见 [02 验收记录](../waker-reactor-validation.md#首次-full-macos-失败与最小夹具修复)。本票继续 awaiting_cross，不以旧 trace 重放或局部回归签发新 macOS 通过。

2026-11-03：合入最新 integration tip `af6c0ff`。第一次完整 Ubuntu check 的真实供应物单测出现一次未归因失败，原笼统断言及临时目录清理未留下具体失败 case；保留首跑红灯，新增非 pass case / actual 失败诊断。相同环境随后 100 次公开 CLI 均通过，第二次完整 check 的 345 tests（169 skipped）及真实 C bootstrap 通过。未扩大时限或放宽判据，不把未复现的首次红灯称为已确诊/已修复；具体边界见验收记录，本票仍 awaiting_cross。

2026-11-03（集成验收）：196cd75 / Full 41607608424 的五原生及实际 64K 各 19 项严格重放通过，原件、真实 loader、源配方、CI、guest serial/payload/cleanup 与当前 M0 Full 235 项均核验；见主 out/qaplet-m2/full-196cd75/verify02/VERIFICATION.md。02 自身完成并放行 06；当前 run 整体 failure（独立 04/05 失败）、首次 Mac 失败及父 M2 未完成事实均保留。

2026-11-04：867356c / Full41799595408 Windows socket.connect-error再次失败：connect=-1/errno115，poll=0/2000ms/revents0，select=-2未得readiness，SO_ERROR记录10061但connection_failed=false；reserve_closed_before_connect=true。原ZIP及waker.json保留在主out/qaplet-m2/full-867356c。重新领取codex/m2-02-win-error-fix / .cache/worktrees/m2-02-win-error-fix，先真实复现/区分具体OS事实与判据，不凭errno数字猜唯一根因或以延长timeout替修复。旧196cd75验收及后续独立产品scope保留原范围。
