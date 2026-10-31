# 11: 应用私有 SCM ABI 探针（BOOT-05A）

**What to build:** 维护者知道锁定Nightly能否通过应用私有薄桥接接入真实Windows SCM，并有可复现结果。

**Blocked by:** [04 · Nightly 工具链锁与离线缓存](04-nightly-toolchain-lock.md)、[05 · 构建 fat APE 与最小 QuickJS 探针](05-fat-ape-build.md)。

**Status:** ready-for-agent

**Execution prerequisites:** 本地可丢弃Windows VM优先用于探索/调试；CI托管Windows临时VM持续回归。分别确认权限、系统身份和清理条件；不在普通开发宿主静默安装服务，不在CI内再嵌套VM。

**Execution override（2026-11-01）：** 用户明确要求“直接使用本机”，本轮允许在当前 Windows 开发宿主测试唯一命名的临时服务；先测权限，限定操作对象、时限和清理范围。权限不足须保留 blocked 和原始错误，不冒称隔离 VM 验证。见[父规格执行更新](../spec.md)。

## Goal

维护者知道锁定Nightly能否通过应用私有薄桥接接入真实Windows SCM，并有可复现结果。

## Scope

仅在服务应用私有native probes中验证Advapi32导入/Win ABI、dispatcher/ServiceMain/HandlerEx/SetServiceStatus及真实安装/启动/查询/停止/删除。回调不运行或等待JS。制品与Core分开构建；验证同一探针在非Windows宿主的普通入口不受条件导入破坏。

## Out of Scope

正式qaplet-svc、JS配置/CLI/日志/恢复策略、SDK/通用FFI、完整stop latch/watchdog、默认Core链接SCM。

## Source Files

服务应用私有SCM C探针、VM测试driver、独立构建与ABI证据；位置遵循总体计划的应用私有边界。

## Acceptance criteria

- [ ] 在隔离VM中以真实SCM查询状态，完成最小生命周期；可重复记录API/错误码、调用约定和状态变化。
- [ ] 原生回调无JS调用/等待，探针超时有界；普通Core制品和link map不增加服务符号。
- [ ] 若ABI不可用，交付最小失败复现、确认过的缺口和overlay修复方向；能力维持failed/unavailable，不能标verified。
- [ ] 非Windows执行路径有独立实际证据；未接入宿主不能当作已通过。
- [ ] 清理临时测试服务、受控进程和句柄；CI失败时仍执行清理，保存错误/事件日志和SCM查询结果。

## Tests

隔离VM真实服务操作和查询；ABI调用边界、重复stop、权限失败、非Windows入口检查。

## Negative Cases

无权限、导入缺失、错误调用约定、重复停止、启动/停止超时、服务残留、非Windows启动失败。

## Evidence

独立SVC制品/输入身份、SCM真值及API错误、VM指纹、非Windows运行结果、清理记录；失败的修复建议不冒充完成能力。

所有结果遵循[父规格](../spec.md)的身份与状态合同。尚未执行的探针不得填造成功结果。

## Rollback

删除本探针创建的测试服务/进程，由CI回收临时runner；真实重启/自启动另在可控VM验证；回退仅作用于应用私有probe/overlay，不回滚已通过Core。

## Context

- [M0 执行规格](../spec.md)
- [任务索引、资源前置与里程碑](../README.md)

## Comments

尚未实施。
