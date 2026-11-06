#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Compile the bounded shared mixed fixture fix against frozen public archives.

This is an explicit source-compiled fixture, not a rebuilt/retagged SDK supply.
The original sealed SDK is untouched and all derived bytes are retained before
entering the first compiler. Use the ordinary sdk.py mixed-consume arguments.
"""
from pathlib import Path
import shutil
import sys

from build_ape import identity
from foundation import ROOT
import sdk
import sdk_mixed

original_compile = sdk_mixed.compile_consumers


def compile_fixed(args, report, directory, tree, helpers, public_sdk, work, output):
    derived = work / "derived-sdk"
    shutil.copytree(public_sdk, derived)
    source = ROOT / "tests/native/sdk_mixed.c"
    destination = derived / "host-mixed.c"
    shutil.copyfile(source, destination)
    retained = directory / "fixture-inputs" / "host-mixed.c"
    retained.parent.mkdir(parents=True)
    shutil.copyfile(destination, retained)
    report["source_compiled_fixture"] = {"source": identity(source), "compiled": identity(destination),
        "retained": identity(retained), "original_sdk_member": identity(public_sdk / "host-mixed.c"),
        "scope": "Only shared mixed child fixture differs; supplied header and both archives unchanged; not SKU3 supplied Host"}
    if any(report["source_compiled_fixture"][key]["sha256"] != identity(source)["sha256"] for key in ("compiled", "retained")):
        raise ValueError("Changed source while retaining shared fixture")
    original_compile(args, report, directory, tree, helpers, derived, work / "compile", output)


if __name__ == "__main__":
    sdk_mixed.compile_consumers = compile_fixed
    sys.exit(sdk.main())
