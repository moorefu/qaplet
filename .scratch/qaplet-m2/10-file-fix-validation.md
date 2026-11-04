# 10 · Windows 文件 fixture 定向修复

从 integration `43753331e870b677be714666428a437837318b03` 领取
`codex/m2-10-file-fix` / `.cache/worktrees/m2-10-file-fix`。产品冻结提交为
`63d7327a393af9e957064cd9c14b96bcdd53f7fa`；交付前实际执行
`git merge --no-edit codex/m2-integration`，结果 Already up to date。
本树 `out/m2-10-file-fix/` 保存全部诊断和本地新供应；旧 `m2-10` 树冻结未改。

## 旧红与紧循环

原 Full `41799595408` / attempt 1 / 产品 `867356c` 保持 completed/failure。
主工作区 `out/qaplet-m2/full-867356c/collected/` 的 Windows SDK 原件中，
`qaplet-host.m2-work-file` 为 exit 256、无 timeout、stdout 空，stderr 精确为：

```text
work contract line 49: fd>=0 && write(fd,expected,sizeof(expected))==sizeof(expected) && !close(fd)
```

此断言在首次 work submit 之前；原件没有记录 mkstemp、write、close 的具体
失败阶段或 errno。`source-proof.json` 重新核对原报告和原始日志，证明红回归所用
`sdk_work.c` 的 SHA `01c4994e086270dc5de2ae7e590ae1854d040d176e9564673137eb40b7a58130`
与原供应 RECIPE 完全相同，不将受控 ENOENT 追认为旧 CI 的实际 errno。

按 diagnosing-bugs、implement-spec 和 tdd，先建立并实际运行：

```text
python out/m2-10-file-fix/boundary_run.py
```

Ubuntu 已验 official-assimilate 工具构建两个诊断 APE；Windows 只消费同字节。
完整旧公开文件消费者仅在 mkstemp 的真实系统边界将 `/tmp/` 指向自有目录下
明确缺失的父目录，不伪造返回码。结果为 fd=-1 / errno=2、exit 256、同一 line 49
断言；约 13ms。再去掉 Runtime、handler 和 worker，仅留原文件准备序列，约 6ms
仍有相同红。旧完整供应在本机则通过；该绿不覆盖 Windows Server 原红。
原始输出见 `boundary-results.json` 及逐命令 stdout/stderr。

最小化后，先向用户展示四项排序可证伪假设，再执行 `hypothesis_run.py`：

| 排名及预测 | 实际结果与适用范围 |
|---|---|
| 1. 用例依赖未提供的全局目录；只建立缺失父目录应转绿 | 同一旧完整 APE、相同重定向路径，仅 mkdir 父目录便由 exit 256 转为 0，完整 work 文件合同通过 |
| 2. 目录可访问/可写条件有差异；存在与成功写入须分别实测 | 自有父目录建立后，真实创建、写入及完整读取成功；旧 CI 权限事实没有记录，仍未知 |
| 3. write 或 close 独立失败；目录建立后应仍红并在该阶段可见 | 最小未重定向边界实际 write=5、close=0；受控父目录建立后完整用例通过，仅排除本受控重现中的这两项 |
| 4. Runtime/worker 改坏文件准备；去掉 Runtime 应不再复现 | 无 Runtime 的最小边界仍红，排除本受控重现中的池/worker 因果 |

已证实的缺陷是 fixture 依赖未建立的 `/tmp` 前置条件；没有以猜测补写旧 CI
具体错误。临时 `[DEBUG-file-stage]` 探针仅保留在明确标为诊断的忽略输出目录，
产品/永久测试源码没有临时 DEBUG instrumentation。

## 永久红绿与修复边界

沿用已经授权的公开 SDK、包外纯 C Host 与真实受控 OS seam，不重复接口访谈。
先在 `sdk_work_faults.c` 加入回归：调用同一供应 `host-work.c` 的公开文件用例，
全局 `/tmp` 被指向自有目录内缺失子目录时，仍执行真正的 mkstemp。相对 fixture
不被重定向。`sdk.py` 的既有包外故障消费现在额外编译该公开 Host 源码并链接；
仍为两个 required 故障 case，work case 的预期 stdout 包含完整文件合同。

修复前 `regression-red-build.json` 在 Linux 返回 1，Windows
`hypothesis-results.json` 中同一回归 APE 返回 256；两者均保留 line 49 红。
修复只让 `sdk_work.c` 在 runner 的可写 cwd 下 mkdtemp 自有目录，再在目录内
mkstemp 文件，最终 unlink/rmdir；创建 Runtime 后不更改进程 cwd。文件准备失败
按 mkdtemp、mkstemp、write、close 分阶段输出返回值/errno，便于下次 CI 留事实。
没有静默回退、调整读取负载或扩时限，没有改任何 SDK `src/`、公开 API 或错误码。

`regression-green-build.json` 与 `regression-green-windows.json` 用同一 Ubuntu
构建的回归 APE 转绿，stderr 空，Windows 自有 fixture 目录实测清空。
原二进制/NUL 字节、5 字节输入但 4 字节输出预留、有界读取、声明/输入复制、
BUFFER_TOO_SMALL 无部分复制、可重读、release、ENOENT 和 native join 清理均保留。
故障夹具的故意不确定 join 仍保留 Runtime 到进程终止，不能解释成 SDK 清理成功。

## 冻结供应与本地验证

全部 SDK、双 archive、Host/CLI/application APE 和包外 APE 均在 Ubuntu 构建；
Windows 仅核对并执行同字节。SDK recipe 为
`14eb8b0f673d7412442349dc83f794bb0b137b95fc7ae3163d5e494a7dd578bb`；SDK identity 为
`9d603570c44e06fb7a6aa0991a97b7535cd5bb8b87e8b0c4b9440000586ba5b2`。
密封供应 ZIP SHA 为
`858652103c5f1048b75103b3bdc43115bb18360695c7a1cbfec3debef7400586`，15,558,323 bytes。
producer 的 `working_tree_dirty=true` 原样保留，不改写为 clean。

| `out/m2-10-file-fix/final/` 原件 | 实际结果 |
|---|---|
| `build.json` | Ubuntu SDK ZIP、x86-64/AArch64 archive、三 APE 和审计 pass |
| `linux.json` | Linux x86-64 / WSL2 6.6.87.2 / native / 4096；142/142 pass，生命周期实际 1000 |
| `windows.json` | Windows x86-64 / 10.0.22000 / native / 4096；141 pass、1 既有 optional symlink blocked，必需缺口 0；生命周期实际 1000；文件 work pass |
| `package.json` / `package-windows.json` | 真 ZIP 树外只用公开 include/archive 编译；相同 APE 在两宿主各 21/21 pass |
| `fault.json` / `fault-windows.json` | 真 ZIP 树外编译，两个 required 受控系统 case 在两宿主各 2/2 pass，包含永久文件目录回归 |
| `dev-check.json` | Ubuntu 完整 check pass；369 tests，177 实际执行、192 条件 skip；真实系统 C bootstrap pass |
| `handoff.json` | 70 个 RECIPE 逐项 raw==产品 Git blob 且全 LF；密封 ZIP/完整 SDK/三 APE 重新核对，metadata 4161535 与 21 个能力名一致 |

两个静态 archive 内容未因本 fixture 修复改变，分别为 x86-64
`fc72f8696ea7b2aad164305d71f52078faeb17ee865b93939a9aa9c1658d0292`、AArch64
`743f7b6e5bffcdb49440b5328122b720dab9857e09d8019bff46ae36699e9c23`。
各实测 14 个 ELF64 REL 成员，machine 62/183；两者保持各自身份，未混用架构。
Linux 使用原锁摘要匹配的 official-assimilate `ape-x86_64.elf`，Windows 使用
PE CreateProcess；loader、SDK、APE、原 ZIP、源配方与实际命令/日志均在报告绑定。

`driver.py` 在每次实际运行前保存自身及 scripts 源码 inventory，运行结束记录
实际已导入闭包并与前 inventory/自身再次比对；六份 `final-*-driver.json` 均通过。
包外 Windows replay 同样在导入前后记录，`package-replay-driver.json` 通过；
`audit-before.json` / `handoff.json` 记录最终核验 driver 与实际导入闭包，避免事后
补写侧车冒充执行身份。只读 Git shim 仅针对本 worktree，未设置全局 Git 环境
或修改共享 core.worktree，输入和构建缓存保持主工作区物理 `.cache/`。

## 待 Cross

本票保持 claimed / awaiting_cross，11 不据本地绿自动放行。旧 `867356c` 的
Windows SDK 红和独立 02 connect-error probe 红均保留；本次不修 02，不把四个
健康 native、实际 64K、旧 M0 Full 235 或旧包外成绩改写为本新配方已通过。
新产品还需 root 重新签发当前五 native、实际 64K 与适用组合 Full；本记录不
宣称整体 710、Full 成功、独立终审或最终 soak 已完成。
