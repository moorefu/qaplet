# Qaplet

Qaplet 是可移植 JavaScript 应用运行时与应用交付平台。以下术语区分通用平台与基于平台交付的应用。

## Language

**Qaplet Core**:
Qaplet 面向通用 JavaScript 应用提供的平台能力集合，不包含服务管理应用的专属业务。
_Avoid_: 服务内核、内置服务管理器

**Qaplet Application**:
基于 Qaplet 平台开发和交付的应用，可以包含自己的资源以及必要的私有原生宿主。
_Avoid_: Kernel 扩展（指应用时）

**qaplet-svc**:
基于 Qaplet 开发的第一方服务管理应用，有自己的发行物与平台能力声明。
_Avoid_: qaplet service 子命令、Core Service 模块

**Qaplet Embed SDK**:
供应用宿主嵌入 Qaplet 并使用其通用能力的开发接口。
_Avoid_: Service SDK、SCM SDK

**Qaplet Runtime**:
可独立创建、运行与关闭的 Qaplet 嵌入实例，拥有自身的 JavaScript 执行状态。
_Avoid_: 进程、沙箱（指 Runtime 时）

**Qaplet Context**:
Qaplet Runtime 内承载应用全局环境与模块状态的 JavaScript 上下文。
_Avoid_: Runtime、权限隔离域（指 Context 时）

**Qaplet Host Module**:
由嵌入宿主为某个 Qaplet Runtime 声明的模块，提供宿主常量与函数。
_Avoid_: 动态原生插件、Node addon（指 Host Module 时）

**Qaplet Event**:
宿主向 Qaplet Runtime 提交、由其 JavaScript 接收函数处理的应用事件；被接收与被交付是两个不同事实。
_Avoid_: 已完成回调（指仅被接收的事件时）

**应用私有 Native Host**:
属于某个 Qaplet Application 的原生宿主，承载该应用必要的系统入口与原生约束。
_Avoid_: 默认 Runtime 服务层、第二套服务框架

**Core APE**:
Qaplet 通用运行时的可执行交付物，其内容身份独立于应用交付物。
_Avoid_: 服务应用 APE（指通用运行时时）

**QJB**:
Qaplet 的只读应用包，承载应用代码、元信息和资源。
_Avoid_: 安全沙箱、OS 隔离边界
