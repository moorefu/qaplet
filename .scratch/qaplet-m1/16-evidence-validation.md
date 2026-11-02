# 16 · 正式 M1 严格证据集成

2026-11-03；实现基线 `c688da5bb346dd3d9ff9d6131a8a840d02b36186`，工作分支 `codex/m1-16`。本票消费已解决的 14/15；父规格保持 claimed，最终独立审查和发布镜像仍是后续门禁。

## 固定产品与真实闭包

只选择成功重试 Full `41489263809`、产品源码 `21bd317f4246571f0048789ca97167632c09f924`。第一次 Full `41486421618` 虽使用相同 SDK，但封装 ZIP 和 producer report 不同且 guest acquisition 失败；本票不混入该次结果，也不补写本地 CI=null 报告。

| 身份 | 实际值 |
| --- | --- |
| SDK producer provider ID | `13162749024` |
| SDK 原报告 SHA-256 | `23aed03868e9d6444e5df5c9107f733948900795b3a1334a31124d1250bf2828` |
| SDK aggregate | `7ec19ce8f7dc6eacbe40d650077a60bf463eff77da4baa87552c3b9f81048ed1` |
| SDK recipe | `5a86423264e76e2213d12ca9fbfbc1023fb1360122a81cd28bd00edcf2d068f8` |
| 重试封装 SDK ZIP | `2524580d49e556dc94cbfd51e1a662953279a121f6b051a3b99123a878e7bbb6` |
| SDK + M0 64K guest provider ID | `13162819905` |
| 当前 M0 Full 外层 provider ID | `13163039687` |

主工作区 `out/m1/ci/14-final-full-retry-complete-collection.json` 保留九个原 provider ZIP 的完整 SHA/长度/CRC/inventory 验证；`14-final-full-retry-artifacts.json` 是原 API ID/name/digest/run/head SHA。新选择只重放八个有执行输入的顶层 transport；matrix summary 没有被当作产品执行。短别名 `14r-ID` 只用于本地路径，选择中的 name 按 ID 恢复真实 provider 名称。所有原报告均保持原始字节。

`out/m1-16/prepare_selection.py` 从这些原件及 15 的已验 M0 selection 派生显式选择，保存 `m1-inputs.json` / `config.json` 与 Windows 路径变体；它不重写原报告。Ubuntu 和 Windows 读取同一供应字节，构建仍只在原 Ubuntu CI 执行。本票没有构建产品，也没有重新派发 CI。

## 实现与独立范围

`check_evidence.py` 增加 `m1-evidence` profile，复用原 RequiredResult、progress/log、失败与零 required skip 规则；`m0-evidence` 保留原合同。`m1_gate.py` 只做显式选择/CI-provider/当前源码/SDK/分发的 join，复用 `ci_evidence.unpack`、`core_gate.Evidence`、`sdk_guest.producer/consume`、`sdk_matrix.validate_native`、原 Core link audit 和不变的 `core_gate.evaluate`。

| 必需项 | 真实重放范围 |
| --- | --- |
| Linux x64 | 98 项，实际 1000 create/close/destroy，最低 1000 |
| Linux ARM64 | native / 4096，98 项，实际 1000，最低 100 |
| macOS x64 / ARM64 | 各 native，98 项，实际各 1000，最低 100 |
| Windows x64 | 97 required + 1 optional file-symlink；本次可选项也实际 pass，1000 循环 |
| 包外纯 C 应用 | 从封装 ZIP 在原 `/tmp/qaplet-sdk-consumer-*` 编译，仅公开 include/archive；实际 link/audit、8 项退出/输出/关闭合同 |
| SDK 64K | 锁定完整系统 TCG，aarch64 / 65536，kernel `6.8.0-147-generic-64k`，98 项和 payload before/after/cleanup |
| 独立当前 M0 Full | 六目标、235 项、离线两次构建、六二进制与两 helper 比较、14 refusals；原 scope 不改写 |

包外应用源码临时目录已不存在；公开源码、header、两个 archive 和模块仍封装于原 ZIP，原 argv/cwd、构建日志、sidecars/maps 与 nm/import 原始输出都保留。外部应用 APE 与 producer 示例的二进制相同是本配方的实际结果；包外实际 compile/link、独立输出路径和审计建立该边界，不以二进制不同作为条件。

原生 validator 原先调用 executor 的 `startup.select_loader`，消费时会 chmod。本票在 evaluator 内实现纯 reader；不修改 `startup.py`、`sdk.py`、SDK README 或任何产品 recipe。`product-recipe-identity.json` 按固定产品 Git blob 重核全部 57 文件，差异为空。许可证仅以 Ubuntu Git 文本的规范 LF 比较公开源码；供应包和外层 transport 仍按精确原字节验证，没有修改换行或产品原件。

## 实际红绿与保留的失败

原始证据在工作树 `out/m1-16/`；交接时完整复制到主工作区 `out/m1/tickets/16/out/m1-16/`，由复制收据保留原路径。不可只保存最后成功摘要。

| 记录 | 实际红与对应绿 |
| --- | --- |
| `01-loader-{red,green}.log` | 真实 POSIX 0640 loader 消费后变为可执行；修复后 3 项公开 matrix CLI 测试通过，成功与失败均保持 mode + SHA |
| `02-full-{red,green}.log` | 旧入口拒绝 m1 profile；新入口接受真实完整重试闭包，preflight scope 独立 |
| `03-deadline-{red,green}.log` | repacked native 的 deadline 小于实测 elapsed 曾被接受；现在拒绝 |
| `05-output-{red,green}.log` | 输出曾能覆盖选中报告；现在拒绝且原字节不变 |
| `06-runner-{red,green}.log` | 中断报告曾缺实际运行计数；现在 required skip/error/empty/interruption 均失败并保留计数/log |
| `08-audit-target-{red,green}.log` | 宣称 pass 的审计命令可指另一 CPU 制品；现在在 inspection target 检查拒绝 |
| `09-producer-commands-{red,green}.log` | producer 可缺实际命令；现在执行记录、区间、deadline 和 build/recipe aggregate 必需 |
| `10-timezone-controls-{red,green}.log` | 原 string comparison 接受移到区间外的 +12 offset，又拒绝同一瞬间的合法 offset 转换；改用已解析瞬间后，两项公开 CLI 控制同时通过；最初独立 probe 也保留为 `10-timezone-red.log` |

第一轮 `04-mutations-first.log` 的 66 项全部通过。随后 `strict-windows.json` 的失败完整保留：实现过程中子 CLI 源码变动造成的失败没有算作通过；已有 guest 语义变异 fixture 在 Windows 使用 text writer 产生 CRLF，先命中 serial byte mismatch，已改为精确 byte writer。第一次 Ubuntu `check-ubuntu.json` / captured command logs 保留重复 config read 引发的 FIFO 进度测试超时；修复后 focused FIFO CLI 回归及完整 Ubuntu check 通过。

加入 timezone slice 前，固定 evaluator/test 源码的 `final-strict-windows.json` 已实际完成 84/84、零 failure/error/skip；`final-check-ubuntu.json` 为 309 项及真实 C bootstrap 通过，`final-preflight-ubuntu.json` 和 `final-native-ubuntu.log` 的 3 项也通过。这些保留为单独的前一版成绩，不代替 timezone 修复后的最终验收。

最终固定源码的验收：

- `accepted-strict-windows.json`：`scope=evidence-regression`，85/85，failure/error/skip 全为 0；M1 与独立当前 M0 Full 都 pass，required/optional gaps 均为空，评估器和 regression source 结束重核通过。
- `accepted-check-ubuntu.json`：完整 `dev.py check` pass，310 项普通合同（164 个平台/显式数据 skips 保留普通 scope）以及真实系统 C bootstrap；这些 ordinary skips 没有进入严格 profile 的通过条件。
- `accepted-preflight-ubuntu.json`：同一八 transport / 当前 M0 Full 原字节的 Ubuntu 只读 preflight pass，明确未运行变异；Windows 完整严格成绩独立保存。
- `accepted-native-ubuntu.log`：3 项实际 native CLI 回归 pass，包含最终 evaluator 的 POSIX loader mode + SHA 在成功和失败后的不变性。
- `product-recipe-final.json`：57 个固定产品 Git blob、当前 index blob 和工作字节逐项一致，`changed: []`；没有修改 frozen M0 startup 行为或触发新的产品构建。
- `accepted-summary.json` 是上述原报告的短摘要，不独立签发执行成绩；原 strict report、preflight、每项 progress、所有早期失败和 repacked/extracted fixture 全部保留。

语义变异显式重打包 outer ZIP，并更新 fixture catalog 的必要 digest/size，深入到 native/SDK/包外/guest 合同；它们与保持原 ZIP 不变的 extracted-only/原 ZIP tamper 是不同断言。覆盖 missing/empty/duplicate/conflicting ID/target/case/component、错误源码/CI/provider/SDK/recipe/APE/loader、原日志与 actual 不一致、同时伪造 expected/actual/log、目录/模块 argv、生命周期、deadline/skip/timeout、公开文件和隐藏 archive 缺失、包外私有 include/wrong archive/漏编译，以及 guest SDK/source/producer/page/mode/payload/serial/cleanup。既有 guest 16 项 provenance 变异和完整系统/no disk/qemu-user 原协议回归也由严格 runner 必需执行。

## 判定边界

`evidence-preflight` 不声称执行变异；只有完整 `evidence-regression` 的真实 required catalog 全部执行且零 skips/errors/failures 才通过。最终报告分别记录 M1、当前 M0 Full、provider/选择/产品与 evaluator/test source 身份、完整已解析 evidence_index、required/optional gaps、计数/log/progress。产品 57 个 recipe 文件保持固定，评估器和测试源码在严格结束时再次核对。

已验能力以公开 ABI 1 / capabilities/defaults 和封装 SDK manifest 为准：单 owner/Context 生命周期、严格复制值与调用方诊断、源码 ESM/TLA/动态 import 与固定缓存、Host Module、Promise pump、有限预算/资源限额、复制 POD 事件、协作 stop、snapshot、幂等 close 与前置条件明确的 destroy；CLI 使用同一 Runtime。每个供应 native 和 64K catalog 都包含公开 metadata 查询与源码/负向合同，而非仅依赖 manifest 声明。

父规格不由本票关闭。M2 OS waker、I/O cancellation、late completion、完整 scheduler、物理 ARM64 64K 和性能都不在本结论内。长期原 ZIP 镜像仍暂缓，`release_ready=false`。
