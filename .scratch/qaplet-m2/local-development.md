# M2 当前执行方式：本地 Linux / Windows 优先

2026-11-05（Asia/Shanghai），用户明确指示：“我们先在本地开发，主要推进Linux跟Windows”。
本记录调整当前开发顺序；原规格的产品合同、平台支持声明及最终验收要求保留。

## 当前阶段的开发依赖

Ubuntu继续统一构建产品和SDK，Linux与Windows消费同一份供应字节。每票完成公开
边界红绿、本地正负验收与独立合并后，记 `Local development: complete`。
该标记满足本阶段后续票据的开发前置；跨平台证据尚缺时保留 `Status: claimed` 和
`cross_deferred`，不冒称该票已完成全范围验收。原Blocked by边不删除。

11、13的实现及Linux/Windows供应验证已经独立合并，可据此推进12、14、15。
11/13仍保留ARM Mac、完整CI/Full等未验收项。本地新增实际64K SDK已170/170通过，
但其 `ci=null`，与CI分开记录；当前原件在主
`out/qaplet-m2/local64k-nofile-034/HANDOFF.md`。

## 本地实施要求

- 按既有implement-spec/TDD流程，每票fresh实现者、独立worktree、公开产品测试；
  测试边界沿用已确认Q49/Q53，无须重新访谈。
- 每个资源首次接入即实施有限预算、stop/close/quiesce安全及物理保留，不能延后到19/20。
- 交接前融合实际integration tip，再由独立merger合并；不因共享文件虚构前置。
- 保存真实SDK/APE/loader/ZIP/配方、命令、日志、宿主和清理事实；未执行项保持可见。
- 当前不push、不触发CI，不改变GitHub付款、预算、仓库可见性或runner配置。

## 延后但未取消的验收

跨平台和CI缺口集中保留到24–27及最终矩阵收口。实际64K可在本地继续补充，但不能
替代缺失的原生Mac或CI身份。Linux/Windows的混合生命周期和1小时长测仍需实际运行。
最终全范围resolved、父M2完成、整体Review与release-ready均不得提前签发。

该执行方式优先于各票此前“等当前Full后才放行下游”的调度记录；新本地证据与历史
失败原件分别保存，不回写旧报告。

## 2026-11-05：剩余开发前置与验证范围

12、14、15已经独立合入实际集成分支，合并点分别为 `3d03f855`、`25d3eb12`、
`65de84e2`；它们是当前集成点的祖先。11–15的本地完成可放行开发，正式状态仍为
claimed/cross_deferred。16与17的真实开发前置均已满足，可并行实施；17不依赖16。

随后17已独立合入 `caa439ae`，接收收据 `5bc82e1c` 重核源码、供应4和全部本地原件。
18据此领取实施；16随后也在组合供应验收后独立合入 `015cdb9f`，接收收据为
`71a5db50`。两票正式Cross/Full仍为deferred，原件和收据在主
`out/qaplet-m2/merge-17-stdio/`、`out/qaplet-m2/merge-16-js-process/`，本地合入不改变正式验收。

| 票号 | 当前开发前置 | 本票本地验证范围 |
| --- | --- | --- |
| 16 | 本地完成并独立合并`015cdb9f`；正式Cross/Full待验 | JS显式spawn、三种stdio、退出/reap与EOF分离、输出保留、双通道/部分写/BUSY/背压、ref/unref/GC、取消及stop/close；轮转与Promise回归 |
| 17 | 本地完成并独立合并`caa439ae`；正式Cross/Full待验 | 真实重定向pipe/文件/无效句柄分类，默认不接管、借用不关原句柄/不改共享flags，安全拒绝；JS/C字节、EOF、取消、预算及物理保留 |
| 18 | 本地完成并独立合并`3e85a272`，接收`64624ddd`；正式Cross/Full待验 | 真实PTY/Console跨Runtime别名独占、显式双向join、完整模式恢复/readback、Host resize桥、严格UTF-8/已验VT、物理join与有限预算；[本票记录](18-tty-validation.md) |
| 19 | 本地完成并独立合入`597020eb`，接收`2b3035f7`；正式Cross/Full待验 | timer/work/process/stdio/TTY组合额度、保留字节、POD跨线程快照、拒绝与终止首因；硬限额及稳定错误 |
| 20 | 本地完成并独立合入`943c1962`，接收`776e50cb`；正式Cross/Full待验 | 混合资源stop/close/quiesce/destroy，取消竞争、OS未退休记录、唯一reap、首因及用户JS屏障；宿主通知安全 |
| 21 | 本地完成并独立合入`13abf9ac`，接收`fe8a17d`；正式Cross/Full待验 | 最小CLI异步、入口副本、真实标准路由/字节与0/1/2，真实overlapped Host输出及未知完成保留；保留固定CRT启动限制，见[21记录](21-cli-validation.md) |
| 22 | 本地完成待独立接收；21独立前置已满足 | 实际 SIGINT/SIGTERM/Ctrl+C、SDK/relay累计5s、最终Host输出、迟到入口保留、真实TTY/child回收；[本地验证](22-cli-signal-validation.md)，外层timeout仍记失败 |
| 23 | 19本地完成并独立合入；仍等待22本地完成并独立合并 | Ubuntu完整SDK分发、包外纯C Host、逐目标archive/头文件/依赖、消费同一供应字节及生命周期示例 |
| 24 | 等待23本地完成并独立合并 | 五原生完整合同及每目标至少1000生命周期、100混合异步；缺失宿主保留not_run |
| 25 | 等待23本地完成并独立合并 | 实际ARM64 64K的新产品完整合同及至少1000生命周期、100混合异步；旧制品成绩不替代 |
| 26 | 等待23本地完成并独立合并 | Linux/Windows各10000混合生命周期及1小时交替长测，保存真实时长、资源与清理事实 |
| 27 | 等待24、25、26实际全范围验收 | 严格原件、身份、失败/缺项及最终矩阵判定；本地完成标记不能替代正式前置 |

所有产品和SDK继续只在Ubuntu构建，Linux与Windows使用同一供应制品。本票新增行为按
已确认公开边界逐个红绿，并复验受影响回归；日常快速检查选相关profile，只有变更范围
或失败需要时扩大检查。真实OS事实、受控故障、正常产品分别记录，工具测试不冒充产品成绩。
共享实现接口只按必要范围协调，不添加虚构依赖，不重复执行无变化的成功长测。

18已冻结供应3产品`958b3e2`，其全部TTY Linux13/Windows20、相关IO/stdio各27和包外各24
通过；供应2原完整normal209/208+optional1、package94/94、fault24/25保留原身份，
供应3未重跑这些完整catalog。18现已独立合入`3e85a272`，接收`64624ddd`重核21scope/
2536raw及合后Ubuntu preflight；19/20当前开发前置已满足并已领取，未提前完成本地验收。
正式跨平台/实际64K/CI/Full与组合长测不由本地selected回归替代，详本票验证记录与原件。

19现已独立合入`597020eb`，接收`2b3035f7`核验当前normal214/213+optional1、package99/99、
fault25/26、TTY13/20及额外环境/终端组合，保留2430raw、guard范围及原失败；合后Ubuntu
preflight通过。20正在融合这份真实产品tip并验证自己的最终集成供件，本地尚未独立接收。
23的19开发前置已满足，22尚未开始；不因共享文件新增20对19的阻塞边。

20现已独立合入`943c1962`、接收`776e50cb`。最终SKU3两端mixed各12、相关回归各28、
首因两序各2通过，1852raw/20scope及合后Ubuntu preflight独立重核；四个保留负向/端
继续标native_cleanup_complete=false。旧SKU1/2原范围保留，不改贴成当前完整normal。
21据此领取，正式跨平台/Full/64K/长测与父M2待验边界不变。

21现已独立合入`13abf9ac`、接收`fe8a17d`。当前供应6 CLI Linux67/Windows72，
100recipe/91ZIP、1871raw/8报告独立合前合后重核；供应4完整normal/TTY/mixed保留原身份，
与6公开头及双库同字节。九个清理未完负向、503→511启动限制和原dirty不改贴为全部完成。
合后Ubuntu preflight通过；22据此领取，23仍待22本地完成及独立合入。正式欠项不变。

以上只校正开发调度与验证范围，不删除原Blocked by边、不修改任何Acceptance criteria，
不降低父规格的产品/安全/ABI/默认额度、五原生、实际64K、Full及最终独立审查合同。

2026-11-06：22 产品冻结 `1343e6c`，从实际 integration `97cc8f1` 完成本地实现。
当前供应普通 CLI Linux67/Windows72、本票14/8通过；109输入/95ZIP、实际callback ABI、
真实OS投递、唯一native reap、两段累计预算、未知/迟到入口保留和原失败均保存于本票
HANDOFF。状态仍 claimed/local_complete/cross_deferred；独立接收前不放行23，
正式五原生/当前64K/CI/Full/长测与最终Review不由本地成绩替代。

2026-11-05（19本地完成待独立接收）：19产品冻结1c4a91c，当前供应normal Linux214/
Windows213+optional1、包外99/99、独立fault25/26及实际环境/TTY组合已验证。完整当前
TTY/工具/原件与失败边界见[19验证记录](19-aggregate-validation.md)及worktree HANDOFF。
19保持claimed/local_complete/cross_deferred，独立合入前不放行依赖其开发的23；与20
无新增依赖。父spec/原边/AC、其他原生/当前64K/CI/Full/组合长测/整体Review保持待验。
