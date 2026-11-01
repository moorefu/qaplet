# 05 · Fast / Cross / Full 验收

2026-11-02，任务 05 resolved。实际执行实现为 `1b2f5393e7e7097e9529f72fc68ee52cd491a3a9`，
统一 pinned input_id 为 `e41f53e2fe3176ce9cdabaeed6a99de2744acfab34669c8db73f9e14f0baf47c`。
本文后的文档提交没有重新签发代码执行成绩。

## 实际远程执行

原 `m0.yml` workflow ID `403214715` 保留；三个 run 均按精确实现 SHA 核验为 completed/success。

| Tier | 实际事件 | Run | 官方结果收据 |
| --- | --- | --- | --- |
| Fast | push | [41346963050](https://github.com/moorefu/qaplet/actions/runs/41346963050) | [run JSON](../../out/v15/ci/run-41346963050-final.json) |
| Cross | workflow_dispatch，tier=cross | [41346967475](https://github.com/moorefu/qaplet/actions/runs/41346967475) | [run JSON](../../out/v15/ci/run-41346967475-final.json) |
| Full | workflow_dispatch，tier=full | [41346974391](https://github.com/moorefu/qaplet/actions/runs/41346974391) | [run JSON](../../out/v15/ci/run-41346974391-final.json) |

Fast 在 Ubuntu 实际校验输入、编译并执行最小 Engine/ESM/Promise。Cross 由同一 Ubuntu producer
分发同字节制品，五个 native 宿主（Linux x64/ARM64、Windows x64、macOS x64/ARM64）的 startup
与 I/O 通过。Full 增加实际断网双构建及复制输入拒绝；Linux ARM64 原生实测 4096 页，仅以锁定
完整系统 QEMU/TCG 补 65536 页，并执行 64K deep、startup matrix 与最终只读 Core consumer。

Full 原始外层 artifact `13099692813` 为 `m0-full-core-1b2f5393e7e7097e9529f72fc68ee52cd491a3a9`，
长度 `64920323`，SHA-256
`a75a795b85f7fb5f39f780f9dcd9c2240fa8d2126ce70708ef44ad0ce77ba615`。
[collection 收据](../../out/v15/ci/full-core-1b-collection.json)已核对原 ZIP、CRC 与解包字节。
[原始 core.json](../../out/v15/ci/full-core-1b/m0-full-core-1b2f5393e7e7097e9529f72fc68ee52cd491a3a9/core.json)
为 `core.status=pass`、errors 为空，选择 12 个原 transport、6 份 startup、6 份 I/O 和 1 份 deep
报告。producer/consumer 的 commit、run、runner/image、原制品 SHA、命令及原始日志由该原消费者
重核；服务 scope 为 not_run，未作为 Core 的前置或成功成绩。

## 本地合同与边界

共享 [tier CLI](../../scripts/ci_tiers.py) 决定 push Fast、PR Cross、敏感路径提升 Full、每日/RC Full
及手工重放；workflow 只调度、获取执行依赖和传输证据。除已知 prose 路径外保守提升，包括新增
Engine/Host/SDK/include、锁、patch、builder/helper 和未知源码位置。

CLI/静态拒绝合同覆盖空 native matrix、缺必需 job、跳过编译/Core、容忍 probe 失败、cache-hit
绕过验证、不可信 SAVE、空 collection、另一 commit 和缺 report。可信 SAVE 还直接限制事件及
默认分支，不能只相信 PR 中 planner 的输出；恢复后无条件执行原 archive/source/tool/helper 验证。
fork PR 无第三方 secret 依赖，固定原件不可取得时失败，不能切 rolling Nightly。
这些是策略/合同成绩：本轮没有真实 PR/fork PR、cache hit、默认分支 SAVE、nightly 或 RC tag run，
手动 Cross/Full 只证明对应 tier 的实际执行，不冒称已实测这些自动事件。

[八项本地重放](../../out/v15/05/replays/summary.json)包括真实 Git diff 的敏感 PR 选择，均明确
`evidence_verified=false`。早期合并后的普通 Ubuntu `dev.py check` 为 210 tests、69 个显式数据/
平台 skips 与真实 C bootstrap 通过；[日志/入口](../../out/v15/05/check-final-workflow.json)不替代
上述实际远程 run。

[本地实际 seal](../../out/v15/05/actual-seal-summary.json)在 b0 实现上验证 current-input 的完整
offline pair，原报告 SHA `b4f2ef994e9307342914b65cc912c286f2be934cfa52ec874bb5d297a0bb0863`、
长度 `208456` 保持不变，四份 helper 原字节/路径映射一致；既有
[recipe preflight](../../out/v15/05/actual-seal-preflight.json)为 identical，24 项 staged raw inputs
一致。该步骤只准备 transport；最终 Full 实际 seal/Core 则由上表 1b run 独立执行。

## 保留的失败与修复

- a184 实现的 [Cross 41343504925](https://github.com/moorefu/qaplet/actions/runs/41343504925)
  startup 五宿主 17/17 及 matrix 通过，但非 Windows I/O 的共享 loader 访问缺失
  `args.controlled_probe`，不能签发 Cross 通过。04 的 `8b3f556` 对未提供该选项的 caller 保持
  普通 pinned 模式，使用原 CI 字节复现后通过实际 I/O 回归；未改变 APE builder recipe。
- 同一 a184 的 [Full 41343511950](https://github.com/moorefu/qaplet/actions/runs/41343511950)
  实际 offline pair 通过，随后 root-owned `out/m0/offline` 阻止非 root seal 原子写 receipt。
  `f9a1b9a` 改为 runner 在 sudo 前创建该父目录，不递归更改仓库或 cache 权限；实际 POSIX
  namespace producer / 非 root receipt writer 的
  [red](../../out/v15/05/ownership-red-root-controller.log) / [green](../../out/v15/05/ownership-green.log)
  复现了权限边界。该旧 Full 保持失败，原证据在 [collection](../../out/v15/ci/full-a184-collection.json)。
- b0 的 [Fast 41344912879](https://github.com/moorefu/qaplet/actions/runs/41344912879) 与
  [Cross 41344937038](https://github.com/moorefu/qaplet/actions/runs/41344937038) 通过；
  [Full 41344951913](https://github.com/moorefu/qaplet/actions/runs/41344951913) 在固定 guest
  `noble-updates/InRelease` 获取时收到 HTTP 503，未获得所需 guest/Core 成绩。
  [失败 acquisition](../../out/v15/ci/guest-fetch-b0/m0-page-gap-65536-b0a54f8e6e3d8604bcdfab61787625d7a90587e5/acquisition.json)
  与 curl 原日志保留。04 的 `5dc16ca` 只对显式 fetch 的瞬时 HTTP 错误作至多三次同 URL 重试，
  共用原 190 秒预算，继续严格核对锁定签名/摘要；新 1b Full 才取得完整通过。

Node compatibility（M3）、runtime soak（M2）、network TLS security（M4）保持 not_implemented；
长期原 ZIP 镜像为 missing_resource / release 06。必需已实现输入缺失或失败继续阻断所选门禁；
未实现项不生成空绿灯。06 仍 needs-info、用户暂缓、`release_ready=false`，本票没有长期存储上传。
