# 正式 M2 实施记录

**Execution state:** in_progress
**Integration branch:** `codex/m2-integration`
**Product baseline:** `fdd5e82`；已完成 M1 产品及工具。
**Approved-input checkpoint:** `5685bb6`；保存用户提供的规格、27 票及入口文档修改。

用户于 2026-11-03 调用 `/implement-spec`，随后要求中文工作。沿用已确认的公开测试边界及依赖图，不重新访谈。

## 执行约定

- 每票使用独立分支与 worktree，完成后先合并集成分支，再由独立合并代理集成。
- 构建仅在 Ubuntu；本机通过已安装的 `C:/Program Files/WSL/wsl.exe`、`Ubuntu-22.04` 进入。受限进程报告发行版不存在，提升执行上下文后实际可用；不据前者推断系统未安装。
- 共享可复用缓存为主工作区 `.cache/`。报告及制品使用各票独立输出路径，不以计划、旧 M1 或探针结果替代当前产品验收。
- 逐票保留红绿测试、当前源码/配方/制品及宿主身份、原始日志、失败历史和真实缺口。必需证据不足时不关闭该票或放行依赖。
- 所有票集成后才执行一次整体 Standards/Spec 独立审查；先生成并核验分轴材料。

## 当前分派

| 票号 | 分支 | worktree | 实施状态 |
|---|---|---|---|
| 01 | `codex/m2-01-runtime` | `.cache/worktrees/m2-01` | `0945484` 已集成，待 Cross |
| 02 | `codex/m2-02-waker` | `.cache/worktrees/m2-02` | `8fd870e` 已集成，待 Cross |
| 03 | `codex/m2-03-pipe` | `.cache/worktrees/m2-03` | `dfac18d` 已集成，待 Cross |
| 04 | `codex/m2-04-process` | `.cache/worktrees/m2-04` | 实施中 |
| 05 | `codex/m2-05-tty` | `.cache/worktrees/m2-05` | 实施中 |
| 06–27 | 待分派 | 待创建 | 等待各自直接 blocker |

## 环境事实

`out/qaplet-m2/context.json` 记录实际上下文：Ubuntu、Linux x86_64、WSL2 kernel `6.6.87.2-microsoft-standard-WSL2`、4096 页、uid/gid 1000、构建允许。该报告只证明执行环境，不证明产品合同通过。

`out/qaplet-m2/guest-inputs.json`：现有固定 64K guest 输入经离线签名、长度及摘要核验通过，`qemu-system-aarch64` 已安装；尚不表示新探针已经执行。

## 公共 CI 接入

02/03 的独立 producer、Linux Fast、五原生供应消费及 native-first 64K guest 路径已接入既有 Fast/Cross/Full。各探针保留独立配方、制品和日志，不改变旧 M0 原生 I/O 配方。实现尚在独立分支中时不推送执行此编排；待源码集成后取得实际成绩。

已执行公开 `build_ape.py check-ci` 边界的红绿测试：删除任一 probe 的 build/native/guest 路径或在 consumer 重编译共八种损坏，新增检查均会拒绝。随后 Ubuntu `scripts/dev.py check` 通过，报告 `out/qaplet-m2/check-ci-wiring.json`，334 个工具合同测试中 165 个需要显式供应输入的用例未执行；此处不将其记为产品/平台验收。bootstrap 和输入锁预检通过。

为在正式集成点取得规格要求的即时 Cross/Full，`codex/m2-integration` 的实际 push 直接选择 Full；其他普通分支的 Fast 及可信缓存规则不变。该选择通过 CLI 红绿合同验证，保留实际 push 事件身份。GitHub 连接器没有工作流 dispatch 操作，本机 Chrome 的工作流入口与一次重载均为 `ERR_CONNECTION_CLOSED`，不通过新 PR 或 RC tag 触发测试。

分支选择增量的 Ubuntu 完整本地检查通过，报告 `out/qaplet-m2/check-integration-tier.json`（同样 334 项、165 项需显式输入而未执行）。这两个本地检查都没有代替新 SDK、探针或远程 Full 的实际执行。

01 的独立 Ubuntu `sdk.py fault-consume` 已接入必需 Fast 步骤，使用独立 `fault-consume` scope、受控夹具标记及制品，不混入正常 Host catalog。删除该步骤的公开 CI 合同先红后绿。Ubuntu 仓库检查通过：`out/qaplet-m2/check-creation-fixture-ci.json`，341 项、169 项需显式输入而未执行；没有宣称故障夹具具备尚未实现的跨宿主消费入口。

## 当前已保存的逐票证据

02 的同一 APE 在 Linux x64、Windows x64、实际 ARM64 64K guest 各 19/19 通过，两原生目标的 CLI 合同各 6/6 通过。macOS 两目标和 Linux ARM64 native 尚待远程 Cross；票保持 `awaiting_cross`。[逐项原件与后端结论](waker-reactor-validation.md)保留 Windows failed-connect 的 `poll` / `select` 差异。

02 输出已停止写入。主工作区 `out/qaplet-m2/tickets/02/` 已保存完整去重归档；`store-02-plan.json`、`store-02-created.json`、`store-02-verified.json` 记录 308 文件、53,224,414 逻辑字节及完整原件核验。源 worktree 仍保留，保存收据不单独授权删除。

01 的 SDK 空实例及兼容生命周期、03 的 pipe/stdio 真实探针已分别通过独立机械合并进入集成分支。01 本地正常 SDK 目录 Ubuntu 105/105、Windows 104 pass + 1 既有可选 symlink blocked；包外消费者和 Ubuntu 独立故障夹具通过。03 在 Linux x64、Windows x64、实际 ARM64 64K guest 各 19/19，Linux/Windows 的 CLI 合同各 4/4，通过后的限制及首次失败仍见[01](01-runtime-validation.md)、[03](03-stdio-validation.md)。三票尚未放行完整 Cross 或依赖。

01 初次保存核验 1,213 文件、202,193,960 逻辑字节，03 保存核验 359 文件、57,679,949 逻辑字节，收据在主 `out/qaplet-m2/store-01-*.json` 与 `store-03-*.json`。01 合并后另增加一份 1,251 字节的窄 CI 收据，因此保留初始归档，再将含该收据的最终输出树保存至 `tickets/01-after-merge/`；清理前以最终保存及遗漏输出审计为准。

01 合并后保存及原件核验已通过（1,214 文件、202,195,211 逻辑字节）。三票首次集成的 Ubuntu `scripts/dev.py check` 通过：`out/qaplet-m2/check-initial-integration.json`，345 项工具合同、171 项因需显式输入而未执行，bootstrap 通过。当前代码准备推送 `codex/m2-integration` 执行实际 Full；本地检查不替代平台矩阵。

## 一次性测试入口修复

01 首次 Git shim 为 `git init` 夹具误注入 worktree 环境，向共享 `.git/config` 写入 `core.worktree=/mnt/d/Workspace/dairn/qaplet/.cache/worktrees/m2-01`，使 Windows Git 报 `/mnt` 无效。根代理与独立 merger 先用显式 git-dir/work-tree 确认集成分支 `791af3d` 且干净，保存 `out/qaplet-m2/git-config-before-worktree-repair.txt` 后，仅按精确值移除该项，普通 Windows Git 恢复。01/03 的临时 shim 已改为仅当前仓库的只读查询，其他 fixture 和写操作使用真实 Git；修正后的本地检查分别通过。没有改写产品或历史验收以掩盖此次失败。
