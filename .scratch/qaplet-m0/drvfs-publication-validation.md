# DrvFS 缓存发布与编辑器占用（2026-11-01）

当前宿主为 WSL2 Ubuntu-22.04 / Linux x86_64，物理缓存仍在主仓库 `.cache/`。
Nightly CLI 的候选目录改名偶发 `EACCES (13)`；这个故障不是输入校验通过，
此前失败报告保持原样。最终修正是仓库级 VS Code 文件监听排除规则，供应脚本、
权限策略、锁和缓存位置没有更改，也没有增加发布重试。

## 反馈循环与区分实验

使用固定提交 `914a04e` 的源代码副本；副本归档 SHA-256 为
`f4b24a26d2ea98ea5c50b1c29b2ff41ee659958dc1fb56280e801b4068126ccc`。
所有诊断输入在 `.cache/diagnostics/drvfs-publication/`，日志在
`out/m0-integration/drvfs-publication/`。诊断脚本在 ignored `out/m0-integration/`，
不进入产品 CLI。

原始完整 `make check` 和独立进程组 unittest 都复现相同改名错误。
单测和部分前序测试的成功不能证明修复。继续缩小为一次公开 CLI `fetch`：
构造只有三个 POSIX shell fixture compiler、一个头文件和一个内部链接的 ZIP，
逐次导入全新缓存，不需要已有锁或先在 ext4 导入。

```sh
PYTHONDONTWRITEBYTECODE=1 python3 out/m0-integration/drvfs_minimal_feedback.py
```

最小循环 20 次中有 14 次同样错误，报告
[`e2d5d2ec/result.json`](../../out/m0-integration/drvfs-publication/e2d5d2ec-2274-4bec-b174-81898e49cceb/result.json)。
改名发生在 `freeze()` 之前，因此排除“冻结后无法改名”的解释。
另外建立有明确 DEBUG 标记的独立脚本副本，逐项检查三个假设：执行工具留下句柄、
候选属性/权限阻止改名、外部文件占用。跳过工具执行仍复现；去掉 ZIP 内链接仍为
14/20 同样失败；0.1 秒的一次诊断延迟也没有消除失败。

失败现场的 Linux `/proc/self/fd` 只有标准输入/输出/错误，候选和所有普通文件
仍有写权限。原生 Windows `Directory.Move` 同时返回 `0x80070005`。
没有改变失败目录的内容、模式或 ACL，原进程结束后再对保留目录执行一次改名，
五个目录均成功。这排除了固定目录权限错误，尚不能单独识别占用者。

## 实际占用者与修正验证

在另一次相同错误发生且原 Linux 进程仍活跃时，Windows Restart Manager
`RmRegisterResources` / `RmGetList` 返回一个占用者：PID `50292`、
`Visual Studio Code`。只读进程核查确认它是 `Code.exe` 的 Pylance 进程，
不保存完整命令行或环境变量。
[`占用证据`](../../out/m0-integration/drvfs-publication/owners-probe2-request.response.json)
包含 API 返回码、候选文件列表和占用者。查询结束后 `RmEndSession` 返回 0；
未关闭或重启任何编辑器/服务，也未调用 Restart Manager 的 shutdown/restart API。

仓库 `.vscode/settings.json` 将 `.cache/`、`out/`、`toolchains/evidence/` 排除于
VS Code 的递归文件监听。使用相同源代码、CLI 和缓存卷做可逆 A/B：

| 条件 | 20 次全新导入 | 原始日志目录 |
| --- | --- | --- |
| 原配置 | 14 次 EACCES | `e2d5d2ec-2274-4bec-b174-81898e49cceb` |
| 加入排除 | 20 次通过 | `04c70ab9-9d5f-406c-ad45-c77a7c9aa1b6` |
| 暂时移除排除 | 20 次 EACCES | `4dcbc7f0-937c-4034-bb0d-793a201049c1` |
| 恢复排除，原始完整检查 | 78 个合同，6 个明确 opt-in/平台 skip，其余通过；静态检查与 C bootstrap 通过 | `3fd31c5d-de18-4d6d-a041-f7aff1c22416` |

这是当前宿主编辑器占用的修正，不声明所有 Windows 文件占用问题都已消除。
没有停止 Pylance、改变全局编辑器设置、WSL/binfmt/mount、缓存权限或供应验证。
另一个编辑器或扩展主动打开缓存文件时，操作仍可能被 OS 拒绝并保留失败报告。
独立诊断副本及原始失败保存在明确标记的诊断目录，正式脚本没有 DEBUG 代码。

随后在集成提交 `8fb7c39` 上运行当前完整检查，启用已供应的新字节 I/O 与七项
startup CLI 合同：102 项测试、15 项明确 opt-in/平台 skip，其余通过，静态检查和
系统 C bootstrap 同样通过。
[`最终命令记录`](../../out/m0-integration/current-check/6a382b60-6cd6-4140-82e1-630e9ceb5c32/command.json)。
此前一次 wrapper 把所有临时测试 fixture 的 `TMPDIR` 指向 DrvFS，导致要求真实
0644 的 POSIX transport fixture 无法成立（实测 0777）；那次失败仍保留在
`current-check/7ce00178-b174-497f-a0e3-a5ae98112047/`。恢复该测试声明的临时
POSIX fixture 上下文后通过；这些一次性测试 fixture 不是可复用缓存，主仓库
输入、实际构建缓存和产品/证据路径没有迁出。

Microsoft 的 [files.watcherExclude 说明](https://code.visualstudio.com/docs/setup/linux)
和 [file watcher API](https://code.visualstudio.com/api/references/vscode-api)
支持该配置用途；[Restart Manager API](https://learn.microsoft.com/en-us/windows/win32/api/restartmanager/nf-restartmanager-rmgetlist)
用于识别注册资源的占用应用。官方 WSL 1 历史故障说明不作为本机 WSL2 的原因证明，
本记录依据上面的实际占用查询和 A/B 结果。
