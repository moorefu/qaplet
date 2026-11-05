# 效率改造五项审查发现修复验证

Status: claimed（实现和本地验证；原两轴独立复核由 root 协调）

固定原审查范围为 `e11a032448a4e86d64cf37dddffa01b41e9f6a5f` →
`ce9cd7e41fed5361c54a9112d6fccccd15ee1f21`；原 packet、报告和有限 repro 未改。
本轮基于实际 integration `f9b47d9e10336c13dac5ccec0ac2d9ad94ad1bc0`（已含独立票15），
修复代码提交 `d72f7bafb162dd948405fff7c312aeae50260b4f`。不开始16/17，不 push/CI/发布。
工作区 `.cache/worktrees/efficiency-review-fixes`，分支 `codex/efficiency-review-fixes`。
原件统一在该工作区 `out/review-fixes/`；以下路径相对此目录，报告不可冒充远端成绩。

| 发现 | disposition | 红 → 绿与限制 |
|---|---|---|
| Standards P2 手工运行取消 | implemented，待独立复核 | 9个 M0 产品/required job 的 group/cancel 同时豁免 workflow_dispatch；push/PR targeted 继续取消旧运行，Full/RC/manual按runID隔离。实际workflow表达式正反例及去掉豁免的check-ci突变被拒绝 |
| Standards P3 两份配方 | implemented，待独立复核 | SDK公开纯 `recipe_identity(files,input_id,cflags)` 同时供producer/local使用，Git/dirty另附。flags、输入/成员摘要变化及选中合同expected变化各自改变身份；编译/link flags语义未变。未修改 pinned Makefile/evidence.py/foundation.py |
| Spec P2 私有服务子串 | implemented，待独立复核 | 锚定真实四个私有源码、脚本、workflow和fixtures；startup/build/input公共依赖传递SCM+Job。假的src/unknown路径及临时实际存在src/svc_scm.c均Full；已知Runtime源码不能成为服务only |
| Spec P1 公共ABI | implemented，待独立复核 | 所有公开include保守Full，push/PR/manual Cross均不可降低，保留五native和当前64K/offline/Core合同；一般native OS仍Cross。不构建C解析器 |
| Spec P2 结果复用未接入口 | implemented，待独立复核 | 普通dev/local启动先读一个可再生成功指针（SHA/recipe/contract/platform），每次严格复验；CI Fast获取输入前用同入口查询最多3个成功候选/上个成功attempt，默认30秒网络预算。原ZIP/provider+offline verify+SDK+raw通过才skip；miss/reject回落原执行。只读token不外发/不保存签名URL、不扩大可信cache写权限。远端未实跑 |

## 红绿原件

- `policy-red.log`：19个真实断言失败（私有路径、公共头和9处manual并发）；另有5个初始测试遗漏ci-efficiency排除项的harness错误，已修正。
- `policy-green.log` / `policy-final.log`：11 tests通过；包含普通push/PR取消、Full/RC/manual隔离、真实私有文件和存在的假Runtime源反例、公共ABI Full。
- `recipe-red.log`：新公开helper缺失的1个预期AttributeError；`recipe-green.log`：11 tests通过。原contract expected变化测试继续通过。
- `reuse-red.log`：真实入口缺少复用参数，7个身份/字节场景拒绝；`workflow-reuse-red.log`：真实Fast缺少前置verifier。
- `provider-cache-red.log`：新run同名ZIP占用旧cache名字，导致仍有效的老provider原件被错误拒绝；改成provider ID文件名后转绿。普通fetch路径和旧receipt保留。
- `reuse-final-2.log`：4 tests通过，内部覆盖本地7种、Actions 11种场景，真实dev拒绝后进入supplier，以及workflow去除verifier/伪skip/manual取消突变。Actions含正常、旧原件回退、previous-attempt、陈旧、错SHA、recipe、contract、缺报告、坏raw、错provider digest和坏既有ID字节；两份同名不同ID/ZIP并存，坏原件不覆盖，loopback全请求不携token。
- `remote-reuse-first.log` 的最初valid失败来自fixture给出相对下载Location，既有check_url按合同拒绝；改为合法绝对loopback参考后通过，不归类为产品故障。

## 新供应与真实宿主

SDK helper冻结后只重供一次：Ubuntu-22.04 / WSL x86_64，`build.json`通过，226.370秒。
使用主 `.cache/` 的原锁输入、工具链和构建缓存，无 fetch/QEMU/Full。

- recipe：`a7a2d15e76fdfd59b5141d1269644f45e923e8ffb36ce14d66b7533bebb7392b`。
- SDK：`d99004a46bb24c69c17bd6bf981664dfd82e25a9f6f06e6be404bd8a5cde3aad`。
- ZIP：`45e309fc40d5c3b46179762ec270c07c409eb35c19b9a257476ba334eeafd715`。
- 80 recipe文件、80 ZIP成员、81 distribution文件。`source-audit.json` 将所有80件原build摘要与d72f7ba Git blob/当前rawLF字节逐件对齐。
- 供应时Git元数据为f9b47d9、dirty=true，保持原事实；实际所消费recipe字节与修复提交d72f7ba一致，不回写供件元数据。
- 票15旧 `build-final-3.json` / `final-3-artifacts` 保留原身份，没有改旧报告冒充新recipe。

| 执行 | 原件 | 实际范围 | 时间 |
|---|---|---|---|
| Linux首次实际L0 | `linux-dev.json`、`linux-actual/local-check.json`及selected-sdk/public-c/raw | 127 pass，1000生命周期；Ubuntu普通C头编译pass | local-check 23.300秒，行为13.783秒，C头0.302秒 |
| Windows首次实际L0 | `windows-dev.json`、`windows-actual/local-check.json`及selected-sdk/raw | 同字节127 pass，1000生命周期；不编译SDK | local-check 21.011秒，行为19.151秒 |
| Linux普通dev重复 | `linux-reused-dev.json`、`linux-reused/local-check.json` | 自动成功指针命中；严格复验303个文件；reused_previous，0新产品执行 | 9.704秒 |
| Windows普通dev重复 | `windows-reused-dev.json`、`windows-reused/local-check.json` | 自动成功指针命中；严格复验254个文件；reused_previous，0新产品执行 | 1.328秒 |

复用命令没有传显式原件或supplier，证明日常启动路径接入。原execution/report/producer/宿主/时间身份均留在reuse中，ci仍null；wrapper不签发新OS或Full成绩。以上为本机不同执行上下文的小样本，不宣称Actions优化百分比。

## 实际命令

Linux工作区：`/mnt/d/Workspace/dairn/qaplet/.cache/worktrees/efficiency-review-fixes`，
每条命令局部 `PATH="$PWD/out/review-fixes/bin:$PATH"` 使用只读git adapter。
adapter只对本工作区rev-parse/status/ls-files/show/diff/log/worktree list转换linked Git路径；
fixture和写操作走真实git，不改全局GIT_DIR/WORK_TREE/config。Windows Git命令逐条带`-c core.autocrlf=false`。

```sh
python3 -B scripts/sdk.py build --output-dir out/review-fixes/artifacts --report out/review-fixes/build.json
python3 -B scripts/dev.py check-fast --profile all --supplier-report out/review-fixes/build.json --supplier-dir out/review-fixes/artifacts --output-dir out/review-fixes/linux-actual --report out/review-fixes/linux-dev.json
python3 -B scripts/dev.py check-fast --profile all --output-dir out/review-fixes/linux-reused --report out/review-fixes/linux-reused-dev.json
python3 -B scripts/dev.py check --output-dir out/review-fixes/repository-check --report out/review-fixes/repository-check.json
```

Windows用`python -B`运行同两条dev check-fast，输出名分别为windows-actual/windows-dev和windows-reused/windows-reused-dev。
目标合同：`python -B -m unittest discover -s tests/contracts -p test_ci_efficiency.py -v`、
`-p test_local_check.py`、`-p test_ci_reuse.py`。红绿分别保存，不重写失败。

## 一次广泛本地回归

`repository-check.json` 通过；对应 `repository-check/command-0.stderr.log`：410 tests，210执行通过、200条件skip，275.456秒；后续真实C bootstrap通过。check-ci、M0 check-ci与统一锁audit均通过。跳过项未计为OS产品执行。未在验证通过后重复全suite。

## 保留限制

原两轴reviewer各自复核前不宣称独立审查零发现。未触发真实远端Actions；未跑完整M2矩阵、
QEMU/当前64K、Mac/ARM native、混合资源1小时或Release。Full合同在选择/gate/工作流合同中保留，
这些本地成绩不将M2父票或跨平台/最终验收提前resolved。发现过期/不匹配或有界候选不可用时会重新执行，
包括超过候选范围的老原件，避免无界扫描或重试。INDEX只更新票15本地完成，历史性能数仍绑定修复前ce9原source。
