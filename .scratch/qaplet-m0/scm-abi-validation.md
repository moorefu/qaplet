# BOOT-05A：应用私有 SCM ABI 探针

2026-11-01；独立分支 `codex/m0-11-scm`，初始 integration `42fb679`。公共 seam 沿用父规格已确认的外部构建/消费命令、portable native probe、真实 SCM 查询与结构化 evidence。没有生成正式 qaplet-svc、Runtime/SDK API 或 JavaScript 服务入口。

本轮按用户“直接使用本机”的覆盖要求在当前 Windows 开发宿主执行；不把该宿主称为隔离 VM。所有编译在 Ubuntu-22.04 完成，Windows 只消费同摘要制品；物理缓存、中间文件仍为主工作区 `.cache/`。

## 边界与可复现命令

`apps/svc/probes/{scm_main.c,scm_callbacks.c,scm_abi.h}` 是应用私有探针。默认 Core 编译配方、源清单、制品和 `.github/workflows/m0.yml` 未修改。SCM 动态名称只在该独立制品及 Windows driver 中出现；ARM64 slice 以实际 `nm` 检查没有 `appscm_*`、`LoadLibrary`、`GetProcAddress`。

```sh
python3 scripts/svc_scm.py build \
  --toolchain-cache /mnt/d/Workspace/dairn/qaplet/.cache/toolchains/cosmocc \
  --helper-source-cache /mnt/d/Workspace/dairn/qaplet/.cache/sources/ape-loader \
  --build-dir /mnt/d/Workspace/dairn/qaplet/.cache/build \
  --output-dir out/scm-build-02/artifacts \
  --report out/scm-build-02/build-info.json
```

生产方先后独立验证锁定 Nightly；原 ZIP SHA-256 `f2f984d3a9790674fc0da11c1c00a32d4fe3f8dafde99af74b0c6dc24a2a16cf`，GCC 14.1.0，libc source commit 继续 `unproven`。复用经独立检查的 Linux ELF 工具视图和 07 的 source-locked portable helpers；不修改原归档、tree 或全局 WSLInterop。

Windows 从相同 worktree 运行：

```powershell
python scripts/svc_scm.py run `
  --input-report out/scm-build-02/build-info.json `
  --artifact out/scm-build-02/artifacts/svc-scm.com `
  --report out/scm-windows-ordinary/scm.json
```

Linux 消费入口使用同一 `run`，另显式提供生产方分发的 `--loader out/scm-build-02/artifacts/ape-x86_64.elf`。非 Windows 的 SCM 入口在任何 DLL lookup 前返回非零；普通入口只报告本机架构、`sysconf` 页大小及 `scm_calls:0`。

## 调用约定与回调约束

私有头使用固定 32-bit DWORD/BOOL、64-bit handle、16-bit UTF-16 单元。Ubuntu 编译通过 `SERVICE_TABLE_ENTRYW` 16 字节、`SERVICE_STATUS` 28 字节、`SERVICE_STATUS_PROCESS` 36 字节及 callback/PID/checkpoint offset 的静态断言；没有把宿主 `long` 或 `wchar_t` 当作 Windows 类型。

每个 DLL raw function pointer 和两个回调定义均显式 `__msabi`。`scm_callbacks.c` 不调用 libc、JS、pthread、分配器、stdio、TLS，也不假设 SCM 创建的线程有 Cosmopolitan TLS。回调仅使用启动线程预先解析的 Win32 指针、静态数据和经编译器断言 lock-free 的 4/8 字节原子。ServiceMain 注册 HandlerEx，依次提交 START_PENDING/RUNNING；STOP handler 提交 STOP_PENDING 并触发 Win32 event；ServiceMain 最长等待 20 秒，提交 STOPPED 后关闭 event。

生产方对 callback-only x64 对象实际运行 `nm -u`，结果为空；保留 `objdump -dr` 原始汇编，检查无 FS/GS TLS segment、stack protector、atomic 外部 helper。编译显式关闭 profiling/tracing/stack protector；没有猜测 TLS attach/detach。汇编显示 raw MS ABI 参数使用 RCX/RDX 等寄存器和 shadow space，回调保留对应非易失寄存器。静态/汇编检查不能替代真实 SCM 回调执行。

## 本地实际证据

初次真实构建因缺少 `_COSMO_SOURCE` 扩展 opt-in，`IsWindows` 未声明而失败；原始报告保留于 `out/implement-spec/11/out/scm-build-01/build-info.json`。修正后 producer `out/implement-spec/11/out/scm-build-02/build-info.json` 通过，report ID `755d014e-0947-434c-9ec2-f2f8632f483f`。

| 制品 | 字节数 | SHA-256 |
| --- | ---: | --- |
| `svc-scm.com` | 755423 | `f0c4c88e253cab7266e4de4bffdbe8a3aa767b65b59a2aba93e07377e949d0f3` |
| callback-only x64 object | 3192 | `21196d7c3c911760b33736598700c658e3e220dc09ba39bb6b3a899a5937cf23` |

Ubuntu / WSL2 Linux x64 / kernel `6.6.87.2-microsoft-standard-WSL2` / 4096 字节页实际普通入口通过，SCM 入口实际拒绝。两个 fat slice、独立 sidecar、双 CPU map、PE imports、实际 BUILDLOG 和所有命令/stdout/stderr 均在相同 producer evidence/artifacts 下。

普通 Windows token 报告为 `out/implement-spec/11/out/scm-windows-ordinary/scm.json`，report ID `b1113a4f-483c-4332-9f65-3d38d796e501`。Windows x64、kernel `10.0.22000`、OS API 页大小 4096、token `elevated:false`。

- 同摘要普通入口通过。
- 8 个 raw export 解析通过；返回私有结构大小及 `ms-x64` ABI。
- 故意缺失 export 被拒绝，实际 Win32 error 127；该负向用例不会把真实可解析的能力标为 unavailable。
- `OpenSCManagerW(SC_MANAGER_CONNECT)` 成功且句柄关闭。
- `OpenSCManagerW(CONNECT|CREATE_SERVICE)` 实际返回 0 / error 5；整体 `blocked`、SCM capability `blocked`。该权限结果不是 Nightly callback ABI 失败。
- 没有创建服务或 fixture 进程；报告保留 `service_created:false` 及原始 API 错误。System/Service Control Manager 事件查询的原始输出也保留。

## 有界生命周期与清理

Windows supervisor 在验证分发 SHA/长度后生成 `qaplet-m0-scm-<UUID>` 及精确 binary command line；服务是 own-process、demand-start、LocalSystem，不设置自动启动或恢复策略。worker 只创建这一命名对象，OS `QueryServiceStatusEx` 是 RUNNING/STOPPED 真值；重复 stop 期望 1062。每次 API 记录起止时间、返回值和立即读取的 Win32 error；状态查询和服务配置保留原值。

启动/停止轮询各 8 秒、服务 event wait 20 秒；supervisor worker deadline 90 秒，独立 recovery deadline 60 秒。`finally` 执行 stop/delete/close，并轮询 `OpenServiceW` 到 1060，不能仅把 DeleteService 成功当作已删除。SCM handle 用 `CloseServiceHandle`；status handle 无需关闭。

正常 worker 在 OS RUNNING 查询后保留精确 process handle，并记录 PID、creation FILETIME、image、服务 UUID 和 artifact SHA。停止后通过同一 kernel process object 等待退出。恢复 worker 在任何强制终止前重新核对 UUID 服务的完整配置，以及原来记录的 process creation time 和 image；PID 复用只证明原进程已经退出，不能触及复用该 PID 的进程。没有把宿主进程或现有服务当作清理对象。

`cleanup --plan <fixture-plan.json>` 是独立恢复入口。CI 的 Windows job 使用 `always()` 调用它并上传原始证据；producer 是单独的 Ubuntu job，Windows job 不编译。`check-ci` 合同检查此编排；下节另有真实远程执行证据。

## TDD 与当前能力状态

Ubuntu 上逐个红→绿：缺缓存先因 CLI 缺失失败后 blocked/不创建缓存；篡改分发物先缺 `run` 后在执行前 SHA 拒绝；无 loader 的 consumer 先虚假 pass 后 blocked；独立 CI 入口先缺命令后检查 Ubuntu producer/Windows consumer/always cleanup；向 Windows consumer 注入 `make build-ape` 先错误接受、修正后拒绝。五项保留测试均走 CLI seam，不 mock SCM 或 callback 成功。

真实管理员 SCM 生命周期已在用户授权的当前宿主通过。`out/implement-spec/11/out/scm-uac/` 固定 service plan、invocation manifest 和 entry script 经协调者审核并通过必要 OS UAC 启动；entry 在执行前验证 manifest 和全部 imported scripts、Python executable、producer、artifact、plan 的 SHA-256。该次 entry SHA `7940fc8078f96798d6b2d3334f51617db4546b7230b44b1ac03fad94f671680d`，invocation SHA `79dc76a99645169c080b49309d2a075f84c34f1f40e48bc26b412d278e85c90e`，plan SHA `fd5adee3031dac35860e8628081518faf36907da84b1ed2925164534755b8dbe`。

管理员报告 `out/implement-spec/11/out/scm-uac/scm.json`，report ID `8ded2fbb-603c-4ffd-ba95-8c4235b9d08f`，整体 `pass`、capability `verified`，token 实测 `elevated:true`。唯一 service 为 `qaplet-m0-scm-9b98fc414d26444885734f0fe5c7bbec`；SCM 实际查询 STOPPED（初始未启动的 error 1077）→ RUNNING（PID 79136、error 0）→ STOPPED（error 0），重复停止返回 0/error 1062。

回调结果由 dispatcher 返回后的原始 Cosmopolitan 主线程写出：dispatcher=1/dispatch_error=0、ServiceMain=1、HandlerEx=1、stop=1、status mask=30（START_PENDING/RUNNING/STOP_PENDING/STOPPED 的成功状态位）、first_error=0、wait_result=0、event_closes=1、JS calls=0。该 callback JSON 的 SHA-256 为 `d0f7a3d7fd034c0a584dd758add6f868fdda529e023e5cd4c12baadce90c6deb`。其中 OS RUNNING/STOPPED 有独立 SCM 查询，pending 状态由成功 SetServiceStatus 的回调位确认，不声称 driver 轮询恰好看到了每个短暂 pending 状态。

进程 identity 为 PID 79136 / creation FILETIME `134379785814933503` / 精确 artifact image；保留原进程 handle 等待返回 0，`forced:false`。SCM delete、manager/service handle close、process exit、post-delete service absence 全部为 true。entry 的 `finally` 再运行 `final-cleanup.json`，独立 worker 返回 pass，OpenServiceW 实际 0/error 1060，OpenProcess 对原 PID 为 error 87（已经退出）。无残留服务、受控进程或句柄；原始事件查询输出随 evidence 保存。

Core 边界在独立应用实现后实际再次检查：`out/implement-spec/11/out/scm-core-boundary/{hello,quickjs-eval}.json` 均 pass；Core Hello SHA `467f8889db7a671cd238a7cc56df933951fea686c7c524480f4112870da3695d`、eval SHA `426a7b05367fc6fd8d4831eebb2439782b3b66141af115094cc1efa766b05cd4` 与 07 的已提供 producer 一致，两个 CPU link maps/global symbols 和 PE imports 均无服务符号。Linux 独立消费报告 `out/implement-spec/11/out/scm-linux-consumer/scm.json` 同样 pass；它与 producer 的 Linux 普通入口不是同一条执行记录。

API 合同依据：[CreateServiceW](https://learn.microsoft.com/en-us/windows/win32/api/winsvc/nf-winsvc-createservicew)、[QueryServiceStatusEx](https://learn.microsoft.com/en-us/windows/win32/api/winsvc/nf-winsvc-queryservicestatusex)、[ControlService](https://learn.microsoft.com/en-us/windows/win32/api/winsvc/nf-winsvc-controlservice)、[DeleteService](https://learn.microsoft.com/en-us/windows/win32/api/winsvc/nf-winsvc-deleteservice)。当前服务探针结果不改变 M0 Core 完整门禁状态。

## 最终集成检查与证据保留

实施提交 `1109264` 后，已把 integration `8fb7c39` 合入本实施分支（merge `4f90c03`），包含 06 的 adaptive startup 和 editor watcher 修复。随后 Ubuntu 直接 `wsl --cd <worktree> -- make check OUT=out/scm-final-check-2` 返回 0：运行 107 项合同测试，其中 23 项显式实际输入/guest/宿主 opt-in 测试按其条件 skipped，其余 84 项通过，bootstrap C compile/run 通过；五项 SCM 合同全部通过。原始完整输出在 `out/scm-final-check-2/make-check.log`，静态/基础报告在同目录。此前一次 `make check` 的内部检查已通过，但外层 bash exit-code 引用错误返回非零；该输出也保留，没有把包装器失败隐藏为通过。独立 SCM 真实 Ubuntu/Windows 证据在上文另列，不用这些 skipped 的其他任务测试代替。

为后续清理实施 worktree 保留原始字节，全部本票 `out/` 输出（包括初次编译失败、普通权限 blocked、管理员成功、两次检查输出和 UAC 审核输入）已复制到主工作区 `out/m0-integration/svc-scm-11-verified/`，227 个文件 / 18547822 字节。`export-manifest.json` 为每个相对文件记录 SHA-256 与长度，复制后逐文件复核。原始报告仍保留当时实际执行的 worktree/Linux 路径；移动后按该 export 根和相对路径定位原始日志。UAC entry 只是历史审核证据，不是可重复执行的安装命令；新运行生成新 UUID/新 evidence。中间构建仍在主工作区 `.cache/build/`，没有把输入缓存或原始 evidence 纳入 Git。

## 真实 CI（2026-11-01）

集成提交 `1129cd82059c3a8ea62515196516342205c96b82` 的独立
[SCM run 41271366311](https://github.com/moorefu/qaplet/actions/runs/41271366311)
已 completed/success：Ubuntu producer 与 Windows consumer 均通过。实际制品
755423 字节，SHA `f0c4c88e253cab7266e4de4bffdbe8a3aa767b65b59a2aba93e07377e949d0f3`，
与本地产物完全一致。Windows 实测 x86_64/native/4096、kernel `10.0.20348`，
runner image `win22` / `20261026.347.1`，管理员 token 为 true。

实际唯一服务 `qaplet-m0-scm-22bcda6b726a4ba7845c3b10c47a3f02` 完成生命周期，
callback mask 30、first_error 0、JS calls 0，重复 stop 1062。
所有 service/process/handle cleanup 断言为 true，CI `always()` 独立 cleanup 也为 pass。
已取回两个原 ZIP、验证服务端长度/SHA-256、全部 ZIP CRC，以及 192 个唯一原始
命令日志路径。producer artifact ID `13069691115`，Windows artifact ID
`13069426617`；归档保存在主 `.cache/downloads/ci/`，解包证据位于
[`ci11/`](../../out/m0-integration/ci11/)，传输及语义复核分别为
`verification.json` 与 `acceptance.json`。原始失败和本机成功证据仍保留，CI 成功
不覆盖普通权限 error 5 的限制。
