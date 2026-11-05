# 23 · SDK 分发与包外 M2 C Host：本地验证

当前仍在实施。专属 `codex/m2-23-sdk-delivery` 从实际 integration
`9117eaae6153ccce642add21fa4f2d9ea26e95d8` 开始；19/22均已独立合入。
本记录不签发 M2、正式 Cross/Full、五原生、实际64K、长测或 release-ready。
父 spec、27票38边135AC、默认额度和旧 M1 ABI/创建模式保持不变。

## Windows 公开 SDK → CLI child

原21/22 parent ×21/22 child的四项 `CONTROL_FAILURE` 诊断仍保留在主工作区
`out/qaplet-m2/sdk-cli-child-*`；直接无Console对照也保留原范围，不重写旧报告。
本票先重放原driver：实际Windows的两项均parent0、child1/reaped、无supervisor
timeout。受限进程的两项未入main/timeout和WSL `DISTRO_NOT_FOUND` 单列为执行
上下文失败；获准的普通用户上下文使用既有Ubuntu-22.04，未安装/升级任何工具链。

最小对照与公开创建诊断原件均位于本worktree
`out/m2-23-sdk-delivery/diagnosis/`：

- `original-repro/f2d02dd7-5476-43ad-b468-5e1ae742f150/report.json` 重现旧供件。
- `stdio-contrast/06ec818c-6bdf-4d3c-8480-42082f189cfa/report.json`：stdin
  ignore/inherit都失败；空环境失败；显式单项native SystemRoot的大小写与三项
  排序对照成功。没有删除句柄白名单或隐式添加环境。
- `create-probe-run/e973c047-8488-4db8-a332-2df063b99386/report.json`：仅消费
  公开SDK的扩展创建和受控OS调用包装，子进程第一个socket即返回errno10106，
  factory返回空句柄、`creation_rollback=COMPLETE`、uncertain0；直接同字节
  factory/close/quiesce/destroy成功。标准流fstat/flags一致，排除简单低fd猜测。
  CRT `getenv(SYSTEMROOT)`是POSIX drive形态，直接启动的原生SystemRoot是native
  drive形态；SDK启动后原生值也变成POSIX形态。`setenv`不更新Win原生环境，
  因此直接改读原生环境会丢掉Host创建时修改。
- `mungentpath-build{,2}` 分别保存完整调用错误及纠正为value参数后的实际literal
  探针。固定导出的函数把 `/c/Windows` 转成 `c:\Windows`，native drive输入
  幂等；普通数据也可能被改，故产品只适配既有SYSTEMROOT而不泛化所有变量。

errno10106的含义来自[Microsoft Winsock错误表](https://learn.microsoft.com/en-us/windows/win32/winsock/windows-sockets-error-codes-2)。
读取同provider commit的上游
[`mkntenvblock`](https://github.com/jart/cosmopolitan/blob/aefad27c09ffda64c6ac64bd69f758e9f9c7b91d/libc/calls/mkntenvblock.c)
仅作研究：它会补当前SYSTEMROOT，不能用于本合同的完整替换。该源码与固定Nightly
libc的映射仍unproven；产品结论依据固定二进制的真实OS对照，不以研究替代映射。

产品修复 `7f95d46` 仅在Windows factory复制环境时将已有SYSTEMROOT的CRT
`/C/...`形式恢复为native路径；复制长度/预算不变。缺失key不增加，Host后续setenv
不生效，显式replacement/空mapping/其它值逐字保留，继承规则不变。

永久回归位于 `tests/native/sdk_process.c` 的 `--m2-process-cli-child`：公开C
spawn同一CLI、复制READY字节及双管道EOF，观察实际exit/reap/delivery，最后验证
close/quiesce/destroy。最初共享文件stdout/stderr发生覆盖，后续受管pipe版本
保留各次真实失败；另将测试外层上限调整到能容纳失败CLI真实五秒清理路径，
没有改产品预算。

最终窄红绿原件：Windows
`diagnosis/regression-Windows/69abc257-e005-4cfc-95fc-e9c48c1dcff9/report.json`，
Linux `diagnosis/regression-Linux/6391d9eb-c667-4980-ab7c-e91d698d9d74/report.json`。
旧库在Windows真实child26/exit1/reaped且保留Host输出清理未完成；新候选库精确
READY/EOF、exit0/reaped、parent原生清理完成。Linux同一APE旧/新均成功。
`process-candidate-build` 只替换22双库的process.o用于快速定位，所有源码/公开头/
私有产品头/QuickJS头/实际依赖/原双库/编译命令/packing原件均保存；不是完整SDK供件。

`candidate-windows/4ee6a05e-399d-4648-b15a-c958f1e9df0d/report.json` 的原始JS
parent候选 ×child21/22均READY/exit0/reaped；显式native root成功、POSIX root
及空mapping仍child26，证实没有偷偷补环境。该report的早期C Host stdout期望
失败保留，未改贴成通过。

受影响既有process输入/JS输入/继承/错误/取消/清理/pipe-control/pipe-bytes
Linux与Windows各8项均通过：
`related-Linux/1c56ede7-e46b-4c5a-bd9a-3341b2f7ccdd/report.json`、
`related-Windows/5e1843d1-6e86-437b-8bfa-131eb1954b3f/report.json`。
其中实际完整替换、空环境、Unicode、创建后setenv快照及额外fd/HANDLE隔离
仍按公开边界执行。未重复无变化的1000生命周期或长测。

## 分发与正式工具收口

独立内部实现分支分别负责consumer隔离/输入packing保留/sender与强门，及新增
M2 C组合例。最终融合后的供件、完整包外消费、身份和结论在本节后续补录，
此处不提前宣告本票本地完成。
