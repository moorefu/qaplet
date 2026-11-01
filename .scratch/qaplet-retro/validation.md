# M0 复盘改进实施验收

**Status:** resolved
**Branch:** codex/m0-retro-workflow
**Base:** e302bc8b2e44f67c44ad2b6f1d19d44eec9e4f8d
**Reviewed code:** 4113ced7ca637ff5196698e343450d8516216a83

## 已交付

| 复盘项 | 落点与实际行为 |
| --- | --- |
| 严格证据回归 | `scripts/check_evidence.py` 显式配置实际完整Core、4K guest和五producer；复用已有consumer tests，缺数据/失败/error/必需skip/未完成均失败；可先只运行preflight |
| 隔离审查 | `scripts/review_packet.py` 固定base/head、保存Git或明确文件快照、生成并校验摘要；独立report与aggregate按轴排除，规范reference改动在两轴diff中保留 |
| 证据操作 | `scripts/ci_evidence.py` 显式fetch、完全离线verify、按kind汇总、过滤保留run结果；检查归档SHA/长度、逐文件CRC、路径、symlink、重复和解包限额；目标未完整验证前不写报告 |
| worktree执行 | `scripts/dev.py` 从Git主worktree解析物理主`.cache/`，自动传给既有check/build/offline入口，编译入口要求Ubuntu |
| 便宜预检 | 已跟踪生成输出、Python语法、工具CI合同、所选recipe工作/暂存字节先核对；offline先真实namespace/降权写入preflight，之后才运行原双构建 |
| 按需导航 | `docs/development.md` 39行入口，环境/CI/证据/产品探针分流；旧ENV-01/Core anchors与历史验收保留，AGENTS仅增加审查导航 |

新增 `.github/workflows/workflow-tools.yml` 在Ubuntu先preflight再运行公开CLI合同，并保留失败报告。原Makefile、构建脚本和22个已验收recipe inputs字节保持不变；本地实施未重做APE构建、断网pair或Windows服务生命周期。推送还会触发原M0/SVC workflow，其Ubuntu构建与托管宿主验证按实际run独立记录。

## 实际验证

- `out/retro/evidence-regressions.json`：真实保留数据执行32/32 consumer regressions，0 failures / 0 errors / 0 skips；完整日志和逐测试progress在其UUID evidence目录。该回放不代表新的OS执行。
- `out/retro/check-release.json`：新dev入口调用既有Ubuntu `make check`，165 tests / 0 failures / 50明确平台或数据opt-in skips，静态合同与真实系统C bootstrap通过。该普通profile不代替上述32项严格回归。
- 实际required-skip负向：临时源码副本给既有ActualCoreEvidence加skip，仍消费真实数据执行preflight，严格入口正确失败；普通CI无此数据时仅该专用集成回归明确skip。
- 最终工具合同显式提供真实证据时16/16通过、零skip；普通CI保留该实际数据集成用例的opt-in边界。
- `out/retro/collected.json`：新工具从主cache离线核验实际最终CI的14个ZIP、CRC及694份解包文件。真实回环HTTP验证显式获取、命中缓存、verify不下载、坏缓存不自愈；ZIP traversal/symlink/大小写碰撞/坏CRC均拒绝。
- `out/retro/core-summary.json` 正确计数175 cases / 617 evidence items，声明evidence_verified=false；`run-overview.json` 从保留响应提取精确提交的4条实际成功结果，network_requests=0。
- `out/retro/namespace-preflight.json`：新offline wrapper只执行已有实际preflight，worker UID/GID1000、独立netnamespace、仅lo、literal外连ENETUNREACH101；未执行后续pair。`offline-plan.json`仅为明确标识planned的命令计划。
- `out/retro/tooling-preflight.json`：主物理cache、生成输出跟踪、Python语法与新CI合同通过；22个recipe工作字节和暂存Git blobs匹配已验收receipt。

全部新增合同经过公开CLI、真实临时Git/worktree/文件系统/回环HTTP边界验证；未用模拟正向OS结果代替平台证明。输出、测试数据和原始M0证据继续在Git忽略目录。

## Standards

初审一项P3 judgement：重复Git执行/错误处理。已合入共享`workflow_support.git_read`，没有增加中间转发层。

最终复审（仅本轴材料）：0项硬性违规，0项未解决Fowler启发式发现。固定`4113ced`，仅使用`out/retro/reviews-v3/standards`；新增目标验证状态和负向回归未引入新规范问题。审查为只读静态检查，未读取另一轴或聚合报告，未运行构建或探针。

## Spec

初审P2：无效63字符摘要在目标检查期间抛错，旧错误分支可能将failure JSON写入原ZIP。临时ZIP真实red已重现；现只有guard完整成功后才置target_validated=true，所有目标检查异常均只stdout，原ZIP字节保持不变。

最终复审：0项遗留。已逐项核验`out/retro/reviews-v3/spec`的材料摘要并只读检查修复控制流和精确触发回归；未发现新的缺失、错误行为或scope问题。执行结果基于提供的实际验证事实。

两轴最终均零遗留，各轴均无未解决严重项。完整材料、原始失败与收据位于`out/retro/`；远程最终提交的实际CI状态另存该目录的ci-summary.json。
