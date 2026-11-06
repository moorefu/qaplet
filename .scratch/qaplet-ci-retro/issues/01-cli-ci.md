# 01: CLI 消费链接入风险定向 CI

Status: ready-for-agent

Execution state: implemented; fixed-head validation tracked separately

实现 [复盘规格](../spec.md) 第 1 项，保留该规格的全部产品与正式验收边界。验收使用已有入口、相应正负合同与原始执行记录，完成后记录具体提交和证据；不把局部工具通过当作 M2 正式完成。

## Comments

首次精确头 CI 暴露原 CLI 测试程序的读容量拒绝遗漏：Windows 双通道复制遇到 read code24 时未等待，变成未处理拒绝。紧反馈已本地复现；只修应用测试程序的显式读取重试，guest controller 同步修正，产品容量/物理回收不改。新增受控应用适配器在每个读首次委派前拒绝一次，修复前红、修复后绿；实际无注入双通道重放 1000 次通过。原失败 ZIP/provider/日志与诊断见主 out/qaplet-ci-retro/cli-read-refusal-diagnosis.md，最新供应与新头 CI 单独保留。

scripts/ci_tiers.py 与 m0.yml 已接入现有 CLI producer/Windows consumer；test_workflow_retro.py 覆盖 missing/skip/continue-on-error 和 Full 不得丢弃 CLI。真实供应执行与精确提交 CI 另保留在主 out/qaplet-ci-retro/。
