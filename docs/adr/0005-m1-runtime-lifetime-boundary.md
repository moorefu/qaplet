# M1 的单 Context Runtime 与不可撤销停止

2026-11-02，用户在正式 M1 的 grill-with-docs 第一轮确认：每个 Qaplet Runtime 仅有一个存活的主 Context，SDK 不公开 Context 句柄，owner 固定为创建线程；重复求值保留全局环境和模块缓存，需要全新环境时创建新 Runtime。QuickJS 的预算与 jobs 归属 Runtime，排队的 job 还会保留 Context，因此 M1 选择完整实例作为生命周期边界，避免承诺独立 Context 销毁就能取消其 jobs。`request_stop` 是不可撤销的终止请求，成功只表示请求已接受；它独立于有界业务队列，恢复执行须创建新 Runtime，单次执行中断若提供则另定合同。

已确认的完整设计边界见 [M1 设计决策](../../.scratch/qaplet-m1/design.md)；本决策不改变 [ADR 0004](0004-v15-platform-baseline.md) 的 M1/M2 分界，也不声明正式 SDK 已实现。

同日第二轮确认 close 与 destroy 分离：owner 可幂等关闭并保留 CLOSED 句柄，最终释放前由宿主撤下生产者、等待在途调用结束，释放后不能使用旧指针。停止后剩余用户 jobs/事件只释放，不以执行用户 JS 完成清理；native 资源仍需回收。M1 不公开承诺可恢复的 request_interrupt，因为引擎中断不能回滚已经发生的状态修改。

收口时确认 Runtime 生命周期与执行阶段分别报告，普通执行错误完成后不等于 Runtime FAILED。close 可关闭当前未执行 JS/宿主回调的 PENDING 实例；destroy 只接受 CLOSED，宿主负责确保没有未来投递或在途调用，SDK 不以释放后的指针检测代替该责任。停止与事件接受/领取按统一顺序决定，第一次记录的终止原因保留；已领取的工作协作退出，未领取的工作不再执行。
