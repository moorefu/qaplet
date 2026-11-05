# 21：CLI 异步运行与入口结果保存，本地验证

2026-11-05，**Local development: complete**，已独立接收并合入`13abf9ac`。票21保持
`claimed / local_complete / cross_deferred`；不签发父 AC 组、正式 Cross/Full、M2 完成或发布。
入口来自实际 root `a3e1a62`，产品/配方冻结 `4892a8976f4ae45692f9f5b5887908fe089ab663`。
源 worktree 为 `.cache/worktrees/m2-21-cli`、分支 `codex/m2-21-cli`。

## 实现及五项 AC 的本地闭环

继续使用统一 M2 drive、私有 `drive_entry` 和 CLI-owned 入口值副本，没有第二调度器。
`run/eval/help/version`、参数和 raw OS 0/1/2 保留。入口 DONE 后继续 ref timer、Process
及在途 stdio；Promise 自身不保活，unsettled 明确报错，unref-only 自然返回后进入安全关闭。
SDK `get_result` 仍是最近独立完成执行；普通回调错误结算 jobs 后可复用，预算/实际分配
失败不可复用。eval 只在正常循环和所需 native 清理完成后输出入口副本一次。

CLI 在第一次 JS stdio 导出前按真实 0/1/2 接入。POSIX pipe 的共享 `O_NONBLOCK` 由 CLI
Host 明确授权、保存完整 flags，并在相应实际 IO 退休后恢复；SDK 普通借用不改共享状态。
同步 Windows pipe 继续不接入，JS 导出为 `null`，不从 duplicate/TTY probe 外推支持。
TTY/Console 先授予零 flags 租约、同终端路由显式 join。应用先释放初始组并等待各 `closed`，
再 acquire raw/join VT 的独立 facade；首次 exports 仍是旧 closed 快照。只有新租约成功才
接管已恢复的 standard 路由，旧 token、控制交接和容量不提前释放；未恢复仍 BUSY，容量拒绝
保持旧路由和模式。SDK 默认创建、不隐式打开替代终端的合同保留。

最终 Host 输出在安全 native 退休之后使用真实标准路由。Windows 原生 `WriteFile` 使用
managed IO 已推进的共享 HANDLE position；Console 采用严格 UTF-8 scalar 到 UTF-16 的
`WriteConsoleW` 文本路径。POSIX 原生 write 用线程局部 SIGPIPE 屏蔽并恢复原 mask，不改
进程 disposition。broken/invalid 最终 stdout 为稳定 IO_ERROR、exit1；stderr 诊断追加在
managed stderr 字节之后。close/quiesce/destroy 保留既有预算、物理保留与未完 exit1。

Host 对既定支持的 Windows overlapped byte-pipe 使用独立 `OVERLAPPED`、event 和 owned
data，实际 `IO_PENDING` 后等待 `GetOverlappedResult` 的终态才复用或释放；同步 pipe 和
Console 走已分类的各自路径，不新增异步文件支持。完成查询或 event 关闭失败时保守保留
记录到进程退出，明确 exit1 和 Host native cleanup incomplete。每个记录的 data 为64KiB；
固定调用流最多同时保留4个未知记录，即 payload 上界256KiB，元数据/allocator/event另计
（最多4个event）；入口副本、render buffer 和 OS 缓冲另外计算，未提高SDK默认额度。
这个上界来自 execution/unsettled、SDK cleanup、flags restoration、finish 四个固定输出点；
正常 eval 的 result/error/finish 路径最多3个。采用的原生参数和保留期依据
[WriteFile](https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-writefile) 与
[GetOverlappedResult](https://learn.microsoft.com/en-us/windows/win32/api/ioapiset/nf-ioapiset-getoverlappedresult)。

| 本票 AC | 当前公开证据 |
| --- | --- |
| 1：参数和统一驱动 | 普通 catalog 两端、旧头/ABI、run argv/cache/ESM、help/version 与预算 |
| 2：入口副本和一次输出 | 文件 `B NUL Z` 后仍精确输出 `entry NUL 中`；双通道真实 child；大结果；后台错误无成功值 |
| 3：DONE/保活/unsettled/unref | timer/TLA、独立 Process drain/EOF、悬挂 Promise、unref done 与安全 close |
| 4：0/1/2 与最近 SDK 结果 | 包外 C `m2-cli-results`、普通错误 jobs/reuse、终止 latch、分配/期限/dispatch、用法错误、broken/invalid stdout |
| 5：实际 stdio 与两阶段回收 | 真文件/pipe、所有256字节及 NUL、独立 stdout/stderr、128KiB/通道 child、4096字节 consumer、PTY/ConPTY 和当前 mixed/TTY 回归 |

实际默认 helper 容量仍为4。双通道测试的原 producer 曾在前次 helper 物理退休前遇到原子
`RESOURCE_LIMIT(24), domain0/native0/partial0`；当前 producer 对这个未接受拒绝 yield 完整
immediate 检查点再重试。没有增加默认值、提前归还物理容量或对已部分写入盲目重试。

## 当前供应6及保留的供应4回归

所有 SDK/APE/消费者构建都在 Ubuntu；Windows 只消费同字节。当前供应6原件在源 worktree
`out/m2-21-cli/`，最终入口为 `HANDOFF.md`、`handoff.json`。当前身份：

- recipe `575ffea3382863d87f9b69fe10f38478f1ccc6fabcbeac108c30449358064a81`。
- SDK `27980dc4717df3e920b8938faf7a642faba64df10b71094992af48348a29e44e`。
- 原 ZIP `a32077c28e944cd53a21099a6f00566896a3a81253deac4ab13b059c78808c7f`，17596312 bytes。
- CLI APE `5b47f77c5790b38b9a864126af5c103308fc5394005984e2bddb7486f9d77e43`。

独立静态预核主 `out/qaplet-m2/merge-21-cli/preaudit-6.json`（SHA256
`ff3ae6667afae1703e89a924fe3b454adbf20c024041e311f2a5b979598b8d37`）已核100输入
raw/Git4892/producer及LF、91 ZIP成员、公开/M1头、三fat APE与sidecar、双archive各19
ELF对象。原 `working_tree_dirty=true` 保留；当时仅非recipe文档待收口，当前身份由逐项
raw==固定Git==producer证明，不改写为clean producer，也不为这个字段额外重建。
供应4原98输入/90ZIP/原dirty和独立preaudit-4均保留。供应5在提醒前已经启动，已成功但
仅属额外build-only，不替换/重贴供应4或6的运行成绩。

| 原件与实际供应 | Linux x86-64 / WSL2 4K | Windows x86-64 native 4K |
| --- | --- | --- |
| 供应6原ZIP包外CLI | `cli-6-linux.json`：67/67 | `cli-6-windows.json`：72/72，同一CLI/consumer字节 |
| 供应4完整普通catalog | `normal-4b-linux.json`：222 pass | `normal-4-windows.json`：221 pass + 既有optional symlink blocked1 |
| 供应4 TTY normal/fault | `tty-4-linux.json`：13/13 | `tty-4-windows.json`：20/20 |
| 供应4 mixed normal/fault | `mixed-4-linux.json`：12/12 | `mixed-4-windows.json`：12/12 |

公开头和双archive的实际SHA逐项相同，见 `core-library-4-to-6.json`。最后增量只涉及CLI
Host原生输出和相应consumer/配方；供应4回归因此按原scope保留，不冒称供应6完整catalog。
`cli-4-*`原67/67也保留，但当前CLI决定性结论来自供应6。

供应6 Windows新增五例中，两个未包observer的正常value/diagnostic只证明公开数据，
`protocol_observation=not_observed`。两个受控observer例实际观察value三次、diagnostic一次
`IO_PENDING → GetOverlappedResult TRUE`，event/buffer/OVERLAPPED没有提前释放或复用。
observer保留errno/GetLastError、真实参数和返回，关联malloc块内偏移，不用私有SDK布局；
被测调用前后无额外日志分配，不持日志锁跨阻塞调用。第五例明确注入一次完成查询拒绝，
exit1、unconfirmed1且未提前free/close/reuse，`native_cleanup_complete=false`，没有宣称
输出完整或未知请求已物理退休。包外纯C public SDK Host与产品源受控observer分别列身份。

供应4未包observer的旧真实数据两个成功仍保留为成功；协议红来自独立observer对旧
NULL OVERLAPPED/无event/无pending的真实参数观察，不能伪称旧数据失败。
`overlapped-green-2.json`中普通例旧`protocol_status=pass`是空观察的vacuous字段，原件不改；
最终派生审计仅列not_observed/公开数据，受控例才列协议实测。直接绿与最终供应6闭环分别保存。

mixed 每端 nonreturning、tty-nonreturning、uncertain、tty-uncertain 四例继续明确
`native_cleanup_complete=false`；正常 catalog 的 nonreturning smoke 也不证明全部 native
清理成功。ConPTY facility reap/关闭/reader join 与 SDK quiescence 分开。普通1000次 lifetime
及既有 process/IO 循环只按原 scope 记录，不外推为完整 M2 混合长测。

普通 catalog 的三个 `m2-mixed-*` 使用固定名 `mixed-result.txt`、`mixed-waits.ndjson`。
当前由冻结的 `out/m2-21-cli/run-normal-isolated.py` 精确在这三个真实调用边界设置
report UUID/host/case 专属 cwd；其余调用仍由原 sdk.main/run 执行，结果没有替换。
实际 argv/cwd、每调用前及最终 adapter SHA、父实际 imports 单列在原报告和 adapter metadata。
这是非默认调用上下文，不注册或冒充默认 L0/CI 复用缓存命中；23可把隔离纳入正式工具并同步
消费身份。首次 Linux 相对 artifact 在新 cwd 下的三次 loader ENOENT 留在
`normal-4-linux.json`，发生于 main 前；后续绝对路径调用成功，原失败不改。

## Console 的两层事实

私有设施显式建立真实 Console 标准句柄，再原生 CreateProcess 启动 CLI；这些设施打开
CONIN$/CONOUT$ 不属于产品 fallback。CLI 和 SDK 使用的是实际标准路由。每例
`cli-startup.ndjson` 独立保留 facility 的输入503/输出7，到固定 PE CRT 后输入511/输出7。
同一 CLI 的 help 暖身仅冻结实际入口 baseline，不覆盖原 before，也不证明恢复503。

正式本地租约结论为 acquire 前 `511 → raw504 → restored511`，三路完整 mode 逐字段比较；
POSIX 还比较完整 termios、控制字节、速度与共享 flags。Windows 原生观察在 raw 已建立后
设 output CP437，再发布输入使入口结算；CLI 保存值 `A中🙂Z` 真实输出，结束仍为437，
设施随后恢复其原65001。BMP 输入与补充平面输出分开，不宣称补充平面 Console 输入或任意
非法 UTF-8 raw 检测。pipe NUL/任意 bytes 用独立字节判据。

独立早期点观察原件为主
`out/qaplet-m2/console-startup-observation/run-20261105-early-points-derived/summary.json`
（SHA256 `0a85534803cd15c730823511476053b0f6fd1da27df52e12bf95de6722960b3c`）：
无 SDK 的 preinit、constructor101/65535、main 都已见511/7/7、CP65001。没有找到公开的503
读取点，未猜清 bit8、未修改 WinMain/私有 CRT ABI/工具链。**启动前503的恢复未被实现或
验收**，22及最终矩阵必须继承这个已测固定 CRT 限制。

Windows `require_escalated` 仅指离开受限执行上下文；本次原生 token 的
`IsUserAnAdmin=false`，见 `windows-execution-context.json`，不冒称 OS 管理员。

## 失败、历史与保留范围

原红绿、命令、日志、编译源及不同供应原件全部保留。包括旧 stdout=null 红例；供应1的
入口副本绿；供应2的复租 BUSY 红、Windows CRT offset 覆盖、两端 broken/invalid stdout、
普通 producer 额度拒绝；早期无效 Console 标准句柄设施；mode503/511 原失败；首次 Git路径、
错误 GIT_DIR 污染输入校验、原工具链 WSLInterop 启动失败、测试 helper 头/GBK 读取错误。
它们不被改贴为供应6当前成绩。observer首轮缺声明编译失败和直接绿也保留原source/配方，
不替代冻结供应6消费。未完成物理清理的明确负向合计九例：供应4 mixed两端八例，供应6
Host完成未知一例；每例所属原件和scope分开。

供应3 Linux普通222、CLI66、TTY13、mixed12保留原身份。其 Windows 普通220pass + optional1
+ 新 file-case fail1，是 generic runner 的同步 pipe 现场；产品正确导出 null。当前把 file
专属案例放入显式文件 runner、generic 检查 API/snapshot，不扩张同步 pipe 支持。
供应3两端曾共享 root 并跑三个固定名 mixed smoke；旧 pass 字段不改，但这三例的轨迹可互扰，
两个留存文件最终 writer unknown，仅保存在 `root-generated-supply-3/`，不作逐 case 决定性
证据。当前决定性证据来自隔离后的供应4。

早期 TEMP helper 源消失后，`probe4-recovered.c` 及 red/probe3 源由已保存 patch/Git恢复，
SHA精确匹配原 compiler 输入；供应1的95 recipe源也逐项恢复并匹配原 SHA。
见 `historical-source-closure.json`、`probe4-source-recovery.json`，只补字节闭包，不补造执行。
静态 recipe 是保守依赖表；CLI报告/normal adapter记录实际父 imports，child动态 imports
仍 unknown。当前包外 CLI/TTY/mixed 不代替未重跑的完整 package/fault catalog。

正式五原生、当前实际64K、CI/Full、24–27完整矩阵、10000混合/1小时、整体 Review 与
release-ready 仍待验。22专门负责真实 SIGINT/SIGTERM/Ctrl+C、重复中断、5秒主动清理预算
耗尽；本票没有安装 SDK/CLI handler，也没有把该专项签成已验。22需撤下/join外部 bridge
后再 destroy；最终 Host 输出在 SDK清理之后，阻塞输出与真实中断的组合也留给其专门闭环。

## 独立本地接收

Fresh merger从clean root`a3e1a628e28b0fcde1a927d37f3aba83cf4a4db4`以实际no-ff合入最终clean
source`9f2019f18a475b6c16572bbf5d423cf10ba0e4e3`，合并`13abf9ac81188334042f362f41e52af2035047e4`
的tree与source同为`2ced24bb358b58d05eac73df5d29c4c203c96495`。产品4892之后仅四份非recipe
MD，独立重核100项raw/Git/producer、91ZIP/公开/M1头/三APE及双archive保持原字节。

主`out/qaplet-m2/merge-21-cli/`的`premerge-9.json`与`postmerge.json`独立核验8报告、
1871raw、613索引引用和2613实际文件；完整normal/CLI/TTY/mixed catalogs、实际argv/cwd、
字节/退出、Console读回、CP437、overlapped五scope和24项历史依赖快照均按原身份核对。
供应6 CLI67/72与供应4 normal222/221+optional1、TTY13/20、mixed12/12分开，九个保留负向
未改为清理成功。ConPTY设施收尾不替代SDK/Host原生物理退休；普通lifetime不外推混合长测。

Windows normal4的三项隔离mixed保留相对executable token。独立guard按实际parent source ROOT
与Windows Popen解析语义精确绑定该供应binary；child cwd仍是各自UUID/host/case目录，原argv和
marker/waits保留，未泛化路径放行。详主`path-interpretation.md`；merger解析失败1–7及helper
v1–v7保留，pass8旧helper由保存v7及记录patch重建并核原hash，不冒称原时点快照。

合后Ubuntu root preflight pass，49Python解析、52编码和主物理.cache。Merger产品执行0、无SDK
重建/重复suite/长测；仅三份必要MD记录接收。最终`handoff.json`、`HANDOFF.md`与全部命令在主
接收目录，源worktree与原件保留。票21本地开发前置可放行22，本接收者未领取；27票/38边/135AC、
父spec、启动503→511限制、历史源重建语义、producer dirty和所有正式欠项保持原合同。
