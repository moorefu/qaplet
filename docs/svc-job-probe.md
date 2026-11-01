# 应用私有 Job Object 探针

`apps/svc/probes/job_probe.c` 与 `scripts/svc_job*.py` 只属于 M0-SVC 应用探针，不是 Qaplet Core 或 Qaplet Embed SDK 的 API。SCM 有独立制品、工作流及判定。所有构建在 Ubuntu 执行；Windows 只验证 producer 记录的同一 SHA-256/长度制品。

```sh
python3 scripts/svc_job.py build \
  --output-dir out/svc-job/artifacts --report out/svc-job/build-info.json
# Windows，不调用编译器
python scripts/svc_job.py run --input-report out/svc-job/build-info.json \
  --artifact out/svc-job/artifacts/svc-job.com --report out/svc-job/windows/job.json
python scripts/svc_job.py check-ci --report out/svc-job/check-ci.json
```

Worktree 的构建须显式传 `--toolchain-cache /mnt/d/Workspace/dairn/qaplet/.cache/toolchains/cosmocc`、`--helper-source-cache /mnt/d/Workspace/dairn/qaplet/.cache/sources/ape-loader` 和 `--build-dir /mnt/d/Workspace/dairn/qaplet/.cache/build`，保持实际可复用输入和中间件都在主仓库 `.cache/`。离线校验、原 Nightly/派生 ELF helper、64K packer 及构建后 recheck 复用既有 producer 路径；没有新下载 fallback。输出目录必须为空。Linux 可传 producer 分发的 `--loader artifacts/ape-x86_64.elf` 验证 ordinary entry 和 Windows 私有入口拒绝，不能由此声明 Job 能力。

私有 ABI 对全部动态函数指针显式声明 `__msabi`，在 x86_64 ordinary main thread 上运行；没有 foreign-thread callback、TLS attach 或 JS。Windows runtime guard 先于 DLL 解析，ARM slice 只有普通入口。Ubuntu 保留结构大小/offset 编译断言、调用反汇编、两种 CPU map、fat headers 和 PE imports 审计；private Job API 不成为静态 PE import。Core 构建配方未引用本探针，Core 自身的服务导入门禁继续独立执行。

Windows launcher 先测当前 Python runner 的已有 Job 归属及 immediate limits。它创建一个新的、不可继承的 unnamed Job，设置并检查 kill-on-close，然后 suspended 创建指定制品，测该制品原有归属，再 assign、核验新 Job 归属、resume。它从不 assign runner，也不取得或 terminate runner 的既有 Job handle。C 探针在这个 fence 内创建自己的 Job：同样先设置 kill-on-close，suspended 创建 fixture，assign、验证、resume。所有 process 创建禁用 breakaway。launcher 在 `finally` 终止自己的 fence，等待制品、查询 accounting 的 `active_processes == 0`，关闭 Job/process/thread handle。fence 创建或分配不成立时不 resume 制品。

实际覆盖的场景为：

- 一个 owned parent 通过 CreateProcessW 创建一个 inherited child；在同一私有 Job 查询恰好两个 PID，分别核验归属，TerminateJobObject 后分别 wait，记录两个实际 exit code 及 accounting。
- 一个 owned leaf 依次加入两个新 Job；核验双重 membership，close 内层最后 handle 后 wait，外层 accounting 为零；立即重复 close 得到错误 6。
- 新 Job 的 query-only duplicate handle 拒绝分配，记录错误 5 和前后 membership；该 suspended fixture 不 resume，独立终止和 wait。
- 一个 leaf 用原始 ExitProcess(37) 提前退出；另一个 leaf 在 150ms wait 返回 WAIT_TIMEOUT 后由 kill-on-close 终止和 wait。
- 三个不 resume 的初始化 fixture 之后，记录 owned opened/closed 和全进程初始/重复/最终 handle count。只要求重复基线稳定及所有 owned handle 的 CloseHandle 成功；初始增长保持原值，不把未归因的首用增长隐藏或解释成已证明的 Windows 内部缓存。
- `--probe-mode hang --timeout 1` 实际触发 launcher deadline。报告必须 fail；独立 fence 清理仍须证明零 active process、制品已 wait 及 handle 已关闭。CI 的公开命令合同检查该负向结果和清理。

`svc-job-{build,run,check-ci}` v1 是独立结构化报告；公共 source/toolchain/build/artifact、host/CI 和时间身份由 case 继承，每个 case 另保留 expected/actual、exit code、时限和原始命令日志。consumer 独立检查 native actual 值，不信任 native 的 pass 标签。缺项是 not_run、错误值是 fail；真实 assignment/nesting ERROR_ACCESS_DENIED 与 ABI 失败分开保留。build/Linux 验证的 capability 保持 not_run；Windows 全部必需 case 和外层清理成立才标记本覆盖范围 verified。`m0_core_status` 始终 not_run。

每次 Windows launch 的完整 command/facts 分别保留在 `supervisors.missing-export` 和 `supervisors.suite`；两次都独立核验非继承/kill-on-close/suspended/assign/verify/resume、预期 exit、timeout/error、terminate、accounting、reap 及全部 handle close。第一 fence 任一失败立即停止，不能由后续 suite 覆盖。`--resolve-missing` 断言 API 返回 null/error 127 后本身应 exit 0；非零进程状态不能作为负向 API 的预期错误被忽略。`consumer_source`/`runner` 保存当前 consumer 字节身份，历史 producer source/build/C artifact 维持原身份。

`.github/workflows/svc-job.yml` 独立调度 Ubuntu producer 和 Windows consumer；后者只下载同 commit 的应用制品、验摘要、调用共享 runner，并保留正常/超时清理证据。工作流静态检查不代表远程执行。当前本地授权是用户指定的开发宿主，不冒称可丢弃 VM。

没有验证任意进程树、breakaway、跨 session/权限策略、已存在的无关 Job、重启、Windows ARM 或正式 Managed Process API。这些结果不能作为生产环境任意树回收保证。

ABI/行为依据：[Microsoft Job Objects](https://learn.microsoft.com/en-us/windows/win32/procthread/job-objects)、[AssignProcessToJobObject](https://learn.microsoft.com/en-us/windows/win32/api/jobapi2/nf-jobapi2-assignprocesstojobobject)、[QueryInformationJobObject](https://learn.microsoft.com/en-us/windows/win32/api/jobapi2/nf-jobapi2-queryinformationjobobject)、[extended limits](https://learn.microsoft.com/en-us/windows/win32/api/winnt/ns-winnt-jobobject_extended_limit_information)。锁内 libc 与上游源码对应关系仍保持 `unproven`。
