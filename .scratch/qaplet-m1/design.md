# 正式 M1 设计决策

**讨论状态**：已完成。Q1–Q32 全部获用户确认，作为正式 M1 的设计基线。
**确认日期**：2026-11-02，用户分别回复 `Q1–Q6：全部是`、`Q7–Q14：全部是`、`Q15–Q24：全部是`、`Q25–Q32：全部是`；最后一轮同时确认共同理解已达成。
**交付范围**：本文件、术语、ADR 与总体计划的设计细化；C 函数原型和逐票实现由后续实施规格承接。本文件不使用实现票的 ready-for-agent 状态，也不宣称正式 Runtime/SDK 已实现或验收。
**实施入口**：[正式 M1 实施规格](spec.md)已于同日按 to-spec 发布，Status 为 ready-for-agent，沿用 Q32 测试边界。本文件保留设计确认与源码依据；公开 C 声明和枚举数值由规格驱动的实现头文件落实，逐票工作由后续拆票承接。
**基线**：[v1.5 正式 M1](../../docs/plans/qaplet-v1-implementation-plan-v1.5.md#m1--quickjs-engine--通用-c-embed-sdk)、[ADR 0004](../../docs/adr/0004-v15-platform-baseline.md)、[ADR 0005](../../docs/adr/0005-m1-runtime-lifetime-boundary.md)、[ADR 0006](../../docs/adr/0006-m1-frozen-module-identity.md)、[ADR 0007](../../docs/adr/0007-m1-cooperative-resource-failure.md)。

## 已有阶段约束

JS 操作只在 owner thread 执行；跨线程只提交数据或协作式停止请求。M1 由 owner 主动处理有界 POD 队列；M2 负责跨 OS waker、I/O 取消、迟到 completion 与完整调度，不将这些能力提前列为 M1 前置。既有 M0 微型 Host 只提供入口证据，不是正式 Runtime/SDK 的实现。

## 生命周期与执行状态

每个 Runtime 只有一个主 Context，owner 固定为创建线程。生命周期与执行阶段分开查询：

| 生命周期 | 含义与允许的推进 |
|---|---|
| OPEN | 创建成功；可注册模块或执行。PENDING 执行存在时，新顶层执行返回 BUSY |
| STOP_REQUESTED | 停止已接受并拒收普通新工作，owner 尚未确认停止 |
| STOPPED | owner 已到检查点，不再执行用户 JS；可查询、close、重复 stop |
| FAILED | 首次终止原因为执行期限、调度量或实际分配拒绝；可查询、close、重复 stop |
| CLOSED | 执行资源已关闭，控制句柄保留；可查询、重复 close/stop，宿主撤下生产者后 destroy |

执行阶段为 IDLE/RUNNING/PENDING/DONE，完成结果另有错误 code；普通 JS 错误的 DONE 不表示 Runtime FAILED。首因竞争遵循 Q27，不能由后来发生的失败改写既有停止原因。

owner 可在 PENDING 且当前没有执行 JS/宿主回调时 close。close 不等生产者退出，幂等关闭后保留句柄；destroy 只接受 CLOSED，释放前宿主必须撤下所有生产者并等在途调用结束。释放后的任何别名都失效。

关闭先关闭普通工作接收并停止用户 JS 调度，释放事件及 owner 持有的 JS 引用，再释放 Context/Runtime 和 SDK 存储，最后发布 CLOSED。引擎析构期间宿主绑定描述、函数代码与 userdata 仍须存活；native finalizer 不得执行 JS。关闭控制句柄及其状态同步存储留到 destroy，不能随着事件队列提前释放。

## 调用权限与数据所有权

| 操作 | 普通调用线程 | 宿主回调内 |
|---|---|---|
| 创建 Runtime | 当前线程成为 owner | 不重入当前 Runtime 的执行 |
| 注册 host 模块 | owner，首次 eval/run/pump 前 | BUSY |
| eval/run_entry/pump/get_result | owner | BUSY |
| close/destroy | owner，当前未执行 JS/宿主回调 | BUSY |
| post_event/request_stop/POD 快照 | 任意线程，句柄仍存活 | 允许 |
| 返回值 setter | 当前 owner 宿主回调 | 只在当前调用中有效 |
| 全局 ABI/能力查询 | 任意线程，不需要实例 | 允许 |

回调参数只借用到回调返回，输出通过 setter 同步复制。PENDING 不保存调用方输出缓冲区；get_result 复制最近完成执行的稳定结果，容量不足可以重试。宿主代码和 userdata 保留到 close 返回，SDK 不擅自释放。所有跨线程事件都是复制的内联 POD，不携带 JS 引用、指针或析构函数。

## 默认预算

| 项目 | 默认值与计量边界 |
|---|---|
| 引擎堆 | 64 MiB，由引擎分配器计量，不是 RSS |
| 引擎栈 | 1 MiB，不约束任意宿主 C 栈使用 |
| SDK 原生存储 | 16 MiB，队列、注册表、rejection、诊断、桥接副本；源码缓存另计 |
| 单份源码 / 模块源码缓存 | 4 MiB / 32 MiB，实际字节；Script eval 不累计进模块缓存 |
| 模块 | 256 个，包含内建、已注册 host、文件及规范身份的失败记录 |
| 事件队列 / 单事件载荷 | 256 槽 / 1 KiB |
| 单个桥接字符串或字节值 / 参数数 | 1 MiB / 每次宿主函数 32 个 |
| 一次执行期限 | 启动后 30 秒，单调时钟，含交还宿主后的等待；pump 不重置 |
| 一次执行总调度量 | 100,000 个 jobs/事件回调 |
| 单次 pump 执行量 | 最多 256 jobs、32 事件；jobs 未结算完不插入下一事件 |

这些是可配置的工程默认值。0 使用有限默认值，M1 不开放无限模式；单次执行只能收紧实例限额。已有顶层执行中的事件共用其账本，空闲时每个事件及其可运行 jobs 使用独立账本；跨 pump 都不重置。源码/桥接输入预检超限只拒绝调用，实际引擎或 SDK 分配拒绝则永久 FAILED。GC 保持自动策略，不作为硬限额或每 pump 强制动作。

## 队列与停止的可观察结果

| 情况 | 合同 |
|---|---|
| 接收函数未注册 | NO_HANDLER，不入队 |
| 载荷超过上限 | 明确拒绝，不复制部分载荷 |
| 队列满 | QUEUE_FULL，不等待空位、不覆盖、不重试 |
| 入队成功 | 数据已复制并接受；不承诺已经交付 |
| stop 与投递/领取并发 | 按统一接受顺序；stop 后不接受普通新工作，已领取事件协作退出 |
| stop 时队列已满 | stop 使用独立控制状态，仍可接受 |
| 尚未领取的事件与剩余 jobs | owner 停止调度，close 时释放，不执行用户清理 JS |
| 重复 stop | 幂等；FAILED/CLOSED 上无副作用成功，不改写首因 |

有效入参与 ABI 先验证，再验证适用的线程/重入权限、实例状态，最后检查接收函数与队列容量；不能靠错误文本或事后时间重排终止首因。普通执行异常在原预算内结算可运行 jobs 后允许复用；模块失败仍按固定缓存报告。

## 第一轮：已确认

### Q1：一个 Runtime、一个主 Context

M1 每个 Runtime 只有一个存活的主 Context，公开 SDK 只有 Runtime 句柄，不公开 Context 句柄。owner 固定为创建线程，不提供线程迁移。多次求值共享全局状态与模块缓存；需要全新环境时创建新 Runtime。重复创建销毁以完整 Runtime/Context 组合验收。

### Q2：源码 ESM 与显式宿主模块

M1 模块范围为本地文件源码 ESM、显式注册的宿主模块与统一 normalize/load 接口，支持静态 import、动态 import、TLA。不支持的 scheme 明确拒绝，不把任意字符串隐式交给 OS。CJS、Node package resolver、QJB 和外部字节码分别留到对应阶段；路径身份、缓存与加载失败策略由 Q12/Q18/Q28 确定。

### Q3：分层且默认有界的预算

分别约束 Runtime 堆与栈、单次执行的时间与工作量、输入与事件桥容量。默认值有限，SDK 宿主可显式调整；Q30 确定 M1 不开放无限模式。QuickJS 堆限额不等于进程 RSS 上限。数值、跨调用累计规则及超限后的复用由 Q16/Q17/Q23/Q30 确定。

### Q4：状态码与调用方持有的诊断

使用稳定状态码区分参数、线程、生命周期、队列满、模块失败、JS 异常和预算终止。诊断写入调用方提供的版本化结构与缓冲区，不使用可能被并发调用覆盖的共享 `last_error`。正常错误路径不退出宿主进程，JS 异常不直接穿透 C ABI。截断、首因与执行状态由 Q17/Q22/Q27/Q31 确定；枚举数值与 C 原型由后续实施规格落实。

### Q5：stop 不可撤销

`request_stop` 一旦接受就永久终止这个 Runtime，执行返回后不允许再次启动，只允许查询状态、关闭及幂等重复 stop。返回成功只表示请求已被接受，停止完成由 owner 报告。M1 不公开可恢复 interrupt，见 Q10；剩余 jobs、事件与清理由 Q9/Q27 确定。

### Q6：队列满立即拒绝新事件，停止不占队列

`post_event` 不等待空位、不覆盖旧事件、不隐式重试。成功表示 POD 数据已复制并入队，失败则完全未入队；满队列返回可区分的 `QUEUE_FULL` 状态。stop 使用独立控制状态，不依赖业务队列有空位。顺序、拒绝时点、事件去向与销毁责任由 Q9/Q14/Q26/Q27 确定；C 枚举数值由后续实施规格落实。

## 已核对的实现事实

- 锁定 QuickJS `2833510432045f72915919cef680ba59e5153b16` 的 `quickjs.c` 中，Runtime 持有 jobs、interrupt 和 loader hooks；内存与栈设置也是 Runtime 级。job 入队会保留 Context，释放一个 Context 引用不等于取消它的 jobs；公开 job API 不提供按 Context 清除队列的能力。
- normalize/load hooks 属于 Runtime，已加载模块表属于 Context。默认 normalize 只处理部分词法相对路径，不提供 Qaplet 的文件、scheme、symlink 或 package 解析政策。
- QuickJS interrupt 是协作式引擎中断；不可撤销 stop 是 Qaplet 需要实现的策略，不能从引擎返回 interrupted 就推断生命周期已关闭。
- [M0 Host](../../tests/native/quickjs_eval.c) 使用固定 fixture loader 与 256-job 探针限制；它没有正式 SDK、通用文件 loader、堆/栈限额或引擎 interrupt 实现。旧探针的 256 是整次运行限制；本设计 Q16 另行确认的 256 是每次 pump 执行量，两者不是同一合同。
- [M0 ESM/Promise 验收](../qaplet-v15-alignment/esm-promise-validation.md) 已区分 jobs 清空与入口 Promise 完成；两者不能互相替代。
- 引擎 interrupt 可被 async/Promise 的内部流程转为 rejection，因此 Qaplet 必须独立记录 stop/预算终止原因，并在 owner 返回和调度前检查，不能只依赖 Eval 或 job 的异常返回判断停止。
- 公开 job API 没有队列计数或入队拦截接口。M1 可以限制每轮执行的 jobs 数，并用引擎内存限额间接约束占用，不能据此宣称 Promise jobs 的排队条数有精确上限。
- 锁定源码的默认引擎栈限额为 1 MiB。parser/compiler、GC 与任意 native 回调不具备统一的 interrupt 轮询，关闭还可能执行 native finalizer；协作式期限不承诺固定停止或关闭延迟。
- Runtime 析构释放剩余 jobs 而不调用其 job 函数，但仍可能执行 C finalizer 和宿主内存释放回调；这些清理不能执行 JS。OOM 是可被 JS 捕获的普通异常，错误文本不能作为可靠的资源分类依据。
- 自定义分配器从 Runtime 首次分配起执行预算并记录实际分配拒绝；普通 `realloc(ptr, 0)` 的 NULL 不算拒绝，可不缩减非增长 realloc 以避免可选收缩产生人工失败。该记录表示 Qaplet 观察到的分配拒绝，不表示“最终未捕获 OOM”，也不覆盖引擎分配器外的宿主/OS 内存。
- 同文件名再次 `JS_Eval(..., MODULE)` 会新建模块；统一的只求值一次与失败缓存需要 Qaplet 注册表。加载/解析失败可能没有引擎缓存，链接失败可能保留部分图，求值/TLA 失败则保留 evaluation Promise；不能承诺失败自动回滚模块图。
- rejection tracker 是“添加/移除未处理记录”的通知，最终检查点由 Host 决定；只读入口 Promise 状态不将其标为已处理，要避免对同一入口失败重复报告。JS_Call 本身不等待回调返回的 Promise。
- QuickJS 字符串输入可能替换非法编码，输出可能保留孤立 surrogate 为 WTF-8；严格 UTF-8 的 SDK 合同需要在边界另行验证。全局 Script eval 保留 Context 词法绑定，重复 let/const 声明可能正常抛 SyntaxError；import.meta 只适用于模块，其 url/main 由 Host 初始化。

## 第二轮：已确认

### Q7：显式分步驱动

异步入口未完成时返回可区分的 PENDING，由 owner 后续显式 pump 推进 jobs 和事件。pump 每次处理有限工作后返回，SDK 不忙等、不启动隐藏线程；入口完成、暂无工作和执行失败分别报告。

### Q8：close 与 destroy 分开

owner 的 close 关闭执行资源，保留可查询的 CLOSED 句柄，重复调用安全；destroy 最终释放句柄，只调用一次。destroy 前宿主必须撤下所有生产者并等待在途调用结束，释放后不允许使用旧指针。运行或回调中调用 close/destroy 返回可区分的 BUSY，需要停止时使用 request_stop。

### Q9：停止后放弃剩余用户工作

stop 接受后立即拒收普通工作，重复 stop 幂等。owner 到检查点后不再调度新的用户 JS，未消费事件与 jobs 在关闭时释放，不额外运行用户清理 JS；native 资源仍由 owner 清理。总体计划 §4.2 的“最后 JS jobs 结算”不再表示 stop 时保证执行完，JS finally 也不是停止清理保证。

### Q10：不公开可恢复 interrupt

M1 只用内部 interrupt 实现 stop 和执行超限，不公开承诺可恢复的 request_interrupt。若未来需要“终止本次执行、保留 Runtime 继续使用”，另定义合同；普通 JS 异常与资源超限后的复用见 Q17。

### Q11：单调时钟执行期限

M1 用单调时钟执行期限替换总体计划 §6.1 的 CPU time budget 表述，在引擎中断点、jobs 和宿主回调边界检查。不承诺精确线程 CPU 计费或抢占任意 native 调用；期限跨 pump 累计，见 Q16/Q26。

### Q12：文件模块的规范身份

文件模块以 realpath 后的规范绝对 file URL 为身份，相对 import 以导入模块为基准。入口相对路径使用创建 Runtime 时固定的工作目录；内存源码需要相对 import 时必须提供 base URL。M1 不猜测后缀、目录入口或 package；URL、Windows 路径与失败缓存见 Q18/Q28。

### Q13：有限宿主类型桥

宿主模块首版只声明常量和同步 C 函数，支持标量、UTF-8 和有界字节数据；C 回调在 owner 执行。复杂对象和异步 API 用 JS 包装与 POD 事件组合，稳定 SDK 不公开裸 JSContext/JSValue。数据所有权、具体类型与注册时机见 Q19/Q20/Q31。

### Q14：固定槽与内联载荷

事件队列预分配固定槽，事件包含类型、关联 token、长度和有界内联字节，不携带指针、析构函数或 JS 引用；超大事件直接拒绝。按成功入队顺序 FIFO，多生产者同时投递时，以队列实际接受顺序为准。容量、JS 接收接口及预算账本见 Q23/Q24/Q26。

## 第三轮：已确认

### Q15：单顶层执行与完成条件

同一 Runtime 只允许一个顶层执行，其为 PENDING 时新 eval/run 返回 BUSY。求值结果若为 Promise 则等待其 settle；成功还要求相关可运行 jobs 结算完。入口未完成且暂无工作时仍为 PENDING；业务事件队列是否清空不决定入口是否成功。

### Q16：跨 pump 的执行账本

每次执行默认从启动计算 30 秒单调时钟期限，包含交还宿主后的等待时间，pump 不重置；SDK 在被驱动和检查点检查。总调度量默认最多 100,000 个 jobs/事件回调；每次 pump 最多 256 jobs、32 个事件。jobs 优先，尚未结算完就交还宿主，暂不插入下一事件。该限额是执行量，不是 Promise 排队条数。

### Q17：普通失败与永久 FAILED

普通 JS/模块异常只使本次执行失败，保留已发生的状态修改；在原预算内结算可运行 jobs 后可再次执行。期限/总调度量耗尽或分配器记录到实际堆分配拒绝则置 FAILED，只能关闭，即使 JS 捕获异常也不能恢复。M1 不为分类新增引擎补丁，无法独立识别的栈溢出等引擎错误仍报告 JS_EXCEPTION，不匹配异常文本。

### Q18：固定模块缓存与失败记录

首次成功规范化后记录模块身份；同规范 URL 通过统一注册表只求值一次，不监视文件变化。已有规范身份的加载、链接或求值失败也保存失败；修复文件后使用新 Runtime 验证。无法规范化的请求只报告失败，不创建模块记录。其他模块可继续使用，失败不回滚已发生的应用修改或部分模块图。

### Q19：执行前注册宿主模块

宿主模块使用 host: 命名空间，在首次 eval/run/pump 前注册完毕，之后锁定。重复名称拒绝，无替换/注销。SDK 复制名称、导出声明和常量；函数代码与 opaque userdata 属于宿主，须保留到 close 返回，SDK 不擅自释放。

### Q20：类型与数据所有权

桥接类型为 undefined/null/bool/double、64 位整数、UTF-8 和字节数据。double 对应 Number，64 位整数对应 BigInt；对象、函数和超出 64 位的 BigInt 不隐式转换。回调参数只在回调期间有效，宿主要保留就自行复制；返回数据通过 SDK setter 同步复制，避免悬空指针。宿主函数失败转为带稳定 code 的 JS Error，可被 JS 捕获。

### Q21：版本结构与默认值

ABI 主版本从 1 开始，struct_size 限制读写范围，支持零填充的尾部扩展；未知非零 flags/reserved 明确拒绝。提供 ABI、能力和有效限额查询。0 表示默认值，Q30 确定 M1 不开放无限模式。实例容量初始化后固定，单次执行只能收紧限额，不能突破实例上限。

### Q22：无用户代码的诊断与首次终止原因

诊断不调用异常对象的 toString/getter 等用户代码，只附加安全可读的有界文本，无法安全读取则省略。截断有标志，有空间时保证 NUL 终止；缓冲区不足不丢失状态码。首次终止原因保留，后续清理错误不能覆盖。

### Q23：工程默认限额

首批默认值为引擎堆 64 MiB、栈 1 MiB、单源码 4 MiB、模块源码缓存 32 MiB、模块 256 个、队列 256 槽、单事件载荷 1 KiB、桥接字符串/字节值 1 MiB、单次宿主函数 32 个参数。Q30 补齐 SDK 原生存储 16 MiB 与计量边界，统一数值见上方默认预算表。这些是可配置的初始工程限额，不是性能或 RSS 承诺。

### Q24：显式 JS 事件接收函数

保留 qaplet:events 模块，由 JS 注册一个接收函数，owner pump 将事件交给它。未注册时 post_event 返回 NO_HANDLER，不入队。事件包含 type、无精度损失的 token，以及拥有独立存储的 Uint8Array 载荷。接收函数执行失败按普通 JS 错误处理，不隐式重试事件。

## 第四轮：已确认

### Q25：无进展、rejection 与 CLI

未处理 rejection 在 jobs 已结算完的检查点判定；同一检查点结束前补上处理器可消除对应记录，因 pump 数量配额且仍有 jobs 而提前返回时不提前报错。入口 Promise 的失败只报告一次。SDK 对尚未完成且无就绪工作的执行返回 PENDING，并报告本轮未推进，不忙等。M1 CLI 没有外部事件生产者，遇到该状态立即报告 UNSETTLED_ENTRY，退出码 1；成功退出 0，执行错误/预算终止退出 1，参数错误退出 2。

最小 CLI 为 `qaplet run <file> [-- args]`、`qaplet eval --source <text>` 及 help/version；eval 是 Script 语义，不是 CJS 或自动模块包装。CLI 与 SDK 共用 factory/loader/预算与错误映射；run 不自动打印模块命名空间，eval 只打印可桥接结果。

### Q26：事件接收函数与独立事件账本

`qaplet:events.onEvent(fn)` 只允许 owner JS 注册一次，后续替换/注销拒绝。它与“执行前锁定的 C 宿主模块注册”是不同窗口；PENDING 入口仍可先注册接收函数，再等待外部事件。type 是 uint32，对 JS 为 Number；token 是不由 SDK 解释的 uint64，对 JS 为 BigInt，0 合法。

事件函数返回值不被等待；其产生的可运行 Promise jobs 仍结算，返回 Promise 的后续未处理 rejection 走统一检查点规则，不将 pending Promise 作为下一事件的阻塞条件。有顶层执行时事件调度计入该执行；没有顶层执行时，每个事件及其可运行 jobs 使用独立账本，跨 pump 不重置，完成检查点后才处理下一事件。事件一旦领取不重试，投递成功不等于已交付。

### Q27：线程、终止竞争与最终释放

实例的 post_event、request_stop、POD 状态快照可由任意线程调用；其余实例操作只允许 owner。宿主回调内只允许返回值 setter、post_event、request_stop 与快照查询，其余重入返回 BUSY。全局 ABI/能力查询不依赖实例，可任意线程调用。生命周期单独使用 OPEN/STOP_REQUESTED/STOPPED/FAILED/CLOSED，执行阶段单独使用 IDLE/RUNNING/PENDING/DONE；普通执行错误的 DONE 不等于 Runtime FAILED。

有效入参先验证，再检查终止/关闭状态，最后检查接收函数与队列容量。stop、事件接收和 owner 领取以统一接受顺序决定：已领取的事件算在执行中，协作退出；未领取的事件不再执行。第一次被记录的 stop/预算/分配失败为终止首因，不按异常文字或事后时间重排；在 FAILED/CLOSED 上重复 stop 是无副作用的成功。

close 允许 owner 在没有执行 JS/宿主回调时关闭 OPEN（含 PENDING）、STOP_REQUESTED、STOPPED 或 FAILED 实例，停止接收并清理资源，不等待外部线程退出；可检测的清理错误单独报告，不覆盖执行首因。destroy 只接受 CLOSED，非 CLOSED 返回 BAD_STATE，owner 执行/回调中的重入返回 BUSY。撤下生产者并等待在途调用结束是宿主调用 destroy 前必须满足的责任，SDK 不承诺检测未来调用或已释放指针；成功释放后任何别名均失效。创建失败返回空输出并清理部分初始化，不向生产者发布半成品句柄。

### Q28：文件 URL、入口与元信息

模块支持显式 `./`、`../` 与绝对 file URL；入口还可使用本机绝对/相对 OS 路径。只读取 realpath 后的普通文件；硬链接按不同规范 URL 视为不同模块，不承诺 inode 去重或抵抗宿主并发换文件的沙箱语义。

M1 拒绝 query/fragment、非空 file authority、NUL、编码后的路径分隔符，以及 UNC/设备路径；Windows 本地盘符规范大写，路径以 OS realpath 结果为准，不自行将整条路径转小写。显式文件入口/依赖的 .js、.mjs 或无扩展名按 ESM 源码处理，.cjs、JSON、WASM、字节码和动态 native 库明确 unsupported；其他扩展名不猜测。内存 eval 的相对 import 使用显式绝对 file base URL；eval 是新 Script，不进入文件 ESM 的只求值一次注册表。file/host/qaplet 模块的 import.meta.url 初始值为其规范身份，main 在模块首次实例化时按是否显式 run_entry 根确定，随缓存保持；已作为依赖加载的模块后来 run_entry 时不重新执行或改写 main。

CLI 参数在创建时保存到保留的 `qaplet:runtime` 只读 argv/cwd 快照中，所有入口共用 factory；不据此声明 Node process API 已实现。所有实际提供的内建模块同样纳入模块数量与能力声明。

### Q29：异步执行结果的读取与所有权

每次 start/eval/run/pump 只写入本次调用有效的输出结构，PENDING 不保存调用方缓冲区。owner 的 get_result 读取最近完成执行的稳定结果，字符串/字节写入调用方缓冲区；不足时返回 BUFFER_TOO_SMALL 与所需长度，可重试，不重新执行 JS。结果保持到下一次顶层执行或 close；失败的新调用若未被接受，不清除旧结果。eval 只自动等待实际 Promise，不隐式读取/吸收任意 thenable；其他返回对象不隐式 JSON/stringify，报告 VALUE_TYPE_UNSUPPORTED，状态修改仍保留。模块 run 只报告完成，不桥接整个 namespace。

### Q30：资源计量、GC 与有限配置

32 MiB 累计源码指 Runtime 模块源缓存；256 模块包含内建、host、文件与已有规范身份的失败记录。重复缓存命中不重复收费，内存 Script eval 每次受单源码限额约束，不累计进模块缓存。source 与桥接限额按实际字节，在分配/复制/编译前检查。

增加独立的 16 MiB SDK 原生存储默认预算，覆盖队列、注册表、rejection 记录、诊断与桥接临时副本，源码缓存单独计量；宿主 userdata、原生工作线程及 OS/libc 内存不在此预算内。可预先发现的输入超限只拒绝该调用；已经发生的引擎/SDK 存储分配拒绝进入永久 FAILED。创建阶段分配失败仍按 Q27 清理并返回空输出。

保留引擎自动 GC，可在 owner 安全检查点回收，但不每 pump 强制 full GC；阈值是性能参数，不是硬额度。M1 不开放无限模式，0 一律采用有限默认值，所有加法/乘法/时间换算先检查溢出。快照提供引擎当前/峰值用量、队列深度/峰值、事件接收/拒绝/消费/丢弃计数及终止原因，不读取或暴露 JS 指针。

### Q31：错误与字节边界

将 Runtime 生命周期、执行阶段和稳定错误 code 分开，错误至少区分参数、ABI、线程、BUSY/BAD_STATE、UNSUPPORTED、NO_HANDLER、QUEUE_FULL、载荷过大、模块解析/加载、JS_EXCEPTION、VALUE_TYPE_UNSUPPORTED、BUFFER_TOO_SMALL、期限/调度量/内存终止；不把 PENDING/IDLE 当错误。已接受执行的本次错误可通过 get_result 读取；未接受的调用错误只写本次诊断，不覆盖旧执行结果。关闭附加错误不覆盖首次终止原因。

SDK 使用显式指针+长度、严格 UTF-8；路径/模块名拒绝嵌入 NUL。普通字符串允许 NUL，不能表示为合法 UTF-8 的 JS 孤立 surrogate 返回值拒绝而不静默改写。桥接整数精确检查 64 位范围，字节值只接受可安全复制的 Uint8Array；不作隐式用户类型转换。setter 只在当前宿主回调内有效，成功未设值为 undefined，重复设置拒绝且保留第一次值，回调外使用拒绝；这些约束须写入公开头文件。

### Q32：验收与讨论收口

Ubuntu 构建 CLI、静态 SDK 与纯 C Host 示例；其他宿主验证供应的同字节 APE/SDK 制品。Linux Fast 验证全部 M1 正/负合同，Cross 验证 Windows x64、macOS x64/ARM64、Linux ARM64；敏感源码改动仍按现有规则提升 Full，ARM64 64K 覆盖对应的新输入与 SDK 关键合同。缺目标或必需用例失败/跳过不签发对应通过结论。

验收覆盖 ESM/TLA/循环的实际支持子集、缓存与失败、重入与非 owner 拒绝、事件数据复制、满队列、stop 饱和/并发、rejection、预算/分配失败、异常后复用、创建失败回滚及 close/destroy 次序；Linux 至少 1,000 次创建关闭销毁，Cross 每目标至少 100 次。沿用真实外部入口和现有证据身份规则，不以预检或旧 M0 证据替代新产品执行。

用户已确认 Q25–Q32，并确认本次 M1 设计达到共同理解；grilling 完成，没有仍待用户选择的设计分支。具体 C 函数原型、枚举数值和逐票实现由后续实施规格落实；本文件不使用 ready-for-agent 状态，也不作为产品已实现/验收的证据。

## 源码核对范围

上述实现事实依据[源码锁](../../third_party/quickjs/source.lock.json)中的 QuickJS `2833510432045f72915919cef680ba59e5153b16` 核对，检查文件摘要与锁一致。关键位置如下，行号属于该固定快照：

| 事实 | 固定源码位置 |
|---|---|
| Runtime jobs 保留 Context、Runtime 析构释放 jobs | quickjs.c:2317–2389、2466–2478 |
| 默认栈、分配器与资源失败 | quickjs.h:337–338、363–389；quickjs.c:2099–2114、7912–7925 |
| 协作式 interrupt 与跳过 JS 异常处理 | quickjs.c:7992–8017、21243–21262 |
| 模块查找、链接/求值失败、重复 Eval 新建模块 | quickjs.c:30950–31014、31797–31804、32504–32534、38253–38330 |
| Promise 状态读取与 rejection 通知 | quickjs.c:56677–56690、56770–56775、57850–57868 |
| UTF-8 与无副作用诊断边界 | quickjs.c:4482–4504、4559–4638、8975–9003、11230–11236 |

本次只验证设计文档与源码事实，未构建或运行正式 M1 产品；Q32 是后续实现验收合同。
