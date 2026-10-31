# Nightly 选型与固定构建身份并存

沿用总体计划的输入策略：实施时从原版 Bellard QuickJS master 和官方 cosmocc Nightly 显式取得候选输入，记录真实源码提交、原归档与编译器摘要、补丁清单和来源；通过探针后以锁定身份构建。后续普通构建使用已校验缓存，更新通过独立显式流程，不能让移动上游在构建时改变输入。

Cosmopolitan 源码 HEAD 与 Nightly 内 libc 的源码身份是不同事实；无法证明关联时必须标明未知。该取舍保留 Nightly 能力，同时使失败复现、跨宿主同一制品和离线重构有可靠锚点。

来源：[总体计划 §1、§15、§17](../plans/qaplet-v1-implementation-plan.md)。
