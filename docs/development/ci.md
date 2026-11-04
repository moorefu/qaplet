# CI 编排与证据收集

日常检查和工具合同使用现有 foundation workflow 与独立 `workflow-tools.yml`；两者均在 Ubuntu 执行。新工具 CI 先拒绝已跟踪的生成输出、检查 Python 语法与自身编排，再执行公共CLI合同，并在失败时保留报告。完整 M0/SVC 仍由各独立workflow调度。foundation中的 RESERVED 占位不等于这些独立workflow被禁用。

## 小输出与可重放结果

GitHub 插件取得 API 结果后先保留原始 JSON，仅将判定字段返回会话。`scripts/ci_evidence.py runs --input-response <saved-api.json> --commit <full-sha> --report out/ci-runs.json` 只过滤已保留结果，不发网络请求；输出不展开 repository/actor 等大对象。成功完成的 run 以该收据复用，状态变化或显式刷新才重新获取。

`ci_evidence.py fetch --manifest <archives.json> --output-dir out/collected --report out/collected.json` 显式获取或导入归档；`verify` 用相同参数完全离线重核缓存与解包字节。manifest schema_version 为1，artifacts为非空数组，每项包含唯一的 id/name 和 archive：精确 sha256、size_bytes，以及缓存缺失时的 path或url（二选一）。url使用插件提供的下载地址；工具不读取Git凭据、不向重定向附加认证，收据不保存签名URL。输入选择文件应放在忽略的out下。

```json
{
  "schema_version": 1,
  "artifacts": [{
    "id": 123,
    "name": "producer-COMMIT",
    "archive": {"sha256": "64位小写摘要", "size_bytes": 12345, "path": "original.zip"}
  }]
}
```

缓存固定为主工作区 `.cache/downloads/ci/<name>.zip`；已有坏缓存立即失败。每个交付文件检查CRC，拒绝越界路径、symlink、大小写重复和超限解包，重复验证同时拒绝已修改或额外文件。临时下载/解包目录在失败后清理，既有解包目录的错误字节保留。收据含原归档与逐文件摘要、物理root，可作为Core manifest的transports输入；原报告中的路径保持不变，显式path_mappings负责迁移。collection报告使用checkout的out下、解包目录之外的独立文件；目标检查未完整成功或发现与输入/交付字节碰撞时，只在终端报告错误，保持原字节。

`ci_evidence.py summarize --input-report <report.json> --report out/summary.json` 按已知schema/report_kind提取计数和清理事实。汇总明确 evidence_verified=false，不能代替完整consumer或新的OS执行；deep producer缺ci时保持null，Job supervisor从facts.cleanup读取。完整结果写报告，终端只显示短摘要。

SDK (`qaplet-sdk`)、M1 gate (`qaplet-m1-evidence`) 和严格回归也支持小摘要：保留产品/SDK/配方身份、实际测试计数、必需/可选缺口数量、有限失败片段；不展开 commands、raw logs 或完整 evidence_index。报告文件保留最多八项失败/缺口/制品样本。

### 即时引用下载

已有 provider catalog 时，使用 `ci_evidence.py fetch-actions --manifest CATALOG --repository OWNER/REPO --commit FULL_SHA --run-id ID --output-dir out/collected --report out/collection-NEW.json`。`--artifact-id ID` 可重复以显式选择子集。工具校验 provider 的 run/commit、原 ZIP digest/长度/expiry，逐件向固定 GitHub API 取得引用并立即开始下载；引用只在内存中存在。已有 `GITHUB_TOKEN` 仅发送给固定 API host，下载重定向不携带认证。单件下载 401/403/410 最多刷新一次引用，失败和后续成功各留一条 attempt，不重试坏摘要/CRC。没有可用 API 读取授权时保留失败，沿用插件取得本地 ZIP 后的普通 fetch 入口。

collection 必须使用新报告路径，保存全部失败尝试。成功报告中的 `artifacts` / `local_manifest` 仅含已验本地 archive.path，可直接复用普通离线 verify；原 catalog 保留安全的 API 地址，签名下载 URL 和 token 不写入输出。测试允许显式 literal loopback HTTP API，始终不携带 token；生产 API 固定为 `https://api.github.com`。

## Fast / Cross / Full

`m0.yml` 保留原 workflow 身份。共享 `scripts/ci_tiers.py plan` 从实际 event/ref 与 PR diff
选择层级，并输出可重放的 JSON 与 GitHub outputs；计划中的 `status=pass` 只表示选择有效，
`execution_status=not_run` 和 `evidence_verified=false` 不表示执行通过。

- 每次 branch push 运行 Ubuntu Fast：仓库/统一锁检查与实际 Hello、eval、ESM/Promise producer。
  正式 M2 的 `codex/m2-integration` 推送提升为 Full，以便在集成点及时取得当前五原生及实际 64K 证据；仍保持非默认分支不可写可信缓存。普通实施分支继续 Fast。
- PR 运行 Cross：同一 Ubuntu producer 分发给 Linux x64/ARM64、Windows x64、macOS x64/ARM64。
  除 `docs/`、`.scratch/` 和根级已知文字说明外的改动均提升到 Full，包含新增未知源码位置、
  Engine/Host/SDK/include、锁、patch、native fixture 和 CI 脚本。无法取得 PR diff 时失败。
- 每日 03:17 UTC、`v*rc*` / `v*RC*` tag push 和敏感 PR 运行 Full：增加实测 ARM64 页补缺、
  64K 深探针、实际断网双构建及供应输入拒绝；Full 不宣称已实现未来 Node/soak/network TLS 合同。
- `workflow_dispatch` 的 `tier` 可显式选择 fast/cross/full，在所选 ref 重放。手工执行和本地
  CLI 静态检查须与实际远程 run/commit 的成绩分开记录。

恢复输入缓存之后仍无条件执行原件 SHA/长度、源码、完整 toolchain tree 与 helper 验证。
cache key 绑定统一锁原始字节，不使用宽松 restore prefix；只有可信默认分支的 push、schedule、
manual run 且所有验证成功才可 save。PR（包括同仓库 PR）不能写可信 cache。fork PR 只使用
Actions 提供的只读 token 与可读缓存，不依赖第三方 secret；固定原件无法取得时保留失败原因，
不能选择 rolling Nightly。长期原 ZIP 镜像仍为 06 的 `missing_resource` 发布门禁。

所有执行通过共享本地 CLI，workflow 仅负责调度、安装执行依赖与分发。每个 job 的证据即使失败
也上传。完整 Core 的 manifest、原 ZIP transport、日志和 producer/consumer 身份仍由既有只读
consumer 判定；startup Cross aggregate 不等于完整 Core。

Full 的 `seal-offline` 在实际 pair 成功后重新核对 recipe 与 helper 原字节，保留四个 helper 比较
原件并生成 identical recipe receipt；这一步只准备 transport。`collect-actions` 只读取当前 run 的
相同 commit 制品，下载原 ZIP 并核对 provider SHA-256，再复用 `ci_evidence.py fetch` 检查 CRC、
路径与解包内容。鉴权只发往 GitHub API；下载 redirect 不携带 token，不保存 token 或签名 URL。
PR API head 与实际 merge commit 分别保留，消费者仍绑定实际测试的 merge commit。
`manifest` 保留显式原路径映射、原 ZIP/provider ID 与必需 producer/native/guest/offline 选择，
然后 `core_gate.py evaluate` 执行原完整合同。缺必需 report、错误 commit、坏原件或失败 probe
不能以空 manifest、计划状态或下载成功替代。

特权 producer 前先由 runner 创建 `out/m0/offline`；sudo 仍只执行 namespace setup，worker
降权运行实际构建，原报告由 root 写入。随后非 root seal 可在该 runner-owned 父目录新建 receipt
与 transport，避免对仓库/缓存做递归 chown/chmod。可用 `QAPLET_CI_POSIX_UID` / `_GID` 在 Ubuntu
root controller 显式运行对应 CLI 合同：实际 namespace preflight 与 `setpriv` 非 root receipt writer。

## 显式固定 guest 输入供给

实际 64K page job 可显式消费同仓库固定输入供给。`guests/ci-inputs.json` 绑定原
`guests/arm64.lock.json` 的完整长度/SHA 与独立 data-only 提交；`guest_inputs.py plan`
只输出经过格式检查的完整 commit，第二次只读 checkout 固定到
`.cache/guests/supplied`，不持久化凭据。数据提交只含当前 65536 profile 的七件
公开上游原输入及 manifest，不合入产品源码，也不属于 Release 或长期公开镜像。

```sh
python scripts/guest_inputs.py import --source-dir .cache/guests/supplied \
  --source-commit FULL_SHA --cache .cache/guests --page-size 65536 \
  --report out/page-gaps/65536/supply-import.json
```

入口在独立暂存目录复用原 `arm64_guest.py verify`，校验当前锁、完整成员、全部
SHA/长度、真实 gpgv 签名及 InRelease→Packages→package 关系后才发布 canonical 输入。
额外/缺失成员、错误 manifest、路径别名及坏既有输入都拒绝，既有正确字节保留。
供给原件和目标不重叠，报告不得覆盖它们。收据记录固定 data commit、manifest、
每件来源/目标身份、原 URL、签名原日志与 `imported` / `existing-verified`。
来源说明为已验签既有缓存传输，不能称为本次 HTTP 获取。

随后仍执行原 fetch、prepare 与当前产品的实际 guest；fetch 缓存命中不表示新 HTTP 200。
4096 profile 继续原获取入口。fork/token 按同仓库普通只读权限读取；固定 data commit
不可读时 checkout 保留真实失败，import 保留 `missing_resource`，不依赖第三方 secret
或可信 Actions cache 写入。导入成功不等于实际 64K、SDK、Core 或 M2 验收通过。
实现及本地证据见[固定输入供给验证](../../.scratch/qaplet-m2/guest-input-supply-validation.md)。

## CI skeleton

`.github/workflows/foundation.yml` 用 JSON 表达 YAML，便于在无第三方 Python 包的本地环境解析。`make check-ci` 检查触发器、只读权限、共享命令、失败时上传证据及禁用的产品槽位，并验证静态检查先于编译和合同测试；同时用 `build_ape.py check-ci` 委派共享 tier 合同检查独立 `m0.yml` 的分层选择、可信 cache、固定输入/构建/分发合同，拒绝测试 job 再编译。没有执行 Actions 服务端校验或 actionlint。

提交前在 Ubuntu 运行 `make check`。Windows/WSL 差异由普通用户上下文的对应验证命令采集，并分别保存报告；其他平台消费 Ubuntu 已构建制品。受限上下文仅补充可见范围内的证据。WSL 不可用负向用例用显式不存在的 `--wsl` 隔离；其回归位于 `tests/contracts/test_foundation.py`。

bootstrap 编译矩阵仅含 Ubuntu runner，compiler 使用 PATH 中的 `cc`。静态检查拒绝非 Ubuntu 编译行及开发机编译器绝对路径；Ubuntu 本地 `CC` 仍可显式使用绝对路径。历史 Windows GCC 配置修复保留在验收记录中，当前不再调度 Windows/macOS bootstrap 编译。

启用的 `bootstrap` job 在 Ubuntu 上做环境盘点、真实 C 冒烟和合同回归；doctor 的 blocked 允许继续，测试失败则阻断。报告与原始日志即使失败也上传。runner 页大小来自实际 OS 查询，不能从镜像名推断。该 workflow 名称和 artifact 均标为 bootstrap；其他平台的产品/SDK 验证由对应 workflow 消费同摘要制品。

禁用的 `m0-build-distribute`、`m0-native-pages`、`m0-qemu-page-gaps` 分别为锁定构建/同摘要制品分发、原生 ARM64 页探测和缺失页配置完整系统 TCG guest 预留位置。SCM 和 Job 分属 `svc-scm`/`svc-job`，使用独立应用制品，不进入 Core 门禁。启用这些位置之前须由对应任务接入可运行的共享脚本、输入与制品；当前不能从 disabled/skip 推出通过。首次远程 bootstrap 的实际结果及 Windows 配置修复见[验证记录](../../.scratch/qaplet-m0/foundation-validation.md#首次远程-ci-与-windows-配置修复)。

## CI 查询与故障定位

1. 先按待验证的提交 SHA 找到对应 run，读取 status/conclusion。run 已 completed/success 即完成成功核验；将 SHA、run 链接和结果写回任务证据。
2. run 失败时读取 job 摘要，定位失败步骤，再读该 job 的日志。日志只有 blocked/摘要时，从已上传的 artifact 读取具体 reason 和原始命令日志。
3. 插件传输失败先做一次有限重试，仍失败就报告查询暂不可用。GitHub 404 与账号登录状态分别核验；读取当前身份和仓库可见范围后再判断是否需要用户重新授权。

每次只返回判定所需字段和失败片段。保留已取得的 run/报告对象供后续解析；只有重新运行、状态尚未结束或用户要求刷新时才再次查询远程。
