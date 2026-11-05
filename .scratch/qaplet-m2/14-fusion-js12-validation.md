# 14 与已合入 JS Work 12 的本地融合

2026-11-05。实际 integration `3d03f855867ae363cfb528c55a4c2b1c7b54e58e` 融合到票14，merge提交 `2a770d4`。唯一手动冲突是 `sdk.py` 同一位置追加的 catalog，保留两侧并集：JS Work 7项、Process 12项及原来的普通包外/独立故障入口。`inventory.json` 验证无重复或丢项，12的实现与fixture字节匹配3d；未带15未完成IO。

旧票14 source tip `327489c`、product `3462fe3` 和完整旧sourceout保持冻结，原成绩仍见[原本地记录](14-process-cancel-validation.md)。本次所有新helper、供件和报告独占主仓库 `out/qaplet-m2/14-fusion-js12/`，不覆盖旧目录。

## 新发现的 Windows 真实竞争

15的实际IO系统gate先发现：child已退出，但helper尚未发布时，`TerminateProcess` 返回access denied，旧14主干无条件把native5记成清理错误。15的IO gate原件在其worktree `out/m2-15-process-io/fault-gated-red-windows.json` 等；14负责唯一共用修复，15已经取exact修复，没有再维护第二套。

14从旧密封SDK公开header/archive包外编译新的系统fixture。真实 `GetExitCodeProcess` 已读37后，在返回helper、取得Runtime锁和发布退出之前阻塞于条件gate；两个独立命令实际复现：

- `--exit-race`：cancel返回OK，但公开status为CANCELLED/domain2/native5/reaped0。
- `--exit-race-close`：close返回27，公开清理诊断27/domain2/native5。

`race-red-build.json` / `race-red-windows.json` 保留编译配方、旧SDK/ZIP/archive、当前fixture前后身份、真实命令/raw日志和两项预期产品red；它们不是产品通过。该fixture只使用公开SDK与获确认的OS seam，不读取私有Runtime布局，也不靠sleep猜竞争。

修复提交 `5724f4ed5e091bbdd19182a32038edd6eda2fb71`：仅在 `TerminateProcess` 失败后保存原生错误，再对仍在接受锁下拥有的HANDLE执行零等待；实际已signaled时不制造伪清理错误。helper仍唯一发布实际exit/reap并关闭HANDLE，owner不伪造结果。真正活child的终止拒绝仍保留native错误。成功终止路径没有额外等待调用。15已融合此exact提交。

## 一次新的组合供应和必要回归

产品/配方source为5724f4e，Ubuntu正式供件 `build.json` 通过；SDK aggregate
`580a020011dc8a47f4f4abaf7693a6427fcffd501445f0e105c417f94ade01bc`，recipe
`2ee9fd59b74dab4612cf4f45a8091f1ff4c500cd8c0f05de1caccc17a0e8aef9`。
原ZIP为 `f01c24115307138560f144e02d24b8d3d8424d0e2c5adb17fa274c41a5536527`，16404075 bytes。当前实际77 recipe、79 ZIP成员；目标archive/APE/loader身份及raw/Git/LF由交接审计派生，不写成通用常量。

| 原件 | 实际结果 |
| --- | --- |
| `inventory.json` / `preflight-final.json` | catalog并集、实现来源、无IO混入、raw/Git/LF及仓库预检通过 |
| `linux.json` / `windows.json` | 179pass / 178pass+既有optional symlink blocked1；同字节供应，含全部7项JS Work及12项Process |
| `package.json` / `package-windows.json` | 两端65pass；包外只使用密封ZIP公开头/archive/source，Ubuntu编译、Windows只消费 |
| `fault.json` / `fault-windows.json` | Linux12pass / Windows10pass；包含12 caught JS calloc、14 physical/native-error/HANDLE-reuse/wake等；Windows-only `--exit-race` 正式供件green，cancel与close两序都保留actual37并实际排空quiesce |
| `related-check.json` | 与catalog合并直接相关的 `SDKNativeContract` 三项实际报告/缺项拒绝/只读loader合同，3pass/0skip |
| `handoff.json` / `merge-final.json` | 新scope只读身份审计与交接前再次融合当时actual integration；待独立merger核验合入 |

Windows-only退出竞争不会出现在Linux执行报告。Windows replay在全部消费scope启动前，按当前权威 `PROCESS_FAULT_CASES` 补Windows适用但Linux未执行的case，实际增加了新gate；没有用Linux报告的case数替代Windows catalog。原build未使用的旧replay helper保留为 `replay_windows-build.py`；这次纯消费helper修订未改产品或SDK配方字节，没有重供。

本次不重复380项或单独重审不变toolchain；既有本地全check仍保留原范围。本轮手动改动是catalog并集，实际C+SDK新组合和新Windows修复分别经新完整供件消费、独立故障red/green和3项针对检查验证。所有正式执行supervisor timed_out=false。

## Python范围与阶段边界

新guard从 `git ls-files -z -- '*.py'` 在每个scope前取得全部79项tracked Python（含35项contracts），加固定scope helpers，前后保留真实摘要；实际父解释器imports/called code另列。inventory/preflight/build静态集合89项，消费/related90项，差异是保留的未执行旧replay helper快照。父实际SDK相关14项、preflight9项、related10项；子Python动态imports没有profile，不声称该范围。tracked源码另以raw/Git blob限定，未倒填旧before。

本地实施/组合验证complete，等待独立merger登记Local development放行。票14仍claimed、fullCross deferred；未push/触发CI，未完成其他原生宿主、actual64K、Full/1小时组合长测、父M2或整体Review。旧原件和本次新red/green分别保留。
