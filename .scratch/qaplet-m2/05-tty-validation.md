# 05 · 真实 TTY / Console 本地验证

2026-11-03：独立 OS 探针与公开 producer/consumer CLI 已实现，本地 Linux x64 4K、Windows x64 4K 和完整系统 Linux ARM64 64K 已实际通过。macOS x64/ARM64、原生 Linux ARM64 及集成 Full 仍由集成分支验收；票保持 claimed，不以本地成绩 resolved。

## 配方与供应身份

- 新入口 `scripts/m2_tty.py`，原生源 `tests/native/m2_tty.c`，公开 CLI 合同 `tests/contracts/test_m2_tty.py`。
- `scripts/m2_tty_terminal.py` 是从既有 `native_io_terminal.py` 的私有 ConPTY 生命周期设施派生的扩展，增加显式 resize / 输入门控和原始字节返回。没有修改 M0 配方、源或 helper，也没有公开完整 PTY 创建 API。
- 最终 Ubuntu producer 的源码提交为 `4044c7e4cd62f32c076ad62474a3fb993bbd6b6b`，包含本票实现，已合并 `af6c0ff`。逐一复核所有 recipe 文件与当前提交后的实际字节一致。早期 accepted producer 基线 `c338ca9` 及 final producer 基线 `af6c0ff` 原样保留，最终验收只选下述 verified 原件。
- 供应 `m2-tty.com`：980739 bytes，SHA-256 `5af6aa6ba0008c65370d55aad0596bbc7891237774afbb25c8b2b60c2a3ded6d`。
- 最终源/配方聚合摘要 `4bfc6e4bc3dc21a24e7c203cdc2634c752f9acee26601e0c3bf2e9be841f46e4`；构建配方摘要 `e5b4e1733f2460bb141fdab6c5d4264f1194dfee5c60054849a56866401b17be`。producer 包含固定输入前后复核、完整 toolchain tree、原 ZIP、双 CPU ELF LOAD、PE import 与供应 helper 身份。加强 consumer 和规范化配方换行后，新旧构建 APE 摘要仍完全相同；最终仍重新执行全部三个目标。
- 固定 `files.h` 自带重复的 `NtSidIdentifierAuthority` 定义，最初真实编译失败。探针使用该锁定头中的原样 `DuplicateHandle` 函数声明，链接实际 Cosmopolitan wrapper；Windows 的复制、共享模式、关闭和 owner 存活均实际执行，不以声明或编译成功代替 ABI 验证。

## 实际观测与边界

| 能力 | 本票实际行为与判据 |
|---|---|
| 实际 stdin | fd0 经真实 pipe、文件和关闭路由分别检测；POSIX 再将私有 slave 路由到 fd0，实测 TTY 后恢复原 fd0。Windows 普通进程与 ConPTY 子进程原始 fd0 均实测为 `other`，没有据私有 `CONIN$` 成功改称 Console。 |
| Windows Console 路由 | 显式 `SetStdHandle(STD_INPUT_HANDLE, owned_CONIN)` 后，对真实 `GetStdHandle` 读回 Console 能力并还原原 HANDLE；同时记录 libc fd0 未被路由、仍为 `other`。这是独立 native Console 路由，不宣称 libc fd0 已变为 Console。 |
| 模式租约 | fixture 的显式独占状态拒绝第二次 acquire；保存、修改、读回并恢复。每条结果保留 `before`、`modified`、`restored`，POSIX 含全部四类 flags、控制字节与输入/输出 speed，Windows 含 input/output mode。consumer 要求 before 与 restored 完全相同且 modified 不同。该最小租约原型是 OS 事实，不是已实现的 Runtime/SDK 租约 API。 |
| 四条恢复路径 | normal 正常释放；error 在持有租约时执行真实失败的模式查询；stop 由 helper 经 mutex/condition 发布协作请求，owner 观测并 join 后恢复；close 对实际租约持有的复制端点恢复、关闭并核对 owner 仍可用。再次释放幂等。强杀不进入恢复保证。 |
| resize | POSIX 子进程成为私有 slave 的 session / foreground owner，先阻塞 SIGWINCH；父进程真实执行两次 TIOCSWINSZ，子进程 sigwait 观测合并通知和最终尺寸后退出并回收。Windows controller 真实两次 ResizePseudoConsole，C 端读取 WINDOW_BUFFER_SIZE_EVENT 和最终 screen buffer。两端均从 80×24 到 90×31 再到 100×33，最终通知/查询一致。 |
| 分块文本 | POSIX raw PTY 输入/输出逐字节传输 `A中🙂Z`，独立 literal hex `41e4b8adf09f99825a` 与严格 scalar decoder 双重核对。Windows ConPTY 逐字节输入实测支持 `A中Z`，事件单位 `[65,20013,90]`；输出适配器将分块 UTF-8 严格解成 scalar，再用 WriteConsoleW，原始 ConPTY 日志包含完整 `A中🙂Z` 字节。 |
| 非法数据 | adapter 拒绝 overlong、surrogate、超出 Unicode 上限、truncated 和 stray-continuation 五类输入，未输出替换字符或将其写到 Console。POSIX raw PTY 完整保留 `c0afeda08051` 后由严格 decoder 拒绝；Windows 对同一原始非法输入仅收到 sentinel `[81]`，明确记录原始非法 UTF-8 检测 `UNSUPPORTED`，不把 OS 丢弃/替换后的字符当原字节证明。 |
| 补充平面输入 | 本机 ConPTY 对 `f09f998251` 实际仅收到 `[81]`，记录 `UNSUPPORTED`。原始事件数组与输入控制器逐字节写入记录均保留。该限制不被 BMP 输入或补充平面输出的成功掩盖；其他宿主仍须实测，consumer 只接受事件与能力标签一致的结果。 |
| VT 子集 | 仅 SGR 31 与 reset。POSIX 证明 raw PTY 精确字节 `1b5b33316d581b5b306d`，不声称渲染；Windows 实际读取 screen cell 为 `X` (88)、红色前景，再恢复模式。 |
| 二进制 pipe | 真实 pipe 保留 0–255 所有 256 种 byte，包括 NUL 与非法 UTF-8，关闭两端。与 Console 文本判据分开；Console 任意二进制明确 `UNSUPPORTED`。 |
| 借用与取消 | POSIX dup 实测共享 termios 与 O_NONBLOCK，修改后完整还原；Windows DuplicateHandle 实测共享 Console mode。helper 只等待自有控制管道/事件及借用端点；控制请求唤醒等待、join 后关闭自有资源，核对原 owner 仍可用。取消发生在共享 read 之前；取消已开始的共享阻塞 read、无协调修改共享状态的借用均 `UNSUPPORTED`。没有强杀线程或用外层 timeout 证明原生取消。 |

全部报告保持 `product_support_verified=false`。能力限制项中的 pass 表示已完成真实观察并如实记录限制，不是发布该能力的支持声明。

## 本地矩阵与原件

最终原件统一在本票 worktree 的 `out/m2/tty/verified/`；早期 accepted、final、red 输出均保留，未删除任何输出或 worktree。producer 的同一 APE 字节用于以下三项：

| 报告 | 实测身份 | 结果 |
|---|---|---|
| `linux.json` | Linux x86_64 / 4096 / native，kernel `6.6.87.2-microsoft-standard-WSL2` | 19 / 19 pass |
| `windows.json` | Windows x86_64 / 4096 / native，kernel `10.0.22000` | 21 / 21 pass，ConPTY/process/pipe handles、reader 均已结束 |
| `guest-64k.json` → `arm64-64k.json` | Linux aarch64 / 65536 / emulated-system，kernel `6.8.0-147-generic-64k` | 完整系统固定 QEMU/TCG 运输后独立 consumer 19 / 19 pass |

guest 运输绑定实际 machine、CPU、QEMU executable/version、kernel/config/rootfs、serial log、guest 内两种页查询、所有 payload 执行前后 SHA-256 及显式 loader/program argv。`payload-64k.json` 使用独立 `/payload/m2-tty/` 前缀与 Unicode/空格程序路径。导出 `export-64k.json` 本身不计执行成绩。

公开 CLI 的 7 项合同在 Linux 和 Windows 各实际通过，0 skipped：正常同字节执行及四路径恢复、非法终端输入、缺失供应物 blocked、篡改 byte 在启动前拒绝、报告不得覆盖供应物、外层 deadline 保持 fail 且实际回收进程，以及真实 guest 原件的五种拒绝子用例。guest 子用例复制真实 raw evidence 并重新计算修改日志摘要，分别删除项、添加重复项、把 restored 改 false 但保留 pass、删除完整模式快照字段，以及篡改执行后 payload 摘要；没有 synthetic success。最终两平台测试命令与原始输出分别见 `verified/contracts-linux.json`、`verified/contracts-windows.json` 及所引日志。

## 可重放入口

在 Ubuntu 执行构建，共享缓存显式指向主工作区。以下为实际执行入口，重跑须选择新的空 output-dir 和新 report，保留既有原件：

```sh
python3 scripts/m2_tty.py build \
  --toolchain-cache /mnt/d/Workspace/dairn/qaplet/.cache/toolchains/cosmocc \
  --helper-source-cache /mnt/d/Workspace/dairn/qaplet/.cache/sources/ape-loader \
  --build-dir /mnt/d/Workspace/dairn/qaplet/.cache/build \
  --output-dir out/m2/tty/verified/artifacts \
  --report out/m2/tty/verified/build-info.json
```

`run --build-report BUILD --artifact m2-tty.com --report RUN` 消费供应字节；Linux/macOS 额外传固定 `--loader` 或供应 `--loader-dir` / `--loader-manifest`。Windows 不构建，普通用户提升上下文消费相同 PE 字节并创建私有 ConPTY。

`guest-manifest --build-report BUILD --artifact m2-tty.com --loader ape-aarch64.elf --page-size 65536 --manifest PAYLOAD --report EXPORT` 导出后，由 `arm64_guest.py run --cache /mnt/d/Workspace/dairn/qaplet/.cache/guests --page-size 65536 --manifest PAYLOAD --report GUEST` 真正执行，再用 `import-guest --build-report BUILD --artifact m2-tty.com --guest-report GUEST --report CONSUMER` 只读核验。与 waker 相同的 build/run/guest-manifest/import-guest 接口，scope 分别为 build/host/manifest/guest。

合同选择使用 `QAPLET_TEST_M2_TTY_PRODUCER`、`QAPLET_TEST_M2_TTY_GUEST_REPORT`；Linux 额外使用 `QAPLET_TEST_M2_TTY_LOADER`，随后 `python3 -m unittest tests.contracts.test_m2_tty -v`。不提供真实供应物/guest 时的已有默认 skipped 不构成矩阵；以上本票独立验收均显式选了原件。

## Red / green、失败历史与未完成范围

1. CLI 缺失时首个合同真实 red：没有供应缺口报告；添加独立 consumer 后 green 为 blocked、0 commands、无编译。
2. 原生租约合同先在旧 consumer 的错误 required-case 集合上真实 red；独立语义集建立后，首轮 Linux `posix-red.json` 又真实记录 tcgetattr(-1) 返回 EINVAL(22)，与初始夹具的 EBADF 假设冲突。保留该失败，改为记录实际非零原生错误，四路径模式恢复 green。
3. `green-posix/build.json` 保留固定头重复 SID 定义的真实编译失败；未修改锁或 M0 绕过输入审计。
4. `windows-red.json`、`windows-v3.json` 保留原始 fd0 不是 Console、open(CONIN$) 不能把 libc fd0 安全改成 Console，以及分块 emoji 输入丢失的失败；最终分开 native 标准 HANDLE 路由、libc fd0 和文本子集，原始丢失事实继续作为能力限制保留。
5. 非法终端输入的新合同在旧 producer 上真实 red（没有 text.invalid-input 观察）；加入真实 PTY/ConPTY 输入后 Linux/Windows green。`windows-v4.json` 还保留 consumer 变严格后旧 producer 没有完整模式快照而拒绝的失败，最终 accepted producer 补齐实测快照与真正 close 的租约端点。
6. 首次 `make-check.json` 在 352 项中有 18 项失败，均由 Windows 创建的 linked worktree `.git` 内 D: 指针在 Ubuntu 中不可识别而发生，原始 stderr 保存。后续使用仅本 worktree 的只读 Git 查询映射；worktree 只允许 list，其他 fixture 的 -C / init / config 原样透传，不设置全局 GIT_DIR，不写共享 Git 配置。映射前后共享 `.git/config` SHA-256 均为 `18de2af639cb0d28809b71bfa845293181862452ac47bab3e6d7ea3296422b19`；`make-check-mapped.json` 的 352 项回归成功，其中 179 项执行通过、173 项既有外部输入条件 skipped，本票 7 项全部显式执行。真实系统 C bootstrap 与全部 CI 静态检查也通过。
7. 交付前增加模式字段缺失负向用例，实际发现空 before/restored 字典仍会被旧 consumer 接受，先得到真实 red；增加 Windows/POSIX 完整字段集、数值与 cc_hex 类型检查后同一真实 guest 变体 green。保留旧 accepted 与 final producer 原件；最后一轮 producer 使用已提交并按 `.gitattributes` 规范化 LF 的实现字节，避免把 CRLF 工作副本的配方身份当作 Git 源码身份。
8. 加强后的当前源码再次运行完整 `make check`，`accepted/make-check-strict.json` 的 352 项回归成功，其中 179 项执行通过、173 项既有条件 skipped；本票 7 项显式执行且无跳过，真实 bootstrap 通过。该次所选 accepted APE 与最终 verified APE 已证实同摘要；最终 verified 两平台各 7 项还分别独立重跑并保留原始日志。

完整 Cross/Full 尚未在本票关闭前完成，尤其 macOS 两架构及原生 Linux ARM64。它们的缺口不被本地模拟 guest、M0 旧成绩或本票的 59 项 OS 观察替代；集成 coordinator 负责当前提交的统一矩阵和票最终关闭。

## 196cd75 Full 失败后的独立修复

原 Full `41607608424` / `196cd75e1b8cf02bf5072bf3c2b26b5c44468c74` 的原 ZIP、raw 和源码/producer 核验保留在主工作区 `out/qaplet-m2/full-196cd75/`，未重写。fresh 修复树为 `.cache/worktrees/m2-05-fix`、分支 `codex/m2-05-cross-fix`，确认以该集成点为祖先；实现提交为 `fe37a634efb366f6be601319391a3683b0fc1d76`，随后合并集成文档点 `78cce5c`。本节全部新证据位于该修复树的 `out/m2-05-fix/`，不复用旧成功身份。

### 先复现、再区分原因

`diagnosis/raw-replay-before.json` 实际重算旧 raw：两台 Mac 各 19 项中有 10 个 OS fail，ARM 另因 raw `arm64` 与 consumer `aarch64` 拼写不同误拒绝 facts；Windows suite 仅 3 项、独立 ConPTY 15 项，缺少 `stdin.pipe/file/invalid`。最小首租约、后续 EBUSY、resize 和 stdin cleanup 事实保存于 `diagnosis/minimal-raw.json`。五个预先提出的可证伪假设分别针对：Darwin 新增 PENDIN、active 未释放的后续级联、resize 通知/终端存活期、Windows setup syscall 失败、架构别名；没有先把 `/tmp` 或 PENDIN 当成已实测根因。

Apple 的[termios 定义](https://github.com/apple-oss-distributions/xnu/blob/main/bsd/sys/termios.h)将 `0x20000000` 标识为 PENDIN；[TTY 实现](https://github.com/apple-oss-distributions/xnu/blob/main/bsd/kern/tty.c)在 raw→canonical 时设置它，FIONREAD 经 ttnread 完成队列重新处理而不消费输入。修复执行真实 tcsetattr/tcgetattr；仅在 Darwin 还原时出现原快照没有的新 PENDIN 才查询队列，随后再次 tcgetattr 并比较**全部** flags、控制字节和双向 speed。未过滤 bit、未修改 before/restored、未接受只有 restored flag 的成功。尚无新 Mac 执行，这仍是由原 raw 与一手机制支持、待 native Full 核验的修复。

旧 resize 的子进程返回最终尺寸，通知为 0，退出后父查询为 0。上游[公开 sigwait 实现](https://github.com/jart/cosmopolitan/blob/master/libc/calls/sigwait.c)说明 XNU 的限制，但固定 Nightly 的 libc 源码映射仍为 unproven，不能据此声称已经测得该供应版本的 ENOSYS。当前统一 POSIX 设施在私有子进程先阻塞 SIGWINCH，用 signal handler/self-pipe 实际接收通知；父查询完成后才 ack 子进程退出。resize 另用独立 PTY，避免 session 退出使后续 lease/text/borrowed 共用终端失效。Linux/64K 已实测新设施；Mac 的具体旧 syscall errno 和新设施仍待 native Full。

Windows 本机同摘要旧 APE 的独立 suite 实际 6/6 通过，`old-windows-suite.strace` 保留真实调用，不能由本机成功确认原 CI 的具体失败 syscall。修复由 host consumer 显式提供自有 evidence 目录给 stdin fixture，并分别记录 dup/pipe/file errno，逐个关闭部分成功资源。新 APE 用不存在的私有目录实际得到同样的 3 项/exit 256，`file_errno=2`，同时 `owned_routes_closed=true`、`original_fd0_restored=true`；原件为 `candidate/missing-root.*.log`。这是受控故障与清理验证，不是原 CI errno 的替代。

### 新配方本地闭环与反向合同

`candidate/build-info.json` 是 Ubuntu-only 固定输入构建，source commit 为上述 `fe37a63`。`candidate/source-verified.json` 逐项核对 11 个 source/recipe 文件的 producer 摘要、工作区原字节与精确提交 blob；聚合 source/build 摘要和供应 APE 长度/摘要均再次吻合。新 APE 为 **980691 bytes**，SHA-256 `0b98b5544c64df660dc3a06d8974b2650dd115e94bfb29edc2b243f5151aac22`。

| 新报告 | 实际身份 | 结果 |
| --- | --- | --- |
| `candidate/linux.json` | Linux x86_64 / 4096 / native / WSL kernel 6.6.87.2 | 19/19 pass |
| `candidate/windows.json` | Windows x86_64 / 4096 / native / kernel 10.0.22000 | 21/21 pass |
| `candidate/guest-64k.json` → `candidate/arm64-64k.json` | 完整系统 Linux aarch64 / 65536 / emulated-system / kernel 6.8.0-147-generic-64k | 19/19 pass；独立 guest consumer pass |
| `candidate/contracts-linux.json`、`candidate/contracts-windows.json` | 同一新 producer、真实新 guest、两宿主公开 CLI | 各 8/8 pass，0 skipped |

新 POSIX 取消场景用 mutex/condition 门控 waiter，先使借用输入与自有取消管道同时 ready 再释放；实际 raw `borrowed_ready_on_cancel=true`，控制就绪优先，未开始共享 read，join/owner/完整共享模式和 status flags 还原均通过。Windows 仍仅声明原有自有 event 取消范围，不把 POSIX 双就绪观测外推到 Console。

架构别名的真实 guest 复制变体在公开 import CLI 上先 red、再 green，仍保留 raw `arm64`；错误 `x86_64` 对 aarch64 仍 fail。新增退出后 resize 查询和缺少双就绪取消事实的伪成功变体都先实际 red，再加强 consumer 变 green。原有缺项、重复、假 restored flag、空模式字典、payload 变更负向全部保留，并增加 restored lflag 异于 before 但 status/flag 仍声称成功的负向；共八类 guest 反向变体。`diagnosis/*red*.log` 和 `*green*.log` 保留已执行的切片结果。

Windows 原始 libc fd0 在普通 suite 与 ConPTY 内仍为 `other`，显式 SetStdHandle 的 native Console route 另行成功并还原。BMP 分块输入事件仍为 `[65,20013,90]`，输出保留 `A中🙂Z` 的完整 UTF-8；非法输入与 emoji 输入实际均仅观测 `[81]`，继续为 UNSUPPORTED，未以 replacement 解码声称原始字节保真。

### 必需检查和当前缺口

Ubuntu `candidate/make-check.json` 已实际执行 362 项，180 项既有外部输入条件 skipped；本票 8 项均执行通过。全入口未通过：30 个 failure（含 subtest）和 1 个 error 都发生在 Nightly fixture 获取前的既有磁盘门槛；当时 D 盘可用 `4167704576` bytes，小于该入口默认 `4294967296` bytes。Linux ext4、C 盘并不缺同等空间，但源码盘 D 仍是必测输入。未降低阈值、修改 cache 布局、设置全局 GIT_DIR、修改共享 core.worktree 或删除原件。仅当前 worktree 的只读 Git 查询使用已记录适配器，其他 fixture 原样透传，worktree 仅允许 list；共享 `.git/config` 前后 SHA-256 均为 `18de2af639cb0d28809b71bfa845293181862452ac47bab3e6d7ea3296422b19`。空间门槛恢复后须以新报告路径重跑必需检查。

随后 coordinator 以标准 NTFS `/C` 压缩静态 SDK 编译缓存恢复空间，主工作区 `out/qaplet-m2/cache-compression-verified.json` 记录 4783 个文件内容摘要全部不变、无 reparse，D 盘可用恢复为 `4862255104` bytes。没有改变产品、配方、既有 4GiB 阈值或缓存物理目录。保留上述首轮失败，在同一新 producer/真实 64K guest 输入下补跑既有 `python3 scripts/dev.py check --output-dir out/m2-05-fix/check-after-space --report out/m2-05-fix/candidate/dev-check-entry.json`；`candidate/dev-check-after-space.json` 绑定环境收据与旧失败报告，`dev-check-entry.json` 记录实际 Ubuntu checkout、主工作区 `.cache`、预检和执行命令。补检 **pass**：362 项中 182 项执行通过、180 项既有外部条件 skipped，本票 8 项均执行；统一锁审计、CI 静态合同和真实系统 C bootstrap 全部 pass。原始 stdout/stderr 在 `check-after-space/command-0.*.log`，共享 Git config 前后摘要仍不变。完成后合并最新集成文档点 `1e1908f`，没有产品变化或额外制品重构。当前本地必需检查已通过；新 Mac native/当前集成 Full 仍保持下述缺口。

`diagnosis/replay_current.py` 再用修复树 consumer 读取冻结原件，`raw-replay-after.json` 仍将两 Mac 的 10 项真实失败和 Windows 的缺三项保留为 fail；不能把 consumer 别名修复算成新 OS 执行。新增 POSIX lifetime/取消合同也拒绝缺这些事实的旧 Linux raw，不以旧成功补齐新配方。

票保持 **claimed / local_verified_cross_pending**，`product_support_verified=false`。当前没有新 Mac x64/ARM64 原生执行，原 CI 的 Windows setup errno 也仍未知；新集成 Full 必须重新供应当前配方并验证五原生及实际 64K，不能据本地修复关闭本票。

### 当前配方 Cross 已完成

后续 Full `41616881692` / `0e4ba21` 整体成功。五原生 97 项及实际 64K 19 项共 116/116 由独立当前 consumer 重算通过，真实 Mac 完整模式恢复/resize、Windows ConPTY 原始 controller 动作、同 APE/loader、源码/配方/CI/宿主身份和 guest 清理均核验，本票已 resolved。严格摘要及原件选择见主 `out/qaplet-m2/full-0e4ba21/HANDOFF.md`、`verify/scope-summary.json`；重放不计新 native 执行。Windows 非法及补充平面输入仍为实测 UNSUPPORTED，不能由本票 OS 探针外推未来 stdio/TTY 产品支持。修复树已重新源对档核验后清理，冻结输出在主 `out/qaplet-m2/tickets/05-cross-fix/`，分支保留；旧失败及原 setup errno 未证实的边界不变。
