# 04: CI 小摘要、精确制品选择与合并收据

Status: ready-for-agent

Execution state: implemented; fixed-head validation tracked separately

实现 [复盘规格](../spec.md) 第 4 项，保留该规格的全部产品与正式验收边界。验收使用已有入口、相应正负合同与原始执行记录，完成后记录具体提交和证据；不把局部工具通过当作 M2 正式完成。

## Comments

ci_evidence.py status/select-artifacts/finalize 复用原 JSON/connector/stdin；精确平台名称、歧义、错误提交/run、失败/取消/缺 jobs、源原件保护均有合同。真实 main 3953497 的五 workflow/完整 jobs 已经 status 和 finalize 只读重放通过；不是新头成绩。
