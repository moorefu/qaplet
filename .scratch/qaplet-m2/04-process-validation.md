# 04 · 真实子进程、stdio 与继承隔离验证

2026-11-03。范围是独立 `m2-process.com` backend 探针和同目录供应的
`m2-process-winchild.exe`。没有修改旧 M0 源码或配方，不签发 Runtime 产品支持、
整树清理或温和终止能力。macOS 两目标和 Linux ARM64 native 仍待统一 Cross；本票保持
`awaiting_cross`，不是 resolved。

## 配方与公开入口

从集成祖先 `791af3d` 开始，先快进 `8fd870e`，交付前合入 `0460f71`；没有 reset。
实现文件是 `scripts/m2_process.py`、`scripts/m2_process_pe.py`、
`tests/native/m2_process.c`、`tests/native/m2_process_winchild.c` 和公开 CLI 合同。
producer/consumer 延续 02 的输入锁、源配方、APE/loader 身份、实际执行日志和只读 guest
复核方式；M0/M1 的原有成绩保持原范围。

全部编译在 Ubuntu-22.04，输入和中间文件显式指向主工作区物理 `.cache/`。
Windows 不编译。无 CRT 的 PE 子程序由固定 cosmocc 的 x64 GCC 生成 freestanding 代码，
同一固定 ld 链接，明确导入 KERNEL32/SHELL32。自有小型 PE 包装只映射 `.text/.rodata`
和导入表，固定基址；没有 Windows SDK、MSVC 或宿主 DLL 字节参与构建。
PE 的源码、包装配方和命令均在 producer 身份中。

```sh
python3 scripts/m2_process.py build \
  --toolchain-cache /mnt/d/Workspace/dairn/qaplet/.cache/toolchains/cosmocc \
  --helper-source-cache /mnt/d/Workspace/dairn/qaplet/.cache/sources/ape-loader \
  --build-dir /mnt/d/Workspace/dairn/qaplet/.cache/build \
  --output-dir out/m2/process/closure/artifacts \
  --report out/m2/process/closure/build.json

python3 scripts/m2_process.py run \
  --build-report out/m2/process/closure/build.json \
  --artifact out/m2/process/closure/artifacts/m2-process.com \
  --loader-dir SUPPLIED_NATIVE_HELPERS --report out/m2/process/closure/linux.json

python3 scripts/m2_process.py guest-manifest \
  --build-report out/m2/process/closure/build.json \
  --artifact out/m2/process/closure/artifacts/m2-process.com \
  --loader out/m2/process/closure/artifacts/ape-aarch64.elf \
  --page-size 65536 --manifest out/m2/process/closure/guest-manifest.json \
  --report out/m2/process/closure/manifest.json
python3 scripts/arm64_guest.py run \
  --cache /mnt/d/Workspace/dairn/qaplet/.cache/guests --page-size 65536 \
  --manifest out/m2/process/closure/guest-manifest.json \
  --report out/m2/process/closure/guest-64k.json
python3 scripts/m2_process.py import-guest \
  --build-report out/m2/process/closure/build.json \
  --artifact out/m2/process/closure/artifacts/m2-process.com \
  --guest-report out/m2/process/closure/guest-64k.json \
  --report out/m2/process/closure/guest-import.json
```

Windows 使用同一 `run`，省略 loader，另加 `--expected-os Windows --expected-arch x86_64`。
脚本启动前核对同目录 PE 子程序的 producer SHA/长度，再显式传入 native child。
guest 文件全部使用 `/payload/m2-process/` 前缀；命令是显式
`[loader, program, --suite, --launcher, loader]`，子进程也使用同一供应 loader，无 shell。
初次导出沿用 02 路径的问题已由公开 CLI 的真实 red/green 修正，旧输入和报告保留。

## 实际行为与限制

- 在带 Unicode/空格的目录逐字节复制供应程序，再以该显式路径启动；argv 包括空参数、
  中文、空格、双引号、尾部反斜杠、单引号和 shell 元字符。子进程逐项验证边界，cwd
  使用独立 Unicode/空格目录；缺程序和缺 cwd 保留实际 ENOENT、无 child。
- stdin/stdout 使用 `49 00 ff 4e 0a`，stderr 使用 `45 00 fe 52`。
  pipe/inherit/ignore 均由真实子进程执行；inherit 借用受控三路标准流并恢复父流。
  ignore 使用真实 null 端点，不承担等待宿主流 EOF 的责任。
- EOF-before-exit：child 主动释放它拥有的全部输出端点后，父方已读到两路 EOF，
  child 仍在 stdin gate 上存活；父方随后释放 gate 并回收。
  exit-before-eof：先实际 wait/reap，再读取两路各四字节保留输出及 EOF。
- 非 CLOEXEC waker pipe、文件、socket 和高位 fd 197 同时存在。授权 stdio dup2 后关闭
  本探针拥有的有界描述符集合；child 检查无额外 fd，且 child 存活时父 waker 已 EOF。
  这是有界 fixture 的证明，不是任意进程 fd 枚举或产品关闭算法。
- 已保留一个真实 EOF 挂起实验：Cosmopolitan 启动时有额外 trace stderr OS duplicate，
  Linux `/proc` 显示为 fd 100，且它不在通常 fd 表中；只关闭 fd 2 不产生 stderr EOF。
  受控 child 明确释放该 pinned-libc trace duplicate 后才宣告端点关闭。报告保留该观测，
  不声称任意 Cosmo 应用只关闭 fd 2 就结束 stdio。
- 通过 child 的 ready gate 确认直接子进程存活，再强制终止并 wait/reap；POSIX/Cosmo
  waitpid 保留 raw wait status、signal 9 和重复 wait 的 ECHILD。
  Windows 另用真实 TerminateProcess，保留 Win32 退出码 `53261`、wait 返回和关闭后
  WAIT_FAILED / error 6，不把 Cosmopolitan wait status 冒充 Win32 原始退出码。

Windows 的 `posix_spawn(envp)` 严格环境替换没有通过：显式 envp 两项，到应用 environ
有 13 项。探针分别读取实际 Win32 OS block、应用 environ、下划线协议条目、libc-only
和 OS-only 名字，区分协议传输与启动处理；没有过滤应用环境来制造成功。
本机 OS block 实际 14 项，其中 8 项为下划线协议字段；应用 environ 移除了 OS-only
`_COSMO_FDS_V2`，没有额外的 libc-only 名字。因此不能把 USER/TERM 等额外条目直接
归因于 child libc 新生成：本次它们已经存在于 OS block。OS/app 两域的名字与计数都保留。
原生 CreateProcess 则使用显式 Unicode environment block 和三 HANDLE 白名单，供应的
无 CRT child 实际只看到两项环境，同时通过 argv/cwd/任意字节 stdio 检查。

最初仅按 HANDLE 数值是否有效检测继承会误判：child 启动可把该数字分配给另一对象。
`final/windows-repeat/6.json` 保留该失败。修正后的 sentinel 使用独立文件的
volume serial + file index 身份，并单独记录数字复用。无 CRT child 的正向控制实际
继承 sentinel，必须识别相同文件对象并以 44 退出；正常三 stdio 白名单必须拒绝该对象。
`object-identity/repeat.json` 记录 50 套 Windows 全部通过，并真实观察 2 次数字复用。
该重复运行是诊断回归，不替代正式 producer 和 Cross。

## 证据与状态

最终原件保存在当前 worktree 的 `out/m2/process/closure/`；早期 `red`、`slice1`、
`prototype`、`accepted`、`final`、`verified` 和对象身份诊断均保留，没有覆盖为成功。
下表记录最终 closure 的同字节矩阵和原始报告 ID。

最终 APE 为 970927 bytes，SHA-256
`ab5ab594406167e2ac3eae7fc876f2d39964a4ef0304a5d355258fac7f9ea51d`；PE child 为
9216 bytes，SHA-256 `17943b060e32dba29fe1c735af8a911e0213e216a570814af5a48a8fc929196a`。
三个目标消费同一 APE 字节，未复用早期制品的通过状态。

| 原件 | 实际目标及范围 | 结果 / report ID |
| --- | --- | --- |
| `build.json` | Ubuntu 固定输入、fat APE 审计、无 CRT PE 配方 | pass / `a3b09bb6-327e-4025-904d-54a1f23b81c2` |
| `linux.json` | Linux x64 native，kernel `6.6.87.2-microsoft-standard-WSL2`，4096 页 | 13/13 pass / `e43b805c-847f-4031-9fed-10cf0799baba` |
| `windows.json` | Windows x64 native，kernel `10.0.22000`，4096 页 | 18/18 pass / `f4012acb-3950-42fa-b690-b8dbb04c11c9` |
| `guest-64k.json` | 实际 ARM64 kernel `6.8.0-147-generic-64k`，页大小两次查询 65536、smaps 64 KiB | transport pass / `1639e192-b861-4b78-ac70-4941b13ce68e` |
| `guest-import.json` | 同摘要 APE、显式 loader、串口/用例日志、全部子进程观测只读复核 | 13/13 pass / `4c89f7af-5f4c-4b3b-81ba-2e4109871e37` |

guest 使用固定 QEMU 6.2.0、`virt-6.2,gic-version=3`、`cortex-a72`、
`tcg,thread=single`；kernel/config/rootfs/QEMU/serial 和 payload 的前后摘要均在 transport
报告。64K 不是 qemu-user 或仅 LOAD alignment；不外推物理性能或未测原生 ARM64。

公开 CLI 的真实 red/green 包括：缺供应物、只有 facts 的最小 APE、缺 stdio 状态观测、
guest 前缀冲突，以及实际 Windows 环境差分/EOF 挂起/HANDLE 数值复用。
正负合同拒绝改 APE/PE 字节、缺失输入、输出覆盖原件、外部 deadline、缺项、重复、
保留 pass 但修改观测字段，以及 guest payload 的执行后摘要变化。
最终 `closure/negatives/` 保留四种实际失败报告与原始日志；deadline 仍是 fail，不能作为
探针已正常取消或清理的成绩。

closure 的独立 CLI 合同在 Ubuntu 为 9 项中 8 过、仅 Windows 子程序替换用例明确不适用；
Windows 为 9/9 通过。四个 guest 变体在两平台均实际拒绝，改变后的日志摘要也会重算，
没有用 synthetic success 代替真实 guest。两平台完整测试输出分别保存在 closure 的
`linux-contracts.*.log` 和 `windows-contracts.*.log`。

Ubuntu `make check` 首次因 Windows `.git` marker 在 WSL 无法解析而失败，完整 stack
指向 SDK 的 `workflow_support.workspace`。ignored Git wrapper 只对当前仓库的只读
命令显式指定 git-dir/work-tree；worktree 只映射 list，fixture init / `-C` 其他仓库
均原样交给系统 Git，不设置全局 GIT_DIR，不修改任何共享 Git 配置。
受影响证据合同 10 项及 SDK 3 项已重放通过，随后完整 349 项合同、178 项明确跳过和
真实系统 C bootstrap 通过。跳过的其他票/完整矩阵输入不计入本票实际平台成绩。
合入最新集成后的检查原件另保存在 `out/m2/process/validation/`。
最终合入 `0460f71` 后，`make check OUT=out/m2/process/validation/final-integration`
完成 361 项合同（185 项按其显式输入/目标标记跳过）、CI/输入锁合同和真实 C bootstrap，
退出 0。最终 producer 的 11 个源码/配方文件在交付前逐项重算 SHA，与报告完全一致。
