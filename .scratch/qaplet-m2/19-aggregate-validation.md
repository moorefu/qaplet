# 19：跨资源预算、POD与首因本地验证

票据保持claimed；本地实现及独立接收完成，Cross/Full递延。
父M2、原Blocked by/AC及24–27不因本记录完成。产品与SDK只在Ubuntu构建；Windows
消费相同字节。工作树`.cache/worktrees/m2-19-aggregate`，分支`codex/m2-19-aggregate`。
实际起点与领取`b49f487fd1cd252695ee633e6eea2f5df9c50579`，含18 no-ff `3e85a272`
及接收`64624ddd`。交接前实际merge integration返回Already up to date，尖端仍b49。

## 产品与公开边界

`5909b8f`先以18最终供应3的公开头/archive复现普通IO诊断拒绝写回，后只修诊断检查失败
立即返回：16个普通IO/stdio入口的短declared-size、物理完整POD/canary，Linux和同一APE
Windows均实际short16、HostNotify两次callback合计32违规。Runtime先清理再报红；不是
从静态样式推断缺陷。当前正常/包外catalog保留该公开负向；新资源查询同样拒绝重入。

`1c4a91cba35f47a1b36d0a8d6bd8a71f56479041`增加独立版本化
`qaplet_resource_snapshot` / `qaplet_get_resource_snapshot`，补共享Work/IO count/data、
IO endpoint/op/data及共享Process/file/Console helper当前与持久峰值、请求栈。只在实际
取得资源时更新，释放按既有真实退出/join责任计量；池退休后峰值仍可查询。已有M1/M2
快照布局不扩大；主out独立preaudit已验证移除此新增块后旧header逐字节相同。

五项AC的本地纵向覆盖：

| 合同 | 当前公开SDK实际观测 |
| --- | --- |
| 默认/有效表与真实count | 既有完整默认/配置负向与M1兼容重验；同Runtime的timer、token、handler、Work、child、stdio小额度拒绝，未改变token/accepted、此前资源继续有效；handle/registration/current/peak及环境baseline可查询 |
| 数据、总额、元数据/环境与栈 | Work+真实file IO保留分别命中默认8MiB子额/16MiB总额、含真实4MiB事件存储的10MiB总额优先拒绝；实际增加32KiB环境令factory environment/native两公开量均增长至少32KiB；worker/helper请求栈单列，未测RSS硬上限 |
| cancel/result/physical预留 | 3操作/24字节场景，运行中取消并C release仍占额；排队取消经owner交接后才退额；C保留IO/Work结果继续收费；真实native退出/join/owner交接后补提；Process与file IO竞争同一helper1，实际reap/join后恢复；Host原句柄存活 |
| 永久故障/接受后/首因 | 独立系统配方8种Work+timer+token组合：stop/control两序、SDK分配先与stop后、stop先与实际引擎分配拒绝后、接受work后wake失败仍返回有效token且不重复执行、clock/wait故障、JS捕获真实引擎拒绝仍终止；deadline首因跨CLOSED后clock cleanup错保留；真实join后销毁 |
| POD/峰值/计数/安全诊断 | 外线程对9类纯POD各1000 OPEN/1000 CLOSED采样，无collection/JS副作用；新POD短结构、零扩展尾/非零尾及完整canary；普通IO短诊断/HostNotify不改调用方POD；现有安全异常提取、零/短缓冲、队列drop、wake/cancel/IObytes合同完整normal重验 |

另从实际原ZIP包外编译一次性公开接受夹具：真实joined TTY/Console lease+Work同场占满
2操作/16字节并原子拒绝，Windows确实计一个Console join controller和一个IO helper、
请求栈2MiB；取消后物理Work保留到真实join，全部原生结束后模式完整readback相等、
峰值保留、Host句柄恢复。PTY均O_NOCTTY；ConPTY为私有测试设施，全部清理标志true。
计数饱和沿既有UINT64_MAX分支静态核对，实际可行累计单调性由公开用例观察；没有注入
私有计数，未声称穷举执行2^64次或将线程栈/OS缓冲当SDK heap/RSS。

## 冻结供应与执行范围

唯一最终供应：`out/m2-19-aggregate/build-1.json`、`supply-1-artifacts/`，产品freeze
`1c4a91cba35f47a1b36d0a8d6bd8a71f56479041`，生产者working_tree_dirty=false。
92 recipe，原ZIP86成员/17516862字节，双archive各19个ELF64 REL。recipe
`3435066862cabe9944181099b4df39bba8e2e00b92313908b9722e8f79cf43a4`，SDK
`4eeb030a510ca56d93e3e9b56662ea191c3af37a09b37a4652207695a46bc802`，原ZIP
`77a3393e40197f19b9782639db009ac325beb4e126fc8927a36166f23106e159`。
其后本票验证Markdown不改变产品配方，未重贴旧供应身份。

| 当前供应实际范围 | 结果/原件 |
| --- | --- |
| 完整normal | Linux214 pass (`normal-linux-2.json`)，Windows213 pass+既有optional symlink1 (`normal-windows.json`)，required gap0 |
| 原ZIP包外公开normal | Ubuntu真实/tmp编译，Linux99/Windows同字节99 pass，`package-linux-2.json`/`package-windows.json` |
| 独立受控fault | Linux25/Windows26 pass，`fault-linux-replay.json`/`fault-windows.json`；原Ubuntu编译/25用例也在`fault-linux.json`，外围guard失败另列 |
| 额外环境/终端组合 | `extra-artifacts-2/report.json`两例Linux exit0；Windows原两例exit0、全部ConPTY cleanup true，由`extra-windows-adjudicated.json`只读核对 |
| 当前全部TTY/Console回归 | Linux13/Windows20 pass，`tty-linux.json`/`tty-windows.json`；全部ConPTY清理标志true，当前供应包外编译/同字节重验，未重贴18旧成绩 |
| 工具与预检 | Ubuntu preflight pass；SDK工具3项/1执行+2既有条件skip；local_check工具12项/11执行+1既有条件skip；未宣称完整make check或CI |

Linux实测Ubuntu22.04/WSL 6.6.87.2/x86_64/4096页；Windows11 10.0.22000/AMD64/4096页，
正常PE消费同一Ubuntu字节。所有source/recipe/SDK/APE/loader/原ZIP、实际命令、raw日志、
耗时及原生事实保留在`out/m2-19-aggregate/HANDOFF.md`、`handoff.json`。原有lifetime1000
和JS Process1000+100混合由完整normal实际执行；不是本票一小时/10000混合长测。

## 失败、helper与正式欠项

- 首轮counts测试漏计Process自身的reactor registration，公开预期修正为file+child+pipe三项；原失败不算产品red。
- `controlled-1`夹具在SDK allocator持锁时等producer stop，造成设施自锁，外部60s timeout只证明未完成，清理未知；改用公开Host callback先stop再真实engine malloc拒绝。`controlled-2`漏malloc wrap链接；3/4预期误把QuickJS重试后setter成功当未拒绝，实际malloc拒绝已发生、独立终止latch仍以stop为首。5及正式fault配方通过；全部原件保留。
- 首轮Linux normal与package缺显式loader目录，分别blocked，无产品成功声明；补已验证供应loader后新报告通过，不改写旧报告。
- 首轮额外Windows夹具未显式打开CONIN$/CONOUT$而失败，修后两例实际exit0。随后helper错误要求“恢复Host stdout之后的puts”出现在ConPTY raw；旧fail保留，只读auditor在原driver stdout找到该输出并核对原生清理，没有为改判重复成功产品用例。
- 首轮正式fault产品25例已pass，但外围静态过近似清单含一个无关extra-windows helper运行中变化，guard失败。实际父imports/calls稳定；仍保留失败，冻结helper后同原故障APE/Linux25重新执行并guard通过；Windows26亦guard通过。
- 各guard分别保存执行前后全tracked Python/当时本地helper静态过近似、父实际imports/calls、子argv/raw日志；child动态imports继续unknown。早期实验部分只有源hash而无同时完整源快照的范围不追认完整源码绑定；公开诊断red源可由5909b8f还原，最终供应全92 recipe已冻结并独立核对。

故障catalog中的unknown reap/uncertain close是责任保留负向成功，不表示全部已清理；16的
lost-reap、17的detach/full-jobs与18的lease/真实join恢复责任保留。其他原生目标、当前实际
64K、正式CI/Cross/Full、10000混合/1小时、24–27矩阵、整体Standards/Spec Review及发布
门禁仍deferred，release_ready=false。未push/CI/main/tags/release、未自合root、未删除原件。

## 独立本地接收

2026-11-05：fresh merger从clean root `b49f487fd1cd252695ee633e6eea2f5df9c50579`
实际no-ff合入最终source `5aa284d26c7609bb84b9ed0dc4cfa8e7c1daf019`，merge为
`597020eb1d22ad607ee530223ab9f748f4ce0b52`，tree与source完全相同。冻结产品仍为
`1c4a91cba35f47a1b36d0a8d6bd8a71f56479041`，其后四份MD不改变92项配方。

复用独立preaudit-1原件并重核producer/ZIP/成员/原始配方LF与Git身份；公开新POD之外
旧头字节、M1 fixture、86ZIP成员、三fat APE/sidecars及按当前ar命令导出的双archive
各19个ELF64 REL均保真。合前/合后以冻结19权威catalog只读重放当前normal214/213+
既有optional1、package99/99、fault25/26、TTY13/20；2430份raw日志、8个产品scope、
各guard实际父依赖与静态过近似分开核验，child动态imports继续unknown。
原ZIP额外两端各2例同APE已核；Windows原wrapper fail及原exit0/全ConPTY清理true
保留，成功输出确在恢复Host句柄后的原driver stdout，未再跑产品。

历史fault的无关静态helper漂移、旧fixture/fault/timeout与对应v1等字节快照继续原地
保留，不改变原source/dirty字段或旧报告。merger首轮helper只兼容size_bytes、未兼容
额外夹具bytes字段而失败，失败helper快照replay-v1和原日志均保留；只改独立helper后
重放通过，未改供应。合后受限进程WSL不可见发行版的失败原日志也保留；按既有环境
文档在可访问用户上下文执行同入口Ubuntu preflight通过，主物理.cache、46解析/
52编码文件、零违规。merger产品执行0、未重建SDK或重复成功suite/长测/QEMU。

本次额外接收提交仅本验证、票19及implementation三份Markdown；27票/38边/135AC及
父spec不变。接收原件、实际命令和最终Git收据见主
`out/qaplet-m2/merge-19-aggregate/HANDOFF.md`、`handoff.json`。
本地开发前置按既有依赖可供后续使用，未由merger领取后继；正式状态继续
claimed/local_complete/cross_deferred，整体M2/Review/发布门禁不变。
