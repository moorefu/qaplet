# Source-built APE helpers

`source.lock.json` independently pins official Cosmopolitan commit
`aefad27c09ffda64c6ac64bd69f758e9f9c7b91d` and all 26 acquired source, header,
license and recipe files by SHA-256 and Git blob identity. This identifies the
new helpers; bundled Nightly libc provenance remains `unproven`.

Explicit acquisition and offline verification:

```sh
python3 scripts/ape_helpers.py fetch --report out/ape-helper-fetch.json
python3 scripts/ape_helpers.py verify --report out/ape-helper-verify.json
```

The default source root is `.cache/sources/ape-loader/<commit>/`. Worktrees pass
`--helper-source-cache` to the main repository cache for each producer. All
compilation happens on Ubuntu and all objects stay under `.cache/build/`.
Final artifacts and evidence go under `out/`.

`scripts/ape_helpers.py` compiles the freestanding ARM64 loader from pristine
official sources, with both link page settings at 65536. It retains Linux ELF
OSABI0, records actual program headers and relocations, and omits the upstream
FreeBSD/TLS postprocessing. The original loader and archive remain unchanged.

The ordered patch manifest applies one reviewed change to an independent copy
of `tool/build/apelink.c`: retain the maximum PT_LOAD alignment during merging
and packing, and preserve the first copied offset's residue when rebasing.
Virtual addresses and segment protections remain separate. Original and
patched source hashes, compiler commands, effective header closure and helper
hashes are recorded. Official sources use C23 `bool`; the host build therefore
selects `-std=gnu23`.

The shared producer uses the unmodified official `cosmocc` driver with
MAXPAGESIZE=65536, retaining its per-CPU COMMONPAGESIZE settings. It then repacks
the two actual sidecars with the source-built packer and embeds the new ARM64
loader. Applying COMMONPAGESIZE=65536 universally causes real x86 PC16 linker
overflow and is not this recipe. Actual sidecar and final fat LOADs must pass
64K congruence and protection checks.

The root ISC license is preserved in `LICENSE`. HACL source files retain their
original MIT grants and notices; the HACL notice unit remains linked into the
host packer. Producer distributions include source metadata and license text.
All five independent producers (Hello/eval, native I/O, pages64k, SCM and Job)
publish `ape-helper-notices/{LICENSE,source.lock.json,HACL-LICENSE.txt}` beside
their loader. The shared publication function also requires one map per CPU;
each producer retains its own C recipe, audit and report.
The source research and real failure/regression evidence are recorded in
[the M0 notes](../../.scratch/qaplet-m0/arm64-64k-validation.md).
