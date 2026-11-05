# 正式 M2 实施记录

**Execution state:** in_progress
**Integration branch:** `codex/m2-integration`
**Product baseline:** `fdd5e82`；已完成 M1 产品及工具。
**Approved-input checkpoint:** `5685bb6`；保存用户提供的规格、27 票及入口文档修改。

用户于 2026-11-03 调用 `/implement-spec`，随后要求中文工作。沿用已确认的公开测试边界及依赖图，不重新访谈。

## 执行约定

- 每票使用独立分支与 worktree，完成后先合并集成分支，再由独立合并代理集成。
- 构建仅在 Ubuntu；本机通过已安装的 `C:/Program Files/WSL/wsl.exe`、`Ubuntu-22.04` 进入。受限进程报告发行版不存在，提升执行上下文后实际可用；不据前者推断系统未安装。
- 共享可复用缓存为主工作区 `.cache/`。报告及制品使用各票独立输出路径，不以计划、旧 M1 或探针结果替代当前产品验收。
- 逐票保留红绿测试、当前源码/配方/制品及宿主身份、原始日志、失败历史和真实缺口。正式必需证据不足时不关闭该票；当前本地开发依赖按用户[本地开发指示](local-development.md)放行，正式欠项保留。
- 所有票集成后才执行一次整体 Standards/Spec 独立审查；先生成并核验分轴材料。

## 当前分派

| 票号 | 分支 | worktree | 实施状态 |
|---|---|---|---|
| 01 | `codex/m2-01-runtime` | `.cache/worktrees/m2-01` | completed（本票范围独立原件重放通过） |
| 02 | 旧分支及 `codex/m2-02-win-error-fix` | 原件见 `tickets/02` / `02-macos-fix` / `02-win-error-fix`；源树已清理，分支保留 | completed（`453fe76` 五原生95+实际64K19，Windows回归闭合） |
| 03 | `codex/m2-03-pipe` | `.cache/worktrees/m2-03` | completed（五原生及实际 64K 原件重放通过） |
| 04 | `codex/m2-04-process` / `codex/m2-04-windows-fix` | 原件见 `tickets/04` / `04-windows-fix` | completed（`0e4ba21` 五原生及实际 64K 共 83 项严格重放通过） |
| 05 | 旧分支及 `codex/m2-05-resize-fix` | 原件见 `tickets/05` / `05-cross-fix` / `05-resize-fix` | completed（`46e5cf4` 五原生97+实际64K19，resize回归闭合） |
| 06 | `codex/m2-06-wait` 及两条定向修复分支 | 原件见 `tickets/06`、`06-creation-fix`、`06-guest-fix` | completed（`bd14a1e` 五原生 560 + 实际 64K 112 严格验收通过） |
| 07 | `codex/m2-07-timers` | 原件见 `tickets/07` | completed（`46e5cf4` 五原生645+实际64K129本票验收通过） |
| 08 | `codex/m2-08-scheduling` | 原件见 `tickets/08` | completed（`e742c76` 五原生765+实际64K153/package39/fault2严格通过） |
| 09 | `codex/m2-09-host-notify` | 原件见 `tickets/09` | completed（`563ff33` 五原生 575 + 实际 64K 115 本票严格验收通过） |
| 10 | `codex/m2-10-work` / `codex/m2-10-file-fix` | 原件见 `tickets/10` / `10-file-fix` | completed（`f2b0e0c` SDK五原生710+实际64K142，包外21/fault2） |
| 11 | `codex/m2-11-cancel` | `.cache/worktrees/m2-11` | local_complete / cross_deferred（已合取消/结果release与物理保留；正式跨平台欠项见后续记录） |
| 12 | `codex/m2-12-js-work` | 原件见 `12-js-work-validation.md` | local_complete / cross_deferred（独立合并`3d03f855`） |
| 13 | `codex/m2-13-process` / `codex/m2-13-cross-fix` | 冻结 `.cache/worktrees/m2-13`；`.cache/worktrees/m2-13-cross-fix` | local_complete / cross_deferred（后续修复已合；历史Full失败与当前本地证据分别保留） |
| 14 | `codex/m2-14-process-cancel` | 原件见后续记录 | local_complete / cross_deferred（独立合并`25d3eb12`） |
| 15 | `codex/m2-15-process-io` | `.cache/worktrees/m2-15-process-io` | local_complete / cross_deferred（独立合并`65de84e`；Linux186、Windows185+optional1、两端package72、fault16/17；claimed保留） |
| 16 | `codex/m2-16-js-process` | `.cache/worktrees/m2-16-js-process` | local_complete / cross_deferred（独立合并`015cdb9f`、接收`71a5db50`；claimed保留，见16验收记录） |
| 17 | `codex/m2-17-standard-stdio` | `.cache/worktrees/m2-17-standard-stdio` | local_complete / cross_deferred（独立合并`caa439ae`；Linux195、Windows194+optional1、两端package81、fault23/24；claimed保留） |
| 18 | `codex/m2-18-tty` | `.cache/worktrees/m2-18-tty` | local_complete / cross_deferred（独立合并`3e85a272`、接收`64624ddd`） |
| 19 | `codex/m2-19-aggregate` | 见19验收记录 | local_complete / cross_deferred（独立合并`597020eb`、接收`2b3035f7`） |
| 20 | `codex/m2-20-mixed` | 见20验收记录 | local_complete / cross_deferred（独立合并`943c1962`、接收`776e50cb`） |
| 21 | `codex/m2-21-cli` | `.cache/worktrees/m2-21-cli` | local_complete / cross_deferred（独立合并`13abf9ac`、接收`fe8a17d`） |
| 22 | `codex/m2-22-cli-signal` | `.cache/worktrees/m2-22-cli-signal` | local_complete / cross_deferred（独立合并`b8b9f572`、接收`6cca6f9b`） |
| 23 | `codex/m2-23-sdk-delivery` | `.cache/worktrees/m2-23-sdk-delivery` | implementing_local（19/22开发前置已独立接收；最终供件与包外组合验收收口中） |
| 24–27 | 未领取 | 见[当前前置与验证范围](local-development.md#2026-11-05剩余开发前置与验证范围) | 24–26等待23本地完成且独立合入，27等待24–26实际正式全范围；原合同保留 |

2026-11-05前置校正提交 `2d538a1`：逐一比对27票，原Blocked by和Acceptance criteria
全部保持原文；父规格原字节未变，当前SHA-256为
`f203700a2b1ad82a67f3e0fc3c0ecbae96ff8db470ae85f1c019b131a51c400b`。
祖先/合同核验在 `out/qaplet-m2/alignment-16-27.json`，启动预检在
`out/qaplet-m2/preflight-16-17-start.json`（pass）。这些只证明前置与材料正确，
不计16/17产品测试。两票独立实现者已分别在上表worktree启动。

## 环境事实

`out/qaplet-m2/context.json` 记录实际上下文：Ubuntu、Linux x86_64、WSL2 kernel `6.6.87.2-microsoft-standard-WSL2`、4096 页、uid/gid 1000、构建允许。该报告只证明执行环境，不证明产品合同通过。

`out/qaplet-m2/guest-inputs.json`：现有固定 64K guest 输入经离线签名、长度及摘要核验通过，`qemu-system-aarch64` 已安装；尚不表示新探针已经执行。

04/05 候选修复的完整本地检查曾因 D 盘可用空间低于既有 4 GiB 门槛失败，保留原失败，没有降低阈值。确认无活动构建后，对静态派生 `.cache/build/sdk` 执行标准 NTFS `compact /C /S /I /Q`（不使用 `/EXE`），4,783 个文件压缩前后长度与 SHA-256 全部相同，目录无 reparse 项，缓存位置与原件均保留。`out/qaplet-m2/cache-compression-before.json`、`cache-compression-compact.log` 与 `cache-compression-verified.json` 记录可用空间从 4,011,765,760 增至 4,862,255,104 字节；该收据只证明环境恢复，04/05 完整检查仍需重跑。

随后以相同标准 NTFS 方式压缩其余 220 个静态派生 build 子目录，排除正在构建的 `m2-06`：另 3,643 文件数量、长度及 SHA-256 全部相同，无 reparse 项；`static-cache-compression-verified.json` 记录 D 可用空间增至 5,567,799,296 字节。06 自行在静态阶段压缩自己的 build 目录并核验 87 个文件不变，再继续构建。以上仅改压缩属性，不移动缓存、删除输入或改验收门槛。

## 公共 CI 接入

02/03 的独立 producer、Linux Fast、五原生供应消费及 native-first 64K guest 路径已接入既有 Fast/Cross/Full。各探针保留独立配方、制品和日志，不改变旧 M0 原生 I/O 配方。实现尚在独立分支中时不推送执行此编排；待源码集成后取得实际成绩。

已执行公开 `build_ape.py check-ci` 边界的红绿测试：删除任一 probe 的 build/native/guest 路径或在 consumer 重编译共八种损坏，新增检查均会拒绝。随后 Ubuntu `scripts/dev.py check` 通过，报告 `out/qaplet-m2/check-ci-wiring.json`，334 个工具合同测试中 165 个需要显式供应输入的用例未执行；此处不将其记为产品/平台验收。bootstrap 和输入锁预检通过。

为在正式集成点取得规格要求的即时 Cross/Full，`codex/m2-integration` 的实际 push 直接选择 Full；其他普通分支的 Fast 及可信缓存规则不变。该选择通过 CLI 红绿合同验证，保留实际 push 事件身份。GitHub 连接器没有工作流 dispatch 操作，本机 Chrome 的工作流入口与一次重载均为 `ERR_CONNECTION_CLOSED`，不通过新 PR 或 RC tag 触发测试。

分支选择增量的 Ubuntu 完整本地检查通过，报告 `out/qaplet-m2/check-integration-tier.json`（同样 334 项、165 项需显式输入而未执行）。这两个本地检查都没有代替新 SDK、探针或远程 Full 的实际执行。

01 的独立 Ubuntu `sdk.py fault-consume` 已接入必需 Fast 步骤，使用独立 `fault-consume` scope、受控夹具标记及制品，不混入正常 Host catalog。删除该步骤的公开 CI 合同先红后绿。Ubuntu 仓库检查通过：`out/qaplet-m2/check-creation-fixture-ci.json`，341 项、169 项需显式输入而未执行；没有宣称故障夹具具备尚未实现的跨宿主消费入口。

## 当前已保存的逐票证据

02 的同一 APE 在 Linux x64、Windows x64、实际 ARM64 64K guest 各 19/19 通过，两原生目标的 CLI 合同各 6/6 通过。macOS 两目标和 Linux ARM64 native 尚待远程 Cross；票保持 `awaiting_cross`。[逐项原件与后端结论](waker-reactor-validation.md)保留 Windows failed-connect 的 `poll` / `select` 差异。

02 输出已停止写入。主工作区 `out/qaplet-m2/tickets/02/` 已保存完整去重归档；`store-02-plan.json`、`store-02-created.json`、`store-02-verified.json` 记录 308 文件、53,224,414 逻辑字节及完整原件核验。源 worktree 仍保留，保存收据不单独授权删除。

01 的 SDK 空实例及兼容生命周期、03 的 pipe/stdio 真实探针已分别通过独立机械合并进入集成分支。01 本地正常 SDK 目录 Ubuntu 105/105、Windows 104 pass + 1 既有可选 symlink blocked；包外消费者和 Ubuntu 独立故障夹具通过。03 在 Linux x64、Windows x64、实际 ARM64 64K guest 各 19/19，Linux/Windows 的 CLI 合同各 4/4，通过后的限制及首次失败仍见[01](01-runtime-validation.md)、[03](03-stdio-validation.md)。三票尚未放行完整 Cross 或依赖。

01 初次保存核验 1,213 文件、202,193,960 逻辑字节，03 保存核验 359 文件、57,679,949 逻辑字节，收据在主 `out/qaplet-m2/store-01-*.json` 与 `store-03-*.json`。01 合并后另增加一份 1,251 字节的窄 CI 收据，因此保留初始归档，再将含该收据的最终输出树保存至 `tickets/01-after-merge/`；清理前以最终保存及遗漏输出审计为准。

01 合并后保存及原件核验已通过（1,214 文件、202,195,211 逻辑字节）。三票首次集成的 Ubuntu `scripts/dev.py check` 通过：`out/qaplet-m2/check-initial-integration.json`，345 项工具合同、171 项因需显式输入而未执行，bootstrap 通过；本地检查不替代平台矩阵。

首次实际 Full 为 [41599033541](https://github.com/moorefu/qaplet/actions/runs/41599033541)，实际 push 提交 `5fb999fda5b671bbf559ea66db2d91741355123b`，最终 `completed/failure`。原始 API、任务日志及 ZIP 收集/离线核验收据集中于 `out/qaplet-m2/initial-cross/`；正常 SDK、包外消费、独立故障夹具及 02/03 Linux Fast 步骤已成功，具体合同及跨宿主完成度仍须原件重核。

04/05 已给出独立 `m2_process.py` / `m2_tty.py` 的供应与 guest 接口，公共 CI 已准备同样的 build/Fast/五原生/64K 路径；删除任一 probe 的必需路径或在 consumer 重编译共 16 种损坏，通过公开 CI CLI 的红绿合同验证。新入口待实际实现集成后才推送执行，不将编排就绪当作 OS 通过。

四探针公共编排的 Ubuntu 仓库检查已通过：`out/qaplet-m2/check-all-probe-ci.json`，345 项、171 项需显式输入而未执行；本次仅准备编排，当前远程 Full 仍严格绑定先前两探针版本 `5fb999f`。

首次 Full 在两台 macOS 的 `socket.connect-error` 失败，其余五原生 SDK、stdio 及实际 64K/旧 M0 Full 步骤均已有成功原件；仍须严格消费，不以步骤成功替代验收。失败原件及可复验重放位于 `out/qaplet-m2/initial-cross/HANDOFF.md`。已分派独立 `codex/m2-02-macos-fix`；04/05 继续实施。为避免未完成的独立票成为 02 修复的虚构前置，暂将公共 CI 恢复为字节匹配 `5fb999f` 的两探针版本；四探针准备提交 `c338ca9` 保留，待 04/05 源码就绪再恢复执行。

暂退预备入口后的 Ubuntu 仓库检查通过：`out/qaplet-m2/check-two-probe-replay.json`（345 项、171 项需显式输入未执行）。旧失败保持失败；当前未关闭 01–03 或放行依赖。

后续独立只读重放已完成：`out/qaplet-m2/initial-cross/verify/VERIFICATION.md` 与 `summary.json` 记录五原生 SDK、实际 64K 105 项、包外 8 项、受控故障夹具 1 项/4 阶段、五原生 stdio 各 19 项及实际 64K 19 项，以及当前 M0 Full 六目标 235 项。01、03 自身范围无独立阻塞，按逐票范围完成并勾选其验收；02 的两台 Mac 失败和整个 run 的 failure 均保留，父 M2 仍未完成。Windows SDK 本次实际 105 项全通过，其既有 required 目录为 104（可选 symlink 本次也通过）。

Mac 候选修复 `9a390f9`、05 `0460f71`、04 `a64afa6` 已分别由独立 merger 集成。候选关闭预留端口后再测真实非阻塞连接错误，consumer/判据/timeout 不变；旧 Mac trace 仍红，Linux/Windows/64K 各 19 项和 100 次 CLI 通过，首轮本地检查一项未归因失败及后续完整检查成功都保留。04/05 本地原件仍各自绑定源树原始配方；04 部分源文件在主树按既有属性检出为 LF，并清理一行尾随空白，所以必须由接下来的当前配方 Full 重新取得成绩。

04、05 代码均已就绪，已恢复 `c338ca9` 的四探针编排，准备对当前集成重新执行 Full。05 全部输出已保存/核验 820 文件、143,245,848 逻辑字节；Mac 候选输出保存/核验 436 文件、23,857,384 逻辑字节，收据分别为主 `store-05-*.json`、`store-02-macos-fix-*.json`。

五票当前集成的 Ubuntu 仓库检查通过：`out/qaplet-m2/check-five-ticket-integration.json`，361 项总数中 178 项实际执行通过、183 项因需外部显式输入而跳过，C bootstrap 通过；`git diff --check` 无问题。下一次 Full 将使用主树当前 LF 源码及清理后的配方，不继承 04/05 本地旧源字节的签发身份。

实际 [Full 41607608424](https://github.com/moorefu/qaplet/actions/runs/41607608424) 绑定 `196cd75`，整体 `completed/failure`，失败属于 04 Windows fixture setup 与 05 Mac/Windows TTY。02 自身五原生各 19 项及实际 64K 19 项（共 114）、真实 native loader/provider 原件、源/配方/CI/同 APE、guest serial/4 suite/8 payload/清理、当前 M0 Full 六目标 235 项均已严格重算，见 `out/qaplet-m2/full-196cd75/verify02/VERIFICATION.md` 与 `summary.json`。02 按本票范围完成，01/02 的直接依赖 06 已放行；04/05 独立失败继续由 fresh agent 修复，父 M2 不完成。

01/02/03 旧 worktree 已在干净、全部提交合入、绝对路径与非 reparse 检查后清理。全部 out 先再次对照已保存归档核验（`store-01/02/03-pre-cleanup.json`）；额外忽略文件分别只含 64/24/67 个可再生 Python 字节码，无其它遗漏，分支保留。原路径可由 `evidence_store.py restore/materialize` 恢复到相应备份的 `out`，不改变旧报告内容。

04 Windows 修复及 05 Mac/Windows 修复分别由独立 merger 无冲突集成为 `87fe34f`、`73e84df`；逐票 11 项当前 LF 源码/配方与供应原件逐字节相同，窄公开 CI 合同及 diff 检查通过。恢复空间后的两票完整检查均通过且保留旧失败；主树再次执行 `scripts/dev.py check` 通过，报告 `out/qaplet-m2/check-probe-fixes-integration.json`：364 项中 178 项实际执行、186 项需显式外部输入而跳过，真实系统 C bootstrap 通过。尚未取得新 Mac/Windows CI/ARM native，本地成绩不关闭 04/05 或父规格。

两修复树输出分别完整保存并核验为 `tickets/04-windows-fix/`（349 文件、56,903,491 逻辑字节）与 `tickets/05-cross-fix/`（242 文件、41,090,449 逻辑字节），收据为主 `store-04-windows-fix-*`、`store-05-cross-fix-*`。旧 04/05 out 在清理前重新对照原归档通过（`store-04/05-pre-cleanup.json`），干净、已合入、绝对路径及非 reparse 根检查均通过，out 外各仅 69 个 Python 字节码。仅清理本会话原始旧 worktree `m2-04`、`m2-05`，保留分支和所有归档；新修复树仍保留。

当前 [Full 41616881692](https://github.com/moorefu/qaplet/actions/runs/41616881692) 精确绑定 `0e4ba214cdb7bd6e103cfade9fba748cbd8f85fa`，整体 completed/success。29 个 provider 原 ZIP 已保存并通过 fetch/offline verify；独立读取当前 consumer 重算 04 五原生 70 + 实际 64K 13 项、05 五原生 97 + 实际 64K 19 项全部通过，同源码/配方/CI/APE、Windows PE child、实际 native loader、ConPTY 动作、guest serial/4 suite/8 payload/清理均核验。root 另在 `out/qaplet-m2/full-0e4ba21/04-05-acceptance-check.json` 重核数量、逐项状态及 native/guest 同 APE；本次重放不计新执行。04/05 自身范围 resolved，不外推后续产品资源、最终 T03 或父 M2 完成。v1 adapter 已通过 consumer 抛错拒绝非零退出/timeout；v2 仅增加显式重复检查，不把 v1 记录为失效。

已完成的 02 Mac 修复旧 worktree 也在重新源对档核验 436 文件、23,857,384 逻辑字节、干净/已合入/绝对路径/非 reparse 根审计后清理；out 外仅 67 个 Python 字节码，分支保留，原件仍在 `tickets/02-macos-fix/`。收据为 `store-02-macos-fix-pre-cleanup.json` 与 `old-02-fix-worktree-{audit,cleanup}.json`。

04/05 的修复 worktree 在本票 resolved 后再次对照归档通过（349/242 文件），干净、已合入及绝对路径/非 reparse 根审计通过，out 外各仅 71 个 Python 字节码，已清理并保留分支。收据在 `store-04-windows-fix-pre-cleanup.json`、`store-05-cross-fix-pre-cleanup.json`、`completed-fix-worktree-{audit,cleanup}.json`。当前 0e4ba21 的独立 M0 Core 也重放 235/235 通过；可选 02/03 通用适配器的接口差异失败保留，不扩大其已完成结论。

06 产品提交 `921f6f6` 经独立 merger 合入为 `1708edec3aca9d7593cc530e6e4e31256c5af3fe`；全部 63 项 RECIPE 原字节与源 tip、实际 producer `2a9ab07`、merge Git blob 一致、CRLF 为零，71 个供应文件及 ZIP 核验通过。最终本地 Linux 111 pass，Windows 110 pass + 1 optional symlink blocked（必需缺口 0），包外应用 8、独立故障夹具 1 通过。两平台各实际 500 次 token 与 100 次 concurrent 场景通过，绑定最终 Host 字节；仅说明本票资源重复范围，不算最终 T03 全混合。producer 的 `working_tree_dirty=true` 原样保留：Ubuntu Git 对既有 CRLF 非 RECIPE 文档的观测与 Windows Git clean 不同，不能改写报告或将其说成 Ubuntu 全树 clean。

06 的真实 close 故障先暴露错误成功诊断，再以明确拒绝、实际关闭后同号复用两种受控情形验证：FD 只尝试关闭一次，不确定句柄独立计量并保持 CLEANING/destroy BUSY，quiesce 不重关复用 fd，不把这类故障称为资源清零成功。主树当前 `scripts/dev.py check` 通过（`out/qaplet-m2/check-wait-integration.json`，364 项中 178 实际执行、186 条件 skipped，真实 C bootstrap 通过）；已推送触发 [Full 41621764245](https://github.com/moorefu/qaplet/actions/runs/41621764245)。五原生及实际 64K 当前原件与独立重放仍待齐，06 不 resolved，07/09/10/13 不提前放行，父 M2 保持 in_progress。

06 原 worktree 的 8,062 个冻结输出（649,029,428 逻辑字节）已经保存到 `tickets/06/`，`store-06-created.json`、`store-06-verified.json` 完成源成员/内容/元数据、归档 CRC 和摘要核验；源树保留，归档不单独授权清理。

Full `41621764245` 最终 completed/failure，唯一失败 job 为实际 64K `arm64-page-gaps`。独立严格重算五原生各 111（含六个新增等待合同）、包外应用 8、独立故障夹具 1 和当前 M0 Full 235 均通过，Windows 本次 optional symlink 也实际通过。29 provider 原 ZIP、12 嵌套供应物、11 job 原日志及离线核验齐全，见 `out/qaplet-m2/full-1708ede/HANDOFF.md` 与 `verify/scope-summary.json`。SDK guest serial 原文恰缺 `qaplet.run-unsettled` 的 START：110 项六字段完整，111 项退出/输出均符合 catalog，但该项立即退出时长无法验证；原 parser 在此前 86 pass 后拒绝，不能补造字段或称全部 111 guest 通过。原 init 有无条件 START 发射，serial 未见相邻交错；QEMU 正常退出、DONE=1、26 payload 前后 hash 同且清理完整，不能由此推断产品挂起、ABI 错误或具体 errno。fresh `codex/m2-06-guest-fix` / `.cache/worktrees/m2-06-guest-fix` 负责区分发射、console 传输及 host 采集并修复。

另一个相邻定向系统夹具实际两次复现未发布创建的诊断缺口：accept 失败且 close 成功时活 socket 为 0；listener close 明确拒绝、或 accept EMFILE 后 close EACCES 时各仍有 1 个活 socket，返回 CONTROL_FAILURE/outNULL，但内部不确定清理/原始 setup 与 cleanup 错误随 Runtime 释放而丢失。夹具最后自行清理，不把它算 SDK 回滚完成，原件在主 `out/qaplet-m2/06-init-close-diagnosis/report.json`。fresh `codex/m2-06-creation-fix` / `.cache/worktrees/m2-06-creation-fix` 处理公开诊断和安全责任边界；不重关数值 fd、不伪造清零、不发布成功半成品。06 继续 awaiting_cross，后续 blocker 不放行。

创建诊断修复产品 `90b73d8`、供应源码 `3019c3f` 经独立 merger 合入 `b97b09c`：新增独立版本化 M2 诊断 POD、扩展创建和线程安全查询，主因/control 与 cleanup 原始 domain/value 分别保存；未发布状态无内部执行者，可释放已证明无后续访问的 C/引擎存储，未知 OS 资源只报告 INCOMPLETE 及数量，不代表实际释放或调用方可按 fd 重关。63 RECIPE 原字节/Git blob/LF、71 供应文件/ZIP、Linux 112、Windows 111 必需 pass + 1 optional blocked、包外 8、fault 1 及每宿主 2×4 系统 tracer 原件已由 merger 重核；本地 producer dirty=true 原样保留。受控原始 pthread 返回值、CLOSED 清理时钟、短结构/缓冲及跨线程诊断均保留红绿，见[创建修复记录](06-creation-fix-validation.md)。全部 1,408 输出、252,554,190 逻辑字节已保存/独立核验于 `tickets/06-creation-fix/`，收据 `store-06-creation-fix-*`。

guest 修复经独立 merger 合入 `bd14a1eb319ceba93ffe362f8b0a1d6df6438b7d`：记录在 guest RAM journal 逐条计算一次，payload 全部结束后才打开独立 virtio port 传完整 journal 与长度/SHA seal，console 原件继续保留；不更改 payload 标准流或泄漏证据 fd，不从残缺 console 推造时间。旧丢失层级仍 unproven，1708 失败不变；新的同供应本地实际 64K 111/111、26 payload 前后 hash、725 条双路记录及 raw 身份核验通过，仅属旧产品字节上的新 guest 协议范围。合入创建修复后的现成 ci=null/dirty=true 112 SDK 公开导出通过，不冒充匹配 CI/112 guest。额外 Core 负向套件的 13 个失败 subtest 经旧基线复现，修复测试的 native 目标选择、每变体新报告和完整路径映射后 20 项实际 pass、1 service pointer 未选择 skip；SDK 导出旧 105 计数红灯以独立 literal 111 ID 下限和公开输出一致性修复，允许 112 增量。旧 CI 在新配方下只记 stale-recipe 拒绝，先前同配方 CI-null 身份拒绝另有原件。见[guest 修复记录](06-guest-transport-validation.md)。604 输出、11,021,967 逻辑字节已保存/独立核验于 `tickets/06-guest-fix/`，收据 `store-06-guest-fix-*`。

两修复合并后的主树完整检查 `out/qaplet-m2/check-wait-repairs-integration.json` 通过：368 项中 179 实际执行、189 条件 skipped，真实 C bootstrap 通过。已推送 `bd14a1e` 触发 [Full 41632318680](https://github.com/moorefu/qaplet/actions/runs/41632318680)，当前 112 项五原生/实际 64K、独立故障及新 journal 严格闭包仍待原件，06 不 resolved，后续 frontier 继续等待。

该 Full 最终 completed/success，11 jobs 全成功，独立严格重算五原生 560、实际 64K 112、包外应用 8、独立 fault 1、当前 M0 六目标 235 均通过，必需缺口 0；Windows 本次 optional symlink 也实际 pass。29 provider ZIP、12 嵌套供应物、11 原日志、63 RECIPE 原字节与 SDK 双 archive 各 12 个 ELF64 REL 成员（机器 62/183）及密封 ZIP 同字节均核验。新 SDK journal 731 条（672 CASE 字段、26 payload 前后 hash）、M0 journal 145 条（21 完整命令、6 payload）的长度/SHA seal、实际 argv/身份/清理全部通过，console 镜像只作诊断。同配方真实 CI 供应被本地 CI-null export 正确拒绝，非旧配方更早拒绝。主 `out/qaplet-m2/full-bd14a1e/HANDOFF.md`、`verify/scope-summary.json` 及 root `06-acceptance-check-002.json` 保存全选择与结论；adapter 初始重复 mapping 拒绝和 root 初次把标量 case_count 当数组的检查器失败均保留，修正后通过，不是产品失败。

06 按自身范围 resolved，07、09、10 已领取；13 的直接 blocker 也满足，待槽位。并行实现能力位分别为 timer bit 19、HostNotify bit 20、C work bit 21，接口具体形状依父规格由公开头文件落实，新的全局错误码若需要由 root 协调避免碰撞。每票首个资源即保持预算及 close/quiesce 安全，不延后 19/20；全部票完成后才做一次整体独立审查。当前旧 M1 1,000 循环及 06 本地重复范围不冒充最终 T03 全资源混合，父 M2 仍 in_progress。

## 一次性测试入口修复

`e742c76` / Full `41812117094` completed/success，11 jobs成功。08 当前自身scope五原生各153/总765、实际64K153、包外39、core+work fault2、archive各14及当前M0 Full235严格通过，必需缺口0。70 SDK+9 eval raw对精确CI提交、29原ZIP/12nested/11job日志、两guest完整journal/seal/清理、77 repo Python+15 helper前后不变和3089消费文件最终rehash均核验。root另核9份摘要及精确计数，见 `out/qaplet-m2/full-e742c76/HANDOFF.md`、`08-acceptance-check.json`、`root-scope-acceptance.json`。GC实际65,536,000字节/62.5MiB；先前64MiB是注释约称，当前如实修正叙述，不改已验供应字节。08按本票范围完成；12仍等待11，不外推最终T03/父M2或发布。

02 Windows连接错误分路修复 `d547150` 已独立合入 `620b996`，19项三实际本地目标和公开CLI回归等原件由merger核验，见 `out/qaplet-m2/merge-02-win-error/HANDOFF.md`。13产品 `6583ed3` / 源tip `1b0886a` 已独立合入 `a5c8b10`：77 RECIPE raw/Gitblob/LF/供应全等、双archive15 ELF64 REL、595条原始实际命令/1422闭包文件、合并树同源/双树clean与两项窄UbuntuCI通过，详 `out/qaplet-m2/merge-13/final-handoff-001.json`。原checker错误及std-route缺执行前hash的范围保留。两票保持awaiting_cross，准备以当前集成同一次新Full验证五原生/实际64K；14未放行，11正在融合实际a5供应169项及直接取消生命周期回归。


`f2b0e0c` / Full `41809892923` completed/success，11 jobs成功。10自身严格重放五native各142/总710、公开matrix cross、实际64K142、包外21、core+work fault2全部pass；Win file正原件exit0/无timeout/精确stdout/无stderr，不反补旧867 errno。70SDK+9eval/锁对精确提交raw/LF全等，29原ZIP/12nested/11job原API+UTF8copy/离线verify、双archive14机器62/183（与旧SHA相同仍独立重审）、三APE/实际loader、当前M0六目标235与两guest seal/完整六字段/payload前后/清理均核。77repo Python+17helper前后不变，2901消费文件最终rehash；见 `out/qaplet-m2/full-f2b0e0c/HANDOFF.md`、`verify/scope-summary.json`、`root-scope-acceptance.json`。按本票范围完成10并放行11，未外推最终T03/父M2/发布。02旧867连接错误回归仍须独立修复，不能因本轮同旧probe通过而取消已复现缺陷；08新Full `41812117094` / `e742c76` 正在验收。11预留新code28/29如实际需要表达CANCELLED/ALREADY_COMPLETED，13的27/Win32域2/bit22保持。

08 已由独立 merger 合入 `6c8c0eaffa058f59131be35cb540206bde1be582`，源tip `28a309c` / 产品 `bc5fc49`。timer bit19和共享1,024槽保持，新增interval原计划跳周期/immediate/FIFO、四类持久轮转与完整jobs检查点、普通错误恢复/累计预算、丢弃句柄后4MiB堆下64MiB循环垃圾GC仍交付的公开红绿均保存。最终 Linux153、Windows152+旧optional1，包外39与fault2各两宿主通过，369项177实际192条件skip及bootstrap通过。70 raw/Gitblob/LF、密封包成员、三APE/双archive14、六scope原日志及执行身份独立核验；原Windows包外/fault只嵌driver自身而缺repo导入闭包，保留此限定，merger仅以新scope补18个timer+1个corefault并嵌实际repo imports前后身份，不重写原39/2。合并后源树同一/双树clean/窄Ubuntu CI通过，首次误用WSL发行版与票状态审计错误均保留；见 `out/qaplet-m2/merge-08/handoff-001.json`。准备推送08新Full，仍不resolved或放行12，13已接续融合实际四类native provider，未虚构13→08阻塞边。

10 文件夹具修复由独立 merger 合入 `166ab50b1f0d4ef6bfb17aa856791047cbdb421c`，源tip `01f7147` / 产品 `63d7327`。仅3个RECIPE文件修改：自建相对临时目录、分阶段文件错误和清理、controlled work fault追加同供应完整文件合同的永久回归；不改SDK实现/API/等待时限。真实OS缺失父目录旧源码两宿主红，仅创建该目录转绿；旧CI复合断言的具体失败阶段/errno仍unknown。Linux142、Windows141+optional1，密封ZIP包外21/fault2均双宿主通过，完整369/177实际192条件skip/bootstrap通过。独立70 raw/Gitblob/LF、ZIP/三APE/双archive14（与旧SDK相同）、六scope原日志/执行driver导入闭包及历史红绿、post源树等同性/双树clean/窄CI均通过，见 `out/qaplet-m2/merge-10-file/handoff-001.json`。追加而非必需的Windows重放已取消，其父子进程确已创建但未完成，是否进入main/阻塞阶段unknown；外部终止仅记incomplete，不计SDK成功清理或原六scope成绩。准备立即推送该修复Full，10仍等待当前原件，11不提前放行。

已完成07/09/05 resize的worktree在完整归档及独立源对档验证后清理：07为13,744文件/1,013,460,833逻辑字节，09为2,308/348,998,746，05 resize为882/96,731,033。`store-07/09/05-resize-verified.json`、`completed-07-09-05-audit.json`、`completed-07-09-05-cleanup.json`及独立branch-retention收据确认干净/全部已合入/绝对路径/无reparse；out外仅71/80/82个可再生Python字节码，分支与所有归档及CI原ZIP均保留，缓存/输入不删除。旧full563/full46 adapter固定的源码Root路径已不存在；再次重放须先以保留分支重建原绝对worktree并restore/materialize归档out，或另存新的位置映射adapter，不改已引用脚本或历史报告。08、02新修复、10及13活动树均保留。

WorkPool `867356c` / Full `41799595408` 最终 completed/failure：Windows native与startup-matrix失败。Windows SDK142项中唯一nonpass为`qaplet-host.m2-work-file`，exit256、无timeout，stderr `work contract line 49: fd>=0 && write(fd,expected,sizeof(expected))==sizeof(expected) && !close(fd)`；代码显示该断言位于首次submit前，原`/tmp/qaplet-work-XXXXXX`创建/写/关哪个阶段及原errno仍未区分，不能猜测故障归因。fresh `codex/m2-10-file-fix` 定向诊断，票10不关闭/不放行11。另有02 Windows `socket.connect-error`：connect -1/errno115，poll0/timeout2000ms/revents0、select -2、socket_error10061、connection_failed=false，reserve已在connect前关闭。02重新claimed定向调查，旧196cd75范围保留，不因当前探针失败改标此前通过的各产品scope。四健康native各142、实际64K142、包外21/fault2、双archive14与M0Full235本轮已独立重放通过；Windows失败不得据此伪造710完整通过。原件/typed失败及29原ZIP、12nested、11job日志保留于 `out/qaplet-m2/full-867356c/`，待独立handoff收尾。

`46e5cf4` / Full `41796608784` 最终 completed/success，11 jobs成功；fresh严格消费 SDK五原生645、实际64K129、包外应用8、controlled fault1（当前六行合同）、双archive各13 ELF64 REL、当前M0六目标235，05正常TTY五原生97+实际64K19全部通过，必需缺口0。29原ZIP、12nested、11job原日志/UTF8copy、冻结05树对精确提交的67SDK+9eval/锁+12TTY去重81文件raw身份、三guest journal/seal/实际argv/清理均核验；SDK journal129项/27payload，M0 21/6，M2四suite/8payload，未用console镜像补字段。WinServer实际正常probe在最终通知后查询100×33，模式恢复与四项资源清理通过，未将受控fixture当正常CI，旧563失败仍失败，原Server唯一调度原因仍未证明。`out/qaplet-m2/full-46e5cf4/HANDOFF.md`、`verify/scope-summary.json`、`05-07-acceptance-check.json` 与 root `root-scope-acceptance.json` 保存全选择。按自身scope重新关闭05并完成07，放行08；当前更新的WorkPool另由867新Full验收，父M2/最终T03/发布未完成。

10 已由独立 merger 合入 `01e72a6f5bc037b3e63118b9f76dc524ea823a25`：源 tip `2d488951`、冻结产品 `9c69877` 与合并后全部70 RECIPE原字节/Git blob/LF一致，76供应文件、三APE与双archive各14 ELF64 REL、密封SDK及元数据累积能力4161535/21名称均核验。Linux142、Windows141+1旧optional symlink blocked，包外普通21与controlled fault2各在Linux和同字节Windows通过；冻结/同步后完整check及bootstrap通过。真实三类持久cursor、ref work与unref timer存活、pure-C不新建JS账本/active entry累计预算、HostNotify lease与物理TLS退出红绿均保存。merger补充两个独立Windows scope各23项，最终scope嵌driver及全部repo Python导入闭包前后身份；前scope遗漏审计helper的范围限定保留，总新增46次不冒充原CI。源final1155文件inventory前后不变，首轮审计失败、历史超时/旧ABI错误及候选源码后改均保留；详 `out/qaplet-m2/merge-10/handoff-001.json`。窄CI/diff/双树clean通过，准备推送当前WorkPool增量Full，10不提前关闭或放行11。13正在融合实际Work完成交接并封口PID失去可证明身份后的安全保留，未将候选当最终供应。

07 由独立 merger 合入 `fa4d76892d164721bf078d629f87830c410abf6e`：源 tip `eddee103`、最终供应产品 `ddf00aa` 与合并 raw/Git blob 的 67 RECIPE 全等且零 CR；74 供应文件、密封 ZIP、三 APE、双 archive 各 13 ELF64 REL、正常/包外/故障 raw 原件均核验。Linux 129、Windows 128 pass + 1 optional blocked；每宿主各 1,000 timer 生命周期/100 本票混合、包外应用 8、timer 两宿主各 10、fault 各 1 全通过。Windows fault 原报告只有事后侧车身份，保留该限定，merger 冻结新 driver 同 APE 新 scope 实际重跑 1 次并嵌前后 driver/导入脚本身份。历史 metadata 红、计时红、两宿主各 102 observer 与暂停绿均保留，候选源码后来变化明确记录并用保存的 controlled_source 重构原 hash，不冒充当前文件。见 `out/qaplet-m2/merge-07/handoff-001.json`；仅本地完成，不提前关闭07或放行08。

05 resize 修复由独立 merger 合入 `d0ee29e877d412473da783b92c20f0609cacdabe`：真实受控顺序 loop 3/3 红，修复后 controlled20/20、normal20/20；正常 Linux19、Windows21、实际64K19，CLI与 dev check369/183实际186条件skip/bootstrap通过。两供应各12 RECIPE与源HEAD/产品`527bfd8`/合并 raw/blob/LF、681身份引用、316 raw语义检查及公开64K重放均通过，合并后窄公共CI/diff/双树clean。原WinServer具体调度只记与已复现顺序兼容，不声称唯一归因；缺最终resize负向在内部2秒明确失败且模式/资源清理完成，无外层timeout通过。见 `out/qaplet-m2/merge-05-resize/handoff.json`。当前准备共推07+05取得新五原生/实际64K与WinServer验收，两票保持awaiting_cross，父M2继续in_progress。

`563ff33` / Full `41788055889` 最终 completed/failure，仅 Windows TTY resize 失败；09 的独立严格闭包及 root 再核均通过，按本票范围 resolved。五原生各 115/合计 575、实际 64K 115、包外应用 8、controlled fault 1、当前 M0 六目标 235；29 原 ZIP、12 nested、11 job 原日志及64 RECIPE/冻结 evaluator/同密封 SDK/双 archives/原生 loader/journal seal 与实际清理均核验，见 `out/qaplet-m2/full-563ff33/HANDOFF.md`、`verify/scope-summary.json`、`09-acceptance-check.json`。M0 console 镜像本次与完整 journal 不同，严格 transport 仍按实际密封 journal 通过，不要求诊断镜像完整。旧 adapter 故障及首次 SDK 下载 EOF、未校验 partial 被重试覆盖的过程缺口如实保留，不谎称该 partial 已归档。

09 全部 2,308 个本地输出（348,998,746 逻辑字节）已保存至 `tickets/09/`，`store-09-created.json` 与 `store-09-verified.json` 通过；源树继续保留供冻结 consumer 重放。05 当前回归重新 claimed，独立旧验收不改标；本机同字节 ConPTY 普通/并行共 420 次未复现，不冒充修复。07 的真实元数据名称遗漏已修复；另一个 15ms timer/1ms wait 的 Windows 断言经两宿主实际单调时钟与受控暂停证明属于夹具未容纳合法 oversleep，正按 B05 修正夹具后重新供应，不修改产品等待或放宽 timeout。10 的真实 HostNotify lease 接线候选已绿，待07集成后完成三类轮转与最终供应；13 独立推进实际 spawn/reap 和继承安全。

09 的源 tip `4615525` 已由独立 merger 合入 `2c44fc3`，本地逐项原始日志重放与供应身份补核通过，见 `out/qaplet-m2/merge-09/evidence-verification-006.json`；旧 Windows fault 消费 driver 被修订后的身份缺口保留，以冻结的新 driver 同 APE 重跑 1 项通过，不改原报告。当前集成的 Ubuntu `scripts/dev.py check` 再次通过：`out/qaplet-m2/check-notify-integration.json`，368 项中 179 实际通过、189 条件 skip，系统 C bootstrap 通过。准备推送当前集成取得新五原生及实际 64K；09 仍 awaiting_cross，父 M2 未完成。

2026-11-04 继续实施时核对：07 的现有分支正在融合 `2c44fc3`，保留尚未完成的 merge 冲突；10 已合入该集成点并保留未提交测试增量，由 fresh implementer 接续，不重置代码或覆盖失败输出。09 已由独立 merger 完成机械合并，继续补齐原始日志重放收据并等待当前配方 Full；13 的直接 blocker 已满足，开始独立实施。父 M2 保持 in_progress。

06 原始实施及两条修复 worktree 已在源对归档核验、干净/已合入/绝对路径与非 reparse 审计后清理，分支与全部原件保留。`completed-06-worktree-audit.json`、`completed-06-worktree-cleanup.json` 和三份 `store-06*-pre-cleanup/verified.json` 保存收据。guest 修复的 604 个原件实际来自主树 `out/m2-06-guest-fix`，该原始输出也继续保留；不以空 worktree/out 冒充原件来源。清理不改变旧报告或产品验收范围。

01 首次 Git shim 为 `git init` 夹具误注入 worktree 环境，向共享 `.git/config` 写入 `core.worktree=/mnt/d/Workspace/dairn/qaplet/.cache/worktrees/m2-01`，使 Windows Git 报 `/mnt` 无效。根代理与独立 merger 先用显式 git-dir/work-tree 确认集成分支 `791af3d` 且干净，保存 `out/qaplet-m2/git-config-before-worktree-repair.txt` 后，仅按精确值移除该项，普通 Windows Git 恢复。01/03 的临时 shim 已改为仅当前仓库的只读查询，其他 fixture 和写操作使用真实 Git；修正后的本地检查分别通过。没有改写产品或历史验收以掩盖此次失败。

## 当前回归与集成进度（2026-11-04）

`453fe76` / Full `41817613432` completed/failure：8 jobs成功，两个Mac native与startup-matrix失败。严格机读记录两Mac各154pass/8fail，其中Intel limits exit0但stderr非空，其余15个失败exit1且全部无timeout；三其他native各162、实际64K162、package39/fault2、双archive各15、当前M0 Full235及三个独立guest ID均通过。SDK journal162命令/27payload（同ID direct+nested双transport）、M2四命令/8payload、M0 21命令/6payload的seal/raw/deadline/前后hash/清理均核。29原ZIP/12nested/11原日志、offlineverify、3635唯一消费文件最终rehash及34实际imports收据/44实际module/77 repo Python/27helper前后一致，signedURL文件扫描0；30次下载中的受限curl失败与独立002成功、helper历史失败均保留。见 `out/qaplet-m2/full-453fe76/HANDOFF.md`、`final-handoff.json`、`acceptance-check.json`，完整性pass不等于Full/13通过。02范围五原生95+实际64K19严格充分，root复核14份收据后按本票重新完成；108普通+6受控故障分开记，旧867失败不改。13继续定向修复两Mac16失败及真实B02单Process POD跨线程查询缺口。

11由独立merger合入 `d8a9ba9`（源d8b5e826 / 供应产品96e8eb4）：77 raw/Gitblob/LF、ZIP79成员/SDK/三APE/archive各15、normal169/package46/fault2与公开红绿均核验。3579份原件/632,688,371逻辑字节内容和mtime前后不变；合并树同源/双树clean/diffcheck、Ubuntu input-lock、两窄CI和真实bootstrap通过，未重复矩阵或建新SDK。父Python profile未覆盖make/test子进程实际imports的限定明确保留，静态全repo前后身份与实际profile分开记录。见 `out/qaplet-m2/merge-11/AUDIT.md`、`post.json`。11保持awaiting_cross，准备与13最小查询/诊断阶段一同验证新Full；12/15尚未放行。

已完成08/10及文件修复worktree已在完整归档/源对档核验及独立Git/绝对路径/reparse/遗漏输出审计后清理：各4352/2591/1203文件、717,661,830/620,370,368/213,567,341逻辑字节，out外各79个可再生pyc；分支实际SHA与归档保留，共享Git config不变。收据 `store-08/10/10-file-fix-{created,verified}.json`、`completed-08-10-{audit,cleanup}.json`。历史full08/f2等helper原源码绝对路径已不存在，重放须以保留分支重建原worktree并restore/materialize，或另存新映射，不改历史脚本/报告。02新修复冻结out也已保存并源对档核验360文件/52,371,986字节，现按 `completed-02-win-error-fix-{audit,cleanup}.json` 的干净/已合入/绝对路径/reparse/遗漏输出审计清理该树，分支与归档保留，共享Git配置不变；13/11/13-cross-fix不清理。

13 fresh `codex/m2-13-cross-fix` 已实际融合 `6b0de319`，最终产品 `0b725cc` 本地供应通过：
B02新增 `qaplet_get_process_status` 纯POD任意线程查询，保留旧owner collect和POD布局，
首次slots发布同步。旧密封SDK双宿主WrongThread3真实red、缺新符号red与永久query新ZIP
包外双宿主绿均保存。normal Linux170/Windows169+旧optional1，package46/fault2各两宿主，
Process永久公开10各两宿主/系统8与4、check370中177实际193skip/bootstrap通过。
77 raw/Gitblob/LF、ZIP79、三APE、双archive各15实际ELF64 REL及623唯一命令/1246 raw日志/
18父解释器动态repo Python闭包与370静态superset输入前后一致，限定不冒称child动态imports。
原件见新树 `out/m2-13-cross-fix/final/handoff-002.json`；首次审计schema字段错误与stage1
旧候选均保留。Mac仅新增失败时公开code/domain/error/helper及实测rlimit诊断，排序假设先行，
未改FD继承或spawn行为。13保持in_progress，待独立合并及新的原生Full取得Mac事实；两Mac16
原失败、14未放行与最终整体Review边界不变。

13 当前Mac定向修复已在fresh `codex/m2-13-macos-fix` 基于实际4d13c20本地交付，
产品54b03c8：Full434两Mac18原失败中8×2的公开domain1/error75与hard无限guard明确；
inherit旧诊断隐藏仍独立保留。旧密封SDK scoped hard无限公开red、固定原对象/thunk
审计及实验aa76dc7 /41834422457两Mac FD8192/soft256/188项6页/父不变/真实reap先行。
产品Darwin以额度内CLOEXEC目录FD在fork child枚举真实FD表关闭非stdio，保留ack，
不截断范围或弱化继承白名单；永久高FD回归和失败公开诊断同步加强。正式normal170/
Windows169+旧optional1、package46/fault2各双宿主、Process公开10与系统8/4、同scoped
hard无限绿、370项177实际193skip及bootstrap通过。77 raw/Gitblob/LF、ZIP79、三APE/
双archive各15、627唯一命令/1254 raw与父动态/完整静态Python闭包已核，详新树
`out/m2-13-macos-fix/final/handoff.json`与[13验证记录](13-process-validation.md)。
临时workflow只留实验branch；ARM实验loader不同于早报的限定保留。继续等当前产品
Mac SDK原生完整链和实际64K新Full，旧失败/独立guest输入503不改标；13仍in_progress，
14未放行，不派发整体Review或签发父M2/T03/发布。

2026-11-05（继续核验）：Mac 修复已由独立 merger 合入 `91b9a51`，对应 Full
`41848288564` 的五原生公开 SDK 严格原件共850/850通过，包含 Process50、任意线程
query5、Work100及新增取消35；package46/fault2、双archive各15、ZIP79、三APE与能力
名称均通过。该轮实际64K仍因固定输入三次HTTP503而为0，M0 Full235及三个guest
journal/seal没有成功原件，故11/13均未关闭。详主 `out/qaplet-m2/full-91b9a51/HANDOFF.md`。

固定 guest 输入供给已独立合入 `94cda36`，data-only commit `c67014e` 单独保留、
不合入产品；七份原始输入共24,773,729字节，经原签名链与锁验证后供给原fetch。
这不是新HTTP200，也不是guest执行。用户已明确批准本次M2的codex/*推送与CI，
当前 Full `41859852472` 对应94c；早期API列表显示三非Mac原生严格510/510，
两Mac排队且未列出page job。新的独立消费者 `.cache/worktrees/m2-consumer-94cda36` 以raw LF
冻结当前79项Python，当前审计在 `out/qaplet-m2/full-94cda36/`，未借旧原件补缺。

等待期间，公开同字节SDK的最小真实NOFILE诊断复现fixture准备缺口：inherit在
soft1024/hard1048576与soft1024/hard4096均于SDK调用前的F_DUPFD断言失败，
soft16384的inherit与soft1024的inputs通过，四项均无timeout。实际guest限额未知，
不能称产品或64K失败。原件 `out/qaplet-m2/nofile-fixture-probe/README.md`；fresh
`codex/m2-13-nofile-fixture` 独立修复准备阶段，保留高FD、降soft256和完整继承断言。
旧11/13初始/13查询out已归档核验后移除，源码、分支和当前Mac供应保留；恢复映射见
`out/qaplet-m2/inactive-output-storage/RESTORE.md`。父M2与最终T03、整体Review未完成。

2026-11-05（94c实际guest及初次边界封存）：当前CI固定输入checkout/import、七件
真实签名链和原fetch缓存命中通过，实际64K SDK完整执行170项，169pass/唯一inherit
准备断言fail；全部六字段、27payload前后、journal/seal及QEMU清理通过。Work当前
三native60+guest20、新取消21+7通过；两Mac原matrix明确reports=[]/blocked，仍缺
当前340常规项（其中Work40/取消14）。原Core及独立consumer均因缺Mac失败，只可得
161pass，未签发235。M0 21命令/6payload与M2四suite/8payload各自完整seal通过。
21provider原ZIP/8nested/9原日志、3033消费文件最终rehash及精确94c的90去重源码
输入/79Python/77SDK原字节已核，完整期望29/12/11缺口保留。见主
`out/qaplet-m2/full-94cda36/HANDOFF.md`、`final-handoff-001.json`。API仍将部分已有
制品的job显示queued，保留观测矛盾；GitHub官方Actions同期partial_outage仅作背景，
不归因具体job。11/13继续不足，后到原件另建增量，不改已封存初次边界。

2026-11-05（NOFILE fixture修复独立合并）：`ae6d7ce` 已no-ff合入source `5e6efd7` /
产品及配方提交 `034f3c8`，tree同源、两树clean，77 RECIPE raw/Gitblob/LF全等。
新recipe `35d10266…`、ZIP79/三APE/双archive各15及636实际命令/1272原日志均核验，
两个archive与旧54b供应同字节；只有fixture行为及两份13记录变更。Linux170、
Windows169+既有optional1、包外各46/Process各10/fault各2、低NOFILE三组最小绿和
最终Host两组绿、本地380项中180实际通过/200条件skip与bootstrap均核验。
源1561文件内容/长度/mtime前后不变，未重复构建或完整矩阵，原checker与静态全集
失败均保留并有独立成功后继。详主 `out/qaplet-m2/merge-13-nofile-fixture/HANDOFF.md`
及源 `out/qaplet-m2/13-nofile-fixture-fix/README.md`。13转awaiting_cross，11/13均未
resolved，准备推送新的Full；当前Mac及实际64K仍须新配方证据，不放行后续blocker。

2026-11-05（d1当前CI的runner缺口）：`d1d0936` / Full `41870752184` 的A1严格四原生
680项通过，含Work80/取消28/Process40/query4，包外46/fault2及双archive各15也通过。
ARM Mac job取消时API记录runner_id=0、steps=[]；page-plan/startup-matrix/full-core
亦无runner或执行步骤，guest skipped，不能归因产品。A1完整20原ZIP/6原日志及
3992消费文件封存，两个终态完整catalog均无ARM四类或page-gap provider。详主
`out/qaplet-m2/full-d1d0936/HANDOFF-attempt1.md`。

在上述无重复Core输入条件核验后，仅恢复被取消的ARM job及依赖，原四平台和构建
继续保留A1实际执行身份。A2截至17:47:32 UTC仍run/ARM及部分依赖queued、guest
skipped，20继承provider/0新上传，未冒称A2取消或新执行。18原报告CI attempt1与
11最新API envelope attempt2分别核对；源、A1原件与supplier内容/mtime不变。
详主 `out/qaplet-m2/full-d1d0936-attempt2/HANDOFF-attempt2.md`。11/13依旧不足；
后到证据另建增量。GitHub官方同期报告过事件触发/状态回传延迟并已转monitoring，
没有据此推断本job原因。现以本记录提交启动一次新的Full实例，区分旧run控制上下文
与当前runner可用性；产品、配方、平台要求和每命令时限保持原值。

2026-11-05（用户确认账号账单阻塞）：用户分别读取 D1 attempt2 ARM job 和新Full
的Annotations，原文明确job未启动是账号近期付款失败或支出限额需调整。具体是哪一项
仍须由账号账单设置确认。此前runner0/steps空和日志404的原因未知边界保持不变，
原件不重写；当前不再归因公共Actions事件，也不继续触发CI。详[账单阻塞及后续方式](ci-billing-blocker.md)。
11/13保持awaiting_cross，后续直接blocker不提前放行；可独立消费现有本地034 SDK
执行新fixture的实际ARM64/64K，单列ci=null与真实journal/seal/清理，不能冒充CI或
完整Core。没有代用户修改付款或预算，当前记录仅本地保存以避免重复触发已知受阻CI。

2026-11-05（用户调整当前开发阶段）：用户明确“我们先在本地开发，主要推进Linux跟Windows”。
按[本地开发方式](local-development.md)，11/13的本地实现验收支持后续开发，跨平台与CI
缺口保留最终24–27收口；正式Status不冒称resolved。领取12/14/15，各fresh实现者与
独立worktree并行TDD，Ubuntu供应/Linux与Windows消费，当前不push或重跑CI。
新fixture的本地实际64K SDK170/170、Process10/Work20/取消7、27payload/1081journal
与seal、QEMU0无timeout及原件不变性已经核验，明确ci=null；详主
`out/qaplet-m2/local64k-nofile-034/HANDOFF.md`、`handoff.json`。不将本地实测替代CI或完整Core。

2026-11-05（14与实际12独立融合合并）：票14 source `1294230b` 已no-ff合入actual
integration `3d03f855`，根merge `25d3eb12bfecb4661d64cb32a8460217426167d0`，tree等于source。
新组合供件normal Linux179/Windows178+既有optional1、包外两端65、fault Linux12/
Windows10及SDKNative3/0skip的当前catalog/raw日志均核验；新Windows已exit/helper未pub
竞争由14唯一修复5724f4e负责，旧密封SDK cancel/close两序真实red与正式green分别保留。
77配方raw/Git/LF、ZIP79、双archive各15 ELF64 REL、三APE headers及Ubuntu postmerge
preflight通过；root19个既有非配方Python仅CRLF恢复为Git原始LF，未改产品字节。
旧preaudit/冻结原件直接引用、无整out重复制，原source和供应未改写。14登记Local
development complete，claimed/cross_deferred保留；15已收到actual integration SHA。
详主 `out/qaplet-m2/merge-14-fusion-js12/HANDOFF.md`、`handoff.json`。不重复380/QEMU/
Full，不push/触发CI/整体Review；父M2、其他原生平台、actual64K及组合长测仍未验收。

2026-11-05（15本地实现与验收）：`codex/m2-15-process-io` 已自然融合 actual root
`ce9cd7e41fed5361c54a9112d6fccccd15ee1f21`，保留12完整JS Work与14唯一cancel/reap修复。
产品供应 `74906ff4` 的 owned Process三PIPE、可靠operation token、共享128operations/
8MiB data与物理保留、独立有限背压、EOF/ref及stop/FAILED/close全合同已在公开边界实跑。
Linux normal186、同字节Windows185+既有optional1，两端package各72、独立fault16/17，
两端生命周期各1000次；devcheck402项/202实际/200条件skip与bootstrap通过。
80配方raw/Git/LF/ZIP、双archive各16 ELF64 REL及1448 raw logs、全tracked Python/helper
前后静态身份和父实际imports分别核验；子动态imports未知明确保留，非recipe EOL仅分类。
详[本票验证记录](15-process-io-validation.md)及其物理根 `handoff.json`。本票
local_complete/cross_deferred，待独立merger；当前其他原生/实际64K/CI/Full与组合长测
仍未验收，不push、不自合root、不签发父M2或release-ready。

2026-11-05（17独立本地接收）：最终source `8b9e389c` 已融合实际root `8212c055`，
独立no-ff `caa439ae`合并树同源。冻结产品61603f2的供应4通过独立合前/合后重放：
85 recipe raw/Git/LF、原ZIP82、公开header/完整分发、双archive各18 ELF64 REL、
三fat APE及1608raw logs；normal Linux195、Windows194+既有optional1/必需缺口0，
package各81、独立fault23/24、真实stdin PTY/显式Console分类各1、原lifecycle各1000。
84 tracked Python与devcheck-3/159bd900相同，旧411工具合同及bootstrap保留原范围；
合后新实际执行Ubuntu preflight通过，共享主`.cache`。供应4覆盖最后C/header/adapter
detach握手，未重建/重复全suite/QEMU/push/CI；全部原件和旧失败未改写。
详主`out/qaplet-m2/merge-17-stdio/HANDOFF.md`及`handoff.json`。17本地完成已可放行18，
Status claimed/cross_deferred保留，原38边/135AC与父spec未变。其他原生、当前64K、
CI/Full、最终混合长测、父M2和release-ready仍未完成；16专属Process后续负向不外推。

2026-11-05（16独立本地接收）：source `353ee949` 已融合actual root `a5067a9c`，
实际no-ff `015cdb9f`合并树同源。产品13282ae6的86 recipe raw/Git/LF、82原ZIP成员、
双archive各19 ELF64 REL、公开header/三fat APE和1722 raw logs合前/合后独立重放通过。
normal Linux208、Windows207+既有optional1/required0gap，package94/94、fault24/25，
各13个JS Process、本票1000+100循环及普通SDK1000生命周期均保留原实际执行身份。
共用17 IO/stdio/header/runtime字节和STDIO24/9cases/detach BUSY保留，M2 builtin6。
lost-reap负向证明未知原生责任/容量安全保留，closed pending、显式unref和CLOSED后的
quiesce error/pending、destroy BUSY不被冒称清理成功。84 tracked Python与原devcheck
同字节，411总/210执行/201条件skip与bootstrap保留原scope；合后Ubuntu preflight通过。
静态过近似auditor的两处后续文档收据改动独立内存还原原摘要，parent actual与静态清单
分开，child imports仍unknown；原件/旧失败不改写，未重建或重复产品/历史长测/QEMU。
详主`out/qaplet-m2/merge-16-js-process/HANDOFF.md`及`handoff.json`。16保持claimed、
local_complete/cross_deferred；19/20仍等待18本地完成并独立合入。原38边/135AC、父spec、
其他原生/当前64K/CI/Full/混合长测及最终整体Review未完成，不push或签发release-ready。

2026-11-05（18本地实现与验收）：独立分支已融合actual root `516ddd4`，最终产品
`958b3e26`的供应3实现TTY25、同终端跨Runtime/别名冲突、同lease双向join、完整模式
保存/恢复/readback、显式Host resize桥和Console完整合法UTF8调用边界。Console真正
join控制helper计入既有限额，取消/stop/close等待physical/TLS/join；恢复失败保留责任，
partial scalar的实际UTF16单位/完整UTF8前缀及不可盲重试语义C/JS可见。新公开诊断与
HostNotify canary真实red后仅修TTY前置，旧非TTY写法作为19待公开证伪风险保留。
供应3全部TTY Linux13/Windows20、相关IO/stdio各27和包外公开消费各24通过；供应2
完整normal209/208+optional1、package94/94、普通fault24/25保留原执行身份，未机械
重复或重贴为供应3成绩。90 recipe与双archive各19，源/recipe/SDK/原ZIP/三APE/loader、
raw日志、实际清理及静态/父actual imports分别在[本票记录](18-tty-validation.md)和
worktree `out/m2-18-tty/HANDOFF.md`、`handoff.json`交接。SIGHUP设施失败、控制终端
alias真实产品红、event/TLS屏障红与非recipe Windows路径错误等原件均保留。
18保持claimed/local_complete/cross_deferred，待独立合入后放行19/20；不自合root。
其他原生、当前实际64K、CI/Full、组合长测和最终整体Review仍未完成，不push或release。

2026-11-05（18独立本地接收）：fresh merger实际no-ff `3e85a272`，从clean root516合入
最终source8e8833a，tree同源；冻结产品958b3e2、90recipe、84ZIP成员及双archive19+19不变。
合前/合后只读重核21scope/2536raw：供应3 TTY13/20、selected27/27、包外24/24；供应2
完整normal209/208+optional1、package94/94及fault24/25保留原身份，供应1工具411/210/201
保留原范围。合后Ubuntu root preflight通过，46解析/52编码文件及主物理.cache。父actual
依赖与静态清单/child imports unknown分开；merger无新产品执行、重建或历史长测重跑。
收据与命令见主 `out/qaplet-m2/merge-18-tty/HANDOFF.md`、`handoff.json`。18保持claimed/
local_complete/cross_deferred，本地可放行19/20，未由merger领取；旧非TTY诊断仅留19待公开
证伪风险。27票/38边/135AC和父spec不变，其他原生/当前64K/CI/Full/混合长测/整体Review
及发布门禁仍待验，不push或release-ready。

2026-11-05（19/20真实开发前置满足）：18独立no-ff `3e85a272`及接收提交`64624ddd`
已完成，主收据`out/qaplet-m2/merge-18-tty/handoff.json` SHA256为
`823f8f3ac6daf4abe90ccf973be1dff0b7215dc1284e751dbae26f97e7828e11`。
09正式resolved、16本地独立合入`015cdb9f`与18本地独立合入均已满足，领取19/20，
各fresh实现者、独立worktree并行TDD；19收预算/观测/诊断，20收混合停止和两阶段清理。
共享文件按必要接口协调，不增加19→20边。19的旧IO诊断样式风险先公开证伪，不预称缺陷。
20完成并独立合入才放行21，随后21→22、19/22→23。当前领取不等于本地完成；
父spec、原38边/135AC、正式Cross/Full/实际64K/长测和最终整体Review保持待验，不push/CI。

2026-11-05（20本地组合验收）：供应2产品`e3b4bc4`完成公开C/JS混合work/timer/直接child/
pipe/stdio/TTY/HostNotify停止与两阶段清理；包外12/12两端通过，相关Host回归各23/23，独立包外
stop/waker故障首因双序各2/2。同步binding到close、work到native真join、notify和未来producer到
撤下/join的屏障分别证明；nonreturning和uncertain保持pending/BUSY，其他资源安全回收。
SDK1完整normal保留212/211+optional1原身份，未充当SDK2全矩阵。真实ConPTY完整恢复、
所有fixture/context失败和零等待重试修正见[本地记录](20-mixed-cleanup-validation.md)。
20保持claimed、local_complete/cross_deferred，待独立合入才放行21；父M2、24–27最新矩阵、
其他原生/实际64K/Cross/CI Full/组合长测/最终Review及发布仍未完成，不push或release-ready。

2026-11-05（19本地预算/观测/首因）：产品冻结1c4a91c，旧IO诊断以公开SDK16入口
canary/HostNotify在Linux及同字节Windows真实red后最小前置修复；新增独立resource POD
补共享helper和IO当前/持久峰值，保留旧ABI布局。小公开额度联立timer/token/handler/Work/
child/stdio、8MiB子额与原生总额/环境、cancel/C保留/physical容量及8种混合永久故障首因
已公开验证；原ZIP包外真实TTY/Console+Work、环境增量也单列。当前normal214/213+optional1、
package99/99、fault25/26通过，完整当前TTY及工具/原件/失败见[19验证记录](19-aggregate-validation.md)。
保持claimed/local_complete/cross_deferred，交独立merger，不自合root；正式矩阵/实际64K/
CI/Full/长测、父M2及整体Review继续待验，原Blocked by/AC和父spec不改。

2026-11-05（19独立本地接收）：fresh merger实际no-ff `597020eb`，从clean root b49f487
合入最终source5aa284d，tree同源；冻结产品1c4a91c、92recipe/86ZIP成员/双archive19+19
保真。当前完整normal Linux214/Windows213+既有optional1、package99/99、fault25/26、
TTY13/20和原ZIP额外环境/终端组合各2例，2430raw/8scope合前合后独立只读重放通过；
原Console退出/清理和恢复Host stdout后的成功通道已核，旧wrapper fail不改。
保留原source/dirty、旧fixture/fault静态helper漂移和等字节v1快照，实际父依赖与静态
过近似/child imports unknown分开。merger首轮bytes字段兼容错误及受限WSL失败均保留，
修独立helper/按既有可访问用户上下文重核后通过，供应不变。合后Ubuntu root preflight
pass，46解析/52编码和主物理.cache；产品执行0、无重建/重复suite/长测/QEMU。
接收与命令见主`out/qaplet-m2/merge-19-aggregate/HANDOFF.md`、`handoff.json`。
19保持claimed/local_complete/cross_deferred；不提前完成20或领取后继，27票/38边/135AC、
父M2/正式其他原生/当前64K/CI/Full/长测/整体Review及发布门禁均保持原范围，不push/CI。

2026-11-05（20实际集成收口）：融合19实际no-ff `597020eb`及MD-only root `a4e5fec1`，
最终SDK3冻结产品`0916ff72`、95recipe。Core/header与19产品`1c4a91c`逐文件同字节，
新供应独立原ZIP包外mixed两端12/12、相关Host/aggregate/旧头各28/28、包外首因双序各2/2
通过；ConPTY真清理、native pending/unknown保留和用户JS屏障仍单列。SDK1/2及19旧完整
范围全部保留原source/SDK/dirty，未重贴或机械重跑全catalog。当前20仍claimed、
local_complete/cross_deferred，待独立merger接收后才放行21；实际清洁HEAD/原件/回放摘要见
[本地记录](20-mixed-cleanup-validation.md)及本worktree `out/m2-20-mixed/HANDOFF.md`、`handoff.json`。
其他原生/当前64K/正式Cross/CI Full/24–27矩阵与组合长测、父M2和最终Review仍未完成。

2026-11-05（20独立本地接收）：fresh merger从clean root `a4e5fec1`实际no-ff合入最终
clean source `ec93a4ca`，合并`943c1962`的tree与source同为`c201ccf6`。产品0916、95recipe/
88ZIP/双archive19+19/三fat APE与公开/M1头静态身份未变，Core/header/SDK README与19实际
合并点无差异；合前合后独立只读重放20scope/1852raw/2376证据文件通过。当前SDK3 mixed
两端各12、selected各28、首因双序各2；每端四个safe-retention负向仍为native清理未完成，
五个终端模式恢复与ConPTY设施实际清理另核。SDK1完整normal212/211+optional1与SDK2旧
范围、source/dirty、早期失败及父actual/静态依赖漂移均保留原身份，不重贴为SDK3完整矩阵。
merger预核2拒绝和四轮解析helper原失败/v1–v4快照保留，修正仅主out；已知包内字面`..` argv
精确对冻结catalog和原ZIP child成员，未泛化路径放行。合后Ubuntu root preflight pass，47解析/
52编码及主物理.cache；merger产品执行0，无重建/重复suite/长测。接收和命令见主
`out/qaplet-m2/merge-20-mixed/HANDOFF.md`、`handoff.json`。20 **Local development:** complete，
保持claimed/local_complete/cross_deferred，本地可放行21但merger未领取。27票/38边/135AC与
父spec、正式其他原生/当前64K/Cross/CI Full/长测/整体Review及发布门禁保持原范围，不push/CI。

2026-11-05（21真实开发前置满足）：20独立no-ff`943c1962`及接收`776e50cb`已完成，
主收据`out/qaplet-m2/merge-20-mixed/handoff.json` SHA256为
`04b0e3d4d9eeebbca0a97b0102cd9778259fe5ed657c754ddbc04226352cc0a5`。
两端SDK3 mixed12/selected28/首因2原件及95recipe、88ZIP、19+19archive、1852raw已核；
保留负向不改清理语义，旧完整normal保持原scope。Root实际领取21，fresh实现者独立
worktree TDD，复用统一驱动、入口副本及标准流/终端公开边界，当前未完成本地验收。
22等21本地完成与独立合入；23的19已满足、22仍待。原38边/135AC及父spec不变，
正式Cross/Full/当前64K/长测/整体Review/M2仍未验收；当前不push/CI或release-ready。

2026-11-05（21本地完成待独立接收）：产品4892a897、选定供应6的100输入/91ZIP/
双archive19+19已由独立静态预核匹配raw/Git/producer，原dirty=true不改。供应6包外CLI
Linux67/Windows72通过；真实overlapped Host输出有pending→终态协议观察，未知完成负向
exit1并保留原生记录，native_cleanup_complete=false。供应4普通catalog Linux222、Windows
221+既有optional1、TTY13/20、mixed各12按原身份保留；公开头/双archive与6同字节，八个
typed mixed负向仍native_cleanup_complete=false。CLI复用统一drive/入口副本，实际0/1/2、
pipe/file/Console路由、旧closed exports与新raw/VT facade、最终输出错误已验。
503→511固定CRT启动差异保留，租约只签actual-entry511→raw504→511；独立preinit/ctor
观察没有更早公开读取点。普通三个固定名mixed调用由out adapter按host/case隔离，真实
argv/cwd/父imports/hash另存，不注册默认L0/CI复用。旧并跑归属unknown、初次相对路径
loader ENOENT、供应1–3与额外build-only5及所有旧失败保持原scope。完整报告、SDK/APE/
loader/原ZIP/源闭包见[21记录](21-cli-validation.md)与worktree `out/m2-21-cli/HANDOFF.md`。
21保持claimed/local_complete/cross_deferred，待独立merger；22未领取，23仍等待22。
27票/38边/135AC、正式其他原生/当前64K/CI Full/长测/整体Review及发布门禁仍待验，不push/CI。

2026-11-05（21独立本地接收）：fresh merger从clean root`a3e1a628`实际no-ff合入clean source
`9f2019f1`，合并`13abf9ac`的tree与源同为`2ced24bb`。固定产品4892、供应6的100recipe/91ZIP、
双archive19+19、三fat APE与头文件重核稳定；合前合后独立重放8报告/1871raw/613索引引用/
2613证据文件通过。CLI6 Linux67/Windows72按当前供件；供应4 normal222/221+optional1、
TTY13/20和mixed各12保留原身份，同头/双库字节相等，九个明确保留负向不签发native清理成功。
Windows三项mixed relative executable仅按实际parent ROOT/Popen语义精确解析，原child cwd、
argv/marker/waits不改；解析helper失败和旧源码版本保留。合后Ubuntu preflight pass（49解析/
52编码、主物理.cache）。父503→main511与lease511→504→511、历史源重建、producer dirty和
child动态imports unknown限制不变。主收据`out/qaplet-m2/merge-21-cli/HANDOFF.md`、`handoff.json`
保存完整身份与命令。21 Local development: complete，claimed/local_complete/cross_deferred，
可放行22本地开发但merger未领取。产品执行0，无重建/重复suite/push/CI；27/38/135、父spec、
正式Cross/64K/Full/长测/整体Review/M2及发布门禁继续待验。

2026-11-05（22真实开发前置满足）：21独立no-ff`13abf9ac`和接收`fe8a17d`已完成，
主收据`out/qaplet-m2/merge-21-cli/handoff.json` SHA256为
`f077708962c1691433cd9535cb317c1bd400cb0ef9cc634cc919c7eb99b17ab6`。
供应6 CLI两端67/72、100recipe/91ZIP及8报告/1871raw已独立重放，合后Ubuntu预检通过。
Root实际领取22，fresh实现者独立worktree TDD；真实OS中断、在途relay/未来生产者、
Host最终输出与累计5秒主动等待按原合同实施，不从原有0x03 raw输入或SDK stop成绩外推。
21启动模式差异、九个保留负向和旧供应范围继续保留；22尚未本地验收，不提前放行23。
19的23前置已满足；原27票/38边/135AC、父spec及正式Cross/Full/64K/长测/Review不变。

2026-11-06（22独立本地接收）：fresh merger从clean root `97cc8f15`实际no-ff合入最终
clean source `d52371a3`，合并 `b8b9f572` 的tree与source同为 `ad22f50d`。冻结产品1343、
109recipe/95ZIP、双archive19+19、三fat APE、公开/M1头及两份最终输入快照重核稳定，
原dirty=false保留。合前合后只读重放20选定报告/164行为case/2283保留文件/2519实际证据
文件通过；普通CLI Linux67/Windows72与专项16/9分别计数。真实OS sender/target、Ctrl+C
私有Console、累计5秒主动等待、迟到目标保留、mixed SDK唯一reap/QUIESCENT/destroy已核。
三个保留负向仍native_cleanup_complete=false，strong三项全SDK cleanup=null；Win child
真实终态53261与GetExitCodeProcess成功、CLI exit1分开。queue是独立public C Host共享边界，
不是CLI满队列端到端。两个已删除/tmp pre-repack中间物仅保留原身份，不用作最终proof；
最终供应/全部配方与ZIP输入/执行raw闭合。历史tracer输入限制、旧失败/timeout/强制设施
清理、same-CLI Windows child原因unknown、503→entry511/lease511→504→511、原dirty与
child动态imports unknown均不改贴。merger预核helper失败与源版本保留；合后Ubuntu preflight
pass（54解析/52编码、主物理.cache）。merger产品执行0，无重建/重复suite/push/CI。
主收据 `out/qaplet-m2/merge-22-cli-signal/HANDOFF.md`、`handoff.json` 保存身份与命令。
22 Local development: complete，保持claimed/local_complete/cross_deferred，可放行23本地开发
但merger未领取。27/38/135、父spec/原Blocked by/AC及正式Cross/64K/Full/长测/最终两轴
Review/M2与发布门禁继续待验。

2026-11-06（23真实开发前置满足）：19独立`597020eb`/接收`2b3035f7`与22独立
`b8b9f572`/接收`6cca6f9b`均已完成。22主收据SHA256为
`227ecb1afad46fe53427ee647d9e7e2accd0abff7a3b4a705398fcb8f3cd2c8e`，109输入/95ZIP、
164行为case与20选定报告、合后Ubuntu预检通过，false/null和诊断中间物限制分别保留。
Root实际领取23，fresh独立worktree先诊断已复现的Windows SDK→CLI child CONTROL_FAILURE，
再完成真正M2组合C Host、发行物及consumer隔离/接受门/原件身份收口。旧/新parent和child
四组合均有真实child exit1/reaped与stderr，直接无Console启动两版正常；不预设根因。
本票尚未本地完成，24/25/26等待实际独立合入；27仍需正式全范围验收。原27/38/135及
父产品/安全/正式Cross/64K/Full/长测/最终Review/M2合同不变，当前不push/CI/发布。
