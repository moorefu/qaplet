# 20：组合停止与两阶段清理本地验证

2026-11-05。票20保持 claimed、local_complete / cross_deferred，待独立merger接收。
本记录不签发正式Cross/Full、其他原生、当前实际64K、24–27最新矩阵/组合长测或M2最终Review。
每个新增混合case各实际执行1次；普通lifetime的1000次不冒称1000次混合长测。

## 实现与供应身份

从实际领取及integration `b49f487`开始，包含18独立no-ff `3e85a272`和接收`64624ddd`。
19最小公开诊断前置修复`5909b8fa`以`a564aab`纳入本分支；它是已有公开IO诊断拒绝的
必要共享修复，不是20新增生命周期实现。20组合探针未复现需要另改Runtime的生命周期缺陷。

最终集成供应3产品冻结于`0916ff72e063f29c5ddf3d85146041499d4ed64f`，已真实融合19独立
no-ff `597020eb`；随后融合真实MD收据`2b3035f7`及实际frontier `a4e5fec1`。
后两次只改MD，95项产品配方不变。`src/`、`include/`和`examples/SDK-README.md`逐文件
与19产品`1c4a91c`同字节，20差异为公开消费者/配方和文档；未重贴任何19或SDK2旧成绩。

- recipe `fdd57244022269bdac216a2887fd2149911d90e18e000b7286b506b06df6cfbb`，95项。
- SDK `d84c251269ed4c7efdbcc6b18649627dfa841a151935a60df9a6d5b7b8a430e8`。
- 原ZIP `edcd4b448a51d5cabc20245d91f7fef4c7a6255acf105fcdb0115f643f679556`，17564863字节。
- 原件为`out/m2-20-mixed/build-3.json`及`supply-3-artifacts/`；最终当前范围见下表SDK3。

历史供应2产品冻结于`e3b4bc476f1529ba74e9e2f80a53b155fddec4a2`：

- recipe `86bfa357258c693f2b3151fcfc463607045c991ac64b13c564b29c98f41ce4b2`，93项。
- SDK `3dab0f52c6427361e77af134625e63a70aea736c5a2630d0cb77734f413d8706`。
- 原ZIP `699545d37ba0269364e7aa34943fc9ee9166f5994718ad486942d6fcf8cef62e`，17520221字节。
- 原件位于本worktree `out/m2-20-mixed/build-2.json`、`supply-2-artifacts/`。

Ubuntu-22.04构建产品与包外消费者，使用主物理`.cache`，WSL x86_64、kernel
6.6.87.2、实际4096页。Windows11 x86_64、build22000、实际4096页消费相同APE字节；
Windows未编译产品或SDK。原ZIP含公开/M1头、x86_64及隐藏aarch64 archive，各19个ELF64 REL。
SDK2不重贴供应1或18的执行成绩。

## 五项验收映射

| 票20合同 | 公开可观察证据 |
| --- | --- |
| 请求只发布，owner即取消 | 同Runtime运行/排队work、timer、直接child及pipes、stdio、通知和TTY。满业务队列仍接受producer stop；owner前任务取消计数不变、child未退出、timer/lease仍活跃；owner确认后队列撤销、work协作取消、child在close前实际exited/reaped。 |
| DONE / IDLE / STOPPED / CLOSED / QUIESCENT | 入口42与drive IDLE单独观察；终止后then/catch/finally和event/timer canary不执行；CLOSED引擎为0而work仍保留；实际join后才QUIESCENT。CLOSED清理错误不改首因、不复活生命周期。 |
| 可重试的原生清理 | 每例实际quiesce请求0/5/5ms并记录调用实耗，pending时destroy BUSY；受控OS合法spurious wake保持同一绝对deadline。主动等待预算不是整次调用或native析构硬上限。cleanup error立即返回时，反复公开quiesce(0)仍回收可行worker。 |
| 不同保留期 | 同步Host binding的独立FILE/heap对象在close后实际释放，work随后仍检查自己的复制输入和userdata；notify在真实Host write系统边界在途时单独保留BUSY；work真join后仍因notify在途不能quiescent，撤下/完成未来producer并join后才destroy。 |
| 确定竞争、首因及不确定资源 | completion-first返回ALREADY_COMPLETED；queued cancel-first不运行副作用；release后复用slot的新generation不受旧token影响；close失效旧Process/IO token。真实socket EMFILE初始化回滚后新实例成功；close真实释放再数值复用/EINTR保留unknown；永不释放work gate时，child/IO/stdio/TTY仍独立回收。独立包外Host另验stop与waker EIO两种确定接受顺序。 |

正常Host为快速原生提示；通知gate只存在独立`mixed-faults.c`的Host write系统边界配方。
其他故障只包裹socket/close及pthread_cond_timedwait系统边界，不访问SDK私有布局、计数或JS句柄。
Windows process/file/Console同时占用共享helper，夹具显式配置有限6，未改变默认4。

## 实际执行范围

| 供应/报告 | 实际结果与边界 |
| --- | --- |
| SDK3 `mixed-package-3.json`、`mixed-windows-3.json` | 当前集成公开头/archive从原ZIP包外编译，正常/故障各自配方，两端12/12 pass；5个ConPTY均完整模式恢复及设施reap/close/reader结束。 |
| SDK3 `selected-linux-3.json`、`selected-windows-3.json` | 各28/28 pass：SDK2选定22个Host加19的5个公开aggregate合同及metadata；实际1000次普通lifetime，明确不是完整normal矩阵。 |
| SDK3 `causes-linux-3/report.json`、`causes-windows-3/report.json` | 独立包外C Host各2/2 pass，`causes5.py`冻结源改写/OS-send配方；公开WAIT_TIMEOUT交接、两种首因、实际QUIESCENT；双APE sidecar、工具/loader前后摘要单列。 |
| SDK2 `mixed-package-2.json` | Ubuntu从原ZIP解至包外临时目录，只用公开头/archive，编译正常及独立故障APE；12/12 pass。 |
| SDK2 `mixed-windows-2.json` | 同两个Ubuntu APE，12/12 pass；其中5个真实ConPTY用例均实际reap、关闭Console/句柄、结束reader，并完整模式恢复。 |
| SDK2 `selected-linux-2b.json`、`selected-windows-2b.json` | 各23/23 pass：22个Host混合/关闭/work/process/stdio/旧M1头/owner合同及metadata；包含实际1000次普通lifetime。明确`complete_normal_matrix=false`。 |
| SDK2 `causes-linux-gate/report.json`、`causes-windows-gate/report.json` | 独立包外公开C Host，自己的复制/改写后源码和`send`故障配方；两端各2/2 pass。stop-first主因23/control errno5，control-first主因26/domain1/error5，最终native state2；不是SDK分发配方改写。 |
| SDK1 `normal-linux-1.json`、`normal-windows-context-1.json` | 产品`b787e88`，Linux212 pass、Windows211 pass+既有optional symlink1/required gap0；保留原SDK/recipe，未贴为SDK2完整catalog。 |
| SDK1 `tools-1.json`及其local-check原件 | 当前源TTY快速profile实际构建/执行通过，保留其b787e88身份；不是完整仓库或正式Full。最终preflight另存。 |

nonreturning与uncertain各两种（普通/终端）在12例中都为
`native_cleanup_complete=false`。前者work gate保留至宿主进程结束；后者unknown句柄责任使
Runtime永久BUSY，replacement由夹具持有并证明不会被SDK迟到close。它们验证安全保留，
不是SDK最终清理成功。进程结束、外部timeout和ConPTY设施清理不替代SDK原生清理事实。

## 原失败及修正

- `first-stop`误把work协作读取已发布stop等同于owner遍历，修为观察公开取消记录和child/lease。
- `races-tty-baseline`忘记取消计数是累计值，保留失败并用明确历史基数修夹具。
- SDK1 `mixed-package-1.json`的两例uncertain假定cleanup error仍主动等worker，改为反复公开零等待收集至真join。
- `normal-windows-1`受限执行上下文在首标记前超时，按已知普通用户context对照；只中止本票PID子树，保留`windows-restricted-abort.json`和缺失最终report，不宣称清理。供应1普通context完整回放通过。
- `binding-tty-windows-final`等真实拒绝暴露夹具默认helper4不足，显式有限6后通过；不改产品默认。
- `selected-*-2`使用了错误catalog label，在产品执行前拒绝；`selected2.py`准确选择`m1-frozen-header`后产生2b成绩。
- 首因补证的Windows初版一次run_once只推进就绪IO、没有送出受控wake。原失败和公开facts保留；`causes3.py`改为先观察公开WAIT_TIMEOUT再决定接受顺序，两端通过。

早期首次stop夹具未另存执行当时完整源文本，仅有源摘要/编译物/原日志，不将其作为最终源码闭包。
最终SDK3及历史SDK2、包外公开源、首因补证改写源、实际命令/日志均有独立身份。guard区分静态过近似与
父进程实际import/call；child动态imports仍unknown。全部原件留在worktree out，不删除或回写旧source/dirty。

## 交接与欠项

正式AC checkbox、Blocked by及父spec未改。local_complete仅供独立merger接收，接收后才放行21；
其他原生、当前实际64K、正式Cross/CI Full、24–27最终矩阵及混合长测、M2整体Standards/Spec Review
仍deferred。不push、修改main/tag、启动CI或签发release-ready。最终清洁HEAD、实际root融合、
逐文件原件与可重放收据以本worktree `out/m2-20-mixed/HANDOFF.md`和`handoff.json`为准。
