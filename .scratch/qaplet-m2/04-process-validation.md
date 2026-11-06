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

## Windows Full 失败后的当前修复与重验

2026-11-03，独立修复分支 `codex/m2-04-windows-fix`，工作区
`D:/Workspace/dairn/qaplet/.cache/worktrees/m2-04-fix`。从集成祖先 `196cd75` 开始，
交付前快进合入 `78cce5c`；该集成增量仅更新 02 的验收文字，11 个实际源码/配方输入未变。
本节报告均在该修复工作区的 `out/m2-04-fix/`，旧工作区、原 ZIP 和 Full 原件未覆盖。

### 原始红色结果与诊断边界

读取主工作区 `out/qaplet-m2/full-196cd75/HANDOFF.md`、
`raw-replayed-001.json`、`source-and-producer-verified-002.json` 与真实 Windows suite raw，
再次执行 `replay_raw.py --report out/m2-04-fix/original-raw-replay.json`。
Windows process 仍只有 facts，exit_code=768、timed_out=false、stderr 空，缺 17 项。
再将输入缩到该一条 facts 和原 suite 元数据，直接调用本票 collector，稳定拒绝缺项；
没有改 consumer 判据使其通过。`windows-original-minimal.json` 保留最小重放输入。

先列出四个可证伪假设并交 root：literal `/tmp` 根不可用、早期 realpath 失败、
Unicode mkdir/realpath 失败、宿主文件系统访问受限。仅加有界 stage/errno 的第一轮
Ubuntu producer 在本机 Windows 为 18/18，原 APE 直接运行亦为 18 项 exit0。
原 CI producer 直接经本机 CLI 消费被原有 CI 身份合同拒绝，失败报告
`original-local-windows.json` 保留；没有补造 CI 身份。
只改受控子进程 TMPDIR 为不存在的私有路径，literal `/tmp` 的 mkdir 仍成功。
该次 `--strace` 污染受控 stderr，导致 inherit 的 binary_exact 失败，原始输出仍在
`original-tmpdir-absent.*`，不算正常 stdio 验收。单改 COSMOSDRIVE 亦未复现早退。

因此，本地证据不能证明原 CI 具体是哪一次 syscall、哪一个 errno 失败。
当前修复移除未经保证的宿主临时根前提，并留下真实 setup 诊断；原 CI 的精确 errno
仍须由新 Windows CI 原件确认，不用本地通过替代新的 Full/Cross。

### 私有临时根与实际 cwd 表示

`run` 先通过宿主 Python `TemporaryDirectory` 创建实际独立目录，再以
`--fixture-root` 显式供应。native suite 不再回退到 literal `/tmp`，而是在该根内
用 mkdtemp 分配自己的独立子目录。原生实际 realpath 的目录字节以 hex 记录在
`process.path.temporary_directory_hex`；consumer 校验该目录确为供应根的直接子目录，
并记录根在执行前已存在、执行后已回收。controller 的 finally 和 native 的早期失败清理
分别承担各自拥有的目录；没有创建宿主全局 `/tmp` 或改变系统配置。

最初的私有根完整 Windows 执行保留在 `private-root/windows.json`：17 项通过，
Cosmopolitan 的 cwd_exact 失败。最小独立 `diagnosis/cwd.c` 在 Ubuntu 用锁定编译器构建，
Windows 实际观测 input/realpath 为 `/C/Users/.../Temp/...`，chdir 成功后 getcwd 为
`/Users/.../Temp/...`，errno0。两种表示指向同一目标。
native setup 现在短暂切换到目标，使用与 child 相同的 getcwd 取得预期字符串，再恢复
父 cwd；child 原有 strcmp 精确检查、Win32 的精确 cwd 检查和 consumer 预期保持不变。

公开 CLI 的私有根回归先红（旧入口没有独立根证据），第一轮仍因上述真实 cwd 差异红，
修正后转绿。另一条真实 Windows native seam 回归先红：旧 APE 忽略显式缺根参数，仍
运行 18 项 exit0；新 APE 对同一不存在的私有根只输出 facts，exit768，stderr 为
`stage=mkdtemp errno=2 cleanup=true`，前后均未创建缺根。
`closure/missing-root.json` 绑定当前 APE SHA，保留此实际失败命令及 raw。
这证明缺根能造成与原 CI 相同的早退形状，仍不声称它就是原 CI 已证实的原因。

guest 明确供应其临时文件系统 `/tmp`，native 仍独立 mkdtemp 并回收；manifest 和只读
import 同时要求完整的显式 loader/program/root argv。没有 shell 或隐式 fallback。

### 当前同字节闭环

`closure/build.json` 重新签发当前全部 11 个 LF 源码/配方输入，
`closure/source-current-verified.json` 逐项重算 SHA/长度全部相同。
当前 APE 为 971648 bytes，SHA-256
`f52b62bafcad30d9bcb952cbf455fe790d465f46e3070e007eea6b4de677680c`。
供应 PE 仍为 9216 bytes，SHA-256
`17943b060e32dba29fe1c735af8a911e0213e216a570814af5a48a8fc929196a`；
PE 源码、配方、执行前后 SHA 核验均保留。

| 当前原件 | 实际范围 | 结果 / report ID |
| --- | --- | --- |
| `closure/build.json` | Ubuntu 固定输入、fat APE 与无 CRT PE 配方 | pass / `6df2650c-baf0-49f7-b87f-49fdc48354f6` |
| `closure/windows.json` | Windows x64 / 4096 / native | 18/18 / `4ace0ae4-0056-432d-8565-56924ec39e19` |
| `closure/linux.json` | Linux x64 / WSL kernel 6.6.87.2 / 4096 | 13/13 / `28d6601d-066e-4e47-876c-657726b5b926` |
| `closure/guest-64k.json` | 实际 ARM64 完整系统 guest / 65536 | transport pass / `c1340521-7a67-4f69-b871-4d47ea9ab570` |
| `closure/guest-import-windows.json` | 同摘要 APE/loader、实际 raw 与执行后 payload SHA 只读复核 | 13/13 / `bb0128f1-f397-436a-99ad-a00bd70af9d0` |

当前同字节完整 suite 保留复杂 argv、环境完整替换与 Cosmo 协议差分、三路任意字节、
EOF/exit 两种顺序、强制终止/reap、三 HANDLE 白名单、文件对象身份检测与故意继承
sentinel 正向控制。未借用旧 APE 的通过身份。
Windows 公开 CLI 合同 11/11，Ubuntu 9 项通过、两项 Windows 专属明确不适用；
当前真实 guest 的缺项、重复、false-success 和 payload-changed 四种变体均实际拒绝。
日志是 `closure/{windows,linux}-contracts.{stdout,stderr}.log`。

实际命令延续本页原入口，构建/guest 缓存显式使用主工作区物理 `.cache/`：

```sh
python3 scripts/m2_process.py build \
  --toolchain-cache /mnt/d/Workspace/dairn/qaplet/.cache/toolchains/cosmocc \
  --helper-source-cache /mnt/d/Workspace/dairn/qaplet/.cache/sources/ape-loader \
  --build-dir /mnt/d/Workspace/dairn/qaplet/.cache/build \
  --output-dir out/m2-04-fix/closure/artifacts --report out/m2-04-fix/closure/build.json
python3 scripts/m2_process.py guest-manifest \
  --build-report out/m2-04-fix/closure/build.json \
  --artifact out/m2-04-fix/closure/artifacts/m2-process.com \
  --loader out/m2-04-fix/closure/artifacts/ape-aarch64.elf --page-size 65536 \
  --manifest out/m2-04-fix/closure/guest-manifest.json --report out/m2-04-fix/closure/manifest.json
python3 scripts/arm64_guest.py run \
  --cache /mnt/d/Workspace/dairn/qaplet/.cache/guests --page-size 65536 \
  --manifest out/m2-04-fix/closure/guest-manifest.json --report out/m2-04-fix/closure/guest-64k.json
```

### 仓库检查与仍待范围

当前 WSL 仍使用本工作区独立 ignored Git shim：只为当前仓库的只读命令映射 Git
metadata/work-tree，worktree 只处理 list；其他 fixture、init 和其他 `-C` 原样透传，
未设置全局 GIT_DIR、core.worktree 或共享配置。首次 shim 未处理 git_read 的 `-c` 前缀，
实际失败保留在 `validation/dev-check.json`；修正 shim 后重新执行完整入口。

`validation/dev-check-002.json` 的 preflight、输入锁和 CI 合同通过，但完整 363 项合同
为 30 fail、1 error、187 skip。失败均位于 nightly_toolchain：实测 D 盘仅
4167708672 bytes，低于原有默认 4 GiB（4294967296 bytes），因此 CLI 在合同目标前被
容量门禁拒绝。原始 stderr/stdout 保留在 `validation/check-002/`；未降低阈值，未删除
共享缓存、源码或原件，也没有将这次完整 check 记为通过。
未进入的真实 C bootstrap 随后独立执行，通过报告在
`validation/bootstrap-only/bootstrap.json`。

当前新 APE 的 macOS 两原生、Linux ARM64 native、新 Windows CI 和完整 Cross/Full
交 root 继续采集。原 CI 的准确 syscall/errno 仍未知；本票保持 awaiting_cross，
不 resolved，不宣称 Runtime 产品支持或父 M2 已验收。

### 容量恢复后的完整补检

root 对静态 `.cache/build/sdk` 做标准 NTFS 压缩后，主工作区环境收据
`out/qaplet-m2/cache-compression-verified.json` 记录 4783 个文件前后数量一致、
内容摘要变化 0、reparse 条目 0，D 盘可用 4862255104 bytes，超过原有 4 GiB 门槛。
未降低门槛，前述真实容量失败及全部日志保持原样。

在同一 Ubuntu 上、沿用本工作区只读 Git shim 与主物理缓存，实际补跑：

```sh
python3 scripts/dev.py check \
  --output-dir out/m2-04-fix/validation/check-003 \
  --report out/m2-04-fix/validation/dev-check-003.json
```

新报告 `dev-check-003.json` 为 pass，实际 `make check` exit_code=0、timed_out=false：
preflight、输入锁、CI 合同和完整 363 项合同全部完成，其中 176 项实际通过、187 项按其
显式输入/目标条件跳过；真实系统 C bootstrap 同轮通过。
完整 stdout/stderr 和 bootstrap 报告位于 `validation/check-003/`。
没有补做大构建或改产品配方；交付前重核 closure 的 11 个源码/配方输入，摘要全部未变。

补检后确认已合入最新 integration `1e1908f`，本分支合并提交为 `7f099b9`；该增量只改
实施记录和磁盘恢复说明。上述本地通过仍不替代待采集的新 Windows CI、其原始失败
syscall/errno，以及当前新 APE 的 macOS/ARM native 和完整 Cross/Full。

### 当前配方 Cross 已完成

后续 Full `41616881692` / `0e4ba21` 整体成功。独立读取五原生和实际 64K 原件，83/83 项重算通过，源码/配方、供应 APE/实际 PE child、loader、CI/宿主身份与 guest 清理完整核验；本票已 resolved。严格摘要、不可变原件选择及边界见主 `out/qaplet-m2/full-0e4ba21/HANDOFF.md`、`verify/scope-summary.json`。这是原执行证据重放，未计新 native 执行；原 CI 失败 syscall/errno 仍未从旧 raw 证实，不以本地受控 errno 替代。修复树已在重新源对档核验后清理，冻结输出保存在主 `out/qaplet-m2/tickets/04-windows-fix/`，分支保留。
