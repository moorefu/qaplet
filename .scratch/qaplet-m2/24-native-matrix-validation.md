# 24 · 原生合同矩阵：本地验证

本票已独立本地接收，为 `local_complete / cross_deferred`，仍 `claimed`。
正式五原生AC未勾选，`release_ready=false`。Linux x64与Windows x64的18个
mandatory分组均有当前实际原件；macOS x64/ARM64及Linux ARM64原生为`not_run`。
实际64K guest、10000次/一小时、正式CI与最终M2门禁由25/26/27分别负责，不在本票
两槽位成绩中冒领。父规格、27票38边135AC、公开API、默认额度和M1 ABI不变。

## 固定供应与独立工具

最终目标为共同集成祖先`8a0badb36c03baed79f74b88f651e7e095ea0dc4`的SKU4。
producer为兄弟供件worktree的`out/m2-validation-supply-4/build-4.json`，SHA256
`b14bcdccb854d9a4004ea181b584189b29b514c05e49e08eea03483a20aa13e9`；
SDK `324192be6af08779e8ce60eff4aac2a74d222bd376456bb54a6115846b726c38`，
recipe `4a59fdcf6cc545e87fd11033dcafafe4afb064537652ef6d8cce435d26996136`。
所有编译及打包在Ubuntu完成，Windows消费相同字节。

本worktree的`out/m2-24-native-matrix/selection-sku4-current.json`逐份以完整
SHA/长度明确选择当前实际执行。最终严格报告、全部原件清单、命令与交接步骤位于
同目录`HANDOFF.md`、`handoff.json`和`matrix-sku4-final-2.json`；Ubuntu只读同原件
重放为`matrix-sku4-final-linux-2.json`，Windows路径先经Evidence映射再取父目录，
原先丢失Windows反斜杠路径的Linux reader红件独立保留。

新增`m2_native_matrix.py`及case-name-only catalog列出全部五原生18分组；缺失、
必需skip、fail、超时、guest替代native、错供应、重复case、M1循环替代M2、计数不足
及原始输出篡改均不能通过。reader保存实际使用的原件索引、评估器源及导入身份；
它不声称追踪每个child Python动态导入。

共享C9工具不属于115项产品RECIPE。`0e169a9`固定Windows观察器仅借用已加载
kernel32一次解析native PID/TID和OS计数，缺API/部分线程枚举或/proc读错误会失败。
受控旧C8红/新C9绿分别保留；不把观察器缺陷称为SDK实测泄漏。C9 producer为
`cycles-build-9.json`，SHA `e6cb58e65f9f16bcc18c6d53936370a1926a51ff31e781aa01f305f5c546aeac`；
APE SHA `261baef841dbb4bda7686328978660a870fedfff00f4e4983c6f432b27706b19`。
所有实际C/传递include/header/双archive在首次compiler前保存，SDK linker的原packer
输出保留。C8/f8旧短测和旧guest1000/100保持原身份，不改贴C9。

## 实际本地范围

| 分组 | Linux x64 | Windows x64 |
|---|---:|---:|
| 当前普通公开SDK/CLI/Host兼容集 | 229 pass | 228 pass；module-symlinks optional blocked 1 |
| 包外两个公开C例子 | 14 | 14，精确复用当前normal内该子集 |
| 当前CLI合同与受控output observer | 67 | 72 |
| 真实OS sender / strong gate | 8 / 2 | 4 / 1 |
| queue / mixed signal / cumulative budget / late target | 2 / 2 / 1 / 1 | 1 / 1 / 1 / 1 |
| controlled fault / 真实TTY | 25 / 13 | 26 / 20 |
| mixed正负资源合同 | 12 | 12 |
| 四真实backend | waker/process/stdio/tty全部适用ID | 同字节probe全部适用ID |
| 独立M2生命周期 / mixed循环 | 1000 / 100 | 1000 / 100 |

表中分组存在明确子集重叠，不将各列相加成唯一执行次数。原普通M1 lifetime1000仍是
M1兼容用例；独立M2 counters另外实际完成。普通集中的M2示例、混合smoke和包外例子
也按原case范围表达。信号合计为Linux16/Windows9，当前sender/strong只执行一次；
queue使用公开C Host relay而非stock CLI，其余派生观察器/受控故障来源逐一标明。
两个严格普通矩阵reader分别Linux pass、五槽位cross blocked（三缺失），只读重放
当前普通原件，不重复执行229目录。

fault和fixed-mixed辅助producer起源SKU3。新运行先逐字核对所有实际C、传递include、
公开头、双库、compiler/flags/loader/link/packing与SKU4，相等才另列target4 binding；
原producer及旧执行不改context。TTY oldfault传递include已改变，因此25针对SKU4
最小重编normal/fault两组，本票Windows消费这两份同字节供件。backend probe保留独立
原生OS探针身份，既不冒充SDK source producer，也不单独证明公开SDK支持。

## 生命周期、清理和等待

共享C fixture每轮通过公开API真实close/quiesce/destroy，混合循环覆盖完成、接受后
取消、真实缺文件错误，创建/触发/取消timer、加入外部producer、直接child真实退出/
reap、复制字节与双pipe EOF、借用stdio、实际PTY/ConPTY输入取消和完整mode恢复。
默认worker2/helper4；Windows Console控制职责保留时通过有限IO次序适应helper4，
不调高产品默认。原既有mixed fixture的worker1/helper6/queue1是明确独立配置。

每端三次额外warmup不计入请求的1000/100。只冻结一次可解释OS warm baseline，最终
计数必须不增长；保留背景thread从7降至4等实际变化，不滚动重基线，也不单凭全进程
thread数量宣称所有SDK职责退出。公开QUIESCENT/helper/worker/token归零与destroy、
已知直接child真实reap另行核对。soak模式保持同一个OPEN Runtime，暖机后固定操作/
data/token/endpoint等精确基线，允许已说明WorkPool/Console控制驻留；真实idle使用
ref timer及系统等待。C9两端31秒短证明只验证工具，不能冒充26一小时。

`native_cleanup_complete=false`保留预期的nonreturning、uncertain、受控native线程
退休失败及cleanup-pending等实际范围；fault的整进程概括字段为null，公开输出逐项
证明退休或保留责任。strong只证明已知child物理退休，whole-SDK为null。metadata/
argument等不适用项仍null。信号budget证明一次累计SDK+relay约5秒active wait和退出
1/不完整诊断，重复信号不能重置预算。Windows mixed无独立stderr停止字节，以真实
close原因/reap/quiesce/destroy和实际Console entry模式恢复证明，不宣称pre-CRT恢复。

## 失败历史与正式欠项

旧SKU3 Windows TTY output正向fixture偶发提前release，公开返回BUSY且仍有physical
retention；仅`38deb873`改正向路径等待公开pending/handoff/helper真实退休，保留5秒、
VT/字节/最终restore及TLS负向。旧red1/5、诊断red1/30、新20cases与output100/100均
封存；共同SKU4含该修正及25的mixed child接受取消前保持stdin开放修正，core/API不改。

`queue-windows-4.json`是本票错误把C Host专属--queue-stop传给stock CLI的调用失败，
原INVALID_ARGUMENT保留；`queue-windows-4b.json`改用正确固定C Host并通过，不覆盖旧件。
早期compiler错误、旧观察器不failclosed的红件、OS严格相等失败及随后授权的单次warm
baseline无增长准则、新reader实现红、UTF-8预检红和所有旧SKU3scope均保留原身份。

实际缺少三个原生宿主完整适用公开合同/backend/1000+100，当前本地不能签发五原生或
正式M2验收。CI/Full、实际64K、10000次及一小时、最终strict gate分别保持独立责任；
无push/main/tag/release/billing，也未执行正式Standards/Spec两轴审查。

## 独立本地接收

2026-11-06，fresh merger从实际integration `8a0badb36c03baed79f74b88f651e7e095ea0dc4`
独立no-ff合入最终clean source `e1d483f4d53fc32dff81fc31c0f95b20762031b4`；合并
`dd9e700b2baa740bdc9a5b537773413bf90e4aeb`的tree与source同为
`a5366e62b9391ce2764fb23b5aaa018f9690167e`。主收据位于
`out/qaplet-m2/merge-24-native-matrix/HANDOFF.md`及`handoff.json`。

最终7092项原件及显式外部闭包（669108535字节）合前/合后逐项核验通过；两端
canonical各2694项实际输入、64份评估器输入和30个parent导入身份分别重核。当前
115项产品RECIPE、210份供应raw、SKU4原ZIP/公开头/双archive、C9及历史失败均保留
原身份。独立合前/合后只读重放18组×2，均local pass、overall blocked，三缺失
native仍not_run；Windows普通228 pass与optional blocked1不改成229次通过。
原件内两端1000完整M2生命周期及100混合（34完成/33取消/33错误）已核；各scope的
cleanup false、null及未证实范围分别保留，不汇总成全SDK cleanup=true。

合后Ubuntu实际普通用户UID/GID1000、x86_64/4096、主物理`.cache`预检通过
（61解析/58编码）。103份历史Markdown raw与冻结收据逐字相等，命令级
`core.autocrlf=input`仅用于已有精确EOL差异；未重写文件或配置。父spec raw、原
27票/38边/135AC及4份冻结构建文件保持不变。merger产品执行/重编/重复suite为0，
未修改25/26原件或控制其会话；27、三缺失native、正式Cross/CI/Full/64K/长测、最终两轴
Review、父M2及发布门禁仍待验，本地接收不放行27。
