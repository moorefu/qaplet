# 03: 本地临时目录与原生文件系统预检

Status: ready-for-agent

Execution state: implemented; fixed-head validation tracked separately

实现 [复盘规格](../spec.md) 第 3 项，保留该规格的全部产品与正式验收边界。验收使用已有入口、相应正负合同与原始执行记录，完成后记录具体提交和证据；不把局部工具通过当作 M2 正式完成。

## Comments

dev.py --temp-root 与 --min-temp-free-bytes 仅作用于子进程；临时空间不足与既有输出覆盖拒绝均有回归。check-native-2 已通过 487 项（238 可选跳过）及真实 C bootstrap；最终头执行使用新的 out 路径。
