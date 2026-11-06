# 23 · SDK 分发与包外 M2 C Host：本地验证

本票本地完成，状态为 `local_complete / cross_deferred`，待独立合并接收。
专属 `codex/m2-23-sdk-delivery` 从实际 integration
`9117eaae6153ccce642add21fa4f2d9ea26e95d8` 开始；19/22均已独立合入。
本记录不签发 M2、正式 Cross/Full、五原生、实际64K、长测或 release-ready。
父 spec、27票38边135AC、默认额度和旧 M1 ABI/创建模式保持不变。

## Windows 公开 SDK → CLI child

原21/22 parent ×21/22 child的四项 `CONTROL_FAILURE` 诊断仍保留在主工作区
`out/qaplet-m2/sdk-cli-child-*`；直接无Console对照也保留原范围，不重写旧报告。
本票先重放原driver：实际Windows的两项均parent0、child1/reaped、无supervisor
timeout。受限进程的两项未入main/timeout和WSL `DISTRO_NOT_FOUND` 单列为执行
上下文失败；获准的普通用户上下文使用既有Ubuntu-22.04，未安装/升级任何工具链。

最小对照与公开创建诊断原件均位于本worktree
`out/m2-23-sdk-delivery/diagnosis/`：

- `original-repro/f2d02dd7-5476-43ad-b468-5e1ae742f150/report.json` 重现旧供件。
- `stdio-contrast/06ec818c-6bdf-4d3c-8480-42082f189cfa/report.json`：stdin
  ignore/inherit都失败；空环境失败；显式单项native SystemRoot的大小写与三项
  排序对照成功。没有删除句柄白名单或隐式添加环境。
- `create-probe-run/e973c047-8488-4db8-a332-2df063b99386/report.json`：仅消费
  公开SDK的扩展创建和受控OS调用包装，子进程第一个socket即返回errno10106，
  factory返回空句柄、`creation_rollback=COMPLETE`、uncertain0；直接同字节
  factory/close/quiesce/destroy成功。标准流fstat/flags一致，排除简单低fd猜测。
  CRT `getenv(SYSTEMROOT)`是POSIX drive形态，直接启动的原生SystemRoot是native
  drive形态；SDK启动后原生值也变成POSIX形态。`setenv`不更新Win原生环境，
  因此直接改读原生环境会丢掉Host创建时修改。
- `mungentpath-build{,2}` 分别保存完整调用错误及纠正为value参数后的实际literal
  探针。固定导出的函数把 `/c/Windows` 转成 `c:\Windows`，native drive输入
  幂等；普通数据也可能被改，故产品只适配既有SYSTEMROOT而不泛化所有变量。

errno10106的含义来自[Microsoft Winsock错误表](https://learn.microsoft.com/en-us/windows/win32/winsock/windows-sockets-error-codes-2)。
读取同provider commit的上游
[`mkntenvblock`](https://github.com/jart/cosmopolitan/blob/aefad27c09ffda64c6ac64bd69f758e9f9c7b91d/libc/calls/mkntenvblock.c)
仅作研究：它会补当前SYSTEMROOT，不能用于本合同的完整替换。该源码与固定Nightly
libc的映射仍unproven；产品结论依据固定二进制的真实OS对照，不以研究替代映射。

产品修复 `7f95d46` 仅在Windows factory复制环境时将已有SYSTEMROOT的CRT
`/C/...`形式恢复为native路径；复制长度/预算不变。缺失key不增加，Host后续setenv
不生效，显式replacement/空mapping/其它值逐字保留，继承规则不变。

永久回归位于 `tests/native/sdk_process.c` 的 `--m2-process-cli-child`：公开C
spawn同一CLI、复制READY字节及双管道EOF，观察实际exit/reap/delivery，最后验证
close/quiesce/destroy。最初共享文件stdout/stderr发生覆盖，后续受管pipe版本
保留各次真实失败；另将测试外层上限调整到能容纳失败CLI真实五秒清理路径，
没有改产品预算。

最终窄红绿原件：Windows
`diagnosis/regression-Windows/69abc257-e005-4cfc-95fc-e9c48c1dcff9/report.json`，
Linux `diagnosis/regression-Linux/6391d9eb-c667-4980-ab7c-e91d698d9d74/report.json`。
旧库在Windows真实child26/exit1/reaped且保留Host输出清理未完成；新候选库精确
READY/EOF、exit0/reaped、parent原生清理完成。Linux同一APE旧/新均成功。
`process-candidate-build` 只替换22双库的process.o用于快速定位，所有源码/公开头/
私有产品头/QuickJS头/实际依赖/原双库/编译命令/packing原件均保存；不是完整SDK供件。

`candidate-windows/4ee6a05e-399d-4648-b15a-c958f1e9df0d/report.json` 的原始JS
parent候选 ×child21/22均READY/exit0/reaped；显式native root成功、POSIX root
及空mapping仍child26，证实没有偷偷补环境。该report的早期C Host stdout期望
失败保留，未改贴成通过。

受影响既有process输入/JS输入/继承/错误/取消/清理/pipe-control/pipe-bytes
Linux与Windows各8项均通过：
`related-Linux/1c56ede7-e46b-4c5a-bd9a-3341b2f7ccdd/report.json`、
`related-Windows/5e1843d1-6e86-437b-8bfa-131eb1954b3f/report.json`。
其中实际完整替换、空环境、Unicode、创建后setenv快照及额外fd/HANDLE隔离
仍按公开边界执行。未重复无变化的1000生命周期或长测。

## 分发与正式工具收口

内部独立分支 `codex/m2-23-consumer-tools` 的 `0bcc9918` 通过 `04b68e3`
融合；包外示例 `5e8cfb3` 通过 `7ced44c` 融合。最终产品配方冻结
`144d5e18b2b8041c9b0b05aaf49302fcd0e8eef9`。随后 `d6d1d16` 只为
`tests/contracts/test_sdk_native.py` 两处read_text补显式UTF-8，不属于115项
产品RECIPE；最终consumer保存实际更新的测试字节，不修改producer的119项旧快照。
最终消费者身份不是声称动态追踪所有child import；该部分仍unknown。

最终原件根为本worktree的 `out/m2-23-sdk-delivery/`；准确路径、摘要、raw清单和
接收步骤见其中 `HANDOFF.md`、`handoff.json`。以下所有最终运行均消费SKU3，
没有以SKU1/2成绩改贴当前身份。所有编译/打包/fixture在Ubuntu；Windows消费同字节。

### 冻结供应

`build-3.json`（671259字节，SHA-256
`785cb3c7617c9947d1932a663e5bf9d034662c586862b9d997941abd55b9a604`）
为成功且working_tree_dirty=false的producer。SDK recipe为
`e9c533fa65ad75e6b592399e6d3a24efbcbc916d44ac62ee463930b0a0608eff`，
SDK身份为`2891f740b55e386ea53d51baf3e21c99d207605a64b9d059b1c23b2833670501`，
原ZIP为`aafff422f0328c421587de6f664f22e8b9a33124e7fe6059ceb008ba02a5d96e`。
包含115项recipe、119项提前保存的build输入、18项engine输入、76项SDK成员、
104项ZIP成员、4个fat APE、x86_64/aarch64各19对象的archive，以及4份original-packer。

| 最终APE | SHA-256 | 字节数 |
|---|---|---|
| qaplet-host.com | 1f6fda6076ce6475c1485819a51cedd7f142e43a2d35095a01c9d7c55a9bb9aa | 6183414 |
| qaplet.com | 20fdd5ab0ef0e5c6573b21b375ea91521fa483ea7aff21aace847311a4a21da2 | 2746430 |
| qaplet-example.com | c4645d6fc72331180b1cc30a027edbbb286287fb15d2e87702cecf9d7a1e9593 | 2817275 |
| qaplet-m2-example.com | eaa8867ce295175d44172f65370d83820766564e0ae778717e2e28569a21a40c | 2750675 |

`supply-1-2-3-byte-comparison.json`保存三个真实供应及最终包外重编译的逐字比较。
公开头、旧M1头、双archive、两份示例源码、loader、CLI及两例相等；Host不同，
两个CPU slice的`__FILE__`嵌入供应目录，产生30B字面值及派生APE字段差异。
不归一化、不声称四个APE跨供应均相等。SKU3本身的四个APE逐一绑定两端真实消费。
最终ZIP在checkout外独立编译的两例与SKU3两例也各自逐字相等。

### 同一M2 Runtime的包外纯C组合例

新增 `examples/application-m2.c` / `sdk/application-m2.c` 与5字节真实输入文件。
示例只依赖公开头/目标archive，注册显式命名文件work，使用自己的外部producer线程、
复制event、keepalive和非阻塞HostNotify提示接入外部loop；同一Runtime继续spawn
child、复制stdin/finish、独立并发拉取stdout/stderr、观察exit/reap/delivery及POD。
不添加SDK内置handler、默认额度或第二executor。旧 `application.c`、`m1-qaplet.h`
及旧创建方式保留。实例能力/公开能力均67076095；manifest含14项旧默认和19项M2
默认，worker2/helper4，与CLI帮助和运行实例metadata一起验收。

六种实际模式为normal、file-error、cancel、cleanup-pending、metadata、arguments。
正常文件work返回原始5字节；错误返回稳定code19/domain1/error2；取消返回code28，
先确认接受取消及physical_pending，再释放gate并等待helper真实退出。child退出37，
stdin/stdout复制字节、stderr及两个EOF独立验证。正常/错误/取消完成真实
close/quiesce/destroy并join producer。cleanup-pending保留未退役work、字节、代码和
userdata，CLOSED后短quiesce超时，destroy返回BUSY，不谎称已清理；该负向由进程退出
收回OS资源。metadata/arguments的summary清理字段为null，分别代表未纳入该行为
模式的清理判定，不与strong下整实例清理未知混同。示例实现过程中的Windows
9472退出值和helper暂未退休错误24原件保留，没有重标旧失败。

### 最终实际消费与只读重放

| 报告 | 真实结果与范围 |
|---|---|
| normal-linux-3.json | 229/229；162 Host、53 CLI、8 M1例、6 M2例 |
| normal-windows-3.json | 228 pass + 1既有可选symlink blocked；无required skip |
| examples-linux-3.json | ZIP包外独立编译两例，14/14；不是完整package-consume目录成绩 |
| cli-linux-3.json / cli-windows-3.json | 67/67及72/72，真实输出/结果/退出/未知overlapped保留 |
| sender-linux-3.json / sender-windows-3.json | 2/2 SIGINT/SIGTERM及1/1 Ctrl+C；保存senderPID、API返回值和实际OS事实 |
| strong-linux-3.json / strong-windows-3.json | 2/2及1/1；独立native arm确认实际child身份后发布READY，公开spawn无await后同栈for(;;) |
| native-guard-linux-3.json | 只读Fast语义重放pass，不另执行产品 |
| native-guard-local-3.json | 两份真实normal均接受：Linux/Windows两个native slot pass；其余三个缺失blocked，总体blocked/exit2 |
| guest-export-3.json | 仅导出229合同和第四APE的实际64K payload；guest未执行 |

Linux宿主为Ubuntu22.04/WSL2 `6.6.87.2-microsoft-standard-WSL2`、x86_64、4096页；
Windows为10.0.22000、x86_64、4096页，普通用户实际PE执行；CPU为13th Gen Intel
Core i9-13900K。每份报告保留自身OS/执行方式和真实命令时长；没有supervisor timeout
被计为通过。旧Host的`--lifetime`输出1000是M1 qaplet_create兼容循环，不外推成
M2+quiesce1000；五原生/M2完整循环、实际64K、100混合、10000混合和1小时仍归24–26。

三个mixed固定文件smoke各在UUID/case独立cwd执行，argv使用canonical供应child，
执行前把小child逐字保留到该case内。`process_child`同时绑定精确original/retained/
package_member。strict只读UUID evidence根内retained快照并核路径/hash/size，不访问
原supplier、不扩大guard、不用hash搜索或隐式路径映射。`moved-and-mixed-contracts-3`
真实运行foreign_supplier回归1项：Ubuntu移出真实Windows报告的三个child快照即可接受，
错original路径和改变快照字节均拒绝；`mixed-contract-linux-3`另验固定文件及三cwd。

两份normal的mixed-nonreturning和cleanup-pending分别保持cleanup=false；包外例的
pending也false；Windows CLI overlapped-unknown-completion为false。strong三个
case的整SDK清理均null：Linux独立subreaper核ECHILD及/proc消失，Windows持有确切
child HANDLE核wait0/GetExitCodeProcess成功及53261，CLI退出1；这些实际退出事实
不冒充整实例quiescence。`selected-summary-3.json`列全部false/null的准确case与范围。
22旧三个false、三个strong null、两个已消失pre-repack中间物、503→511启动限制
仍保留原身份与范围，未作支持扩张。

### 输入保留、失败历史与工具验证

外部consumer在第一个compiler/helper前保存全部76个SDK member；每条记录以
`package_member/original/retained`绑定原命令路径和精确ZIP成员。每次portable link
立即保留original-packer，受控repack失败也有原件。最终两份外部报告各有76输入及
2个packing。两个原/tmp目录在清理前已不可达，原因unknown，没有执行rmtree或重建；
`commands/cleanup-temporary-3`的首个安全断言失败、第二次实际缺失观察及
`temporary-consumers-retention-3.json`逐字核验全部保留输入/ZIP/packer均保存。
最终两份retention合同各2项通过，原目录缺失不破坏审计。

`signal-build-1.json`保留独立arm及其他fixture的真实早期供应身份，其公开头/双库
与SKU3相等；arm没有链接SDK archive。后续只消费SKU3 CLI及这些确切fixture字节，
没有重标成SKU3构建。工具子分支保存Win/Linux local_check各12项（1项适用性skip）、
受控packing失败及旧22 sender/strong红绿；本分支最终相同profile合同和preflight
再次核验当前源。`package-contract-linux-3`2项验证封包/能力及拒绝伪造reap/cleanup。

SKU1在严格重放首次暴露真实`modules/../process-child.exe`路径guard失败；保留
`native-guard-linux-1`和`red-canonical-child`原件，随后RECIPE修改产生SKU2。
新增跨OSchild引用的可搬移闭包再次修改RECIPE产生SKU3，SKU2未启动完整normal。
旧SDK控制错误、受限执行上下文失败、示例早期失败和30秒test_ci_reuse聚合超时
都保留。后者的单场景离线回放12.352秒pass及strace只作诊断，不能改贴为整套CI通过。
固定Nightly libc源码映射unproven、动态child imports unknown、其它三个native目标/
guest/CI/Full及最终Standards/Spec两轴Review仍欠缺。父M2保持in_progress，release_ready=false。

## 独立本地接收

2026-11-06，fresh merger从实际integration
`9117eaae6153ccce642add21fa4f2d9ea26e95d8`独立no-ff接收最终clean source
`2c856c85223bf8565cbaa040694b940d2260cb33`。合并
`6ab8b73ca5a12acc6782e3f1e182f12b1d680f2e`的tree
`2d7ee44f23350c1478ac2b53a44b58fd27606c50`与source完全相等。主接收原件为
`out/qaplet-m2/merge-23-sdk-delivery/HANDOFF.md`、`handoff.json`；实际命令、失败
尝试、原始日志及每条读取身份保存在同目录，未改写实现者供件和历史报告。

完整合前`premerge-1.json`与合后`postmerge.json`只读重放均通过。13份最终选择中
617个实际case记录为616pass及既有Windows optional symlink blocked1；producer、
两份只读native guard和guest export不增加产品执行。正常Linux229、Windows228+
optional1，包外两例14、CLI67/72、sender2/1及strong2/1保持各自真实scope。
本票6696文件/1306747344字节、内部tools983文件及示例491文件均逐raw/hash重核；
115产品配方、119旧producer快照、18 engine输入、76 SDK成员、104 ZIP成员、四个
fat APE/sidecar/loader及双archive各19个ELF64对象闭合。119旧snapshot逐字绑定
Git144，115产品RECIPE与最终源码/原producer不变；d6唯一test文件两处UTF-8修正
及119当前consumer快照单列绑定Gitd6，不重建SKU4或改producer dirty=false。

包外例与CLI各76输入按明确original→retained→exact ZIP member核验，各两份
original-packer保留原件和精确published repacked输出可取；包外两例APE与最终
SKU3两例逐字相等。原两个/tmp已不可达，原因unknown；首个清理安全断言失败
保留，不声称由当前源码清理、原tmp仍可取、或按hash搜索重构。最终sender/strong
使用SKU3 CLI；signal fixture仍保留SKU1原供应身份，公开头/双目标库与SKU3直接
字节相等，独立native arm不以新SDK构建身份重标。CLI两端各17项实际parent
imports可取；动态child imports继续unknown，不以静态输入inventory替代。

6项typed cleanup=false分别保留正常两端mixed-nonreturning和M2 cleanup-pending、
包外pending及Windows CLI unknown-overlapped；6项metadata/arguments null属于
未适用清理判断，3项strong null属于整SDK原生清理未直接证实，不能混为成功。
Windows strong child实际exit53261、GetExitCodeProcess成功及CLI exit1分别记录；
Linux保留subreaper ECHILD+/proc消失证据。两native slot只读接受、其余三原生缺失，
native-guard-local总体blocked；实际64K仅229项payload导出，guest未执行。
旧--lifetime1000是M1 qaplet_create兼容，不当作M2+quiesce1000或100混合。

真实工具raw另核package2、mixed1、foreign supplier搬移1、retention2+2和两端
profile各12（各1项适用性skip）；不算新产品case。原工具聚合timeout、早期失败、
SKU1 guard失败、SKU2未全量normal、旧22丢失中间诊断及503→511限制均保留。
merger包外输入parser首轮误分已明确记录为helper source-lock编译，失败原件及
v1脚本保留，修正仅按确切portable source/patched-source记录分类，不泛化放宽guard。

合前/合后Ubuntu独立重核root的103份历史MD：原raw逐字保持，只CRLF→LF才等于
Git，mode不变；source worktree自身core.autocrlf=false严格clean。root单次命令
core.autocrlf=input且GIT_OPTIONAL_LOCKS=0的status/diff为空，不把原生Git缓存clean
误称raw==Git。root原EOL收据SHA12474610保持，未重写历史MD、spec或Git配置。
合后Ubuntu root preflight pass：57 Python解析、55文本编码/零违规、pinned v3与
主物理.cache；只对该进程传递GIT_CONFIG_COUNT=1的input语义，完整env/argv已留。

Local development: complete。仍claimed/local_complete/cross_deferred；本地可放行
24–26，merger未领取。父spec/27票/38边/135AC、原Blocked by/AC、安全与默认额度
均保持；五原生剩余三个、当前实际64K、M2+quiesce1000/100mixed、Linux/Windows
各10000mixed+1h、远程CI/Full、最终Standards/Spec两轴Review及父M2仍待验。
merger新产品执行数0，无SDK重建/重复suite/长测/QEMU；接收仅改本票三份Markdown。
