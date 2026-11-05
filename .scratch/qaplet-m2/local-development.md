# M2 当前执行方式：本地 Linux / Windows 优先

2026-11-05（Asia/Shanghai），用户明确指示：“我们先在本地开发，主要推进Linux跟Windows”。
本记录调整当前开发顺序；原规格的产品合同、平台支持声明及最终验收要求保留。

## 当前阶段的开发依赖

Ubuntu继续统一构建产品和SDK，Linux与Windows消费同一份供应字节。每票完成公开
边界红绿、本地正负验收与独立合并后，记 `Local development: complete`。
该标记满足本阶段后续票据的开发前置；跨平台证据尚缺时保留 `Status: claimed` 和
`cross_deferred`，不冒称该票已完成全范围验收。原Blocked by边不删除。

11、13的实现及Linux/Windows供应验证已经独立合并，可据此推进12、14、15。
11/13仍保留ARM Mac、完整CI/Full等未验收项。本地新增实际64K SDK已170/170通过，
但其 `ci=null`，与CI分开记录；当前原件在主
`out/qaplet-m2/local64k-nofile-034/HANDOFF.md`。

## 本地实施要求

- 按既有implement-spec/TDD流程，每票fresh实现者、独立worktree、公开产品测试；
  测试边界沿用已确认Q49/Q53，无须重新访谈。
- 每个资源首次接入即实施有限预算、stop/close/quiesce安全及物理保留，不能延后到19/20。
- 交接前融合实际integration tip，再由独立merger合并；不因共享文件虚构前置。
- 保存真实SDK/APE/loader/ZIP/配方、命令、日志、宿主和清理事实；未执行项保持可见。
- 当前不push、不触发CI，不改变GitHub付款、预算、仓库可见性或runner配置。

## 延后但未取消的验收

跨平台和CI缺口集中保留到24–27及最终矩阵收口。实际64K可在本地继续补充，但不能
替代缺失的原生Mac或CI身份。Linux/Windows的混合生命周期和1小时长测仍需实际运行。
最终全范围resolved、父M2完成、整体Review与release-ready均不得提前签发。

该执行方式优先于各票此前“等当前Full后才放行下游”的调度记录；新本地证据与历史
失败原件分别保存，不回写旧报告。
