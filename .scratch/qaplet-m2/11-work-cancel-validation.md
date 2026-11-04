# 11 · 取消接受顺序、C 结果 release 与物理保留

从 `5bf82a48291ae0d1bb53817cb4e7e962d12acdc7` / `codex/m2-11-cancel` /
`.cache/worktrees/m2-11` 的独立已领取工作树开始。先读 implement-spec、tdd、
tests/mocking、根 AGENTS、开发入口、B02/B03/B07/B11/B12/B13、10 与其文件修复
记录及 ADR 0005/0007。沿用已确认的公开 SDK、包外纯 C Host 与真实受控 OS seam；
不重复接口访谈，不交付 12 的 JS task/Promise，不关闭父 M2。

## 公开增量与交接

- `qaplet_work_cancel` 任意线程，以 worker 领取/结果发布同一 mutex 的接受点排序。
  `OK` 表示取消已记录，保留的取消终态重复取消幂等；结果先发布为
  `QAPLET_ALREADY_COMPLETED=29`。`QAPLET_CANCELLED=28` 是逻辑结果，native 域/码
  为零，不伪造 errno。预留 27 给并行 Process，既有枚举值与旧 POD 不变。
- 排队取消后永不调用 handler；运行中取消仍允许真实 syscall/副作用继续，
  迟到输出不能再次发布终态。既有 bounded file handler 原样被门控包装，
  验证取消在 syscall 前、真实二进制读出后与完成发布后三个顺序。
- 独立版本化 `qaplet_work_status` / `qaplet_get_work_status` 任意线程只返回 POD，
  区分逻辑 state/code、任务 native call 的 `physical_pending`、owner 默认 ref
  的 `pending_handoff`。pool standby 线程与其 TLS 析构另由 native lifetime 约束。
  完整结构/版本与零 unknown tail；无 JSValue、Context 或借用内部指针。
- owner copy/release 保持线程/驱动重入限制；owner 回调可以取消与查询，结果复制与
  release 仍 BUSY。HostNotify 的 TLS guard 对两个新 API 都返回 BUSY；任意线程
  并不表示 signal-safe。跨 Runtime、旧 generation、release 后与 CLOSED token
  均 INVALID_HANDLE，M1 实例 UNSUPPORTED。
- release 立即使 token 失效；尚未交接的记录/ref 留到完整 jobs 检查点之后的
  owner handoff。尚未返回的 native call 继续保留输入、预留输出及操作记录。
  纯 C handoff 不创建 JS ledger；活跃入口仍扣原累计 dispatch/绝对 deadline。
- 取消/完成/回收继续使用接受前预留的记录，不新增业务槽或完成时分配。
  128 操作、8 MiB 输入加输出子额与 16 MiB 总额同时预检。结果保留、已取消而
  仍物理活动，以及尚未完成 owner handoff 的 release 均继续收费。
- stop/close 的取消终态同样带稳定 CANCELLED；close 只移除 JS，不强杀线程，
  0/有限 quiesce 与 destroy BUSY 可重试。函数代码/userdata 保留到真实 native
  quiescent；`pthread_tryjoin_np` 非 EBUSY 错误退休未知身份，不再次尝试该身份。
  一个 heap gate 刻意永不释放至进程结束，验证持续保留；该负向用例不称清理成功。

## 红绿与保留的非成功原件

所有本票输出在本工作树 `out/m2-11/`，不改写既有原件：

| 原件 | 事实 |
|---|---|
| `cancel-red/report.json` / `host.stderr` | 新公开取消 tracer 在旧头编译失败，无取消入口与 CANCELLED 类别 |
| `cancel-green/report.json` | 实际 foreign cancel、排队不启动、运行副作用继续、唯一取消结果、物理收费通过 |
| `result-red/report.json` | native 完成先发布，8 个 Promise jobs 尚未清空时 release 提前清零 handoff/容量，公开断言失败 |
| `result-green/report.json` | token 立即失效，完整 jobs 后才 handoff/ref/记录回收；并复验 cancel、STOPPED retention、file |
| `status-red/report.json` | 独立状态 POD/入口尚不存在，公开编译失败 |
| `status-green/report.json` | 测试误将既定 run_once PROGRESS 写为 IDLE，保留失败；未据此改变产品 |
| `status-green-2/report.json` | 改为先断言 PROGRESS，再独立 0 空闲检查；版本/尾部/跨实例代次/等待唤醒/close 通过 |
| `native-contracts/report.json` | 真实 file 三顺序、回调/HostNotify 权限、不可返回 gate 的安全保留通过 |
| `cancellation-limits/report.json` | 实际默认 128、8 MiB 和 16 MiB 各拒绝超额；取消/release 物理收费与恢复通过 |
| `final/dev-check-imports.json` | 首个逐函数磁盘追踪 driver 主动 SIGINT，`KeyboardInterrupt`/exit 1；不算 check 成功 |
| `final/dev-check-fast.json` | 缓存相同源码路径、保持实际调用/导入闭包前后身份绑定后，完整标准 check 与 C bootstrap 通过 |

首个慢 driver 中断后只读核对进程树，未遗留其旧 check 子进程。后续 driver 仍
在 import/run 前记录脚本库存，并在实际调用及导入闭包结束后比较同一原字节；
没有为性能改变产品、合同或验收范围。

## 首批冻结供应 `8810828`（保留独立范围）

在提交前实际执行 `git merge --no-edit codex/m2-integration`，由 `5bf82a4`
快进至 `620b9967172057311eadabe45918c92c06c4c3b1`（含 02 Windows probe 修复），
然后产品提交 `8810828d4ccec4b4fb66ffdb07ff3a2d215b6f65`。从未融合旧历史的 Git
名为 main 的分支。所有 SDK、双 archive、三 APE 及包外 APE 在 Ubuntu 构建；
Windows 只消费同字节，使用正常提升用户上下文与 PE CreateProcess。

| `out/m2-11/final/` 原件 | 实际结果 |
|---|---|
| `build.json` | 完整静态 SDK ZIP、双 CPU archive、Host/CLI/application 三 APE 与审计 pass |
| `linux.json` | Linux x86-64 / WSL2 6.6.87.2 / native / 4096，160/160 pass，生命周期实际 1000 |
| `windows.json` | Windows x86-64 / 10.0.22000 / native / 4096，159 pass + 1 既有 optional symlink 权限 blocked；必需缺口 0，生命周期实际 1000 |
| `package.json` / `package-windows.json` | 真 ZIP 解在源码树外，仅公开 include/archive 编译，同一 APE 两宿主各 46/46 pass |
| `fault.json` / `fault-windows.json` | 两宿主各 2/2 必需受控系统 case pass，含取消已发布后 send EIO 与未知 join 后端恢复仍不重碰身份 |
| `dev-check-fast.json` | 370 tests，177 实际执行、193 条件 skip；真实系统 C bootstrap pass |
| `handoff.json` | 70 RECIPE raw==产品 Git blob 且全 LF；SDK 聚合、密封 ZIP/全部成员/三 APE、双 archive 实际各 14 ELF64 REL、machine 62/183 全核验 pass |

SDK recipe `07385f660bb9588b5b39e65383cef96d32956aab33c26937c2f9f0fde0e8397f`；
SDK aggregate `5a1fc5fb8c7889beb4b15e43153434a3eda7539732cbe6fed015f1d260ae99c1`；
原 ZIP `791106938e1f9290bc9dff865c9279675dfd2efffce8f823e419fd4b66acf696`，
15,621,819 bytes。producer 的 `working_tree_dirty=true` 原样保存。

`build-imports.json`、`*-imports.json` 与主报告内 `execution_identity` 均保存实际
driver、真正调用及导入的 repository Python 闭包的执行前/后原字节。没有以
仅事后自 Hash 的侧车代替执行身份。只读 Git shim 只映射本 linked worktree 的
查询，fixture init/write 透传真实 Git；未设置全局 GIT_DIR/GIT_WORK_TREE 或
更改共享 core.worktree。主工作区物理 `.cache/` 仍为输入/构建缓存。

本批仅证明已列本地 scope；Process 新集成后的最终供应、当前五 native/实际
64K 与组合 Full 由后续实际原件另行签发。保持 claimed，不追认当前父 Full、
独立终审或最终 T03/1h/10k 完成。

## 终止交接补漏与实际 Process 融合

交付前实际融合 `codex/m2-integration` 的 `a5c8b10` Process 合并，得到
`7267508fe81d5c4e1b106cf2d2d655e6b23101d4`。只有公开头与诊断映射两处预期
冲突；人工保留 PROCESS_ERROR 27、CANCELLED 28、ALREADY_COMPLETED 29 与全部
诊断。Process 的 bit 22、WIN32 域 2、共享内部完成 provider 轮转与既有四类
32 交付预算全部保留。`fusion/build.json` 及 `fusion/dev-check.json` 构建与
标准检查实际成功，但该中间供应未选择为最终结果，不冒充已经做过 normal/包外。

自查 release 延迟 handoff 的直接终止路径后，先用真正供应的公开 SDK 构建
最小纯 C Host：`stopped-release-red/report.json` 实际得到
`operations=1 data=1 handoffs=0 retained=1`，exit 1。任务物理完成、C 已 release，
随后 owner 确认 STOPPED，记录仍滞留到 close。永久回归加入既有
`--m2-work-cancel-result`，`stopped-permanent-red/report.json` 同一公开 POD
断言失败；无私有字段、分配次数或 sleep 猜顺序。

修复 `c29e72e` 只在 owner shutdown 清 handoff 后，对 `released && !physical`
调用既有 retire。此时没有 C 结果、物理调用或后续交付责任；物理仍运行的记录
保留。`stopped-permanent-green/report.json` 的 cancel-result、cancel、status、
STOPPED retention、真实 file、默认容量、不可返回 gate、真实 TLS 析构 8 个
直接影响 scope 全通过。无需新增 mode，normal/包外 case 数保持原组合计数。

随后实际融合仅 tracker/docs 更新 `453fe767380c52e489c63eb6fa60113646bf644e`，
产品冻结为 `96e8eb4743c7147356258e7626bab65a1df48691`。最终在新的
`out/m2-11/fusion-fixed/` 供应及验证，保留首批与中间批原件；最终结果另记如下。

## 最终选择 `96e8eb4` 与待 Cross

最终只选择 `out/m2-11/fusion-fixed/`。交接前再实际融合纯 tracker 更新
`5c2f019e7178f3221343a5b56dfb14884e4cfed3`，得到 `98ff514`；该更新不改变
产品配方。最后本票 Markdown 交接提交也不改变任何 SDK RECIPE 原字节。

| 最终原件 | 实际结果 |
|---|---|
| `build.json` | Ubuntu 完整 SDK、双 CPU archive、三 APE 与审计 pass，产品 `96e8eb4`，producer dirty=true 原样保留 |
| `linux.json` | native x86-64 / 4096，169/169 pass，生命周期实际 1000 |
| `windows.json` | 相同 APE，native x86-64 / 4096，168 pass + 1 既有 optional symlink blocked，必需缺口 0，生命周期实际 1000 |
| `package.json` / `package-windows.json` | 真 ZIP 树外仅公开 include/archive 编译，相同 APE 两宿主各 46/46 pass |
| `fault.json` / `fault-windows.json` | 相同 Ubuntu 构建 APE，两宿主各 2/2 pass；故意不确定 join/不可返回负向范围不宣称成功清理 |
| `dev-check.json` | 370 tests，177 实际执行、193 条件 skip；真实系统 C bootstrap pass |
| `handoff.json` / `audit-imports.json` | 77 RECIPE raw==产品 blob==当前 HEAD blob 且全 LF；完整 ZIP/SDK/三 APE 与 22 个能力名、8355839 元数据一致；双 archive 实测各 15 个 ELF64 REL、machine 62/183；全部执行身份闭包 pass |

最终 SDK recipe 为
`67ab830e5df94dc418ceb1c36142638ba6c80fff72c1430b24d6e1a6e18ede13`，
SDK aggregate 为
`9742806fbc088d0a5e0a1c7e68f6a6ee21602b5966ecdf9f1f92dfa972597c33`；
原 ZIP SHA
`efc133293d88abbc5da33bc68ac761e7a97c704fc47b27d986950f90a9d8ad2f`，
16,285,449 bytes。两宿主使用各自实际 OS/kernel/CPU/页大小记录，Linux 使用
原锁摘要匹配的 official-assimilate x86-64 loader，Windows 使用 PE CreateProcess。

实际入口为 Ubuntu-22.04 WSL `--cd` 本工作树后执行 `guarded_fast.py` 包装
`scripts/sdk.py build/run/package-consume/fault-consume` 和 `scripts/dev.py check`；
Windows 用同一 driver 运行 `sdk.py run`、`replay_windows.py`，不编译。
每次精确 target/arguments、实际编译/执行 argv、stdout/stderr 与 timeout 事实
保存在主报告的 `execution_identity`、`commands` 及对应 `evidence/` 原件。
最终审计 driver 同样在 import/run 前后绑定真正调用/导入的 repository 闭包。

本票五条 C work 合同的本地正负/竞争范围已完成，保持 `claimed / awaiting_cross`。
待 root 独立 merger 核对与合入，再以新当前产品签发五 native、实际 64K 和
适用 Full；此前 08/10/13 或旧配方 Full 不外推为本票新配方成绩。
没有合并主树、push、关闭本票或父 M2，也没有执行整体 Standards/Spec 终审。
