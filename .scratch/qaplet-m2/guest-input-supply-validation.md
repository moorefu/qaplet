# M2 实际 64K 的显式同锁输入供给

2026-11-04；实现基线与最终核对的 integration tip 均为
`91b9a516be41c96266026b9514f073be499b9f2a`。本增量只准备可核验的 CI 输入，
不关闭 11/13、不放行 12/14，也不声明当前 guest 或完整 M2 已执行通过。

## 传输与身份

独立 data-only 分支为 `codex/m2-locked-guest-inputs`，root commit 为
`c67014e83205e70a74eb6fc23f2a341793072598`；该树恰好八件 tracked 文件：
`manifest.json` 与 65536 profile 的七件原输入。数据分支永不 merge 到产品源码。
主代理完成独立 byte 审计后统一推送；实施代理没有推送、Release 或镜像操作。

原 guest lock 为 5692 字节，SHA-256
`22b7512319c9484071eaf90e4db22bde4f0ecf8300e2997f72e5e149a01a976e`。
最终 manifest 为 2589 字节，SHA-256
`538d0c0f7b1e7889857d2cbeb48f3e4b04cd51f0e08d43f71af2389165bf151e`，
记录当前 lock、profile、七个名称/长度/SHA/原 URL，以及
“来自已验签既有缓存、非本次 HTTP 获取、无 CI/guest 执行声明”的 provenance。

| 成员 | 字节数 |
| --- | ---: |
| noble-updates-InRelease | 126125 |
| noble-updates-main-arm64-Packages.xz | 1461744 |
| busybox-static_1.36.1-6ubuntu3.1_arm64.deb | 910700 |
| libc-bin_2.39-0ubuntu8.9_arm64.deb | 591286 |
| libc6_2.39-0ubuntu8.9_arm64.deb | 2776414 |
| linux-image-6.8.0-147-generic-64k_6.8.0-147.147_arm64.deb | 18207916 |
| linux-buildinfo-6.8.0-147-generic-64k_6.8.0-147.147_arm64.deb | 699544 |

总输入 24,773,729 字节；最大单件 18,207,916 字节。原物理缓存
`.cache/guests/inputs` 的全部九件锁输入在准备前后逐件 hash 相同；七件供给的
原 cache、数据工作文件和实际 Git blob 逐字一致。数据树没有 SDK、产品、out 或旧 guest 结果。
完整成员摘要/URL/路径与 Git blob 证明见实施树
`out/m2-ci-guest-seed/source-byte-proof.json`、`canonical-nine-before.json`、
`canonical-nine-after.json`；初始准备记录中的 manifest 长度属于 Git LF 规范化之前，
最终身份以本节和 source-byte-proof 为准。

## 公开入口及拒绝行为

`scripts/guest_inputs.py plan` 校验固定配置，仅向 GitHub outputs 写入单行完整
commit。`import` 使用显式 source-dir/source-commit/cache/page-size/report；
核对实际 checkout HEAD 和完整 manifest 后复制到独立暂存目录，直接调用原
`arm64_guest.verify` 的长度/SHA、真实 gpgv、签名索引与五个包的关系校验。
所有验证成功且既有 canonical 字节正确后，缺失目录一次 rename 发布；已有目录
使用逐文件排他原子链接，不覆盖已有文件，失败时撤回本次新增成员。

源和目标重叠、额外/缺失成员、symlink/hardlink/路径别名、错误锁/页/profile/
长度/SHA/URL、重复声明、不可信签名和坏既有输入均拒绝。正确既有输入保留原字节、
inode 和 mtime，收据记为 `existing-verified`；新增输入记为 `imported`。
输出先保护所选原件及其路径/文件别名。原 manifest 的完整字节复制进本次 evidence，
随 page artifact 保留；收据记录实际 CLI argv/cwd。供给不可用报告 `missing_resource`。

CI 仅在既有 Ubuntu 22.04 的 65536 job 原 fetch 前接入 plan、同仓库固定提交的
第二次只读 checkout、import；不持久化凭据、不增 secret/写权限/cache save。
原 4096 路径、fetch URL/摘要/重试预算与后续实际 guest 命令保持原样。
`arm64_guest.py`、两个输入锁及全部 77 项 SDK RECIPE 与 integration 基线逐字一致，
因此本次不重供 SDK 库；新工具自身、原 verifier、lock 与供给逐输入身份单独写入收据。

## TDD 与本地验证

按已确认的公开 CLI 和 check-ci 边界逐片 red→green；原始日志位于
`out/m2-ci-guest-seed/{red,green}-*.log`。测试使用七件真实固定输入和 Ubuntu keyring；
不可信签名用空信任 keyring 触发真实 gpgv 非零，字节损坏在签名前即拒绝。
未显式提供原输入的普通回归保留外部输入 skip，不构造假 guest 执行。

- `full-check-mapped.json`：Ubuntu `dev.py check` 实际通过；380 tests，187 通过、
  193 既有外部输入条件 skip；新增九个输入 CLI 测试和一个 CI mutation 测试全部实际执行。
  input-lock audit、两个 CI 合同和真实 C bootstrap 均通过。
- 完整检查后的最后一片只增加原 manifest/CLI invocation 保留，
  `green-retained-manifest.log` 再次实际执行九项 CLI 合同通过，`check-ci-final.json`
  通过；未重复构建或大解包。最后收据 `supply-import-final.json` 在同一正确缓存保留
  七件 `existing-verified`，其 manifest 原件进入本次 evidence，非新 HTTP。
- `supply-plan.json`、`supply-import.json`：独立新缓存
  `.cache/guests/ci-inputs-proof-20261104` 的七件完整导入与签名链通过，
  `ci=null`、`guest=null`、`cases=[]`。
- `original-fetch-cache-hit.json`：未改原脚本 fetch 成功，仅 gpgv，无 curl 命令；
  这是原件缓存命中，不是新 HTTP 200。
- `canonical-verify-after.json`：原 canonical 缓存只读签名链通过；九件前后 hash 同。
- `source-byte-proof.json`：七件原输入、raw/Git blob/lock、77 SDK recipe 不变及
  4 GiB reserve floor 证明；完成时 D 盘仍约 4.8 GiB 可用。

首次完整入口因 Windows 创建的 worktree `.git` 含 D: 路径在 WSL 不可识别而未运行；
保留 `full-check.json`。最终仅在新 ignored out 目录使用既有模式的当前 checkout
只读 Git 适配器，fixture init/config/write 与其他仓库透传真实 Git；没有设置全局
GIT_DIR/core.worktree，也没有修改旧冻结树。共享 `.git/config` SHA 前后同为
`c9bd915c61e05abef84335f9ed93229033875f9e44c64220b905f06b61bf7ef6`。

实际新 CI/64K/SDK/Core 仍由主代理组合当前代码后独立 Full 验证；历史 upstream 503
与零 guest case 失败保持原状态。本地输入供给和 verifier 成功均不替代该验收。

## 2026-11-05 · 当前 CI 固定输入供给实测

独立合并 `94cda36` 的 Full `41859852472` 已实际消费固定data commit `c67014e`。
完整manifest原件、当前lock/profile、七件source与canonical长度/SHA/URL、7 imported、
gpgv原日志及InRelease→Packages→五包关系均经独立消费者核验。原fetch仅执行
gpgv缓存命中，无curl，不称新HTTP200；原cache9/data8前后内容与mtime相同。

当前实际64K SDK执行170项，169pass/1inherit fixture准备失败；M0 21命令与M2四
suite通过各自完整journal/seal和清理。输入供给已恢复当前guest执行，但完整SDK、
11/13及Core仍有必需缺口：两Mac当前原矩阵reports=[]，SDK继承fixture待修复，
Core因缺Mac仅可得161pass而非235。初次边界原件封存在主
`out/qaplet-m2/full-94cda36/HANDOFF.md`、`verify/input-supply-audit.json`，完整性通过
不等于Full通过；后到原件单独增量，不覆盖旧503或当前失败。
