# 08: Windows/Unix 原生 I/O 行为基线（BOOT-07）

**What to build:** 维护者得到后续 Reactor/Process/TTY 设计所依据的真实系统行为，而非假定所有句柄都能统一 poll。

**Blocked by:** [05 · 构建 fat APE 与最小 QuickJS 探针](05-fat-ape-build.md)。

**Status:** ready-for-agent

**Execution prerequisites:** 本地Windows/WSL/QEMU先探索，CI对应原生runner与缺失ARM64页配置的QEMU guest回归，区分native/emulated-system；TTY/PTY/ConPTY由fixture创建，无人值守runner缺少的交互设施显式记录。

## Goal

维护者得到后续 Reactor/Process/TTY 设计所依据的真实系统行为，而非假定所有句柄都能统一 poll。

## Scope

编译并执行 socket readiness/半关闭、pipe/EOF、TTY/Console 检测与恢复、基本 spawn/wait/退出码/Unicode路径、pthread/ZipOS 必要基线。形成逐句柄、逐OS的结果与已验证限制；M0只测原生合同。

## Out of Scope

QapletLoop、可取消WorkPool、完整ProcessHost、Node绑定、网络TLS、性能优化backend、每连接一线程方案。

## Source Files

native socket/pipe/process/TTY探针、宿主适配runner、行为基线矩阵和清理逻辑。

## Acceptance criteria

- [ ] 各类句柄/OS明确区分通过、受验证的限制、测试失败和未测；受验证限制有实际行为依据。
- [ ] socket、pipe、TTY与进程至少覆盖成功、EOF/关闭和错误退出；有timeout防止探针无限挂住。
- [ ] 进程路径/参数不借隐式shell规避问题，记录Unicode/空格路径结果。
- [ ] 探针恢复终端、关闭fd/socket、回收受控子进程；未接入的必需宿主保持待验。
- [ ] 矩阵为M2提供事实，不提前声明Node API或事件循环已实现。

## Tests

逐native宿主及ARM64完整系统guest运行、重定向/受控PTY模式对照、实际返回值和退出码断言。

## Negative Cases

EPIPE/半关闭、无效句柄、阻塞超时、子进程异常退出、TTY设施缺失、清理失败。

## Evidence

逐OS/句柄的原始日志、结果矩阵、可重跑命令及资源清理结果；新增制品都有独立身份。

所有结果遵循[父规格](../spec.md)的身份与状态合同。尚未执行的探针不得填造成功结果。

## Rollback

回退单个探针/适配，保留历史基线与失败样本；不通过隐藏失败或扩张支持声明解决。

## Context

- [M0 执行规格](../spec.md)
- [任务索引、资源前置与里程碑](../README.md)

## Comments

尚未实施。
