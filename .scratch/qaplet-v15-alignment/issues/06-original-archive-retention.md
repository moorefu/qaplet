# 06：原始 cosmocc ZIP 长期留存与恢复（发布门禁）

**Status:** needs-info
**Blocked by:** 无；不阻断 M0-A/M1 开发。
**Execution prerequisites:** 维护者落实长期存储目标、访问权限、保留/覆盖策略及写入授权；当前尚未确定。

## Goal

在发布前证明固定工具链原 ZIP 不依赖 GitHub Actions 有效期或可丢弃缓存，仍可按同一 digest 恢复。

## Scope

长期保存 Run `41169443573` / Artifact `13031425078` 原 ZIP 的相同字节：`419652152` 字节、SHA-256 `f2f984d3a9790674fc0da11c1c00a32d4fe3f8dafde99af74b0c6dc24a2a16cf`。使用受控对象存储、Release 原件附件或制品库，记录不可覆盖/保留及权限策略、来源和恢复程序；从无本地缓存的受控目标恢复并深验原件/物化内容。清空实验缓存使用一次性副本，不删除用户唯一原件。

## Out of Scope

把 `.cache/` 或 Actions cache 当永久镜像、重打包后冒充原 ZIP、自动更换工具链 Run、在本轮未授权上传、以该任务阻断 M1 的已验缓存使用。

## Source Files

统一 v3 锁的来源/留存引用、获取/恢复校验器、受保护存储配置引用、来源证据与发布 gate；凭据只从受控环境读取。

## Acceptance criteria

- [ ] 长期目标、保留/覆盖/访问策略及原件 digest 明确，可独立于可丢弃缓存取回。
- [ ] 实际取回文件的长度/独立 SHA 与固定原 ZIP 一致，并通过物化深检和必要工具链 probe。
- [ ] 上游 Artifact 不可用时，可从已批准同 SHA 镜像恢复；双双不可用/损坏时失败。
- [ ] Release gate 明确消费该证据；未完成时不签发 release-ready，M1 开发仍可用已验缓存。
- [ ] 留存地址类型/报告进入 Git，原 ZIP、临时缓存和凭据不进入 Git。

## Tests

在一次性空缓存中完成真实恢复、摘要/长度/清单检查；注入缺失、篡改、错误来源和不可访问目标。保存恢复命令与原始日志。

## Negative Cases

上游过期、镜像删除/覆盖、只有解压后重打包 ZIP、不同 SHA、网络/权限不足、凭据泄露、只写了 URL 却未做恢复试验。

## Evidence

存储及访问策略、原件来源、恢复报告、digest/size、深检/功能结果、发布门禁结果及拒绝日志。

## Rollback

回退获取/发布引用，保留原 ZIP 与已取得证据；对外存储删除/权限变更另按授权处理，不自动清除唯一副本。

## Context

[ADR 0004](../../../docs/adr/0004-v15-platform-baseline.md) · [现有原件证明](../../qaplet-m0/nightly-toolchain-validation.md) · [对齐规格](../spec.md)

## Comments

2026-11-01：用户确认镜像发布前补齐；存储目标和授权在实施本任务前落实。

2026-11-02（implement-spec）：用户要求先保留本地暂存，后续再选择长期存储。原 ZIP 继续位于主工作区 `.cache/downloads/official-nightly-cosmocc-41169443573.zip`；重新核对长度为 419652152 字节，SHA-256 与本票固定原件一致。没有上传、删除或替换原件。长期目标、策略及上传授权仍待落实，本票保持 `needs-info`，不阻断 02–05；本地暂存不作为长期留存或 release-ready 证据。
