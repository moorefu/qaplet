# BOOT-02 · 目标 QuickJS 升级验收

2026-11-02，任务 02 已完成；统一锁保持 `selected_pending_validation`，任务 03 完成
ESM/Promise 实际探针后再晋级。本文只签发精确源码、受控 Ubuntu x64 与 ARM64 64K
eval/对象/指针/错误路径成绩；不签发完整跨宿主矩阵、M1 或 release-ready。

## 精确源码与补丁决定

显式 `quickjs_source.py select --commit` 从官方 Bellard Git 仓库获取
`2833510432045f72915919cef680ba59e5153b16`，未解析 moving master。实际 Git Tree 为
`b2763aa1a9395672adc131322f17d7fad7383053`，VERSION 为 `2026-06-04`。原始完整内容 SHA-256
为 `8afcc7e394aae1ddae380ed63a1ca329d2bbeeb5c9ffdc08b50e0c65e111ac8f`，叠加后为
`562217889e78aa63828113ff112034755d010cd8ec36ad9756eeded91d39138c`。

旧 `e621135…` 原始 source manifest 保存在 `third_party/quickjs/history/`，其源码缓存、
既有 patch、旧锁 Git 历史及 M0 成绩均保留。新旧 VERSION/原始 LICENSE/quickjs.h 字节相同；
变化为 Makefile、quickjs.c、run-test262.c、tests/test_builtin.js。官方对象提取、原始字节和
逐文件差异见[差异及诊断报告](../../out/v15/02/source-diff/report.json)，首次官方精确获取的
命令/原始日志另保留于 `out/worktrees/v15-02/out/quickjs02/selection.json`。

唯一有序 ARM64 full-pointer overlay 无需适配，保留原 SHA-256
`6197ebf127147d917470759f603ff614f6eff68fe581a43f2a5dd37a2be26eff`，patch manifest SHA 为
`dc12be2f49d37dd237e195b9a491be10c68a367c9be1b8c5899222e0692ba616`。从目标官方对象提取
未补丁 header，用固定工具链编译只经过公开 JSValue API 的 bit47 指针检查：
[实际 ARM64 64K 诊断](../../out/v15/02/pointer-red-guest.json) 确定输出
`public JSValue pointer round-trip failed` / exit 3。它不解引用合成指针，直接证明原表示仍截断；
叠加后的实际引擎与对象生命周期在下述同 guest 通过。诊断使用独立制品，不冒充正式 eval。

## 当前候选与缓存边界

最终 v3 锁 SHA-256 为 `f2f3afe2e07c952e357913a0ef7385571c09f035637b6d1eb07ec0bade7e23fb`
（8072 字节）；input_id 为
`2aa67523f14c96169dc15884ca71acd7397345ddb0ee18b0de1d9820a336ebc6`。
source manifest SHA 为 `1458cd1d19f701a579fb4f375c7145eec2fca6c43b03fddf8b6ed4f5b4504f6a`。
固定 cosmocc 原 ZIP、组件、来源关联、libc `unproven` 与独立 helper 源码身份不变。

实际复用主工作区物理 `.cache/`（Ubuntu 为 `/mnt/d/Workspace/dairn/qaplet/.cache`）。
新源码使用 `283351…-dc12be2f49d37dd2/source`，旧 commit 目录未覆盖。显式
`fetch --mirror <已验本地 Git 对象>` 在新的 `.cache/validation/quickjs02-restore-committed`
重新取得 exact commit、检查 pristine inventory、有序应用 overlay，再验证 patched 内容、Tree
与 VERSION；[最终离线物化报告](../../out/v15/02/offline-final-rematerialized.json)保留全部实际 Git
命令，仅使用本地对象，没有远程获取、移动 ref 或重写权威锁。
[共享源缓存复核](../../out/v15/02/source-final-verified.json)与[消费者审计](../../out/v15/02/input-final-audit.json)
也通过。

`engine_compatibility_id` 为
`20225270321a7e33eba3e50724e7e74d88f784dc530ad7bd698c0fee93de7364`，绑定 repository、Commit、
pristine/patched 内容及 ordered patch。build fingerprint 包含此值，对象始终在新的唯一目录编译。
当前尚无持久 bytecode reader/cache；没有旧 bytecode 被读取或复用，也不宣称已测试不存在的 reader。
同 VERSION 不同源码的公开 CLI 红绿合同证明 compatibility identity 改变。

## 实际制品与执行

最终[受控 producer](../../out/v15/02/build-final/build-info.json) ID 为
`18476ca2-9dae-4e84-9e79-a3daa4f90366`，build fingerprint 为
`30ec1c31fc75094f5e7c9710b1f5169699679c27a830eeebd5e1526c3f769eaf`。Ubuntu-22.04 / WSL2
x86_64 / 4096 页实际编译两个 fat APE，完整源码/Nightly 前后验证、派生 Linux 工具视图复核、
双 CPU header/map/LOAD/import 审计及七项本地用例全部通过。

- 新 quickjs-eval.com：2273546 字节，SHA-256
  `f07cadd0d2c1c070d1362b0e90391ea599cfaf9e89813a7b1239c7a3d95c7493`。
- 此次独立构建的 hello.com：754446 字节，SHA-256
  `467f8889db7a671cd238a7cc56df933951fea686c7c524480f4112870da3695d`。
  Hello 不链接引擎，因此与旧 Hello 相同字节；这里重新执行并绑定新 producer，没有沿用旧运行成绩。

使用同批原字节和独立 source-built loader 导出
[最终 manifest](../../out/v15/02/startup-final-manifest.json)，在锁定完整系统 QEMU/TCG guest
实测 Linux/AArch64/65536、kernel `6.8.0-147-generic-64k`。
[实际 guest](../../out/v15/02/startup-final-guest.json)与[既有只读 startup consumer](../../out/v15/02/startup-final-validated.json)
对 Hello/eval facts、42、bit47 pointer roundtrip、20×1000 object/Map 生命周期（9990000）、异常和
语法错误七项均通过。报告保留 QEMU/kernel/config/rootfs、串口、时限、退出、清理及 payload
前后摘要；执行方式为 `emulated-system`，不外推物理 ARM64 或其他页配置。

## 红绿与拒绝

- exact-commit selection、同 VERSION compatibility identity、显式本地 mirror 恢复、candidate
  report 不得覆盖 candidate lock、selected controlled builder 均先记录失败，再最小实现并通过。
  原红绿日志在 `out/worktrees/v15-02/out/quickjs02/`；最终来源合同
  [18 tests](../../out/v15/02/source-contracts.log)通过（1 个 Windows-only skip），builder
  [7 tests](../../out/v15/02/builder-contracts.log)通过（2 个实际重型数据 opt-in skip），
  [精确 target-lock 合同](../../out/v15/02/target-lock-contract.log) 1/1 通过。
- candidate guest export 的原始 loader 身份拒绝保留于
  [首次红报告](../../out/v15/02/startup-export-red.json)。显式受控路径现在先验证 helper 源码、patch
  和 recipe，再比对分发 loader 字节；普通路径不放宽。
  [guest startup contracts](../../out/v15/02/startup-guest-contracts.log) 8 tests 通过（5 个其他数据
  opt-in skips）；实际 candidate 导出及改变 loader 字节的执行前拒绝均已运行。
- [九项 source CLI 实测](../../out/v15/02/refusals-final/summary.json)通过：候选基线验证、普通候选
  拒绝、dirty source、错 HEAD、相同 VERSION 混入旧源码、patch 漂移、缺 LICENSE、错 Tree 和旧
  cache 不得命中新 target。只修改 disposable metadata/source 副本，原源码与唯一原 ZIP 均未删除。
- [普通门禁实测](../../out/v15/02/ordinary-refusals-final.json)按预期拒绝 ordinary make check、普通
  startup consumer 与 controlled matrix。`make check` 在 selected input audit 停止，未宣称完整
  generic suite pass。另一次直接 generic startup 合同被当前 selected gate 阻断（1 error / 1 failure / 4
  skips），原日志[保留](../../out/v15/02/startup-contracts.log)；03 晋级后必须重跑完整适用合同。

最终锁统一为仓库规定的 LF 字节后，重新执行最终 producer、离线源码物化和 guest/consumer。
早先 `build/` 和 worktree `out/quickjs02/build/` 的报告保留自己的原始配方/锁字节范围，不替代
`build-final/`；没有改写旧报告身份。所有新制品、原始日志和报告位于主工作区物理
`D:\Workspace\dairn\qaplet\out\v15\02`，早期 worktree 证据另按原路径保留。

任务 03 仍负责 ESM/Promise 与候选晋级；任务 04 负责新完整跨宿主覆盖；任务 06 的长期原 ZIP
留存仍是独立发布门禁。回退时恢复旧 metadata/统一锁及对应消费者版本，保留两代源码缓存和证据。
