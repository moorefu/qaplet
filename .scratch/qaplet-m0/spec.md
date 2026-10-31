# Qaplet M0：Core APE、工具链与原生风险探针

**Status:** ready-for-agent  
**Baseline:** 总体计划 v1.4（App-first / 本地优先与原生页覆盖），2026-10-31；SHA-256 `C1A16D76C2453EF42BAFE9DCACE9673CCEA0FC1C95FBF6A9CFAA77C93A0E8237`。  
**Execution state:** BOOT-01 / ENV-01 已完成工程入口与现有 Ubuntu-22.04 环境验收（见[记录](wsl2-environment-validation.md)）。BOOT-02 已锁定并在 Windows / WSL 验证实际 QuickJS 提交，见[源码记录](quickjs-source-validation.md)。BOOT-03 已完成官方 Nightly 原始 GitHub 制品锁定、编译器测量、断网缓存校验及损坏拒绝验收，见[工具链记录](nightly-toolchain-validation.md)。正式产品 APE 构建/矩阵仍待后续任务，不声明任何 Runtime 能力已验证。
**Tracker:** 本地 Markdown，任务见[索引](README.md)。

**Execution override（2026-11-01，用户确认）：** 对后续 SCM / Job Object 探针，用户明确要求“直接使用本机”。因此 11/12 的本地执行改用当前 Windows 开发宿主，不再等待隔离 VM；只操作唯一命名的临时测试服务和受控 fixture 进程，执行有时限并保存清理结果。实际权限不足时记录 blocked，不把当前宿主标成隔离 VM。此确认覆盖下文及 ADR 0003 中针对本轮服务探针的本地隔离 VM 前置；Core/App 边界和独立验收不变。

## Problem Statement

Qaplet 尚没有可执行工程、锁定构建输入或新项目的跨平台运行证据。后续 Runtime、Node API、SDK 和应用交付均依赖 APE/工具链与底层系统行为的可行性。需要以有限的原生探针建立可追溯的地基，并让服务应用的专有 ABI 风险独立于 Core。

规划时曾因旧入口和受限进程观测判断 WSL 缺 kernel/发行版。实施时在普通用户上下文确认现有 Ubuntu-22.04 为 WSL2 Linux x64/4096 字节页，ENV-01 完整验收已通过。BOOT-01 已有提交及首次远程 bootstrap 结果；详见[环境记录](environment-check.md)与[CI 记录](foundation-validation.md)。Linux ARM64仍优先原生runner并用QEMU补缺，SCM/Job本地隔离VM探索、托管Windows VM持续回归；环境/bootstrap 结果不是产品运行成绩。

## Solution

建立可诊断、可锁定、可重构的 M0 工程；在 WSL2 Linux x64 生成 fat APE Hello 和最小 QuickJS C eval 探针，并将相同制品分发到计划要求的native宿主或完整系统QEMU guest。用最小系统探针记录64K页面、线程、资源、I/O和进程行为，汇总可审计的Core门禁。

M0-SVC另外构建应用私有SCM/Job探针，优先在本地可丢弃Windows VM探索，再由GitHub托管Windows runner持续验证；服务能力失败形成独立阻断项。M1以后的产品功能仍保持总体计划中的阶段边界。

## User Stories

1. 作为维护者，我希望运行环境诊断，以便知道当前主机具备什么、缺少什么。
2. 作为开发者，我希望具备真实WSL2 Linux x64环境，以便使用统一的APE构建路线。
3. 作为贡献者，我希望明确自有代码采用MIT，以便遵循项目许可约定。
4. 作为构建者，我希望锁定实际解析的QuickJS提交，以便重取同一源码。
5. 作为审核者，我希望区分原版源码与补丁，以便追踪每项引擎修改。
6. 作为构建者，我希望锁定Nightly原归档和编译器身份，以便避免滚动内容改变构建。
7. 作为审核者，我希望未知的libc源码关联如实记录，以便不依赖猜测的provenance。
8. 作为开发者，我希望显式获取依赖后可离线构建，以便在无网环境复现。
9. 作为维护者，我希望缺失或损坏缓存立即失败，以便不使用错误输入。
10. 作为开发者，我希望编译原生Hello和最小QuickJS表达式，以便证明工具链与引擎可行。
11. 作为测试者，我希望每个制品有唯一摘要和构建记录，以便跨主机比对身份。
12. 作为使用者，我希望相同APE在已声明宿主运行，以便验证可携带交付。
13. 作为审核者，我希望知道实际执行架构，以便避免把模拟执行误认为ARM64原生。
14. 作为测试者，我希望在真实64K Linux验证系统合同，以便提前发现loader和映射问题。
15. 作为后续Host开发者，我希望取得socket/pipe/TTY/进程的行为矩阵，以便据此设计适配。
16. 作为测试者，我希望探针超时有界并清理资源，以便测试失败后环境仍可使用。
17. 作为维护者，我希望未接入的主机保持待验，以便不会被空结果或skip误导。
18. 作为Core使用者，我希望默认制品不链接服务专有代码，以便平台边界稳定。
19. 作为服务应用开发者，我希望独立验证SCM ABI，以便在实现业务前识别Windows缺口。
20. 作为服务应用开发者，我希望独立验证Job基础行为，以便明确可管理进程的原生边界。
21. 作为发布负责人，我希望Core与服务探针独立判定，以便局部失败不扩散到无关发行物。
22. 作为后续实现者，我希望每张任务有前置、测试、证据和回退说明，以便从新会话继续。

## Implementation Decisions

### 已确认的边界

- 沿用Bellard官方QuickJS master、摘要锁定的Cosmopolitan Nightly、Cosmopolitan Host ABI和目标CPU组合的fat APE；不迁移旧fork。
- 自有代码采用MIT；第三方组件保留各自许可并记录来源，不将项目MIT声明套用于全部依赖。
- 默认Core/通用SDK没有SCM、Job、服务配置或systemd业务；M0-SVC属于服务应用的私有原生探针。
- QuickJS源码锁与Nightly锁相互独立；消费两者的最小编译/eval探针置于构建任务。
- M0最小eval只验证原版引擎编译、确定性求值和错误路径；不形成正式Runtime API或稳定SDK。
- 主构建环境为WSL2 Linux x64；本机Windows执行普通探针，编码与探索优先本地；SCM/Job先本地隔离VM再CI回归；Linux ARM64原生优先，QEMU只补缺失的页配置。
- 所有拟议命令和接口在实现任务完成前均属待实现，不能把本文当成已完成报告。

### 构建输入与身份

- 源码记录repo/ref/commit、获取时间、内容身份、原始许可和有序补丁清单。
- 工具链记录官方URL、原归档SHA-256/长度/获取时间、工具版本、编译器摘要与可证明的libc身份。无法证明的源码关联显式保留未知。
- 首次fetch和后续bump是显式动作；默认构建只读取已验证缓存，绝不隐式更新移动HEAD或Nightly。
- 候选输入、已校验输入和已通过探针的构建分别记录，不以下载成功表示可行性通过。
- Build Evidence记录实际命令、flags、资源顺序、slices/header信息、大小、源码/工具链/补丁身份及每个artifact摘要。
- 新增系统探针和服务探针都有各自artifact身份；同一artifact的跨宿主证据必须匹配其自身摘要，不能要求所有不同探针具有同一个摘要。

### 验收入口与报告合同

主要测试seam是外部命令、实际操作系统行为和结果报告；用户已确认make doctor、make test-foundation、portable native probes及结构化evidence。内部函数布局由实施任务选择，不通过镜像实现细节的测试定义合同。

- doctor报告主机、工具和前置条件；缺少WSL2/依赖时准确给出未就绪。
- foundation runner以真实产物和可断言用例执行，本地结果与全矩阵完成判定分离。缺少必要输入/用例不能静默成功。
- Core聚合门禁验证全矩阵、64K、I/O、离线重构和链接边界；M0-SVC采用独立结果集合。
- 结果至少包含schema/case身份、执行时间、source/toolchain/build/artifact身份、OS/CPU/实际执行架构/kernel/page size、execution_mode、命令、原始日志定位、期望/实际值、退出码、时限、结果及原因。CI补充runner标签/image版本、workflow run/commit；QEMU补充host/guest架构、QEMU版本/TCG、machine/CPU及kernel/config/rootfs摘要。
- 通过必须有实际执行；缺设备/权限/依赖是blocked或not_run，用例失败是fail。必需项的skip/not_run/blocked都不能让Core完整门禁通过。
- I/O矩阵中受实测证明的OS限制可以作为探针发现记录，不能把未经执行的限制说明当成通过；若限制使Core合同不可行，形成Core blocker。
- 版本/摘要、缺失/冲突报告和链接边界都由可执行检查消费，Markdown只解释状态。

### 宿主与资源

Core基线保持Linux x64、Windows x64、macOS x64、macOS ARM64、Linux ARM64 4K、Linux ARM64 64K六类目标。覆盖按OS/架构/实际内核页大小/execution_mode识别；同样4K页的WSL x64与Linux ARM64是不同组合。

**本地优先：** 日常编码、探索、红绿测试和失败复现先在本机Windows、WSL2 Linux及本地QEMU完成。WSL就绪后先测架构/页大小；若为x64 4K，本地快速profile补一个ARM64 64K完整guest。该profile覆盖本地常用路径和64K风险，不宣称ARM64 4K已在本地验证；涉及ARM64 4K专属变化时可选择完整本地profile再补4K guest。本地快速成功不能关闭完整M0门禁。

**CI持续集成：** GitHub提供原生ARM64 Linux runner，优先用明确标签的原生runner验证其实际页配置。job开始先实测架构、kernel、sysconf/getconf页大小，并记录runner image/run身份。若ARM64原生为4K，仅补64K QEMU；若为64K，仅补4K。若实际为16K等其他配置，报告两项目标缺口，不将其计入4K/64K，不默认假设单个guest必然补齐。Linux x64 runner的页配置也要记录，但不能替代ARM64组合。

**QEMU补缺：** 仅对未覆盖的目标启动qemu-system-aarch64完整guest，用TCG和固定QEMU/machine/CPU/kernel/config/rootfs；guest内测量页大小后运行APE。qemu-user、MSYS查询、模拟getconf输出不能替代内核用例。报告区分native/emulated-system；原生ARM64结果可以记录native覆盖，QEMU结果不冒称物理硬件或性能验证。

**共享执行路径：** 本地脚本负责构建、探针、结果采集和矩阵判断；workflow只调度这些脚本并发布证据，不另写一套仅能在CI调试的实现。探索先本地复现，提交后CI做集成回归；锁、配方和报告合同一致，CI分发同一摘要制品。

**服务应用：** SCM/Job的真实探索优先本地可丢弃Windows VM，普通Windows宿主不静默安装测试服务。CI直接使用托管Windows临时VM验证基础API，无需嵌套Windows VM。权限、既有Job约束、超时、清理及OS状态都要实测；重启/自启动、交互桌面或特殊策略用对应可控VM。

**实施更新：** 本机 Ubuntu-22.04 的 WSL2 Linux x64 环境已验收，页大小实测 4096；此结果不替代 ARM64 页配置或产品启动验证。ARM64 产品覆盖仍须由对应任务采集真实页大小及制品身份。资源未就绪时相关验收保持待验，六类目标不缩减。

### 依赖与推进

正式任务图以[任务索引](README.md)和各任务Blocked by为准。输入锁可以并行；制品构建后，跨宿主启动、64K、I/O、离线复现和服务探针可按资源并行。

M0 Core完成必须满足其全部门槛；M0-SVC结果不进入Core阻断集合。沿用总体计划CORE-01仅依赖BOOT-04的细粒度前置：对应构建与跨宿主启动成立后可规划/开始M1工作，与其余探针并行；这不等于提前关闭M0，也不在本轮生成M1实现任务。

## Testing Decisions

- 优先测试命令输出/退出状态、原生API可观察结果、artifact身份和资源清理；不锁定内部数据结构。
- 工程诊断从现有Windows工具与真实WSL状态出发；当前没有项目测试可复用，建立最小报告契约而非庞大框架。
- QuickJS测试覆盖确定性表达式和异常退出；完整ESM/Promise/生命周期压力留在M1。
- fat APE检验slices/header和实际跨宿主运行，两类证据相互补充。
- 64K测试覆盖loader、pthread与线程局部存储、分配/mmap、ZIP资源；大型资源产品指标留在M7。
- I/O测试覆盖socket、pipe/EOF、TTY/Console、基本spawn/wait/Unicode路径、超时和清理；M2据此设计Host。
- 离线实验验证固定输入重构，并对缓存/源码/补丁/摘要篡改执行拒绝测试。优先字节一致，任何差异必须定位并可核验；未解释差异阻断。
- SCM/Job基础用例先在本地隔离VM探索，再在托管Windows临时VM持续回归。SCM状态以OS查询为真值，回调不调用/等待JS；Job测试不外推任意子树安全保证。
- 验证聚合器对空结果、缺项、错误artifact、过期报告、冲突报告、Core失败和SVC失败的判定；不能通过过滤失败恢复绿色。

### Completion criteria

- [x] 基础工程、MIT、真实WSL2构建环境及环境报告就绪（01、02）。
- [x] 实际QuickJS源码与Nightly输入已锁定，provenance/补丁/许可可核验，无伪造占位值。
- [ ] Hello和最小eval fat APE可构建，slice/header、构建命令和artifact身份齐备。
- [ ] 六类Core目标有实际启动/eval证据；Linux ARM64优先使用原生页覆盖，QEMU仅补缺失组合，native/emulated-system明确，摘要一致。
- [ ] Linux ARM64 64K深度探针（原生满足时直接测，不足时QEMU）及Windows/Unix I/O基线齐备。
- [ ] 离线重构和输入损坏拒绝路径有实际结果。
- [ ] Core结果可追溯，默认Core无服务专有导入，缺项/失败不能通过。
- [ ] M0-SVC独立记录成功/失败/未执行及修复方向；Windows服务未验证时不声明支持。该项不改变Core完成判定。

## Out of Scope

正式qaplet run、Runtime/Context factory、ESM/TLA/Promise/Jobs、稳定C Embed SDK、Loop/Reactor/WorkPool、Node/Web API、QJB格式/解析/编译器、1GB以上资源产品能力、TLS实现或后端准入完成、完整qaplet-svc JS业务和watchdog、生产服务安装、正式发行及未来阶段的详细spec/tickets。

## Further Notes

- 规划完成后的实施状态：01、02 已完成基础工程、MIT 和 WSL2 环境验收；产品构建工具链和 probe 仍待后续任务，详细结果以任务记录为准。
- 参考SHA、历史TLS版本说明和计划中的示例命令不是当前验证事实；实施时获取并记录真实输入。
- 服务ABI发现失败可交付最小复现和独立修复建议，能力保持failed/unavailable；本地隔离VM/托管runner或所需权限不可用则相关任务尚未执行，不能假装已完成可行性调查。
- 后续阶段只在[索引](README.md)保留里程碑；总体计划继续作为背景，不在本轮重写。
