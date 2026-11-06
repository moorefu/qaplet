# M2 CI：账号付款或支出限额阻塞

2026-11-05（Asia/Shanghai），用户分别提供 D1 attempt2 ARM Mac job 与新 Full
`41881834665` 页面上的相同 Annotations 原文：

> The job was not started because recent account payments have failed or your spending limit needs to be increased. Please check the 'Billing & plans' section in your settings

该消息证明 GitHub 在启动 job 前施加账号账单限制，但未区分付款失败和支出限额
哪一项触发。原文来源是用户读取页面后的回复；此前 API 的 runner/steps 空值及
日志404本身没有给出这一原因。不是产品命令执行失败，也不能只归因于之前的
GitHub Actions 公共服务事件。

## 已保存的执行边界

- `d1d0936` / Full `41870752184` A1：四原生680项严格通过，ARM缺失；20原ZIP、
  6原日志及3992消费文件封存。A2只有20继承上传/0新上传，实际报告attempt1与
  最新API envelope attempt2分开记录，未把queued改称终态。
- `7e10d11` / Full `41881834665`：tier-plan在取得runner前失败、后续6项skipped；
  0当前制品、0已验证产品命令、0当前case。旧SDK身份未填成新CI身份。
- 证据入口分别为主 `out/qaplet-m2/full-d1d0936/HANDOFF-attempt1.md`、
  `out/qaplet-m2/full-d1d0936-attempt2/HANDOFF-attempt2.md` 与
  `out/qaplet-m2/full-7e10d11/HANDOFF.md`。这些原件不因补充原因而重写。

## 继续方式

停止新的CI触发与重跑，直到账单限制解除或用户提供新的执行资源安排。没有修改
账号付款、预算、仓库可见性、runner配置、产品、输入锁或验收门槛。

11、13继续 `claimed / awaiting_cross`；12、14、15的直接阻塞尚未全部完成。
本地仍可消费冻结034供应执行真实ARM64/64K，结果必须标明本地system-emulation、
`ci=null`，不替代缺失的当前CI、ARM Mac或完整Core。父M2保持in_progress，
最终T03、整体Review和release-ready均未完成。
