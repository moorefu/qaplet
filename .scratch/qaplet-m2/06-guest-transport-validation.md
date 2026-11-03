# 06 · 64K guest 证据传输定向修复

本记录只覆盖 guest/tool/test 修复。产品供应固定为 Full `41621764245` 的
`1708edec3aca9d7593cc530e6e4e31256c5af3fe`；全部新输出位于主工作区
`out/m2-06-guest-fix/`。没有重编产品、改写原件、扩大时限或随机重试到绿。
06、后续依赖及父 M2 的状态仍由集成验收决定。

## 原事故与归因边界

原 `sdk/guest.json` 保持 fail，原 parser 只签发此前 86 pass。111 条命令的实际
exit/stdout/stderr 都符合合同，但 `qaplet.run-unsettled` 唯独缺 START，无法核验
时长与立即退出合同。原 init 有无条件 printf，原 serial 缺该行且邻接无交错；这些原件
没有证明 guest 发射失败、console 丢字节、host 捕获遗漏三者中的具体层级。

公开 `sdk_guest.py verify` 的初次及最终重放均失败，报告为
`red-original-sdk64.json`、`final-old-sdk64-still-fails.json`。旧 Full 继续失败，
不从相邻字段补算 START，也不把原输出符合合同推导成完整 guest 通过。

未修改 runner 的同供应 SDK 本地实际 64K baseline 为 111/111，见 `baseline-111.json`。
这是一次新的执行；原事故的丢失层级仍为 **unproven**。

## 新传输合同

`da6f911` 为每条新记录只计算一次值，检查写入 guest RAM journal 的 printf，
并保留原 console 诊断镜像。全部 payload 退出后才打开固定名称
`org.qaplet.evidence` 的 virtio port，传输一次完整 journal，末尾附 guest 计算的
精确长度和 SHA-256 seal。没有给产品进程继承额外证据 FD，既有 fd0/1/2 路由保持原状。

新报告显式声明 `qemu.protocol.version=2`、`transport=virtio-serial-journal`、
设备路径、端口名及原始日志身份。consumer 同时绑定当前 runner 源码、完整 QEMU argv、
锁定 kernel/config、payload 前后摘要和清理事实。新模式没有 console fallback；
缺通道、坏 seal、缺字段、重复字段、未知记录、缺 DONE、非零 QEMU 或错误命令结果都拒绝。
旧 console-only 证据仍按原严格字段及重复规则消费。

## 真实执行与受控实验

- `final-111.json`：提交后的 runner 在固定主 `.cache/guests` 中消费原 CI 的同字节
  SDK/APE/loader，实际 ARM64 65536 页执行 111/111。26 个 payload 前后摘要相同；
  725 条 console 协议记录与 sealed journal 逐字节相同；QEMU exit 0、未 timeout、
  DONE 恰好一次、RAM-only、无磁盘/网络/宿主全局修改。
- 两个 unsettled CLI 的 START/END 完整且同秒，exit 1、空 stdout、stderr 为
  `qaplet: UNSETTLED_ENTRY`。`final-local-transport-audit.json` 重新读取 raw bytes、
  seal、当前 transport consumer、完整公开输出与时限合同，保留逐文件 evidence index。
- `dual-channel-minimal.json`：真实 run-unsettled 最小实验的 65 条双路记录相同。
- `controlled-console-loss/observation.json`：一次明确标记的真实 QEMU/64K 最小实验，
  包装器只从 host console 故意删除 START。底层 console 和 journal 都有 65 条完整记录，
  返回 console 有 64 条；runner 从独立 journal 通过。包装器、底层完整 raw console、
  过滤后 console、journal、实际 QEMU 身份及 cleanup 全部保存。该实验只证明隔离能力，
  不给旧事故补签层级归因。

所有新真实本地执行的 `ci=null` 原样保留。当前匹配 CI 的新 SDK strict transport 闭包仍需
后续实际 Full；`final-local-transport-audit.json` 明确 `strict_sdk_acceptance=false`。

## 回归与失败历史

受控 QEMU 系统边界通过公开 `arm64_guest.py run` 先复现
`Missing complete guest execution: probe`，再由新 journal 通道通过；
`red-console-loss.*`、`green-console-loss.*` 保存红绿。`green-transport-negatives.*`
覆盖缺 START、重复 START、缺 DONE、坏 seal、假成功、无通道和 QEMU 非零退出七种拒绝。
该 fixture 不作为实际 kernel 或产品执行证据。`test_guest_command.py` 在约定的
`foundation.command` 边界核对跨 pipe buffer 的 128 KiB stdout/stderr 原始字节。

`final-legacy-core.json` 对现有完整 M0 Full 的六目标、235 项严格消费通过。
启用旧 Core 公开变异套件后首次 21 测试出现 13 个失败子用例，保存在
`final-core-regressions.*`。针对相同五测试的旧源码对照复现同样 13 个失败，签名比较见
`core-regression-baseline-comparison.json`。原因是 guest-first 顺序误作 native、
嵌套 SDK serial 的 basename 误选，以及多变体复用 gate.json 读到消费者保留的旧结果。

`e77d100` 仅修测试夹具：按实测 native Linux x64 选输入、通过显式映射定位精确日志、
每个变体使用新输出。原断言和 consumer 判据不放宽。`green-core-regressions-2.*`
结果为 21 总、20 实际 pass、1 未选择独立 service pointer 的 skip，全部目标变异都命中拒绝。

SDK 导出旧测试对真实本地供应复现 `111 != 105`，见 `red-sdk-export-catalog.*`。
`54b1d69` 使用独立列出的完整既有 case 覆盖下限与 manifest/count/SDK/recipe/package/
producer 身份一致性；不调用生产 catalog 生成期望，允许后续增加 case。
`green-sdk-export-local.*` 使用既有 `verified/build.json` 的独立本地
`2a9ab07`、`ci=null`、`dirty=true` 原样身份验证正向导出和不可覆盖原件边界。
`green-sdk-export-ci-refusal.*` 用原 `1708ede` CI producer 实际验证本地 CI-null 导出被拒绝；
manifest/selection 未创建，`evidence_verified=false`。没有借用或改写 CI 环境字段。

提交前各改动源码均为 LF 且与 Git blob 逐字节相同。Ubuntu 完整 `dev.py check`
的早期临时 Git shim CRLF 失败保留于 `final-devcheck.json`；修正 shim 后
`final-devcheck-2.json`、`final-devcheck-3.json` 均通过，367 总、179 实际执行、
188 条件 skip，真实系统 C bootstrap 通过。最后测试修改后的 `final-devcheck-4.json`
通过：368 总、179 实际执行、189 条件 skip，真实系统 C bootstrap 通过。
shim 只处理当前 worktree 的只读查询，其他 fixture 和写操作使用真实 Git，没有全局 GIT_DIR。

## 合并最新集成后的边界

`0746f29` 已合入集成 `b97b09c` 的独立初始化诊断修复。guest runner 与 core transport
源码字节保持不变，所以前述同字节 111/725 条实际 guest 成绩保留原供应范围。
新产品的 63 项 SDK recipe 则与现成创建修复 producer `3019c3f` 逐项相同，
全部改动源码与 Git blob 字节相同且均为 LF，见 `final-source-and-export.json`。

`after-creation-sdk-export.*` 使用该真实 `ci=null`、`working_tree_dirty=true` 的现成供应，
公开正向导出实际 112 项，全部既有独立必需 ID 仍在；原件覆盖拒绝同时通过。
原 `1708ede` CI 供应在当前新配方下由 `Consumer recipe differs from selected SDK producer`
拒绝，见 `after-creation-stale-ci-export.json`；这是 stale-recipe 拒绝。
此前同配方的 CI-null 身份拒绝保持原记录，新同配方 CI 身份与完整 112 项实际 64K
闭包由后续真正 Full 供应重放。没有重建 SDK 或修改任何 CI 字段。

没有 push、整体审查或 resolved 06。初始化 close 诊断由独立修复处理；新匹配 CI Full、
最终 T03 全资源重复及发布门禁保持各自范围。
