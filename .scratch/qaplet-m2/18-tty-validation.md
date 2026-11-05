# 18 · TTY 租约、resize 与 Console 文本

实施分支 `codex/m2-18-tty`，从实际集成点 `a5067a9` 起步；已融合16的
`71a5db50`及根的文档同步，当前根 `516ddd4a6a3516f23ad493358f4d4953b78f4990`
是本分支祖先。05/17提供前置事实，不冒充18产品成绩；没有增加对16的依赖边。

沿用Q14/Q49/Q53/T01确认的公开SDK、JS和包外纯C Host边界，以真实OS设施及
独立受控系统故障纵向红绿。实现使用implement-spec/TDD技能。原件全部保留在
本worktree `out/m2-18-tty/`，包括失败与早期实验；不移动或覆盖原报告。

## 公开合同

能力位25为TTY，24仍为STDIO；沿用既有错误值，不复用reserved，不改M1结构布局。
新options/status/support/write-status遵守size/version/零未知尾部规则。旧创建仍无
TTY能力，默认三个标准流不接管。`qaplet_tty_inspect`分别查询真实标准路由及支持项。
SDK仅关闭自持的非继承duplicate，Host原始fd/HANDLE保留；dup不隔离共享flags/mode。

`qaplet_tty_acquire`明确独占真实当前标准路由，`qaplet_tty_join`显式加入同一终端的
另一标准方向。组有一个restore owner；不能用第二独立lease代替join。进程内同一份
SDK的多个Runtime共享无动态分配的intrusive registry，拒绝终端别名冲突。Linux实际
核验包括dup和`/dev/tty`/pts：先保存完整fstat，再以该fd的正`tcgetsid`补充控制终端
身份；普通`getsid(0)`不作为身份。同进程同session的无关PTY仍可并存。其他进程、
分别加载的SDK副本及绕过SDK的修改仍由Host协调；未宣称OS全局排他。

POSIX保存一次完整termios，各加入fd保存共享OFD flags，退休按join逆序恢复；Windows
分别保存完整Console mode。release、IO error、owner观察stop、Runtime close都结束
整组，取消全部IO并等待真实物理退出/join后恢复和完整readback。单端close只结束该
方向，其closed屏障等待整组恢复，不能提前破坏另一端。恢复失败保留句柄、预算、
registry和责任，IO_ERROR可查询并可重试；不假报closed/quiescent/destroy成功。
强杀不能保证恢复；用户JS不参与终止后的原生清理。

`qaplet:stdio.acquireTerminal`返回既有IO facade；复用read/write、可取消任务、ref/unref、
closed和完整Promise jobs交接，增加joinTerminal/releaseTerminal/getSize/watchResize/
unwatchResize，不复制调度系统。模块仍满足完整module quota，M2 builtin总数6。

Host负责SIGWINCH self-pipe或Console window event到`qaplet_tty_notify_resize`的桥接；
SDK不安装进程级handler。notify是离开signal handler后的any-thread POD发布，不是
async-signal-safe。一个预留slot合并latest；owner在通知后查询实际尺寸，revision不是
事件计数。真实最终尺寸通知先于查询的fixture保留；input-only Console尺寸明确
UNSUPPORTED，显式join output后才可查询，不隐式打开CONOUT$。

POSIX TTY和普通pipe保留任意二进制字节；Windows Console write每次公开调用须提供
完整且严格合法的UTF-8文本。非法或截断文本在接受前无副作用拒绝，不跨独立write缓存
半scalar。一次op可跨native字节块/pump增量转换；实际Console接受的UTF16单位单独保留。
正常partial可在同op继续剩余wide单位；error/cancel时`complete_utf8_bytes`/bytesWritten
仅表示完整交付scalar对应的原UTF8前缀，`native_utf16_units`保留真实OS接受单位，
`partial_scalar`揭示半scalar副作用。这个前缀不是可盲重试offset，C/JS均能观察。
已验BMP输入、补充平面输出、连续完整Unicode块、SGR31/reset；ConPTY补充平面输入及
原始非法UTF8检测仍为UNSUPPORTED，不做替换解码，也不宣称完整VT/Node/readline/PTY API。

## 原生预算与完成屏障

TTY endpoint、mode snapshot、registry记录和固定转换空间由原IO pool预算承担；操作与
数据保留共用既有配额。POSIX TTY无helper；Console首个成功lease按需启动每Runtime一个
join控制helper，计入helper4/native handles/请求栈1MiB，保留至Runtime close/quiesce。
每个物理Console IO另占helper，input cancel event也计句柄；可预检冲突/低额度先拒绝。

Console IO worker只由控制helper唯一pthread_join，真实join发布后才可复用记录、generation
和缓冲；控制helper不触终端或JS。owner收集后恢复整组。worker TLS gate实际证明closed
保ref并进入OS等待，gate释放后由真正join通知自动恢复，无外部SDK wake。控制helper自身
TLS/退出未确认仍阻止quiescence/destroy；有限quiesce可重试，不承诺即时/硬超时，不再生
无穷reaper。IO retirement兜底关闭已无physical/helper责任的event，防JS完整jobs提前
退休造成handle泄漏。普通pipe/file/Process/Work未泛化改造。

## 关键红绿与失败历史

- `red-lease/`以17供应SDK编译公开TTY consumer，类型/函数缺失；slice1之后逐片完成。
  `red-cross-runtime/`记录独立Runtime重叠lease红，slice2全SDK registry绿。每片重编全部
  Runtime单元，仅复用未变QuickJS对象；这些candidate明确实验，不冒充正规供应。
- `red-resize/`、`red-js/`记录新接口/模块未实现；slice3/4b完成。slice4错误JS_IsArray
  签名编译失败也保留。
- `windows-red-output/`记录非法UTF8被接受的真实Console红；slice5严格文本/输入绿。
  `red-join-c/`、`red-join-js/`之后slice8完成同lease双向、查询、Unicode和resize。
- `windows-red-exit-9/`记录TLS未退出时closed未进入OS等待；slice10控制helper真实join
  通知闭环绿。`windows-exit-10/`、`windows-detail-10/`保留JS退休后的event泄漏红：
  handles1而ops/helper/endpoint0；slice11在retire兜底关闭后回基线。控制helper自身TLS
  延迟也实际证明quiesce pending/destroy BUSY，真实join后预算归零。早期fixture误用立即
  destroy、input-only watch的失败原件保留，按既有两阶段合同修正fixture。
- 供应1普通normal Linux209/Windows208+optional1、包外Linux94、fault Linux24、L0及
  preflight成功保留原scope；`tty-1-linux.json`实际失败，不以normal覆盖：大多数真实
  exit=-1且无输出。`pty-session-diagnosis/report.json`同一制品仅start_new_session差异
  得到exit0/-1，定位Host openpty偷取控制终端导致SIGHUP。私有设施改为显式O_NOCTTY，
  只有专门信号/别名子进程设置控制终端；`session-suite-12/report.json`为实验13pass。
- `red-controlling-alias/tty-controlling-alias.json`以供应1记录pts与/dev/tty fstat不同但
  tcgetsid同正值，错误接受第二lease；无关PTY同session仍应允许。产品补终端身份后
  `controlling-alias-12b/`实际conflict4/unrelated0/join0，并有完整恢复；供应2正规重验。
- `fault-2-windows.json`保留4个失败：非recipe replay helper遗留17路径，向Windows
  传入/mnt/d子进程路径触发真实ENOTDIR20，另漏16新增Win-only用例。原helper/报告不改，
  独立`replay_windows_v2.py`修正映射/catalog后仅重跑权威fault25并通过；未重复已绿94。
- 最后公开诊断canary：`red-diagnostic-prefix/`、`red-diagnostic-notify/`以供应2 archive
  实际显示10次调用改写短诊断，HostNotify两次回调合计20次违反拒绝/诊断不变合同；
  两Runtime均先清理再报红。同一Ubuntu制品Windows `driver-red-diagnostic-windows.*`
  实际exit256并复现相同计数。958b3e2仅修新TTY入口，诊断检查失败直接返回；NULL join
  也先过HostNotify guard。现有非TTY IO相同写法仅为未独立复现风险指针，交19公开证伪，
  不在18扩改。新负向包含在最终TTY validation，旧红不覆盖。

## 冻结供应与实际执行范围

供应2冻结`6a012943f1002de4842b3c300a42d66f1af8e60c`，recipe
`e7646c4d9448aefc5f13bc00c7b50930252328c2c0a88f9ae3ab96d8ab7cb3a6`，SDK
`6d458170d799bf7de27973f2ba31b6c4473d546c9c1442df0d645d5613f48611`。
normal Linux209、Windows208+旧optional symlink1/required缺口0；package各94、普通fault
Linux24/Windows25；TTY Linux13/Windows20均pass。它们是供应2原执行，供应3未重复这些
完整normal/package/fault catalog，不改贴新SDK身份。

最终供应3产品冻结`958b3e26ec5bfa8824259bf1aed60b0224eab543`，clean生产者，Ubuntu
`build-3.json`于2026-11-05 10:13:57UTC结束pass。90 recipe，19+19 ELF64 REL对象；
recipe `302725e66717b1fbc426e37f5aacc009a49d42df119a22f7e20a560aeb4d0f31`，SDK
`fb46f0bfdb158637d8ec55cad87308e2b47c2e9510f3e461218a1d1b05619447`。
原ZIP84成员/17467024字节，SHA256
`61999a2f175084a07e0fccf4932d9042686292dbfcd744207a44b8b4c9d4fceb`。

两次供应的产品差异仅`src/io.c`新TTY诊断前置和`tests/native/sdk_tty.c`公开负向。
共享IO/stdio正常函数、其公开消费源码/故障源码、JS/CLI/Work/Process及Python产品配方
均无变化；据此执行最终全部TTY、相关IO/stdio及包外消费，不机械重复完整旧catalog。
`selected_io.py`显式27项：TTY诊断、9项stdio、7项Process pipe、8项JS IO及facts/lifetime；
其中既有lifetime实际1000次，不宣称TTY1000或本票混合长测。供应3两端selected各27pass；
包外从供应3 ZIP公开头文件/archive重新编译，Linux24/Windows同字节24pass。全部TTY
Linux13/Windows20pass（含新诊断/HostNotify负向、完整模式readback、真实alias/resize/
Unicode、受控partial/error/cancel/恢复和join屏障）；全部ConPTY清理标志为真。相关L0/
preflight及只读审计结果在HANDOFF和原报告逐项列明。

完整工具检查`dev-check-1.json`保留原source0842320：411总/210实际/201条件skip和真实
C bootstrap通过，实际工具日志426秒；不是958b3e2重新执行411。自084到当前改变的tracked
Python仅sdk_tty.py的控制终端fixture/catalog，供应2真实13/20和targeted工具12项（11执行、
1既有条件skip）保留原scope；供应3用当前同Python真实TTY和L0重验。pinned Makefile/
evidence.py/foundation.py未改，不重复历史probe/QEMU/长循环。

## 原件与欠项

`out/m2-18-tty/HANDOFF.md`、`handoff.json`指向最终SDK/ZIP/APE、各scope原件、实际命令、
raw日志、OS/kernel/CPU/页大小/执行方式、loader和独立审计。Linux为Ubuntu-22.04/WSL
6.6.87.2/x86_64/4096；Windows11 10.0.22000/AMD64/4096真实PE消费相同Ubuntu字节。
ConPTY仅验证设施，记录process、pseudo console、线程/句柄全部实际清理。

每个guard分别保存全部tracked Python/本地helper静态过近似，以及父进程实际import/call
路径和前后hash；子进程argv/raw日志另存，子动态imports仍unknown。供应1/2原生产者没有
动态父imports抓取的部分如实unknown，不能由后续auditor反推。最终审计helper不是早期
consumer实际import；若审计后续适配，另存新版本并保留旧字节，不改写静态清单。
早期实验部分仅保存实际编译对象hash，未有同时完整源快照的部分不追认完整源绑定。

当前仅本地Linux/Windows增量，正式Status保持claimed，local_complete/cross_deferred，
交独立merger接收，不自行合入根。其余原生目标、当前实际64K、CI/Full、五平台最终
矩阵、混合资源长测/1小时及整体Review仍未完成；release_ready=false。没有push/CI/
main/tags/release或账户改动，没有自合root或清除原件。

## 独立本地接收

2026-11-05：fresh merger从clean root `516ddd4a6a3516f23ad493358f4d4953b78f4990`
实际no-ff合入最终source `8e8833a706989b89d61c7311cc49533615a87c91`，产生
`3e85a27222ddcd2959437b947db4d517047f728d`。两parent及合并树与source相同已核；
产品仍为 `958b3e26ec5bfa8824259bf1aed60b0224eab543`，其后5份MD不改变90项配方。

独立接收复用主 `out/qaplet-m2/merge-18-tty/preaudit-3.json` 的固定静态结果并逐项确认
当前身份稳定：90项raw LF/Git/producer、原ZIP84成员、双archive各19个ELF64 REL、
公开/M1头及三fat APE与sidecars。供应2静态原件和2→3两文件差异继续保留。
`premerge-2.json`、`postmerge.json`独立只读消费21scope和2536原始日志，结果通过：

| 原实际范围 | 独立重放 |
| --- | --- |
| 供应3当前TTY，真实模式/alias/resize/Console与独立fault | Linux13、Windows20 pass；ConPTY清理全部true |
| 供应3相关IO/stdio与TTY诊断、facts/lifetime | 两端各27 pass；其中既有lifetime实际1000 |
| 供应3 ZIP公开包外IO/stdio消费 | 两端各24 pass；Ubuntu源树外编译，Windows同字节 |
| 供应2原完整normal | Linux209 pass；Windows208 pass与既有optional symlink1；required缺口0 |
| 供应2原完整package/fault | package94/94、fault24/25 pass；仍为原source/SDK身份 |
| 供应1完整工具检查 | 原source0842320，411总/210执行/201条件skip及真实C bootstrap |
| 后续工具与L0 | 供应2定向12项/11执行+1条件skip；供应3TTY L0 executed/pass |
| 合后新实际检查 | Ubuntu root preflight pass；主物理.cache、46脚本解析、52编码文件 |

实际父imports/calls、静态过近似清单、子argv与child动态imports unknown分别保留；
没有把auditor倒推成早期consumer依赖。merger首次helper把动态facts的None期望误当
固定stdout，原失败命令/log保留，仅修主out helper的公开facts语义后通过；源和供应原件未改。
没有重建SDK、运行新产品或重复成功suite/历史probe/长测/QEMU，没有push或触发CI。

接收后保持claimed、local_complete / cross_deferred，19/20本地开发前置可放行；merger
未领取19/20。27票、38条Blocked by、135项AC及父规格原字节未改。其他必需原生、当前
实际64K、正式CI/Cross/Full、组合长测/1小时、整体Standards/Spec Review及发布门禁仍待验，
release_ready=false。旧非TTY诊断样式保留为19待公开证伪风险，不表述成已复现漏洞。

主 `out/qaplet-m2/merge-18-tty/HANDOFF.md` 和 `handoff.json`保存actual merge、后续
仅三份接收MD的commit、检查与命令日志身份；供应worktree、原HANDOFF/JSON及旧失败原地保留。
