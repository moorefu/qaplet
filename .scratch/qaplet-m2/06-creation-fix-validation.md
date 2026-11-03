# 06 · 未发布创建的回滚与独立原生诊断修复

本次只修复 06 的未发布创建诊断与相关已发布清理诊断，不实现后续资源，
不改父规格、不关闭 06。工作树为 `.cache/worktrees/m2-06-creation-fix`，
分支为 `codex/m2-06-creation-fix`，从 integration `287736d` 开始。
沿用已确认 T01/T02：供应公开 SDK、包外纯 C Host 和受控系统 wrapper。

## 公开合同

新增独立版本化 `qaplet_m2_diagnostic`、
`qaplet_create_m2_with_diagnostic` 与 `qaplet_get_m2_diagnostic`。
旧 `qaplet_create` / `qaplet_create_m2` 的签名和返回类别、M1 布局、reserved、
旧错误数值及已有 `qaplet_m2_snapshot` 布局均保留；失败仍为空 Runtime，
不发布成功半成品。正常供应的冻结 M1 头消费者继续实际通过。

POD 分开记录主因及其原始 native domain/value、首次控制错误、首次清理错误及
原始 domain/value、原生状态、不确定句柄数量和创建回滚 COMPLETE/INCOMPLETE。
成功创建与已发布实例的回滚字段为 NONE。get 是任意线程的同步 POD 复制，
不执行 JS，CLOSED 后仍可查询。短结构、版本和非零未知尾按既有 ABI 合同拒绝；
文本诊断仍使用调用方有界缓冲，独立于 POD 和其它线程的调用方缓冲。

close 失败的资源身份只退休一次，绝不按数字 FD 重试，也不以 F_GETFD 推断身份。
未发布构造没有内部执行者、Host 回调或未来完成记录；没有后续使用者的引擎、
队列、mutex/condition 等存储可以释放。原始 kernel 资源仍未知时明确报告
INCOMPLETE/CLEANING 和不确定数量。POD 是错误与不确定性记录，不是资源句柄、
清理 ticket、资源已经释放的证明或调用方可以重关 FD 的授权；SDK 没有安全的
身份重试动作。已发布实例则继续保留 CLOSED 控制存储，未知资源阻止 quiescent
和 destroy，后续 quiesce 也不覆盖首次清理错误。

## 实际红绿

全部新原件位于本树 `out/m2-06-creation-fix/`；原 06 输出与主树冻结诊断未改。

1. `original-red.*.log`：实际重放冻结 tracer，退出 1，accept EMFILE 后 close
   EACCES 时公开旧诊断只有 CONTROL_FAILURE，errno 观察被覆盖且仍有 1 个 socket。
2. `slice1-red.*.log`：新增公开 C 合同对冻结 SDK 头实际编译失败；首片供应
   `slice1/build.json` 后 `slice1-green/report.json` 通过，普通创建拒绝完整回滚，
   setup 原始 EMFILE 独立保留。
3. `slice2-red/report.json`：listener close 拒绝实际执行失败在独立 cleanup
   字段断言。`slice2-green/` 转绿；`slice2-boundaries-green/` 同时覆盖
   accept EMFILE/close EACCES 和实际关闭后同号资源复用，均无二次关闭。
4. `slice3-red/`：已发布 getter 缺失导致真实编译红灯；`slice3-green/` 验证
   控制错误先发生与 stop 先发生两种首因顺序，关闭诊断各自保留。
5. `slice4-red/`：pthread 初始化返回值未进入公开 POD；`slice4-green/` 验证
   四个初始化拒绝阶段原始 ENOMEM/EINVAL，不从全局 errno 猜测。
6. `slice5-red/`：CLOSED 后清理时钟错误混入控制错误；`slice5-green/` 改为
   独立 cleanup 锁存，并验证后续时钟错误不能覆盖已有 close 错误或执行首因。
7. `public-diagnostics-final/`：包外正常合同通过大小、版本、零/非零尾、零/短
   文本缓冲、旧模式拒绝、结果保留及 CLOSED 后跨线程独立查询。

反馈 loop 的 candidate archive 在 Ubuntu 包外临时目录中从首片供应 archive
复制，再重新编译本树 Core 文件；每次记录明确的受控配方、输入和原始日志，
首片供应文件前后不变。它们是红绿过程，最终选择仅为下方 `final/`。

## 最终已提交供应与消费

产品修复提交 `90b73d8`，合入最新 integration `c835ab4` 后的供应源码为
`3019c3ff8f8a90bbd6a9fd1c00d43924a0a75959`。构建前 Windows Git clean；
`recipe-blobs.json` 记录全部 63 个 RECIPE 输入与该提交 Git blob 原字节相同，
0 mismatch，全部 LF。后加的本文不属于 RECIPE。

producer 的 `working_tree_dirty=true` 原样保留：Ubuntu Git 对既有 CRLF 的
非 RECIPE 文档观测为 modified，不将 Windows clean 外推为 Ubuntu 全树 clean。
未修改这些文档、全局 Git/WSL/mount 配置或主物理 `.cache` 布局。

| 原件 | 实际结果与范围 |
|---|---|
| `final/build.json` | Ubuntu 固定输入供应构建 pass，源码为上述 3019c3f，dirty=true |
| `final/linux.json` | Linux x86-64 native / WSL2 6.6.87.2 / 4096，112 必需项全部 pass |
| `final/windows.json` | Windows 10.0.22000 x86-64 native / 4096，111 必需 pass、1 可选 blocked |
| `final/package-consume.json` | 密封 ZIP 包外纯 C 应用，公开头/供应 archive，8/8 pass |
| `final/fault-consume.json` | 密封 ZIP 包外受控系统夹具，1/1 pass，含初始化/创建/已发布清理诊断 |
| `final/fault-windows/report.json` | 直接执行同一 Ubuntu 供应故障 APE，pass；不代表随机 OS 故障覆盖 |
| `final/tracer/report.json` | 独立公开 SDK tracer，Linux 实际 2 次 × 4 模式，全部 pass |
| `final/tracer-windows/report.json` | 同字节 tracer，Windows 实际 2 次 × 4 模式，全部 pass |
| `dev-check.json` | Ubuntu dev.py check pass；364 tests / 83.843 秒，188 条件 skipped；真实 C bootstrap pass |

Windows 可选 blocked 为 `qaplet-host.module-symlinks`、required=false，
未获得真实文件 symlink fixture 权限，不计通过。正常 catalog 从 111 增至 112，
新增 `qaplet-host.m2-diagnostics` 为必需项，两平台均实际执行通过。
普通仓库检查中的 188 skips 不代替产品或平台验收。

独立 tracer 原始观察如下；四种调用均返回 CONTROL_FAILURE / outNULL。

| 模式 | 主因 domain/value | cleanup domain/value | SDK 回滚结果 | SDK 后活 socket / 不确定句柄 |
|---|---|---|---|---|
| accept 拒绝，close 成功 | errno / 24 | none / 0 | COMPLETE | 0 / 0 |
| listener close 明确拒绝 | errno / 5 | errno / 5 | INCOMPLETE | 1 / 1 |
| accept EMFILE + close EACCES | errno / 24 | errno / 13 | INCOMPLETE | 1 / 1 |
| 实际关闭后同号复用并返回 EINTR | errno / 4 | errno / 4 | INCOMPLETE | 1 / 1 |

最后一种活 socket 是 fixture 自己创建的不同身份；其余负向模式由 wrapper 明确
拒绝关闭原对象。每个 listener 只有一次 SDK close 尝试，fixture 最后只清理自己
已知的对象。fixture 的清理不是 SDK 回滚完成，不能把这三个负向结果称作资源
全部释放。冻结旧 tracer 的“必须全部关完”红灯仍保留为 OS 拒绝的事实；新公开
tracer 修复的是诊断丢失与错误的完成表达，没有伪造物理释放。

最终配方 SHA-256：
`282322b29cddf0e22155e51b9629d44e20019de311f2cd130eb3a6f4b97cfa4e`。
SDK 集合 SHA-256：
`857e801044429a6df7f9462f2cfcad5d13d6dfb97b1927ac0f4c4b35ac66b84c`。
供应 ZIP 为 14,282,207 bytes，SHA-256：
`f1fd5ef71876e3561faed383968919687c1e3100ec8656a3f4756bcfaf19be10`。

| APE | bytes | SHA-256 |
|---|---:|---|
| Host | 5,118,234 | `d3ccf348435634fdb4888f763a15d3fc4d16122109c55f5de40d756d28bb82be` |
| CLI | 2,428,530 | `aa7b174f7e6d8c867727e00265ba68a23e716963fc0d5fb2c2500d8ee8004688` |
| 完整 C 应用 | 2,435,311 | `d1c6fdd425915ce2e6e3172c74bb13b5aa00ef99a5b67cc2279661b15248d9f8` |

## 入口与保留限制

实际 argv、时刻、外部 cwd、输入/源/工具/SDK/APE/loader 身份、前后校验、
原始 stdout/stderr/退出均在相应 JSON 和 evidence 目录。构建统一在 Ubuntu，
Windows 只执行供应字节。主缓存固定在 `/mnt/d/Workspace/dairn/qaplet/.cache`。
正常 SDK build/run/package-consume/fault-consume 使用现有 `scripts/sdk.py`；
本次最终输出前缀为 `out/m2-06-creation-fix/final/`。一次性独立入口为
`out/m2-06-creation-fix/tracer.py`，源码为同目录 `creation-tracer.c`。

dev.py 首次仅指定 report，原始命令日志按入口默认写到本树 `out/dev/<uuid>`。
`dev-check-evidence-original/` 是这 18 个文件的逐字节保存副本，
`dev-check-evidence-map.json` 保留原路径→保存路径、摘要/长度及全部一致的收据；
未改写原 `dev-check.json` 的真实路径。只读 Git shim 只映射当前 worktree
查询，其它 fixture/写操作透传真实 Git，不设置全局 GIT_DIR 或 core.worktree。

两个一次性入口设置错误均保留：首次 shim 路径替换多加后缀，SDK 报告开始前
Git 查询失败；修正 ignored shim 后构建通过。首个正常候选 runner 用错
standalone 宏而链接缺 main，原件在 `public-diagnostics-green/`；修正为既有
SDK_STANDALONE 后通过 `public-diagnostics-green-fixed/` 与最终普通 catalog。

本次未取得新 macOS 两架构、Linux ARM64 native、实际 ARM64 64K 或合并后
Full 成绩。旧 `1708ede` Full 的成功子范围和 guest 失败仍保留，不外推到新
112 项产品。root 将机械集成另一条 guest 修复，再对当前合并点签发新 Full。
未 push、未做整体 Standards/Spec 审查、未 resolved 06，也未签发 release-ready。
