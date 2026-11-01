# 统一 v3 构建输入锁

`toolchains/qaplet.lock.json` 是唯一当前输入入口，schema 为
`qaplet.toolchain-lock.v3`。任务 01 迁移的是已验 M0 组合：QuickJS
`e621135dc542e000e31038bedc52944aa0adaf1a`、Tree
`cdabddcf0f783af024481272227c1f56c3d3e48a`、VERSION `2026-06-04`、现有
ARM64 overlay 和固定 cosmocc 原 ZIP。任务 02 的目标
`2833510432045f72915919cef680ba59e5153b16` 仍待升级；
`docs/plans/qaplet-toolchain-lock-selected.json` 仍是有未知字段的选型样例，不是可构建锁。

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

旧 `--lock` 只接受与 v3 绑定身份一致的清单副本，不能另选版本。历史供应单元 fixture 显式使用
`--fixture-lock`，仅能作用于非生产清单路径；报告标注 fixture_only，普通 producer 要求对应
v3 身份，拒绝把 fixture 报告用作构建输入。生产 `bump` 和 rolling `download` 均被拒绝。
固定 Artifact 的显式获取仍用 `fetch_locked_toolchain.py`。

## 入口覆盖与缓存

| 入口 | v3 解析与验证落点 |
| --- | --- |
| Makefile 的 check-ci / CI 中的 make check | `input_lock.py audit`；dev preflight 也验证锁与旧权威读取扫描 |
| QuickJS verify/fetch、toolchains bootstrap/verify、Nightly verify/fetch/path | 供应 CLI 先解析完整 v3，随后验证缓存；QuickJS 另核对实际 Commit/Tree/VERSION |
| `fetch_locked_toolchain.py` | 从 v3 解析固定 Artifact 原件；令牌仍只在环境/stdin 中传递 |
| `build_ape.py`、native I/O、pages64k、SCM/Job producer | 共用 `verify_inputs`，构建前后比较同一 v3 身份，拒绝不一致的供应报告 |
| `ape_helpers.py`、`startup_loaders.py`、`native_toolchain.py` | helper 清单从 v3 解析；native derivation 接收上游已验的 v3 组件，保留独立派生身份 |
| `offline_integrity.py` | v3 选择原缓存，recipe inventory 包含统一锁和解析器；双构建/负向 fixture 保留既有合同 |
| startup / native I/O / pages64k / SCM/Job 消费者 | 新 producer 的 v3 身份必须一致；历史报告保留 legacy-component-identities 范围，不被改写成新成绩 |
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
