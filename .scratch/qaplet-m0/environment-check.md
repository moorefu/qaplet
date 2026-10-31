# M0 环境核查：架构与页大小

核查日期：2026-10-31（Asia/Shanghai）。本次仅只读查询和官方资料核对，未安装WSL、启动产品探针或触发GitHub Actions。

> 下方“目前无法测量”是规划时查询旧入口的历史记录。BOOT-01 实施时在普通用户上下文重新核验成功，见文末“BOOT-01 实施复核”；受限进程的失败不能外推为系统未安装。

## 本地 WSL：目前无法测量

- wsl --status：默认版本2；明确报告“未找到 WSL 2 内核文件”。
- wsl --list --verbose：没有已安装的发行版，退出码-1。
- 因而WSL的kernel、实际架构和page size均不能填作已测量值。不能借用Windows或MSYS返回的页大小。
- ENV-01就绪后，在实际WSL中采集uname -s、uname -m、uname -r、getconf PAGESIZE，并用C sysconf(_SC_PAGESIZE)交叉确认。

可复用的Linux只读探测命令：

```sh
uname -s
uname -m
uname -r
getconf PAGESIZE
```

## GitHub 原生 Linux ARM64：可用，页大小未实测

[官方runner参考](https://docs.github.com/en/actions/reference/runners/github-hosted-runners)提供ubuntu-24.04-arm等原生ARM64标签。[官方镜像清单](https://github.com/actions/runner-images/blob/main/images/ubuntu/Ubuntu2404-Arm64-Readme.md)本次读取到image 20261026.166.1、kernel 7.0.0-1017-azure，但未列页大小。它不能替代执行getconf/sysconf的证据；也不能仅凭Ubuntu发行版或azure内核名推定配置。

本次没有可读取的本项目CI运行结果，也未为探索新发起CI。第一个正常集成job须输出并归档实际架构、页大小、kernel、image版本与run/commit身份。4K是待验证的常见预期，不是本次测量结果。

## 覆盖与补充规则

| 场景 | 原生测量结果 | 补充 |
| --- | --- | --- |
| CI ARM64 | 4096字节 | 一个ARM64 64K完整系统guest |
| CI ARM64 | 65536字节 | 一个ARM64 4K完整系统guest |
| CI ARM64 | 其他/未测 | 明确4K/64K缺口；不得计为任一目标 |
| 本地WSL x64快速profile | 页大小实测后记录 | 通常补一个ARM64 64K guest；ARM64 4K不计为本地已覆盖 |

本地快速profile服务日常探索；完整M0门禁仍核对全部(OS/架构/页大小)目标。QEMU在guest内再次读取页大小，运行目标APE，记录emulated-system与固定镜像身份；不会因宿主也有4K页就省掉ARM64 4K在CI中的原生验证。

## BOOT-01 实施复核：2026-10-31

用户指出开始菜单 `C:\ProgramData\Microsoft\Windows\Start Menu\Programs\WSL.lnk` 和 WindowsApps 中的 WSL。只读解析 MSI 广告快捷方式后，实际入口为 `C:\Program Files\WSL\wsl.exe`；另一个入口为 `C:\Program Files\WindowsApps\MicrosoftCorporationII.WindowsSubsystemForLinux_2.6.3.0_x64__8wekyb3d8bbwe\wsl.exe`。两个 exe 的 SHA-256 均为 `7577d6257a3c1e3e914155dc6ebc8f4f1c4c94b093de100cec01cd8a0e57acd7`。

受限 agent 进程调用 system32 旧入口报缺内核，调用 MSI 入口报无发行版，调用 WindowsApps 入口报拒绝访问。沙箱外同一普通用户上下文的只读核验则实际取得：

| 项目 | 实测 |
| --- | --- |
| WSL 版本 | 2.6.3.0 |
| 默认发行版 / WSL 模式 | Ubuntu-22.04 / 2 |
| `uname -s` | Linux |
| `uname -m` | x86_64 |
| `uname -r` | 6.6.87.2-microsoft-standard-WSL2 |
| `getconf PAGESIZE` | 4096 |
| 系统 C 冒烟 | 编译、执行、输出 answer=42、页大小=4096、退出码=0 |

本次没有安装或更新 WSL。`out/doctor-store-wsl.json` 保存普通用户上下文的完整命令、版本与原始日志定位；`out/wsl/bootstrap.json` 保存真实 Linux C 构建/运行证据。它们是未纳入 Git 的本机生成物，可通过 `make doctor`、`make test-bootstrap OUT=out/wsl` 重建。

这纠正了规划时“缺 kernel/发行版”的判断；任务 02 的完整工具/磁盘/下载与解包合同仍需单独验收，本任务不关闭任务 02，也不将 WSL x64 4K 计入 ARM64 目标。未运行产品 APE、QuickJS 或远程 CI。
