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
