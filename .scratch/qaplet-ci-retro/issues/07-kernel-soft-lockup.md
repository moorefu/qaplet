# 调查实际 64K guest 的偶发 kernel soft lockup

Status: needs-triage
Execution state: open
Type: research

## 已知事实

Full 42171911281 attempt 1 的 guest 日志含 kernel soft lockup；1000+100 生命周期用例已结束，最后开始的产品用例为 m2-process-js-terminal，最终触发既有 900 秒外层期限。同一 host SHA 的 push 42171903808 与失败作业的 attempt 2 通过。根因归属 Unknown，不能将一次重跑通过解释为修复。

失败原件、时间线和相同制品对照保留在 out/ci-main-7f0b312/a96-kernel-lockup-differential.json 及其引用，不改写失败观测。后续执行使用 arm64_guest 的有界宿主/QEMU 样本；样本只帮助调查。

## 完成条件

建立可复现条件或足够直接证据，区分宿主资源竞争、QEMU、guest kernel 与产品；保留负向对照和准确身份。在原因明确前记录 Unknown。不得调整 1000+100、清理预算、kernel/输入锁或正式验收门槛以消除失败。
