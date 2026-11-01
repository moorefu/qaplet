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

用户后续明确所有产品构建统一在 Ubuntu，其他平台只做制品与 SDK 验证；缓存实际数据统一仓库 `.cache/`，不得将 `.cache` 链接到仓库外。实现默认值因此为 `.cache/quickjs`、`.cache/toolchains/cosmocc`、`.cache/build/<report-id>`；worktree 显式指向主仓库同名目录，报告/分发制品仍在 `out/`。

迁移后在 Ubuntu 实际执行：

```sh
make build-ape \
  QUICKJS_CACHE=/mnt/d/Workspace/dairn/qaplet/.cache/quickjs \
  TOOLCHAIN_CACHE=/mnt/d/Workspace/dairn/qaplet/.cache/toolchains/cosmocc \
  APE_BUILD_DIR=/mnt/d/Workspace/dairn/qaplet/.cache/build \
  OUT=out/ticket05/canonical
```

结果 `pass`，报告为 `out/implement-spec/05/out/ticket05/canonical/build-info.json`，report ID `80c4e13b-17e4-4f39-9cee-13f1415358c0`；真实中间文件全部在主仓库 `.cache/build/<id>/`。源码、工具链前后四次离线验证全部通过，五个原生 case 和双 CPU headers/maps/imports 全部通过；两个最终 APE 的大小和 SHA 与上表完全相同。构建配方及实际输入身份集合摘要为 `147fcf3ac9ef91a15517f5f2b5302af3205afdd0866a36d076c843bc6d4c5e9e`。自有探针文件摘要与实际 argv 在报告内。

Nightly 输入报告记录实际 `9p` / `/mnt/d` / `drvfs-no-metadata-v1`：文件可无写权限，目录无写位和非执行位不可按 POSIX 表示；原 ZIP、所有内容/类型/link、可写文件拒绝仍启用，并保留完整 chmod 探针记录。构建前完整 Nightly 校验约 74 秒，构建后约 55 秒，均在 300 秒边界内。源锁、工具链锁和原 ZIP 身份未改变。嵌套 GNU 工具的自解包 loader 缓存在同一 `.cache/build/<id>/tmp/.ape-1.10`，实测 SHA `bf65f331e87544761b8ed3a2c03a61c1b61072c8776b689a67b06f50eefd546f` 与锁中 ELF loader 相同；不依赖旧 home 缓存中的实际数据。

迁移后的原 ZIP 另经 `fetch_locked_toolchain.py` 的已有归档路径实际验证通过，报告 `out/implement-spec/05/out/ticket05/canonical-archive.json`。它证明锁中原字节仍然一致，尚不能代替真实 CI 中跨仓库鉴权和下载的验收。

## WSLInterop 修正与最终本地验收

后续完整合同回归暴露真实启动路由错误：部分 WSL invocation 有启用的 `WSLInterop` / `MZ` / `/init` 注册。原版工具链 helper 被启动为 Windows 进程，无法使用 Linux `/mnt/d/...` 路径；原生 Windows 的错误退出实测 256，bridge 观测 0，与 8 位截断一致。`mktemper` 没有输出但 driver 继续，产生空重定向及找不到对象文件的连锁错误。原始失败保存在 `out/implement-spec/05/out/native-contracts/fat-contract-10vbhf9h/`；root 独立 `/proc` 与 Windows 进程证明见 `out/m0-integration/builder-diagnosis/same-invocation-red-2cdfd44f-3637-4bf8-9efc-5ba90a59ac62/` 及 `direct-watch-2144f85a-a79c-440d-b839-6e311a0a83ae/`。此前没有 WSLInterop 的成功上下文不适用于这一启动路由。

修正只在构建消费层：`native_toolchain.py` 用锁中原生 ELF loader 启动锁中官方 `assimilate -e -x -o`，从原工具副本得到独立 Linux ELF 视图。62 个可执行 MZ helper（原总长 610039160 字节）全部覆盖，含 GCC `cc1` 等内部程序；shell driver 按原字节复制，目录相对查找与安全 archive links 保留。原 ZIP、原 tree、权威锁及 libc `unproven` 声明均不变。派生物单列为 `derived-linux-elf-x86_64`，不冒充官方原始归档字节。

每个转换结果都必须匹配独立预期 SHA：从已验证原文件解码 x86_64 ELF64 header、按官方 Linux 转换规则规范 OSABI，保留并散列完整原 body，同时重新核对原文件 SHA。复用也重新计算预期值、核对全输出/readonly/layout/link；可写 receipt 只是来源说明，不是完整性权威。构建后再次检查派生视图。缓存全在主仓库 `.cache/toolchains/cosmocc/official-assimilate-linux-x86_64-v1/<archive-sha>/tree`；不 seed loader、不重试失败编译、不改全局 binfmt/WSL/mount。

同一启用 WSLInterop 的 invocation 完成原始/派生对照：原 driver 3/3 复现错误（0.063–0.068 秒），派生 driver 3/3 编译成功（0.232–0.239 秒）；派生进程 `/proc` 采样没有 `/init`，同时 Windows 观察器记录原始 helper 2 次、派生 helper 0 次。证据位于 `out/implement-spec/05/out/ticket05/route-proof/` 的 `summary.json`、`windows-helper-events.json`、`original-16227d1c-878b-4590-9ccb-fac55c1ddaf8/` 与 `derived-4d5287f2-600d-46ed-ae9b-433c6a5773de/`。

最终产品报告为 `out/implement-spec/05/out/ticket05/native-build/build-info.json`，report ID `f8b29a87-fb56-4187-abb2-c10a79e17c73`，UTC 01:47:06–01:50:20。当次 build-info 同样记录启用的 WSLInterop，原输入和派生输入前后检查通过、五项真实运行和双 CPU/map/import 审计通过。两个最终产品仍是上表完全相同字节的 fat APE。构建身份摘要 `3cd1593095bf417e107166ff2d4b2e3a6f05763c964495ad61c693ff044b0b15`，派生 helper 集合摘要 `356c58a98fd84d8cabd55006bbb6027cf617121bfe49c7abef44d82e467f209b`。

最终 `native-build/invalid-flag.json` 证实真正 GCC 拒绝 `-qaplet-invalid-flag`，退出 1、无制品、非通过 case 和原始诊断齐备。测试现明确断言该 flag 与 `unrecognized command-line option`，启动失败不能冒充预期编译错误。公共构建合同检查派生格式/内部 helper/原派生身份及实际 compiler argv 路径。native fixture 的产物/日志保留在 ignored `out/native-contracts/`，失败不会丢失诊断。共享 `run` 委托已测试的 `foundation.command` 保留时限、进程组清理与原字节日志。

合并 integration `664bec0` 后，Ubuntu 普通 `make check OUT=out/ticket05/native-build/check` 通过：65 个合同、0 失败、5 个明确 skip（2 个重型实际构建由上述 focused 入口完成，3 个平台专属合同），CI 静态检查和实际系统 C bootstrap 均通过。四个独立 inspect 用例此前已由独立诊断完整验证，5/5 native public methods 通过；产品/inspect 行为和最终字节未改变，没有重复四次昂贵输入扫描。新的 CI 合同还以红绿回归拒绝非 Ubuntu build runner。报告、制品、maps、原始日志和生成的诊断脚本全部保持 ignored `out/` / `.cache/`；`toolchains/evidence` tracked 文件为 0。

## CI 与后续上下文

新增 JSON/YAML `.github/workflows/m0.yml`：Ubuntu build job 先运行本地 `make check`，显式从锁定官方 URL 获取原 ZIP，逐 SHA/长度校验，再用共享 input fetch 与 `make build-ape`，失败也上传 `out/m0/` 中原始制品及证据。token 仅从 GH_TOKEN 环境经 curl stdin 传入，不在 argv/报告中；下载失败/归档过期/鉴权失败不回退滚动通道，不改锁。新的 `build_ape.py check-ci` 同步进入 `make check-ci`，静态拒绝其他 job 再编译。此票未声称远程 workflow 已运行；06 接入同摘要跨宿主消费矩阵，SDK 验证只在实际实现对应 SDK 后声明。

07/08 的最小复用指针：先调用 `verify_inputs(..., source=False)` 得到原输入，再用 `native_toolchain(args, report, directory, original)` 得到派生编译目录；不要直接执行原缓存 MZ helper。也可通过公共 `prepare-toolchain` CLI 的 `native_toolchain.tree` 获取目录。`run`、`identity`、`headers`、`audit` 和 `build` 的每对象编译/双 CPU map 收集循环可供独立探针脚本复用并生成自己的 artifact 身份，不需同时改本票构建文件。构建后需复核原输入及 `native_toolchain.recheck`。这些是构建证据工具，不构成稳定产品 SDK。

本地代码可合入，ticket 05 仍保持 `claimed`，等待真实 Ubuntu CI 验证 GH_TOKEN 跨仓库固定归档获取、共享配方构建及原字节上传；不把本地 helper 检查代替远程获取验收。完整 M0 / 跨宿主 / SDK 支持声明仍由后续任务负责。
