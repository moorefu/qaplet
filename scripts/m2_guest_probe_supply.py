#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Supply existing independent backend probes with retained original packers.

The adapter is an explicit additional build input. It preserves all Python
helpers and backend C/header inputs before entering the existing Ubuntu build
entry point, and routes its linker through sdk_link's diagnostic retention.
The original producer report is kept unchanged beside this adapter receipt.
"""
import argparse
import importlib
import json
from pathlib import Path
import shutil
import sys

from build_ape import identity
from foundation import ROOT, now
from nightly_toolchain import write_json
import sdk_link


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--probe", choices=("waker", "process", "stdio", "tty", "pages64k", "cli"), required=True)
    parser.add_argument("--cache-root", type=Path, required=True)
    parser.add_argument("--output-root", type=Path, required=True)
    args = parser.parse_args()
    output = args.output_root.resolve()
    if output.exists():
        parser.error("Supply output must be new")
    output.mkdir(parents=True)
    receipt = {"schema_version": 1, "report_kind": "m2-guest-probe-supply", "status": "running",
               "started_at": now(), "probe": args.probe, "adapter": identity(Path(__file__)),
               "invocation": {"argv": list(sys.argv), "cwd": str(Path.cwd())},
               "link_adapter": "sdk_link.link_portable retains original-packer before temporary inputs disappear",
               "sdk_archive": "not_applicable: independent OS probe links only locked toolchain libc",
               "inputs": []}
    sources = sorted((ROOT / "scripts").glob("*.py"))
    sources += sorted((ROOT / "tests/native").glob("m2_*.c"))
    sources += sorted((ROOT / "tests/native").glob("m2_*.h"))
    if args.probe == "pages64k":
        sources.append(ROOT / "tests/native/pages64k.c")
    if args.probe == "cli":
        sources.append(ROOT / "tests/native/sdk_guest_cli.c")
    for source in sources:
        target = output / "inputs" / source.relative_to(ROOT)
        target.parent.mkdir(parents=True, exist_ok=True)
        before = identity(source)
        shutil.copyfile(source, target)
        retained = identity(target)
        if any(before[key] != retained[key] for key in ("sha256", "size_bytes")):
            raise ValueError("Build input changed while retaining " + str(source))
        receipt["inputs"].append({"original": before, "retained": retained})
    write_json(output / "supply.json", receipt)
    module_name = "pages64k" if args.probe == "pages64k" else "sdk_guest_cli" if args.probe == "cli" else "m2_" + args.probe
    module = importlib.import_module(module_name)
    module.link_portable = sdk_link.link_portable
    cache = args.cache_root.resolve()
    sys.argv = [str(ROOT / "scripts" / (module_name + ".py")), "build",
                "--toolchain-cache", str(cache / "toolchains/cosmocc"),
                "--helper-source-cache", str(cache / "sources/ape-loader"),
                "--build-dir", str(cache / "build/m2-25-backend" / args.probe),
                "--output-dir", str(output / "artifacts"), "--report", str(output / "build.json")]
    receipt["build_invocation"] = {"argv": list(sys.argv), "cwd": str(Path.cwd())}
    write_json(output / "supply.json", receipt)
    code = module.main()
    product = json.loads((output / "build.json").read_text(encoding="utf-8"))
    receipt.update(producer=identity(output / "build.json"), packing=product.get("packing", []),
                   status=product["status"], finished_at=now())
    loaded = {str(Path(value.__file__).resolve()) for value in sys.modules.values()
              if getattr(value, "__file__", None) and Path(value.__file__).resolve().is_relative_to(ROOT)}
    receipt["loaded_python_inputs"] = sorted(loaded)
    for row in receipt["inputs"]:
        source = Path(row["original"]["path"])
        if (str(source) in loaded or source.suffix in (".c", ".h")) and identity(source) != row["original"]:
            raise ValueError("Source changed during supply: " + row["original"]["path"])
    if code == 0 and (not receipt["packing"] or any(not row.get("original_packer_output") for row in receipt["packing"])):
        receipt.update(status="fail", reason="Missing original packer diagnostics")
        code = 1
    write_json(output / "supply.json", receipt)
    return code


if __name__ == "__main__":
    sys.exit(main())
