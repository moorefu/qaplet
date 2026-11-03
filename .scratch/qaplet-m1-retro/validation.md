# M1 复盘工具改进验收

Status: resolved
Branch: `codex/m1-retro-tools`
Base: `32c7a908c360e1d36359b1da8f68239b50c264ae`

## 已实现

| 复盘项 | 实现 |
| --- | --- |
| 编码预检 | `scripts/text_encoding.py` 接入 `dev.py preflight`，覆盖首批证据工具、新 `evidence_*.py` 和全部合同 fixture；存量合同测试明确 UTF-8，不扩大生产者配方改动 |
| 保存与容量 | `scripts/evidence_store.py` 提供 plan/create/verify/restore/materialize，保守空间预算、不可变源、归档/元数据摘要、流 footer 校验和独立恢复；标准 tar.gz 兼容 Python 3.13，历史 tar.zst 由 Python 3.14+ 读取 |
| 路径派生 | `scripts/evidence_selection.py` 消费原配置、provider catalog、已验保存收据；生成 native/显式 WSL 变体及原始输入/输出/生成器摘要，保持 prepared 与实际验收分离 |
| 即时下载 | `ci_evidence.py fetch-actions` 逐件解析引用后立即下载，有限刷新、失败 attempt 留存、新报告路径及无 URL 本地 manifest；复用原 SHA/CRC/解包合同 |
| 小摘要 | 现有 summarize 支持 SDK、M1 gate、严格报告；身份、计数、缺口、有限失败片段，明确 evidence_verified=false |

工具合同通过原 workflow 的 `test_workflow*.py` 选择接入 CI。现有 Windows 目录别名 fixture 使用无特权 junction，继续检查真实目录别名；Ubuntu 使用原 symlink。

## 已完成的验证

- `out/m1-retro/check-accepted.json`：Ubuntu `dev.py check`，333 tests，零失败，165 项明确平台/实际数据 opt-in skips，实际系统 C bootstrap 通过。
- 最终工具合同：Ubuntu 27 tests，零失败/错误，1 项既有实际数据 opt-in skip；Windows 27 tests，零失败/错误，另有既有 POSIX FIFO skip，共 2 skips。新增 10 项工具合同在两平台实际执行。
- `out/m1-retro/preflight-accepted.json` 与 `product-and-encoding-proof.json`：编码检查 40 文件、零遗漏；57 个产品配方输入仍与 `43193d6e9163aaccece5e3c30d96bcbabd3cd260` 原 blob 一致。20 个其余旧测试模块的 AST 在移除 encoding keyword 后完全一致；workflow 测试另包含目录别名 fixture 改进。
- `out/m1-retro/legacy-verify.json`：实际 M1 tar.zst 完整流校验，101,472 files / 13,947,727,301 logical bytes，cleanup_authorized=false；未恢复/覆盖既有保存树，原 archive/inventory 与旧独立审计保持原范围。
- `out/m1-retro/selection-accepted/`：从保留的原 M1 配置、原 provider catalog 和实际保存收据派生新的 Windows/WSL 选择。实际 Windows 正向预检通过；修正后的 WSL 正向预检在 `preflight-m1-ubuntu-final.json` 通过。
- `out/m1-retro/strict-windows-final.json`：冻结评估器与回归源码，实际执行 98/98，零 failures/errors/skips，M1/M0 均 pass，required/optional gaps 为空；结束时再次校验源码摘要。
- 最终选择工具增加共享的嵌套 ZIP 实测摘要检查后，再对实际原件生成 `selection-release/`；`selection-equivalence.json` 证明 native/WSL 两套 M0/M1 与严格实测选择相同，仅新目录中的 M0 指针位置不同。两层 archive 的坏摘要负向在两平台工具合同通过。
- `out/m1-retro/summary-real-{sdk,m1}.json`：实际原 SDK build 与 98 项严格报告的小摘要保留 SDK/recipe 身份、98/98、零缺口及 evidence_verified=false。
- 下载网络合同在真实 literal-loopback HTTP server 验证：API→下载→403→新引用→成功；token 从未发送给测试服务器，引用/token 不进收据，缓存复用不访问网络。未重新查询已完成的 GitHub Full。

## 失败与未完成尝试

初次 Windows 工具合同暴露旧 symlink fixture 的 WinError 1314；改用 junction 后通过。初次 WSL 派生预检遗漏 M1 顶层 Windows 绝对引用，失败记录保留；统一解析并转换位置字段后通过。

`strict-windows.json` 在评估器源码补充后中止，仍保持其原始 running/未完成状态；`interrupted-strict.json` 明确其未用于验收。最终完整重放使用新 `strict-windows-final.json`，冻结的评估器与回归模块在结束时再次匹配源码摘要并通过。

## 范围

本轮只改工作流工具、证据评估器、合同 fixture 与文档。未重建或重新声明产品 OS 执行成绩；产品仍绑定成功 Full `41542426189` 的既有原件。本轮未派发独立 Standards/Spec 审查，也不宣称已有独立评审清零。保存工具不执行 worktree 清理；长期原 ZIP 镜像仍暂缓，release_ready=false。
