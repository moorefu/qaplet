# 09 · HostNotify 与宿主自有循环

本票从干净的 `4d86901` 开始，worktree 为 `.cache/worktrees/m2-09`，
分支为 `codex/m2-09-host-notify`。沿用 Q14/Q49/Q53 确认的公开 C SDK、
真实宿主等待、包外纯 C Host 与受控系统边界；没有重复访谈。
本地验收不代替新的五原生/实际 64K Full，父 M2 不完成。

## 产品合同

- 新能力为 bit 20 `QAPLET_CAP_HOST_NOTIFY`。库查询支持，M1 实例禁用；
  M1 结构、错误数值、创建行为及已有 M2 配置/快照布局不变。
- `qaplet_set_host_notify` 在 OPEN M2 的 owner、首次执行/有效 pump 前一次性
  注册，复制版本化声明，拒绝替换、空函数、未知字段、错误线程和冻结后的注册。
- 固定控制存储从 Runtime 首次分配计入原生预算，通知不分配、不创建线程、
  不导出内部 fd/HANDLE。发布锁下取得有限计数的在途 lease，锁外在发布线程
  同步调用 native hook。新独立 `qaplet_notify_snapshot` 只公开 installed、
  inflight 与峰值，不公开代码/userdata 指针，也不把通知数当业务量。
- 事件接受、stop、令牌释放、显式 wake 与当前清理进度通知已经接入；
  内部私有 waker 保持独立。后续 work/process 完成必须在其真实 provider
  发布点接入，不声称本票已经交付这些完成能力。
- hook 必须快速返回、不阻塞、不执行 JS、不调用任何 SDK。SDK 的通知线程
  TLS guard 令违规重入返回 BUSY；普通 Host Module 的既有白名单不变。
- close 不等待外部 hook；已接受、尚在途的 lease 保持 CLEANING，有限 quiesce
  可超时重试，destroy 返回 BUSY。只有 lease 归还且内部保留、句柄和 uncertain
  计数都清空才可 QUIESCENT。既有原始主因/control 与独立 cleanup 诊断不变，
  不重关可能已复用的 fd，不把 CLOSED 改回 FAILED。
- 代码/userdata 同时保留至 native QUIESCENT 和外部生产者撤下、在途调用 join；
  零 inflight 无法证明没有未来调用，close 返回本身不授权卸载。

## 红绿与失败历史

- `out/m2-09/slice1-red.log`：公开 C Host 对原头实际编译失败，缺少 HostNotify
  类型、宏和注册入口。首片供应后 `slice1/linux-2.json` 实际 113 项通过。
- `slice1/linux.json` 使用了错误的显式 loader 目录，报告 blocked /
  `Distributed loader missing`，没有产品执行。随后显式选择已验原生 loader；
  没有自动搜索或修改旧报告。
- `slice2-red.log`：对首片冻结 SDK 头实际编译失败，缺少独立通知 POD。
  第二片 `slice2/linux.json` 的 115 项通过，新增注册、线程、stop 和 silent-hook
  内部等待独立性；`slice2/fault.json` 包外受控系统夹具通过。
- Windows 受限进程中的第二片宽运行无输出，已作为未完成中断，原始日志保留。
  窄 A/B 的 `windows-diagnosis/report.json` 中，旧已验 CI Host、两片新 Host 的
  `--facts` 和新 CLI `--version` 均实际 5 秒 timeout；这些入口不需要通知 API。
  正常用户上下文的 `windows-diagnosis-normal/report.json` 对相同摘要四项均
  约 10ms、exit 0。没有基于未归因猜测修改 TLS 或产品；
  `slice2/windows-normal.json` 完成 115 项目录的消费。
- 受限 runner 的停止只清理本任务启动的 runner/Host 进程，PID/路径记录在
  `slice2/windows-interrupted-processes.json`。进程监督中断不算产品 close 成功。

## 已提交供应与本地验证

产品提交为 `f0feb5ce9693c27893f60a892d7f204e564a3cac`。最终构建前已合入
当时最新 integration（仍为 `4d86901`），`recipe-blobs-final.json` 核对全部
64 项 RECIPE 与提交 Git blob 同字节、CRLF 为零。Ubuntu producer 的
`working_tree_dirty=true` 原样保留，不能将 Windows Git clean 外推为 Ubuntu
非 RECIPE 文档也 clean；没有改动全局 Git、WSL/mount 或主物理 `.cache` 布局。

最终原件根为本树 `out/m2-09/final/`：

| 原件 | 真实范围与结果 |
|---|---|
| `build.json` | Ubuntu 固定输入供应 build pass，64 项 recipe；SDK/双 archive/密封 ZIP/三 APE 审计通过 |
| `linux.json` | Linux x86-64 / WSL2 6.6.87.2 / 4096，115 pass；实际 M1 1,000 次生命周期 |
| `windows.json` | Windows 10.0.22000 x86-64 / 4096，114 pass、1 可选 symlink blocked；必需缺口 0，实际 M1 1,000 次生命周期 |
| `devcheck.json` | Ubuntu 完整 check pass：368 总、177 实际通过、191 显式输入条件 skip；真实系统 C bootstrap pass |
| `package.json` | 最终密封 ZIP 在 `/tmp` 包外只用公开头/供应双 archive 编译正常应用，8/8 pass |
| `fault.json` | 最终密封 ZIP 包外系统 fixture，1/1 pass；通知在途/close 两序、违规重入，以及已安装 silent hook 下原始首因/uncertain fd 保留全部通过 |
| `fault-windows.json` | Windows 执行同字节 Ubuntu 故障 APE，1/1 pass；不算普通原生随机 OS 故障覆盖 |
| `notify-package.json` | 最终密封 ZIP 包外独立纯 C Notify Host，4/4 pass；包括 100 组进程内重复、12,800 个业务事件及显式 join/close/quiesce/destroy |
| `notify-package-windows.json` | Windows 执行同字节包外 Notify APE，4/4 pass；同样实际 100 组、12,800 事件，退出 0 且未 timeout |

`sdk_recipe.sha256` 为 `2f20682a0062e3d3fe82df27108e42483380acdade51d4cf098181302fc6daa4`，
SDK aggregate 为 `438218cdc56b89a9c5a3dbb2ec690c36bd0d2c398b08707da0a46781b4d9e766`，
供应 ZIP 为 `79d327bed92dfd0283e64319ed4f3940068494ef4997ea8851da9ff1ce740fa5`。
同字节 Host APE 为 `0bd87bea8c5975b4fbf8a1ac4e4a3a2c3d400ea083931be631eb3fc74375a785`；
CLI 为 `3ddaebe0c7b4e445e2ab70e4b0f0fb2a9da8e651070f9c4878f1ba92aca93fcc`。
全部实际命令、raw stdout/stderr、时限、SDK/APE/loader/源码身份与调用清理结果在原报告中。

包外消费者的实际 cwd 均由 producer 明确证明在两源码树之外，原 ZIP、公开头、
archive、fixture C、一次性 main、编译/链接/审计参数、实际 loader 及 before/after
身份均保留。`out/m2-09/notify_consume.py` 是一次性验证 adapter，编译实际 ZIP 中
`sdk/host-notify.c`，不包含引擎/私有头、不重新编译 SDK。100 组仅代表本票资源范围，
不冒充最终 T03 全资源混合；M1 的 1,000 次循环保持原有含义。

Windows 额外消费者初次在成功执行 fault 后，因 normal example 用例使用
`commands[]` 而非 `command` 抛出 KeyError，通知 APE 当时尚未启动。
`extra-windows-driver-red.json` 与首个 `fault-windows.json` 保留原结果；
一次性 adapter 接受既有记录形状后，只重跑未启动的通知消费者，4/4 pass。
没有改产品、扩大允许输出、修改 timeout 或补造执行。

## 当前边界

普通 Host hook 仅做非阻塞 send，不运行 JS、不调用 SDK、不等待。Host 使用自己创建的
socket 和真实 poll 收提示，再由 owner pump；128 个复制事件按公开 FIFO 和 payload
结果核对，合并/多余提示不改变数量。silent hook 下的内部 run_once 仍由跨线程令牌
释放唤醒。受控 fixture 单独在 Host send 系统调用处暂停生产者，覆盖接受事件与 close、
CLOSED wake 与 close 两种交接；观察 JS 已释放、原始事件被丢弃、有限 quiesce pending、
destroy BUSY，然后显式放行/join 后检查 QUIESCENT/destroy。另一个明确违规 hook 只用于
SDK BUSY 拒绝负例，不把它描述为允许的 Host 用法。

本票本地完成，保持 `claimed / awaiting_cross`。新的五原生、实际 64K 和最终 T03 全资源混合/长测仍未执行；普通 check 的条件 skip
没有被当成这些范围的通过。`release_ready` 不签发。没有 push、整体审查或关闭父规格。

2026-11-04：独立 merger 已将源 tip `4615525` 以 `2c44fc3` 合入集成分支。`out/qaplet-m2/merge-09/post-verification.json` 核验全部 64 项 RECIPE 原字节/源与合并 Git blob/LF、71 项供应文件、密封 ZIP 及双 archive 各 12 个 ELF64 REL 成员一致，保留 producer dirty=true。补充原始日志重放为 `evidence-verification-006.json`：Linux 115、Windows 114 pass + 1 optional blocked，包外应用 8、独立通知 Host 两宿主各 4、受控故障两宿主各 1，必需缺口 0；完整本地 check 368 项中实际 177、条件 skip 191 及 bootstrap 通过。此处重放不计新执行。

此前一次性检查器分别错误处理 null 时限、Windows 无 loader 文件的原生 PE 路径、包外命令中附带的 APE 辅助编译；修正记录与原脚本均保留，第三次替换未匹配 LF 也单独记录。继续核验发现原 `fault-windows.json` 引用的 driver 随后为另一消费者修订，原报告的 driver 字节已不匹配；该身份缺口没有豁免。冻结 `run_extra_windows_002.py` 后对同一故障 APE 实际重跑 1 项通过，生成 `fault-windows-002.json`，新重放显式选择该原件并保留旧报告、旧摘要及缺口。产品源码、供应 APE、timeout 和判据未修改。新五原生及实际 64K 仍待当前集成 Full，票保持 awaiting_cross。
