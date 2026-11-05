#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Retain SDK link diagnostics while reusing the frozen portable linker."""
import shutil

from build_ape import BuildError, identity, link_portable as portable_link
from foundation import sha256


def link_portable(args, report, directory, tree, helpers, name, objects, destination, work, environment,
                  libraries=("-lm", "-lpthread")):
    start = len(report["commands"])
    try:
        return portable_link(args, report, directory, tree, helpers, name, objects,
                             destination, work, environment, libraries=libraries)
    finally:
        # The frozen linker creates this raw copy before its repack invocation.
        # Preserve it on success and repack failure, before any temporary work
        # directory can disappear. Never reconstruct a missing old diagnostic.
        original = destination.with_suffix(".original-packer.com")
        if original.is_file():
            retained = directory / "packing" / (name + ".original-packer.com")
            retained.parent.mkdir(parents=True, exist_ok=True)
            if retained.exists():
                raise BuildError("SDK packer evidence requires a unique link name: " + name)
            shutil.copyfile(original, retained)
            if sha256(retained) != sha256(original):
                raise BuildError("Original SDK packer diagnostic changed while preserving it")
            packing = report.setdefault("packing", [])
            row = next((row for row in packing if row["name"] == name), None)
            if row is None:
                row = {"name": name, "repacked_output": None, "status": "incomplete",
                       "commands": report["commands"][start:]}
                packing.append(row)
            row.update(original_packer_output=identity(retained), original_packer_work_path=str(original))
