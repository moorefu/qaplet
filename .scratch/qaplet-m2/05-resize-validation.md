# 05 · 当前 Windows ConPTY resize 回归

2026-11-04：本地修复已完成，票保持 **claimed / awaiting_cross**。正常同字节供应物在 Windows、Linux 和实际完整系统 ARM64 64K 通过；新的 Windows Server 原生 Full 仍待集成验证。旧 `0e4ba21` 验收、当前 `563ff33` 的失败及原 ZIP 均保留原范围。

## 原始失败与诊断边界

[Full 41788055889](https://github.com/moorefu/qaplet/actions/runs/41788055889) 的源码为 `563ff3317d92776ae240a636e513b047f2fd64f7`，Windows job `126437030325` 的唯一失败为 `tty.resize`：initial 80×24、latest 100×33、queried 90×31、notifications 2、mode_restored=true。controller 两次 ResizePseudoConsole 均 HRESULT=0；child exit256，无外层 timeout，error=null，process/ConPTY/handles/reader 四项清理全 true。

原 ZIP `13302417443` 及 `tty.json`、`console-results.ndjson`、`conpty.stdout.log` 保存在主工作区 `out/qaplet-m2/full-563ff33/`。root 本机对当前原 APE 的 420 次真实 ConPTY 执行未复现，原件位于 `out/qaplet-m2/05-resize-diagnosis/`。这些成功不消除 CI 失败。公开 consumer 正确拒绝 CI producer 与本地 ci=null 的混用；后续直接 backend 诊断明确为 local scope，没有改写该 CI 身份。

本次在独立 worktree `.cache/worktrees/m2-05-resize-fix`、分支 `codex/m2-05-resize-fix` 实施，初始集成点 `e1f25ad`，修复提交 `527bfd8d0416e51a191d152dc800a4edde302ff8`；随后合并含 07 的最新集成 `fa4d768`，合并点 `99b685f`。没有 reset、修改主树、修改 M0 或增加 Runtime/SDK TTY API。

以下新原件均相对于本 worktree 的 `out/m2-05-resize/`。正常供应物在 `candidate/`，受控顺序供应物在 `controlled/`，诊断及历史 red 在 `diagnosis/`；后两者不代替正常宿主证据。

## 先建立能变红的反馈循环

已确认测试边界为真实 backend 探针及受控系统边界。诊断 header 先取得真实 90×31 查询快照，通过文件握手让 controller 执行第二次真实 ResizePseudoConsole，再等待同一 HANDLE 的真实查询已达到 100×33，最后返回先前取得的快照。后续 C 探针仍读取真实 WINDOW_BUFFER_SIZE_EVENT；没有修改 JSON、stub API 返回值或合成通知。

初始 header 由 `--cflag=-include` 显式注入 Ubuntu 诊断构建，header、driver、controller、供应物和实际宿主身份分别记录。最小触发变化仅为将两次 resize 分隔在该真实查询两侧，其他 Console 阶段均保留并成功。先实际运行：

```powershell
python out/m2-05-resize/diagnosis/loop.py `
  --producer out/m2-05-resize/diagnosis/red-build.json `
  --report out/m2-05-resize/diagnosis/red-001.json --count 3
```

0.60 秒内 **3/3 精确变红**：latest100×33、queried90×31、notifications2、mode_restored=true、exit256，且四项清理均 true。对应 raw、stdout、controller 动作和逐次目录均保留。新增 `test_console_resize_uses_size_after_final_notification` 再对该未修复供应物实际 red，0.182 秒，失败断言和原症状见 `diagnosis/regression-red.log`。

这证明旧探针在实际操作跨过其尺寸快照后，会用旧快照判定较新的通知。它不证明原 Windows Server 上未记录的具体调度顺序；受控 driver 和配方身份始终单列。

## 预先排序的假设及实验

在上述 red 后、修改判据前已向用户展示四项可证伪假设：

1. **决定性尺寸快照早于最终通知。** 预测：同一 HANDLE 在最终通知之后再查询，原差异消失。旧代码确实先 query 后 drain；控制顺序 3/3 red，修复后仍实测早期90×31，而最终查询100×33，20/20 green，支持此原因。
2. **一次排空没有等待最后的 resize。** 预测：省略或延后最后通知时，不能提前判成功。原 CI 已有两条通知且 latest100×33，故缺通知不能单独解释该次精确症状。新增有界事件等待；真实省略最终 resize 的实验记录 latest90×31、notifications1、内部等待耗尽、query_after_final_notification=false、exit256，仍完整恢复和清理，没有外层 timeout。见 `diagnosis/missing-final.json`。
3. **controller 参数、ABI 或 HRESULT 与真实操作不符。** 预测：同一输出 HANDLE 无法真正达到100×33。受控实际动作均 HRESULT0，早期及最终查询分别实测90×31、100×33；本循环不需要此原因即可精确产生旧失败。
4. **恢复租约或句柄关闭改变了查询对象。** 预测：同句柄且在恢复之前的最终查询仍不一致。新的最终查询在 release 之前执行，同一 HANDLE 正确返回100×33，完整模式恢复和清理仍通过；本循环不需要恢复/关闭异常即可产生旧失败。

## 修复与保留的失败判据

Windows probe 保留 `before_notification_cols/rows` 作为早期真实观测，等待最终100×33通知后才执行决定性 GetConsoleScreenBufferInfo。等待使用 Console HANDLE 与一个固定的 2 秒单调时钟预算，不用 sleep 猜测 resize 完成；缺通知、查询失败、等待失败或超时仍为 fail，并继续恢复模式及关闭资源。最终 latest 与 queried 都必须是100×33，没有降低判据。

consumer 额外要求 Windows `mode_restored=true`、`query_after_final_notification=true`、`resize_wait_timed_out=false` 和实际通知类型。POSIX 的独立 PTY、前台 owner、退出前 query 和完整恢复合同保持原范围。

`tests/native/m2_tty_resize_order.h` 仅在 `-DQAPLET_TTY_RESIZE_ORDER_FIXTURE` 构建启用；normal build 仍绑定其配方字节但不启用行为。私有 ConPTY helper 的显式 `resize_order_fixture=True` 只用于受控测试，公开 run 继续使用正常 controller。新测试消费独立 Ubuntu 供应物，并验证真实早期90×31、最终100×33、退出及所有清理，不依赖私有 SDK 布局。

## 当前供应身份

`candidate/source-verified.json` 逐一核对全部 12 个 source/recipe 文件：供应 producer 摘要、工作区原字节和提交 `527bfd8` 的 Git blob 完全一致，全部 LF；source/build 聚合摘要也重算一致。合并最新 07 后这些配方字节没有变化。

| 供应物 | 正常 `candidate` | 受控 `controlled` |
| --- | --- | --- |
| APE bytes | 980728 | 980729 |
| APE SHA-256 | `73f7dd19c38618b31da5c215b1284a1e28a2676cf69403177b0847c77561179e` | `b4d972c7f547e130ade4e56db4148356f3c9035e316d5473357311ed194158a3` |
| source SHA-256 | `0f7b9df87a64a57ef3c69552da82f0d7e05f1f2c6a94c5513ffc6ba2ffdb5ae9` | 相同源码，独立 flags/制品 |
| build SHA-256 | `c833e2856cd001317bf276637e0268ca3d4c7278c8389aa7df8d660eb0537e80` | `d743dec97c815901e9cda6507e1bfd9dc59f1b1aa870580e6c9789d0fd771087` |

两份 build 均仅在 Ubuntu 执行固定输入的前后校验、原 ZIP/工具链树核对、fat ELF/PE 审计和 source 稳定性检查。复用主工作区 `.cache/toolchains/cosmocc` 的已验 official-assimilate 树、固定 helper 源及 `.cache/build`，没有切换工具链或修改锁。共享 Git config SHA-256 仍为 `18de2af639cb0d28809b71bfa845293181862452ac47bab3e6d7ea3296422b19`；只读 worktree Git shim 不写共享配置或全局 GIT 变量。

## 已执行验证

| 证据 | 实际范围与结果 |
| --- | --- |
| `candidate/linux.json` | Linux x86_64 / 4096 / WSL native，19/19 pass |
| `candidate/windows.json` | Windows x86_64 / 4096 / kernel10.0.22000，21/21 pass |
| `candidate/guest-64k.json` → `candidate/arm64-64k.json` | 新同字节供应物，锁定完整系统 ARM64 / 65536 / QEMU TCG；实际执行、运输前后字节、raw、清理及独立 consumer 均 pass，19/19 |
| `controlled/regression-green.log` | 新受控供应物回归，0.180秒 pass |
| `controlled/loop-020.json` | 20/20，早期90×31、最终通知与查询100×33、exit0、无 timeout、四项清理全 true |
| `candidate/loop-020.json` | 正常 controller 20/20，最终100×33、exit0、无 timeout、四项清理全 true |
| `candidate/contracts-windows.json` | 9/9 pass，0 skipped；含新受控回归及全部现有公开 CLI 正负合同 |
| `candidate/contracts-linux.json` | 8项适用 CLI 合同全部 pass；仅 Windows 专属受控用例不适用，原始 unittest 明示1 skipped |
| `candidate/dev-check.json` | 合并最新集成后，原 `python3 scripts/dev.py check` pass；369项中183项执行、186项既有/平台或外部输入条件 skipped；本票8项Linux适用合同实际执行，统一锁、CI静态检查及真实系统C bootstrap pass |

两宿主 CLI 保留了缺供应 blocked且不编译、篡改字节启动前拒绝、输出碰撞、外层 deadline保持fail且回收、真实非法输入观察、四路径完整模式恢复和实际guest反向变体。guest 反向变体仍覆盖缺项、重复、假成功、缺/异模式字段、退出后query、取消就绪、payload变更，以及架构别名/错误架构，没有用合成成功替代原件。

## 交接与未测范围

票仍 **claimed / awaiting_cross**。所有成功均为本地实际范围，`product_support_verified=false`。新的 Windows Server 原生执行、两种 Mac 和 Linux ARM64 原生以及当前完整 Cross/Full 由集成分支重新供应后验收；不能由本机受控时序、旧成功或本地20次循环签发。原始 `563ff33` Full 继续为 failure，旧 `0e4ba21` 成绩继续仅证明其原范围。

本地没有遗留临时 DEBUG 日志；诊断脚本和历史供应物明确保存在忽略的 `out/m2-05-resize/diagnosis/`，没有删除或改写原失败。没有发布 SDK TTY API、修改网络产品、M0、公共 README、workflow 或实施总记录。


2026-11-04：`46e5cf4` / Full `41796608784` 已 completed/success。当前本票范围由独立 typed consumer 重读原件通过，并经 root 核验：SDK 五原生645/实际64K129、TTY五原生97/实际64K19、包外应用8、controlled fault1、当前M0 Full235；29原ZIP/12nested/11job日志及三guest seal/argv/清理完整。WinServer正常probe最终通知后实际查询100×33、模式恢复和四项清理通过，旧563失败不改标。详主 `out/qaplet-m2/full-46e5cf4/HANDOFF.md`、`verify/scope-summary.json`、`05-07-acceptance-check.json`、`root-scope-acceptance.json`。仅按各票scope验收，不外推最终T03混合/长测、父M2或发布完成。
