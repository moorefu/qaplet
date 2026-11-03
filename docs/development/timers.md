# 一次 timer

显式 M2 实例通过 `QAPLET_CAP_M2_TIMERS` 声明一次 timer 能力；旧创建入口不启用它。
`qaplet_timer_create` 接受安全整数毫秒、owner 回调和借用 userdata，返回带 Runtime
身份及 generation 的值句柄。零延迟也由后续 owner 驱动领取。代码及 userdata 保留至
交付返回、成功取消或 owner 终止清退；自取消不提前释放正在回调的记录。

`qaplet_timer_cancel` 取消尚未领取的回调，`qaplet_timer_ref` 修改保活属性；这些操作
允许在 owner JS、同步 Host 和 timer 回调中使用，禁止跨线程操作。求值、嵌套驱动和
关闭仍受整段驱动保护。失效、已完成、已取消和跨实例句柄返回 `INVALID_HANDLE`。

`qaplet_get_timer_snapshot` 是独立版本化 POD 查询，允许任意线程及 CLOSED。
它报告当前/峰值、ref、ready、固定槽表字节及向上取整的最近等待毫秒数；
`UINT64_MAX` 表示没有待调度 timer。最近时间包含 unref timer，不构成保活或通知注册。
宿主自己的循环须另外安排跨线程工作通知。快照不暴露等待句柄或 JS 值。

M2 内建 `qaplet:timers` 导出 `setTimeout(function, delay)` 和 `clearTimeout(handle)`。
句柄提供 `cancel()`、`ref()`、`unref()`、`hasRef()`；错误带稳定数字 `code`。
delay 必须为 Number、有限、非负安全整数，且换算为单调绝对纳秒 deadline 可表示。
无省略 delay、隐式转换、字符串回调、额外回调参数或浏览器兼容转换。丢弃句柄不取消。
尚未实现的 interval/immediate 没有成功占位导出。

timer 默认 ref。入口 DONE 后 ref timer 仍驱动，unref 只解除自然退出责任。
自动 `run` 在完整 jobs 检查点跳过 unref-only 资源；显式 `pump/run_once` 可推进
已就绪的 unref timer，但不为未来 unref-only timer 持续等待。
等待取 max_wait、最近 timer 与活跃入口绝对期限的最小值；纯等待不新建执行账本。
活跃入口（含 TLA）与回调共用累计账本，独立回调及其完整 jobs 检查点另起账本。

每实例预留有限槽表，timer 与后续 immediate 共用配置的合计上限（默认 1024）。
元数据从创建起计入 SDK 原生总存储（默认 16 MiB），创建预检不足返回
`RESOURCE_LIMIT`，实际存储分配拒绝仍按 `MEMORY_LIMIT` 永久终止。
stop/故障由 owner 驱动边界清退，close 在释放 Context 前释放全部 timer 根和槽表，
不执行用户清理 JS。没有后台 timer 线程或每 timer 原生句柄。

最小 CLI 使用同一自动驱动器等待 TLA，在入口完整检查点 DONE 后复制入口结果，
再继续驱动独立 ref timer。SDK 最近结果槽仍遵循下一次接受执行替换的原有合同。
平台验收及未测范围见 [07 验证记录](../../.scratch/qaplet-m2/07-timer-validation.md)。
