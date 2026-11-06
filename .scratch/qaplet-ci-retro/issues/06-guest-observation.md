# 06: 64K guest 有界资源观察与独立诊断

Status: ready-for-agent

Execution state: implemented; fixed-head validation tracked separately

实现 [复盘规格](../spec.md) 第 6 项，保留该规格的全部产品与正式验收边界。验收使用已有入口、相应正负合同与原始执行记录，完成后记录具体提交和证据；不把局部工具通过当作 M2 正式完成。

## Comments

guest_observation.py 已接 arm64_guest，SDK 配方及 evaluator 记录 helper 身份；有界 proc 样本、直接子进程过滤、观察失败与 kernel Unknown 有正负回归。独立票 07 保持 open；实际新 guest 样本随 out/CI 原件验证。
