# 本轮效率改造的独立审查与接收

2026-11-05。用户指定原审查范围 `e11a032448a4e86d64cf37dddffa01b41e9f6a5f → ce9cd7e41fed5361c54a9112d6fccccd15ee1f21`，仅本轮工程效率改造。M2整体验收仍在最后；本记录不是全平台或发布通过。

每轮均先生成并验证分开的 Standards/Spec packet，再派发各轴；只在两轴报告收齐后聚合。原始报告、重现、拒绝和packet未改写。完整原件分别在主 `out/reviews/efficiency-e11-ce9-20261105/`、`efficiency-fixes-f9-d72-20261105/`、`efficiency-provider-3ee-168-20261105/`。

## Standards

原2项均关闭，最终新增0：

- P2手工Cross/Fast并发取消：M0九个相关job使用独立run ID，手工运行不自动取消，普通push仍可取消过期同范围作业。
- P3配方重复维护：producer和快速入口共用`sdk.recipe_identity`，Git/dirty元数据另行附加，原编译与链接参数语义不变。

独立复核源码为`d72f7bafb162dd948405fff7c312aeae50260b4f`；后续provider增量`3eeb730 → 1685022`再次独立检查，新增硬违反0、实际smell0。

## Spec

原3项均关闭，追加1项P2也已关闭，最终新增0：

- P1公共ABI切换只跑Cross：公共SDK头无法证明非ABI编辑时保守选择Full；手工层级不能降低该要求。
- P2服务子串误分类：使用实际私有源/脚本/fixture集合和共享依赖，真实Runtime路径不再因包含服务字样被分成service-only。
- P2结果复用未接生产入口：普通dev从有界成功指针定位原件；Fast在获取输入前查同SHA成功run，严格复验SDK、原ZIP/provider、所选合同及raw后复用，拒绝保留并回落实际执行，不扩大可信cache写权限、不新签OS/CI身份。
- 追加P2同run同名不同ID制品：按独立provider有界逐件复验，失败继续下一件；保留ID与拒绝原件。最多三个run、每run三个未过期候选和网络预算保持。独立8场景数据重放通过，全部拒绝或用尽上限时正确回落。

## 修复与本地验证

修复基于已接收票15的根`f9b47d9`，代码`d72f7ba`，验证记录`3eeb730`，最终provider修复`1685022ceba22e348392de1624694968cb62d9d6`。本地no-ff接收提交为`68ef37fb57cd45b128e5019c48f45374186f16b1`，合并树严格等于独立复核的修复源。

- 实现者Ubuntu完整回归：410项，210执行通过、200条件skip，真实C bootstrap通过。
- 新SDK的Linux/Windows同字节各127项、各1000生命周期通过；普通dev自动复用两端均0次新产品执行，原执行/CI/宿主保留。以上成绩绑定代码提交d72，不改签后来的文档或合并SHA。
- provider追加修复只跑定向合同：Windows/Ubuntu各4项、原传输10项与check-ci通过；独立Spec另做8个最小场景重放。没有因该CI增量重供SDK或重跑全套。
- 合后仓库预检、80件recipe原始LF/Git blob、当前recipe/SDK/ZIP与完整distribution验证通过；没有新增产品执行。原件见主`out/reviews/efficiency-final-postmerge*.json`。

新recipe为`a7a2d15e76fdfd59b5141d1269644f45e923e8ffb36ce14d66b7533bebb7392b`，SDK为`d99004a46bb24c69c17bd6bf981664dfd82e25a9f6f06e6be404bd8a5cde3aad`。只在Ubuntu重供一次。supplier仍保留原`f9b47d9 dirty=true`事实，源审计证明其80件字节等于修复Git blob；没有重写旧producer身份。

实现者交接在`.cache/worktrees/efficiency-review-fixes/out/review-fixes/`的`HANDOFF.md`、`handoff.json`及`duplicate-provider-*`；SDK、测试和原失败均原地保留。

## 票15的独立接收

票15已由另一位merger独立审查并以`65de84e2cd86104f4249211ec06bcc7a352eafd1`合入，随后`f9b47d9`只补三份tracker接收记录。普通Linux186、Windows185+既有optional1；包外各72，故障16/17、各1000生命周期的原件和供件身份均独立重放通过；合后Ubuntu预检/源码校验通过。`process/all`静态覆盖全部7个新IO普通case。

详主`out/qaplet-m2/merge-15-process-io/HANDOFF.md`。票15保持`Local development: complete / Status: claimed / cross_deferred`，未关闭正式全范围验收；16/17本轮未启动。

未push、未运行真实Actions/Full/QEMU/64K/Mac/ARM或长测，未改main、付款、保护设置或发布状态。loopback/API合同与真实远端运行分别记录，`release_ready=false`。
