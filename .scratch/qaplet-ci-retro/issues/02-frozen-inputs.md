# 02: 固定构建输入与推送前验证顺序

Status: ready-for-agent

Execution state: implemented; fixed-head validation tracked separately

实现 [复盘规格](../spec.md) 第 2 项，保留该规格的全部产品与正式验收边界。验收使用已有入口、相应正负合同与原始执行记录，完成后记录具体提交和证据；不把局部工具通过当作 M2 正式完成。

## Comments

dev.py build-sdk 强制显式 source-ref，独立检出提交并前后复核源清单；真实 Git fixture 证明工作区编辑不进入供应、构建期间改动快照会失败。重型 CI 前固定头审查见 docs/agents/review.md。
