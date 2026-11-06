# main 首次 M2 CI 的原生 SDK 回归

基线：合并提交 `7f0b3122171b461f606b2748ab4209788fec8a75`，
[main Full 42151947603](https://github.com/moorefu/qaplet/actions/runs/42151947603)。
该运行实际获得 runner；Linux x64、Linux ARM64、实际 64K、离线重构和 M0 Core 成功。
Windows、macOS ARM64、macOS x64 的 SDK 用例失败，矩阵与最终 build 按合同失败。

## Windows 临时文件

8 项失败发生在 `sdk_stdio.c` / `sdk_aggregate.c` 的 `mkstemp`，均使用硬编码 `/tmp`。
本机相同 CI Host 字节可以通过，不能以本机已有全局目录替代干净 runner 的前置。
用同一字节 Host 在没有 `/tmp`、存在 `/available-temp` 的隔离 chroot 中执行
`--m2-stdio-file`，普通 UID 1000 于与 CI 相同的文件创建断言失败。

修复使用 Host 的 `TMPDIR`、`TEMP`、`TMP`，再回退 `/tmp`；不创建缺失的 Host 根目录。
共享 fixture 头纳入 SDK 包及配方身份。用真实 C fixture 验证 Unicode/空格目录、
三种环境入口、文件清理及缺失目录拒绝。

## Darwin SIGPIPE

两个 Darwin 目标的 `m2-process-js-partial` 均由 SIGPIPE 终止（退出码 `-13`）；
`m2-process-pipe-write-failure` 触发 Host handler 保持断言。
原实现阻塞 SIGPIPE、写入、调用 `sigtimedwait` 消耗新信号，再恢复 mask。
Cosmopolitan 的 [sigtimedwait](https://github.com/jart/cosmopolitan/blob/master/libc/calls/sigtimedwait.c)
和 [sigwait](https://github.com/jart/cosmopolitan/blob/master/libc/calls/sigwait.c)
明确在 XNU 返回 ENOSYS；恢复 mask 会交付原实现未消费的新 SIGPIPE。
上游 master 的说明用于解释限制，最终支持性仍由本仓库锁定工具链的原生执行判定。

修复在 XNU 使用 Darwin 的描述符级 `F_GETNOSIGPIPE` / `F_SETNOSIGPIPE`，
写完恢复原值；其他目标保持原线程级处理。SDK 与 CLI 共用该写入边界。
不修改 process-wide handler；保留调用方 mask、原 pending signal 和描述符状态。
fixture 自己预先 raise 的信号在验证保留后，通过 fixture handler 清理，避免调用 XNU
不支持的 sigwait。原有取消、字节计数、普通错误与 cleanup 断言保持。

`test_pipe_write.py` 以真实 Linux 管道/信号及控制的 Darwin syscall 可用性构建回归：
原算法失败（`signals=1`），修复后通过。该控制场景不是原生 Darwin 成绩。

## 取消 Promise 的测试时序

macOS Intel 的 `m2-process-js-limits` 另有 `IO operation failed`；
实际 Linux 压力循环在第 20 次重现相同症状。测试先取消 IO/Process，然后 await Work，
最后才给取消 Promise 安装 catch。Work 较慢时，取消拒绝先到达完整 jobs checkpoint，
按既有合同产生未处理拒绝。

用原公开 SDK archive 编译诊断 Host，以原子门闩保持 Work 未完成，先执行 owner 的
`run_once(0)`，再放行 Work，原测试在这一确定时序失败。修复只把取消观察 handler
安装到 cancel/await 之前，保留该门闩作为原生 SDK 回归，不改变 Runtime 拒绝语义。
诊断文件保留于 `process-limits-gated-{red,green}.com` 和 `limits-{red,green}.json`。
相同受控时序：旧版 Linux 3/3 失败，修复后 Linux 1000/1000、Windows 100/100 通过。

## 验证范围

原制品与修复制品的无 `/tmp` 场景分别失败和成功；修复制品的 Windows 8 个原失败
用例全部成功。修复后的最终 SDK 在 Ubuntu 构建，同字节 Linux/Windows 验证与新远端
native 执行分别记录于本轮报告，诊断二进制不冒充正式供应包。
首次 DrvFS `make check` 执行 459 项，1 项 reuse fixture 超过既有 30 秒上限、
233 项可选输入缺失而跳过；保留失败，不放宽时限。使用逐文件 raw SHA 一致的一次性
Linux 原生文件系统快照重跑相同 `make check`，具体日志/收据留在本轮目录。
该快照实际执行 460 项（46.561 秒），236 项因可选输入/现场缺失而跳过，
普通回归与真实 C bootstrap 通过；跳过项不计作本轮完整产品验收。
M2 正式整体验收、票 27 与 release_ready 状态不由此记录改为完成。

原件及最小复现：仓库忽略目录 `out/ci-main-7f0b312/`，原 ZIP 位于
`.cache/downloads/ci/`。三个 native SDK ZIP 与 producer ZIP 均核对 GitHub provider
原 SHA-256/长度后，通过既有 `ci_evidence.py fetch` 校验 CRC、成员与解包字节。

## 同提交 push 与失败重跑暴露的 CI 编排问题

`89ac31d` 的 PR Full `42158675424` 首次全部成功；push `42158608008` 的原件
在 64K `m2-process-js-cycles` 上以退出 0、正确输出完成全部 1000+100 周期，但
guest 时间为 111 秒，超过原通用 60 秒。仅失败作业的有限重跑仍为 171 秒；同字节
PR 为 60 秒，本地锁定完整系统 64K 单命令为 52 秒。三份失败/成功原件及相同 Host
SHA 均保留，不能将实际耗时差异猜成已确定的 Runner 或产品原因。

父规格与票 16/25 要求实际周期、身份、回收、有限执行和失败不可冒充通过，没有
这 1100 周期的 60 秒吞吐合同。通用 60 秒是早期短命令看门狗。因此仅为这一命令
在权威 SDK catalog 声明 300 秒有限预算，其他命令和整个 guest 的限时保持原值；
消费者同时拒绝自行扩大 budget 与真正超时。原始 60 秒失败报告不追改为通过。

重跑还暴露两项可离线重现的编排问题：Core 把同名不同 ID 的旧/新 provider ZIP
同时交给要求唯一名称的入口；效率观测把重建 job 记录的新 created_at / started_at
与继承的旧执行/完成时间相减。Core 现在保存完整 catalog 并只选最新原件，不回退
旧成功；效率统计保留旧执行身份，继承项不签发新的 queue/未执行观察区间或成本。
两项均以实际保留 API/ZIP 复现，并增加对应回归。产品、安全、1000+100 次数和
M2 正式收口合同不变。

第二轮本地验证：逐文件一致的 Linux 文件系统快照 `make check` 实际执行 467 项，
46.962 秒、236 项可选输入缺失跳过，bootstrap 成功。显式预算的新真实 64K 单命令
执行为 50 秒、退出 0，原样完成 1000+100 周期；报告明确 300 秒预算。离线回放
真实重跑 API 的效率汇总已通过，Core 的真实调用路径使用两个同名 ZIP 先复现失败、
修复后只消费最新 ZIP，并保留旧 ZIP。无 `/tmp` 与信号/Promise 的前述原件继续保留。

`efe1927` 的 push 原生 Windows 同一条 `m2-process-js-cycles` 被通用 30 秒
supervisor 终止（无功能断言错误、timed_out=true），该失败原件另存。长循环预算
改为在 SDK 权威表统一声明，native/guest 共用 300 秒，普通原生用例仍使用原
`--timeout`，其他 guest 仍为原通用限时；不改 Runtime 自身期限或周期数。
共享声明后的完整本地检查执行 468 项，49.123 秒、237 项可选输入缺失跳过，
bootstrap 成功。旧 SDK 供应因 consumer recipe 不符被正确拒绝，未绕过校验；
对应的新 Ubuntu 供应构建及原生报告另行保留。

最后补齐两项边界：POSIX 管道允许接受正数字节前缀并同时产生 SIGPIPE（Linux
`fs/pipe.c` 的写循环在已有 ret 时仍发 SIGPIPE）。共享写入边界对 EPIPE 或短写
检查并消费本次新 pending 信号，保留已接受字节数；完整成功写入期间的 Host 信号
保持正常交付。真实字节前缀/可控信号的回归先失败、后成功。native 独立聚合还从
同一权威表核对显式整数预算；两处同改为299或300.0不再能绕过声明，普通预算范围不变。
最新完整本地检查为470项、46.424秒、237项可选缺口跳过，bootstrap成功。

本机默认临时输入拷贝阶段另有 errno112 失败，观测 C 盘仅约60MB；未把它作为产品
Process 功能失败处理。同字节 SDK4 使用仓库 out 下新的一次性临时根后 Windows
228项及1000生命周期通过、另有既有optional1；不修改全局环境，失败报告保留。
