# BOOT-09 · 实际 Core evidence gate 验收

2026-11-01：`scripts/core_gate.py evaluate --input-manifest FILE --report FILE` 已消费完整真实证据并返回 Core pass / exit 0。它是只读 consumer，使用现有 public CLI / 实际 OS / 结构化 evidence seam；没有重跑 CI、重新构建产品、编译 Windows/macOS 或安装服务。

当前保留的完整输入与输出在主工作区 `out/m0-integration/core-gate/inputs.json`、`core-gate.json`。可在 Windows 使用 `python` 复跑：

```powershell
python scripts/core_gate.py evaluate --input-manifest out/m0-integration/core-gate/inputs.json --report out/m0-integration/core-gate/core-gate.json
```

manifest 包含显式原路径/当前根映射。`offline/` 保存原报告与所有嵌套 evidence，`offline-helpers/{first,second}/` 仅复制被比较的最终 helper 字节；主仓库物理 `.cache/` 继续保存可复用源码/工具链/Zig/guest。没有改写原 report 的 host、路径、时间或 digest；原始历史目录也保留。跨 OS 消费须将 manifest 的当前本地根改为对应挂载位置，原 source 前缀保持原值。

## 实际输入与覆盖

完整 M0 CI [41271366261](https://github.com/moorefu/qaplet/actions/runs/41271366261) 在 `1129cd82059c3a8ea62515196516342205c96b82` 已 completed/success。14 份官方 artifact ZIP 的 provider 身份、归档 SHA/长度及所有 CRC 先前已核验；gate 再比对归档与全部解包字节，不根据目录名或 matrix 的 pass 标签签发完成。

| 实际目标 | mode / 页大小 | startup | I/O |
| --- | --- | --- | --- |
| Linux x86_64 | native / 4096 | 7 | 20 |
| Windows x86_64 | native / 4096 | 7 | 21，含独立 ConPTY/CONIN$ |
| macOS x86_64 | native / 4096 | 7 | 20 |
| macOS aarch64 | native / 16384，translation 观测保留 | 7 | 20 |
| Linux aarch64 | native / 4096 | 7 | 20 |
| Linux aarch64 | emulated-system / 65536 | 7 | 20 |

另有 12 项独立 64K deep cases，共 175 startup/I/O/deep case。64K 为实际完整 QEMU 6.2.0 / TCG `virt-6.2,gic-version=3` / cortex-a72，kernel `6.8.0-147-generic-64k`，getconf alias/smaps 与 65536 一致。kernel/config/rootfs、QEMU executable、输入签名链、serial、原始 per-case 日志、payload 前后哈希与 cleanup 都保留并校验；不声明物理 ARM64 64K 或性能验证。

四种独立 Core 产品身份：

| artifact | 长度 | SHA-256 |
| --- | --- | --- |
| hello.com | 754446 | `467f8889db7a671cd238a7cc56df933951fea686c7c524480f4112870da3695d` |
| quickjs-eval.com | 2273408 | `426a7b05367fc6fd8d4831eebb2439782b3b66141af115094cc1efa766b05cd4` |
| native-io.com | 982754 | `462a846fc6b9e2aa36a3587a5fc221dd464388ee63e457068b3432344798ef3e` |
| pages64k.com | 832752 | `c80197784669f748cff263cf364d8ae49f4b6eae8f53c3df3f84389ae05cf16e` |

gate 对四个产品分别重哈希二进制、两个 CPU 的 ELF sidecar/map，并消费真实 nm / PE import 原始输出，重新执行 service-only symbol 拒绝合同。Common libc 进程/线程符号可存在。

## 断网与配方身份

真实 09 报告 SHA `1f2f64dde3cb7a77879f0e0c619edcc8cc6d3cf77aeb06a59dd598467a46694e`。两个独立 build 运行于实际新 `unshare --net` namespace，UID/GID 1000，仅 lo、无 IPv4 route，literal 外连 ENETUNREACH=101；各有 192 execve、0 Internet attempt。六个 Hello/eval 交付二进制和 loader/packer 在此 pair 内逐字节相同，14 个复制输入拒绝先于 supplier executable，21,180 个 cache files / 2,815,491,333 字节 before/after inventory 相同。

tested recipe inventory `dbf05fbe593bf0bfcf253b86f0bea7edf299cdf372349c6f868bbc92d6cb9a71` 与随后 current inventory `930102f78a6fed1da01000750a48fe56e1e5e3b1e5ac2756af6f756308551afb` 都保留。gate 重新核验 comparison receipt、原始 source diff 和 tested builder 快照；完整 AST 仅 `check_ci` 不同。当前 raw recipe fingerprint 没有被额外断网重构，不被改写成 tested 值。该实际 pair 证明所选 Hello/eval 与同一固定输入，不把不同上下文的 build ID 或 helper 摘要强制合一。

CI packer `187c2804…` / 2277726 字节与 offline pair `ae823b16…` / 2277662 字节有各自 provenance。额外受控 Ubuntu 诊断仅改变 SDK `-ffile-prefix-map`，重建出精确 CI packer SHA；九个 SHF_ALLOC sections 相同，debug/ZIP 位置数据及 ELF e_shoff 不同，不能声称全部 PT_LOAD 字节相同。两份 producer 实测 effective dependency entries 为 58。

旧 local I/O/deep 与新 CI `.com` 的差异恰为三处 ZIP central-directory UTF-8 flag 字节；两 CPU ELF sidecar 与实际 ZIP payload 相同。内存 append/rewrite oracle 生成精确 CI SHA。旧报告仍不替代新 CI artifact 的执行证明。详细诊断及六条真实 Ubuntu 命令见本轮外部 `probe-identity-notes.md` / `probe-identity-diagnostic-evidence/result.json`；原始 producer/断网 pair 未被修改。

## 回归与状态边界

TDD 先记录空输入、真实全量输入、SVC 隔离、依赖清单的失败，再实现通过；两个额外 red→green 缺口为 native/QEMU 身份矛盾、离线比较脱离实际 producer。真实 consumer tests 以 `QAPLET_GATE_MANIFEST` 显式选择完整证据。副本注入后重新打包并更新归档声明，确保语义检查独立于旧 ZIP 摘要拒绝；原始成功/失败日志始终保留。

覆盖缺 target/case、duplicate/conflict、假 actual、skip 隐藏失败、非零/timeout/deadline、旧/跨 case artifact、x64 伪装 ARM64、页漂移、translated/unknown host、manifest/local-fast 冒充、缺 raw 日志与同名误补、I/O/deep 假值、断网 build/refusal/comparison/helper/trace/inventory 缺项、root/网卡矛盾；更新所有声明 hash 后移除真实 serial DONE 仍失败；真实 map 中注入 CreateServiceW 仍失败。完全相同重复 report ID 也拒绝。

标准 Ubuntu `make check` 最终 135 tests / 0 failures / 43 explicit skips，静态 CI 和实际系统 C bootstrap pass。真实 full-evidence consumer suite 19/19 pass，Windows/Ubuntu CLI 均消费同一 retained dataset pass；没有真实数据时 integration cases 明确 skip。最终日志为 `out/implement-spec/10/out/core-tests-final-19.log`、`check-ready.{stdout,stderr}.log` 和 `core-gate-linux-ready.log`，报告数据不提交 Git。

SVC 的 SCM/Job 状态按独立 capability 和报告指针记录；缺失为 not_run，失败/blocked 不改变 Core verdict，且 artifact 不得复用 Core 身份。SCM 独立真实 CI [41271366311](https://github.com/moorefu/qaplet/actions/runs/41271366311) pass，artifact `f0c4c88e…` 与 Core 分离。Job 实际本地 corrected consumer 与独立 CI [41273469604](https://github.com/moorefu/qaplet/actions/runs/41273469604) 在 `e17981c` pass，artifact `0410864e…`，12 cases / 两次 supervisor / accounting active 0 / 184 raw paths 由父实施任务核验；首次 CreateProcess 路径 +2 handle 身份仍未归因，能力仅限已测 owned fixtures。三个独立原始报告指针均纳入 gate，服务 verifier 不在 Core 阻断集合。gate 不执行服务，不把独立状态指针当作新 SCM/Job 生命周期验证。

实际许可、组件角色和未知项见 [THIRD_PARTY_NOTICES](../../THIRD_PARTY_NOTICES.md)及报告 `core.dependencies`。完整 libc source mapping 仍 unproven，network TLS backend/version/CVE applicability 仍 unknown/unverified，M4 待独立审计；没有依据历史版本猜测当前后端。

## Fresh merge review · 4096 page-gap 修正

原19/19与135/43记录为修正前的实际结果，继续保留。Fresh merger 发现原 `guest()` 无条件要求所有 producer payload 和 pages64k facts/suite/shell-header，错误拒绝规范允许的4096补缺路径；原 canonical lock 的4096 profile只有签名 package IDs，没有 image/config衍生SHA，亦不能无条件按65536字段读取。

本轮没有构建新C。显式获取同一锁中4096 kernel/buildinfo：snapshot URL 实际返回HTTP504，原失败命令stdout/stderr仍在 `guest4096/evidence/8262d8d3-47f6-4b57-b0e3-c74932616768/`；随后显式从锁中原 official Ubuntu archive 取得精确相同 package SHA/长度，记录在 `guest4096/original-archive-acquisition.json`。普通Ubuntu用户使用原 supplied CI1129 Hello/eval、I/O与loader，实际准备/启动 full-system QEMU guest，`guest4096/guest.json` UUID `4836647a-ac6a-4bb2-b8d8-a1d36a6e2838`：Linux aarch64、kernel `6.8.0-147-generic`、4096、TCG，八条实际guest命令、七项startup与20项I/O均pass，payload前后不变。物理宿主仍记录真实WSL x86_64，CI为null。

独立 `dpkg-deb --fsys-tarfile` 流式读取SHA验证后的两个固定packages，再做gzip解压、Image magic/page flag和实际config断言，得到 Image SHA `ce78832564a2705c98d474fe487f11dbd6823eb0c5f5c162ebf1e760089b504f` / 59079048字节、config SHA `42a84d1758a4a33b07962624f41fcc41cc45a0ef8b6b0b1e0b2a1118a1ba5287` / 337427字节，与独立prepare的测量一致。`guests/arm64-derived.lock.json` 固定这些值、compressed kernel identity、base lock SHA及精确packageIDs。原 `guests/arm64.lock.json` 字节未变，旧65536/CI报告继续匹配；gate拒绝错误Image/config/compressed SHA及错误Image page flag，不接受随意填充的测量声明。原始派生报告SHA `ebe4996eabf626d4e8e157058f77db6c548e8fa6163aa1387af65395b17d608d`，位于 `guest4096/derived-identities.json`；两个真实tar-stream命令与stderr、重放脚本也保留在该目录。脚本从仓库根以 `PYTHONPATH="$PWD/scripts" python3 out/m0-integration/core-gate/guest4096/derive-identities.py` 可重放；它只重新测量固定package，不编译C。

公共 `validate-guest` 入口对这份真实4K记录先重现 `KeyError: image`，继而重现 `Guest lacks selected Core payload`，再修正后pass；另一个red/green验证4K独立检查不需要unused deep producer。它只签发guest scope，完整Core保持not_run。按页选择的catalog和纯调度模型覆盖 native64K+4Kguest、native16K+两guest，仍拒绝QEMU替换已覆盖native页；没有伪造native64K或native16K运行。完整Core仍消费原实际1129的native4K+65536guest矩阵。

新负向副本覆盖4K错误kernel/config/compressed摘要、4K/64K不符、Image page flag、emulated冒充native、qemu-user、缺命令、缺serial。最终public consumer 26/26、Ubuntu `make check` 142 tests / 0 failures / 46 explicit skips；实际4K scoped validator与现有完整门禁在Windows/Ubuntu均pass。新完整报告为 `core-gate-corrected.json` / `core-gate-linux-corrected.json`，175 probes与618 evidence items；原19/19、135/43与red记录不删除。4K原始guest报告SHA `1070a35bbe6e965a42f0d0f2613c8e95c8afb1dc5755669db1540d4fa947dad7`；scope结果在 `guest4096/validated-{windows,linux}.json`。最终日志 `out/implement-spec/10/out/core-tests-corrected-final.log`、`check-corrected-final.{stdout,stderr}.log`，副本亦归档在 `guest4096/validation/`。

## 全量双轴审查后的当前配方 proof

2026-11-01 的单一 fresh implementer 修复独立 producer notices/publication、真实 POSIX 降权 worker evidence 写权限、零配方差异 receipt 和直接 guest CLI cwd 路径。重新运行了当前固定配方的两个实际 disconnected builds，16/16 cases、6 binary / 2 helper comparisons、14 refusals、21,180-file cache before/after 全部 pass，所有 build/refusal strace 的 Internet attempts 为 0。新 recipe inventory `0c661f4bf650da1a0371ad3c06abcad916e018195e77d71a935b9009b005a19b`、真实 `changed_inputs: []`，不修改原历史 09/10 proof。

新 Windows / Ubuntu full Core 均 pass，175 cases / 617 evidence items；使用同一 canonical CI1129 实际矩阵与新 offline proof，独立 SVC 仍不阻断 Core。`out/m0-integration/core-gate/inputs{,-linux}.json` 已转向该证明，旧 manifests 逐字节保存在同目录 `retained-before-final-fixes/`，旧完整 reports 不覆盖。完整摘要、精确 hashes、red/green 与五 producer 实际输出见[最终审查修复记录](final-review-validation.md)。直接 guest CLI 相对路径的 actual 4K consumer 仍只签发 guest scope，不新增 guest 执行成绩。
