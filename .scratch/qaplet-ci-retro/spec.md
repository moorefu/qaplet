# CI 修复会话复盘改进

Status: claimed

用户确认“都落”，实施本次会话复盘的六项改进，基线 main 39534978ad0eb0fceffec144ceeb055e458553d1。

## 验收范围

1. 复用现有 Ubuntu cli-consume 与 Windows supplied consumer；CLI 相关 Cross、所有 Full 执行完整 CLI 和受控正负回归，静态 CI 合同拒绝缺失、跳过或容错接线，失败原件上传。
2. SDK 构建从明确提交的独立输入快照执行，原工作区后续编辑不进入构建；继续校验输入/配方，复用主仓库物理缓存。推送重型 CI 前先完成本地检查与固定头审查。
3. dev 入口验证实际临时根可写与可用空间；仅子进程设置 TMPDIR/TEMP/TMP。提供一次性 Linux 原生文件系统检查，保留原始字节清单与日志，原 Makefile 不变。
4. 在现有 ci_evidence 上支持保留 API 响应的小状态/变化摘要、精确制品选择和合并后收据。接收文件或 stdin 的结构化 JSON，兼容已用 connector 响应形状。缺失、歧义、错 run/commit/平台或失败均不作为成功。
5. 审查明确 managed closure、jobs handoff、physical completion/join、native quiescence 各自证明边界；runner/steps/inherited/反向时间组合使用参数化合同回归。
6. guest 执行保留有界的宿主资源与 QEMU 进程样本，观察失败不改变产品判定。内核 soft lockup 根因仍 Unknown，独立票继续调查，重跑通过不等于根因已修复。

## 保持的合同

全部制品/SDK 构建只在 Ubuntu；Windows 消费同字节供应。可复用缓存仍为主仓库 .cache；报告/最终制品为 out。产品 ABI、信号/取消、容量、5秒累计清理、1000+100、五原生/真实64K和正式 M2 票27/release_ready 不因工程改进而改变。冻结 Makefile、scripts/build_ape.py、scripts/foundation.py、scripts/evidence.py 及父 M2 spec 原字节保持。

## 验证

每项有正负合同与实际入口验证；先本地/独立双轴，后精确头 CI；合并后核对新 main 的必需工作流与原件。只读观察/计划/复用不签发新产品执行或正式 M2 完成。
