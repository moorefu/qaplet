# SPDX-License-Identifier: MIT
"""An independently checked Linux ELF derivation of verified Nightly tools.

The official archive/tree remain authoritative and untouched. Receipts are
provenance, never a cache integrity authority: expected derived bytes are
recomputed from the original encoded ELF header and unchanged original body.
"""
import codecs
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import re
import shutil
import struct
import uuid

from foundation import now, sha256
from nightly_toolchain import write_json

RECIPE = "official-assimilate-linux-x86_64-v1"


def file_identity(path):
    return {"path": str(path.absolute()), "sha256": sha256(path), "size_bytes": path.stat().st_size}


def derived_identity(path, entry):
    with path.open("rb") as stream:
        start = stream.read(16384)
        headers = [codecs.escape_decode(match[1])[0] for match in re.finditer(rb"printf '([^']*)'", start)]
        headers = [header[:64] for header in headers if len(header) >= 64 and
                   header[:6] == b"\x7fELF\x02\x01" and struct.unpack_from("<HH", header, 16) == (2, 62)]
        if len(headers) != 1:
            raise ValueError("Expected one original x86_64 ELF64 header: " + entry["path"])
        native = bytearray(headers[0])
        native[7] = 0  # Official assimilate -e (without -b) clears FreeBSD OSABI.
        expected = hashlib.sha256(native)
        original = hashlib.sha256(start[:64])
        stream.seek(64)
        while block := stream.read(1024 * 1024):
            expected.update(block)
            original.update(block)
    if original.hexdigest() != entry["sha256"] or path.stat().st_size != entry["size_bytes"]:
        raise ValueError("Original changed during native derivation: " + entry["path"])
    return {"path": entry["path"], "source_sha256": entry["sha256"],
            "derived_sha256": expected.hexdigest(), "size_bytes": entry["size_bytes"],
            "format": "ELF64 little-endian EXEC x86_64", "header_hex": native.hex()}


def plan_view(original, lock):
    entries = {entry["path"]: entry for entry in lock["files"]}
    executables = {name for name, entry in entries.items() if entry["kind"] == "file" and entry["executable"]}
    helpers = {}
    for name in sorted(executables):
        with (original / name).open("rb") as stream:
            magic = stream.read(8)
        if magic.startswith((b"MZ", b"jartsr")):
            helpers[name] = derived_identity(original / name, entries[name])
    if not helpers or "bin/mktemper" not in helpers:
        raise ValueError("Locked toolchain has no expected executable APE helpers")
    sensitive = set(executables)
    for name, entry in entries.items():
        if entry["kind"] == "symlink":
            target = (original / name).resolve(strict=True)
            if not target.is_relative_to(original):
                raise ValueError("Original toolchain link escaped during derivation")
            relative = target.relative_to(original).as_posix()
            if any(item == relative or item.startswith(relative + "/") for item in executables):
                sensitive.add(name)
    expanded = {""}
    for name in sensitive:
        expanded.update(parent.as_posix() for parent in PurePosixPath(name).parents if parent.as_posix() != ".")
    children = {}
    for name in entries:
        parts = PurePosixPath(name).parts
        for index in range(len(parts)):
            parent = "/".join(parts[:index])
            children.setdefault(parent, set()).add("/".join(parts[:index + 1]))
    plan = []
    def visit(parent):
        for name in sorted(children.get(parent, ())):
            entry = entries.get(name)
            if entry is None:
                kind = "directory" if name in expanded else "data-link"
            elif entry["kind"] == "symlink":
                kind = "archive-link"
            elif name in helpers:
                kind = "native-executable"
            elif name in executables:
                kind = "copied-executable"
            else:
                kind = "data-link"
            plan.append({"path": name, "kind": kind})
            if kind == "directory":
                visit(name)
    visit("")
    return entries, helpers, plan


def check_view(view, original, entries, helpers, plan):
    actual = {path.relative_to(view).as_posix() for path in view.rglob("*")}
    if actual != {item["path"] for item in plan}:
        raise ValueError("Derived native toolchain layout differs; refusing unverified cache")
    for item in plan:
        name, kind = item["path"], item["kind"]
        path = view / name
        if kind == "directory":
            if path.is_symlink() or not path.is_dir():
                raise ValueError("Derived directory changed: " + name)
        elif kind in ("data-link", "archive-link"):
            expected = (entries[name]["target"] if kind == "archive-link" else
                        os.path.relpath(original / name, path.parent))
            if not path.is_symlink() or os.readlink(path) != expected:
                raise ValueError("Derived safe link changed: " + name)
            resolved = path.resolve(strict=True)
            if not (resolved.is_relative_to(view) or resolved.is_relative_to(original)):
                raise ValueError("Derived link escaped verified trees: " + name)
        else:
            expected = helpers[name]["derived_sha256"] if kind == "native-executable" else entries[name]["sha256"]
            if (path.is_symlink() or not path.is_file() or path.stat().st_size != entries[name]["size_bytes"]
                    or sha256(path) != expected or path.stat().st_mode & 0o222):
                raise ValueError("Derived executable differs/is writable: " + name)


def prepare(original, cache_root, lock, report, directory, run, timeout=300):
    original = original.resolve()
    entries, helpers, plan = plan_view(original, lock)
    parent = cache_root.resolve() / RECIPE / lock["archive"]["sha256"]
    view = parent / "tree"
    if parent.parent.is_symlink() or parent.is_symlink() or view.is_symlink():
        raise ValueError("Derived native cache root must not be a symlink")
    creation = None
    if not view.exists():
        required = sum(entries[item["path"]]["size_bytes"] for item in plan
                       if item["kind"] in ("native-executable", "copied-executable"))
        cache_root.mkdir(parents=True, exist_ok=True)
        if shutil.disk_usage(cache_root).free < required + 64 * 1024**2:
            raise ValueError("Insufficient repository cache space for native tool derivation")
        parent.parent.mkdir(parents=True, exist_ok=True)
        candidate = parent.parent / (".native-candidate-" + uuid.uuid4().hex)
        target = candidate / "tree"
        target.mkdir(parents=True)
        started, first = now(), len(report["commands"])
        for index, item in enumerate(plan):
            name, kind = item["path"], item["kind"]
            path = target / name
            if kind == "directory":
                path.mkdir()
            elif kind == "data-link":
                path.symlink_to(os.path.relpath(original / name, path.parent), target_is_directory=(original / name).is_dir())
            elif kind == "archive-link":
                path.symlink_to(entries[name]["target"], target_is_directory=(original / name).is_dir())
            else:
                if kind == "native-executable":
                    run(report, directory, "assimilate-" + str(index),
                        [original / "bin/ape-x86_64.elf", original / "bin/assimilate",
                         "-e", "-x", "-o", path, original / name], timeout)
                else:
                    shutil.copyfile(original / name, path)
                path.chmod(0o555)
        check_view(target, original, entries, helpers, plan)
        creation = {"recipe": RECIPE, "started_at": started, "finished_at": now(),
                    "source_archive_sha256": lock["archive"]["sha256"],
                    "assimilator": file_identity(original / "bin/assimilate"),
                    "launcher": file_identity(original / "bin/ape-x86_64.elf"),
                    "commands": report["commands"][first:]}
        write_json(candidate / "derivation.json", creation)
        parent.parent.mkdir(parents=True, exist_ok=True)
        if parent.exists():
            raise ValueError("Native derivation was concurrently published; candidate retained for explicit inspection")
        os.rename(candidate, parent)
    check_view(view, original, entries, helpers, plan)
    receipt = parent / "derivation.json"
    result = {"kind": "derived-linux-elf-x86_64", "recipe": RECIPE, "tree": str(view),
              "original_tree": str(original), "source_archive_sha256": lock["archive"]["sha256"],
              "helpers": list(helpers.values()), "receipt": file_identity(receipt),
              "copied_executables": [{"path": item["path"], "sha256": entries[item["path"]]["sha256"],
                                       "size_bytes": entries[item["path"]]["size_bytes"]} for item in plan
                                      if item["kind"] == "copied-executable"],
              "receipt_authority": "Provenance only; expected hashes recomputed from original headers and complete bodies",
              "layout_sha256": hashlib.sha256(json.dumps(plan, sort_keys=True).encode()).hexdigest(),
              "helper_set_sha256": hashlib.sha256(json.dumps(list(helpers.values()), sort_keys=True).encode()).hexdigest(),
              "conversion_commands": creation["commands"] if creation else [], "cache_reused": creation is None}
    report["native_toolchain"] = result
    return view


def recheck(native, original, lock):
    entries, helpers, plan = plan_view(original, lock)
    view = Path(native["tree"])
    if view.is_symlink() or not view.is_dir():
        raise ValueError("Derived native cache disappeared/changed during build")
    check_view(view, original, entries, helpers, plan)
    current = hashlib.sha256(json.dumps(list(helpers.values()), sort_keys=True).encode()).hexdigest()
    if current != native["helper_set_sha256"]:
        raise ValueError("Derived native input identities changed during build")
    native["verified_after_build_at"] = now()
