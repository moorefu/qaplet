#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Pinned source-built Linux ARM64 loader and alignment-preserving APE packer.

These are independently derived helpers, not provenance claims about Nightly's
bundled libc or tools. Original supplier caches are never modified.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import shutil
import struct
import sys
import uuid

from foundation import ROOT, host_facts, now, sha256
from evidence import read_json
from nightly_toolchain import write_json

LOCK = ROOT / "third_party/ape-helpers/source.lock.json"
PATCH = ROOT / "third_party/ape-helpers/patches/0001-preserve-load-alignment.patch"
LOADER_UNITS = ("ape/start.S", "ape/loader.c", "ape/launch.S", "ape/systemcall.S")
PACKER_UNITS = ("tool/build/apelink.c", "tool/build/lib/parsesupportvector.c", "third_party/haclstar/blake2b.c", "third_party/haclstar/notice.c")


def identity(path):
    return {"path": str(path.resolve()), "sha256": sha256(path), "size_bytes": path.stat().st_size}


def source_cache(root):
    return root / read_json(LOCK)["commit"]


def verify_source(cache):
    from build_ape import BuildError
    lock = read_json(LOCK)
    overlay = read_json(LOCK.parent / "patches.json")
    if overlay.get("schema_version") != 1 or len(overlay.get("patches", [])) != 1:
        raise BuildError("Expected one ordered reviewed packer patch")
    for entry in overlay["patches"]:
        if sha256(LOCK.parent / entry["path"]) != entry["sha256"] or sha256(cache / entry["source"]) != entry["source_sha256"]:
            raise BuildError("Ordered packer overlay identity differs from manifest")
    for entry in lock["files"]:
        path = cache / entry["path"]
        if not path.is_file():
            raise BuildError("Missing locked APE helper source: " + str(path), "blocked")
        content = path.read_bytes()
        blob = hashlib.sha1(b"blob " + str(len(content)).encode() + b"\0" + content).hexdigest()
        if len(content) != entry["size_bytes"] or hashlib.sha256(content).hexdigest() != entry["sha256"] or blob != entry["git_blob_sha1"]:
            raise BuildError("APE helper source identity mismatch: " + entry["path"])
    return {"lock": identity(LOCK), "commit": lock["commit"], "git_tree": lock["git_tree"],
            "files": [{**entry, "path": str(cache / entry["path"])} for entry in lock["files"]],
            "identity_scope": lock["source_identity_scope"], "patches": overlay["patches"], "patch_manifest": identity(LOCK.parent / "patches.json")}


def loads(data, page_size=65536, require_arm64=False):
    from build_ape import BuildError
    if data[:6] != b"\x7fELF\x02\x01":
        raise BuildError("Expected ELF64 little-endian image")
    machine = struct.unpack_from("<H", data, 18)[0]
    if require_arm64 and machine != 183:
        raise BuildError("Expected AArch64 ELF machine")
    offset = struct.unpack_from("<Q", data, 32)[0]
    count, width = struct.unpack_from("<H", data, 56)[0], struct.unpack_from("<H", data, 54)[0]
    if width != 56 or not count or offset + count * width > len(data):
        raise BuildError("Invalid ELF program header bounds")
    result = []
    entry = struct.unpack_from("<Q", data, 24)[0]
    for index in range(count):
        kind, flags, off, address, _, size, memory, align = struct.unpack_from("<IIQQQQQQ", data, offset + index * width)
        if kind == 1:
            if not align or align & (align - 1) or off + size > len(data) or size > memory or align < page_size or (off - address) % page_size or (off - address) % align:
                raise BuildError("ELF PT_LOAD loses " + str(page_size) + "-byte alignment/congruence")
            result.append({"offset": off, "virtual_address": address, "size_bytes": size, "memory_size": memory,
                           "alignment": align, "flags": flags, "sha256": hashlib.sha256(data[off:off + size]).hexdigest()})
    if not result or not any(item["flags"] & 1 and item["virtual_address"] <= entry < item["virtual_address"] + item["memory_size"] for item in result):
        raise BuildError("ELF entry is outside executable LOAD")
    # Protection classes must not share a rounded kernel page.
    for left, right in zip(result, result[1:]):
        if left["flags"] != right["flags"] and (left["virtual_address"] + left["memory_size"] + page_size - 1) // page_size > right["virtual_address"] // page_size:
            raise BuildError("Different ELF protections overlap a kernel page")
    return result


def prepare(args, report, directory, tree, run):
    from build_ape import BuildError
    if platform.system() != "Linux" or platform.freedesktop_os_release().get("ID") != "ubuntu":
        raise BuildError("APE helper builds require Ubuntu", "blocked")
    cache = source_cache(args.helper_source_cache.resolve())
    sources = verify_source(cache)
    base = (args.build_dir or ROOT / ".cache/build").resolve()
    work = base / (report["report_id"] + "-ape-helpers")
    work.mkdir(parents=True)
    (work / "tmp").mkdir()
    env = dict(os.environ, TMPDIR=str(work / "tmp"), LC_ALL="C", TZ="UTC", SOURCE_DATE_EPOCH="0")
    prefix = ["-ffile-prefix-map=" + str(cache) + "=ape-source", "-ffile-prefix-map=" + str(work) + "=ape-build"]
    cc = tree / "bin/aarch64-linux-cosmo-gcc"
    objects = []
    for unit in LOADER_UNITS:
        obj = work / (Path(unit).stem + ".o")
        flags = ["-iquote", cache, "-fpie", "-mgeneral-regs-only", "-g", *prefix]
        if unit.endswith(".c"):
            flags += ["-DNDEBUG", "-DSUPPORT_VECTOR=33", "-Wall", "-Wextra", "-Os", "-ffreestanding",
                      "-fno-asynchronous-unwind-tables", "-fno-stack-protector", "-fno-ident", "-fno-gnu-unique"]
        run(report, directory, "helper-" + Path(unit).stem, [cc, *flags, "-MD", "-MF", str(obj) + ".d", "-c", cache / unit, "-o", obj], args.compile_timeout, work, env)
        objects.append(obj)
    debug = work / "ape-aarch64.elf.dbg"
    loader = work / "ape-aarch64.elf"
    run(report, directory, "helper-loader-link", [tree / "bin/aarch64-linux-cosmo-ld.bfd", "-pie", "-static", "-nostdlib", "--no-dynamic-linker", "-z", "norelro", "-z", "common-page-size=0x10000", "-z", "max-page-size=0x10000", "-o", debug, *objects], args.compile_timeout, work, env)
    run(report, directory, "helper-loader-strip", [tree / "bin/aarch64-linux-cosmo-objcopy", "-g", debug, loader], args.compile_timeout, work, env)
    loader_loads = loads(loader.read_bytes(), require_arm64=True)
    if loader.read_bytes()[7] != 0:
        raise BuildError("Source-built native Linux loader requires ELF OSABI0")
    run(report, directory, "helper-loader-readelf", [tree / "bin/aarch64-linux-cosmo-readelf", "-h", "-l", "-d", "-r", loader], args.timeout, work, env)
    # Apply an ordered reviewed source patch to an independent working copy.
    patched = work / "source"
    (patched / "tool/build").mkdir(parents=True)
    shutil.copyfile(cache / "tool/build/apelink.c", patched / "tool/build/apelink.c")
    run(report, directory, "helper-patch", ["patch", "--batch", "--fuzz=0", "-p1", "-i", PATCH], args.timeout, patched, env)
    if sha256(patched / "tool/build/apelink.c") != sources["patches"][0]["result_sha256"]:
        raise BuildError("Patched packer source differs from reviewed result")
    cc = tree / "bin/x86_64-linux-cosmo-gcc"
    objects = []
    for unit in PACKER_UNITS:
        selected = patched / unit if unit == "tool/build/apelink.c" else cache / unit
        obj = work / ("packer-" + Path(unit).stem + ".o")
        flags = ["-fportcosmo", "-std=gnu23", "-O2" if unit.endswith("blake2b.c") else "-Os", "-g", "-fno-pie",
                 "-D__COSMOPOLITAN__", "-D__COSMOCC__", "-D_COSMO_SOURCE", "-nostdinc", "-iquote", tree / "include", "-iquote", cache,
                 "-isystem", tree / "include", "-include", "libc/integral/normalize.inc", "-mno-red-zone", "-mno-tls-direct-seg-refs", "-fno-semantic-interposition", "-ffunction-sections", "-fdata-sections", *prefix]
        run(report, directory, "helper-packer-" + Path(unit).stem, [cc, *flags, "-MD", "-MF", str(obj) + ".d", "-c", selected, "-o", obj], args.compile_timeout, work, env)
        objects.append(obj)
    lib = tree / "x86_64-linux-cosmo/lib"
    packer = work / "apelink-linux-x86_64"
    run(report, directory, "helper-packer-link", [cc, "-static", "-nostdlib", "-no-pie", "-fuse-ld=bfd", "-Wl,-z,noexecstack", "-Wl,-z,norelro", "-Wl,--gc-sections", "-Wl,-T," + str(lib / "ape.lds"), "-Wl,-z,common-page-size=4096", "-Wl,-z,max-page-size=16384", "-o", packer, lib / "ape.o", lib / "crt.o", *objects, lib / "libcosmo.a"], args.compile_timeout, work, env)
    run(report, directory, "helper-packer-fixup", [tree / "bin/fixupobj", packer], args.timeout, work, env)
    run(report, directory, "helper-packer-help", [packer, "-h"], args.timeout, work, env)
    dependencies = []
    for depfile in sorted(work.glob("*.d")):
        text = depfile.read_text().replace("\\\n", " ").split(":", 1)[1]
        for name in text.split():
            path = Path(name)
            if not path.is_absolute():
                path = work / path
            if path.is_file():
                dependencies.append(identity(path))
    unique = {item["path"]: item for item in dependencies}
    result = {"source": sources, "loader": identity(loader), "packer": identity(packer), "loader_loads": loader_loads,
              "patched_source": identity(patched / "tool/build/apelink.c"), "effective_headers": list(unique.values()),
              "recipe": identity(Path(__file__)), "build_directory": str(work),
              "loader_postprocessing": "Raw Linux OSABI0 ld/objcopy; upstream FreeBSD fixup deliberately omitted",
              "toolchain_archive_sha256": read_json(ROOT / "toolchains/cosmocc.lock.json")["archive"]["sha256"]}
    result["sha256"] = hashlib.sha256(json.dumps(result, sort_keys=True, separators=(",", ":")).encode()).hexdigest()
    report["portable_helpers"] = result
    verify_source(cache)
    return result


def fetch(args, report, directory, run):
    from build_ape import BuildError
    cache = source_cache(args.cache.resolve())
    for entry in read_json(LOCK)["files"]:
        destination = cache / entry["path"]
        if destination.exists():
            if sha256(destination) != entry["sha256"]:
                raise BuildError("Existing helper source is corrupt; refusing replacement")
            continue
        destination.parent.mkdir(parents=True, exist_ok=True)
        part = destination.with_suffix(destination.suffix + ".part")
        try:
            run(report, directory, "fetch-" + entry["path"].replace("/", "-"), ["curl", "--fail", "--show-error", "--location", "--proto", "=https", "--connect-timeout", "20", "--max-time", "120", "-o", part, entry["raw_url"]], 130)
            if part.stat().st_size != entry["size_bytes"] or sha256(part) != entry["sha256"]:
                raise BuildError("Fetched helper source differs from pinned identity")
            part.replace(destination)
        finally:
            part.unlink(missing_ok=True)
    report["source"] = verify_source(cache)


def main():
    from build_ape import BuildError, run
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("operation", choices=("verify", "fetch", "check-loads"))
    parser.add_argument("--cache", type=Path, default=ROOT / ".cache/sources/ape-loader")
    parser.add_argument("--artifact", type=Path)
    parser.add_argument("--report", type=Path, required=True)
    args = parser.parse_args()
    directory = args.report.resolve().parent / "evidence" / str(uuid.uuid4())
    directory.mkdir(parents=True)
    report = {"schema_version": 1, "report_kind": "ape-helper-input", "report_id": directory.name, "started_at": now(), "commands": [], "source": None, "status": "not_run", "host": host_facts()}
    try:
        if args.operation == "check-loads":
            if not args.artifact:
                raise BuildError("check-loads requires --artifact")
            report["loads"] = loads(args.artifact.read_bytes(), require_arm64=True)
            report["artifact"] = identity(args.artifact)
        elif args.operation == "fetch":
            fetch(args, report, directory, run)
        else:
            report["source"] = verify_source(source_cache(args.cache.resolve()))
        report.update(status="pass", reason="Pinned helper input/layout check completed; no runtime claim")
    except BuildError as error:
        report.update(status=error.status, reason=str(error))
    except (OSError, ValueError, KeyError, TypeError) as error:
        report.update(status="fail", reason=str(error))
    report["finished_at"] = now()
    write_json(args.report, report)
    print(json.dumps({"status": report["status"], "reason": report["reason"]}))
    return {"pass": 0, "fail": 1, "blocked": 2}[report["status"]]


if __name__ == "__main__":
    sys.exit(main())
