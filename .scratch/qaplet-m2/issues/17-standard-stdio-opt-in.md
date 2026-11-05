# 17: 标准 stdio 的显式安全接入

**What to build:** C Host 显式接入真实标准流，JS/C 通过统一有界字节操作读取和写入；默认创建不改变宿主标准流。

**Blocked by:** [12 · JS 命名工作、Promise 与取消闭环](12-js-named-work-promises.md)、[15 · 子进程三路有界字节 I/O 与 EOF](15-c-host-process-pipe-io.md)。

**Status:** claimed

**Execution state:** local_complete / cross_deferred；独立分支 `codex/m2-17-standard-stdio`。

**Local development:** complete；公开 C/JS、密封包外 SDK、受控系统故障及真实标准终端分类在本地 Linux/Windows 验收完成。详细范围及保留失败见[验收记录](../17-stdio-validation.md)；原件与最终收据位于该 worktree 的 `out/m2-17-stdio/HANDOFF.md` 和 `handoff.json`。其他必需原生目标、当前实际 64K、CI/Full、最终混合资源长测与发布就绪保持待验；不改变 claimed 状态或下列正式验收合同。

**Execution prerequisites:** 12、15本地完成并已独立合并（`3d03f855`、`65de84e2`），实际集成祖先已核验；可与16并行，不新增对16的阻塞边。验证范围及正式欠项见[本地开发方式](../local-development.md#2026-11-05剩余开发前置与验证范围)。

**Spec stories:** 6、39、59、61、62、63、66、67、68、73、79、81、83、109。

**Confirmed decisions:** Q7、Q8、Q19、Q23、Q24、Q27、Q28、Q34、Q41、Q42、Q47。

**Spec acceptance groups:** AC-02、AC-11、AC-17、AC-19、AC-21、AC-24。这些是本票涉及的父规格验收组；复杂组由多票分别完成子合同，再由完整矩阵重放，不把局部成功冒充整组完成。

## Acceptance criteria

- [ ] 默认不接管标准流；显式接入时实际识别 pipe、文件、TTY/Console 与无效句柄，不以私有 fixture 或 isatty 单值代替。
- [ ] 借用端点不被擅自关闭或修改共享状态，不假设 dup 隔离 flags；不安全接入明确 UNSUPPORTED 且不留下副作用。
- [ ] JS/C 使用同一可靠完成、取消、部分写、BUSY、字节/NUL、EOF 与有限背压合同。
- [ ] 标准流 I/O 同样计入共享操作和存储额度；取消/close 不释放仍在 OS 中使用的缓冲或留下不可说明的保活。
- [ ] 以真实 stdin 重定向、文件和无效句柄 fixture 验证，终端文字/模式仅在其已完成能力范围内声明。

## Blocking details

- [12 · JS 命名工作、Promise 与取消闭环](12-js-named-work-promises.md)：标准流 JS 入口复用异步任务/Promise。
- [15 · 子进程三路有界字节 I/O 与 EOF](15-c-host-process-pipe-io.md)：复用字节端点、可靠完成、取消和数据额度。

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

2026-11-05：本地完成；以 source `61603f2` 的第四份 Ubuntu 供应为最终产品原件，Linux/Windows 消费同字节。独立合并前保存源码/配方/SDK/ZIP、命令、宿主、真实清理与全部红绿。完整工具检查 `devcheck-3` 保持原 source `159bd900` 范围，逐个核验全部 tracked Python 与最终源相同；最后 C/header/adapter detach 修复由第四份完整 native/package/fault/终端验收及当前 preflight 覆盖。正式 Cross/Full 欠项不转为通过。

2026-11-05：按用户指示领取本地实现；保留全部产品、安全和正式验收合同。本地验收并独立合并后放行开发，正式跨平台验收仍保留最终收口。

2026-11-03：用户明确回复“按 27 张发布”，确认 27 张纵向任务及 38 条直接阻塞边；本票按已确认提案独立发布为 ready-for-agent，尚未领取或实施。父规格保持不变。

