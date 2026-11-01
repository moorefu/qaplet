# v1.5 审查修正与新配方身份

2026-11-02，三条 P3 审查意见在同一变更中处理。CI identity 的九个必需字段与精确小写 Git SHA
由纯 `ci_identity.validate_ci_identity` 共享验证；startup 保留 producer/consumer commit 关联，
Core 保留自己的 verified transport、provider artifact/commit 与实测 host 检查。源码内部入口改名为
`create_source_lock`，诊断同时覆盖 exact selection 与 fixture bump；公开 `select`/`bump` CLI
兼容，moving master 仍仅允许历史 fixture。文档入口、计划当前摘要和开发入口明确 04/05 已完成
各自所记范围、06 由用户暂缓、正式 M1/SDK 尚未实现。

## 配方变化与原始证据

这次内部命名调整触及冻结的 `scripts/quickjs_source.py`，不能沿用原输入身份。
[迁移](../../out/v15/review-fixes/migration.json)与[逐项比较](../../out/v15/review-fixes/recipe-delta.json)
确认唯一 recipe 变化为该脚本：20870 字节，SHA-256
`c69eb098ec5c4c276cd2df2f063212902fa22aa07607da4c7d7579f8480ff1e1`。
selection、组件清单、metadata、policy 和 archive_retention 均未改变；原始与 patched QuickJS、
ordered ARM64 overlay、cosmocc 原 ZIP、独立 helper/source 与现有物理缓存均保留。
精确引擎仍为 `2833510432045f72915919cef680ba59e5153b16`，engine_compatibility_id 仍为
`20225270321a7e33eba3e50724e7e74d88f784dc530ad7bd698c0fee93de7364`。

新 input_id 为 `f0b8dbcf6e44b58248e45bd755bbce5414b8715d3895caf5318efb31b48e5ead`。
[候选锁](../../out/v15/review-fixes/candidate.lock.json)为 selected、8072 字节、SHA-256
`00d2dce9350fdd1a5369a6e2285ab554f70e7db063653871b4400dfe895c7cf6`；
[晋级收据](../../out/v15/review-fixes/promotion.json)绑定实际受控构建和独立 startup 的原字节。
[pinned 锁](../../out/v15/review-fixes/pinned.lock.json)为 8051 字节、SHA-256
`861ae65bbbb87743091103e1e5707dfe2feb422fa9b35ca9652ddc80610e2c0e`；state 变化不改变 input_id。
所有锁和 recipe 原字节均使用 LF，未重新摘要或重标旧报告。

旧 input_id `e41f53e2fe3176ce9cdabaeed6a99de2744acfab34669c8db73f9e14f0baf47c` 与
`1b2f5393e7e7097e9529f72fc68ee52cd491a3a9` 的 Fast/Cross/Full、完整 Core/offline 和严格回归
继续仅证明原记录的组合，见 [04](cross-host-revalidation.md)与 [05](tiered-ci-validation.md)。
这些真实旧成绩没有变为本次新 recipe 的执行证明。新输入的远程 Fast/Cross/Full、64K、offline、
matrix/Core 与最后严格回归仍待合并后的精确提交验收；本地没有重复执行 full offline pair。
06 原 ZIP 仍只在用户现有本地存储，未上传原件，也未进行服务操作或声明 release-ready。

## 本地实际验证

测试 seam 沿用用户已确认的外部 CLI、真实制品与现有 evidence consumer，没有新增私有函数镜像测试。
[源码红测试](../../out/v15/review-fixes/source-red.stderr.log)先证明 exact selection 缺 Git 时旧诊断
误称 bump；[绿测试](../../out/v15/review-fixes/source-green.stderr.log)通过 19 tests、1 个明确平台 skip，
保留 exact commit/master 已移动、fixture legacy bump、离线获取、旧记录与失败不覆盖等合同。

[受控 producer](../../out/v15/review-fixes/candidate/build-info.json)和
[独立受控 startup](../../out/v15/review-fixes/candidate-startup.json)均真实执行 17/17。
Ubuntu-22.04 原生 x86_64、WSL2 kernel `6.6.87.2-microsoft-standard-WSL2`、实测 4096 页；
普通用户上下文使用 `C:\Program Files\WSL\wsl.exe`。受限进程看不到现有发行版的观测仅限该上下文，
没有安装发行版、更改 WSL/mount/编译器或缓存布局。所有构建输入与中间产物仍位于主工作区物理
`.cache/`，报告与交付物位于 `out/v15/review-fixes/`。

[普通 Ubuntu `dev.py check`](../../out/v15/review-fixes/preliminary-check-workflow.json)通过：
218 tests、0 failures/errors、75 个明确平台/数据 opt-in skips，实际系统 C bootstrap 通过，
`m0_core_status=not_run`。[完整日志](../../out/v15/review-fixes/preliminary-check/command-0.stderr.log)
保留所有执行和 skip；这次通用检查未选择旧产品报告迁就当前输入。

[晋级后重新构建的普通 producer](../../out/v15/review-fixes/pinned-workflow.json)与
[普通 startup](../../out/v15/review-fixes/pinned-startup.json)各为 17/17、`controlled_probe=false`，
CI=null，没有把候选报告重标为 pinned。两类制品原 SHA 与旧组合相同，但这里保留本次实际重建与执行。
[跨宿主身份合同](../../out/v15/review-fixes/cross_host_identity-contracts.stderr.log)为 7/7；真实新报告的
拒绝变异覆盖九个字段分别缺失、非字符串/大写/非完整 SHA、commit 漂移和 producer 关联。
[Core CLI 合同](../../out/v15/review-fixes/core_gate-contracts-corrected-fixture.stderr.log)为 21 tests、
0 failures/errors、19 个明确 full-matrix opt-in skips；新身份拒绝及有效 CI 仍须 verified archive
的两项实际 CLI 测试已执行。首次夹具缺少 manifest 必需键，四个子用例在身份验证前被拒绝；
[原日志](../../out/v15/review-fixes/core_gate-contracts.stderr.log)保留，补齐夹具结构后重跑通过。
未用旧 full-matrix 报告冒充新输入的完整成绩。

本地 4K/publication 的最终结果将在各自原报告完成后补充；这些后续验证不再更改被冻结的配方。
