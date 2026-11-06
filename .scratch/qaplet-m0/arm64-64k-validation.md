# BOOT-06 · 实际 ARM64 64K 探针与三个修正

2026-11-01，本地完整系统 QEMU 已完成 Linux/AArch64/65536 的实际验收。宿主是
Ubuntu-22.04 / WSL2 / x86_64 / 4096；guest 明确记录为 `emulated-system`。
本记录不替代 06 的六类宿主矩阵，也不作物理 ARM64 或性能声明。

## 固定 guest 与共享入口

[`guests/arm64.lock.json`](../../guests/arm64.lock.json) 锁定 Ubuntu 的五个实际
ARM64 Debian 包、signed InRelease、Packages.xz、selected userland 文件和
QEMU 6.2.0 / `virt-6.2,gic-version=3` / Cortex-A72 / single-thread TCG。
每次离线校验签名与摘要链，然后解包已验证输入；普通 `run` 不联网。
获取入口已改为[官方 immutable snapshot](https://snapshot.ubuntu.com/)
`20261031T234300Z`，metadata 和全部 package URL 使用同一固定时间。实际探测发现
moving ports InRelease 已漂移为 `af271fdc...`，没有放宽原 SHA 或增加 fallback。
snapshot 返回的 InRelease、Packages.xz 和小型 ARM64 busybox 包与原锁逐字节相同；
五个 package URL 的 HEAD 均成功。原五个 Debian archive 被复用，fresh metadata
`fetch` 与全新解包 `prepare` 均通过，见 `out/snapshot-{fetch,prepare}.json` 和
`out/url-probe/62d9cf11-5a13-4daa-b3f4-d172d25eb502/report.json`。

实际内核 `6.8.0-147-generic-64k` 的 Image SHA 为
`39e298eabca51c5589424b1d78408233aeae04cc73be9a7b4c28f32661b2e9a8`，
config SHA 为 `17325c06fb19d6a4f7f18703c2b1e2bd6676a44b3d1d42c1f78bb05afa2c281e`。
内核 Image page flag、config `CONFIG_ARM64_64K_PAGES=y`、guest `getconf`
两个名称、`smaps` 64 KiB 和独立 C `sysconf` 相互核对。

`scripts/arm64_guest.py` 提供显式 `fetch`、离线 `verify` / `prepare`、
`merge-manifests` 和有时限的 `run`。06/08 复用同一个 argv/file/serial seam。
合并 manifest 对同路径同摘要去重，拒绝冲突和重复 case；Unicode/空格路径保持
原字节。每条命令有独立 TMPDIR，防止前一个 APE 的 loader cache 掩盖后一个的
embedded loader。guest 内前后重新散列所有 payload，原 fat APE 从不被修改。
RAM-only guest 没有磁盘或外部网络；仅其内部 loopback 和 devpts 用于 I/O。
UTC 由宿主在启动前设置到隔离 guest，外部进程时间仍保留在实际 QEMU 命令记录。
QEMU 超时会杀掉进程组，并保留串口、stderr、退出与清理记录。

## 实际失败与最小修正

1. 原锁 loader 的 RX/RW LOAD 只满足 16K congruence；实际 64K guest 中连无参数
   启动也退出 139。单独标为诊断的布局副本证明 loader 本体布局是第一处问题；
   此 RWX 副本从未用于正式制品。随后按[精确官方源码研究](arm64-loader-source-research.md)
   构建独立 Linux OSABI0 loader，两项 link page 参数为 65536，代码不改，未套用
   上游面向 FreeBSD 的 fixup。新 loader 在同一个 guest 正常显示 usage / exit 1，
   并对旧 fat 精确拒绝 `p_vaddr incongruent w/ p_offset modulo AT_PAGESZ` / exit 127。
2. `cosmocc` 的 MAX64K-only CPU link 成功，但原 `apelink` 又按硬编码 16K 移动
   片段，破坏了更强的输入 congruence。独立源码 packer 的有序小补丁保留合并时的
   最大 `p_align`，并使重定位 delta 为所有 LOAD alignment 的公倍数。官方 driver
   保持原字节，保留各 CPU 的 COMMONPAGESIZE，只显式提高 MAXPAGESIZE，再从两份
   实际 sidecar 重新打包。新 ARM loader 同时作为 embedded `-l` 资源写入。
   每个 final fat 与 sidecar 都重新核对 64K LOAD congruence、边界和保护页无重叠。
3. 启动修正后 Hello 和 eval facts 通过，但引擎上下文创建崩溃。真实指针诊断记录
   `malloc=0xe28ed63779c0`，QuickJS 的 47-bit NaN boxing 将其截为
   `0x628ed63779c0`，随后在该截断地址加 16 处 fault。`JS_NewRuntime` 成功，
   `JS_NewContextRaw` 失败；独立分配、pthread/TLS 与 mmap 探针均通过。
   ARM64 原生 4K CI 的旧制品也有相同引擎失败，故这不是 64K mmap 推测。
   项目 overlay 只从 47-bit 选择条件移除 ARM64，使用原版已有的完整指针 JSValue
   表示。Bellard pin、原始 LICENSE 与 pristine upstream inventory 不变，原 cache
   不改；新 derived cache key 为 `e621135...-dc12be2f49d37dd2`，源码集合 SHA 为
   `1da9f9ae2f2557143a9f44471fdb73d646bdfa87e11ccfec8dca8f757952b616`。
   patch SHA `6197ebf127147d917470759f603ff614f6eff68fe581a43f2a5dd37a2be26eff`。

新的 source helper 使用独立 Cosmopolitan pin `aefad27c09ffda64c6ac64bd69f758e9f9c7b91d`，
26 文件均核对 SHA-256 / Git blob；ISC 与 HACL MIT 授权及 notice 保留。
原 Nightly ZIP、原 tree、原 helper、原锁和 libc `unproven` 声明保持原事实。
Linux native tool view 仍通过 05 的 before/after 原输入验证及独立 recheck。

## 独立 probe 与当前实际结果

`tests/native/pages64k.c` 与 `scripts/pages64k.py` 生成独立 `pages64k.com`：
832752 字节，SHA `d15a43d39c044c23077338375067bfdd4cc2927ec856b374e64184dd97ab10ec`。
producer report ID `e9f3f6e1-a07a-4dee-b42d-876b04954468`，完整 build/source/helper/
toolchain/双 CPU map 与禁用服务导入审计在
[`pages-acceptance/build-info.json`](../../out/implement-spec/07/out/pages-acceptance/build-info.json)。
loader SHA `255583cb792109859388afc600366c8e048a07c68cb81a23e97846cd0837a74d`。

实际 12 项 C 合同全部通过：真实环境；4 线程 × 128 轮 barrier 下 TLS/key/地址隔离；
1、65535、65536、65537、1048577 字节分配；跨 3 页读写与 mprotect/munmap；
非对齐 munmap、零长 mmap、非对齐 MAP_FIXED 和 file offset 拒绝；文件创建/往返/
删除后 ENOENT；131089 字节 deflated ZIP 在页边界和尾部读取；EOF/越界/负 seek；
缺失资源拒绝。MAP_FIXED 的实际 portable-libc errno 是 EPERM，原测试先假定
EINVAL；高于低地址保护范围仍复现 EPERM，因此最终合同要求真正拒绝并接受
EINVAL/EPERM，保留 `observed_errno`。没有弱化正向映射或 65536 前置。

最新单独验证为
[`guest-final.json`](../../out/implement-spec/07/out/pages-acceptance/guest-final.json) 与
[`validated-final.json`](../../out/implement-spec/07/out/pages-acceptance/validated-final.json)。
它也执行独立 facts 和 fresh-cache shell/header 启动。每个必需 C case 继承真实
suite 时间区间、argv、时限、退出、raw logs、独立 artifact 与根级 build/toolchain。
`run-native` 要求真实 native Linux/AArch64/65536；本机 WSL x86_64 被正确 blocked。

最终 Hello SHA `467f8889db7a671cd238a7cc56df933951fea686c7c524480f4112870da3695d`；
eval SHA `426a7b05367fc6fd8d4831eebb2439782b3b66141af115094cc1efa766b05cd4`。
[`final-producer/build-info.json`](../../out/implement-spec/07/out/final-producer/build-info.json)
producer ID `c4255efb-bbe0-4bb7-b8ea-635bfe2135d5`：本地 x86_64 七项实际结果、
before/after suppliers、派生工具视图、两种 CPU/header/map/import 检查均通过。
[`guest-seven.json`](../../out/implement-spec/07/out/final-producer/guest-seven.json)
在真实 64K 下通过 Hello/eval facts、42、异常、语法错误、public JSValue pointer
roundtrip 和 20 × 1000 object/Map 分配及上下文销毁（9990000）。
[`guest-shell.json`](../../out/implement-spec/07/out/final-producer/guest-shell.json)
额外证明两个新 fat 各自从空 TMPDIR 提取自己的 embedded ARM loader 后启动成功。

新 I/O artifact 保持独立身份 `b5576c3ae0e9391266f69dfd523039a3e5de6dc2664b9f03d3c3fef8b215ecb9`。
Linux native 20 项、普通用户 Windows 21 项和真实 Unicode/空格 executable guest
路径 `/payload/进程 空格/native io.com` 的 20 项均通过；语义 gate 由 08 维护。
受限 Windows context 的实际超时原样保留，普通用户结果不覆盖它。

## 红绿与拒绝证据

- 缺 guest 输入的 CLI 从未实现红到准确 blocked，未创建 cache、联网或启动 QEMU。
- 实际原 loader/sidecar 139、旧 fat congruence 127、实际 QuickJS pointer fault
  都保留为独立阶段报告，见 `out/diagnostic-*`、`out/source-loader-guest.json`、
  `out/js-debug/{stage,intrinsic,pointer}-guest.json`。
- 永久 `--pointer-check` 只经过公开 `JS_MKPTR` / `JS_VALUE_GET_PTR` / tag API，
  ARM64 使用 bit47 置位的合成指针但从不解引用。对原始 header 的真实 guest
  regression 必然 exit 3，见 [`abi-red/guest.json`](../../out/implement-spec/07/out/abi-red/guest.json)；
  修正后 final artifact exit 0。这不依赖 ASLR 碰巧分配高地址。
- 修改 payload SHA、错误 4096 报告、旧 16K loader layout、实际 x86/4096 native
  host、manifest 字节冲突/重复 ID 都拒绝。Unicode hash protocol 的真实首个失败
  也保留；改为从最右侧读取 SHA 后，同一原字节 fixture 通过。
- [`negative-guest-timeout-final.json`](../../out/implement-spec/07/out/negative-guest-timeout-final.json)
  是实际 1 秒 kernel deadline，QEMU exit -9、process group 已清理、串口保留；
  status=fail，没有将未完成 boot 或没有用例变成 pass。
- 五项 focused generic CLI contracts 通过；完整 `make check` 的命令/原始结果另存
  `out/final-check/`。合并 integration `611bc06` 后，Ubuntu `make check` 已通过：
  88 个合同、14 个明确平台/重型 fixture opt-in skip、真实系统 C bootstrap 和
  两份 CI 静态合同；本票的重型实际构建/64K 运行另由上述 focused CLI 完成。
  首次 80-contract 回归的既有 DrvFS candidate rename EACCES 与另一次实际
  QuickJS fresh-cache rename EACCES 均保留，root 独立诊断；后来的检查成功不删除
  或覆盖这些失败，也不替代完整 M0 门禁。

所有构建在 Ubuntu，物理缓存都位于主工作区 `.cache/`，报告/原始日志/最终制品
位于 ignored `out/`。新 APE SHA 必须重新跑 06 的各 native 宿主；旧 16K/旧
QuickJS SHA 的成功或失败均只属于历史证据，不能移植为新字节成绩。
