# M2 票据 01 实施与本地验证

2026-11-03，工作树 `.cache/worktrees/m2-01` / `codex/m2-01-runtime`，
开始基线 `5685bb6e26b9b181ccf68208c73f69e355ce7275`。
已实现不等于全部 M2 或平台验收；本票等待集成 Cross/Full，未标 accepted/resolved。

## 交付范围

- 不改变 M1 结构布局、错误数值和 reserved 语义；冻结基线公开头文件作为真正的旧消费者编译输入。
- 独立 M2 配置/创建、实例能力/有效限额快照、空实例 run_once/run、完整驱动 owner/重入保护。
- close 后 CLOSED 与 native 状态分开，0/有限 quiesce 可重试；创建系统资源逐阶段回滚。
- 后续原生 provider 的 retain/release 边界仅在物理退出/join 和存储退休后解除；CLOSED 不被后台首因写回 FAILED。
- 能力位仅 `M2_RUNTIME`。不提供或声明 timer、OS 等待、worker、process、stdio、未测 OS backend 或完整 M2 资源合同。

## 已确认 seam 与红绿

沿用父规格 T01/T02 已确认的纯 C SDK、供应 APE 与包外消费者，不重复访谈。
`tests/native/sdk_m2_runtime.c` 只包括公开头文件；`sdk_m1_compat.c` 只包括
冻结的 `tests/fixtures/sdk/m1-qaplet.h`。系统故障夹具只包装 pthread/clock 系统符号，
不读取私有 Runtime 布局、不使用私有分配次数，也不把故障产品当成正常产品。

- `out/m2-01/red-empty.log`：新增 M2 公共接口尚不存在，公开消费者编译红灯。
- `out/m2-01/red-drive.stderr.log`：run 自然空闲时错误返回 PROGRESS，修正后进展与原因独立。
- `out/m2-01/red-quiesce-guard.stderr.log`：quiesce 在单调时间准备前未设置 guard，受控系统入口重入红灯；保护提前后通过。
- `out/m2-01/green--m2-*.stdout.log` 和 `fault.stdout.log`：临时包外候选 SDK 的逐条绿灯；这些不替代最终正式供应物证据。

## 最终实际执行

输入/缓存根均显式为主仓库 `/mnt/d/Workspace/dairn/qaplet/.cache`；
Ubuntu 22.04 / Linux x86-64 / WSL2 kernel `6.6.87.2-microsoft-standard-WSL2` / 4096 页。
产品、双 CPU 静态 SDK 及所有 C 消费者只在 Ubuntu 构建。Windows 消费相同 APE/SDK 字节。

| 入口 | 实际结果与原件 |
|---|---|
| `scripts/dev.py check --output-dir out/m2-01/check-final` | pass；333 tests，167 明确 skip（缺显式远程/Full 输入的既有合同），CI 本地合同、input audit、bootstrap 均通过；`out/m2-01/check-final/workflow.json` |
| `scripts/sdk.py build` | pass；公开头文件、双 archive、完整 ZIP、正常 Host/CLI/application APE 均生成与审计；`out/m2-01/final/build.json` |
| Ubuntu `scripts/sdk.py run` | 105/105 pass，实际 M1 1000 次 create/close/destroy，M2 256 组公开 heap/native 预算及恢复；`out/m2-01/final/run.json` |
| Windows 用户上下文 `scripts/sdk.py run` | 104 pass，1 已知可选 `module-symlinks` blocked，零 required gap；同一 SDK/APE，实际 1000 次 lifecycle；`out/m2-01/final/windows-run.json` |
| `scripts/sdk.py package-consume` | pass；从实际 ZIP 包外解压，仅公开 header/archive 编译 application，8 项实际 case；`out/m2-01/final/package-consume.json` |
| `scripts/sdk.py fault-consume` | pass；实际 ZIP 包外单独编译/审计故障 Host，4 个系统初始化失败阶段释放所有已取得 mutex/condattr/cond，恢复创建正常；PENDING/CLOSED 整段重入拒绝；`out/m2-01/final/fault-consume.json` |

当前候选供应配方 `58de8fb5141c26d58f7cae0420de0a62da60181883b6e6b7d11281b4d0ba4a2b`；
SDK aggregate `37747d3b390211e2f2cf0aea8fd93eaf48a9dfbce9d8500d93d47c7b15fa6d05`。
构建记录为开始基线 commit 加 dirty recipe；最终集成提交须重建并获取自己的新身份，不外推这些候选制品为集成提交。

主要实际命令（均在本工作树）：

```sh
python3 scripts/sdk.py build \
  --source-cache /mnt/d/Workspace/dairn/qaplet/.cache/quickjs \
  --toolchain-cache /mnt/d/Workspace/dairn/qaplet/.cache/toolchains/cosmocc \
  --helper-source-cache /mnt/d/Workspace/dairn/qaplet/.cache/sources/ape-loader \
  --build-dir /mnt/d/Workspace/dairn/qaplet/.cache/build/m2-01 \
  --output-dir out/m2-01/final/artifacts --report out/m2-01/final/build.json
python3 scripts/sdk.py run --build-report out/m2-01/final/build.json \
  --artifact out/m2-01/final/artifacts/qaplet-host.com --loader-dir SUPPLIED_BIN \
  --report out/m2-01/final/run.json
python3 scripts/sdk.py package-consume --build-report out/m2-01/final/build.json \
  --artifact out/m2-01/final/artifacts/qaplet-host.com --loader-dir SUPPLIED_BIN \
  --output-dir out/m2-01/final/package-artifacts --report out/m2-01/final/package-consume.json
python3 scripts/sdk.py fault-consume --build-report out/m2-01/final/build.json \
  --artifact out/m2-01/final/artifacts/qaplet-host.com --loader-dir SUPPLIED_BIN \
  --output-dir out/m2-01/final/fault-artifacts --report out/m2-01/final/fault-consume.json
```

SUPPLIED_BIN 为报告中实际核验的主缓存原生工具链 `tree/bin`；全部精确 argv、
开始/结束时间、stdout/stderr、退出、输入与制品摘要保留在各 JSON 和 evidence 目录。
Windows 使用相同 run 参数但不提供 loader-dir，通过 `require_escalated` 用户执行上下文运行。

## 执行环境诊断与失败原件

- 受限 Windows 进程执行当前 Host `--m2-runtime` 在 15 秒无输出后由 supervisor 终止；
  `out/m2-01/windows-empty.json` 与原始空日志保留为未完成。相同字节在用户上下文
  `--facts` 0.006 秒成功，完整 SDK run 也通过；不把受限进程可见差异外推为系统不支持。
- Windows 创建的 linked-worktree `.git` 使用 `D:/...`，Ubuntu Git 需一次性只读路径转换。
  全局 GIT_DIR 曾污染 QuickJS cache Git 查询，已取消；最终 shim 只对当前 checkout 的
  rev-parse/status/ls-files/show/diff/log/worktree 查询转换，其他 Git 完全 unset 相关变量。
- 第一次 check 的 shim 错将 `git init /tmp/fixture` 绑定本工作树，产生 12 fail/4 error；
  还在共享 config 引入精确 `core.worktree=/mnt/d/.../m2-01`。root 已获知并单独保存原件、
  仅删除该会话引入 key；没有整文件覆盖配置。修正 shim 后完整 check 通过。
  首次失败 `out/m2-01/check/` 保留，不能冒充通过。
- 一次故障夹具草稿在持有 Runtime mutex 的 clock 包装点查询 POD 导致 supervisor 超时；
  夹具改为只验证必须在碰锁前即时拒绝的 owner 操作。最终正常与受控成绩均为实际正常退出。

## 尚未签发的范围

macOS x86-64/ARM64、Linux ARM64、真实 64K、集成 Cross/Full 由 root 统一获取；
本地不签发这些成绩。资源尚未接入，因此真实 worker/句柄保留、native 清理中等待、
未清理 destroy BUSY、清理错误与资源组合由首次资源票及 19/20 后续实际覆盖；
本票只建立边界，不用内部伪造 retain 冒充公开资源测试。无代码实现 blocker。
