# BOOT-04A：fat APE 与原版 QuickJS 最小 eval

2026-11-01，在 `codex/m0-05-fat-ape` 实施。开工时 integration 与本分支均为 `cf0d125`，已核验 ancestry。主要公共 seam 为 `build_ape.py` CLI / 实际原生进程 / JSON evidence，沿用规格中用户确认的 TDD 边界。

本票生成独立 M0 探针，不形成正式 Runtime、SDK 或跨宿主支持声明。当前执行证据为 Ubuntu-22.04 / WSL2 Linux x86_64 / kernel `6.6.87.2-microsoft-standard-WSL2` / 实测 4096 字节页。CPU slice 存在只证明构建内容，ARM64 runtime 和其他宿主仍由 06/07/08 验收。

## 固定输入与实际配方

- 原版 Bellard QuickJS `e621135dc542e000e31038bedc52944aa0adaf1a`，已校验源码集合 SHA-256 `708c970fcd2d4f26f4e7c1a1f1de2464d16c4d8defb5bafd8c0bdbf21b953447`；有序补丁为空，清单摘要 `671e09884c6aecf658799b947d15de92871f7c44c78ac7f6ca42e7a4f6cbfaec`。
- 官方 Nightly GitHub run `41169443573` / artifact `13031425078` 原 ZIP：419652152 字节，SHA-256 `f2f984d3a9790674fc0da11c1c00a32d4fe3f8dafde99af74b0c6dc24a2a16cf`。编译器实际版本 GCC 14.1.0，原始工具和归档身份由输入锁及 before/after verifier 报告保存；libc 对应源码 commit 继续 `unproven`。
- 编译 `quickjs.c dtoa.c libregexp.c libunicode.c cutils.c` 与自有 `quickjs_eval.c`。`-Os -fwrapv -D_GNU_SOURCE -DCONFIG_VERSION="2026-06-04"`，不定义 `CONFIG_TEMPORAL_INTL`，无 LTO；固定路径映射和 `SOURCE_DATE_EPOCH=0` 另在 build-info 记录。
- 直接使用未改动官方 `bin/cosmocc`，每个对象同时生成 x86_64 与 AArch64 内容。链接 `-Os -Wl,-Map=%.map ... -lm -lpthread`；实际 BUILDLOG 保存底层双 CPU 编译/链接、TLS fixup、apelink、pecheck 命令。
- apelink 的实际资源次序是 x86_64 loader、AArch64 loader、`ape-m1.c`、x86_64 ELF、AArch64 ELF。运行显式采用锁定 `bin/ape-x86_64.elf`，不修改全局 binfmt / WSL 设置；当次 binfmt 清单保存到报告。

## 本地结果与原始证据

历史初次构建（用户要求统一仓库缓存之前）的报告保留在 `out/implement-spec/05/out/ticket05/build-info.json`，原始日志在其 `evidence/<report-id>/`，制品/双 CPU sidecar/map/BUILDLOG 在相邻 `artifacts/`。两次独立工作目录的构建得到相同字节：

| 制品 | 字节数 | SHA-256 |
| --- | ---: | --- |
| `hello.com` | 656122 | `5671ea00e3ba10ce544fce487eaf77cdca7f31a60d947b2930fe7982599d983b` |
| `quickjs-eval.com` | 2139396 | `61c52cd18bee95de3302405dde84c69f22dd5ff8debc3581d4ad83e01122bc8e` |

两个原制品均以 `MZqFpD='` 开头，分别解码两个 ELF64 EXEC header：machine 62（x86_64，phoff 2888 / 6 项）与 183（AArch64，phoff 3752 / 4 项）。检查实际 LOAD 片段边界和摘要、执行片段存在性、两个独立 sidecar 的 machine；没有用 `file` 的 DOS 识别代替 fat 检查。两个 CPU 的链接 map/global symbols 与最终 PE32+ imports 均实际取得，无 SCM / Job Object / 服务配置代码的符号或链接输入。

五项本地真实结果均通过：Hello facts、eval facts（均报告 x86_64 / 4096）、`6 * 7` 输出 `42\n` / exit 0、`throw new Error('m0-probe')` stderr 含 `Error: m0-probe` / exit 1、语法错误 `(` stderr 含 `SyntaxError:` / exit 1。每项继承同次输入/宿主/构建身份并保存对应 artifact SHA、完整 argv/cwd/时限/退出状态和 stdout/stderr 原始文件。完整 Core 状态保持 `not_run`。

四类实际负向执行保存在 `out/implement-spec/05/out/ticket05/negative-{compile,missing-slice,service-link,corrupt-resource}.json` 及其原始日志：

- 真实错误编译 flag：编译退出非零，报告 `fail`、非通过 case 和编译器原始诊断。
- 删除 fat 中的 ARM64 encoded header：拒绝缺失 slice。
- 向副本 ARM64 link map 注入 `CreateJobObjectW`：拒绝服务链接污染。
- 修改副本尾部资源字节：与预期 artifact SHA 不符，在执行任何工具之前拒绝。

合同测试另覆盖缺源码时不获取/不创建缓存、缺 sidecar 拒绝、独立完整 inspect、CI 测试 job 不得重新编译，以及无 GH_TOKEN 时显式 archive bootstrap 为 blocked/不改锁/不创建 ZIP。最小 TDD 红绿记录：缺源码CLI从缺程序报告失败到通过；实际双fat构建从未实现 blocked到native通过；摘要拒绝从未识别参数到fail；真实编译失败从空cases到非通过case；新CI合同从未实现命令到通过。

## Ubuntu / 仓库缓存要求

用户后续明确所有产品构建统一在 Ubuntu，其他平台只做制品与 SDK 验证；缓存实际数据统一仓库 `.cache/`，不得将 `.cache` 链接到仓库外。实现默认值因此为 `.cache/quickjs`、`.cache/toolchains/cosmocc`、`.cache/build/<report-id>`；worktree 显式指向主仓库同名目录，报告/分发制品仍在 `out/`。迁移后的最终复核结果将在下节记录，历史 home/ext4 结果不作为新缓存能力的证明。

## CI 与后续上下文

新增 JSON/YAML `.github/workflows/m0.yml`：Ubuntu build job 先运行本地 `make check`，显式从锁定官方 URL 获取原 ZIP，逐 SHA/长度校验，再用共享 input fetch 与 `make build-ape`，失败也上传 `out/m0/` 中原始制品及证据。token 仅从 GH_TOKEN 环境经 curl stdin 传入，不在 argv/报告中；下载失败/归档过期/鉴权失败不回退滚动通道，不改锁。新的 `build_ape.py check-ci` 同步进入 `make check-ci`，静态拒绝其他 job 再编译。此票未声称远程 workflow 已运行；06 接入同摘要跨宿主消费矩阵，SDK 验证只在实际实现对应 SDK 后声明。

07/08 的最小复用指针：`scripts/build_ape.py` 的 `verify_inputs(..., source=False)`、`run`、`identity`、`headers`、`audit`，以及 `build` 的每对象编译/双CPU map收集循环。可由独立探针脚本导入并生成独立 artifact 身份，不需要同时改动本票构建文件。它们是构建证据工具，不构成稳定产品SDK。
