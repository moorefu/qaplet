# 第三方许可边界

根级 MIT 许可适用于 Qaplet 自有代码和文档，不替换第三方软件的许可。

BOOT-01 未下载、复制或随仓库分发 QuickJS、Cosmopolitan、QEMU、系统编译器或其运行库。Python、GNU Make、GCC/Clang、Git、WSL 是维护者环境中的外部开发工具，继续遵循各自发行包的许可。bootstrap 二进制仅为本地开发证据，不是产品发行物。

GitHub Actions 引用官方 [checkout](https://github.com/actions/checkout)、[setup-python](https://github.com/actions/setup-python)、[upload-artifact](https://github.com/actions/upload-artifact) 和 [download-artifact](https://github.com/actions/download-artifact)；它们的源码与许可由各自上游管理，没有复制入库。

任务 03/04 获取实际引擎与工具链时，必须一并记录来源、内容身份、原始许可及补丁清单；后续聚合与发行只依据实际纳入的依赖生成许可清单。不得以此文件声称尚未取得的依赖已经完成许可核验。
