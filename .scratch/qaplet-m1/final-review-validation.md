# 正式 M1 最终集成、审查与保存记录

**Status:** resolved
**Date:** 2026-11-03
**Integration:** `codex/m1-integration`；接受源合并锚点 `7ea6e3c0ccd2fe2d544fccddbfb27ef953ec443f`。
**Approved requirements:** `b4f52336450218fc40dabb937ed297f30815ca15`；规格正文与 Q1–Q32 合同保持不变。

正式 M1 的 01–16 与父规格已完成。实现包含统一单 Context Runtime、严格值/诊断与复制结果、ESM/动态 import/TLA、Host Module、跨 pump 预算、有界 POD 事件、协作停止、close/destroy、公开静态 Embed SDK、共用执行器的 CLI 和包外纯 C 应用。最终修复补齐模块与显式 base 的单次执行选项，提前准备可表示的绝对期限，并统一原始证据输出保护、原生计量和 SDK Host 文件清单。

## 身份与真实验收

| 项目 | 已验事实 |
|---|---|
| 产品源码 | `43193d6e9163aaccece5e3c30d96bcbabd3cd260` |
| UTF-8 测试修正 | `6eaa53d6c06691144723085928f66aebf01e944c`；仅三个证据测试模块明确文本编码 |
| 最终接受源 | `4c4b8c9e856d061e647f1364190e991d146909bb`；末次提交仅追加验收记录 |
| 实际 Full | [41542426189](https://github.com/moorefu/qaplet/actions/runs/41542426189)，completed/success，准确绑定产品提交 |
| SDK 集合 | `c44d739fe43589353668cc1245ee31a68a84a0891df3ffcbb2ebfd3ca42eac3d` |
| 产品配方 | `2489d34a07b5a86c1efa5c899c6b2f9ffe6173ef5dd89aed2b56ca02948f64ff` |
| 封装 SDK ZIP | `dd446de3d5378d9bcf711811cdeb5ac31225ffa5036f16910fb0f53c920f2a63` |
| 五原生宿主 | Linux x64/ARM64、Windows x64、macOS x64/ARM64；各自运行 98 项 SDK/CLI/应用合同与 1,000 次生命周期 |
| 实际 64K | 固定完整系统 TCG guest，98 项当前 SDK 合同；执行方式与内核页大小均有原件 |
| 包外纯 C | 从封装 SDK 的公开材料独立编译/链接、审计并运行 8 项应用合同 |
| 独立当前 M0 | Full 235 项、六目标、断网双构建六二进制/两 helper/14 refusal，原合同保持 |
| 普通 Ubuntu 入口 | 323 tests，167 项按显式数据/平台选择 skip，真实 C bootstrap 通过 |
| 严格必需入口 | 实现源和清理后主工作区均实际运行 98/98，零 failures/errors/skips |

全部 57 个产品配方文件在测试修正、最终接受源和集成后保持产品提交的准确字节；[集成后证明](../../out/m1/merged-product-proof.json)与[接受后的证明](../../out/m1/review-fixes/out/review-fixes/utf8-final/product-files-after.json)分开记录产品和评估器身份。三个测试文件的显式 UTF-8 I/O 修正保留时间区间、Unicode、catalog 和 ZIP/hash 断言语义；正常 Windows cp936/非 UTF8 模式的四项当前数据控制通过。

## Standards

最终产品修复的[独立报告](../../out/reviews/m1r6/standards-report.md)为零 hard breaches、零 actionable Fowler findings；绝对期限拒绝发生在模块/base/源码存储变更之前，重复保存断言统一。随后测试编码修正的[独立报告](../../out/reviews/m1r7/standards-report.md)同样为零 hard breaches、零 actionable Fowler findings/source regressions。

## Spec

最终产品修复的[独立报告](../../out/reviews/m1r6/spec-report.md)为零剩余缺失、范围扩大或错误实现；SDK matrix 的 raw-log 输出碰撞已解决。测试编码修正的[独立报告](../../out/reviews/m1r7/spec-report.md)确认零发现，全部 63 处差异仅明确 UTF-8 I/O，既有正负控制语义不变。

最初完整 M1 审查与有效修复复审链为 `m1r2 → m1r4 → m1r5 → m1r6 → m1r7`。每轮固定 base/head、分别生成并验证两轴 packet，fresh reviewer 只收到本轴 packet/brief/本轴历史报告；Standards 保留完整 12 项 Fowler 基线。原报告和按轴计数留在 `out/reviews/`，最终修复详见[修复记录](review-fixes-validation.md)。产品与测试的最后两轮各自独立清零；评审结果与实际执行验收分别记录。

## 清理后主工作区重放

移除最后的实现 worktree 后，从主工作区新建路径选择，原报告、原始选择与原 ZIP 均保持原字节。派生变更仅为 M0 选择指针和已删除 worktree 的显式映射根/前缀。

- [Windows 选择](../../out/m1/final-relocated/config-windows.json)与[Ubuntu 选择](../../out/m1/final-relocated/config.json)，[派生收据](../../out/m1/final-relocated/derivation.json)。
- [Windows 正向预检](../../out/m1/final-relocated/preflight-windows.json)与[Ubuntu 正向预检](../../out/m1/final-relocated/preflight-ubuntu.json)均 pass。
- [主工作区完整严格报告](../../out/m1/final-relocated/strict-windows.json)：98/98，零 failures/errors/skips，M1 与 M0 Core 均 pass，required/optional gaps 为空。
- [实现阶段完整严格报告](../../out/m1/review-fixes/out/review-fixes/utf8-final/strict-windows.json)、[POSIX 三项](../../out/m1/review-fixes/out/review-fixes/utf8-final/native-ubuntu.json)与[接受汇总](../../out/m1/review-fixes/out/review-fixes/utf8-final/acceptance-summary.json)均保留。

严格报告的原始 regression log SHA-256 为 `9529c31c833bdf62f4e382bd3c20f05d7e65a340dec826afe21a14cb4c30f2b7`。完整执行与 `evidence-preflight` 的范围分开；原先失败的时区 fixture 尝试仍为失败，后续新目录的完整通过才用于本次收口。

## 原件保存与 worktree 清理

01–16 的输出已逐票保存、独立审计后清理。最后修复 worktree 的全部 101,472 文件、8,204 目录、13,947,727,301 逻辑字节在删除前与独立 archive 逐项 SHA/模式/mtime/物理身份核对，并验证 Git 分支、祖先关系、路径边界、无 reparse 与额外未保存输出。源码分支引用和根 `.cache/` 均保留；最终仅主 worktree 注册。

保存位置为 `out/m1/review-fixes/`。[保存说明](../../out/m1/review-fixes/README-preservation.md)、[原始精确元数据/链接拓扑](../../out/m1/review-fixes/source-inventory.json)、两份 Windows/WSL 映射和全部独立审计记录均随副本保存。完整 archive `evidence.tar.zst` 为 376,477,164 字节，SHA-256 `cdeda4051496300d7478b0eadb27478c669849e94afdeb8b5358fc933fc2d88e`。

完整物理树曾全部还原并独立验证。为给最终重放留空间，随后仅将 131 个完整物理组的 803 个历史二进制副本改为 archive-backed 保存；100,669 文件仍展开，全部 reports/logs/text/proofs 和当前 `utf8-final` 保持物理可读。每个 archive-only 路径显式记录在 [catalog](../../out/m1/review-fixes/packed-catalog.json)，所有原始字节仍由不变 archive 覆盖。按需展开工具严格校验 SHA、拒绝覆盖不同的既有文件；映射计数是已审计快照，后续展开另有收据。

- [删除前 archive 独立审计](../../out/m1/review-fixes/audits/m1-final-archive-independent-20261103-001.md)：零 blockers。
- [完整物理还原独立审计](../../out/m1/review-fixes/audits/m1-final-restored-independent-20261103-001.md)：零 blockers。
- [最终物理+archive 完整覆盖独立审计](../../out/m1/review-fixes/audits/m1-final-virtual-independent-20261103-001.md)：零 blockers，逻辑文件/字节/元数据完整，保留组无外部 hardlink/reparse，archive frame/checksum/footer 完整。

当前 Full 的九份必需外层原 ZIP 共 101,063,369 字节，provider SHA/CRC/成员均验证；[收集收据](../../out/m1/ci/deadline-fix-collection.json)与原 artifacts API JSON 保持。所有签名下载地址已清成已验本地路径。旧失败 Full、未完成/失败的严格尝试、原始诊断与各候选输出均按原范围保存。

## 完成范围

本记录解决正式 M1。M2 的 OS waker、I/O 取消、迟到 completion、完整调度及物理 64K/性能不外推。长期原 ZIP 镜像仍为用户暂缓的独立发布门禁；`release_ready=false`。
