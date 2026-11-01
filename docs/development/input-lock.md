# 统一 v3 构建输入锁

`toolchains/qaplet.lock.json` 是唯一当前输入入口，schema 为
`qaplet.toolchain-lock.v3`。任务 01 迁移的是已验 M0 组合：QuickJS
`e621135dc542e000e31038bedc52944aa0adaf1a`、Tree
`cdabddcf0f783af024481272227c1f56c3d3e48a`、VERSION `2026-06-04`、现有
ARM64 overlay 和固定 cosmocc 原 ZIP。任务 02 已选择目标
`2833510432045f72915919cef680ba59e5153b16`、Tree
`b2763aa1a9395672adc131322f17d7fad7383053`，VERSION/许可和有序 overlay
保持不变；原始与 patched 源码摘要各自更新，旧 source manifest 保存于
`third_party/quickjs/history/`。任务 03 的 Ubuntu ESM/Promise 微型 Host 和受控 APE
通过后，新配方晋级 `pinned`，重新执行普通 producer；02 的受控成绩保留其原输入身份，
不继承旧跨宿主覆盖。实际状态转换与范围见[03 验收](../../.scratch/qaplet-v15-alignment/esm-promise-validation.md)。
`docs/plans/qaplet-toolchain-lock-selected.json` 仍是有未知字段的选型样例，不是可构建锁。
显式候选也必须直接位于某个仓库的 `toolchains/`；解析器和报告防覆盖校验共用物理根目录判定。

审查后的源码选择内部重命名改变了冻结 recipe，已按 selected、受控 17 项实际 producer/startup、
pinned 的流程形成新 input_id；精确引擎/patch/toolchain 保持原值。新身份、晋级原字节与旧报告的
历史范围见[审查修正验收](../../.scratch/qaplet-v15-alignment/review-fixes-validation.md)。

## 字段与独立身份

| 字段 | 合同 |
| --- | --- |
| `lock_state` | `pinned` 或 `selected_pending_validation`；普通构建只接受前者 |
| `manifests` | QuickJS、cosmocc、APE helper 源码、startup loader/Zig 四份旧清单的路径、原始字节长度、SHA-256；旧文件不再独立选版本 |
| `selection.quickjs` | repository、Commit、原始 Git Tree、VERSION、upstream/source 内容清单摘要和有序 patchset 摘要，分别保留 |
| `selection.cosmocc` | 原始 ZIP 长度/SHA、完整组件清单摘要、Run/Artifact 关联、`libc_source: unproven`、无独立签名 attestation |
| `selection.ape_helpers` | 独立源 Commit/Tree、组件清单摘要、provider 原 ZIP SHA；不作为原 Nightly helper/libc 的源码证明 |
| `selection.startup_loaders` | Zig 原件/编译器、Mac helper 源码及目标/flags 配方 |
| `selection.native_toolchain_recipe` | `official-assimilate-linux-x86_64-v1`；每次实际派生的原始/结果 SHA、模式、布局及转换命令继续由 native helper 报告记录 |
| `metadata` | 原 acquisition receipt、原始 license、有序 patch manifest 和每个 patch 的精确原始字节身份 |
| `recipes` | Hello/eval 和共享 helper 的实际构建脚本、C 输入、Makefile 及解析器的逐文件身份，避免更改配方后沿用旧输入身份 |
| `policy` | 构建断网、禁止 rolling fallback、保留原始 Artifact ZIP |
| `archive_retention` | 任务 06 的独立发布门禁，目前 pending / release_ready=false，不阻断已验缓存开发 |

原始文件摘要是文件字节的 SHA-256，长度以字节计。cosmocc/helper 组件清单摘要对其旧清单的
`files` 数组计算：保留数组顺序，JSON object key 排序、无多余空白、`ensure_ascii=true`，
再以 UTF-8 编码并计算 SHA-256。QuickJS upstream/source 内容摘要沿用原有相同 canonical
JSON 算法及其 `files` 顺序；patchset 摘要仍是原 `patches.json` 文件字节摘要，不能互换。

报告中的 `input_lock` 包含锁文件 SHA/长度、schema、state 和 `input_id`。
`input_id` 对 `selection/manifests/metadata/recipes/policy` 五项组成的对象按上述 canonical
JSON 算法求摘要；state 和 retention 独立记录。路径使用仓库相对形式，可在跨宿主比较。
各 producer 自己的 flags、C 源码、独立 helper 结果及制品 SHA 仍保留在原报告合同中。

## 读取、迁移及状态转换

```sh
# 只解析元数据；不读取大缓存、不执行供应工具、不签发产品成绩。
python3 scripts/input_lock.py resolve --report out/lock-resolve.json
python3 scripts/input_lock.py audit --report out/lock-audit.json

# 从现有清单与实际已验源码缓存重放映射；output 必须是 toolchains/ 下新文件。
python3 scripts/input_lock.py migrate --source-cache .cache/quickjs \
  --output toolchains/candidate.lock.json --report out/lock-migration.json
python3 scripts/input_lock.py resolve --lock toolchains/candidate.lock.json \
  --purpose probe --report out/candidate-resolve.json
```

迁移只生成 `selected_pending_validation`，不自动晋级。它检查 patched source 全清单，实际读取
Git Commit/Tree 和 VERSION，保留 Git 命令、退出码及原始日志；所有 metadata/recipe
文件重新摘要。候选先写唯一 staging 文件，完整验证后原子发布；已有 output 拒绝覆盖，失败
不修改原锁、清单或缓存。发布使用同目录 staging 文件及不覆盖既有文件的原子 hard link；并发出现
的 output 也会被保留。报告必须位于仓库 `out/` 或隔离的外部临时目录；与锁/output 重合、指向
链接、位于源码或缓存中的路径只向终端报告拒绝，不写报告；显式 `--source-cache` 位于仓库外时
同样受保护。非对象 JSON 和供应验证异常生成本次
结构化失败，替换旧的通过报告。候选缺字段或未知字段的来源尚未获得时，不能填造摘要使其可消费。

初次迁移的 `pinned` 只对应上述既有 M0 输入；本任务重新执行输入校验、实际 Ubuntu 构建及
断网消费后记录证据。后续任何源码/patch/provider/recipe 变动先成为候选。晋级需要完整统一锁、
原件/组件/模式/链接及 source/patch 身份验证、实际工具链与目标引擎探针，并在同次受审查变更
中记录证据、更新 state；迁移工具没有“跳过验证并 pin”选项。普通 builder、离线构建和 Core
consumer 拒绝 selected。源码和 Nightly CLI 的显式 `fetch` 以及 `verify --controlled-probe`
可验证完整候选输入；这些不是普通产品构建成绩，不能签发 release-ready。

任务 02 增加 builder 的显式 `--controlled-probe`：仍逐次完整验证供应输入、派生 helper
及构建前后身份，只为候选产生标记清楚的真实探针制品。startup 的同名选项允许执行、导出和
只读导入该候选的 guest 证据；Linux ARM64 guest 导出同时验证 producer 中的独立源、patch
及 helper 配方，再比较分发 loader 字节。普通消费者拒绝 controlled 报告；受控模式不能签发
matrix/page-gaps 或完整 Core 成绩。普通 `make check` 的输入 gate 拒绝 selected。
03 已在受控探针通过后晋级，并使用新的 pinned 锁身份重新构建和执行适用回归；候选原报告不重标。

源码 `select --commit <exact>` 仅生成隔离的新 source candidate，不解析 moving master；
`fetch --mirror <physical-local-git-repository>` 从已缓存 Git 对象离线重新物化，仍对比锁定的
Commit/Tree、原始内容、ordered patch 与 patched 内容，并保留官方锁身份。新的
`engine_compatibility_id` 绑定精确 commit/原始/叠加/patch，进入 build fingerprint；相同 VERSION
不表示可复用字节码或对象。当前没有持久字节码 reader/cache，对象一律使用新的唯一构建目录。

旧 `--lock` 只接受与 v3 绑定身份一致的清单副本，不能另选版本。历史供应单元 fixture 显式使用
`--fixture-lock`，仅能作用于非生产清单路径；报告标注 fixture_only，普通 producer 要求对应
v3 身份，拒绝把 fixture 报告用作构建输入。生产 `bump` 和 rolling `download` 均被拒绝。
固定 Artifact 的显式获取仍用 `fetch_locked_toolchain.py`。

导入 producer 的派生启动 helper 和分发 `helpers.json` 必须携带当前 v3 身份；显式候选或其他
组合会在使用前拒绝。派生 helper 的源 Commit/Tree、独立来源范围、原清单与有序 patch 身份
另与 v3 对照。旧派生 bundle 保留原验收记录，须重新生成后才能作为当前组合分发；官方原版
loader 的直接消费仍按绑定的 Nightly 组件清单验证。Core 同时核对供应报告、host context、
bundle 以及选中 producer 的实际派生 loader 身份。
helper 校验按目标与原件字节区分官方组件、Linux ARM64 派生物和 Zig 生成的 Apple ARM64
派生物，各自强制所需来源和配方字段，不能通过删除 `source` 改变类别。

所有当前 producer/host/bundle 证据消费均要求当前 v3 身份。没有 v3 身份的历史报告保留原记录，
不能在当前入口被附上新组合重新签发通过；这不修改旧验收文件或结论。

## 入口覆盖与缓存

| 入口 | v3 解析与验证落点 |
| --- | --- |
| Makefile 的 check-ci / CI 中的 make check | `input_lock.py audit`；dev preflight 也验证锁与旧权威读取扫描 |
| QuickJS verify/fetch、toolchains bootstrap/verify、Nightly verify/fetch/path | 供应 CLI 先解析完整 v3，随后验证缓存；QuickJS 另核对实际 Commit/Tree/VERSION |
| `fetch_locked_toolchain.py` | 从 v3 解析固定 Artifact 原件；令牌仍只在环境/stdin 中传递 |
| `build_ape.py`、native I/O、pages64k、SCM/Job producer | 共用 `verify_inputs`，构建前后比较同一 v3 身份，拒绝不一致的供应报告 |
| `ape_helpers.py`、`startup_loaders.py`、`native_toolchain.py` | helper 清单从 v3 解析；imported producer/bundle 强制当前 v3 与独立源/patch；native derivation 接收上游已验组件，保留独立派生身份 |
| `offline_integrity.py` | v3 选择原缓存，recipe inventory 包含统一锁和解析器；双构建/负向 fixture 保留既有合同 |
| startup / native I/O / pages64k / SCM/Job 消费者 | producer 的 v3 身份必须存在且一致；缺失身份的历史报告拒绝重标为当前成绩，原记录继续保留 |
| `core_gate.py` / `check_evidence.py` | Core 只读解析 v3，核对当前组件和新 producer 身份，严格回归仍需显式选中完整证据 |

`audit` 检测脚本中直接读取旧清单的 JSON 调用，覆盖字面路径及直接赋值的路径变量；它是
可执行迁移护栏，不是 Python 任意动态代码的安全证明。现有 JSON CLI fixture seam 明确隔离。
CI workflow 继续调用这些共享入口，本任务不改变分级调度或远程验收结论。

现有 `.cache/quickjs/<commit>-<patch-prefix>/source`、
`.cache/toolchains/cosmocc/<archive-sha>/{archive.zip,tree}`、派生 native view、
`.cache/sources/ape-loader/<commit>` 和 Zig 布局不变，不重打包 ZIP、不搬迁缓存或补做 chmod。
缺失/损坏只会拒绝；普通构建不隐式联网或修复。复用缓存仍逐次验证完整内容、模式/链接与
实际工具行为；DrvFS 限制遵循[环境合同](environment.md)。新构建在 `.cache/build/` 的唯一
工作目录生成，不混用旧对象。回退时恢复消费者和旧入口版本，保留新锁、原清单及所有原始缓存
和历史证据供审计。
