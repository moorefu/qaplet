# 第三方许可与实际 M0 输入

根级 MIT 许可适用于 Qaplet 自有代码和文档，不替换第三方软件的许可。

当前机器可读清单由 `scripts/core_gate.py evaluate` 的 `core.dependencies` 从选中 producer、已提交输入锁、实际 link map/symbol 日志及分发 notice 生成。它区分交付依赖、构建工具和验证环境，不是正式发行 SBOM。

| 组件 | 实际身份与许可范围 |
| --- | --- |
| QuickJS | Bellard commit `e621135dc542e000e31038bedc52944aa0adaf1a`；原始 MIT 许可位于 `third_party/quickjs/LICENSE`，SHA-256 `598fd7fc928e4350abce36e337ba5a1346923c5c692f5be92c3d8e29ddd7c18d`；有序 ARM64 full-pointer overlay 独立记录于 `patches.json`，不改写上游身份。 |
| Nightly / libcosmo | 原始官方 Nightly artifact 来自 run `41169443573`，归档 SHA-256 `f2f984d3a9790674fc0da11c1c00a32d4fe3f8dafde99af74b0c6dc24a2a16cf`。两个 CPU 的 libcosmo.a、工具、归档许可文件均按 `toolchains/cosmocc.lock.json` 记录；运行库按各组件 embedded notice 适用，不能套用项目 MIT 或独立 helper 的 ISC。完整 libc 源码关联仍为 `unproven`。 |
| 独立 APE helper 源码 | 固定 Cosmopolitan commit `aefad27c09ffda64c6ac64bd69f758e9f9c7b91d`；loader/packer 源码闭包逐文件声明 ISC/MIT，包括 packer 的 HACL* MIT notice。来源和 ordered patch 见 `third_party/ape-helpers/`；交付的 `artifacts/ape-helper-notices/` 保留 LICENSE、source.lock.json 和 HACL-LICENSE.txt。此许可声明不外推到整个 Nightly libc。 |
| Zig | `toolchains/startup-loaders.lock.json` 固定 0.15.2 原归档/编译器；MIT，归档内其他组件保留各自许可。仅用于 Ubuntu 构建独立 Apple Silicon loader，不是所有 Core APE 的运行库声明。 |
| Guest / QEMU | `guests/arm64.lock.json` 固定 Ubuntu package、签名链和 QEMU 配置；实际报告记录 kernel/config/rootfs 与 QEMU executable SHA。它们属于验证环境，分别遵循原发行包许可，不计为嵌入 Core 的依赖。 |

网络 TLS backend、版本及 CVE 适用性仍为 unknown/unverified。计划中 Mbed TLS 2.26.0 的历史观察没有与本次 Nightly 建立映射；`pthread.tls` 等线程局部存储用例不能当作网络 TLS 验证。M4 必须依据实际交付代码确认 TLS 来源、许可、版本与漏洞适用性；M0 不选择或实现后端。

Python、GNU Make、GCC/Clang、Git、WSL 是外部开发工具，继续遵循各自发行包许可。bootstrap 二进制仅为开发证据。

GitHub Actions 引用官方 [checkout](https://github.com/actions/checkout)、[setup-python](https://github.com/actions/setup-python)、[upload-artifact](https://github.com/actions/upload-artifact) 和 [download-artifact](https://github.com/actions/download-artifact)；它们的源码与许可由各自上游管理，没有复制入库。

后续发行仍须对实际纳入的组件补齐完整许可/notice 与源码义务审计；上述未知项保持公开，不能从归档获取成功推断完整 provenance。
