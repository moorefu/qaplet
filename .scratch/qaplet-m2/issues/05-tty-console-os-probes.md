# 05: 真实终端租约、resize 与 Console 文本探针

**What to build:** 在受控 PTY/ConPTY 和真实标准句柄路由中验证模式恢复、尺寸通知与 UTF-8/VT 子集，给出 Console 与二进制流的实际能力边界。

**Blocked by:** None (can start immediately)。

**Status:** claimed

**Execution state:** local_verified_cross_pending

**Execution prerequisites:** 已满足本票直接 blocker；Ubuntu 构建环境及固定输入可用，复用仓库约定的输入、缓存和证据方式。ready-for-agent 表示需求已就绪，不绕过 blocker。

**Spec stories:** 67、68、69、70、71、72、73、100、103。

**Confirmed decisions:** Q13、Q14、Q23、Q27、Q41、Q42、Q49。

**Spec acceptance groups:** AC-19、AC-20、AC-26。这些是本票涉及的父规格验收组；复杂组由多票分别完成子合同，再由完整矩阵重放，不把局部成功冒充整组完成。

## Acceptance criteria

- [x] 区分真实标准输入和私有 CONIN$/PTY，不能从私有 Console 成功推断 fd0 是 Console。
- [x] 模式修改必须读回并完整恢复；尺寸及变化通知有真实观测，关闭 fixture 资源可核对。
- [x] 验证 UTF-8、Unicode、分块输入/输出和非法数据，不能用替换解码或剥离控制字符当作逐字节证明。
- [x] 观察取消/关闭与共享模式、借用端点的影响，明确安全可用及 UNSUPPORTED 的范围。
- [ ] 受控设施、失败历史、供应字节及各适用目标原始证据完整；不增加面向用户的完整 PTY 创建 API。

## Blocking details

无实施票前置，可立即领取；仍须遵守已完成 M1 基线与开发约定。

## Validation and evidence

本票的纵向边界是 Ubuntu producer、供应原生探针、真实 OS 操作与结构化 consumer 报告。先实测再选择 backend，保留成功、失败、能力限制和实际资源清理；探针结果不自动成为产品支持声明。

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

2026-11-03：独立 worktree 在 codex/m2-05-tty 实施，先合并集成点 93a7aa6，再合并 c338ca9 / af6c0ff，未 reset。独立 Ubuntu producer 供应同一 `m2-tty.com`，Linux x64 4K 19/19、Windows x64 4K 21/21、实际完整系统 ARM64 64K 19/19 通过；Linux/Windows 各 7 项 CLI 合同全部通过、0 skipped。Windows 原始 libc fd0 为 other，显式 native 标准 HANDLE 路由另行检测；ConPTY 非法及补充平面输入实测丢失，明确 UNSUPPORTED，保留原始事件。原件、真实 red/green、失败历史、公开命令和限定范围见[本票验证记录](../05-tty-validation.md)。

当前仍为 claimed；第五条适用目标原件完整性待集成 Cross/Full，不以本地成功或旧 M0 成绩 resolved。未修改公共 workflow/ci_tiers/README，也未修改 M0 配方/源。

最终 producer 绑定已提交实现 `4044c7e`，原件位于本 worktree `out/m2/tty/verified/`；逐一核对 source/recipe 字节与当前源码一致。最终三目标再次全部通过，两宿主各 7 项 CLI（含 5 种真实 guest 反向变体）全部通过。Ubuntu 当前源码完整 `make check` 352 项回归成功，其中 179 项执行通过、173 项既有条件 skipped；本票显式合同 0 skipped。只读 Git 路径映射前后共享 `.git/config` 摘要不变，原始 18 项路径失败和所有历史失败保留。

