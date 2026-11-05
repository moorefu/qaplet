# Qaplet 工程推进与 CI 效率审计

审计锚点：2026-11-05 Asia/Shanghai，`codex/m2-integration` / `3d03f855867ae363cfb528c55a4c2b1c7b54e58e`。附件任务书全文已读。源码只读；没有构建、测试重跑、SDK全量重哈希、ZIP复制、push、CI重跑、账单或权限设置变更。本报告是现状和计时审计，不是新的产品验收。

## 已证结论

1. 本地 M2 已超过附件历史快照：01–10正式resolved；11/13本地complete而Cross欠项保留；12已独立本地合入3d03f85；14/15在独立分支继续融合与修复。TTY产品17/18、JS Process16尚未实现。Node/npm/QJB/AppCompiler没有现有实现证明。
2. 现有 `scripts/ci_tiers.py:51` 把M2集成push强制提升Full；PR除少数文字路径外全部Full。五个常驻workflow均接收所有push/PR。最近100条API（244总条）中可完整观察19个push SHA，19个都创建相同五workflow；4个诊断分支另有第六个专用workflow。只能说重复触发/入口，不能把所有平台结果当等价重复测试。
3. 当前Fast确实运行产品编译/SDK合同，但仍无条件构建历史M0 I/O、64K deep和四类M2 OS probe、工具loader、完整SDK及两个包外/故障consumer。两个真实Fast样本Build分别238/321秒，流程跨度250/336秒。目标2–4分钟尚未实现。
4. 保存数据中的四个成功Full，Build350–521秒，流程跨度671–789秒。附件“约43分钟”不是一般Full执行成本：94c的API created→updated是2572秒，但最后job实际结束给出的流程跨度为1618秒；其中两Mac各900秒runner0/steps[]，没有产品执行。
5. 7e最新Full五秒pre-runner failure，0providers/0cases、runner0、steps[]。这不是产品regression或“Fast成功”；已由用户提供的Annotations确认账单前置问题。D1 A2只有A1继承证据，新增Runner执行为0。
6. 远程branch列表12项均`protected:false`，rulesets读取返回[]；main protection读取403“Resource not accessible by integration”，不能审出required-check配置细项。变更建议仍应创建轻量scope-gate，让不适用范围产生明确结论，避免将来required永久pending；不改仓库设置。

## 分支和当前实现

| ref/工作区 | 实际HEAD | 审计含义 |
|---|---|---|
| main / origin/main | efeed762290823ae01130ee9217127cf21aab4c3 | 旧WSL环境验收历史；不是当前产品稳定发布线，不据此合并 |
| codex/m1-integration / origin同名 | 32c7a908c360e1d36359b1da8f68239b50c264ae | M1完整阶段闭包，产品验收提交43193d6，Full41542426189 |
| 本地codex/m2-integration | 3d03f855867ae363cfb528c55a4c2b1c7b54e58e | 本地领先remote11提交，已含12本地实现 |
| origin/codex/m2-integration（API亦一致） | 7e10d110b36baab9f91c6c4920673a91a268152d | 最新remote Full账单阻塞，无新产品成绩 |
| m2-11 / m2-12-js-work | d8b5e82 / 327c04d | 冻结本地交接分支；已进入root，正式claimed |
| m2-13 / cross-fix / macos-fix / nofile-fixture | 1b0886a / aa2efd7 / 3f22c5f / 5e6efd7 | 原Process实现和定向修复各留历史；不是四份新能力 |
| m2-14-process-cancel | 5724f4e（现场后续快照） | 正在实施；此前2a770d4已融合12，冻结旧原件preaudit不证明新组合 |
| m2-15-process-io | 1dc3444（现场后续快照） | 正在实施；原048ca9c之后有新增修复，未root合入 |
| ci-efficiency | 初始3d03f85 | root独立CI改造工作区，审计不触碰其tracked文件 |

现场共15个registered worktree：root、CI1、实施/定向修复9、只读consumer3、route-probe1。该数量含已冻结证据消费者，不能称15个并行实现者。读取时tracked均clean；14/15的HEAD会随另两位实施者继续推进，不冻结它们。

### 能力表

下表“已验”均限定所引产品/配方，不能把M1或旧M2逐票成绩签到3d03f85整树。

| 能力 | 已实现 | Linux已验 | Windows已验 | 其他平台已验 | 阶段/发布与剩余阻塞 |
|---|---|---|---|---|---|
| Engine / ESM | 锁定Bellard QuickJS；文件ESM、dynamic import、TLA、Promise；`src/modules.c`、`src/execution.c` | M1正式；M2当前局部供应也有真实合同 | M1正式；M2当前局部供应有真实合同 | M1五原生+实际64K，98项/宿主 | M1resolved；M2新配方全矩阵未验；release_ready=false |
| Runtime / SDK | owner单Context、值/Host Module、预算、事件/stop/close/destroy、公开静态SDK、最小CLI | M1 98项及1000生命周期；12当前177pass，包外53/fault2 | M1正式；12当前176pass+optional symlink blocked1，包外53/fault2 | M1五原生+actual64K；当前12无Mac/ARM/64K | M1resolved；M2跨资源19/20、SDK23和最终24–27未完成 |
| Event Loop | M2 opt-in/waker/wait/keepalive/timer/interval/immediate/fair checkpoint/HostNotify；`src/loop.c`、`src/timers.c` | 01、06–09各自本票已验 | 同左，各自真实供应合同 | 已resolved各票五原生+actual64K；不是完整M2 | 01、06–09resolved；完整mixed teardown/soak未验；未发布 |
| WorkPool | C有界工作10、取消保留11、`qaplet:work` JS Promise12已root合入 | 10正式；11局部供应；12本地177总合同、Work27包外 | 同一供应字节，12本地176+opt1；Work包外27 | 10当前本票五原生+64K；11仅部分原生/本地034 actual64K；12无 | 10resolved，11/12localcomplete+cross_deferred，未签全平台或阶段完成 |
| Process | root为显式C spawn/exit/POD query13；PIPE默认明确UNSUPPORTED；取消14与pipe15在活跃分支 | 13本地已验；14旧冻结原件172pass且preaudit通过；15仍新增真实负测修复 | 13本地已验；14旧原件171+opt1；当前14/15组合需独立核验 | 13历史两Mac失败及后续定向修复；94两Mac缺失/64K169/170；034本地170/170 ci=null；D1仅四native680 | 13localcomplete/cross_deferred；14/15未root合入；16 JS Process未实现 |
| TTY | 只有03借用stdio、05真实TTY/ConPTY等OS probes；没有产品stdio/TTY模块 | 03/05探针本票已验 | 03/05含Console/resize真实探针已验 | 探针对应五原生/actual64K（精确各票范围） | 17/18未开始；不能把probe声称Node TTY或npm交互支持 |
| Node Compatibility | root模块白名单仅qaplet Host/runtime/timers/work；没有Node resolver/CJS/Node内置兼容矩阵 | 未实现npm应用验收 | 同左 | 同左 | M3未开始；没有Clack/pi-tui成功证明，QuickJS内置ArrayBuffer等不等于Node Buffer |
| QJB / App Compiler | 现有SDK ZIP/APE供应和Host示例已实现；未发现QJB parser/VFS或pack/compile CLI | 现有APE/SDK成立；QJB/npm App未验 | 现有供应APE成立；QJB/npm App未验 | 既有APE跨宿主不替代未来应用编译器 | M5/M6未开始；`src/cli.c:79` help仅run/eval，不支持pack/compile |

证据入口：`.scratch/qaplet-m1/final-review-validation.md`，`.scratch/qaplet-m2/README.md`、`implementation.md`、`local-development.md`；`out/qaplet-m2/merge-12-js-work/HANDOFF.md`及其handoff.json（SHA44e965a0…）；`merge-14-process-cancel/preaudit.json`仅旧冻结14范围；`local64k-nofile-034/HANDOFF.md`准确记录170/170及ci=null。`docs/INDEX.md`仍称M2未实施，计划第22节同样过时，需同步导航而不改历史合同。

## 改造前触发与具体重复入口

| 入口 | 实际行为/文件行 | 成本及建议边界 |
|---|---|---|
| m0 | `m0.yml:4`全部branch push，PR，03:17UTC每日，RCtag，manual；`ci_tiers.py:42–60`M2push Full/PR文字也Cross，普通源码PR Full | 需要基于实际diff风险；未知fail-closed；日常Fast、定向Cross、阶段Full的合同分开 |
| foundation | `foundation.yml:3`无过滤push/PR/manual；:12–15双Ubuntu x64/ARM；:23 bootstrap，:24整套contracts | m0 makecheck已含x64 bootstrap与整套contracts；ARM本地编译语义不同，不能说完全同证据；按基础设施风险调度 |
| svc-scm | `svc-scm.yml:3`全push/PR；:12–16合同+固定Nightly获取/物化+helpers+独立build；:27真实Windows | 服务App已经退出Runtime v1主线；限定源码及基础依赖，保留手工/周期验证 |
| svc-job | `svc-job.yml:3`全push/PR；:11–15同固定输入供给和独立build；:25–26 Windows执行+真实deadline | 同上；有独立清理合同，不能删除测试换速度 |
| workflow-tools | `workflow-tools.yml:3`全push/PR；:12 preflight，:13 test_workflow* | 全suite已由m0一次和foundation两次调用；workflow子集又重复一次；形成4次调用入口，具体skip/平台行为不得算同成绩 |
| acquire-nightly | 仅manual（第六个当前文件） | 不属于普通push五workflow；禁止为了减少计数删除 |
| 并发/复用 | 上述五文件没有concurrency键；m0制品名绑定SHA，但无成功同SHA/recipe/contract run发现及复用索引 | 先对Fast过期工作cancel；正式Full/Release不得自动取消；SHA相同本身不足证明可复用 |

`Makefile:36–44`的`check`是check-ci+整个contracts discover+系统C bootstrap；`dev.py:86–94`日常check仍原样调用，不具功能选择或轻量Runtime生产路径。

`m0.yml:99–197`无条件串行执行：
固定输入取回/物化 → Hello/eval → native I/O → 64K deep → native loaders → 完整SDK → Linux全SDK/1000生命周期 → 包外consumer → controlled fault → SDK证据matrix → 四M2独立probe builds/执行。
共9个直接产品/loader/probe build入口，再有2个含编译的package/fault入口。不是同一二进制重复9次；问题是Fast相关范围过大。Full另在:199–215安装strace、断网pair+refusal+seal，五native依赖整个build job完成（:264–266），增加关键路径。

`build_ape.py:72–105`每次verify_inputs会调用完整nightly_toolchain verify；:224及:307前后各一次。SDK build（`sdk.py:582/681`）、package/fault（:299/368）、nativeIO、pages64k、四M2build均复用这一前后核验。`pages64k.py:34/79`、`m2_tty.py:32/90`是具体例子；native tool view另recheck。可以减少无关build入口来自然减少深核验，不能仅因目录存在跳过内容身份。不能从workflow step时间中精确拆出每轮hash用时；本次没有为量化重hash整个工具链。

### 证据成本

m0 producer上传3组（:227–257），各native下载3组（:316–332）并上传M2/SDK/startup/I/O，聚合器再次下载后生成SDK/startup matrix（:716–759）；Full consumer另外取得原ZIP、CRC/路径/摘要核验、manifest+Core consumer（:811–822）。这些原件完整性有验收目的，但外层Full artifact包含收集树会形成嵌套传输/本地多份逻辑树。当前94保存21provider ZIP+8nested，D1保存20provider；新增A2只有继承20份，不能称新增20份执行。

M1既有保存记录为101,472文件/13,947,727,301逻辑字节，最终376,477,164字节archive且原始报告物理保留；这是已完成的历史收据，不是本次扫描或新复制成本。`evidence_store.py`、selection及archive-backed catalog已经存在，优先复用，不建立第二套收据系统。可优先引用同一已验原ZIP/provider及receipt，避免同一原件为每个case重复复制/解包。未证明身份匹配时仍必须核验。

## 实际计时基准

原始新API：仓库`out/qaplet-efficiency/audit-baseline/*.api.json`；最新100run、branches、rulesets、94最终jobs、D1A2最终jobs、两个Fast jobs均保存。既有Full raw run/jobs仍在原`out/qaplet-m2/full-*/`，没有改写。小摘要`summary.json`（18.8KB），详细步骤`baseline.json`，离线复算`measure_saved_runs.py`。JSON内逐项保留原件相对路径。

“流程跨度”用本attempt开始到最后已完成job的completed_at，包含依赖等待与调度；“Runner合计”只加真实runner_id且steps非空的job运行区间；“排队合计”仅相同真实Runner job的created→started，可并行，不是关键路径加数。runner0/空steps的区间只作未执行等待观测，运行时间记null。run.updated_at是服务端最后状态时间，单列，不能代替产品执行秒表。

| 层级/样本 | 结果 | Build秒 | 流程跨度秒 | 真实Runner合计秒 | 可测真实job排队合计秒 |
|---|---|---:|---:|---:|---:|
| Fast41833620149 / e2eb687 | success | 238 | 250 | 246 | 4 |
| Fast41845857127 / a581fec | success | 321 | 336 | 330 | 4 |
| Full41632318680 / bd14a1e | success | 521 | 789 | 890 | 26 |
| Full41796608784 / 46e5cf4 | success | 391 | 696 | 815 | 27 |
| Full41809892923 / f2b0e0c | success | 481 | 773 | 881 | 24 |
| Full41812117094 / e742c76 | success | 350 | 671 | 796 | 27 |
| Full41859852472 / 94cda36 | failure，Mac缺/guest fixture fail | 482 | 1618 | 840 | 20 |
| Full41870752184 A1 / d1d0936 | failure，ARM Mac缺/下游未执行 | 521 | 1450 | 749 | 321 |
| 同run A2 | failure；6成功job执行时间均早于A2、仅继承 | null | 909 | 0新增 | null（无新Runner） |
| Full41881834665 / 7e10d11 | pre-runner failure | null | 5服务状态跨度 | 0 | null（无Runner） |

采样：保存16个Full attempt记录（含初始/失败/partial、4个success），Fast2个。没有Cross有效计时样本，没有代表性总体抽样，故p50/p95、单日总Runner分钟、同合同重复执行率、cachehit/miss均为null。不是0。上述Fast分支本身还启动诊断workflow，不把额外专用诊断归为普遍开销。

实际阶段计时示例：
- Fast a581fec Build321秒：完整仓库check50；输入获取/校验与helper compiler35；Hello/eval43、SDK55、历史nativeIO+deep20、四M2probe42、loader19、package+fault32；SDK实际执行5；producer三个上传12。build steps混有audit/smoke/hash，不能把211秒混合build/verify称纯编译CPU时间。
- Full e742c76 Build350秒：check39；SDK build35；Hello/eval30；四M2probe30；package+fault23；断网pair/refusal93（另strace5）；producer上传11。全run所有步骤的evidence transport+verification合计146秒，是并行Runner工作量，不可从671秒直接减掉146得优化工期。
- 94c两Mac runner0/steps[]各900秒，page-plan依赖native，因而15分钟未执行等待在关键路径；不是SDK测试15分钟。最终jobs16:05:28完成，API updated16:21:22，差954秒是状态时间差，不归到产品、Runner执行或排队。
- D1 A1 Intel Mac实际队列308秒（其余job队列合计13秒）；ARM Mac无Runner。A2 API给继承成功job新ID/run_attempt2，但执行区间16:46–17:01早于A2 17:24开始，必须基于真实时间和原provider身份排除新增执行，不能靠attempt字段计费估算。
- 最新7e整体5秒是失败通知延迟，0SDK/产品步骤；不将其纳入Fast/Full产品反馈分位数。

## 失败分类和环境风险

| 已证事实 | 分类 | 不能推出的结论 |
|---|---|---|
| 用户两段Annotations“recent payments…failed / spending limit…”；最新7e无Runner | Environment / Runner，子因billing prerequisite | 不归Product；也不改账单/额度，不试第3次重跑 |
| 94两Mac/ D1 ARM Mac cancelled、runner0/steps[] | Environment / Runner；具体该job原因Unknown，保留取消状态 | 不由无log推断Runtime hang、OS bug或付款发生时间 |
| 94actual64K SDK169/170，inherit highFD assertion errno22及cleanup errno39 | 测试fixture/实际受限资源适配问题（Build/Test Tooling）；证据中guest NOFILE未测，具体资源值Unknown | 不是丢弃Work已通过结果；也不能把该170全部标green |
| 旧两Mac Process FD枚举/纯POD query、Windows failed-connect差异 | Platform ABI / Product行为，各自修复票范围 | 历史修复不代表3d03f85新整树Cross |
| guest固定URL/签名输入缺失，采用固定c670供给后校验 | Missing Resource（取得阶段） | cachehit不等于新HTTP200；输入供给成功不等于guest/SDK通过 |
| 缺native报告导致matrix/Core失败 | Evidence / Aggregation（上游缺失是保留原因） | 不能补旧提交绿色报告或空manifest |
| 自动/手工停止过期任务 | Cancelled / Superseded，只有已证时才用 | 无runner cancelled本身不证明由新push取代 |
| 无够日志/时间 | Unknown / null | 不猜根因、百分比或性能 |

## 当前关键路径与工期证据

正式resolved是10/27；11/12/13本地complete共3张，仍claimed；14/15并行实际修复；16–27尚待实施。当前关键依赖是14/15→16，12/15→17→18，再19/20→21→22→23，24/25/26并行最终验收→27。本地开发可依用户已授权方式继续，平台缺口不能提前冻结ABI/签发发布。

从Nov3 14:33批准输入到Nov4 18:56附近，前十票达逐票正式闭包约28小时；包含既有合同拆分、探针与并行实施，不代表任何后续票每天8张。12首次JS主实现提交08:10，本地交接08:48，独立merge08:59；只是一个约49分钟提交窗口，不是完整工程人时。Nov4晚至Nov5清晨出现多次Mac/64K供给/NOFILE/账单协调，正式11/13仍未关闭，说明“本地能力增长”和“全平台阶段封闭”差异很大。

根工期估计应使用条件场景：乐观建立在14/15短期稳定、Linux/Windows主线不断、已有测试复用；基准需为16–23资源组合和真实npm API长尾留余量；保守包括账单/原生Mac不可用、取消/pipe字节竞态和最终长测返工。无可据以承诺具体日期的稳定吞吐样本。CI优化可减少反馈等待和无关probe工作，但不能将一次Full的十分钟节省线性当作M3–M6工期百分比。

## 立即可执行的最小改造建议

1. 复用ci_tiers现有选择器，diff驱动Fast/受影响平台Cross/Full；未知路径升级、敏感公共ABI及工具链保留Full；manual不得把必需风险降级。原Full完整证据选择保持。
2. scope-gate始终创建，SCM/Job依实际源码和共享基础依赖决定工作job；避免workflow级paths导致required无结果。foundation/tools的重复检查用同一职责选择约束，保留失败日志。
3. Fast使用相关Runtime/SDK真实编译与合同；将无关历史M0 probes、64K deep、全套断网重构留Full。优先减少入口以减少重复工具链深核验，保持身份核验强度。
4. 增量L0复用当前SDK supplier时严格核对当前产品配方/source，不把旧SDK当当前；无匹配supplier只从已验root缓存构建相关当前SDK，不自动fetch/QEMU/整套makecheck。输出local/development，不伪装Cross或发布。
5. 轻量效率报告复用现有JSON/evidence模型，分开run更新时间、实际step时间、可测队列、依赖等待、继承provider、cache与失败分类；无远程改后样本时只交可重复命令+待测字段，不写提升百分比。

本报告之后只对root指定的独立local_check入口实施；原始改造前基准不随CI实现变化重写。

## 本轮修改落地与验证

上述为3d03f855锚点的改造前审计，不回写历史成绩。随后已有14经独立merger以25d3eb12合入根，最终e11a0324开发轴完成，Cross欠项保留；15仍在其分支修复。CI改造保留原Full验收，移除M2普通push特殊提升、按实际diff和共享依赖选择重作业，SDK公开头五native；Fast当前SDK独立，Full offline并行且Core等待其独立原件。scope始终创建，不把未选中范围当产品执行。

本地完整check399项（199pass/200skip）及真实C bootstrap通过。最终热供应all118项/1000生命周期20.659秒，engine40项/1000生命周期14.201秒；普通公开头C编译约0.31秒。当前SDK首次供应209.761秒，单列cold；早期187.640秒的冗余包外链和一次Makefile漂移锁拒绝均保留。与旧CI不同宿主/范围，未计算提速比例。新的同SHA/recipe/contract成功结果复用已对实际all/engine原件验证，0新产品执行，身份不改。精确命令和限制见[本地验证](../../.scratch/qaplet-efficiency/validation.md)。

[执行优化](../plans/qaplet-v1.5-execution-optimization.md)提供前后调度/关键路径对照、条件工期和恢复CI后的同口径复测；[npm路线](../plans/qaplet-npm-compatibility-roadmap.md)使用实际发布包而非过期main摘要。改后远端计时、跨run自动跳过和全部未来产品能力仍无执行证明；账单阻塞期间不触发CI。没有改main、发布、可见性、付款或保护设置。
