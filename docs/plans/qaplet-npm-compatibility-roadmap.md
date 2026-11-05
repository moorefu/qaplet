# 真实 npm CLI/TUI 兼容路线

2026-11-05 直接读取 npm registry 的版本 metadata 和原发布 tarball，按 registry SRI 校验后只读检查 JS import/process 使用；未运行 install script、包或 native addon。缓存位于主 `.cache/downloads/npm/`，原 metadata/导入清单位于本次 `out/efficiency/npm-primary/`。这是需求调查，不是 Qaplet 兼容通过。

## 选择及可复现输入

| 顺序 | 调查的实际发布版本 | tarball SHA-256 | 当前 Qaplet 状态 |
|---|---|---|---|
| 1 | `@clack/prompts@1.8.1`，直接依赖 `@clack/core@1.5.1` | `a4e1e70bf4bd35e82a76a3fc1dd7ab0081d0a885dd80b81101310fe31c3e2d0b` | 未运行，缺 Node 模块解析与 stdio/TTY 兼容 |
| 1a | `@clack/core@1.5.1` | `256c7ba43e14da5e3bc5e79870f7e95bcbd4eb7831f263fd11f5666b44d020dd` | 未运行 |
| 2 | `@earendil-works/pi-tui@1.5.0` | `327d398a3f2b288588345ca72a2b2d7a8b2942f1702a14be657cd745535ba762` | 未运行；不是完整 pi coding agent |

发布 metadata：Clack 要求 Node >=20.12.0，pi-tui >=22.19.0。这是上游需求，不是 Qaplet 声明 Node 等级。Clack 发布包的 sisteransi 依赖范围与 main 分支 package.json 不同；pi 的旧 main 缓存也曾显示 0.85.1，不能用 mutable main 或搜索摘要代替实际发布包。后续应用首次验收应固定全部传递依赖、原件 SRI、许可证和 bundle 输入；本次未把上述候选写入产品工具链锁。

依据：[Clack 发布 metadata](https://registry.npmjs.org/@clack%2fprompts/1.8.1)、[core metadata](https://registry.npmjs.org/@clack%2fcore/1.5.1)、[pi-tui metadata](https://registry.npmjs.org/@earendil-works%2fpi-tui/1.5.0)。

## 从实际包反推接口

Clack prompts 发布 JS 导入 `node:fs/path/process/util`，core 导入 `node:process/readline/tty/util`。pi-tui 的发布 JS 含 `node:child_process/fs/module/os/path/perf_hooks/url`；native build script 的 import 单独记录，不当作运行必需面。下面对具体场景做增量实现，不承诺整个导入模块。

| 能力 | Clack 向导所需面 | pi-tui 增量 | 当前/实现及验收约束 |
|---|---|---|---|
| package/ESM | package exports、bare specifier、发布 `.mjs` | 发布 ESM 与传递依赖 | 当前仅文件 ESM/白名单；M3 实现解析。开发可外部 bundle，必须保留真实包代码/依赖清单，不能替换成手写假 Clack |
| process | env/platform/cwd、stdin/stdout、exit、事件监听/移除 | arch/argv/pid/execPath/nextTick/kill | 当前 Runtime 快照/定时器仅有部分基础；Node 对象、优先级、退出语义需独立负测，不能把 qaplet API 改名即称兼容 |
| stdio/stream/events | write、resize、keypress、on/off、输入恢复 | stdin setEncoding/resume/data/removeListener，stdout write/resize | M2 17/18 后提供借用句柄、安全写入、背压和清理；Node stream facade/事件行为属 M3，EOF 不当空数据 |
| tty/readline | ReadStream instanceof/isTTY/setRawMode；emitKeypressEvents/createInterface 等实际调用 | raw 状态保存/恢复、终端输入解析 | 真实 Linux PTY、Windows Console/管道重定向、Ctrl-C/取消/异常均恢复原模式；无 TTY 必须明确降级或报错 |
| util | 发布代码使用 styleText 等着色能力 | 视具体组件继续收集 | 先实现纯 JS leaf 及色彩策略，不伪造整个 node:util |
| fs/path | path prompt 的目录建议；基础向导可先不启用 path prompt | debug/log、资源文件和终端图像相关路径 | 通用文件 API、路径/编码/权限错误；不用交互基本成功冒称文件场景通过 |
| Buffer/Unicode | UTF-8/ANSI/宽度依赖与分块输入 | 图像/剪贴板、输入解码及更多 Unicode | QuickJS Uint8Array 不等于 Node Buffer；双宽字/组合字符/拆分 UTF-8 和粘贴必须实际测试 |
| timers/performance | spinner/异步 prompt、AbortSignal 取消 | render 调度、setImmediate、performance.now | 复用通用 Loop；当前 timers 可作基础，Node facade/AbortController/clock 语义未验 |
| child_process/native | 首个向导不需要 npm 安装或子进程服务 | 可选剪贴板/平台 helper、createRequire/native addon 路径 | 通过通用 Process 支持明确子集；N-API/.node 当前不支持，不能为演示静默伪造。必须检查包是否可靠 fallback，失败时明确 UNSUPPORTED |

静态导入存在不等于该场景会执行，也不能证明未扫描的动态调用不存在。正式应用验收需加载完整固定依赖图并捕获首次不支持的模块/API；逐项补接口，每个接口同时有合法行为、错误、资源上限和恢复合同。

上游实现参考：[Clack prompt](https://github.com/bombshell-dev/clack/blob/main/packages/core/src/prompts/prompt.ts)、[Clack TTY 辅助](https://github.com/bombshell-dev/clack/blob/main/packages/core/src/utils/index.ts)、[pi terminal](https://github.com/earendil-works/pi/blob/main/packages/tui/src/terminal.ts)。这些 main 链接只供定位，需求清单依据上述固定发布包。

## 可交付的纵向场景

1. **文件 CLI**：真实 npm 参数解析包 + 读写文件 + Promise；正常退出、参数错误、权限/路径失败、stderr、非 TTY 重定向。Linux/Windows 同供应字节。
2. **Clack 向导**：intro → text/confirm/select → 写配置 → outro；取消符号、Ctrl-C、验证失败、EOF、resize、Unicode、异步 spinner、关闭与原终端恢复。先不启用 path prompt，随后以 fs 子集扩展。录制真实输入/输出与退出，不用截图替代行为。
3. **pi-tui 最小编辑器**：真实 Text/Input/Select 组件、差量重绘、粘贴/方向键、resize/退出恢复。先声明不支持的图像/剪贴板/native addon 范围；若 package 顶层加载无法避开该面，场景保持 blocked，不能删掉上游 import 假称原包成功。
4. **Portable App Alpha**：将已工作的 CLI/向导打成 QJB/APE，换目录和无 Node 环境运行，同一 APE 在 Linux/Windows 通过；包篡改、缺资源、路径越界和不支持接口均真实失败。macOS/ARM64 在 D 里程碑补真实执行。

每个场景保留 `implemented/local/Linux/Windows/other/full/release` 各轴；当前上述四个场景全部未验收。开发继续使用同一 Runtime、通用 Host/Loop/Process/TTY 与公开 SDK；不增加 Clack/Pi 专用 Core，不扩成完整 Node、Rust Runtime、GUI 或服务平台。
