# 02: WSL2 Linux x64 构建环境就绪（ENV-01）

**What to build:** 维护者拥有一个真实可执行的 Linux x64 构建环境，并能用事实证明它满足 M0 工具要求。

**Blocked by:** None (can start immediately)。

**Status:** resolved

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

- [x] WSL 内实际执行 uname/架构/页大小查询，证实为 Linux x64；仅 wsl.exe 存在不算完成。
- [x] make、C 编译器、Git、HTTPS 下载、归档解包和 SHA-256 工具可以实际运行，记录版本。
- [x] 完成系统 C hello 的编译和执行，记录命令与退出码；不将其标为 APE 证明。
- [x] 说明 Windows 编辑目录与 Linux 构建目录的对应关系和磁盘要求；不假定跨文件系统权限完全一致。
- [x] 需要的人工权限或重启如未完成，保留未就绪状态，不能生成虚假的 Linux 指纹。

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

2026-10-31：开始按 implement 工作流验收现有 Ubuntu-22.04，不重复安装 WSL。报告与测试沿用父规格确认的外部命令、真实 OS 行为、退出码、内容身份及结构化证据边界。

2026-10-31：完成。复用现有 WSL 2.6.3.0 / Ubuntu-22.04（WSL 模式 2），实测 Linux x86_64、kernel 6.6.87.2-microsoft-standard-WSL2、页大小 4096；只补齐 unzip/zip，没有安装 WSL 或重启。33 项环境验收通过；系统 C 编译和执行退出 0，独立 login shell 复核通过。Windows 编辑目录、Linux ext4 构建目录、D 盘及 C 盘 VHD backing 实际空间均记录。

新增 `make test-environment` 和 schema v2 的 environment scope，显式指定 HTTPS 探针 URL，保留 GitHub TLS 失败及 apt 更新超时历史。审查后 Windows `make check`：21 通过/4 Linux 专用项 skip；WSL：24 通过/1 Windows 专用项 skip；静态 CI 检查和两端系统 C bootstrap 均通过。skip 不计产品覆盖。

Standards 审查 0 项；Spec 审查发现权限拒绝误判 fail 的 P2，已通过隔离 mount namespace 的真实 noexec 回归复现并修正为 blocked，复审后 0 项。完整证据与回退见[验收记录](../wsl2-environment-validation.md)，实测覆盖见[本地覆盖记录](../local-coverage.json)。没有未完成的人工权限/重启步骤；未将 WSL x64 4K 计入 ARM64 或 APE 产品成绩，完整 M0 保持未完成。
