# 01: CLI 消费链接入风险定向 CI

Status: ready-for-agent

Execution state: implemented; fixed-head validation tracked separately

实现 [复盘规格](../spec.md) 第 1 项，保留该规格的全部产品与正式验收边界。验收使用已有入口、相应正负合同与原始执行记录，完成后记录具体提交和证据；不把局部工具通过当作 M2 正式完成。

## Comments

scripts/ci_tiers.py 与 m0.yml 已接入现有 CLI producer/Windows consumer；test_workflow_retro.py 覆盖 missing/skip/continue-on-error 和 Full 不得丢弃 CLI。真实供应执行与精确提交 CI 另保留在主 out/qaplet-ci-retro/。
