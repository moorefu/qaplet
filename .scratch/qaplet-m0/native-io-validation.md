# BOOT-07：Windows / Unix 原生 I/O 基线

2026-11-01，`codex/m0-08-native-io` / `out/implement-spec/08` 实施，开工基于 integration `dc5c0c3`，已核验 ancestry；交付前合入最新 integration。状态保持 **claimed**：原生本地基线已实现，完整 native CI、ARM64 页组合和完整系统 guest 尚须真实运行；M0 Core 保持 `not_run`。这些探针不实现 Node、Reactor、Runtime 或 Embed SDK。

## 公共接口与身份

`scripts/native_io.py build` 只在 Ubuntu 构建独立 `native-io.com`，使用 05 的原输入离线校验、官方派生 ELF helper、双 CPU 编译、headers/maps/symbols/PE imports 审计，以及构建后的原输入和派生目录复核。独立制品、sidecars、maps、BUILDLOG 与 `build-info.json` 保存在指定输出目录；编译中间文件只在主工作区 `.cache/build/<uuid>/`。不读取 QuickJS 源码，不将此探针身份当作 Hello/eval 身份。

```sh
python3 scripts/native_io.py build \
  --toolchain-cache /mnt/d/Workspace/dairn/qaplet/.cache/toolchains/cosmocc \
  --build-dir /mnt/d/Workspace/dairn/qaplet/.cache/build \
  --output-dir out/ticket08-final/artifacts \
  --report out/ticket08-final/build-info.json
python3 scripts/native_io.py run \
  --artifact out/ticket08-final/artifacts/native-io.com \
  --build-report out/ticket08-final/build-info.json \
  --loader-dir ../06/out/ticket06/loaders \
  --expected-os Linux --expected-arch x86_64 \
  --report out/ticket08-final/linux.json
```

消费端不会编译或填充输入缓存；先核对原制品 SHA-256/长度、producer scope/version、source/build 身份和双 CPU 审计，再执行相同字节。Linux/macOS 从 06 的 `helpers.json` 选择实际 OS/架构对应 helper，并核对摘要/长度；本地也可显式传入锁定的 `--loader`。Windows 直接执行 Ubuntu 制品。消费端将原字节复制到独立 evidence 下的 `进程 空格 fixture/native io.{com,exe}` 并复核摘要，以该路径执行/派生子进程；不用隐式 shell。结束后再核对原件、fixture 和 helper。

`native-io` schema v1 的 `scope` 为 `build` 或 `host`。根级记录 source、toolchain、build、artifact、host、ci、qemu（本地 native 为 null）、producer、runner 当前文件摘要；每个 case 记录期望值、实际值、状态、开始/结束、deadline、退出码、artifact 和实际命令/原始日志。host 的 CPU/执行架构/kernel/页大小来自实际 OS API，C facts 另外断言执行架构/页大小一致；CI runner label 和 image/run 元信息分别保存，不用镜像名推断页大小。`required_cases` 是必需 case inventory，缺项生成 `not_run`，报告非通过；实测设施不可用为 `blocked`，失败为 `fail`。`limitation` 只允许有实际返回值和明确原因的观测，不等于未测；当前本地通过项不形成其他 OS 的支持声明。

## 操作和清理合同

| 类别 | 实际操作 |
| --- | --- |
| socket | TCP 127.0.0.1 临时端口；空 poll 50 ms、写入后可读、SHUT_WR 后 EOF、反向仍可传输、无效 fd 的 EBADF；关闭并用 F_GETFD/EBADF 复核全部三个 socket |
| pipe | 空 poll、写入后读取、关闭 writer 后 EOF、忽略 SIGPIPE 后 write 返回 EPIPE；四个 fd 均关闭并复核 |
| Unix TTY | 单独 pipe 的 isatty/tcgetattr 对照；posix_openpt/grantpt/unlockpt/ptsname + O_NOCTTY slave；实际检测、切换 ECHO、读取确认、恢复并比较 flags/control chars；关闭全部 fixture fd |
| Windows Console | 无可见窗口的私有 ConPTY + 显式 CreateProcessW；子进程拥有独立 CONIN$ handle，实际 GetConsoleMode/SetConsoleMode 切换 ECHO、确认、完整恢复、CloseHandle 后验证无效；父进程回收子进程、关闭 ConPTY、四个 pipe handle、线程/进程 handle 并等待 drain thread |
| process | 原生 posix_spawn + waitpid；同一 Unicode/空格路径、Unicode/空格/quote/backslash/shell 元字符 argv 校验，正常非零 exit 37；不存在程序的 ENOENT；观察挂起子进程仍存活，150 ms deadline 后 SIGKILL、wait、再次 wait 得到 ECHILD |
| pthread / ZipOS | 新线程 TLS 初始 17、线程改为 99、parent 保持 7，pthread_join 完成；独立 append 的固定 ZIP 资源读取 18 字节及 EOF、写入打开 EROFS、关闭 fd |

外层每条命令默认 30 秒，`--probe-mode hang --timeout 1` 是明确的负向 fixture：超时必须 `fail`，保留实际终止/回收退出码，不能生成成功。Unix 使用独立进程组，Windows 使用受控进程树清理；ConPTY 另有终止/等待和关闭/reader 的 cleanup deadline。没有宿主网络或全局 WSL/binfmt/mount 设置变更，没有应用私有 SCM/Job 操作。

## TDD 与实际发现

用户已确认 seam 为外部 CLI、实际 OS 和结构化 evidence。逐步红绿：缺 producer/制品从无报告到 blocked；真实 socket 先缺项后通过；pipe 的 EOF/EPIPE/清理先缺项后通过；TTY 先缺项，再保留真实失败后修复；process 先缺项后验证 Unicode 路径/argv 和 bounded cleanup；pthread/ZIP 先缺项后通过；外层 timeout 从未识别参数到实际 kill/reap 且 fail；CI 先能漏过 consumer 编译，再拒绝它。摘要篡改回归证明工具执行前拒绝，`commands` 为空。

保留的失败与范围：

- `out/ticket08-tty2/evidence/*/link.stderr.log`：两个 CPU 同时使用 common-page-size=65536 时 x86 real-mode PC16 relocation 超界。随后验证表明原 apelink 的 ARM64 16K repack 也不能由公开 flags 单独修正；07 负责共享 64K source-built loader/packer 修复。当前 native 基线保持官方默认布局，**没有宣称 ARM64 64K 可运行**。
- `out/native-contracts/native-io-osuzby_0/` 等：最初 openpty slave 获得 controlling terminal；模式恢复完成后关闭 master 触发真实 SIGHUP（exit -1），缺 cleanup case。改为显式 O_NOCTTY 创建 master/slave 后，同一进程组清理路径通过。
- `out/ticket08/conpty-control/` 和 `out/ticket08-tty3/`：ConPTY 存在不表示 GetStdHandle 指向 Console；父进程的重定向句柄可能继续存在。最终 fixture 独立打开其私有 CONIN$，另存 `standard_handle_console` 观测，避免把 pipe 当 Console。所有模式操作只针对这个私有 Console；结果写入独立文件，原始 terminal stream 仍保留。
- `.yoink` 不受锁定 assembler 支持的编译失败保存在 `out/ticket08-resource/`。按工具链随附 README，普通 `open('/zip/...')` 已能使用 ZipOS；最终没有额外 linker 魔法或修改供应输入。

开发 tracer 的完整工具链校验在 `out/ticket08/` 和 `out/ticket08-pipe/`；后续快速迭代明确标为 `development_rebuild`，引用先前已核验 native view，最终 producer 重新完整校验。所有这些报告、产品及原始日志均为 ignored `out/` / 主 `.cache/`，不提交生成物。

## 最终本地 native 基线

最终 producer：`out/ticket08-final/build-info.json`，report ID `1c6ac8db-6bd7-472a-a845-0922533653af`，status `pass`；原工具链前/后 verify、派生 helper 复核和双 CPU 审计均通过。独立 `native-io.com` 为 **884428 字节**，SHA-256 **`8d34081ddbff7ef2b0d705ebeae5ab75bc23c015f9267fb70d039ad87224aebe`**；build SHA-256 为 `8e40bc177f6d89d619bef574cb554280b88c2f5942c50ad11783bda4fd1eccc5`。Linux git 无法解析 Windows 创建 worktree 的 `D:/.../.git/worktrees/08` 指针，实际 query 失败日志保留在 producer evidence；`source.git_commit` 因此保持 null，源码/所有 recipe 的实际逐文件 SHA-256 和集合摘要完整保留，没有填造提交身份。分支 ancestry 由 Windows git 独立核验。

| 报告 | 实测宿主 | 结果 |
| --- | --- | --- |
| `out/ticket08-final/linux.json` / `a2284cb6-3f90-4d55-9026-ddf01d82c236` | Linux / x86_64 / native / kernel 6.6.87.2-microsoft-standard-WSL2 / 4096 | 20/20 required cases pass，同摘要 fixture 与 helper 运行后复核一致 |
| `out/ticket08-final/windows.json` / `ebcf612d-f292-4558-9728-f5e122f7db61` | Windows / x86_64 / native / kernel 10.0.22000 / 4096 | 21/21 pass，含独立 Console handle 关闭验证 |

两个宿主的原生 wait 状态均为 9472，WEXITSTATUS 为 37；挂起子进程在 150 ms 观察边界后终止，raw wait status 为 9，第二次 wait 的 errno 为 ECHILD (10)。线程实际 TLS 为 initial 17 / child 99 / parent 7，pthread_join 成功。Windows 私有 Console mode 实际 503 → 499 → 503，关闭独立 CONIN$ 后 GetConsoleMode 失败 / Win32 error 6；ConPTY、reader、pipes、thread/process handles 均完成清理。`isatty(0)=0` / `standard_handle_console=0` 的实测值保留，不能从私有 Console 存在推导标准输入是 TTY。

持久负向证据：`out/ticket08-final/timeout-linux.json` 为真实 1 秒 deadline、status fail、进程回收完成和原始终止退出码；`out/ticket08-final/corruption.json` 为末尾 ZIP 资源字节变更后，在任何工具执行前拒绝 SHA-256，commands 为空。Windows/Linux 全部 9 个 focused CLI contracts 均通过；Ubuntu `make check OUT=out/ticket08-final/check` 通过 77 个合同（6 个明确非本票/重型可选或平台 skip）、静态 CI 合同和实际系统 C bootstrap。完整检查后的共享 measured-host/loader 接入另经同一最终 artifact 的全部 focused contracts 及显式 bundle-consumer 命令复核。

## 尚待验收

本地 Windows x86_64 与 WSL Ubuntu x86_64 为 native / 4096 字节页。正式同摘要 native CI 五个 runner 由共享 `.github/workflows/m0.yml` 编排：Ubuntu producer 构建一次；五个消费 job 运行 startup 和 I/O，分别上传失败/原始日志。I/O 即使 startup 先失败也尝试采集，不在消费 job 编译。

macOS x86_64、macOS ARM64、Linux ARM64 原生实际页配置及 ARM64 完整系统 guest 在此记录写入时尚未执行，保持待验。07 的 guest runner 需要 guest 内 lo UP、devpts 和 /dev/ptmx；不会开启宿主网络或重复实现 guest 基础设施。64K 修复会改变 artifact ID；届时必须重新生成同摘要宿主证据，不能沿用旧 artifact 的通过记录。完整 M0/08 在实际所需宿主、缺失页配置和聚合门禁完成前保持未关闭。
