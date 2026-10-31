# Qaplet v1：全新实施总体架构与工程交付计划

> **文档状态**：实施基线提案（Draft for Execution）  
> **版本**：v1.4（App-first / 本地优先、原生页覆盖与 QEMU 补充）/ 2026-10-31  
> **产品名称**：Qaplet  
> **产品定义**：**An Actually Portable, Node-compatible JavaScript Runtime & Application Platform**  
> **核心构成**：Bellard QuickJS `master` + 锁定的 Cosmopolitan Nightly `cosmocc` + Host Loop/Node/Web APIs + QJB/Application Compiler + **通用 C Embed SDK**。`qaplet-svc` 是利用这些能力开发的**第一方 Qaplet Application**，不属于 Kernel。  
> **设计决策**：从零创建独立代码仓库，不迁移历史实现和 API 兼容债；旧 `moorefu/quickjs-cosmo` 只作为事故清单、可复用测试素材与行为依据。
> **本次修订范围**：从“可选 SDK 扩展产物”进一步明确为“**用 Qaplet 开发的服务管理应用**”：JS 实现业务与 CLI，应用私有 Native Host 只负责 OS 回调/进程强约束，Linux systemd 优先 Direct 模式，产品、工程、构建、发布和验收均按 App-first 设计。

> **M0 验证策略更新（2026-10-31，用户调整）**：代码探索与调试优先在本地完成，CI用于持续集成。先测本地WSL和GitHub原生Linux runner的架构/页大小，再用完整系统QEMU补缺失的目标组合，不默认启动两套guest。GitHub原生ARM64 runner承担其实际页配置的ARM64验证；WSL x64的相同页大小不能替代ARM64。M0以§19及[ADR-0003](../adr/0003-m0-ci-qemu-validation.md)为准，native/emulated-system分别记录。

## 0. 执行摘要

**Qaplet 是可移植 JavaScript 应用运行时与应用交付平台；服务管理器 `qaplet-svc` 是由 Qaplet 开发、构建并发行的一款真实应用。** 这不是把服务管理功能从 Kernel 移到一个同样以 C 为中心的“扩展框架”，而是确认 Qaplet 能承载系统软件：服务管理器的配置、CLI、OS 管理编排、日志、健康检查、恢复策略等使用 Qaplet JS/ESM 实现；只有 Windows SCM 与 Job Object 等必须有原生入口、强状态边界和可靠兜底的地方使用薄 C Bridge。

**正式产品关系**：

```text
Qaplet Platform = Engine + Host Loop + Node/Web APIs + QJB/App Builder + C Embed SDK
                                           |
                                           v
Service Manager Application (`qaplet-svc`) = JS App + app-private Native Bridge
                                           |
                                           v
Windows SCM / systemd / SysV / OpenRC / launchd → Managed Business Process
```

**核心交付路线**：

1. **先证实 APE 基础**：锁定 Bellard QuickJS 主干和 Cosmopolitan Nightly、native优先、QEMU补充的APE与Linux ARM64 4K/64K页覆盖、Windows TTY/进程/IO；Windows SCM ABI 仅作为服务应用的独立早期探针。
2. **实现通用 Host Kernel**：沿 Redis `ae` 单逻辑 Loop 思路实现 Reactor、Timer、WorkerPool、CompletionQueue、Process、关闭/取消合同，不复制整套 libuv。
3. **形成 JS/Node 最小闭环**：ESM/CJS/package resolver、FS/Stream/Process/TTY/Net/HTTP/Web API 等真实能力矩阵，通过真实 Node Oracle 差分验收。
4. **形成真正的应用平台**：QJB、QJB/APE VFS、大资源 mmap、单文件 APE App Compiler 和 C Embed SDK，让普通 App 与具有私有 C Host 的 App 都能使用同一构建链。
5. **将服务管理作为第一方示范应用实施**：`apps/service-manager/src/*.mjs` 首先完成 JSON/YAML/.env、CLI、systemd unit、健康/日志/退避；`native/*.c` 仅补 SCM/Job Object/停止/Watchdog。既可单独发布 `qaplet-svc`，也可随业务软件发行。
6. **分别建立真实发布证据**：Runtime/SDK、一般 App Builder、服务管理应用各自有发行物、能力矩阵、跨 OS/CPU、原生回调与进程树负向测试和 SBOM，不以服务应用失败阻断 Core 发版。

**重要设计红线**：默认 `qaplet` 二进制**不链接 SCM、Job Object、service config、systemd unit**；普通 APE 不因服务应用增加代码。所有 QuickJS 操作只在 owner thread；Windows SCM HandlerEx / Unix async signal handler **严禁直接运行 JS**，由 app-private 原生 stop latch、线程安全事件投递和 native watchdog 保证兜底。Job Object/进程组保障需要真实 OS 测试；QJB/QuickJS Context 不是 OS 沙箱。核心 Runtime 和 App 的 `supported` 声明均以实际构建与测试为准。

**重要事实边界**：上游 SHA 与版本信息沿用原计划在 2026-10-31 调研时的参考记录（见 §1），**不是这次修订重新联网核验的最新 HEAD**。本文为实施设计，不把拟议接口、命令和 Demo 描述为现有已实现功能。旧仓库 `moorefu/quickjs-cosmo` 只用于复现负向案例和借鉴行为，不是改名迁移。

---

## 1. 事实输入、选型基线与版本锁定

### 1.1 前版于 2026-10-31 记录的上游提交参考（本次未重新核验）

| 组件 | 仓库 | 分支 | 前版记录 SHA（待实施时更新） | 在 Qaplet 的角色 |
|---|---|---|---|---|
| **QuickJS** | [`bellard/quickjs`](https://github.com/bellard/quickjs) | `master` | `19d6bd865e709e2b5717fb4f2290c0cecc57ee4b` | 唯一 JS 引擎、ESM、Promise Jobs、QuickJS C API |
| **Cosmopolitan** | [`jart/cosmopolitan`](https://github.com/jart/cosmopolitan) | `master` | `aefad27c09ffda64c6ac64bd69f758e9f9c7b91d` | APE、Libc、ZipOS、系统抽象 |
| **txiki.js** | [`saghul/txiki.js`](https://github.com/saghul/txiki.js) | `master` | `e6b651c81eefb084c6fc99960c17cb15a13563eb` | 工程组织、宿主生命周期、Web APIs、网络/Worker 参考 |
| **Redis ae** | [`redis/redis`](https://github.com/redis/redis) | `unstable` | `6a5fee67583480de1fec323649f43fd000001b49` | 事件循环思想参考，**不复制新版源码** |
| **AWS LLRT** | [`awslabs/llrt`](https://github.com/awslabs/llrt) | `main` | `587c9a2b953df0fba33f12ac19efb0cd98bdcda6` | Node/Web 能力矩阵、差分和安全回归参考 |
| **llamafile** | [`mozilla-ai/llamafile`](https://github.com/mozilla-ai/llamafile) | `main` | `a3ccf098ed9c7c77687f5430b0143156d3bcc45a` | ZIP64、未压缩权重、对齐 mmap、大文件加载 |
| **历史项目** | [`moorefu/quickjs-cosmo`](https://github.com/moorefu/quickjs-cosmo) | `main` | `0c72dc734d896a904891611429dc5c05061a41f7` | Windows/64K/TLS/TTY/QJB/CI 负向测试案例 |
| **zipalign** | [`jart/zipalign`](https://github.com/jart/zipalign) | `main` | `2ee138518c3b336b2be93f414172beaedbdff012` | ZIP STORE 成员跨平台页对齐方案参考 |

以上 SHA 是 **本计划调研时刻的参考输入**，不是承诺未来任何时间仍为“最新”。**实施真正开始时**须在无修改的原版上游重新解析 HEAD，在 PR 中记录新 SHA，并先做可行性验证，之后以 lockfile 固定。后续升级走独立 PR/CI，禁止构建期间 `git pull`/自动覆盖固定依赖。

### 1.2 Nightly cosmocc 是滚动工具链，不等于固定的 Cosmopolitan Git SHA

- 官方通道：[`https://cosmo.zip/pub/cosmocc/cosmocc.zip`](https://cosmo.zip/pub/cosmocc/cosmocc.zip)，其内容可能随时间更改。
- 前版 2026-10-31 调研时记录的正式 Release 为 `4.0.2`；**此处是历史参考数据，未在本次文档更新中重新核验**。构建选型仍采用锁定摘要的 Nightly，不以旧 Release 代替。
- `bootstrap-toolchain` 下载当时的 Nightly 后必须记录原始归档 SHA-256、长度、获取时间、工具链版本输出、编译器二进制摘要、包含的 Cosmopolitan libc/headers 版本标识、在目标主机上可运行的探针摘要；如不能直接证明工具链绑定到具体源码提交，则注明 `source-head` 与 `toolchain-build` 是两个独立事实，不能猜测相等。
- CI 和复现构建只能从只读缓存/版本化镜像获取**已锁定摘要**，Hash 不一致必须失败。使用新 Nightly 必须执行 `toolchain-bump` 专题，并完整重跑 APE、ABI、安全和交付测试。
- 正式发行还需记录构建配方和 SDK `compile/link flags`；禁止在应用交付环境要求 Python、Node、编译器或外网。

建议锁文件：

```json
{
  "schema": "qaplet.toolchain-lock.v1",
  "quickjs": { "repo": "bellard/quickjs", "ref": "master", "commit": "19d6bd865e709e2b5717fb4f2290c0cecc57ee4b" },
  "cosmopolitan_source": { "repo": "jart/cosmopolitan", "commit": "aefad27c09ffda64c6ac64bd69f758e9f9c7b91d" },
  "cosmocc_nightly": {
    "url": "https://cosmo.zip/pub/cosmocc/cosmocc.zip",
    "sha256": "TO_BE_MEASURED_ON_FIRST_PIN",
    "compiler_build_id": "TO_BE_CAPTURED",
    "libc_source_revision": "UNVERIFIED_UNTIL_TOOLCHAIN_ATTESTATION"
  }
}
```

`TO_BE_*` 是实施前必须补全的占位符，绝不在此报告中杜撰 Nightly 哈希。

### 1.3 技术依赖与授权原则

- QuickJS：原版、少量可审计补丁；保留原始许可证；不切到 QuickJS-NG、不引入第二 JS 引擎。
- Cosmopolitan：采用 Nightly 的系统库与 APE 产物格式，唯一 Host ABI，不引进相互竞争的独立 libc 封装。
- Redis：以 `ae` 的模型（file events / time events / beforeSleep / afterSleep）启发自有实现。Redis `<=7.2` 源码 BSD-3-Clause；新 Redis 版本有 RSAL/SSPL/AGPL 多许可证，**不得把最新版 `ae.c` 直接拷入 Qaplet**。
- txiki.js（MIT）：学习 `src/vm.c`、`src/timers.c`、`src/mod_tls.c`、`src/worker.c`、`src/httpclient.c`、`src/ws.c` 的错误/GC/调度边界；不直接移植整套 libuv/libwebsockets/TJS 公共 API。
- LLRT（Apache-2.0）：参考其分模块矩阵、Node 兼容测试和 Web Platform Tests；Rust/rquickjs 的代码若要复用需单独许可评审与转换成本审计。
- llamafile（Apache-2.0）：复用其技术经验（STORE、ZIP64、原始 fd + mmap、对齐），实现要有独立输入校验与 Windows 兼容性审查。
- 第三方协议库如 wslay/nghttp2/Mbed TLS/WAMR 均要遵循“**小闭包、静态链接、裁剪、符号隔离、许可证/SBOM、安全审查**”，每个后端独立 feature gate。

---

## 2. 产品定位、主要场景及非目标

### 2.1 Qaplet 平台的三种通用使用模式

**A. Runtime（执行）**：`qaplet run` 执行 JS/ESM/CJS/QJB，提供经过声明的 Node/Web APIs、FS/TTY/网络/进程/事件循环等，服务管理概念不出现在标准运行时 API 中。

**B. Portable Application Builder（交付）**：`qaplet pack/compile` 制作 QJB 或独立 fat APE。普通 App 走默认宿主；有 OS 特殊入口的 App 可通过**通用 C Embed SDK + 自定义 Host Template**静态链接原生模块（v1 从 Makefile 直接链接起步，后续泛化 `compile --host`）。应用资源/依赖仍由同一 Qaplet 构建体系处理。

**C. Embed SDK（扩展原生宿主）**：任意 C 应用都可以使用 `libqaplet-embed.a` 初始化 Runtime、运行模块、注册应用私有 native module，并通过线程安全 POD 事件交接到 JS owner Loop。SDK 不附带 Windows Service 管理业务。

**第一方应用样板**：`qaplet-svc` 属于**平台上的 Application（`apps/service-manager`）**，与未来的部署 Agent、MCP Server、运维工具、TUI 产品处于同一应用层级。它不是“Qaplet 的第四个内核运行模式”，也不要求其他用户运行 Qaplet 时安装它。

### 2.2 首批目标工作负载

企业远程诊断和维护 CLI、独立 TUI、部署/运维 Agent、MCP HTTP/stdio 服务、离线脚本、自动化任务、精选纯 JS npm CLI、独立 QJB/APE 应用，以及以 `qaplet-svc` 为范例的**长期运行系统应用**。服务管理器既承载 Java/Python/Go/EXE/APE 业务，又应反过来验证 Qaplet 的 JS 模块、文件/流、子进程、网络、SDK 嵌入、APP 编译和观测的真实完整性。

### 2.3 v1 非目标

完整 Node.js 100% API、任意 `.node` 原生扩展动态加载、V8/Inspector 深度兼容、替代 npm/pnpm、所有 CPU 架构、跨 OS 的随意动态 DLL/so、生产 HTTP/3、绕开 Gatekeeper/Defender、安全隔离的 QJB 沙箱、运行时默默联网更新、让 `qaplet` 默认具有 SCM/systemd 服务管理接口、用 C 重写服务管理器的 YAML/日志/恢复/CLI，以及无验证就宣称所有系统 init 版本兼容。

### 2.4 发行物身份与交付原则

| 制品 | 面向用户 | 内容 | 发布门槛 |
|---|---|---|---|
| `qaplet` fat APE | JS 开发者/工具用户 | Engine、Host、Node/Web APIs、QJB/编译 CLI | Runtime/Node/APE CI |
| `app.qjb` / 普通 `myapp.com` | 一般应用用户 | App 资源 + Qaplet 宿主（按模式） | App 构建/资源/跨 OS 测试 |
| `qaplet-sdk` (`libqaplet-embed.a`) | Native 宿主开发者 | C headers、稳定 ABI、样例、static link | ABI/线程安全/版本合同 |
| `qaplet-svc` fat APE | 服务安装/运维人员 | **JS Service Manager Application** + Qaplet Runtime + 很薄的应用私有 SCM/Job Bridge | **应用级** SCM/systemd/故障/权限测试 |
| `qaplet-svc` 随包模式 | Java/Python/Go 等业务安装包 | 同一个服务应用 + 按名自动加载的配置/业务内容 | 应用版本和被托管软件的部署测试 |

同一 APE 字节身份按**每个发行物**独立证明，`qaplet` 与 `qaplet-svc` 不需要 hash 一样；Windows 文件名 `.exe`、Unix `.com` 的重命名不应变更实际 payload。企业签名/公证变体单独发行和测证据。服务应用失败不得无条件阻断 Runtime 发布；但**作为应用平台的端到端参考案例**，它的失败必须形成明确的 App Builder/SDK 改进 Issue，而不是绕过通用能力偷偷做一套 C 特例。

---

## 3. 架构决策记录（ADR v1 冻结候选）

| ADR | 决策 | 理由/可验证边界 |
|---|---|---|
| ADR-01 | **APE 单文件交付为硬约束** | 每个承诺可移植的可执行应用独立验证跨 OS/CPU；签名特殊变体单列 |
| ADR-02 | **原版 Bellard QuickJS `master`** | 锁 SHA 后编译，补丁最小、可复现，不引入第二 JS 引擎 |
| ADR-03 | **Cosmopolitan 是 Host ABI 基础** | Core 优先 Cosmo；应用可有受审计的 OS 私有 ABI Bridge，不能污染通用 Runtime |
| ADR-04 | **一个 Runtime 一个 JS owner Loop** | 非 owner 线程不操作 JSContext/JSValue/GC；原生事件通过 POD/唤醒交接 |
| ADR-05 | **Redis ae 哲学、小型自主 Reactor** | 通用 poll 起步，OS 专用优化先探测/差分，不移植完整 libuv |
| ADR-06 | **WorkerPool 有界、只做阻塞/计算任务** | FS/DNS/KDF/压缩等；不提供每 socket 一线程长期工作模式 |
| ADR-07 | **Node API 分级 + Oracle 差分** | 机器可读能力矩阵，`verified` 必须有实测证据 |
| ADR-08 | **ESM/CJS 共用解析基础** | package exports/imports、QJB/FS 路径、加载缓存事实源唯一 |
| ADR-09 | **QJB v1 自定义、严格解析** | 不继承旧文件格式 ABI，禁止冒充安全沙箱 |
| ADR-10 | **应用包只读/写外部状态目录** | ZipOS/QJB/APE 的资源安全和路径语义一致 |
| ADR-11 | **大文件原始 APE STORE+对齐 mmap** | 不将整个大资源/QJB 匿名内存复制，Windows >4GB 明示旁路资源包 |
| ADR-12 | **Service Manager = Qaplet Application** | 第一方应用 `apps/service-manager`，JS 为业务主体；Kernel 不内嵌服务语义 |
| ADR-13 | **Native Service Host 是应用私有的最小桥** | Windows SCM 回调、状态、Job、队列、watchdog 用 C；回调线程不执行 JS |
| ADR-14 | **默认安全不降级** | TLS、密钥、路径、权限、进程树与 OS 安装操作严格控制 |
| ADR-15 | **构建复现/发布证据为发布合同** | Nightly hash、供应链、SBOM、同一 APE 宿主实跑与签名检查 |
| ADR-16 | **先跨平台正确，再性能优化** | 高性能 backend 保留经验证的公共回退与负向测试 |
| ADR-17 | **通用 C Embed SDK 为任意应用服务** | native module 注册、事件投递、生命周期与 ABI，禁止 service-specific 函数进入 SDK |
| ADR-18 | **平台与示范应用独立交付/门禁** | Runtime/SDK、Service Manager App 有独立 release 和 capabilities；SCM 失败不阻断 Core |
| ADR-19 | **Managed Process 原生强边界、JS 策略** | Windows Job/Unix group 的句柄和强杀归应用 C，健康/重启/日志由 JS 决定 |
| ADR-20 | **Linux 优先 systemd Direct** | 仅生成 unit 与执行管理；确需 JS 持续监管时选 Wrapper，避免双层 supervisor |
| ADR-21 | **应用 Host 构建是通用扩展点** | P0 cosmocc+SDK 手动链接，P1 泛化到任意 `qaplet compile --host`；不发明服务专用编译器 |

### 3.1 职责与不可倒置的边界

- `Qaplet Core` 只提供 JS/OS 通用能力，SDK 只提供宿主生命周期、模块/事件扩展点；JS 服务业务属于 `apps/service-manager/src`。
- Windows SCM/Job Object 的原生代码属于 `apps/service-manager/native`，不是默认 `qaplet`/`libqaplet-embed.a`。应用 C host 仍可通过 Cosmopolitan `IsWindows()` 条件逻辑封装跨 OS APE。
- 管理 CLI、YAML/JSON/.env、健康/日志/恢复策略和 systemd adapter 默认用 JS，**不能借“薄 Native Host”为由将这些回收到 C**。
- 系统级控制状态以 SCM/systemd 为真值；App JS 的业务健康/readiness 是单独的事实，不冒充系统状态。
- `qaplet-svc` 的 SDK 静态链接与 Native Bridge 属正常 **App 构建用例**；需要单独的 Build Evidence 和应用级测试，而非通过特殊默认内核 profile 发布。

---

## 4. Qaplet 平台总体技术架构与运行数据流

```text
                        JS / CJS / ESM / QJB / Portable APE
                                     |
                     Qaplet Node/Web Compatibility APIs
              Buffer | process | fs | stream | net | http | tty | ...
                                     |
                           Qaplet Module Runtime
           Resolver | ESM loader | CJS loader | URL/FS provider | cache
                                     |
       +------------------------- Qaplet Host ---------------------------+
       |                   owner thread / logical loop                  |
       |  QapletScheduler -> jobs / nextTick / microtasks / timers      |
       |         |                     |                   |             |
       |  QapletReactor <--> CompletionQueue <--> QapletWorkPool         |
       |         |             |                    |                    |
       |   TCP/UDP/TTY*      wakeup             FS/DNS/KDF/crypto         |
       |         |                                                       |
       |  Transport (TCP/TLS) ---> HTTP/1.1 / SSE / WebSocket / H2       |
       |  QapletResourceManager: ZIP/ZIP64/QJB/APE mmap/security/cache  |
       |  QapletProcessHost: general child/signal/stdio/exit            |
       +---------------------------+-----------------------------------+
                                   |
                     Cosmopolitan libc / APE / ZipOS
                                   |
                  Linux | Windows | macOS | *BSD (gated)
```

`TTY*`：Windows Console/pipe/HANDLE 不必和 socket 共享 `poll()` backend；通过 OS adapter、waker、专用 I/O 桥统一把 completion 送到 owner Loop，不把每个 socket 变成独占线程。

**通用 App 关系**：`qaplet` CLI 通过这套 Host 运行 JS；独立 APE 经 App Compiler 把同一 Host 和 JS/资源打包。具有原生 OS 特殊入口的 App（如服务管理器）改为“**应用 C Host → Embed SDK → 同一 Qaplet Host**”，它仍是应用，不是被内核反向调用的插件。完整服务管理应用架构单列 §13。

### 4.1 线程与资源归属

| 实体 | 所有者 | 允许操作 | 禁止事项 |
|---|---|---|---|
| `QapletRuntime` | 单独 JS owner thread | JSContext/JSValue/Promise/job/GC | 其它线程直接操作 QuickJS |
| Reactor | owner loop 或受控 I/O backend | FD/Socket readiness、waker、completion | 执行任意业务 JS 回调 |
| WorkerPool | 固定有界原生 workers | 阻塞 FS/DNS/压缩/KDF | 长期独占每 socket；调用 JS_* |
| QuickJS Worker | 独立 Runtime + owner thread | 自己的 JS 和消息 | 共享裸 JSContext/GC 堆 |
| Embedded App C Host | 应用自己的原生线程/事件回调 | SDK 通用事件接口、私有 native module | 回调中跨线程执行 JS、让 Core 知道应用业务 |
| ResourceManager | mount/index/mmap 引用计数 | 有界视图、读取/关闭 | 悬空映射、无校验直接解压 |

### 4.2 通用运行时生命周期

`CREATED -> INITIALIZING -> RUNNING -> QUIESCING -> CANCELLING -> DRAINING -> CLOSED`，失败进入 `FAILED` 并幂等收尾。`QUIESCING` 后停止接收普通新异步工作；owner 线程执行最终 Promise/callback 结算，native 资源有明确所有权；WorkPool 完成事件不能访问已销毁 Context。SDK 提供通用取消/中断入口，**不保证所有同步 native 调用都可抢占**。强制服务停止属于服务管理应用私有 Host 的 watchdog/进程边界，而不是通用 Runtime 可以安全强杀任意 JS 线程的承诺。

---

## 5. 工程目录结构建议

```text
qaplet/
├── README.md
├── LICENSE
├── AGENTS.md
├── Makefile
├── CMakeLists.txt                   # 可选开发辅助，正式以 cosmocc/Make APE 构建为准
├── toolchains/{qaplet.lock.json,bootstrap.sh,verify-toolchain.sh}
├── vendor/{quickjs,third_party}/
├── patches/{quickjs,cosmopolitan}/
├── src/
│   ├── cli/                        # run / pack / compile / doctor / inspect
│   ├── engine/                     # QuickJS context/module/worker
│   ├── host/
│   │   ├── loop/
│   │   ├── reactor/
│   │   ├── workpool/
│   │   ├── process/                # 通用 spawn/pipes/wait/signals，非 SCM
│   │   ├── tty/
│   │   └── resource/               # QJB/APE/ZIP/mmap
│   ├── embed/                      # 通用 C SDK runtime lifecycle/host module/POD event
│   ├── bindings/                   # 通用 Node/Web native primitives
│   ├── transport/
│   ├── protocols/
│   ├── modules/{resolver,cjs,esm,json}/
│   └── js/{node,web,internal}/
├── include/qaplet/                # 稳定 Embed SDK 头文件（无 SCM/service 模块）
├── packages/
│   ├── qjb-pack/
│   └── app-compiler/              # 未来通用 custom native-host target
├── apps/                          # 一等公民：用 Qaplet 开发的第一方应用
│   └── service-manager/           # **主应用**，JS 为主体，详见 §13.3
│       ├── Makefile
│       ├── qaplet.app.json
│       ├── native/host.json         # 通用嵌入式应用的构建事实源
│       ├── src/                   # cli/config/manager/supervision/backends/logging
│       ├── native/                # 最薄的 SCM/Job/event/watchdog Host Bridge
│       ├── resources/             # JSON Schema/unit templates
│       ├── examples/
│       ├── tests/
│       └── docs/
├── tests/
│   ├── native/
│   ├── sdk/                       # 任何嵌入者都须通过的 ABI/线程合同
│   ├── contracts/
│   ├── ttys/
│   ├── bundles/
│   ├── apps/                      # 普通 JS App 与 service-manager 综合应用验证
│   ├── soak/
│   └── portability/
├── compat/{matrix.schema.json,matrix.json,test-evidence.json}
├── examples/{embed-c-host,cli,standalone-qjb}/
├── docs/{adr,specifications,security,operations,delivery}/
└── .github/workflows/
```

**依赖边界**：`apps/service-manager/src -> 通用 JS APIs`，`apps/service-manager/native -> include/qaplet + libqaplet-embed.a`，`libqaplet-embed.a -> Core`。禁止 `Core -> apps/service-manager`；禁止核心 Runtime 的 link map 出现 SCM/service symbols。服务应用的 `native/` 只暴露给**自己**的私有 JS 模块，安全控制与 ABI 测试在应用内做。可以将该应用未来拆成独立仓库，但从开发者体验看必须仍是一款普通 Qaplet App。

运行时本身不能依赖目标主机安装 Python/Node/npm；这些仅可作为开发或构建工具（并应保持可选）。源码 lock、工具链 lock、manifest 和能力矩阵有可执行的单一事实来源，Markdown 不产生相互冲突的双份声明。

---

## 6. 从零实现 QuickJS Engine Host

### 6.1 QuickJS 主干接入策略

- 直接以 Bellard `master` 锁定提交编译源文件；不使用旧版 QuickJS fork 的二进制或 patch stack。
- 保留上游源代码树只读；构建私有补丁层及 patch-hash 报告；默认禁止随意修改 `quickjs.c` 等核心引擎文件。
- 原生 API：`JS_NewRuntime`、`JS_NewContext`、`JS_SetModuleLoaderFunc`、`JS_ExecutePendingJob`、`JS_SetMemoryLimit`、`JS_SetMaxStackSize`、`JS_SetInterruptHandler`、`JS_NewPromiseCapability`，以相应头文件当期实际签名验收；如主干有变更，先更新边界探针。
- Runtime Context 创建要经统一 factory，确保主入口、Worker、qjsc 风格独立 APE、SDK 的内建模块注册、权限、资源 provider、异常回传相同。
- 为模块加载实现 `normalize/load` 钩子；`qaplet:`/`qjb:`/`node:`/`file:` 等 Scheme 一律显式规则，不将任意字符串隐式交给 OS 访问。
- `JSValue` 的 `dup/free` 由 owner thread 管理；native 资源使用 opaque 句柄和引用计数；强制上下文关闭后的异步任务不再访问 JS 内存。
- 运行时预算：内存上限（默认可调）、最大栈、脚本执行中断（CPU time budget）、native handles、任务数、文件大小、计时器数量、Worker 数量。同步 CPU 长循环必须有中断机制；异步任务取消属于另一套显式 API。
- QuickJS 字节码不是跨引擎版本稳定格式，也不是可信输入格式。只允许同一已锁引擎构建/验证的受信任字节码；来自不可信网络或 npm 包时默认以源文本编译，或用强制验证的可信编译清单。
- ES2025/Temporal/Intl 能力依照当前锁定主干和 test262 结果声明；不得从“最新主干”推断所有提案都已经稳定实现。

### 6.2 SDK 与入口合同

| 入口 | 用户调用 | 验收要求 |
|---|---|---|
| 源码 | `qaplet run app.mjs` / `qaplet app.cjs` | argv/cwd/import.meta.main/exit code 正确 |
| QJB | `qaplet run app.qjb -- a b` | 无需解压、模块路径与资产可读、按 mount 生命周期关闭 |
| 独立 App APE | `./app.com -- a b` / `app.exe -- a b` | 无 CLI 选项冲突、载荷指纹一致、路径身份明确 |
| Worker | `new Worker(...)`（声明受支持子集） | Runtime 独立、错误/exit/transfer/terminate 明确 |
| SDK | `qaplet_runtime_new/eval/run/close` | 重复创建销毁、Opaque 不冲突、证据可复现 |
| 第一方 Service Manager App（SDK 使用者） | `qaplet-svc host --name x --console` | JS 应用 + 私有 C Host 使用同一个 SDK，SCM/systemd 与子进程状态不混淆 |

C Embed SDK 的拟议通用入口为 `qaplet_runtime_create`、`register_host_module`、`run_entry/run`、`post_event(POD)`（thread-safe）、`request_stop`/`request_interrupt`（cooperative）、`destroy`，以及资源 Provider 和 Log Sink；正式函数签名由 M1/M2 真实 ABI 探针冻结。SDK 只提供通用有界事件队列、waker 与停止请求，不包含 SCM/Job Object/配置、重启策略。**应用私有 Native Host** 负责不被普通队列满丢失的 STOP latch 和独立 watchdog；versioned `struct_size/abi_version`，外部 ABI 不以裸 `JSContext*` 为强制依赖。见 §13.4。

### 6.3 Node vs Web API 层的实现原则

**JS 优先，C 辅助**：例如 `node:fs` 的 callback/Promise API、错误归一化、选项处理留给 JS；native 仅提供 `open/read/write/stat` 等可验证原语。网络对象、背压、HTTP Agent、TLS 包装等同理。该分层减少跨平台 C 代码量，提高 Node 差分改进的速度。

---

## 7. Host Kernel：Redis ae 哲学的事件模型

### 7.1 “一个 loop 就是一个 loop”的精确定义

Loop 负责时序、调度、存活和退出，不负责 HTTP/FS/DNS 业务语义。Reactor 只报告 OS fd/handle 的 readiness；TimerHeap 维护单调时钟 deadline；WorkerPool 处理阻塞任务；CompletionQueue 统一把完成事件交给 Loop。不能以给所有 API 包 Promise 的方式掩盖实际阻塞。

```text
                    QapletLoop（每个 JSRuntime 唯一逻辑调度中心）
        +-------------+----------------+--------------+-------------+
        |  JS Jobs    |  Ready Queue   |  Timer Heap  |  Ref/Exit   |
        +------+------+--------+-------+------+-------+-------------+
               |               |              |
          JS owner        CompletionQueue     next_deadline
                               ^                |
                         Wake/Channel          v
                              / \           Reactor.poll(timeout)
                         I/O ready /\ Worker done
                                   /            \
                              socket/TTY*     WorkPool
                                             FS/DNS/KDF
```

**关键接口（C 伪合同）**：

```c
// Handles are generation-tagged; no raw JS callbacks in reactor records.
typedef struct QapletLoop QapletLoop;
typedef struct QapletReactor QapletReactor;
typedef struct QapletWorkItem QapletWorkItem;

typedef enum {
    QAPLET_IO_READ  = 1,
    QAPLET_IO_WRITE = 2,
    QAPLET_IO_ERR   = 4
} QapletIoMask;

int  qaplet_loop_run(QapletLoop *loop);
int  qaplet_loop_run_once(QapletLoop *loop, int64_t max_wait_ms);
void qaplet_loop_request_stop(QapletLoop *loop);
void qaplet_loop_wake(QapletLoop *loop);

int qaplet_reactor_watch(QapletReactor *, int fd, uint32_t mask, uint64_t token);
int qaplet_reactor_modify(QapletReactor *, uint64_t token, uint32_t mask);
int qaplet_reactor_unwatch(QapletReactor *, uint64_t token);

uint64_t qaplet_timer_add(QapletLoop *, uint64_t deadline_ns, uint64_t repeat_ns);
int      qaplet_timer_cancel(QapletLoop *, uint64_t timer_id);
int      qaplet_work_submit(QapletLoop *, QapletWorkItem *);
int      qaplet_work_cancel(QapletLoop *, uint64_t work_id);
```

`token` 必须包含 slot + generation，防止 fd 被关闭后由 OS 复用，同一 fd 上错误的迟到事件被交给新对象。watch/remove/close 要有顺序、幂等和回调竞态测试。

### 7.2 运行步骤（语义伪码）

```c
while (!host->stopping) {
    drain_next_tick_queue_with_budget(host);
    run_quickjs_pending_jobs_with_budget(host);
    dispatch_completions_with_budget(host);
    fire_due_monotonic_timers_with_budget(host);
    run_safe_checkpoints(host);

    if (host_can_exit(host)) {
        if (!emit_before_exit_and_recheck(host))
            break;
    }

    int timeout_ms = compute_min_timeout(host);  // nearest timer / stop / max idle
    reactor_wait_or_wake(host, timeout_ms);       // never hold JS heap locks here
}
close_all_resources_in_order(host);
```

该伪码**不是** Node 事件循环阶段的逐步复制。`process.nextTick`、Promise microtasks、timer、I/O callback、`setImmediate` 的时序以锁定 Node Oracle 的差分结果确定；为防止无限 nextTick/microtask 导致 I/O 饥饿，设执行预算并保留可审计的行为差异，不得无说明改变 Promise 微任务的 ECMAScript 语义。

### 7.3 Reactor backend 选择

- **P0：Cosmopolitan `poll()`**，优先覆盖所有目标 OS；启动时验证 socket readiness、关闭、错误、HUP、非阻塞 connect、半关闭和 wake。禁用固定 `poll(..., 10ms)` 空转。
- **P1：Linux epoll**：通过 Cosmo 可用 syscall / wrapper 运行时选择，与 `poll` 同样的状态机；没有并发基准优势时不默认启用。
- **P1：Darwin/BSD kqueue**：作为可选优化；同样要求运行时探测和负向用例。
- **Windows**：socket 优先依托 Cosmo 现有 poll/WSAPoll 路径验证；Console、子进程 HANDLE、pipe 可能需要专门等待/线程桥，不能假定所有 OS fd 与 Winsock socket 混合 `poll()`；IOCP/wepoll 仅在具体性能瓶颈及 APE 可用性已证明后评估。
- 不准把 libuv/libevent/epoll backend 变成第二份从业务层可见的宿主 ABI。所有 backend 输出统一 `QapletIoCompletion`。

**首选线程形态**：一 Runtime 一个 JS owner thread；初期优先验证同线程 Reactor 轮询的可行性，再按 Windows console/pipe 的实际限制决定是否专用 reactor thread。无论线程配置如何，一个连接绝不独占一个 OS thread。

### 7.4 Timer 与 Ref/Unref

- 二叉最小堆，单调时钟，O(log N) 增删，懒删除要有上限以防堆膨胀；ID + generation 防复用错误。
- `setTimeout`、`setInterval`、`setImmediate`、取消、`ref/unref`、`beforeExit` 与 `exit` 都要形成独立的存活合同。
- 在睡眠前计算最近 timer/deadline；调用系统等待而不是忙循环。系统时间回拨不影响单调计时；时钟跳跃与 suspend/resume 后要有恢复规则。
- 长时间运行时避免无限排队：每 tick 处理有界 jobs/completions，单连接写入高水位触发背压，WorkerPool 队列上限可配置，超限返回结构化 `ERR_RESOURCE_LIMIT`。

### 7.5 WorkerPool / Blocking bridge

- 负责 regular file I/O、`getaddrinfo()`、PBKDF2/KDF、大块压缩、可阻塞的证书/图片处理等；不把 TCP/TLS I/O 长期放在一线程一连接模型。
- 初始默认 2～4 条线程，允许受限配置；有界 `pending_count`/`pending_bytes`/并发控制，任务拥有取消和上下文销毁合同。
- 非 owner 线程只处理 POD 数据、原生 byte buffer、错误码；完成后通过线程安全 queue + `wake()` 让 owner thread 创建/销毁 JSValue，解析 Promise。
- 取消区分 `queued`（可直接取消）、`running`（请求取消但 OS syscall 可能不可中断）、`completed`（拒绝重复完成）。所有任务至多一次完成回调。

### 7.6 Transport、TLS、HTTP 与 FS 的归属边界

- `TCP/UDP/pipe` 归 native transport；`readable/writable` 推进状态机。
- `TLS` 归非阻塞 transport codec，`WANT_READ/WANT_WRITE` 调整 Reactor watch；握手、关闭、重连状态不得创建额外独占长寿命 socket thread。
- `HTTP/1.1`、WebSocket、SSE 都是 transport 消费者，HTTP parser/serializer 不直接调用 OS 等待。
- `FS` 通过同步 `open/read/write` 或异步 WorkPool，普通磁盘文件不强塞进 epoll readiness。
- `DNS lookup` P0 WorkPool + getaddrinfo；P2 可选 c-ares 通过 UDP fd 与 timer 接入同一个 Reactor。
- `child_process`：spawn/wait/stdio/EOF 结果进入 CompletionQueue；不要把 exec 创建逻辑写入 QapletLoop。

### 7.7 Qaplet 运行时可观测性

内核原生维护：`loop_tick`、`lag_ms_p50/p95/p99`、`ready_depth`、`completion_depth`、`worker_active/queued`、`open_handles`、`timer_count`、`socket_count`、`bytes_in/out`、`spurious_wakeups`、`dropped_events`、`gc_count/pause_ms`、`memory_rss/virtual`（受 OS 支持限制）。CLI `qaplet doctor` 与 `qaplet inspect --runtime` 输出 JSON，不依赖 HTTP 管理面；外部 SDK host 可通过通用 metrics API 读取，并自行在受保护的本地诊断接口输出 service 维度指标，**Kernel 不收录 SCM/service 特有指标**。生产默认不输出秘钥/请求 body/环境变量全量值。

---

## 8. Node / Web API 能力矩阵（参考 LLRT）

### 8.1 能力声明模型

所有 API 的支持必须有**机器可读事实源** `compat/matrix.json`，字段含 `api`、`profile`、`support` (`verified|partial|experimental|unsupported`)、`since`、`source_tests`、`node_oracle`、`platforms`、`known_differences`、`security_notes`、`owner`、`evidence_sha256`。不能先标 `verified` 再补测试；TypeScript 声明和 `qaplet capabilities --json` 必须从同一事实源产生或校验。

**Node Oracle**：首版以真实的 Node.js `v24.21.0` 已声明合同为基准；通过独立工具锁定其下载包 SHA，不凭经验猜测差异。未来提升基线采用显式 major/minor 合同迁移。要求 Callback/Promise、异常类型、`code/errno/syscall/path`、事件顺序、Buffer 边界、stdio 数据行为、Path/UNC 语义等有差分用例。

### 8.2 v1 模块矩阵（“目标状态”，不是当前实现声明）

| 模块/能力 | v1 目标 | 优先级 | 实现建议与验收说明 |
|---|---|---|---|
| ESM、动态 import、import.meta、TLA | verified | P0 | QuickJS 原生 ESM + 宿主 resolver，URL identity 正确 |
| CJS、require/module.exports/cache | verified 子集 | P0 | Module wrapper、循环依赖、`require.resolve`、JSON CJS |
| `node:module` / `exports/imports` | partial→verified 子集 | P0 | package.json、Node 条件导出、node_modules 查找 |
| `node:buffer` | verified 子集 | P0 | Uint8Array 关系、编码、slice/copy/int/float 读写 |
| `node:process` / `node:os` | verified 子集 | P0 | argv/env/cwd/exitCode/nextTick/platform/arch |
| `node:fs`, `node:fs/promises` | verified 核心集 | P0 | 文件/目录、stat/lstat/junction/UNC/stream、异步非阻塞 |
| `node:path`、`node:url` | verified 核心集 | P0 | win32/posix、file URL、跨 OS 保持 Node 语义 |
| `node:events`、`node:stream` | verified 核心集 | P0 | backpressure、pipeline、destroy、AsyncIterator |
| `node:timers`, `/promises` | verified 核心集 | P0 | event order、ref/unref、AbortSignal |
| `node:tty`, `node:readline`, `/promises` | verified TUI 核心集 | P0 | raw、resize、Unicode、keypress、PTY/ConPTY |
| `node:child_process` | verified 核心集 | P0 | spawn/execFile、shell=false、exit/signal/stdout/stderr；`fork/IPC` P2 |
| `node:dns`, `/promises` | verified lookup 子集 | P0 | getaddrinfo v4/v6 与错误；`resolve*` 后续 |
| `node:net` | verified 客户端+服务端子集 | P1 | TCP connect/listen、backpressure、half-close、timeout |
| `node:tls` | verified 安全客户端子集 | P1 | CA、SNI、hostname、mTLS 分级、TLS1.3 专题 |
| `node:http`, `node:https` | verified HTTP/1.1 子集 | P1 | client/server、chunked、Agent、SSE、Abort |
| 全局 `fetch`、Request/Response/Headers、URL | partial→verified | P1 | WPT subset、stream 取消、redirect、代理策略 |
| WebSocket | verified Client；Server 分级 | P1 | RFC6455、fragmentation、control frames、升级、close |
| `node:crypto` / Web Crypto | verified 精选算法 | P1 | AES-GCM、SHA/HMAC、KDF、Ed25519，正式安全审计 |
| `node:zlib`、压缩扩展 | verified 基础 | P1 | gzip/deflate，zstd/lz4 可选 Full profile |
| `worker_threads` | experimental/unsupported | P2 | 不把 QuickJS Worker 假充 Node worker_threads |
| SQLite、WASM/WAMR | experimental | P2 | 作为可选 profile，严格体积/安全预算 |
| HTTP/2、ALPN、TLS1.3 | planned | P2 | 先安全 backend，再 nghttp2；不阻断 v1.0 |
| HTTP/3 / QUIC、Inspector、完整 V8 API | unsupported | Future | 不公开假承诺 |
| native `.node`、任意 `dlopen` | unsupported | Future | 单 APE/跨 OS ABI 不一致，不做隐式解压执行 |

### 8.3 核心设计：Node 模块解析器

文件提供器和 package resolver 共享一个模块身份规则：

1. 识别 `node:` 内建、`qaplet:` 特有、相对/绝对文件、`file:` URL、裸包名、`#imports`、QJB 虚拟路径。
2. 按上下文和 `package.json` 的 `type` 决定 `.js` 是 CJS/ESM；`.cjs`、`.mjs` 明确，JSON 和 package exports 单独处理。
3. 按 Node 约定解析 `exports`/`imports`/`main`（冲突和自引用要测试），查找 `node_modules`、`node`/`import`/`require` 条件，但不虚构不支持的 Node loader hook。
4. 正确处理循环依赖：CJS module 对象先入缓存再运行；ESM module record 链接和求值按 QuickJS 语义；错误时缓存淘汰规则差分测试。
5. 包内映射不得退回不受控宿主 cwd，也不得允许 `..` 跨 mount；host `require` 与 bundle `require` 指向显式的 provider。
6. 内建模块 C/JS export 名称、default/named export、模块 identity 和 ES/CJS interop 通过 Node Oracle，而不是名字相同就算支持。

CJS 核心伪形态：

```js
function executeCommonJS(source, filename, module, localRequire) {
  // 仅示意：生产实现还要有 scope、cache、source-map、循环依赖与异常处理。
  const wrapped = compileCommonJSWrapper(source, filename);
  wrapped(module.exports, localRequire, module, filename, dirname(filename));
  return module.exports;
}
```

**禁止**把单个全局 `require` 注入所有 ESM realm、禁止以“把 CJS 文本替换成 import”作为通用方案、禁止从远程 npm 下载任意原生可执行资源后静默启动。

### 8.4 TUI 兼容标杆

- **Clack**：P0。检查 `readline.createInterface({terminal:true,output:undefined})`、`rl.terminal` 可变行为、keypress/resize/raw 回收、Unicode 宽度；原版 ESM 依赖或锁定的离线 bundle 必须可运行。
- **Terminal-Kit**：P1。着重 `require.cache`、动态 `require('./termconfig/' + name)`、JSON、Buffer BE/int、ScreenBuffer、TTY/stdin/`execSync`/SIGWINCH。
- **Ink**：P2 技术预研。React/Reconciler 和 Yoga WASM 是额外阻断；考虑通过可选 WAMR 运行上游 yoga-layout，或原生 Yoga+一致 API 适配；不为 Ink 把整个 v1 变成 WebAssembly 优先运行时。
- 终端测试至少覆盖 ASCII/中文/emoji/宽字形、两列布局、颜色、光标、Alt Screen、输入取消、resize、异常/信号退出后完整恢复。

---

## 9. 网络与安全协议方案

### 9.1 传输抽象

`QapletSocket` 管理 `fd`、状态、read/write ring buffer、高低水位、error、read_eof/write_eof、引用计数、超时/取消。`QapletTlsTransport` 在其上运行非阻塞 TLS，暴露相同的 `read/write/flush/shutdown`。任何连接不得新建独占式长期 polling pthread；需要 backpressure 和公平的每轮 byte/callback budget。

### 9.2 HTTP/Fetch/WebSocket

- v1.0 最小网络闭环：DNS、TCP、TLS 安全客户端、HTTP/1.1 request/response、SSE、HTTP Server（限定的 Node 风格 API）、超时/取消/重定向的声明子集。
- HTTP parser 选定维护良好的小型 C parser 或在已有 Cosmopolitan parser 上构建，先做 differential fuzz，不自研不必要的 HPACK/HTTP2 framing。
- WebSocket 可集成纯协议层 `wslay`（MIT），由自己完成握手、TCP/TLS、Origin/证书/代理策略、frame feed、ping/pong/close 和背压。txiki.js 使用 libwebsockets 及自有 event loop 集成作为架构参考，并不强制引入完整 libwebsockets。
- HTTP/2 后续独立工作包使用 `nghttp2`，先实现 ALPN、流控、HPACK、取消和服务端证据；HTTP/3/QUIC 不进入 v1.0 发布门禁。

### 9.3 TLS 安全决策

Cosmopolitan 当前源码的 `third_party/mbedtls/README.cosmo` 仍标识**经大量定制的 Mbed TLS 2.26.0**，不能因为换成 Nightly 就宣称自动获得 TLS 1.3 或所有上游 CVE 修复。

- TLS v1.0 第一安全基线：强制可信 CA/证书链/hostname/SNI 校验、时间验证、弱算法拒绝；禁止“开发用途 insecureSkipVerify”成为默认；私人 CA 和可信根更新均可审计。
- 单独建立 `tls-provider` 私有静态闭包：可在经补丁和审计的 Cosmo TLS1.2 基线和更新的现代 TLS 库之间进行**唯一后端**评估；禁止链接两个不受控的 `mbedtls_*` 符号集。
- TLS1.3 是独立里程碑：Mbed TLS 4.x/TF-PSA-Crypto 的 Cosmopolitan 适配，或其他候选需通过 X509、SNI、ALPN、安全公告、互操作和 ABI 验收后才启用。
- 建立进程共享只读 CA trust store；连接不得默认每个分配巨大固定 arena。内存峰值以并发实测作为准入，不以“连接数限制改大”冒充扩展能力。
- 建立定期安全公告检查的显式升级工作流；SSL 失败绝不自动回退明文 HTTP。

---

## 10. QJB：应用格式、虚拟模块系统与生命周期

### 10.1 从零定义 QJB v1

QJB 保留 `*.qjb` 后缀和“便携 JavaScript 应用包”的含义，但新建完整格式规范、错误码、测试和迁移工具；**不与历史 quickjs-cosmo QJB v1 默认二进制兼容**。初版目的：在同一个运行时内执行受信任的 ESM/CJS/JSON 依赖图，无需先解压到磁盘，并能引用资源。

建议容器采用可审计 ZIP 子集（STORE/DEFLATE、标准 UTF-8 名、确定性排序），配置 manifest 固定路径 `.qaplet/app.json`；避免允许符号链接、特殊文件、绝对路径、路径逃逸、文件名冲突和 ZIP 多盘。文件/中央目录/本地头一致性及 CRC 均需验证。**CRC 检查完整性损坏，不是安全真实性证明**。

```text
app.qjb                       ZIP 数据容器
├── .qaplet/
│   ├── app.json               入口/权限/能力契约/包元数据
│   ├── modules.idx            规范依赖映射（可选）
│   ├── resources.json         成员大小、sha256、compression、用途
│   └── build-info.json        工具链/编译器/Node Oracle/时间戳策略
├── app/main.mjs
├── app/package.json
├── app/node_modules/...
└── app/assets/...
```

**manifest v1 草案**：

```json
{
  "schema": "qaplet.app.v1",
  "name": "sample-agent",
  "version": "1.0.0",
  "entry": "app/main.mjs",
  "moduleType": "module",
  "engine": "quickjs",
  "nodeCompatibility": "node24-subset",
  "requirements": {
    "capabilities": ["node:fs", "node:process", "node:net"],
    "platforms": ["linux-x64", "windows-x64", "darwin-arm64"]
  },
  "assets": { "root": "app/assets" },
  "integrity": { "manifestVersion": 1, "digestAlgorithm": "sha256" }
}
```

`requirements.capabilities` 表示构建期最低运行能力，**不是自动授予的沙箱权限**。不允许 app manifest 以配置文件方式关闭 TLS 验证或绕过安全准入。

### 10.2 QJB 加载流程

1. 打开 package，读取大小及 ZIP 目录，验证格式、成员数、尺寸、文件名和碰撞、entry 合法性与风险上限；**不得先整个包读入匿名内存**。
2. 建立 mmap/按需 pread 视图及紧凑索引；只有被访问的成员才读入/解压；对压缩成员使用带配额的 cache（LRU/引用计数），对 STORE 成员优先直接借用映射内容。
3. 解析 `.qaplet/app.json`，校验内容清单（可按信任等级采取启动关键成员预验证 + 其余成员惰性校验；签名验证另行要求）；不把 `CRC32` 当可信签名。
4. 建立只读虚拟根，主模块 URL 使用 `qjb://<mount-id>/app/main.mjs`；外部 QJB 中 `import.meta.url`/相对资源 API 一致；`require()`、ESM、Worker 共享同一 mount 目录索引，不共享 JSRuntime GC/模块对象。
5. 资源读取为 `read/stream/map` 三种不同内存语义；只有 `read` 才必须返回新的 JS Buffer；外部持有者关闭时映射引用计数回收，悬空 view 必须检测。
6. 应用 `fs` 必须支持已声明的只读 QJB 路径提供器，同时保留宿主真实文件路径；写操作若指向只读 mount 返回 `EROFS`，不能悄悄落到 cwd。

**包内路径规则**：规范化 UTF-8、拒绝绝对根逃逸、`..` 越界、NUL、不同编码大小写别名、非法 ZIP extra、重叠成员、ZIP bomb、畸形 ZIP64、重复记录、权限位冲突。Windows 允许的 UNC/驱动器宿主路径不与 `qjb://` 混淆。`qaplet://app/...` 保留给内嵌 APE 应用的稳定逻辑 scheme，避免将 `/zip/...` 等主机路径直接泄漏给 JS 业务。

### 10.3 QJB 配额分层

- **P0 默认安全配额**：建议最多 8,192 成员、单文本/JS 模块 16 MiB、全部可解压脚本/配置等不超过 256 MiB、单次解压内存 32 MiB、mount 数量默认 16（均需实现前以资源测试调整，非已实现数据）。超限明确 `ERR_QJB_LIMIT`。
- **大型资源**：不纳入“全部解压 JS 模块”的配额，而进入 `QapletLargeAsset` 策略，要求 STORE + 对齐 + 受控 mapping；内存映射本身也有每进程/每应用配额。
- **ZIP64**：作为大资源专题开启。先验证当前 Cosmopolitan ZIP 解析路径对 >4GB 索引与 EoCD 的行为，必要时实现 Qaplet 私有 ZIP64 parser；不因 ZIP64 字段存在就宣称 Windows >4GB `.exe` 可运行。
- **压缩成员缓存**：全局/每应用双预算（bytes + entries），LRU 淘汰必须考虑 JS 借用视图的生存期，避免把 cache 中内存指针交给 JS 后立即释放。
- **格式升级**：新 major 格式应有显式 `schema` 版本，不通过魔法值碰运气读取旧格式。

### 10.4 QJB 构建与升级

- `qaplet pack ./app -o app.qjb`：确定性目录扫描、依赖解析、源文件/资源 manifest、时间戳归一化、版权/许可证收集、原子输出与中断清理。
- `qaplet inspect app.qjb --json`：展示入口、依赖、资源大小、签名/完整性状态、目标 Node 能力、真实解压峰值预算。
- `qaplet verify app.qjb`：校验 ZIP headers、可达依赖、SHA-256、签名/可信根（如配置）、恶意嵌套路径与无效配置。
- `qaplet migrate-qjb <old>`：未来可选；不阻断 v1，全新格式要有误用旧文件时的清晰错误。

---

## 11. 从精选 Node 项目编译出单一 APE 应用

### 11.1 正式产品命令

```bash
qaplet --version
qaplet run app.mjs -- --help
qaplet run app.cjs -- --help
qaplet run ./package-dir -- --help
qaplet run app.qjb -- --help
qaplet pack ./my-app -o my-app.qjb
qaplet compile ./my-app --output my-app.com --profile base
qaplet inspect my-app.com --json
qaplet verify my-app.com
qaplet capabilities --json
qaplet doctor --json
# 服务管理器是独立编译的 Qaplet Application：qaplet-svc；不是 qaplet 子命令
qaplet-svc validate --name my-service
qaplet-svc install --name my-service
qaplet-svc status my-service
```

以上是**拟议目标 CLI**，不宣称全新 Qaplet 仓库已经实现。核心命令仍为 `qaplet`；`qaplet-svc` 是由 Qaplet 编译的一款**独立 JS Application**，它拥有自己的 CLI/配置/版本，可由 `qaplet run` 开发 JS-only 子命令，或编译为带最小 Native Host 的 APE。允许 `qaplet file.js` 作为 `run` 简写，但必须守住 runtime 参数与应用参数边界。

### 11.2 `compile` 工作流与阻断条件

```text
输入 Node 工程
   ↓
读取 package.json、锁文件、入口和依赖图
   ↓
Node resolver 判定 ESM/CJS、内建模块、JSON、资源
   ↓
Node API 能力矩阵校验 + platform availability
   ↓
禁止项检查（.node / native DSO / spawn 外部缺失程序 / 动态无法求解路径）
   ↓
构建 QJB 模块树（可选 minify，必须保持模块语义）
   ↓
用同一锁定 Nightly 工具链产生 Qaplet fat APE
   ↓
将应用内容写入 APE 的 ZIP 区域并生成一致索引
   ↓
复验 APE Loader + ZIP + manifest + 完整性 + 跨 host 实测
   ↓
输出 one-file app + capability report + provenance + checksums
```

对于动态 `require(someVariable)`、动态 glob、读取项目之外资源、运行期下载依赖、含 native addon 的 npm 包，使用显式 denylist/allowlist 与构建错误；可以声明 `--include` 白名单补充资源，但不能在本来缺失的情况下假报完整兼容。禁止把 `eval` 使用动态路径和程序执行行为隐式等价为静态可分析依赖图。

### 11.3 资源层应避免“ZIP 套 ZIP 全量读取”

**目标内嵌结构**：

```text
myapp.com / myapp.exe  （同一个 APE payload，可按平台重命名）
├── APE loader + native code slices
│   ├── QuickJS
│   ├── Qaplet Host Kernel
│   ├── Node API + Transport
│   └── C Embed SDK 的通用应用构建支持（无 SCM/ServiceHost）
└── APE ZIP
    ├── .qaplet/runtime.json
    ├── .qaplet/application.json
    ├── app/main.mjs
    ├── app/node_modules/...  # 精选纯 JS 依赖
    └── app/assets/...        # STORE/DEFLATE 按资源使用分开
```

`compile` 直接把可执行应用模块置于 APE ZIP 中，而不是再在 ZIP 里嵌套一个整体 `app.qjb` 并在运行时完整读取。仍然可以从同一 `QapletVfs` interface 调用已存在的 `QjbReader`/`ZipApeReader`，只要底层索引能复用而不复制整个归档。

### 11.4 单文件与平台安全签名是两个独立门禁

- 生成 APE 后的 ZIP/签名/PE 检验必须有固定处理顺序；不能假设 append 文件后 Windows Authenticode、macOS 公证、ZipOS EOCD 仍有效。**先做签名后可启动的实际 PoC，再承诺企业版单文件签名。**
- 构建输出可以包括可验证的离线附属 `manifest.json`、`.sig`、`.sha256`，不会改变“单一主执行文件”技术能力，但完全离线只交付主文件时的信任根必须有明确方案。
- 不同 OS 可能限制同一 APE 的直接运行：Windows 可执行文件体积、杀软/SmartScreen、macOS 签名和 Gatekeeper、Linux binfmt_misc 与 MZ 误判均需版本化证据；不保证绕开政策限制。

### 11.5 自定义 Native Host 也是一种普通 Qaplet 应用构建方式

`qaplet-svc` 不能成为唯一能选择自己 C 入口的特例。P0 先证明通过 `cosmocc + qaplet-sdk + 业务 JS/QJB + 应用 native/*.c` 可以链接出单文件 APE；P1 在通用 App Compiler 提供不带服务语义的 Host Template 构建配置，例如规划中的 `qaplet compile <app> --host native/host.json`。同一能力必须通过一个非服务示范 App 的交叉验证。此处 C 文件负责 App 的 **OS entry/特殊 handle**，不是新的 JavaScript runtime。

Native Host 的构建描述是**通用 App 构建输入**，例如（拟议格式，需在 M6 冻结）：

```json
{
  "schema": "qaplet.native-host.v1",
  "entry": "native/main.c",
  "sources": ["native/main.c", "native/module.c", "native/windows/scm.c", "native/process/managed.c"],
  "moduleSpecifiers": ["service-manager:native"],
  "sdkAbi": 1,
  "linkToolchain": "locked-cosmocc"
}
```

这只是 `apps/service-manager/native/host.json` 的示意，文件清单应覆盖实际 OS bridge/queue/watchdog 的所有实现并由已锁定工具链选择对应 native slice；**不应因跨平台文件差异而生成悄悄替换的普通 PE wrapper**。构建器应限制任意链接选项、声明允许的系统导入/外部静态库、锁定 provenance，并把文件摘要加入 App Build Evidence；绝不运行时从未知路径下载并 `dlopen` 原生模块。`qaplet` 默认编译流程不因此带入 SCM/Job 库。

---

## 12. llamafile 式大型资源与低内存加载

### 12.1 真实问题与目标

APE 几百 MB / 数 GB 不代表内存启动时同量增长。Cosmopolitan `ZipOS` 在初始化时可 `mmap` 整个可执行文件并建立 ZIP 索引；虚拟映射大小不是 RSS。未压缩 STORE 成员可直接引用映射区域；DEFLATE 成员在 `open()` 时可能完整解压该文件。**若对 `/zip/foo` 使用一般 `mmap`，Cosmo 虚拟文件层可能创建匿名映射并复制数据，不能保证零拷贝。**

llamafile 的做法是从原始可执行文件解析 ZIP 条目，定位未压缩 GGUF 数据的真实偏移，按操作系统 granularity 调整后对**原始文件 fd** 执行 `MAP_SHARED` mmap。`zipalign` 支持按给定边界对齐 STORE 成员，默认建议 64 KiB 对齐。

### 12.2 `QapletLargeAsset` API 草案

```c
// Source is an already validated APE file and verified ZIP entry.
typedef struct QapletMappedAsset QapletMappedAsset;
int  qaplet_asset_open(const char *bundle_id, const char *logical_path,
                       const QapletAssetPolicy *policy, QapletMappedAsset **out);
int  qaplet_asset_map(QapletMappedAsset *asset, uint64_t offset, size_t length,
                      QapletMappedSlice *out);
void qaplet_asset_unmap(QapletMappedSlice *view);
void qaplet_asset_close(QapletMappedAsset *asset);
```

- 使用严格 ZIP64/头/成员边界验证求得 `payload_offset`，检查所有 `(offset + length)` 的 64 位溢出、对齐后范围、真实文件大小、条目压缩方法、成员重复和重叠。
- `mmap` 在 Windows 区分 `page size` 与 `allocation granularity`，统一采用 `sysconf(_SC_GRANSIZE)` 决定 mmap offset 的对齐；实际返回 `base + skew`。
- 只读映射时不允许 JS 获得可写且可越界的内存指针；Native 映射对象的生命周期与所有引用它的 TypedArray/JS buffer/Worker lease 绑定；如不能可靠保持引用关系，则向 JS 提供显式 `read(range)` 安全拷贝 API，而不是伪零拷贝。
- CPU/GPU 访问统一记录 mapping/RSS/page fault/IO throughput；未来 GPU backend 如需设备缓冲区不承诺零复制。
- STORE 大资源可使用分段 mmap（例如每次几十至几百 MB 的窗口，按访问模式调整），避免一次申请极大连续用户虚拟地址；支持 `pread` 只读流式 fallback。
- 低频 JS/text/JSON 适度 DEFLATE；频繁随机访问、大型模型/索引/数据库建议 STORE/分块资源；不得对单个 3 GB DEFLATE 成员执行全量解压造成 OOM。

### 12.3 包体积与跨平台限制

- **Windows 超 4 GB PE/APE**：llamafile 官方记录 Windows 4 GB executable size 限制，跨 Windows 的单 APE **不承诺 >4GB**；应切换 `qaplet.exe + assets.qap` 或统一旁路大资源包模式。
- ZIP64 描述资源能力与 Windows PE 可执行能力不同；必须双门禁验证，不允许通过调大 ZIP64 索引间接越过 OS 限制。
- 资源包可带多款模型，但不能保证全部在启动时都进入内存，也不能保证推理时 RSS 很低；工作集由实际访问决定。
- 需验证大量资源条目时 ZIP 目录扫描/排序占用、ZipOS 初次建立索引延迟及 Windows 杀毒扫描放大成本，必要时引入预排序的 `QapletAssetIndex` 并记录与 ZIP 一致性校验。

### 12.4 `assets.qap` 外部标准资源包

对于 `>= 4 GB`、Windows 可执行大小接近上限、需要单独升级模型或只读索引的场景，使用：

```text
qaplet.exe           # 同一代码 APE，跨 OS
assets.qap           # 同一数据 ZIP64，跨 OS
```

资源包可流式 pread/mmap，保留版本、SHA-256/签名 manifest；应用启动时按包身份检查，缺失时明确报错；不隐式访问公网。资源随应用更新而非强制替换 Qaplet binary。

### 12.5 大资源测试计划

0 B、1 B、4 KiB、64 KiB、1 MiB、100 MiB、512 MiB、1 GiB、接近 4 GiB 及超过 4 GiB 数据包；STORE/DEFLATE、ZIP32/ZIP64、非法 size、重复条目、CRC/签名不符、字节截断、数据区与 CD 重叠、故意 64K 不对齐、Windows `MapViewOfFile`、ARM64 64KB 页内核、低内存/大范围随机访问、高并发 view 关闭。MVP 应用数据大于 100 MiB 时 RSS 上限按实测和缓存配置定义，不能预设“RSS 永远不增长”。

---

## 13. 第一方示范应用：基于 Qaplet 开发服务管理器 `qaplet-svc`

### 13.1 产品定义与最终依赖边界

**服务管理器首先是一个 Qaplet Application，而不是 Qaplet 内核组件、通用服务框架，也不是由 C 实现业务逻辑的独立软件栈。** 建议将 `qaplet-svc` 作为验证 Qaplet 运行时、通用 C Embed SDK、Node/Web API、QJB 和 Application Builder 的**第一方真实系统级应用**，由 Qaplet 语言生态自身开发、打包、测试、发行。

- **Qaplet Runtime**：提供通用 QuickJS Engine、ESM/CJS、文件/网络/进程/流、事件循环、QJB 和通用 SDK。不了解“服务名”“SCM”“systemd unit”“恢复策略”这些业务概念。
- **Service Manager Application (`qaplet-svc`)**：主体是 JavaScript/ESM 应用；负责 CLI、配置发现/验证、系统服务安装编排、适配器选择、健康检查、恢复/日志策略、升级诊断和交付。它同样是 `qaplet run`、`qaplet pack` 和可移植应用编译链的消费者。
- **App-private Native Bridge (C)**：仅提供 JS 不能安全或及时实现的 OS 入口与操作，包括 Windows SCM 回调、Win32 ABI、状态报告、线程安全 STOP 事件入口、Watchdog、Job Object/受管进程和最终强杀。**这是应用私有的宿主适配器，不是 Qaplet 的内核内建模块。**
- **OS Service Manager**：Windows SCM、systemd、SysV/OpenRC、launchd 等作为外部状态权威；应复用而不是重新造一套系统服务管理器。

冻结的依赖方向：

```text
apps/service-manager/src/*.mjs
       | Node/Web API + private service-manager:native bridge
       v
Qaplet Embed SDK <------ apps/service-manager/native/*.c
       |
       v
Qaplet Core / QuickJS / Cosmopolitan APE
```

禁止出现 `Qaplet Core -> service-manager` 依赖，禁止默认 `qaplet` / 普通 APE 含 SCM/Job Object 专有导入；服务功能也**不作为 `node:*` 兼容矩阵中默认内置 API**。将来经过多款真实应用验证，才讨论是否把通用 `spawnManaged` 等非服务语义提升到公开 `qaplet:*` API；v1 无需预先作该承诺。

### 13.2 应用优先的整体架构

```text
                           开发/打包阶段
  Qaplet + qaplet-svc JS 源码 + 轻量 Native Host 模板
              | qaplet pack / cosmocc + Embed SDK
              v
                  qaplet-svc APE（独立应用）

   管理命令: install | start | stop | restart | status | logs | remove
       |
       v
+------------------------ Qaplet Application -------------------------+
| JS CLI / Adapter Registry                                           |
|  Config Loader & Schema  |  Systemd/SysV Unit Generator            |
|  Service Installer       |  Health / Readiness / Log / Restart      |
|  Status / Doctor         |  App Lifecycle / Backoff / Diagnostics   |
|        |                                               ^             |
|        v                                               |             |
|   private `service-manager:native` module (仅本应用)     | JS callback |
|        |                Qaplet Runtime + Event Loop     |             |
+--------+------------------------+----------------------+-------------+
         |   C Embed SDK/host module API, POD events
+--------v------------------------------------------------------------+
| **薄 Native Service Host / platform bridge（应用随包携带）**         |
| Windows: SCM dispatcher / HandlerEx / SetServiceStatus              |
| Process: Job Object / handle ownership / wait / terminate           |
| Stop: atomic latch / wake / independent monotonic watchdog          |
+--------+------------------------------------------------------------+
         |
         +-- Windows SCM：启动/控制 qaplet-svc 进程（Wrapper 模式）
         +-- Unix：systemd 直接管理业务进程（默认 Direct 模式）
         +-- Unix：按需 qaplet-svc Wrapper（有定制探针/兼容需求时）
         |
         v
     Java / Python / Go / EXE / APE / QJB 等受管业务
```

**理解关键**：Windows SCM 的 `ServiceMain` 是应用 Native Host 的特殊 OS 入口；但服务配置、管理 CLI、模式选择、健康检查、日志、重启策略的实现仍然是 Qaplet JS 应用。Unix systemd Direct 模式甚至可以**不常驻 `qaplet-svc`**，仅将它当成生成/安装/诊断工具。两者并不矛盾：同一个应用提供不同平台的可选执行适配。

### 13.3 推荐工程目录（归属 `apps/`，不归属 `addons/`）

```text
apps/service-manager/
├── README.md
├── package.json                    # 应用元数据，不强制用户机安装 npm
├── Makefile                        # 使用已锁 cosmocc + Qaplet SDK
├── qaplet.app.json                 # 应用能力/入口/资源声明
├── src/
│   ├── cli.mjs                     # install/start/stop/status/validate/generate
│   ├── main.mjs                    # console / SCM bridge host mode 的 JS 入口
│   ├── config/{discover,parse,validate,resolve}.mjs
│   ├── manager/{controller,state,events}.mjs
│   ├── supervision/{restart,readiness,health}.mjs
│   ├── process/{managed,shutdown,stdio}.mjs
│   ├── logging/{capture,rotate,redact}.mjs
│   ├── backends/
│   │   ├── windows-scm.mjs          # JS 管理编排，经私有 C bridge 实际调用 SCM
│   │   ├── systemd.mjs              # unit 生成、systemctl、journald
│   │   ├── sysv.mjs
│   │   ├── openrc.mjs
│   │   └── launchd.mjs              # P2
│   └── internal/native.mjs          # 私有模块封装，无 OS 原始指针暴露
├── native/
│   ├── host.json                    # 通用 Native Host App 构建描述/源文件集合
│   ├── main.c                       # 极薄 OS/bootstrap 入口
│   ├── windows/{scm,admin,abi}.c    # SCM 控制入口/状态/ABI
│   ├── process/{managed,winjob,unixgroup}.c
│   ├── event/{queue,stop-latch,wake}.c
│   ├── watchdog/{deadline,cleanup}.c
│   ├── unix/{signal,notify}.c
│   └── module.c                     # 注册 `service-manager:native`
├── resources/{schema,templates}/   # JSON Schema、unit/plist/init 模板
├── examples/                        # Java/Tomcat、Python、Go、QJB
├── tests/{js,native,windows-vm,systemd,security,soak}/
└── docs/{architecture,operations,release}/
```

允许未来把 `apps/service-manager` 整体迁往独立仓库；它与其他 Qaplet App 采用**相同包格式、相同 SDK 版本锁、相同 CI 合同**。唯一特别之处在于其需要一个约数个原生源文件构成的**私有 Host Template**。不另建第二套 C 项目框架来复制 Qaplet 的 JSON/HTTP/文件/CLI 逻辑。

### 13.4 通用 C Embed SDK：为应用扩展原生宿主，而非提供 Service API

SDK 应实现足够小、可由任何 C 应用使用的稳定合同。下列为**设计拟议接口，并非现存头文件**：

```c
/* 公开的通用 SDK 合同（伪头文件） */
typedef struct QapletRuntime QapletRuntime;
typedef struct QapletEmbedConfig QapletEmbedConfig;
typedef struct QapletHostEvent QapletHostEvent; /* POD，值复制，不含 JSValue */

int qaplet_runtime_create(const QapletEmbedConfig *, QapletRuntime **out);
int qaplet_runtime_register_host_module(QapletRuntime *, const char *specifier,
                                         const QapletHostModuleVTable *, void *opaque);
int qaplet_runtime_run_entry(QapletRuntime *, const char *module_url);
int qaplet_runtime_run(QapletRuntime *);              /* JS owner thread */
int qaplet_runtime_post_event(QapletRuntime *, const QapletHostEvent *); /* thread-safe */
int qaplet_runtime_request_stop(QapletRuntime *);     /* thread-safe; no app semantics */
int qaplet_runtime_request_interrupt(QapletRuntime *);/* cooperative interrupt */
int qaplet_runtime_destroy(QapletRuntime *);          /* owner thread */
```

- `register_host_module()` 将应用私有 `service-manager:native` 映射到 C bindings，**而不修改 Qaplet 的默认 `node:*` / `qaplet:*` 模块集合**。JS 对象创建/Promise resolve/JS callbacks 全部在 JS owner 线程执行。
- `post_event()` 提供有界 POD 事件投递和 wake，返回投递成功/关闭/队列满，配合 `generation` 防迟到完成事件和 ABA/UAF。**服务 STOP 不依赖普通队列容量**：应用私有 native stop latch 先 CAS 记录，再通过 SDK 通用 `request_stop` 唤醒 owner，必要时 native watchdog 继续推进。
- `create/register/run/destroy` 的线程归属、扩展注册顺序、调用约定、内存所有权、`struct_size/abi_version`、错误编码必须可测；不把 `JSContext*` 当作稳定公共 ABI。
- 用户机只运行独立 APE，不需 `qaplet` 可执行文件或编译器。应用构建时通过**静态** `libqaplet-embed.a` 与当前锁定的 cosmocc 链接。
- 同一 C SDK 还必须能支持另一个**非服务管理**示范应用（例如原生事件源 / 自定义资源 Provider），避免 SDK 被 `qaplet-svc` 私有需求绑架。

**不引入通用 FFI 的前提**：v1 所需 SCM/Job 原生能力可经静态 host 模块私有导出；不存在“先实现完整 Win32 FFI，才能开发服务管理器”的前置要求。

### 13.5 Windows：最小 SCM 原生宿主与 JS 线程隔离

Windows 服务入口必须由 Native Host 实现。主线程尽早调用 `StartServiceCtrlDispatcherW`，`ServiceMain` 完成 `RegisterServiceCtrlHandlerExW`、初始化原生状态并上报 `SERVICE_START_PENDING`，之后在 JS owner 线程启动 Qaplet SDK 的 App `main.mjs`。仅在 JS 初始化和经约定的 readiness 成功后报告 `SERVICE_RUNNING`。

Windows HandlerEx 只允许：原子记录 `stop/shutdown`、推进原生控制状态请求、非阻塞唤醒 owner、快速返回；**不能调用 JS、等待 JS Promise、解析 YAML、刷盘、同步终止进程或阻塞 SCM**。独立原生协调线程序列化 `SetServiceStatus`、实际 checkpoint/wait hint 与 watchdog deadline；`SERVICE_STOPPED` 只在完成清理后报告一次。如果强制结束宿主进程，不伪造已成功上报 STOPPED，而通过退出码和 SCM 事件反映故障。

```c
/* Service HandlerEx 伪代码：实际类型/内存栅栏/并发由工程实现约定 */
DWORD WINAPI HandlerEx(DWORD code, DWORD type, LPVOID data, LPVOID opaque) {
    AppServiceHost *h = (AppServiceHost *)opaque;
    if (code == SERVICE_CONTROL_STOP || code == SERVICE_CONTROL_SHUTDOWN) {
        atomic_store_explicit(&h->stop_requested, true, memory_order_release);
        app_native_request_stop(h); /* latch + native wake；不触及 JSValue */
        return NO_ERROR;
    }
    return code == SERVICE_CONTROL_INTERROGATE ? NO_ERROR
                                               : ERROR_CALL_NOT_IMPLEMENTED;
}
```

`StartServiceCtrlDispatcherW`、`SetServiceStatus`、`CreateServiceW`/`ChangeServiceConfig*`、`QueryServiceStatusEx` 与 Win32 `__ms_abi__`/导入符号仍须完成**独立 M0-SVC ABI 实测**。真实服务入口与 `--console` 前台调试入口分开，不通过检查某个错误码静默替代 SCM。

**停止时序**：SCM STOP → Native latch/wake → SDK owner loop → `main.mjs` 的 JS stop handler → Managed Process graceful stop → 原生确认退出/关闭 → `SERVICE_STOPPED`。若 JS owner 卡死，原生 watchdog 到达单调 deadline 后清理可管理的业务子树，必要时以非零码结束宿主；**不是宣称任意 JS 同进程卡死都可安全地恢复**。

### 13.6 Private Managed Process API：C 管句柄，JS 决定策略

Windows 受管子进程：创建 Job Object、设定 `JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE`、`CreateProcessW(CREATE_SUSPENDED)`、在恢复主线程前 `AssignProcessToJobObject`；退出和管道事件通过原生句柄与安全队列报告给 JS。先按配置优雅停止（IPC/HTTP/业务指定协议），到期由原生 `TerminateJobObject` 或最后 Job Handle 关闭清理；不可默认假定 Windows 服务支持 Ctrl+C。

Unix Wrapper：创建独立 process group/session（或者由 systemd cgroup 全权管理），`SIGTERM` → deadline → `SIGKILL`、`waitpid` 回收。所有等待/重试必须有界，处理 PID/Handle 复用、子树逃逸、pipe backpressure、Job 嵌套与低权限账户。**不可因 Job 分配失败静默退化为单 PID kill 并声称完整子树清理**。

`service-manager:native` 的私有 JS API 可提供：`spawnManaged`、`requestStop`、`forceTerminate`、`awaitExit`、`setOsStatus`、`getOsStatus`。能力由应用的 native 绑定和权限/状态约束校验，不是 JS 可任意提权的通用能力。任何成功结果应区分 `requested`、`accepted`、`actuallyExited`，不得把停止请求误报为已经退出。

**执行模式边界**：

| 模式 | 业务程序 | 故障隔离 | 优先使用场景 |
|---|---|---|---|
| `process` | Java/Python/Go/EXE/APE 等外部子进程 | 有独立 Job/process group，可以原生强杀和回收 | 默认生产服务管理 |
| `qjb` | 宿主内 QJB/JS 业务逻辑 | 和 Service Manager JS 同进程；脚本卡死可拖累宿主 | 可信、轻量且有明确风险预算 |
| `script` | 宿主内 ESM/JS | 同上，不称为强隔离 | 开发或小型可信后台任务 |
| `systemd-direct` | systemd 直接执行任何已安装业务进程 | systemd cgroup 管理；无额外 Wrapper | **Linux 首选**，服务仅需 OS 原生能力时 |

### 13.7 JS Service Manager：配置、CLI、健康、日志和恢复的主体

应用 JS 模块拥有：

| 功能 | JS 实现 | 原生/OS 附加合同 |
|---|---|---|
| 配置读取 | `JSON.parse`，经过审计的纯 ESM YAML/.env parser、JSON Schema | 使用 Qaplet `node:fs`/`node:path`，路径身份需验证 |
| 命令行 CLI | ESM 解析器、参数校验、dry-run、变更计划 | Windows 安装动作经 SCM 管理接口，Unix 调用受控 `systemctl` |
| 配置发现 | 根据 serviceName、安装目录、显式锁定路径发现 | 获取真实 executable path，不依赖交互式 cwd |
| restart/backoff | JS 状态机、指数退避、预算、熔断、重复事件抑制 | 外部进程重建由 native 管理；systemd-direct 交给 OS |
| readiness / health | HTTP/TCP/exec 及受控 JS hook | Qaplet `fetch/node:net/child_process`；无 HTTP API 时可降级为声明的 probe |
| 日志 | child stdout/stderr Stream、文件轮转、日志结构化与脱敏 | 管道/线程清理由 Native Host 兜底；direct 模式优先 journald |
| systemd/SysV/OpenRC | unit/脚本生成、状态查询和平台差异校验 | OS 管理器最终拥有服务进程；不能两层同时重启 |
| 诊断 | `status --json` 区分 OS/host/child/health 状态 | 不捏造缺失的 OS 字段或运行时状态 |

Qaplet Node 模块与 YAML parser 如尚未达到 `verified`，JS 应用必须在 App Manifest 声明最小能力并在构建期失败；不以“Service Manager 是示范应用”为理由破坏 Runtime 的兼容性事实。

示意控制逻辑（**接口未冻结**）：

```js
// apps/service-manager/src/main.mjs
import { serviceHost } from 'service-manager:native'; // 仅编入本 App 的 native 模块
import { loadConfig } from './config/resolve.mjs';
import { supervise } from './supervision/restart.mjs';

const config = await loadConfig({ name: serviceHost.name, root: serviceHost.installRoot });
const child = config.execution.mode === 'process'
  ? await serviceHost.spawnManaged(config.execution)
  : await startTrustedInProcessEntry(config);

serviceHost.onStop(async ({ deadlineMs }) => {
  await child?.requestGracefulStop?.({ deadlineMs });
  // Native watchdog 无论 JS 是否响应，都会独立检查 deadline
});
await verifyReadiness(config.lifecycle.readiness, child);
await serviceHost.markReady();  // 仅 Native Host 可最终报告 SCM/systemd READY
await supervise(config, child, serviceHost);
```

上述应用入口不是在 Windows SCM 回调线程执行；应用可以通过模拟本地 host module 的测试替身做纯 JS 单元测试。服务管理 CLI 应尽可能共享同一套 JS 配置模型和平台 adapter，不复制一套 C 配置系统。

### 13.8 自动按服务名发现配置和环境文件

```text
invoice-api/
├── qaplet-svc.exe                # 独立编译的 Qaplet 应用（跨 OS APE 经认证）
├── invoice-api.service.yaml      # 可替换 JSON；按服务名加载
├── invoice-api.env               # 可选，受 OS 权限保护
├── hooks/health.mjs
├── bin/invoice-api.exe           # 可替换 java -jar / python / go / app.qjb
├── data/
└── logs/
```

- Windows 安装时将**绝对可执行文件路径**、`--service-host --name invoice-api` 和可选 `--config` 注册到 SCM `ImagePath`；绝不记录秘密、环境变量全量值或访问令牌。Unix 安装时生成绝对路径的 unit，不依赖 systemd 默认 cwd。
- 配置权威路径：显式 `--config` 优先；否则在固定 `installRoot/config/` 和 `installRoot/` 中寻找 `<name>.service.yaml`、`<name>.service.yml`、`<name>.service.json`、`<name>.yaml`、`<name>.json`。只允许一个有效主配置；多个同时存在报“歧义”，不做悄悄选取。`<name>.env` 仅承载经白名单控制的环境值，不能单独定义服务。
- 完整 schema/解析属于 JS 应用，最低原生启动引导只解析可信服务名、根目录和保守超时上限，保证及时进入 Windows SCM dispatcher。JS 验证成功后再授权缩小 deadline；**不存在 C 和 JS 两套权威主配置**。
- 相对资源路径基于配置文件所在目录，不是进程工作目录；拒绝 `..` 越界、Windows junction/reparse point/UNC 逃逸、重复归一化名称、YAML tags/alias bomb、`.env` 命令注入。敏感配置必须依赖受控 ACL/文件权限或显式 Secret Provider，默认不写明文日志/状态输出。
- 配置优先级：显式安全 CLI override > 主配置 environment > 环境文件 > 继承环境允许列表 > 内建默认。安装级的 Windows 服务账户、启动类型、权限和服务二进制路径仅可由授权安装/更新命令修改。

示例配置（拟议的 JS App schema；**并非 Qaplet 内核 schema**）：

```yaml
schema: qaplet.service-manager.v1
name: invoice-api
execution:
  mode: process
  executable: ./bin/invoice-api.exe
  args: ["--listen", "127.0.0.1:9000"]
  workingDirectory: ./data
  environmentFile: ./invoice-api.env
  environment:
    LOG_LEVEL: info
lifecycle:
  startupTimeoutMs: 30000
  stopTimeoutMs: 15000
  shutdown:
    method: ipc       # ipc | http | signal | none；Windows 不默认依赖 Ctrl+C
  restart:
    authority: os     # os | app；不能同时作用于同一业务进程
    policy: on-failure
    maxRestarts: 5
    windowSeconds: 300
    minDelayMs: 1000
    maxDelayMs: 60000
  readiness:
    mode: http
    url: http://127.0.0.1:9000/health
    timeoutMs: 15000
  health:
    intervalMs: 10000
    unhealthyThreshold: 3
logging:
  stdout: ./logs/stdout.log
  stderr: ./logs/stderr.log
  rotate: {maxBytes: 10485760, keepFiles: 5}
windows:
  account: LocalService
  startType: automatic
linux:
  systemd:
    mode: direct           # direct | wrapper
    type: simple           # direct 默认可选 simple/exec；notify 需真正提供通知
```

### 13.9 Native/JS 双状态机与恢复权威

Native Host 维护与 SCM 对应的 `START_PENDING/RUNNING/STOP_PENDING/STOPPED`、控制请求和最终 deadline；JS App 维护 `CONFIG_LOADED/STARTING/READY/UNHEALTHY/BACKOFF/STOPPING`。状态映射要求 `manager_state`、`app_state`、`child_state`、`readiness`、`last_error` 来源明确。`RUNNING` 不是简单的“JS 已创建”，也不等价于 HTTP 健康已通过。

- **默认 Windows Wrapper `authority: os`**：JS 判定业务永久失败后令服务宿主以明确失败码退出，SCM 的 Recovery Actions 执行服务级恢复；JS 不同时重启宿主。
- **`authority: app`**：JS 在**同一个仍活着的服务宿主**中管理子进程的重建与 backoff；OS 不对同一业务故障另外做竞争的自动重启。宿主崩溃后的 OS 兜底属于独立故障域，必须单独显式声明。
- **Linux systemd Direct**：systemd 的 `Restart=`、`KillMode=`、cgroup、journal 是权威；`qaplet-svc` 仅生成 unit/安装/查询，不驻留监督业务进程。Direct 模式没有 JS 长期健康扫描与日志轮换，不能声称已经具备；需要时改用 Wrapped 模式或外部监测。
- **Linux Wrapper / SysV**：如果业务需要 JS 自定义恢复、健康探针或并非 systemd 的管理设施，才运行独立的服务宿主；OS unit 和 JS policy 分别按声明的故障域控制，禁止同一级双重重启。
- 关闭路径均为停止事件幂等、子进程 generation 检查、单调时钟 deadline、可审计退出码；脚本卡死时 C 端拥有最后兜底，不把重启风暴隐藏为成功状态。

### 13.10 构建和使用：像普通 Qaplet 应用一样开发、交付

**开发期（JS 优先）**：以 `qaplet run` 调试纯 JS 命令 `validate/generate`、配置/恢复策略和无 OS 权限副作用的 dry-run。平台相关功能通过 mock native module 测试；真实 SCM/Job 依赖下面的 Native Host 产物。

```bash
# 设想中的开发命令，实际需在相应里程碑实现
qaplet run apps/service-manager/src/cli.mjs -- validate --config ./invoice-api.service.yaml
qaplet pack apps/service-manager -o build/service-manager.qjb

# v1 最稳妥：应用 Makefile 调用 cosmocc + libqaplet-embed.a + 字节码/资源
make -C apps/service-manager release

# 长期通用 App Builder 目标（不是现有 CLI）
qaplet compile apps/service-manager --host native/host.json --output dist/qaplet-svc.com
```

**用户期**：无需 Node/npm/qaplet 单独安装；仅持有编译好的 `qaplet-svc` 和业务/配置文件。CLI 是**同一个 JS App**的命令入口；编译后的 OS Service 模式才通过原生 bootstrap 提前接入 SCM。

```bash
qaplet-svc validate --name invoice-api
qaplet-svc generate --name invoice-api --target systemd --output ./invoice-api.service
qaplet-svc install --name invoice-api --dry-run
qaplet-svc install --name invoice-api
qaplet-svc start invoice-api
qaplet-svc status invoice-api --json
qaplet-svc logs invoice-api --follow
qaplet-svc stop invoice-api
qaplet-svc remove invoice-api
qaplet-svc host --name invoice-api --console   # 前台调试，与 SCM 模式分离
```

- `install/update/remove` 是授权操作；应用 JS 生成变更计划和 dry-run，Native SCM/Admin API 或 OS 系统命令执行实际变更。管理命令无需长驻 Native Service Host，但**可以且应直接运行 Qaplet JS CLI**，不能错误写成“管理端不创建 JS Runtime”。
- 不将 C `main()` 编写为另一个服务管理器 CLI：它仅判别 `--service-host` 并快速进入 SCM dispatcher；其余操作由 SDK 加载 `src/cli.mjs`。Windows SCM 的 `ImagePath` 存显式 `--service-host`，而非尝试在错误码分支猜测运行环境。
- 应用编译器扩展 `--host` 应设计成**适用于任何 Qaplet App 的通用嵌入构建机制**，不能为了 `qaplet-svc` 引入 `qaplet service build` 专属命令。P0 可先用 `Makefile + cosmocc + SDK` 链接，之后再泛化到统一 `compile --host`。
- `qaplet-svc` 可以发布为可独立下载的 Qaplet 应用，也可以随每个 Java/Python/Go/EXE 业务安装包发行，并按应用实际名称自动加载配置；发行包固定 SDK lock、第三方 YAML 库来源、签名/哈希和 Windows 证书流程。

### 13.11 Windows ABI / 应用级验收门槛

在实际锁定的 Nightly cosmocc 上单独验证 `StartServiceCtrlDispatcherW`、`RegisterServiceCtrlHandlerExW`、`SetServiceStatus`、`CreateServiceW`、`QueryServiceStatusEx`、`CreateProcessW`、`AssignProcessToJobObject`、`TerminateJobObject` 的导入/调用约定与 APE 兼容；必要补丁是**应用私有 overlay**（除非发现通用 Cosmopolitan 缺陷并上游修复），不要把 `advapi32` 服务导入塞入 Qaplet Kernel。

服务应用测试必须覆盖：JS-only CLI 的 `validate/generate`、Native Host SCM 实装、STOP 回调微小且非阻塞、队列满/重复停止、JS 线程忙死/Native watchdog、异步 child exit 与并发 stop、Windows Unicode/空格路径、受限账户、Job Object 嵌套/逃逸、系统重启自启动、回收所有可管理子进程/句柄、SCM 真实状态、低权限安装失败和敏感配置不泄漏。对 Linux direct 和 wrapper **分别**验证 systemd/journald/cgroup/restart/stop，不以 Wrapper 成功代替 Direct 模式成功。

每个应用发行物单独有 `--version --json`、`--capabilities --json`、SDK ABI 需求、SBOM、源码/字节码签名/哈希、真机/VM 负向测试和 24h 稳定性证据。**M0-SVC 的 ABI/SCM PoC 失败只阻断该应用对应平台的服务能力；不阻断 Qaplet Core v1 发布。**

---

## 14. Service Manager App 的 Linux / Unix 系统适配策略

### 14.1 systemd Direct：默认不常驻 Qaplet 服务管理器

对于 Linux 上已有可执行程序（Java、Python、Go、APE、传统 ELF），**默认由 `qaplet-svc` 这个 JS 应用负责生成/校验/install unit，但 systemd 直接启动业务程序并持有 cgroup、重启、日志和停止权威**。`qaplet-svc` 不必为了“跨平台一致”再常驻一层 JS Wrapper。

```ini
[Unit]
Description=Invoice API
After=network-online.target
Wants=network-online.target

[Service]
Type=exec
ExecStart=/opt/invoice-api/bin/invoice-api --config /etc/invoice-api/app.json
WorkingDirectory=/opt/invoice-api
User=invoice-api
Group=invoice-api
Restart=on-failure
RestartSec=5s
TimeoutStopSec=20s
KillMode=control-group
NoNewPrivileges=true

[Install]
WantedBy=multi-user.target
```

`Type=exec` 只在目标 systemd 版本确证支持时生成，否则按受支持矩阵使用 `Type=simple`；如果业务程序自己原生支持 `sd_notify`，才可生成 `Type=notify`，且 `READY=1` 必须由真正就绪的服务发出。**`qaplet-svc generate` 不能因为配置写了 `readiness.mode=http` 就假装 Direct 业务进程支持 `sd_notify`**。Direct 模式的健康监测依靠业务自身、独立健康监控或未来明确的 adapter；不是常驻的 JS 健康循环。

JS adapter 负责 `unit` 安全转义、绝对路径/用户权限校验、`systemctl daemon-reload/enable/start/stop/status` 的非 Shell 执行和可审计变更计划。安装/修改系统目录需显式管理员权限；命令不暗中调用 `sudo`。

### 14.2 systemd Wrapped：需要 JS 监督或探针时才启用

当必须具备统一的 JS readiness/health、日志轮转/诊断、特殊退出协议或管理无法直接被 systemd 监督的子程序时，允许 `linux.systemd.mode: wrapper`，由 systemd 运行**同一个编译好的 Qaplet Service Manager Application**：

```ini
[Unit]
Description=Invoice API (Qaplet Service Manager Application)
After=network-online.target

[Service]
Type=notify
NotifyAccess=main
ExecStart=/opt/invoice-api/qaplet-svc host --name invoice-api
WorkingDirectory=/opt/invoice-api
User=invoice-api
Group=invoice-api
Restart=on-failure
RestartSec=5s
TimeoutStopSec=20s
KillMode=control-group
NoNewPrivileges=true

[Install]
WantedBy=multi-user.target
```

`Type=notify` 要求应用私有 native adapter 或经验证的 Qaplet JS/Unix datagram 实现向真实 `NOTIFY_SOCKET` 发送 `READY=1`/`STOPPING=1`，并证明仅在业务真正 ready 后发送；没有此能力使用 `Type=simple`/`exec` 并声明 readiness 差异。优先让 OS 处理 systemd cgroup/进程树，禁止和 JS 的进程子树清理与 `Restart=` 形成互相抢控制权。若 app 内部重启业务子进程，OS 只在宿主故障域重启宿主。

停止信号可由 Native Host 的 `sigwait` 专用线程或异步信号安全 self-pipe 方式转换为通用事件；**不得在 Unix signal handler 直接执行 JS、获取普通锁或进行内存分配**。native watchdog 对 JS owner 卡死时的 deadline 负责，`systemd TimeoutStopSec` 为外层最终保障。

### 14.3 SysV init / OpenRC：生成薄启动脚本，不引入第二套运行时

没有 systemd 的 Linux/Unix，JS App 生成对应的 `init.d`/OpenRC 脚本，通过经验证的 `start-stop-daemon` 或可用系统机制启动业务或 `qaplet-svc host`。`pidfile` 必须校验真实进程身份和启动时间，不得对复用 PID 盲目 `kill $(cat pidfile)`；僵尸清理、信号、进程组、stdout/stderr 和退出码交由 OS/native adapter 按已支持能力实现。没有 readiness 机制时必须标明 unsupported，不伪装成 systemd。

### 14.4 macOS launchd：P2 独立平台后端

由 JS App 生成 `plist`、校验路径/账户/权限，并以受控的 `launchctl` 操作管理。需要单独验证 macOS 签名/公证、后台启动、信号与 launchd 重启的边界。此能力不引入 Qaplet Kernel，不阻断 Windows SCM + Linux systemd 的服务管理应用 v1。

### 14.5 跨平台统一的仅是“应用契约”，不是伪统一 OS 状态

同一服务配置 schema 和 CLI 可以跨平台复用，但各后端的能力矩阵必须说明 `direct|wrapped`、`readiness` 来源、`restart authority`、`log provider`、`process tree authority`、`status` 实测级别。`qaplet-svc status --json` 返回的 `manager_state`、`app_state`、`child_state`、`readiness` 允许按模式标 `unsupported`/`not_applicable`，绝不填充假成功。服务应用不得以牺牲 systemd 原生能力来追求对 Windows SCM 的表面一致。

---

## 15. 开发与交付工具链

### 15.1 构建宿主与发行产物

- 正式构建以 Linux x86-64/WSL2 工具链环境为首选；本地 Windows/macOS 允许进行开发与验证，但必须生成相同内容身份，或明确标明 build-host 差异。
- 获取锁定 Nightly cosmocc，分别编译 x86_64/ARM64，使用 `apelink` 或该 Nightly 推荐的 fat-APE 链接流程；把实际编译/链接命令、构建 flags、资源顺序、section layout、ARM64 PE/ELF headers 收录到 `build-info.json`。
- 不能把仅 x64 生成的 APE 在 ARM64 Windows 可启动（经模拟）误认为 ARM64 原生 PE 已完成；macOS ARM64需要native启动验收；Linux ARM64在M0优先原生runner，按§19实测架构/页大小后用全系统QEMU补缺，native与模拟证据分开。
- 64KB page ARM64 Linux是必须gate；原生ARM64宿主页配置满足时直接验证，否则用全系统QEMU补充：复用旧仓库实测踩坑经验，校验 ELF segment alignment、APE boot header 可读窗口、`MAP_SHARED`/`mmap` granularity、pthread TLS 与资源区 offset。
- 正式运行时以 C 和 QuickJS 源码构建，非编译主机也不要求 Node；可提供 `qaplet` 自身实现的 build/pack 命令，降低依赖。

### 15.2 构建 profile 和体积约束

| Profile | 内容 | 发布策略 |
|---|---|---|
| `minimal`（调试起点） | QuickJS + host loop + ESM + fs basics | 仅测试/诊断，非完整 Node 兼容 |
| `base`（默认） | CLI、ESM/CJS、主要 Node APIs、FS/TTY/net/http/安全 TLS、QJB、通用 Embed SDK 接入点；**无 SCM** | **默认 runtime fat APE** |
| `full` | base + 可选压缩、SQLite/WASM（若准入）、HTTP2（若准入） | 可选；能力声明与二进制实际一致 |
| `sdk` | `libqaplet-embed.a`、稳定 C headers、示例、事件桥与资源 provider | 开发 SDK；可静态链接 `qaplet-svc`，用户机不单独安装 |
| `tests` | 可复现回归输入、oracle 摘要、标准报告 | 独立发布/CI artifact |

**独立应用发行（不属于 Runtime Profile）**：`apps/service-manager` 用它自己的 JS 源码、少量私有 C Native Host 和通用 SDK 链接为 `qaplet-svc` fat APE；App 自行发布、验收和声明 OS Service 能力，默认 `base/full` 不受它的专用 native 依赖影响。

体积不从历史版本直接继承强制硬数。建议目标为 `base` fat APE **尽量保持十几 MB 内或更小**，并以阶段增量、依赖大小贡献表和 A/B startup/RSS 作为每个后端的准入要求；若新 QuickJS master + Nightly 实测明显超标，分析 linker dead-strip、共用 CA、重复字节码和静态压缩库闭包，不用删 TLS 验证去换体积。`qaplet-svc` 是基于 Qaplet 开发的应用，包含静态 Runtime、JS 业务模块及私有 C Host；应用体积/启动/RSS 与 `qaplet` 分别预算，并记录各层大小贡献，不能因为“独立发行”而隐瞒增量。

### 15.3 正式可交付证据

- `qaplet-fat.ape`（无服务相关导入）；各已认证 CPU slice 的测试辅助产物（不作为替代发布要求）。
- `qaplet-base`、`qaplet-full`、`qaplet-sdk`、`qaplet-tests`、capability-matrix、release manifest、SHA256、SBOM、licenses、toolchain-lock、source-provenance；**作为第一方应用另行**发布 `qaplet-svc` APE、service capability/report、JS/Native 文件清单、SBOM/ABI-lock/跨 OS 测试证据。
- 全平台同一 APE payload hash、安装/下载完整性、运行实际 `--version` / `--caps` 回读比对；检查外层 ZIP 资源无重复名/恶意路径/不完整写入。
- 构建离线性验证：归档离线缓存后断网重构，应得到字节可复现结果，或对签名/时间戳差异给出明确可验证理由。
- 私有/企业版本需要 code signing/notarization 时独立发布，不允许悄悄改变 ZIP 资源而仍宣称同一已签名摘要。

---

## 16. 跨平台验收：运行环境、行为合同与真实设备

### 16.1 支持级别不是一个布尔值

| 支持级 | 定义 | 典型宿主 |
|---|---|---|
| Tier-A Release Gate | 每次正式发布执行真实机器/VM、功能/负向/安装/基本性能，全部关键项不跳过 | Linux x64、Windows x64、macOS x64/ARM64、Linux ARM64 4K/64K |
| Tier-B Verified | 定期原生执行、具体功能有对应证据；发布前必须检查高风险回归 | Windows ARM64 上 x64 模拟执行、FreeBSD x64（按资源逐项纳入） |
| Tier-C Experimental | 构建或代码路径研究但未建立完整官方验收 | OpenBSD/NetBSD、更多 Linux CPU 组合、Android/bionic 可能性 |
| Not Supported | 无交付证据；`--capabilities` 不得标为支持 | RISC-V、LoongArch 的直接 APE 承诺，以及未验证旧 Windows 版本 |

**注意**：表中是“计划的验证层级”，并非已经实现的 Qaplet 支持。正式 v1 发布可按资源减少 Tier-A 初期范围，但必须对外如实披露；不能继承旧项目的“七宿主已绿”便自动算新 Qaplet 已绿。

### 16.2 最小真实执行矩阵

| OS / CPU | APE 直接启动 | Node API | QJB / ZIP | TTY | 子进程 | Network/TLS | `qaplet-svc` 第一方应用 | 大资源 |
|---|---|---|---|---|---|---|---|---|
| Linux x86-64 | P0 | P0 | P0 | PTY | P0 | P0 | systemd Direct（App 独立验收） | P1 |
| Linux AArch64 / 4KB | P0 | P0 | P0 | PTY | P0 | P0 | systemd Direct（App 独立验收） | P1 |
| Linux AArch64 / **64KB** | **P0** | P0 | P0 | PTY | P0 | P0 | systemd Direct（App 独立验收） | P1 |
| Windows x86-64 | **P0** | P0 | P0 | ConPTY/Console | P0 | P0 | **SCM App P0/P1** | P1 |
| macOS x86-64 | P0 | P0 | P0 | PTY | P0 | P0 | launchd App P2 | P1 |
| macOS AArch64 | P0 | P0 | P0 | PTY | P0 | P0 | launchd App P2 | P1 |
| Windows ARM64 x64 模拟 | P1 | P1 | P1 | Console | P1 | P1 | SCM App P1 | P1 |
| FreeBSD/OpenBSD/NetBSD | P2 | P2 | P2 | P2 | P2 | P2 | Service App daemon P2 | P2 |

每个 APE 制品（`qaplet`、单文件业务 App、`qaplet-svc`）**分别**在其已声明宿主验证 SHA-256 恒等；Windows 仅修改文件名 `.exe` 不改变内部字节；若某平台必须进行格式同化（`assimilate`、签名），该产物单列为 Managed Distribution，不再等同原始 APE 的同一 hash。`qaplet-svc` 这款应用的 OS service 验收独立统计，**不是 `qaplet` Kernel 发布的前置门槛**；但通用 SDK/App Compiler 因应用测试暴露出的回归应按通用合同跟进。

### 16.3 APE 与 OS 兼容故障样本

- Linux 系统将 MZ 文件送进 Wine/WSLInterop/binfmt_misc 的情况；需要实际测试 loader、可执行权限、mount `noexec` 与 shell 路径。
- Windows Defender/企业白名单、受限账户、`.com`/`.exe` 文件名、Win32 Unicode path、UNC、reparse point/junction；`GetProgramExecutableName()`、`argv0`、实际映像路径区分。
- ARM64 ELF 最小段对齐、16K/64K 内核加载、TLS register 和 Windows ARM64 非原生模式；不以 qemu-user 成功替代真实 64KB kernel。
- Darwin PTY 关闭/恢复及 HUP、终端尺寸；进程结束时恢复所有 raw/cursor ownership。
- Windows `poll()` 对 SOCKET/HANDLE/pipe 的差别、非阻塞 connect、连接半关闭、read/write 粒度和 64KB granularity。
- 子进程：APE 调用另一 APE、原生 EXE、带空格/Unicode 命令、shell=false、stdin EPIPE、stdout/stderr 并发、取消和进程树清理。

### 16.4 Node 差分与真实应用验收

每个 API test fixture 必须能通过原版 Node LTS 与 Qaplet 执行；比较语义而不仅是 stdout 文本。Oracle 被固定在 `test-manifest` 的摘要上；对预期差异提交 `known-differences`，不允许大量无说明 skip。

**真实项目阶段选择**：Clack、Terminal-Kit、几款无需 native addon 的包（例如 commander/chalk 一类）、一个离线诊断 CLI、一个 HTTP/SSE 服务、一个 `fs + child_process` 自动化工具；将第一方 `qaplet-svc` 加入真实应用完整性样本：JS-only 运行 CLI、系统服务运行 Native Host 两条路径分别验收。增加复杂性后可尝试 Ink（因 React/Yoga/WASM 不作为首发硬门槛）。真实项目版本必须固定，收集完整 lockfile、运行命令、报错与修改量。

### 16.5 稳定性测试与优先性能指标

- **首发基础功能**：单 Runtime 运行 24h idle/event-loop、重复 create/destroy 10,000 次、低内存、100～256 TCP 空闲连接（逐步扩展）；GC 压力、取消、异常后再次运行。
- **IO fairness**：多个小/大 HTTP 流并发、SSE 长连接、网络抖动/断网、DNS 超时、慢速 FS、阻塞 CPU 任务；不能出现 nextTick 饥饿或不可取消的无界 queue。
- **运行时间指标**：cold start、steady RSS、process CPU idle、资源映射 VSS/RSS、first-byte latency、p50/p95/p99 Loop lag/HTTP latency、release binary size、hash 一致性。
- **回归基准**：和历史 quickjs-cosmo 的若干可复现场景做 A/B，但不把旧工程的性能测量直接写成新项目成绩；与 Node.js、txiki.js、LLRT 对照相同 JS 任务时要说明 API 差异。
- **观测证据**：发布报告保留 benchmark 使用的主机 OS/kernel/CPU/内存/存储/证书、冷/热运行、重复次数和统计方法。Perf 目标在 M1 测得真实基线后冻结，不凭空承诺具体微秒级启动数字。

---

## 17. 安全、权限、供应链与故障隔离

### 17.1 分层信任模型

- **程序与模块**：本地来源与可信发布物；`qaplet compile` 产物签名/校验和只是身份校验，不等于所含 JS 可安全执行。
- **运行环境**：本地进程默认具备 OS 当前用户权限，Node `fs/child_process/net` 就是 OS 能力，不称其为隔离沙箱；未来采用显式 `qaplet permissions` 时先实现真正 OS 访问边界。
- **包格式**：来自外部 QJB/ZIP/ZIP64 的不可信长度/偏移/路径都必须有整数溢出、重复项、重叠、解压炸弹、符号链接、CRC/SHA 校验和 fuzz。
- **网络**：TLS 证书、CA、主机名、SNI、ALPN、安全版本、代理、跳转、请求体上限；避免同步阻塞 JS 线程、证书全链放宽验证或静默降级。
- **Service Manager Application**：Windows SCM 配置/账户/Job Object 属于该应用私有原生 Bridge 的管理员边界，而不是 Kernel；服务不能默认以 LocalSystem 运行；Unix 尽量以非 root 专用账户执行；HandlerEx 不运行 JS，native 兜底应在 JS 卡死情况下独立生效。
- **操作日志**：环境变量、密钥、请求凭证、原生句柄和内部地址均需隐藏；开启 Debug 时仍要保证默认敏感字段不打印。

### 17.2 供应链控制

所有输入建立 `Source Lock -> Build Evidence -> Test Evidence -> Release Evidence` 链；下载文件先哈希后解包，禁止 TLS 忽略验证、禁止接受无签名来源的改写 Nightly/CA/第三方库。提供 SBOM (SPDX/CycloneDX)、LICENSES、补丁列表、CVE/安全公告状态和源码 URL。发布日期及源仓库 SHA 与运行时 `qaplet --version --json` 一致。

构建过程禁止静默联网：允许显式 `qaplet deps fetch`/开发脚本获取锁定依赖，默认 release build 在离线缓存中完成。Nightly 更新通过维护者独立专题进行，不随 git checkout 自动下载新版本。第一方 `qaplet-svc` App 与其嵌入 SDK 也必须锁版本，禁止自身后台更新，升级采用显式、可回滚的维护动作。

### 17.3 环境隔离与多租户不是 v1 自动能力

QJB manifest 可声明需求，不代表具备 Linux namespace/seccomp 或 Windows Job/Token 级访问隔离。若要执行不可信 npm 脚本，必须使用 OS 原生受限账户/沙箱或进程隔离；仅 QuickJS Context/JSRuntime 隔离了 JS VM 状态，不保证阻止 `fs` / `process` / `net` 系统权限。所有公开文档需把这个边界写明。

### 17.4 证据政策

- 高风险 CRYPTO/TLS/ZIP64 与 **Service Manager App 私有 SCM/Job/终止** 变更需独立 reviewer，并保留原始负向用例和 commit SHA；AI Agent 可以实现与初筛，但不等同人工安全批准。
- CI Gate 包含来源 lock、SHA 校验、C/C++/JS 静态分析、 ASAN/UBSAN（在支持的原生开发构建中）、fuzz corpus、跨平台实跑、签名/产物完整性。
- 每一项 capability 声明对应 `test_case_ids`、`source SHA`、`build hash` 和 `host result`；测试失败不得继续发布假能力状态。

---

## 18. 历史 quickjs-cosmo 踩坑清单与转化措施

旧代码不直接复制，但必须收集并迁移以下负向测试，避免新项目重复交学费。

| 历史经验 / 具体问题 | 新项目策略 | 最迟验收阶段 |
|---|---|---|
| Windows `os.exec`/子进程错误码、Win32 启动行为差异 | 单独 `ProcessHost`；shell=false、错误阶段、stdout/stderr、路径、子树测试，保存 native error code | M2/M3 |
| Windows exec、共享 cursor、`_COSMO_FDS` 继承坑 | 不复制旧逻辑；保留 old repro fixture，确保快照与子进程重入不破坏 stdin/out | M2/M3 |
| Windows junction/UNC/符号链接对 `stat/lstat/realpath` 的差异 | native identity vs 用户可见文本路径分开；循环、悬空、UNC 全测 | M3/M5 |
| APE loader、ARM64 真机 64KB 页面与 bootstrap header | P0 真机验证；解析 load segments、ELF/PE header，禁止以模拟器替代 | M0/M1 |
| 网络每 socket 线程 + `poll(fd,10ms)` 导致扩展性不佳 | 自有 Reactor、事件通知、固定线程池；连接数不决定线程数 | M2 |
| TLS 老旧 Mbed TLS 2.26.0 与旧 CVE 适用边界 | 不把 Nightly 当修复；专门 TLS 闭包/公告/正负验证，后续 TLS1.3 提案 | M4 |
| TLS 连接固定 8MiB Arena、CA 每连接解析 | 共享只读信任根、可调 allocator、连接内存配额、并发验证 | M4 |
| 默认 CA 资源、资源名冲突、覆盖和破坏 | 固定资源名、manifest/hash 校验、防重复 ZIP 成员和错误覆盖；加载测试不回退宿主随机路径 | M5 |
| QJB v1 先 `malloc` 完整 64MiB snapshot | 新版 QjbReader 按需索引/mmap/pread，不扩容旧整包加载模型 | M5 |
| ZipOS `mmap(/zip/...)` 并非零拷贝 | large asset 读取原始 APE，STORE + 对齐 + `MAP_SHARED` | M7 |
| TTY `setRawMode` 多次所有权/exit 与 PTY 回收问题 | Native TTY lease、stream owner、幂等恢复、错误/信号/Worker 退出强测 | M3 |
| `process.nextTick` 与 Promise、ref/unref 差异 | Scheduler 与事件顺序 Node 差分；beforeExit/exit 独立规则 | M2/M3 |
| QuickJS Worker/SDK/qjsc 入口能力漂移 | 所有入口统一 Context factory、资源/模块/能力边界；SDK host 自己不依赖 Python/Node | M2/M5 |
| Profile/发布 hash、DLL/非受控动态依赖、CA 外部文件问题 | Base/Full/SDK 单一 build provenance、私有闭包、SBOM、输出一致性校验 | M8 |
| Windows SCM 当前缺 import wrapper | 在 `apps/service-manager/native` 尽早做 ABI proof；失败只阻断应用 Windows SCM 能力，不影响 Runtime 默认 APE | **M0-SVC 探针 / M8** |

**迁移规范**：在 `tests/legacy-lessons/README.md` 为每个案例记录来源 old commit、复现预期、目前新实现是否已通过；只搬运可验证输入/预期而不照抄旧代码；若测试需由用户或平台许可运行，单独标注环境依赖和 skip 理由。

---

## 19. 分阶段实施路线、依赖与验收门槛

> **排序原则**：Runtime/SDK 是主线，`qaplet-svc` 是在它之上开发的**第一方系统应用**。为了尽早验证平台价值，服务应用的 **JS-only validate/generate/systemd direct 原型**应在 Node/FS/Process 可用时提前开工，而不是等全部 Native SCM 完成。每阶段 `spec -> probe -> implementation -> negative tests -> cross-host validation -> evidence`。

### M0 — Core APE、工具链与原生风险探针

**Core**：建立全新仓库、QuickJS `master` 与 Nightly cosmocc 输入锁；多 OS/CPU fat APE Hello、ARM64原生runner与缺失页配置的QEMU guest、Windows poll/socket/pthread/ZipOS/proc 基础、许可证与 Build Evidence。**Core 不链接服务代码**。

**M0-SVC（独立 PoC）**：仅在 `apps/service-manager/native/probes` 验证 SCM dispatcher/HandlerEx/SetServiceStatus、Advapi32 import/Win ABI、Job Object 的 Cosmos/APE 可行性。不能把探针失败作为 Runtime 代码回滚理由。

**门槛**：Core APE 按本阶段 native / emulated-system 矩阵实跑、工具链 hash/构建可追溯；服务 SCM ABI 另记独立 blocker/修复方案。

#### M0 的本地探索与持续集成矩阵

日常编码、探针探索、失败复现与小范围回归先在本地运行；CI在提交后复用同一脚本做集成和跨宿主验证，不通过反复提交workflow代替本地调试。

| 场景 | 先用的原生环境 | QEMU补充规则 |
|---|---|---|
| 本地快速验证 | Windows x64 + WSL2 Linux x64，先读取实际页大小 | 通常补一个ARM64 64K完整guest；这是本地快速profile，不宣称覆盖ARM64 4K |
| CI Linux ARM64 | GitHub原生ARM64 Linux runner，如ubuntu-24.04-arm | 读取实际页大小：4K则补64K，64K则补4K；不默认两套guest |
| CI Linux x64 / Windows / macOS | 对应架构托管runner | 运行同一摘要APE与对应探针 |
| 服务应用探索/回归 | 本地可丢弃Windows VM先调试；CI托管Windows VM持续验证 | 无需在托管runner中嵌套Windows VM |

覆盖按(OS,架构,内核页大小,execution_mode)判定，不能只比较页大小。若本地修改涉及ARM64 4K专属问题，可选择本地完整profile增加该guest；本地快速profile的成功不是完整M0矩阵成功。若runner实际页配置既非4K也非64K，明确报告两项缺口，不用它冒充任一目标。

每次运行先采集uname/架构、sysconf/getconf页大小和kernel/image身份。当前官方ARM64镜像清单未给出页大小，不能将4K推定写成实测事实；本机WSL2缺kernel和发行版，尚无法实测。配置就绪后，按测得结果选择补充环境。

QEMU仅补缺失组合，使用完整系统guest和TCG，锁定QEMU、machine/CPU、kernel/config/rootfs；guest内核验架构与页大小后直接运行APE。qemu-user不能替代guest内核，模拟结果标emulated-system。CI原生ARM64的覆盖可作为native证据，模拟结果不外推物理硬件性能或设备兼容。

本地与CI使用相同锁、配方、runner和报告合同；CI构建后分发同一摘要制品，失败也保存日志/串口/报告。Core与M0-SVC门禁独立。SCM/Job先本地隔离VM调试，再在托管Windows临时VM验证权限/真实状态/既有Job限制，清理自己的fixture；重启/自启动等另用可控VM。

当前仅修订执行方案；未配置WSL、购买资源、提交或触发CI。原生runner的页大小将在真实集成运行时记录。


### M1 — QuickJS Engine 与通用 C Embed SDK

实现 Runtime/Context Factory、ESM module hooks、Jobs/Promise、GC/interrupt、基础 CLI；通用 SDK `create/register_host_module/run/destroy`、版本化 ABI/owner thread 规定。**不设计 service-specific SDK 函数**。

**门槛**：纯 C 示例嵌入 JS、ESM/TLA/Promise、create/destroy 10,000 次、API/ABI 自测，`libqaplet-embed.a` 无 SCM/Job imports。

### M2 — Loop / Reactor / WorkPool / Process / SDK Event Bridge

实现通用 `poll` Reactor、TimerHeap、completion、waker、ref/unref、WorkPool、子进程/stdio/Exit、SDK POD event post/request_stop/interrupt；服务专用 stop latch/watchdog 留在 App Native Host。

**门槛**：跨线程投递无 `JS_*` 越权调用；队列满/Runtime destroy/并发 wake 无死锁/UAF；spawn/wait/stream 真实可用；无每连接一线程和固定 busy poll。

### M3 — Node 兼容基础 + 第一阶段 JS Service Manager 原型

**Core**：CJS/ESM/package resolver、Buffer/Stream/Events/FS/Path/Process/TTY、Node 差分、CLI 真实项目（Clack/Terminal-Kit）。

**Service App JS（与之并行）**：`apps/service-manager/src/cli.mjs`、配置按名发现、JSON/YAML/.env、安全 schema、`validate/generate --target systemd`、安全绝对路径、unit diff、`status` 的 systemctl 调用。此时可以通过 `qaplet run` 开发 JS-only 子命令，Windows SCM 功能明确 `unavailable`。

**门槛**：Core 的 Node 差分与 8–12 个声明范围真实纯 JS CLI；Service App 的 JSON/YAML/.env/路径负向用例和 systemd unit 静态生成可用，**尚不宣称 Windows service supported**。

### M4 — 网络 / TLS / HTTP / Fetch / SSE / WebSocket

实现 TCP/DNS、经审计 TLS、HTTP/1.1 client/server、Fetch、SSE、WebSocket、stream 取消/背压。服务应用可在已验证的 `node:net`/`fetch` 上添加 HTTP/TCP health probes，不阻断非 HTTP 模式。

**门槛**：证书/主机名/私有 CA/断网/超时负向安全案例通过；HTTP 服务端和取消可追溯，未完成 TLS1.3/HTTP2 不暗示支持。

### M5 — QJB v1 / 按需资源加载

实现 QJB ZIP 子集、依赖索引、VFS/ESM/CJS/资源、按需 mmap/pread、pack/inspect/verify、ZIP fuzz；验证 **Embed SDK 可直接运行已验证 QJB/JS 应用入口**。

**门槛**：真实 QJB 跨 cwd/离线运行、不整包读入匿名内存、损坏/逃逸成员拒绝、SDK/Worker 映射销毁安全。

### M6 — App Compiler + 可选 Native Host 构建合同

实现普通纯 JS Node 项目 → fat APE；同时将 `apps/service-manager` 的 `Makefile + cosmocc + Embed SDK + QJB/JS resources + private Native Bridge` 构建方法沉淀为**通用 Native Host App 模板**。P1 目标为任意应用可使用 `qaplet compile --host <entry.c>`，但不能先把专用服务构建命令混进 Core。

**门槛**：至少三个普通 CLI/TUI 应用与一个非服务 Native Host 测试 App 可单文件运行；普通 Runtime/APP APE import table **没有 SCM**；服务应用 APE 构建严格独立。

### M7 — 大资源：ZIP64 / STORE / 对齐 mmap

审计原始 APE ZIP64 和 ZipOS，STORE 对齐 mmap、Windows granularity、外置 `assets.qap`、Windows 4GB 限制。

**门槛**：100 MB/1 GB/近 4 GB 与外置 >4 GB 资源测试；低内存/畸形 ZIP/签名/映射回收全部有可验证报告。

### M8 — `qaplet-svc` 系统应用完整交付

**M8-A · JS Application（可从 M3 开始）**：完成 CLI、配置 schema、systemd Direct unit/install/status、日志与诊断、明确的 OS/adapters 能力矩阵。优先产出 Linux 无常驻 Wrapper 的实用应用。

**M8-B · Application Private Native Host（依赖 M0-SVC/M1/M2）**：薄 Windows SCM C 入口、事件 latch/watchdog、私有 `service-manager:native` module、Managed Process Job Object/Unix group、Win32 真实状态/权限和停止兜底。

**M8-C · JS + Native 端到端（依赖 M5/M6；HTTP 健康可选 M4）**：应用 `main.mjs` 使用 SDK 监听 stop；支持 Windows process wrapper、可信 QJB/JS in-process；按名配置、健康/退避、stdout/stderr 轮转；systemd Wrapped 仅作为有需求的备选模式；完整制作和发行单独 `qaplet-svc` APE。

**服务应用门槛**：JS-only 命令确实能通过 Qaplet run 执行；Windows `StartServiceCtrlDispatcher/HandlerEx/SetServiceStatus` 真正通；STOP 队列满/JS 卡死 native watchdog 兜底；受管业务树无遗留；systemd Direct 不引入额外 supervisor；Wrapped 不双重重启；应用使用同一 SDK/APP 构建路径，跨 OS 发布证据完整。

### M9 — Profiles、真实应用、性能、安全和供应链

**Runtime/SDK**：Base/Full/SDK、Node diff、QJB App Compiler、API/ABI/ARM64 64KB、24h soak、证书、SBOM、锁文件、离线复现。

**Service Manager App**：独立 Git SHA、私有 C bridge/JS 功能清单、SDK pin、单文件 APE、Windows 真实隔离 VM + systemd Direct/Wrapped 真实 Linux、状态/进程树/配置权限、长期稳定性、安装/升级/回滚说明。

**门槛**：每个能力声明可追溯真实 Case 和制品 Hash；应用不会强迫 Runtime 默认增加 SCM 库。失败应先区分 SDK bug、Native Host bug 与 JS App bug，不能通过修改内核服务接口绕过症状。

### M10 — v1.0 发布判定（独立但相互验证）

- **Qaplet Runtime/SDK v1**：Core APE、Node/Host、QJB/App Builder 与 SDK 的已承诺能力过关；M4/M7 没通过的子功能明确 `partial/experimental`。**Windows 服务 ABI 并非 Core 发布阻断项。**
- **Qaplet Service Manager Application v1**：必须完成自己的 JS CLI/systemd Direct 与已宣布的平台子集；对标称 Windows SCM 支持的平台额外通过 M0-SVC/M8 Native Host/Job/回调与 watchdog 全链路。未过关不能称 `Windows service verified`，允许 Linux-only pre-release。
- **产品价值验证**：用 `qaplet-svc` 的真实源码和 App Manifest 证明“基于 Qaplet 开发系统应用”的开发量/应用代码占比/需要的核心修补数量，而不是只证明可构建一个定制 C 服务宿主。

### 19.1 依赖与并行关系

```text
M0 APE + Nightly -> M1 Engine/Embed SDK -> M2 EventLoop/Process -> M3 Node/FS/TTY
                                                 |                    |
                                                 |                    +--> M8-A JS CLI + systemd Direct
                                                 |                               |
                                                 v                               v
M0-SVC SCM ABI spike -> M8-B private C Host + Job Object ------------------> M8-C JS + Native
                                       ^                                   ^
                                       |                                   |
                                 M1/M2 SDK + Process                     M5 QJB / M6 App Build
                                                 |
M4 Net/HTTP/TLS -> optional health probes -------+
M5 QJB -> M6 App Compiler -> M7 Large Assets

Core release: M0–M6 declared base + M9 Core -> M10 Runtime/SDK v1
App release:  M8-A/B/C platform-defined gate + M9 App -> M10 Service Manager App v1
```

M8-A 可以独立较早交付、继续帮助检验 Qaplet 缺失的 FS/CLI/进程 API；M8-B 的 Windows ABI 风险始终隔离在应用 Native Host。保持所有约束在 ADR/SDK/应用 Spec/CI 上有测试而不是仅写入计划。

---

## 20. 首轮 Engineering Epics / 可验收 Issues

每个 Issue 包含 `Goal / Scope / Out of Scope / Source Files / Tests / Negative Cases / Evidence / Rollback`。**App-First 原则**：`APP-SVC-*` 中，JS-only 的功能先以 `qaplet run` 验证；只有严格必要的 OS 特有功能进入该 App 的 `native/`。不能把 service 功能写成 Core `src/host/service/`。

| ID | 事项 | 前置 | 具体验收 |
|---|---|---|---|
| BOOT-01 | 新仓库、许可、AGENTS、Makefile、CI skeleton | 无 | foundation 能构建/测试 |
| BOOT-02 | 锁 QuickJS master 与补丁清单 | BOOT-01 | 源码 SHA/补丁 provenance |
| BOOT-03 | 锁 cosmocc Nightly 归档与工具链 | BOOT-01 | 离线可复现、Hash 完整 |
| BOOT-04 | 多 OS/CPU fat APE Hello | BOOT-02/03 | 同一 Core APE 按 native / 全系统 QEMU 矩阵运行 |
| BOOT-05 | 应用私有 Windows SCM/Job ABI PoC | BOOT-03/04 | Windows 真 SCM 测试；不影响 Core |
| BOOT-06 | Linux ARM64 64 KB 页深度探针 | BOOT-04 | 原生优先，不足时QEMU补充；实测64K和loader/thread/mmap/ZIP，记录执行方式 |
| BOOT-07 | Windows/Unix Socket、TTY、Pipe/HANDLE 探针 | BOOT-04 | 无静默语义假设的矩阵 |
| CORE-01 | Runtime/Context Factory、最小 ESM CLI | BOOT-04 | TLA/Promise/import/销毁合同 |
| CORE-02 | Loop/Timer/Reactor/WorkPool | CORE-01 | wake/fd generation/ref-unref/无 busy-spin |
| CORE-03 | 通用 Process/stdio/TTY | CORE-02 | 真实 spawn/exit/Unicode/PTY/ConPTY |
| SDK-01 | 稳定 C Embed SDK/runtime lifecycle | CORE-01 | 非服务 C App 可嵌入 JS |
| SDK-02 | host module、POD event/post/wake/cancel | CORE-02/SDK-01 | 跨线程/UAF/过载/关闭竞态 |
| MOD-01 | Node capability matrix 与 Oracle | CORE-01 | test evidence/hash/已知差异 |
| MOD-02 | ESM/CJS/package resolver | CORE-01 | package exports/imports/循环依赖 |
| MOD-03 | FS/Path/Buffer/Stream/Events/Process/TTY | MOD-01/CORE-03 | Clack/Terminal-Kit core cases |
| NET-01 | TCP/经验证 TLS | CORE-02 | CA/SNI/hostname/error/并发 |
| NET-02 | HTTP1/Fetch/WebSocket/SSE | NET-01 | HTTP 流/背压/取消/负向安全 |
| PKG-01 | QJB ZIP/恶意输入/配额 | CORE-01 | 格式与安全 corpus |
| PKG-02 | QJB/VFS/SDK mount | PKG-01/MOD-02 | 不整包复制、销毁安全 |
| PKG-03 | pack/inspect/verify | PKG-02 | 可重复产物/哈希/签名 |
| PKG-04 | 普通 App Compiler fat APE | PKG-03 | Node CLI 多 OS 单文件 |
| PKG-05 | 通用 SDK 自定义 Native Host App Template | SDK-01/PKG-03 | **非服务**样例 App + Service App 同一构建机制 |
| LARGE-01 | 原始 APE mmap/STORE/alignment | PKG-01/BOOT-06 | 1GB/RSS/64K/畸形负向 |
| LARGE-02 | >4GB 外置 `assets.qap` | LARGE-01 | Windows 边界和资源签名 |
| APP-SVC-01 | **JS Config/Schema/CLI validate** | MOD-03 | `qaplet run` 可执行 JSON/YAML/.env 解析、dry-run |
| APP-SVC-02 | **JS systemd Direct unit 生成/安装/状态** | APP-SVC-01/CORE-03 | 真 systemd 直接管理业务进程、无双重 supervisor |
| APP-SVC-03 | Windows app-private SCM Native Host | BOOT-05/SDK-01 | ServiceMain/HandlerEx/真实 SCM 状态，回调无 JS |
| APP-SVC-04 | Private stop latch/event/watchdog | SDK-02/APP-SVC-03 | 队列满、JS 卡死、重复 STOP 可有界兜底 |
| APP-SVC-05 | Private Managed Process Job/Unix group | CORE-03/APP-SVC-03 | Java/Python/Go/EXE 子树强杀/回收 |
| APP-SVC-06 | **JS Controller/Health/Logs/Restart Policy** | APP-SVC-01/CORE-03，NET-01/APP-SVC-05 按探针与运行模式可选 | owner thread 控制策略、重启/日志/健康负向测试 |
| APP-SVC-07 | service-manager native module + SDK link + App APE | PKG-05/APP-SVC-03/06 | `qaplet-svc` 单文件应用；无默认 Core service imports |
| APP-SVC-08 | systemd Wrapped/SysV/OpenRC，launchd 后续 | APP-SVC-01/06 | Direct 与 Wrapper 语义区分、单一权威 |
| APP-SVC-09 | service app 24h soak、安全与回滚 | APP-SVC-02/04/07 | SCM/systemd/Job/环境密钥/日志完整证据 |
| REL-01 | Core Runtime/SDK release | 对应 Core/SDK 模块 | 独立 manifest/SBOM/ABI/Hash |
| REL-02 | 第一方 Service Manager App release | 对应 APP-SVC-* | 独立版本/能力矩阵/真机证据 |

### 20.1 推荐首轮并行工作包

**平台基础**：BOOT-03/04、BOOT-06/07、SDK-01/02；**应用证明**：APP-SVC-01/02（JS CLI/systemd Direct）；**高风险 OS 验证**：BOOT-05（SCM ABI PoC）。这样的顺序保证：即便 Windows SCM Import 适配遇阻，Qaplet 仍可先通过真实 JS 服务管理器的 Linux 管理/配置工作流验证应用价值。

### 20.2 工作量估算（工程人时量级、非工期承诺）

| 工作块 | 粗估人时 | 主要不确定性 |
|---|---:|---|
| M0/M1 APE/QuickJS/SDK 基础 | 120–240 | Cosmocc 版本、ARM64 Loader、SDK ABI |
| M2 Reactor/Process/SDK Thread Event | 160–300 | Windows I/O 与关闭/销毁竞态 |
| M3 Node/FS/CJS/TTY | 220–440 | package 兼容长尾、Node Oracle |
| M4 网络/TLS/HTTP | 200–420 | TLS 库、互操作、安全审计 |
| M5/M6 QJB/App Compiler/Native Host 通用构建 | 240–450 | 静态链接 Native App、ZIP/签名 |
| M7 大资源 | 100–200 | Windows 4GB、64KB mmap |
| **M8 Service Manager JS App + private C Bridge** | **180–360** | SCM/Job、停止竞态、完整 App CI；C 比例须实测而非假定 |
| M9 Core/App 各自真实 CI、安全与供应链 | 180–360 | VM/系统权限/长时间 soak |

以上是项目规划的粗量级，不是已经完成的工时统计或准确交付预报；M8 的多数业务特性应首先用 JS 实现，并记录为弥补运行时能力而产生的 Core changes 数量。

---

## 21. 高风险问题登记簿（Risk Register）

| 风险 | 可能后果 | 预防 / 纠偏 | 阻断点 |
|---|---|---|---|
| Nightly cosmocc 滚动内容/工具链与源码不同步 | 无法复现或引入未知 ABI | 锁定归档 SHA/供应链证据、独立 bump PR | M0 |
| QuickJS 主干与 Cosmo nightly 不兼容 | 编译失败/运行时崩溃 | 零补丁 probe、最小 overlay、独立回归 | M0/M1 |
| **把服务管理器当作 C SDK 扩展框架，而非 JS 应用** | 应用逻辑重复写 C，失去验证 Qaplet 应用平台价值 | JS CLI/config/health/logging 为源代码权威，C 仅 OS 最小桥；审查 `apps/service-manager` 代码占比 | M3/M8 |
| **systemd Direct 与 Wrapper 模式混淆** | 重复 supervisor、错误报告 readiness、错配 cgroup 终止 | Schema 明确 `direct|wrapper`，两种模式分开真机验收 | M8 |
| **服务代码泄漏进 Kernel** | core 体积膨胀/ABI 污染/架构耦合 | `apps/service-manager/native -> Embed SDK -> Core` 单向依赖 + linker import gate | M1/M8 |
| **C SDK ABI 与 Service Manager App 版本不匹配** | 崩溃/事件丢失 | struct_size/version/feature negotiation/compile+runtime probe | M1/M8 |
| Win32 SCM import 缺失 | `qaplet-svc` App 无法作为 Windows Service 运行 | 独立 M0-SVC spike + ABI import/thunk 验证 | **仅 M8/Service** |
| **HandlerEx 调用 JS 或等待 JS** | SCM 控制卡死、系统关机超时 | C callback 快路径、POD 事件投递、回调时长 gate | M8 |
| **普通事件队列满导致 STOP 丢失** | 服务永远不能停止 | atomic stop latch / 高优先槽 / native watchdog | M2/M8 |
| **JS owner 阻塞且 Native Host 没有兜底** | 无法停机/强杀/留下子进程 | 独立 native 单调 deadline + Job/process-group 强清理 | M8 |
| Job Object 子树逃逸或旧 OS 非嵌套限制 | 服务卸载后残留子进程 | CREATE_SUSPENDED→Assign→Resume、breakaway 检测、能力门禁 | M8 |
| SCM 状态与 JS readiness 混淆 | 假 RUNNING/错误告警 | 双状态机/单调状态映射/真实 probe | M8 |
| systemd + JS 双重 restart | 重启风暴/日志爆量 | OS/host 权威互斥、安装生成器校验 | M8 |
| 配置自动发现遇别名/路径逃逸 | 加载错误配置或泄露秘密 | 真实 installRoot、服务名白名单、symlink/junction/ACL 检查 | M8 |
| Windows socket/console/pipe 无统一 poll | Loop 兼容缺陷/忙等 | 通用 OS adapter + 完成队列 | M0/M2 |
| ARM64 64KB page/APE loader | Linux 真机启动失败 | 真机探针和发布门禁 | M0 |
| TLS 旧版依赖/安全公告 | CVE/证书验证错误 | 独立 TLS 审计与负向校验 | M4 |
| QJB ZIP bomb/路径逃逸/越界 | OOM/任意文件访问 | 严格 parser/长度/hash/fuzz | M5 |
| `.node` addon / 动态 require | 打包不完整 | Node 能力矩阵、allowlist、报告 | M3/M6 |
| 大资源 mmap 变成匿名复制 | RSS 暴涨 | 原始 APE STORE/对齐/pread fallback | M7 |
| Windows >4GB APE | 无法运行 | 外置 `assets.qap` | M7 |
| 签名/ZIP EOCD/APE 改写冲突 | 运行或验签失败 | 签名序列实测、Managed Distribution 分列 | M6/M9 |
| 错误宣称 QJB sandbox | 不可信脚本获得 OS 权限 | OS 帐户隔离、明确权限边界 | M3/M5/M8 |
| 无真实 Windows/macOS/ARM64 CI | 跨平台承诺失真 | Core/Service 独立 Tier CI 与证据 | 全部 |

### 21.1 不可接受的捷径

绝不通过以下方式宣称完成：把各平台普通二进制 ZIP 成伪单 APE；让默认 `qaplet` 链入 SCM 以方便编写应用；在 `HandlerEx` / 信号处理器中执行 JS、等 JS Promise 或同步关闭管道；把 STOP 放入可满的普通队列而无兜底；仅由 JS 决定卡死后的强制终止；在 Windows 静默用单 pid kill 冒充可靠子进程树清理；用 `nohup/start /b` 冒充 SCM；跳过系统真实状态直接报告 READY；采用隐式 Shell 执行配置；为过测试禁用 TLS；用 qemu-user 冒充64KB guest内核，或将全系统QEMU结果冒称物理ARM64验证；用服务应用私有桥接的成败倒逼 Kernel 发布；没有证据就把 capability 标记为 verified。

---

## 22. 文档、能力矩阵和质量门禁的组织

单一 `docs/INDEX.md` 提供平台文档入口；服务管理作为**第一方应用**有自己的 App 文档及能力矩阵，而不是新增 Core `service-manager-v1` 规范，更不能和 `src/host` 组成双重权威。

```text
docs/
├── INDEX.md
├── PRODUCT.md
├── ARCHITECTURE.md
├── adr/
│   ├── ADR-001-ape.md
│   ├── ADR-012-service-manager-is-an-app.md
│   ├── ADR-013-native-callback-boundary.md
│   ├── ADR-017-embed-sdk.md
│   ├── ADR-020-systemd-direct-first.md
│   └── ADR-021-native-host-app-build.md
├── specifications/
│   ├── host-loop-v1.md
│   ├── embed-sdk-v1.md
│   ├── node-compat-v1.md
│   ├── module-resolution-v1.md
│   ├── qjb-v1.md
│   ├── app-ape-v1.md
│   ├── native-host-application-v1.md  # **任意**应用的原生宿主/自定义入口
│   └── large-assets-v1.md
├── security/{threats,tls-admission,source-lock,release-signing}.md
└── delivery/{platforms,compatibility,SDK-ABI,release-criteria}.md

apps/service-manager/docs/
├── INDEX.md
├── app-architecture-v1.md
├── native-scm-bridge-v1.md
├── managed-process-v1.md
├── event-queue-watchdog-v1.md
├── service-config-v1.md
├── systemd-direct-and-wrapped-v1.md
├── windows-operations.md
├── service-capabilities.json
└── app-release-acceptance-v1.md
```

- 平台 `capabilities.json` 记录 JS/Node/SDK/Native Host App Builder 合同；**不**把 Windows SCM 作为所有 Qaplet Runtime 的通用能力。
- 服务管理器 `service-capabilities.json` 记录 `windows-scm/systemd-direct/systemd-wrapped/openrc/sysv/launchd`、API 版本、真实证据、缺口、`appVersion/qapletVersion/sdkAbiVersion`。
- 文档/程序漂移自动检测：SDK 头文件和 ABI、`qaplet --help`、`qaplet-svc --help`、应用 schema、C import table、实际运行状态、release manifest 与 CI fixture 对照。服务应用特有变更不应强迫发布一个新的 Core ABI 版本。
- `apps/service-manager` 作为真实 App 参与 Qaplet release 的**回归测试样本**；但其平台专用功能的支持门槛与 Core 自身 release gate 分离。关键缺口进入 backlog，并区分 Runtime bug、SDK bug、App JS bug、App C Bridge bug。
- Agent 可以使用 Dairn 类质量保障流程，但 Qaplet/`qaplet-svc` 不要求部署 Dairn 才能执行；Makefile/CI/Schema/签名证据是最终可复验事实。

---

## 23. v1 产品验收清单（发行前逐项勾选）

### 23.1 Qaplet Runtime / C Embed SDK

- [ ] Bellard QuickJS 实施时的 `master` SHA 已锁，Cosmopolitan Nightly 工具链 digest/来源可复验，离线可构建。
- [ ] Core APE 已在声明的 OS/CPU 真机/VM 实跑，Linux ARM64 64KB 页面通过关键测试。
- [ ] Reactor/Worker/Process/TTY/Loop/Node/Web 兼容矩阵只声明已验证能力，`qaplet` 默认不含 SCM/service 配置及 Job Object 私有导入。
- [ ] 公开 `libqaplet-embed.a`、稳定版本化 C ABI/owner thread 合同、host module 注册、POD 事件投递与关闭/取消测试。
- [ ] **非服务管理** C Native Host 示例能通过 SDK 编译/嵌入/执行 JS 并安全退出，证明这是通用 SDK 而不是服务专用 SDK。
- [ ] Node CJS/ESM/FS/TTY/Stream/Process 的真实应用与 Node Oracle 差分证据可复现。

### 23.2 QJB / App Compiler / 大资源

- [ ] QJB 格式有恶意 ZIP corpus、严格路径/重叠/大小验证，运行时无整包匿名加载。
- [ ] `qaplet pack/compile` 至少把三个真实纯 JS CLI/TUI 做成声明跨 OS 的 APE，且离线不用 Node/npm/Python。
- [ ] App Builder 已能正确检测不支持 `.node`/动态 require 的情况，不靠静默联网解决缺依赖。
- [ ] 应用可通过**通用 Native Host Template 或 SDK 手动链接**构建自带原生入口的 APE；`qaplet-svc` 不是唯一受支持示例。
- [ ] 大资源 STORE/对齐 mmap 与 Windows >4GB 外置 `assets.qap` 能力按实测声明，未完成标实验性。

### 23.3 `qaplet-svc`：**一款 Qaplet Application**（独立发行门槛）

- [ ] 服务管理器代码主体为 `apps/service-manager/src` 下 JS/ESM，JSON/YAML/.env/配置发现/CLI/systemd 生成/日志/健康/恢复等主要逻辑不在 C 中重复实现。
- [ ] `qaplet run apps/service-manager/src/cli.mjs ... validate/generate` 的 JS-only 功能无需私有 SCM C bridge 即可开发/测试。
- [ ] `qaplet-svc` 通过**同一个 SDK + App Builder/Makefile 链接途径**构建独立 APE，可单独下载或随业务安装包发行；用户机不需另装 `qaplet`。
- [ ] Windows Native Host 仅承担 SCM 状态/事件、Managed Process、Watchdog 等不可替代操作；HandlerEx 及 Unix 异步信号处理器不调用 JS。
- [ ] `StartServiceCtrlDispatcherW`、SCM 安装/启动/查询/停止/删除在真实 Windows VM 通过；状态与 `QueryServiceStatusEx` 一致，STOPPED 只报告一次。
- [ ] Native STOP latch + SDK wake 在普通队列满、重复 STOP、JS 线程卡死和 runtime 销毁竞态下仍有界兜底；强制终止与失败状态真实可见。
- [ ] Windows Job Object / Unix wrapper process group 真实清理可管理的 Java/Python/Go/EXE 子树；平台例外/权限/句柄继承明确检测。
- [ ] 按服务名加载同名配置文件、受限环境文件；路径/歧义/YAML 不安全 tag/ACL/密钥/命令行权限测试通过。
- [ ] Windows Wrapper、Linux systemd Direct/可选 Wrapped 分别测试，Direct 不启用额外常驻 supervisor，避免双重重启和错误的 readiness 承诺。
- [ ] status JSON 区分 manager/app/child/health 状态及证据来源，欠缺能力返回 unsupported/not_applicable，而非成功空值。
- [ ] `qaplet-svc` 有应用级语义版本、能力矩阵、SDK lock、SBOM/许可证、二进制 hash、24h soak/负向用例/安装卸载/回滚清单。
- [ ] 真实记录此 App 在开发中遇到的 Qaplet API/SDK 缺口和修复数量，作为 Runtime 平台价值的反馈指标。

### 23.4 安全 / CI / 发行

- [ ] TLS 默认验证证书/主机名/CA；安全差异有负向案例，不通过则不发布已声明网络能力。
- [ ] Core/SDK 与 Service Manager App 产物分别有锁文件、build evidence、CI results、licenses/SBOM、source provenance、支持矩阵。
- [ ] 所有已承诺 OS/CPU 真机/VM 有性能/内存/长期稳定性/权限边界证据，APE/hash/签名变体归属清楚。
- [ ] Service App 的 Windows SCM 特性失败**仅**阻断其 Windows 平台的服务支持，不作为 Runtime 强制发布阻断；但 SDK/App Builder 回归失败仍按其通用能力合同处理。

---

## 24. 本计划所依赖的主要源码与规范索引

以下延续前版调研时收录的来源；**本次更新主要是基于用户已确定的 App-first 架构修订文档，未重新对上游 HEAD 做实时核验**。链接中的 `blob` 文件未来可继续变化，实施时应以上方记录的 commit/Tag 回溯并重新测试。本文关于 Nightly `cosmocc.zip` 的**实际归档 SHA 尚未实测**，必须在 M0 记录；本文不是一份现成编译成功的实现报告。

### QuickJS / Cosmopolitan / APE

1. [Bellard QuickJS GitHub](https://github.com/bellard/quickjs)；[本次 master 提交](https://github.com/bellard/quickjs/commit/19d6bd865e709e2b5717fb4f2290c0cecc57ee4b)。
2. [Cosmopolitan](https://github.com/jart/cosmopolitan)；[本次 master 提交](https://github.com/jart/cosmopolitan/commit/aefad27c09ffda64c6ac64bd69f758e9f9c7b91d)。
3. [Nightly cosmocc 官方获取通道](https://cosmo.zip/pub/cosmocc/cosmocc.zip)；[Cosmopolitan 官方构建文档](https://github.com/jart/cosmopolitan/blob/master/tool/cosmocc/README.md)。
4. [APE 格式与平台限制](https://github.com/jart/cosmopolitan/blob/master/ape/specification.md)，含 x64/ARM64、页大小与 Windows 64K 映射对齐约束。
5. [Cosmopolitan ZipOS `get`](https://github.com/jart/cosmopolitan/blob/master/libc/runtime/zipos-get.c)、[`open`](https://github.com/jart/cosmopolitan/blob/master/libc/runtime/zipos-open.c)、[`mmap`](https://github.com/jart/cosmopolitan/blob/master/libc/runtime/zipos-mmap.c)。
6. [Cosmopolitan 定制 Mbed TLS 说明](https://github.com/jart/cosmopolitan/blob/master/third_party/mbedtls/README.cosmo)（当前仍标记基于 upstream v2.26.0）。

### 工程参考与兼容治理

7. [txiki.js 主仓库](https://github.com/saghul/txiki.js)；[本次提交](https://github.com/saghul/txiki.js/commit/e6b651c81eefb084c6fc99960c17cb15a13563eb)；[VM](https://github.com/saghul/txiki.js/blob/master/src/vm.c)、[Timers](https://github.com/saghul/txiki.js/blob/master/src/timers.c)、[TLS](https://github.com/saghul/txiki.js/blob/master/src/mod_tls.c)、[Worker](https://github.com/saghul/txiki.js/blob/master/src/worker.c)、[HTTP Client](https://github.com/saghul/txiki.js/blob/master/src/httpclient.c)。
8. [Redis `ae.c` 思想参考](https://github.com/redis/redis/blob/unstable/src/ae.c)、[Redis 官方许可证分版本表](https://redis.io/legal/licenses/)；如需要借鉴早期代码应核查 `7.2` BSD-3-Clause 来源。
9. [LLRT 主仓库](https://github.com/awslabs/llrt)、[LLRT Compatibility Matrix README](https://github.com/awslabs/llrt/blob/main/README.md)、[LLRT Modules](https://github.com/awslabs/llrt/blob/main/llrt_modules/README.md)。
10. [历史 quickjs-cosmo](https://github.com/moorefu/quickjs-cosmo)、[Node 规划](https://github.com/moorefu/quickjs-cosmo/blob/main/docs/NODE_COMPAT_RUNTIME_PLAN.md)、[QJB 旧规格](https://github.com/moorefu/quickjs-cosmo/blob/main/docs/QJB_FORMAT.md)、[TTY](https://github.com/moorefu/quickjs-cosmo/blob/main/docs/TERMINAL_CONTRACT.md)、[TLS 审核](https://github.com/moorefu/quickjs-cosmo/blob/main/docs/M5_TLS_AUDIT.md)、[发布 profile](https://github.com/moorefu/quickjs-cosmo/blob/main/docs/RELEASE_PROFILES.md)。

### 大资源与 OS 服务

11. [llamafile](https://github.com/mozilla-ai/llamafile)；[原始 executable 内 ZIP/模型 mmap 实现](https://github.com/mozilla-ai/llamafile/blob/main/llamafile/llamafile.c)；[外置大权重/Windows 4GB 限制](https://github.com/mozilla-ai/llamafile/blob/main/docs/quickstart.md)。
12. [zipalign](https://github.com/jart/zipalign)；其 README 描述对齐未压缩 ZIP 条目，默认 65536 字节对齐，并使用 ZIP64。
13. [Microsoft Service Control Manager](https://learn.microsoft.com/en-us/windows/win32/services/service-control-manager)；[`StartServiceCtrlDispatcherW`](https://learn.microsoft.com/en-us/windows/win32/api/winsvc/nf-winsvc-startservicectrldispatcherw)、[`RegisterServiceCtrlHandlerExW`](https://learn.microsoft.com/en-us/windows/win32/api/winsvc/nf-winsvc-registerservicectrlhandlerexw)、[`SetServiceStatus`](https://learn.microsoft.com/en-us/windows/win32/api/winsvc/nf-winsvc-setservicestatus)、[Service Control Handler Function](https://learn.microsoft.com/en-us/windows/win32/services/service-control-handler-function)。控制回调快速返回、STOPPED 只报一次与真实 checkpoint 是 Native Host 验收依据。
14. [Microsoft Windows Job Objects](https://learn.microsoft.com/en-us/windows/win32/procthread/job-objects)；Job 的进程树约束、`JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE`、旧版本非嵌套限制需要真实验证。
15. [systemd.service](https://www.freedesktop.org/software/systemd/man/latest/systemd.service.html)、[sd_notify](https://www.freedesktop.org/software/systemd/man/latest/sd_notify.html)；只在业务真正就绪后发送 READY，其他状态含义按 OS 区分。

---

## 25. 最终实施决议（供新仓库冻结）

**平台定位**：

> Qaplet = Portable JavaScript Runtime & Application Platform。基于 Bellard QuickJS 主干、锁定的 Cosmopolitan Nightly `cosmocc`，提供真正可携带的 APE Runtime、ESM/CJS/Node/Web API、Host Loop/Process、QJB、应用编译器与**通用 C Embed SDK**。平台不默认内嵌任何具体业务场景的服务管理代码。

**第一方示范应用定位**：

> `qaplet-svc` = **A Service Manager Application Built with Qaplet**。它是由 Qaplet 开发、构建、发行的系统级应用，不是 Qaplet Kernel 模块，也不是与 Runtime 并列的第二套原生服务框架。服务配置、CLI、systemd Unit 生成、安装编排、健康、日志、恢复策略以 JavaScript/ESM 实现；仅 Windows SCM 入口、进程/Job 对象、原生停止事件、watchdog/强制回收使用**应用私有薄 C Host Bridge**，并通过通用 SDK 嵌入 Qaplet Runtime。

**部署选择**：Windows 在真实 SCM 中运行 `qaplet-svc` Native Host，并把控制事件通过线程安全 latch/SDK Loop 投递给 JS；原生兜底始终独立于 JS 及时响应。Linux **默认由该应用生成配置并让 systemd 直接监管业务**，仅在需要 JS 持续策略时运行 Wrapped 模式。支持按业务服务名发现 YAML/JSON/.env，并随 Java/Python/Go/EXE/QJB 安装包发行。

**工程顺序**：先使 Core/SDK 满足通用宿主合同；与此同时用 JS-only `validate/generate/systemd-direct` 实现首款真实应用，尽早暴露 Node/FS/Process/CLI 缺口。Windows SCM/Job ABI PoC 是应用级并行高风险事项；随后通过静态 SDK、通用 Native Host App 构建模板交付 `qaplet-svc` fat APE。

**发布边界**：`Qaplet Runtime/SDK` 与 `qaplet-svc Application` 各有独立版本/签名/能力矩阵/真实机器验收；不能靠 Service C 特例冒充平台能力，也不能把服务应用失败无条件升级为 Core 阻断。最终评价 Qaplet v1 的指标不仅是 CLI/API 数量，而是能否采用**JS 为主、C 仅薄桥接**的方式开发和持续交付 `qaplet-svc` 这种真实系统级应用。

