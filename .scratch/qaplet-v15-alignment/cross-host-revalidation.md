# BOOT-04B/06/07 · 新输入跨宿主回归验收

2026-11-02，任务 04 resolved。实际远程实现为 `1b2f5393e7e7097e9529f72fc68ee52cd491a3a9`，统一 pinned input_id 为 `e41f53e2fe3176ce9cdabaeed6a99de2744acfab34669c8db73f9e14f0baf47c`；目标 QuickJS/ordered overlay 与 03 的 ESM/jobs recipe 继续由该锁绑定。新六目标 Core 和本地严格回归均通过。本票提供后续 M1 Cross/Host/SDK OS ABI 验收的基础证据；正式 M1/SDK 实现和 release-ready 不在本票结论中，06 仍 needs-info、用户暂缓。

## 实际远程同字节覆盖

同一实现的 [Fast 41346963050](https://github.com/moorefu/qaplet/actions/runs/41346963050)、[Cross 41346967475](https://github.com/moorefu/qaplet/actions/runs/41346967475) 与 [Full 41346974391](https://github.com/moorefu/qaplet/actions/runs/41346974391) 均 completed/success。各 producer 在 Ubuntu 生成自己的交付物；宿主/guest 消费各 family 的原字节，独立 helper 保留各自来源与摘要。

Full 原始外层 artifact `13099692813`，长度 `64920323`、SHA-256 `a75a795b85f7fb5f39f780f9dcd9c2240fa8d2126ce70708ef44ad0ce77ba615`；[collection 收据](../../out/v15/ci/full-core-1b-collection.json)验证原 ZIP、CRC 与解包字节。外层内的 12 个原始 transport ZIP、provider ID/commit 名称及原报告均保留。[原 Core 报告](../../out/v15/ci/full-core-1b/m0-full-core-1b2f5393e7e7097e9529f72fc68ee52cd491a3a9/core.json)与[本地只读重放](../../out/v15/ci/core-local-replay-mapped.json)均为 pass：六份 startup、六份 I/O、一份 deep 和实际 offline proof，共 235 项 case index，errors 为空。

| 实际目标 | 执行方式 | 页大小 | kernel |
| --- | --- | --- | --- |
| Linux x64 | native | 4096 | `7.0.0-1017-azure` |
| Windows x64 | native | 4096 | `10.0.20348` |
| macOS x64 | native | 4096 | `24.6.0` |
| macOS ARM64 | native | 16384 | `24.6.0` |
| Linux ARM64 4K | native | 4096 | `7.0.0-1017-azure` |
| Linux ARM64 64K | emulated-system | 65536 | `6.8.0-147-generic-64k` |

Linux ARM64 原生页测量优先，Full 仅以固定 QEMU 6.2.0 / TCG、machine/CPU、kernel/config/rootfs 补 64K。完整串口、实际命令/时限/退出、payload 前后摘要、helper 以及 runner/image/commit 都在原报告；TCG 不计为物理 native 或性能成绩。

| 当前原制品 | 字节数 | SHA-256 |
| --- | --- | --- |
| `hello.com` | 754446 | `467f8889db7a671cd238a7cc56df933951fea686c7c524480f4112870da3695d` |
| `quickjs-eval.com` | 2274662 | `b7c7e0c6692dc221574e55937003be3dd1c81314db99a89b37ad96715c18598d` |
| `native-io.com` | 982754 | `462a846fc6b9e2aa36a3587a5fc221dd464388ee63e457068b3432344798ef3e` |
| `pages64k.com` | 832752 | `c80197784669f748cff263cf364d8ae49f4b6eae8f53c3df3f84389ae05cf16e` |

## 完整严格回归

[稳定原始路径选择](../../out/v15/04/strict/selection.json)将完整 Core、独立本地 4K guest_manifest/guest_report 和五个本地 publication producer 分别绑定。完整 Core 的所有实际 CI producer/consumer 为同一 `1b2f539`；本地 4K 与 publication 保持 CI=null。独立 SCM 仅作为状态指针，用[同提交实际报告](../../out/v15/ci/scm-1b/svc-scm-windows-1b2f5393e7e7097e9529f72fc68ee52cd491a3a9/scm.json)验证 Core/SVC 相互独立，[原获取收据](../../out/v15/ci/scm-1b-collection.json)保留；原 CI Core 的 service scope 仍为 not_run，本地没有执行服务操作。

原数据[预检](../../out/v15/04/strict/preflight.json)与[临时 fixture 预检](../../out/v15/04/strict/staged-preflight.json)均 pass；[最终严格结果](../../out/v15/04/strict/result.json)为 expected=33、run=33、failures=0、errors=0、skipped=[]。[完整 regression log](../../out/v15/04/strict/evidence/c87ac7672f244b98bd4e9c1b6e4c32ca/regressions.log)执行 producer 关联、CI commit/归档后缀、缺项/重复/失败、artifact/log/serial 篡改、页大小/执行方式、offline/recipe 与独立服务状态的必需拒绝。

为降低 DrvFS 重放耗时，[一次性 staging](../../out/v15/04/strict/staging.json)逐文件复核 1,243 个文件的长度/SHA，保留原 ZIP/provider 元数据，只修改外部选择路径/映射；[执行选择](../../out/v15/04/strict/selection-staged.json)指向 POSIX `/tmp` fixture。stage、预检与回归在同一 Ubuntu session 执行，[结束完整性检查](../../out/v15/04/strict/input-integrity-after.json)确认原资料、临时副本及独立 SCM 指针均未改变。原资料始终保存在主工作区 out，可由 [stage_and_run.py](../../out/v15/04/strict/stage_and_run.py) 重建临时 fixture；没有移动可复用缓存。首次临时路径在下一 WSL session 不可用的[预检失败](../../out/v15/04/strict/attempt-1-staged-preflight.json)保留，没有计作产品失败或通过。

实际数据暴露既有 mutation fixture 总是复制 `builder-tested.py`；identical receipt 的 `changed_inputs=[]` 正确不提供该可选 snapshot。[原失败](../../out/v15/04/strict/identical-recipe-fixture-red.log)保留，`43ab5dd` 使 fixture 与生产 consumer 一样仅在非空差异时读取它；上述 33/33 已实际执行该拒绝路径。生产 Core、原执行报告与 24 项构建 recipe 均未改变，后续文档/fixture 提交不重新签发远程执行成绩。

## 实现与早期进度

既有共享 startup/I/O/deep/Core 入口继续使用当前 v3、原制品和独立 helper。startup 新增 `matrix --profile cross`，按五个实际 native OS/架构汇总，Linux ARM64 保留实际页大小；默认 Full 保持六个页目标。重复 report ID、另一 producer 的引用/上下文及 CI producer/consumer commit 漂移或 CI 缺字段现在直接拒绝。pages64k producer 记录实际 CI；guest 导入保留原执行 CI 和 validation_ci。

红绿日志保存在主工作区 `out/v15/04/`：`cross-{red,green}.log`、`identity-{red,green}.log`、`producer-edge-{red,green}.log`、`pages-ci-{red,green}.log`、`duplicate-{red,green}.log`。早期数据合同显式选择 03 的普通 producer 和真实 Ubuntu startup；CI 字段变异只用于拒绝合同。该阶段尚未取得完整跨宿主组合；后续 1b 的实际结果在前述远程验收签发，旧 M0/02/03 报告保留原范围。

第一轮普通 Ubuntu `dev.py check` 通过，202 tests、0 failures/errors、51 个明确数据/平台 opt-in skips，系统 C bootstrap 通过；原始入口报告为 `out/v15/04/check-workflow.json`，日志在 `out/v15/04/check/`。此次选择 03 pinned producer、官方 Linux loader 和真实 startup；后续 05 recipe 改动合入后须重新选择最终 producer 和执行所需验收。

05 冻结后的集成 `1b68a38` 使用新 input_id `e41f53e2fe3176ce9cdabaeed6a99de2744acfab34669c8db73f9e14f0baf47c`。最终普通 Ubuntu check 通过 208 tests、0 failures/errors、69 个显式 opt-in skips；报告为 `out/v15/04/final-check-workflow.json`。实际 root namespace setup 降权至 UID/GID 1000 的 offline preflight 通过；随后最终双构建、六个交付二进制与两 helper 逐字节比较、14 个复制输入拒绝、精确 before/after inventory 和断网 trace 均通过。实际报告为 `out/v15/04/final-offline/offline-integrity.json`，23 分钟执行，cache unchanged=true；05 已实际封装 helper 并由既有 preflight 证明 recipe-identical（24 项）。

## 最终本地执行与消费者

同一 first offline producer 与独立 I/O/pages producer 的 CI 均为 null。Ubuntu x64/4096 的 17 项 startup 和 20 项 I/O 通过；锁定完整系统 QEMU/TCG 在 Linux/AArch64/4096 与 65536 分别执行 18/21 条入口，startup、I/O 和 64K 的 12 项 deep 全通过。报告与完整串口位于 `out/v15/04/local/guest-{4096,65536}/`，两页均通过只读 Core `validate-guest`，scope=guest-transport、core=not_run。`local/guest-inputs.json` 是局部实际输入选择，未声明完整 native ARM64 或六目标 Core。

Windows 初次受限工具上下文的 startup/I/O 超时保留在 `local/{startup-windows,io-windows}.json`；独立同字节/同路径对照定位到该执行上下文。直接 Windows 普通用户上下文复验为 startup 17/17 与 I/O 21/21，报告为 `local/{startup-windows-normal,io-windows-normal}.json`，没有更改产品、系统或安全设置。旧进程已结束，intervention 记录未发现残留目标。

实际数据选中的 startup、guest startup、I/O 和 4K Core 五组消费者/拒绝回归均通过；普通平台/数据 opt-in skips 仍明确保留。严格 runner 现在允许独立 `guest_manifest` 绑定本地 4K producer，完整 Core 仍必须消费远程同 commit producer/matrix；不模拟 CI 字段迁就验收。SCM/Job 仅在 Ubuntu 构建分发制品，capability=not_run；五类实际 producer 的 notices/maps/HACL publication 回归为 3/3 通过，选择在 `out/v15/04/publication/inputs.json`。

Full CI 的固定 snapshot 入口一次 HTTP 503 保留原失败；只对显式 guest fetch 增加原 URL/摘要的至多三次瞬时 HTTP 重试，共用原 190 秒预算。以实际签名输入和本地外部 HTTP fixture 先红后绿验证 503→成功及重试耗尽；`guest-retry-{red,green}.log` 不冒充上游或新远程成绩。

真实远程 Cross `41343504925` / `a1845855688e3bfcbe9eaae0e9bba989986e0767` 的五宿主 startup 和 Cross startup matrix 通过，但非 Windows I/O 因共享 loader 访问不存在的 `args.controlled_probe` 而失败。原 CI archive `13097833073` 经独立长度/SHA/CRC 验证后，以原 I/O 字节和 helpers.json 复现 exact AttributeError；`8b3f556` 将未提供该选项的 shared caller 默认保持普通 pinned 模式。`io-loader-directory-{red,green}.log` 明确只是模拟记录 CI 环境的本地回放，不计为新的远程成绩。真正本地新 producer 为 `out/v15/04/io-consumer-regression/build-info.json`，其 CI=null 的 Linux 20 项实际执行在 `out/v15/04/io-loader-local.json` 通过。该旧 Cross 和 a184 seal 权限失败、b0 HTTP 503 的失败范围继续保留，见[05 失败记录](tiered-ci-validation.md)；仅前述新 1b run 签发最终远程通过。
