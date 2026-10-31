# 02: WSL2 Linux x64 构建环境就绪（ENV-01）

**What to build:** 维护者拥有一个真实可执行的 Linux x64 构建环境，并能用事实证明它满足 M0 工具要求。

**Blocked by:** None (can start immediately)。

**Status:** ready-for-agent

**Execution prerequisites:** 本机 WSL2 安装条件、可用磁盘及所需系统权限；具体发行版在实施时记录。涉及管理员操作或重启时由用户完成相应步骤。

## Goal

维护者拥有一个真实可执行的 Linux x64 构建环境，并能用事实证明它满足 M0 工具要求。

## Scope

补齐 WSL2 内核、Linux x64 发行版和基础构建/下载/解包/摘要工具；记录发行版、kernel、CPU、页大小、工具版本与工作目录访问，并将实际(OS/架构/页大小)写入本地覆盖记录。WSL x64不能替代ARM64组合；QEMU补充由后续probe任务落实。可先用独立只读命令验收，因此不依赖工程骨架任务。

## Out of Scope

获取 Nightly、付费云资源、macOS/ARM64 主机准备、服务 VM、改变产品目标范围。

## Source Files

开发环境说明和环境准备记录；若需仓库内验证脚本，与工程诊断契约对接。

## Acceptance criteria

- [ ] WSL 内实际执行 uname/架构/页大小查询，证实为 Linux x64；仅 wsl.exe 存在不算完成。
- [ ] make、C 编译器、Git、HTTPS 下载、归档解包和 SHA-256 工具可以实际运行，记录版本。
- [ ] 完成系统 C hello 的编译和执行，记录命令与退出码；不将其标为 APE 证明。
- [ ] 说明 Windows 编辑目录与 Linux 构建目录的对应关系和磁盘要求；不假定跨文件系统权限完全一致。
- [ ] 需要的人工权限或重启如未完成，保留未就绪状态，不能生成虚假的 Linux 指纹。

## Tests

在 WSL 内执行环境查询和最小 C 程序；重新打开 shell 后复核命令与工作目录可用。

## Negative Cases

发行版不存在、kernel 缺失、工具缺失、无写入/执行权限、下载或解包失败、空间不足。

## Evidence

真实 Linux 环境指纹、工具版本、最小编译日志、uname/getconf实测页大小、环境准备步骤及仍需人工操作的事项。

所有结果遵循[父规格](../spec.md)的身份与状态合同。尚未执行的探针不得填造成功结果。

## Rollback

保留现有 Windows 工具和工作区；发行版/内核属于用户系统资源，回退步骤单独说明，不能由清理脚本自动删除。

## Context

- [M0 执行规格](../spec.md)
- [任务索引、资源前置与里程碑](../README.md)

## Comments

尚未实施。
