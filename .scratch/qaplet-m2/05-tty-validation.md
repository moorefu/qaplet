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
