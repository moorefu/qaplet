# LOCK-01 统一输入锁验收

**Status:** resolved
**Branch:** codex/m0-retro-workflow
**Base:** 8b1787e7d1921581f423a5c604957ea37a102698
**Reviewed implementation:** 8d714ee05e9889de678a6de770a09bc1827d1bee

## 实现与迁移映射

唯一入口为 `toolchains/qaplet.lock.json` / `qaplet.toolchain-lock.v3`。四份旧锁、原 acquisition
receipt、license 与有序 patch 的原始字节绑定到新入口；字段、摘要算法、状态、缓存兼容路径
及完整入口覆盖表见[统一锁操作合同](../../docs/development/input-lock.md)。

本任务保持既有 QuickJS `e621135dc542e000e31038bedc52944aa0adaf1a`、Git Tree
`cdabddcf0f783af024481272227c1f56c3d3e48a`、VERSION `2026-06-04` 及 ARM64 overlay。
cosmocc 原件仍为 Run `41169443573` / Artifact `13031425078`，419652152 字节，SHA-256
`f2f984d3a9790674fc0da11c1c00a32d4fe3f8dafde99af74b0c6dc24a2a16cf`。原件、组件清单、
源码/patch、独立 helper 与配方身份分别记录，libc source 仍为 unproven，无独立签名 attestation。

基线到实现提交之间，旧四锁、QuickJS/helper patch、选型样例没有变化；未知的目标源码字段仍未填造。
`2833510432045f72915919cef680ba59e5153b16`、ESM/Promise、新跨宿主组合及分级 CI 仍属于任务 02–05。
原 ZIP 长期镜像保持独立 pending 发布门禁，不阻断已验缓存开发，不签发 release-ready。

## 已执行验证

执行环境为本机 Ubuntu-22.04 / WSL2 x86_64 / 4096 页，UID/GID 1000；物理主缓存为
`/mnt/d/Workspace/dairn/qaplet/.cache`。受限 Windows 进程首次返回 WSL distro 不可见后，按环境
文档在普通用户上下文进入现有发行版；没有安装新发行版、移动缓存或改变挂载/binfmt 配置。

- 最终代码 `python3 scripts/dev.py check --output-dir out/lock01/check-consumers` 通过：183 tests、
  0 failures / 0 errors、51 项既有平台或显式数据 opt-in skips，静态 CI 与真实系统 C bootstrap
  通过。此普通 profile 不代表完整 Core/OS 矩阵。完整命令及日志在 `out/lock01/check-consumers.json`
  和对应目录的 `command-0.{stdout,stderr}.log`。
- 统一锁专用 CLI 合同 16/16 通过；覆盖真实消费者旧锁冲突、错 schema/字段/清单/helper、候选限制、
  fixture 生产路径保护、候选发布/重复拒绝、畸形 JSON、供应异常、缺 VERSION，以及报告与原件/
  显式仓库外缓存碰撞保留、历史 producer 拒绝和派生 helper 来源遗漏。
- `out/lock01/migration-provenance/replay.json`：在实际 DrvFS 上重放公共 `migrate`，源 Commit/Tree/VERSION
  来自实际缓存和 Git 命令；新候选以 staging + 不覆盖的 hard link 原子发布，selected 普通消费
  退出 1。候选字节保存到同目录 `candidate.lock.json`，原五份 metadata 前后身份一致。
- `out/lock01/source-refusals-provenance/summary.json`：六项实际 source CLI 重放。已验基线及 selected 的显式
  controlled-probe 通过；错 Commit、实际错 Tree、错 patchset、selected 普通消费均失败。
  仅使用临时 metadata 副本，真实主源码缓存和原锁未修改，命令/退出码/原始日志及逐项报告保留。
- `out/lock01/ape-helper-verify.json`：实际 helper 源码/patch 完整验证通过。
- `out/lock01/helper-bundle-final/helpers.json`：在 Ubuntu 实际生成四种分发 helper，前后完整核验
  已缓存 Zig 原归档/树，导入当前 producer 的独立 Linux ARM64 loader 并交叉构建 Mac ARM64
  Mach-O。`startup-final-bundle.json` 对这个完整 bundle 执行七项真实 Linux x86_64 启动/eval
  用例并全部通过；不签发新的 Mac/ARM64 原生运行成绩。
- 显式设置 `QAPLET_STARTUP_BUILD` / `QAPLET_STARTUP_LOADERS` 后，实际 startup 合同 6 项中
  5 项通过，现有 `/dev/shm` 未提供 noexec 挂载的 1 项明确 skip；未修改挂载。首次验证前分发
  loader 目录尚未填充导致 blocked，补齐原 ZIP 指定 loader 的同 SHA 副本后重放通过。相关原始
  报告保留在 `out/qaplet-native-startup-*` 和 `out/startup-permission-contracts/`。
- `out/lock01/build-provenance/build-info.json`：最终 v3 身份的普通 Ubuntu 构建、两 CPU slice
  审计及本地七项 Hello/eval 用例全部通过；input_id 为
  `92c118e7d32f74b0f6d79a4c9bb71651f13f3bc4bc448f02c765917f6d36783f`，与实际 helper bundle
  和两次断网 producer 一致。独立 `offline compare` 也拒绝缺失、冲突或 selected 身份；其
  合同用字节 fixture 证明实际比较和拒绝行为，不冒称 fixture 是产品构建。
- 早期普通 Ubuntu APE 构建 `out/lock01/build/build-info.json` 已通过，两种 CPU slice 审计和本地
  Hello/eval、指针/对象及异常/语法错误用例均执行。该运行的锁为此前的 parser 配方身份，保留其
  原始 scope；不能替代最终代码的断网构建。`reject-prior-input.json` 已实际证明新 consumer 在
  执行前拒绝旧配方 producer，commands 为空。

## 断网验证

最终独立审查完成后，对固定代码执行原有 namespace/降权、缓存前后 inventory、两次独立真实
构建、六个 binary 与 loader/packer 字节比较、strace 网络审计及复制损坏拒绝合同。初始清点为
21,180 个 regular files / 23,034 个条目 / 2,815,491,333 字节；后置逐字节/类型/模式/链接清点
与初始完全一致。`out/lock01/offline-accepted.json` 和其 `offline-integrity.json` 均 pass：16 项
必需 case 全部通过、六个 binary 与两种 helper 比较均通过、16 份 trace 无隐式 Internet 调用，
`cache_inventory.unchanged=true`，无必需 skip。两次实际 producer 的七项本地用例各自 pass。
该 profile 不签发新的六宿主/ARM64 实际运行或发布就绪成绩。此前两次
长 inventory 因复审需要更改配方而显式停止，其失败/未完成日志保持在
`out/lock01/offline-final/`、`out/lock01/offline-reviewed/`，不计为通过。

## Standards

初审无发现。第二次独立审查提出一项 P3 Duplicated Code 判断：迁移与验证重复构造相同组件
选择映射。`ea0cde0` 提取共享纯映射函数，保留实际 source/cache 检查与供应清单验证。
第三次审查的候选消费策略重复由 `a08298d` 集中为共享 purpose 解析。第四次审查的外部非标准锁
目录报告覆盖，由 `86a8103` 统一物理根目录判定并拒绝非 `toolchains/` 锁。第五次审查的 Core
helper 重复/不完整验证，由 `8d714ee` 调用共享来源验证，保留完整文件库存和实际证据检查。

最终独立报告：硬规则无发现；Fowler heuristic 无发现。固定 `8b1787e…8d714ee` 六个提交，
仅使用 `out/lock01/reviews-v6/standards`，未读取工作区实现、另一轴或聚合结论，未修改文件或
执行构建。结论为静态审查。

## Spec

初审 P1：report/output 与原锁别名可覆盖原件；P2：非对象 JSON / SourceError 可留下旧 pass。
`c383849` 增加输出隔离、no-clobber 原子发布、对象类型检查和供应异常处理；临时副本公开 CLI
红绿回归覆盖原件保留。

第二次独立审查 P1：显式仓库外 source-cache 未受报告路径保护；P2：缺 VERSION 抛出未规范化的
StopIteration，供应 CLI 留下旧 pass。`ea0cde0` 保护实际显式缓存根，缺条目统一转 ValueError；
外部缓存及源码/Nightly/APE 三个真实 CLI 红绿回归通过。

第三次审查的 helper 上游/分发身份遗漏，由 `a08298d` 在使用前验证输入及独立源/patch。第四次
审查的历史 producer 重标和 helper `source` 删除绕过，由 `86a8103` 强制当前身份并按实际
helper 类别核对来源/结果/配方。第五次审查的独立 `offline compare` 迁移遗漏，由 `8d714ee`
统一验证两个 producer 的当前身份；对应公开 CLI 红绿回归通过。

最终独立报告：未发现可确认的需求缺失/部分实现、未要求行为/范围膨胀或实现错误。固定
`8b1787e…8d714ee` 六个提交，`out/lock01/reviews-v6/spec` 的44项材料摘要和长度一致，仅使用
本轴材料，未改文件。结论为静态审查，不替代实际 Ubuntu 构建与断网验收。

最终发现数：Standards 0（无最严重项）；Spec 0（无最严重项）。
