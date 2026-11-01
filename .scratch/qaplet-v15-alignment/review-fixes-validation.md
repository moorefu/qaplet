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
matrix/Core 与独立严格回归另在下文记录；本地没有重复执行 full offline pair。
06 原 ZIP 仍保留用户现有本地存储，没有执行长期镜像上传。本地未执行服务操作，SCM/Job
publication 只构建；独立远程 SCM 只作为状态指针，不计为 Core 或 release-ready。

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

## 新输入的远程验收

修正提交 `7410aaf9c7d78b9a7f7301d8d541fa8ca047d7e5` 合并为
`b3adfb801a967a916c7e68dd1a7f79d4f497e172` 后，精确同一提交的
[Fast 41353595164](https://github.com/moorefu/qaplet/actions/runs/41353595164)、
[Cross 41353601315](https://github.com/moorefu/qaplet/actions/runs/41353601315) 与
[Full 41353612487](https://github.com/moorefu/qaplet/actions/runs/41353612487) 均 completed/success。
官方响应原件分别保留为 `out/v15/ci/run-<id>-final.json`。Fast 为 branch push，Cross/Full
为显式 workflow_dispatch；不将这几次执行外推为未实测的事件触发器。Full 的实际 64K guest、
startup matrix、隔离断网双构建及完整 Core 均通过。

Full 外层原始 Artifact `13102651716` 为 64924679 字节，SHA-256
`c84f9f2ac3b4404386d781907aa50303be01937f6e7e44d395a70377293f05d9`。
[归档收据](../../out/v15/ci/full-core-b3-collection.json)验证原 ZIP、CRC 与解包字节；
外层中的 12 份原始 transport ZIP、provider ID/commit 名称、原报告与日志均保留。
[原 Full Core](../../out/v15/ci/full-core-b3/m0-full-core-b3adfb801a967a916c7e68dd1a7f79d4f497e172/core.json)
与[新 Full 的本地只读重放](../../out/v15/ci/core-b3-local-replay.json)均为 pass：
input_id=f0b8dbcf…、六目标、235 项 case index、errors=[]。完整选择为
[新 b3 manifest](../../out/v15/ci/full-core-b3-local-manifest.json)，不改写报告或模拟 CI 字段。
新六目标/64K/offline 由这次实际远程执行提供，不继承旧 e41/1b 成绩。

实施分支已同步 b3；[提交字节检查](../../out/v15/review-fixes/committed-byte-verification-b3adfb8.json)
逐项核对 12 个冻结 recipe 与锁的工作区/Git 原字节、长度与摘要，均为 LF。下列本地补充与文档
跟进没有再更改 source/recipe，不重新签发不同实现的执行成绩。

## 独立本地 4K 与 publication

[本地阶段收据](../../out/v15/review-fixes/local/completion.json)绑定五个本地 producer、loader、
Linux I/O、真实 4K guest 及导入结果。五个 producer 均属于新 pinned f0 输入、CI=null；
[publication selection](../../out/v15/review-fixes/publication/inputs.json)分别选择新的普通 Hello/eval、
native I/O、pages、SCM 与 Job。后两类 capability=not_run，只生成独立分发制品；
[publication 合同](../../out/v15/review-fixes/publication-contracts.stderr.log)为 3/3，通过 notices、
HACL 原文、两 CPU map 与 loader 摘要核对，没有本地服务操作。

新 loader bundle 完整重核固定 Zig/原 Nightly，使用当前 producer 的独立 ARM64 helper；
Linux x64 I/O 实际执行通过。新锁定完整系统 QEMU/TCG [4096 guest](../../out/v15/review-fixes/local/guest-4096/guest.json)
实际执行 18 条入口，startup 导入 17/17，I/O 导入 20/20，产品/helper 前后 SHA 不变；
[Core guest 验证](../../out/v15/review-fixes/local/guest-4096/core-validated-complete-selection.json)
为 pass、scope=guest-transport、core=not_run。[局部选择](../../out/v15/review-fixes/local/guest-inputs.json)
不提供 Full offline 或六目标成绩，完整 Core 仍选择远程 b3。

一次性外部编排最初的局部 manifest 缺少必需 schema 键，原
`core-validated.json` 与 `guest-inputs-incomplete-shape.json` 保留；补齐选择结构后只读验证通过，
产品 producer、guest、原日志与配方没有改写。本地阶段收据的 pending 是当时的阶段状态，
最终新输入验收由本节远程结果与完整严格回归共同记录。

## 新组合的严格闭环

[最终验收收据](../../out/v15/review-fixes/acceptance.json)以精确 b3/f0 身份绑定官方 run 响应、
原 Full Core、本地只读重放、原始 ZIP collection、独立 SCM 指针、本地阶段与严格回归结果。
独立 SCM 的 [b3 原报告](../../out/v15/ci/full-core-b3/svc-scm-windows-b3adfb801a967a916c7e68dd1a7f79d4f497e172/scm.json)
来自 run `41353595123`，status=pass，仅作为 Core/SVC 相互独立的状态指针，不改变 Core 判定。

[稳定原始路径选择](../../out/v15/review-fixes/strict/selection.json)将 b3 的完整 Full/SCM 与独立
本地 4K、五个新 publication producer 分开；没有改写报告路径或 CI。仓库
[预检](../../out/v15/review-fixes/strict/dev-preflight.json)、
[原数据严格预检](../../out/v15/review-fixes/strict/preflight.json)与
[暂存数据严格预检](../../out/v15/review-fixes/strict/staged-preflight.json)均为 pass。
[最终严格结果](../../out/v15/review-fixes/strict/result.json)为 expected=34、run=34、
failures=0、errors=0、skipped=[]；
[完整 regression log](../../out/v15/review-fixes/strict/evidence/79745c67814544debee79e36188f6e1e/regressions.log)
保留 Core、4K、publication、recipe 的全部必需用例。现有严格入口自动纳入新增的 Core CLI
身份合同；显式的当前本地 producer 由
`out/v15/review-fixes/test-environment.json` 选择，因此新身份测试实际执行，没有 opt-in skip。

[一次性 staging](../../out/v15/review-fixes/strict/staging.json)逐文件核对 1243 个新 b3 bundle 文件的
长度/SHA，保留原 ZIP/provider 元数据，仅修改外部选择与路径映射。
[执行选择](../../out/v15/review-fixes/strict/selection-staged.json)指向 POSIX `/tmp` 测试副本；
stage、预检与完整回归在同一次 Ubuntu session 中运行，可由
[stage_and_run.py](../../out/v15/review-fixes/strict/stage_and_run.py)重放。
[结束远程资料检查](../../out/v15/review-fixes/strict/input-integrity-after.json)确认 1243 个原件及
暂存副本、独立 SCM 状态指针未变；
[结束本地资料检查](../../out/v15/review-fixes/strict/local-input-integrity-after.json)确认 538 个本地
producer/guest/publication/选择文件及 HACL notice 源码未变。可复用缓存没有迁移或替换。

旧 e41/1b 的 33/33 继续保留其原组合；这里新增的 34/34 仅证明本次 b3/f0 与明确选择的本地输入。
正式 M1/SDK 仍未实现，06 长期原 ZIP 镜像仍由用户暂缓，release_ready=false。
