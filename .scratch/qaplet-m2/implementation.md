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
| 02 | `codex/m2-02-waker` / `codex/m2-02-macos-fix` | 原件见 `tickets/02` / `02-macos-fix` | completed（`196cd75` 五原生及实际 64K 严格重放通过） |
| 03 | `codex/m2-03-pipe` | `.cache/worktrees/m2-03` | completed（五原生及实际 64K 原件重放通过） |
| 04 | `codex/m2-04-process` / `codex/m2-04-windows-fix` | 原件见 `tickets/04` / `04-windows-fix` | completed（`0e4ba21` 五原生及实际 64K 共 83 项严格重放通过） |
| 05 | `codex/m2-05-tty` / `codex/m2-05-cross-fix` | 原件见 `tickets/05` / `05-cross-fix` | completed（`0e4ba21` 五原生及实际 64K 共 116 项严格重放通过） |
| 06 | `codex/m2-06-wait` 及两条定向修复分支 | `.cache/worktrees/m2-06*` | 两修复已由 `b97b09c` / `bd14a1e` 集成，当前 112 项新 Full 待严格验收 |
| 07–27 | 待分派 | 待创建 | 等待各自直接 blocker |

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

## 一次性测试入口修复

01 首次 Git shim 为 `git init` 夹具误注入 worktree 环境，向共享 `.git/config` 写入 `core.worktree=/mnt/d/Workspace/dairn/qaplet/.cache/worktrees/m2-01`，使 Windows Git 报 `/mnt` 无效。根代理与独立 merger 先用显式 git-dir/work-tree 确认集成分支 `791af3d` 且干净，保存 `out/qaplet-m2/git-config-before-worktree-repair.txt` 后，仅按精确值移除该项，普通 Windows Git 恢复。01/03 的临时 shim 已改为仅当前仓库的只读查询，其他 fixture 和写操作使用真实 Git；修正后的本地检查分别通过。没有改写产品或历史验收以掩盖此次失败。
