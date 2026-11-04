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
- 逐票保留红绿测试、当前源码/配方/制品及宿主身份、原始日志、失败历史和真实缺口。必需证据不足时不关闭该票或放行依赖。
- 所有票集成后才执行一次整体 Standards/Spec 独立审查；先生成并核验分轴材料。

## 当前分派

| 票号 | 分支 | worktree | 实施状态 |
|---|---|---|---|
| 01 | `codex/m2-01-runtime` | `.cache/worktrees/m2-01` | completed（本票范围独立原件重放通过） |
| 02 | 旧分支及 `codex/m2-02-win-error-fix` | 原件见 `tickets/02` / `02-macos-fix`；新 `.cache/worktrees/m2-02-win-error-fix` | in_progress（旧验收保留，当前Windows connect-error回归） |
| 03 | `codex/m2-03-pipe` | `.cache/worktrees/m2-03` | completed（五原生及实际 64K 原件重放通过） |
| 04 | `codex/m2-04-process` / `codex/m2-04-windows-fix` | 原件见 `tickets/04` / `04-windows-fix` | completed（`0e4ba21` 五原生及实际 64K 共 83 项严格重放通过） |
| 05 | 旧分支及 `codex/m2-05-resize-fix` | 原件见 `tickets/05` / `05-cross-fix` / `05-resize-fix` | completed（`46e5cf4` 五原生97+实际64K19，resize回归闭合） |
| 06 | `codex/m2-06-wait` 及两条定向修复分支 | 原件见 `tickets/06`、`06-creation-fix`、`06-guest-fix` | completed（`bd14a1e` 五原生 560 + 实际 64K 112 严格验收通过） |
| 07 | `codex/m2-07-timers` | 原件见 `tickets/07` | completed（`46e5cf4` 五原生645+实际64K129本票验收通过） |
| 08 | `codex/m2-08-scheduling` | `.cache/worktrees/m2-08` | in_progress，interval/immediate与四类检查点轮转 |
| 09 | `codex/m2-09-host-notify` | 原件见 `tickets/09` | completed（`563ff33` 五原生 575 + 实际 64K 115 本票严格验收通过） |
| 10 | `codex/m2-10-work` / `codex/m2-10-file-fix` | 原 `.cache/worktrees/m2-10`；新 `.cache/worktrees/m2-10-file-fix` | awaiting_cross（文件fixture修复已合 `166ab50`，待新Full） |
| 13 | `codex/m2-13-process` | `.cache/worktrees/m2-13` | in_progress，C spawn 与实际退出回收 |
| 11–12、14–27 | 待分派 | 待创建 | 按各自 blocker 与槽位分派 |

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
