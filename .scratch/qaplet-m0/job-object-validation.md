# BOOT-05B · 应用私有 Job Object 实测

2026-11-01。依据用户“直接使用本机”和 Ubuntu-only build 的执行更新，在 `codex/m0-12-job`、`out/implement-spec/12` 实施；开始于 integration `8fb7c39`，最终已并入 integration `2e8ef9c`（包括 09/11），merge commit `ba92665`。本地 Windows 是开发宿主，不是隔离 VM。SCM 和 Core 均独立于此探针。

## 结果及身份

在所测 Windows x86_64、kernel `10.0.22000`、4096-byte 页普通用户上下文，所锁 Nightly 的私有 MS-ABI bridge 可完成本报告列出的 owned fixture membership、终止、nested kill-on-close、错误路径及清理。**verified 只覆盖这些 fixture 和初始化后的稳定 handle 基线；首用 CreateProcessW 的 +2 handle 增长未归因，不声明所有进程 handle 完全无泄漏。** 任意进程树和正式产品 API 未验证。

Ubuntu-22.04 / WSL2 Linux x86_64、kernel `6.6.87.2-microsoft-standard-WSL2`、4096-byte 页负责全部编译。实际输入、派生 helper 和中间文件都在主仓库 `.cache/`；报告/最终制品在该 worktree 的 `out/`。DrvFS 权限限制沿用[开发说明](../../docs/development.md)，没有更改 mount/全局设置或原工具链。

| 身份 | 值 |
| --- | --- |
| Nightly 原 archive SHA-256 | `f2f984d3a9790674fc0da11c1c00a32d4fe3f8dafde99af74b0c6dc24a2a16cf` |
| 最终 fat `svc-job.com` SHA-256 / 长度 | `0410864e8232379a7559410978f804da534b1550107f9a39ccdc050ef1bfaffb` / 755199 bytes |
| 最终 C source SHA-256 | `e83777204611f313dca1ea862d3017344e2a387441c288e9deb8e6418898e377` |
| producer report SHA-256 | `3484b1b1249eb9ab4aa486ec1dd8348885881410fb2d4cee4a66f87c90aba1d1` |
| producer 所测 Git revision | `1129cd82059c3a8ea62515196516342205c96b82`，附当前输入逐文件 SHA；不宣称 working tree clean |
| libc source mapping | `unproven`，未改变锁中的声明 |

应用自己的两 CPU map、ELF sidecar、64K LOAD 检查、PE imports 和 MS-ABI 调用反汇编均保留。144-byte extended limits、64-byte basic limits、48-byte accounting 和关键字段 offset 有 Ubuntu 编译断言。20 个 raw Win32 export 实际解析/调用；故意不存在的 export 得到 127。Windows 私有代码不进入 ARM slice，private Job API 不成为静态 PE import。普通 Linux 入口通过，私有 Job 入口在任何 DLL 调用前明确拒绝；ARM Windows 运行没有验收。

## 实测场景

| 场景 | 实际结果 |
| --- | --- |
| runner / suspended artifact 的既有归属 | 均已在 Job；runner immediate flags 为 10240 (`0x2800`)。只读取这些事实，不获取/终止既有 Job |
| 独立 launcher fence | 新 unnamed、non-inheritable Job，先 KILL_ON_JOB_CLOSE，再 suspended create→assign→membership verify→resume；制品 immediate flags 实测 8192 |
| query-only Job handle 分配 | Assign 返回 0 / ERROR_ACCESS_DENIED 5；目标 private membership 前后均 0，fixture 不 resume，直接终止/wait/close |
| owned parent + 一个 inherited child | 在私有 Job list 实测恰好 2 PID，各自 membership 为真；显式 terminate 后分别 wait，两者实际 exit 71，accounting total 2 / active 0 |
| 两个新 nested Job + 一个 leaf | 同时属于内外 Job；close 内层最后 handle 后 leaf 已 wait，外层 active 0；立即重复 close 返回 0 / error 6 |
| 提前退出 | membership 在 resume 前核验；raw ExitProcess(37)，wait 返回 0、exit code 37 |
| leaf deadline | 150ms wait 返回 WAIT_TIMEOUT 258；关闭 Job 后 wait 成功，owned handles 全关闭 |
| 全套外层清理 | 3 次完整 cycle 均 total 10 / active 0；probe 已 wait，process/thread/Job handle 均成功关闭 |
| launcher 真超时 | `--probe-mode hang --timeout 1` 实际 timed_out；报告 fail / capability failed，外层 total 1 / active 0、probe 已 wait，3 handle 均关闭 |

父子场景只证明这两个受控 PID 和实际查询的 Job accounting，不由单 PID 退出外推任意树安全；nested 场景只验证一个 leaf 的两层新 Job。没有请求 breakaway，没有 assign/terminate runner 或无关 Job。没有需要 UAC 的操作；当前权限足以执行这些 fixture，不证明其他 token/session/策略具有同样能力。

## 首用 handle 增长：保留失败与限定结论

第一版制品 `1c55531e31681e8c59faeb87ce51b89385e610425827ea47c8e4f9b6e1415ea6` 的严格初始/最终计数检查真实失败：119→121，owned 18/18 已关闭。重复运行同一旧字节仍失败：120→122，owned 18/18 已关闭。两个失败及各自 raw logs 均保留，没有覆盖成 pass。

采用 diagnosing-bugs 的 CLI seam，并在后续 staged probe 前给出三条排序假设及预测：

1. 首次 CreateProcessW 路径保留额外状态：第一次创建后，超出它返回的两个 owned handle 的增长已经出现，随后重复稳定。
2. 首次 terminate/wait 路径保留状态：创建时只增加两个 owned handle，在终止/wait 边界才额外增长。
3. 持续 fixture 泄漏：每次关闭后的数量继续随 cycle 增长，或完整场景使稳定基线再次上升。

最终制品的三个独立完整运行得到相同 staged 结果：initial 118；第一次 suspended CreateProcessW 后 122，terminate/wait 后仍 122，关闭两个 owned handle 后 120；第二/第三次均 created 122、stopped 122、closed 120；完整场景前后均 120。所有 owned 24 handles 实际 CloseHandle 成功。这三个初始化 fixture 不 resume，且在 C private Job 创建或新的 stdio 输出前完成采样，因此支持“+2 出现在首次 CreateProcessW 路径”的边界判断，未见本场景的线性增长；**没有识别那两个 handle 的类型/所有者，不把它们归因为已证明的良性缓存，也没有供应层修复。** 新报告明确保存 initial、created/stopped/closed 各阶段数组和 capability_scope/limitations。进一步要求初始到最终完全无增长或长期进程无泄漏时，仍需定位这两个 handle。

## 验证与复现

共享入口及完整条件见[探针说明](../../docs/svc-job-probe.md)。并入最新 integration 后，最终 `make check` 在 Ubuntu 通过：115 tests，25 explicit skips，CI static + bootstrap pass。Windows 使用最终 producer 的 8 个公开 CLI/真实 OS 合同全部通过，包括故意超时；Ubuntu 使用同 producer/loader 的 8 tests、1 Windows-only skip 通过。TDD 的 missing/changed/malformed producer 与 CI Windows 编译拒绝均先观察 red，再实现 green。

以下位置相对于 `out/implement-spec/12/`；原始命令、API 值及 stdout/stderr 位于对应 report 的 `commands` / `evidence`，报告本身按每次 UUID 保留：

- `out/svc-job/measured/build-info.json`：最终 Ubuntu producer、source/toolchain/build/artifact、完整输入前后校验及编译日志。
- `out/svc-job/measured/windows/cycle-{1,2,3}.json`：三个实际完整 Windows 运行及独立清理。cycle 1 UUID `8b632c05-85d4-4518-854a-605f041139a3`，`2026-11-01T04:07:52.380807+00:00` 至 `04:07:52.653707+00:00`。
- `out/svc-job/measured/windows/timeout.json`：实际 launcher deadline 的 fail 和完整零残留证据。
- `out/svc-job/first/windows/{job,repeated}.json`：两个原版 handle-count fail。
- `out/svc-job/contracts/`：公开 CLI 合同结果与 raw logs；ordinary-user Python 创建的安全临时目录须在相应普通用户上下文读取。
- `out/check-ticket12-integrated.log` 与 `out/check-ticket12-integrated/`：并入最新 integration 后的最终全套 check 输出及各共享检查报告；此前 `out/check-ticket12-final.log` 的 114 tests 通过记录亦保留。
- `out/svc-job/core-inspect-{hello,eval}.json`：对 07 当前 Core Hello `467f8889...` 和 eval `426a7b05...` 的独立同摘要 import/map/headers audit 均 pass。Core 配方未修改，也未加入本应用代码。

`.github/workflows/svc-job.yml` 是独立 Ubuntu producer / Windows consumer，含实际 fence deadline 合同、失败证据上传；后续实际远程结果见末节。所有 M0-SVC 报告的 `m0_core_status` 保持 not_run，不关闭完整 Core 门禁。

## Consumer merge review 修复

初次提交 `c1ae465` 的 fresh merger 发现：missing-export launch 的原始事实虽干净，但它的 facts/error、exit 和 cleanup 会被 suite 变量覆盖，存在未来清理失败仍被判 verified 的路径。因此 integration 未合入该提交；修复只修改 consumer 和公开合同，没有重编 C、修改原 producer 或覆盖原始失败。

现在两次 launch 各自完整保存在 `supervisors`，通过同一真实 launch gate 检查设置/归属/非继承、exit 0、timeout/error、terminate、accounting active 0、reap 和 process/thread/Job handle close。missing-export fence 任一项失败立即停止 suite。其 native 负向 API 断言是 null/error 127，但断言成立后进程应返回 0，不能忽略意外非零退出。单独的 `consumer_source` 保存当前 consumer source set SHA-256 `4e1f1e86163a80b365cc35d6af6f870112e5cb7062d58c76c8c82ce4a9fa6ccb`，其中 `svc_job.py` 为 `4b61fcc63468856ee716c3cf0175b0ea4c06877114026b73ce914a0628bd6913`；历史 producer/C source/制品摘要仍是上文原值。

回归在现有 `windows()` module/report seam 注入一份真实 ordinary-user missing-export fence 记录的 13 种损坏副本：exit、timeout、error/error list、assign/继承和 terminate/accounting/reap/各 handle close。没有模拟正向 OS 成功，也没有为测试启动进程。原 consumer 实际观察 13 red，修复后 green；`out/svc-job/consumer-fix/regression-red.{json,log}` 还保留用原 `c1ae465` 源码在内存中重放的同一 red，及 `regression-green.log`。`tests/contracts/fixtures/svc-job-missing-export.json` 是该实际记录的最小合同输入，原始路径参数已缩为制品名，测量值和来源说明保留。

实际 Windows 使用同一个 `0410864e...` artifact 重新通过；missing-export/suite 各自 exit 0、未超时，accounting total 1/10、active 均 0，reap/3 handle close 全成立。实际 `--probe-mode hang --timeout 1` 正确 fail：missing-export fence pass，suite exit 98 / timed_out，自己的 accounting active 0、reap 和全部 close 仍成立。新原始结果在 `out/svc-job/consumer-fix/windows/{job,timeout}.json`，两次 launch 全部 facts 不再丢失。受限进程上下文另一次 missing-export capture 观测 15s timeout、零残留，普通用户上下文相同字节成功；两份记录均保留，未诊断其上下文差异，不外推为宿主 ABI 失败。

已并入 integration `f24787b`。consumer 修复后的 Windows 9/9 公开合同、Ubuntu 9 tests/1 Windows-only skip 通过；完整 Ubuntu `make check` 116 tests/25 explicit skips、static/bootstrap pass，记录于 `out/svc-job/consumer-fix/check.log` 和 `check/`。原三次 handle 增长/稳定基线、两个初始 failure、Core audits 和首份 archive 保持原样；新 evidence 另行归档。

## 真实独立 CI（2026-11-01）

修正合入 `e17981cee18d1d88d27f63bcbc0fa8691c24fe86` 后，
[Job run 41273469604](https://github.com/moorefu/qaplet/actions/runs/41273469604)
completed/success：Ubuntu producer 和 Windows consumer 均通过。制品 755199 字节，
SHA `0410864e8232379a7559410978f804da534b1550107f9a39ccdc050ef1bfaffb`，
与本地相同。实际 Windows x86_64/native/4096、kernel `10.0.20348`，
image `win22` / `20261026.347.1`；12 个实际 case 全通过。

两次 launch 的完整 supervisor facts 均保留并参与判定：missing-export/suite
accounting total 为 1/10，active 均为 0，terminate/reap/Job/process/thread close
全部成立。Python runner 本身原先在 Job 中、实测 limits 0；只分配新建的受控
artifact 及其后代。native 自有句柄 24 打开/24 关闭；首次进程创建前为 91，
三次初始化后为 93/93/93，完整 suite 前后 93/93。初次 +2 的来源仍未证明，
`capability_scope` 继续限定为已测 owned fixtures 与初始化后的稳定基线。

原 producer/Windows ZIP 的服务端长度/SHA、全部 CRC、制品字节、全部 fence
断言和 184 个唯一原始日志路径已独立复核。artifact IDs 为 `13070352016` /
`13069749760`，原归档在主 `.cache/downloads/ci/`，解包报告位于
[`ci12-e179/`](../../out/m0-integration/ci12-e179/)；`verification.json` 和
`acceptance.json` 分别记录传输与实际结果核查。历史失败与本地实际 timeout 证据保留。
