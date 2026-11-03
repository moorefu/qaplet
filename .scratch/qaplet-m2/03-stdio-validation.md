# 03 · Pipe / 借用标准流原生探针实施与验证

**状态：** 实现与本地 Linux / Windows / 实际 64K 验证完成；最终 Cross/Full 尚未取得，本票保持 claimed。

## 边界与配方

沿用父规格 T01/T02 已确认边界：Ubuntu producer、供应同一字节原生 APE、真实 OS fixture、结构化 consumer。探针不是 Runtime/SDK/stdio 产品实现，不自动签发 AC-17、AC-19、AC-26 整组或 M2 支持声明。

工作分支 `codex/m2-03-pipe` 初始 HEAD 为批准输入 `5685bb6`，已确认 ancestry，并在实施前同步 `bb0d6ea`；随后合并公共 CI 的 `14f3d82`。所有代码只在本票 worktree 修改。新配方为 `scripts/m2_stdio.py`、`tests/native/m2_stdio.c`，复用已锁 producer、APE 重打包/审计、loader 与完整系统 guest transport，不修改 M0 `native_io` 配方。

构建使用 Ubuntu-22.04 / Linux x86_64 / WSL2 / 4096 页。可复用输入与中间件显式位于主工作区 `/mnt/d/Workspace/dairn/qaplet/.cache`；各次构建使用独立 UUID。Windows 只消费 Ubuntu 已构建的 APE。

CLI：

- `build --output-dir DIR --report FILE`：仅 Ubuntu，校验固定输入、构建双架构 fat APE、审计 PE/ELF、核对构建前后配方字节。
- `run --artifact FILE --build-report FILE --loader-dir DIR --report FILE`：校验供应摘要/长度/审计及实际宿主，执行原生探针，严格消费原始 NDJSON。Windows 通过 PE 启动，私有 Console 使用现有 ConPTY fixture。
- `guest-manifest --artifact FILE --build-report FILE --loader FILE --page-size 65536 --manifest FILE --report FILE`：导出 `/payload/m2-stdio.com` 与 `/payload/m2-stdio-loader.elf`，不声称已执行。
- `import-guest --artifact FILE --build-report FILE --guest-report FILE --report FILE`：只读核对完整 guest、QEMU/TCG、页大小、输入/载荷、原始输出及取消/回收事实。

`m2_product_status=not_run` 在探针报告中始终保留。必需 case 缺失、重复、额外项、fail、skip、异常输出或 supervisor 超时不能聚合为通过。

## 覆盖

- Pipe 精确原始字节 `00 01 7f 80 ff 0a`、短读、readiness、EOF、EPIPE、暂不可用、背压和已写量；排空后逐字节核对数据。
- `dup` 的实际非阻塞状态影响与恢复；不假设复制描述符隔离共享状态。
- POSIX 双 fd `poll` + 控制 pipe 的等待取消；Windows 先查询真实 pending I/O，再调用 `CancelSynchronousIo`，核对 `ReadFile` 退出与 join。
- 等待时关闭写端，核对 EOF / Windows error 109；逻辑取消先于 owner 接受结果时，保留原始迟到字节而不交付。
- 真实 fd 0 的 pipe、普通文件、无效描述符与恢复；借用、拥有、alias 分开记录；文件偏移共享实际读回。
- 真实 fd 1 / fd 2 的 pipe 字节往返和恢复。
- 私有 POSIX PTY / Windows ConPTY Console 的标准输入、重复句柄共享模式、原模式读回恢复，以及全部 fixture 的关闭/回收。

## 红绿与失败历史

1. 首个 CLI 红测试：缺少 `m2_stdio.py` 时无报告；实现后缺供应明确 blocked、无任何构建/执行命令。真实供应测试在制品尚不存在时失败，保留实际返回值。
2. `out/m2-03/first/build.json`：编译真实失败。固定 Nightly 的 `libc/nt/files.h` 间接包含重复 `NtSidIdentifierAuthority` 定义。本票不改锁定工具链，只保留两个无结构体参数的原始 wrapper 声明；它们随后在 Windows 真实执行。
3. `out/m2-03/second/build.json` / `linux.json`：独立 APE 构建、Linux 17 项初始探针通过；这是旧一次配方，不代替最终配方。
4. `out/m2-03/second/windows.json`：受限进程 main 前 30 秒超时，stdout/stderr 为空，所有 required 保持 not_run；不计作取消或清理成功。02 对旧 M0 Hello/native-io 的对照也重现该上下文差异。相同 APE 在正常用户上下文立即执行。
5. `out/m2-03/second/windows-user.json`：捕获两个真实差分。非阻塞 libc 大写入 EAGAIN；恢复 duplicate 后 Windows `fstat` 的 inode 值不同。后续保留原始 dev/inode/mode，核对成功 dup2 与类型/设备，不伪造 inode 相等。
6. `out/m2-03/third/windows.json`：真实 `CancelSynchronousIo` 接受，pending=1，`ReadFile` 返回 error 995，线程 join 与所有句柄关闭；端点关闭为 error 109。私有 ConPTY 的模式与 fixture 回收均通过。直接 Win32 NOWAIT byte pipe 在排空 4096 后对 16384 字节请求返回 TRUE/0，原字节完整排空并回收。最终配方把这个已测 OS 结果作为明确判据，另列 `capability_limits`，不把它伪装成“已验证短写”。后续产品写端仍须分块推进。
7. `out/m2-03/third/guest-65536.json`：实际完整 ARM64 `6.8.0-147-generic-64k` / 65536 页，16 项通过，背压 fixture 失败：排空 4096 不释放 64K pipe 槽。最终配方改为排空实测一页、发出 131072 字节写请求；没有降低部分写判据。
8. `out/m2-03/check.json`：338 项普通合同中 12 fail / 4 error，原因是本地旧 Git shim 把 `git init /tmp/fixture` 绑定当前 worktree。只修正忽略的本地 shim，使路径转换仅作用于只读 Git；源码没有因此更改。`out/m2-03/check-fixed.json` 随后完整通过：338 tests、169 个既有显式数据/平台 skip，真实系统 C bootstrap 通过。普通 check 的 skip 不代表 M2 必需矩阵通过。

## 最终验证与剩余门槛

最终供应物和逐宿主成绩记录在本票 worktree 的 `out/m2-03/final/`：

| 原件 | 实际结果 |
|---|---|
| `build.json` / `artifacts/m2-stdio.com` | Ubuntu producer、两 CPU 链接图/符号及 PE import/ELF 布局审计通过；908921 字节 |
| `linux.json` | Linux x86_64 / WSL2 `6.6.87.2-microsoft-standard-WSL2` / 4096 页，19/19 pass |
| `windows.json` | Windows x86_64 / `10.0.22000` / 4096 页，正常用户上下文 19/19 pass；Win32 NOWAIT 大写入零进展约束在 `capability_limits` 独列 |
| `payload-65536.json` / `guest-65536.json` / `stdio-65536.json` | 实际 ARM64 `6.8.0-147-generic-64k` / 65536 页完整系统 TCG，19/19 pass；QEMU 已退出、无超时。实测管道填充 1048576、排空 65536、131072 请求写入 65536，逐字节往返正确 |
| `timeout-negative.json` | 实际外层 1 秒超时，consumer fail，19 项均 not_run；不签发产品取消/回收成功 |

最终 APE SHA-256：`e67fd751037774ee60ff39d3d588f2024d91a4b83319d59c5d895ac6a08e479c`。
源/配方集合摘要：`99d65a717b5108551b762ad0abdefbca5148e3f74e87d51190862de5236cb16d`。
构建摘要：`b3a19096abce13ffb8a015444bf69a59d50f889602f22d186c74bf32d6aa46e2`。
供应 ARM64 loader SHA-256：`255583cb792109859388afc600366c8e048a07c68cb81a23e97846cd0837a74d`。

Linux 与 Windows 分别显式供应最终 APE，执行 `python -m unittest discover -s tests/contracts -p test_m2_stdio.py -v`：各 4/4 pass、0 skip，包括真实 19 项矩阵、缺供应阻断、篡改前置拒绝、外层超时负向。Linux 另传 `QAPLET_TEST_M2_STDIO_LOADERS` 指向已锁 native loader；Windows 不编译、不使用 POSIX loader。

逐命令 argv、真实时间、退出和原始 stdout/stderr、ConPTY 与 guest 日志均保留在各报告旁的 `evidence/<uuid>/`；构建中间件在主缓存。最终 producer 的当前源码/配方在构建后再次核对，没有运行中变更。不能在保存原件之前清理 worktree。

macOS x86_64、macOS ARM64、Linux ARM64 native 的当前配方真实成绩、适用 Cross/Full 及完整矩阵聚合待集成入口补齐；本地 Linux x64、Windows x64 和完整系统 64K guest 不替代这些原生宿主。没有 Full 10,000 次/1 小时长测、M2 产品或 release-ready 声明。
