# 21: CLI 异步运行与入口结果保存

**What to build:** 最小 CLI 使用 M2 Runtime 运行 timer、Process 和 stdio，入口完成后继续 ref 资源，正常结束后输出保存的入口结果。

**Blocked by:** [20 · 组合资源的停止与两阶段清理](20-mixed-stop-close-quiesce.md)。

**Status:** claimed

**Execution state:** local_complete / cross_deferred

**Local development:** complete

[本地验证](../21-cli-validation.md)及主 `out/qaplet-m2/merge-21-cli/HANDOFF.md`、`handoff.json`
保存独立接收、供应6及供应4回归的真实原件与范围。已独立合入`13abf9ac`；本地开发前置可放行22，
本接收者未领取后继。正式Cross/Full仍待验。

**Execution prerequisites:** 20已本地完成并独立合入`943c1962`（接收`776e50cb`），当前开发前置已满足，已领取。正式Cross/Full仍待验；验证范围见[本地开发方式](../local-development.md#2026-11-05剩余开发前置与验证范围)。

**Spec stories:** 6、7、8、9、10、11、16、17、19、20、21、22、23、24、25、66、94、95、98、108。

**Confirmed decisions:** Q1、Q3、Q5、Q6、Q7、Q15、Q16、Q18、Q24、Q25、Q32、Q33、Q36、Q40、Q47、Q51。

**Spec acceptance groups:** AC-03、AC-04、AC-05、AC-06、AC-19、AC-24、AC-25。这些是本票涉及的父规格验收组；复杂组由多票分别完成子合同，再由完整矩阵重放，不把局部成功冒充整组完成。

## Acceptance criteria

- [ ] run/eval/help/version 和既有参数保持一致，通过统一 SDK/驱动运行新模块，不复制第二套执行器。
- [ ] 入口完成先复制保存，后续 callback/任务结果不覆盖 CLI 输出；eval 只在正常循环结束输出一次可桥接值。
- [ ] 入口 DONE 与保活分开，Promise 独自悬挂报告 UNSETTLED_ENTRY；unref-only 可以返回并进入资源关闭。
- [ ] 后台普通错误/终止错误和用法错误保持 0/1/2 合同，SDK get_result 仍是最近完成执行而非永久入口槽。
- [ ] CLI 显式接入标准流并使用两阶段安全回收，适用 timer/进程/双通道字节行为有真实外部命令验收。

## Blocking details

- [20 · 组合资源的停止与两阶段清理](20-mixed-stop-close-quiesce.md)：CLI 采用混合资源的统一退出状态和安全回收屏障。

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

2026-11-05：20独立接收后由root实际领取；分支`codex/m2-21-cli`、worktree
`.cache/worktrees/m2-21-cli`从当前actual integration起步，fresh实现者按公开行为TDD。
沿既有统一驱动/入口结果副本收口CLI、真实标准路由及0/1/2；22信号专属闭环仍在后继。
本地完成及独立合入才放行22；当前领取不代表正式AC组、Cross/Full或父M2已验收。

2026-11-05（本地完成待接收）：产品冻结4892a897，供应6包外CLI Linux67/Windows72通过，
100输入/91ZIP/双archive19+19独立静态预核。真实overlapped Host输出观察pending→终态，
完成未知负向exit1且原生记录保留，不能记为native清理成功。供应4普通Linux222、Windows
221+既有optional1、TTY13/20、mixed各12保留原身份；公开头/双archive与6同字节，八个
mixed保留负向继续native清理未完。统一drive/入口副本、真实标准路由、独立raw/VT facade
与旧exports快照、最终Host输出错误0/1/2已公开核验。固定CRT在main前503→511与租约
511→raw504→511分开，
不宣称恢复503；真实信号/5秒清理预算专项仍22。普通三mixed用例经ticket-local adapter
按host/case隔离，非默认L0/CI缓存契约。供应4原dirty、供应1–3/5范围、旧失败与相对路径
loader ENOENT保留；不resolved、不自合root、不push/CI。详本地验证与HANDOFF。

2026-11-05（独立本地接收）：fresh merger从clean root`a3e1a628`实际no-ff合入clean源
`9f2019f1`，合并`13abf9ac`的tree与source同为`2ced24bb`。固定产品4892、供应6的100recipe/
91ZIP/双archive19+19/三fat APE及头文件保持同字节；合前合后独立重放8报告、1871raw、
613索引引用及2613实际证据文件通过。CLI6 Linux67/Windows72，供应4普通222/221+optional1、
TTY13/20、mixed各12均按原身份核验；九个明确负向仍native_cleanup_complete=false。
Windows三项隔离mixed的relative executable仅按实际parent ROOT与官方Popen规则精确绑定，
child cwd/原argv/marker/waits不改；原guard失败及解析helper版本保留。合后Ubuntu preflight
pass（49脚本/52编码、主物理.cache）。父503→main511、租约511→raw504→511、历史源重建、
producer dirty=true和动态child imports unknown限制保留。Merger产品执行0，无重建/重复suite；
本地可放行22，正式Cross/64K/Full/长测/整体Review/M2和发布仍待验，原AC/Blocked by不改。

