#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Explicitly fetch locked Zig; offline Ubuntu-build a separate native loader bundle."""
import argparse
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import platform
import shutil
import struct
import sys
import tarfile
import urllib.request
import uuid

from build_ape import BuildError, identity, run
from evidence import read_json
from foundation import ROOT, host_facts, now, sha256
from nightly_toolchain import write_json
from startup import ci_facts


def check_archive(args, lock):
    path = args.zig_cache.resolve() / lock["zig"]["archive"]
    if not path.is_file():
        raise BuildError("Locked Zig archive missing; run explicit fetch first", "blocked")
    if path.stat().st_size != lock["zig"]["size_bytes"] or sha256(path) != lock["zig"]["sha256"]:
        raise BuildError("Zig archive SHA-256/size differs from lock")
    return path


def verify_tree(args, lock, archive, extract=False):
    """The pinned archive, rather than a writable receipt, defines every compiler input."""
    tree = args.zig_cache.resolve() / lock["zig"]["tree"]
    if not tree.is_dir() and not extract:
        raise BuildError("Locked Zig extracted tree missing; run explicit fetch first", "blocked")
    expected = set()
    checked_directories = set()
    cache = args.zig_cache.resolve()
    if args.zig_cache.absolute() != cache:
        raise BuildError("Zig cache must be a physical directory without outward links")

    def check_directory(path):
        # Cache directory checks within one verification, never file hashes.
        # Rechecking every ancestor for every small header is costly on DrvFS.
        for parent in (path, *path.parents):
            if parent in checked_directories:
                break
            if parent.is_symlink():
                raise BuildError("Zig cache cannot redirect physical inputs through links")
            checked_directories.add(parent)
            if parent == cache:
                break
    with tarfile.open(archive, "r:xz") as bundle:
        for member in bundle:
            name = PurePosixPath(member.name)
            if name.is_absolute() or ".." in name.parts or name.parts[0] != lock["zig"]["tree"]:
                raise BuildError("Unsafe path in Zig archive")
            if not member.isdir() and not member.isfile():
                raise BuildError("Unexpected non-regular entry in Zig archive")
            path = cache.joinpath(*name.parts)
            check_directory(path.parent)
            if path.is_symlink():
                raise BuildError("Zig cache cannot redirect physical inputs through links")
            if member.isdir():
                if extract:
                    path.mkdir(parents=True, exist_ok=True)
                continue
            expected.add(str(path.relative_to(tree)))
            stream = bundle.extractfile(member)
            digest = hashlib.sha256()
            if extract:
                path.parent.mkdir(parents=True, exist_ok=True)
                with path.open("wb") as output:
                    while block := stream.read(1024 * 1024):
                        digest.update(block)
                        output.write(block)
                path.chmod(member.mode)
            else:
                while block := stream.read(1024 * 1024):
                    digest.update(block)
            if not path.is_file() or path.stat().st_size != member.size or sha256(path) != digest.hexdigest():
                raise BuildError("Zig tree content differs from archive: " + member.name)
    actual = {str(path.relative_to(tree)) for path in tree.rglob("*") if path.is_symlink() or not path.is_dir()}
    if actual != expected:
        raise BuildError("Zig tree contains missing/extra inputs")
    compiler = tree / "zig"
    if sha256(compiler) != lock["zig"]["compiler_sha256"]:
        raise BuildError("Zig compiler differs from lock")
    return tree


def fetch(args, report, directory, lock):
    archive = args.zig_cache.resolve() / lock["zig"]["archive"]
    if not archive.exists():
        args.zig_cache.mkdir(parents=True, exist_ok=True)
        partial = archive.with_suffix(archive.suffix + ".partial")
        try:
            # Public official URL, no credentials. Fetch is always explicit.
            with urllib.request.urlopen(lock["zig"]["url"], timeout=60) as response, partial.open("wb") as output:
                shutil.copyfileobj(response, output)
            if partial.stat().st_size != lock["zig"]["size_bytes"] or sha256(partial) != lock["zig"]["sha256"]:
                raise BuildError("Downloaded Zig archive differs from official lock")
            partial.replace(archive)
        finally:
            partial.unlink(missing_ok=True)
    archive = check_archive(args, lock)
    report["acquisition"] = {"url": lock["zig"]["url"], "checked_at": now(), **identity(archive)}
    tree = args.zig_cache.resolve() / lock["zig"]["tree"]
    verify_tree(args, lock, archive, extract=not tree.exists())
    report.update(status="pass", reason="Explicit locked Zig acquisition and complete extracted-tree verification passed")


def macho_facts(path):
    data = path.read_bytes()
    magic, cpu, subtype, kind, count, total, flags, reserved = struct.unpack_from("<8I", data)
    if (magic, cpu, kind) != (0xfeedfacf, 0x0100000c, 2) or 32 + total > len(data):
        raise BuildError("Helper is not a bounded native ARM64 Mach-O executable")
    facts = {"format": "Mach-O64", "cpu_type": cpu, "dylibs": [], "signature": None}
    offset = 32
    for _ in range(count):
        command, size = struct.unpack_from("<II", data, offset)
        if size < 8 or offset + size > 32 + total:
            raise BuildError("Invalid Mach-O load-command bounds")
        if command == 0xc:
            start = struct.unpack_from("<I", data, offset + 8)[0]
            facts["dylibs"].append(data[offset + start:offset + size].split(b"\0")[0].decode())
        if command == 0x1d:
            start, length = struct.unpack_from("<II", data, offset + 8)
            signature = data[start:start + length]
            sig_magic, sig_length, sig_count = struct.unpack_from(">3I", signature)
            if sig_magic != 0xfade0cc0 or sig_length > len(signature):
                raise BuildError("Malformed Mach-O embedded signature")
            for index in range(sig_count):
                _, blob_offset = struct.unpack_from(">2I", signature, 12 + index * 8)
                blob_magic, blob_length = struct.unpack_from(">2I", signature, blob_offset)
                if blob_magic != 0xfade0c02:
                    continue
                cd = signature[blob_offset:blob_offset + blob_length]
                _, _, version, cd_flags, hash_offset, _, _, slots, code_limit = struct.unpack_from(">9I", cd)
                hash_size, hash_type, _, shift = struct.unpack_from("4B", cd, 36)
                if hash_type != 2 or hash_size != 32 or code_limit > len(data):
                    raise BuildError("Unsupported helper CodeDirectory")
                valid = all(hashlib.sha256(data[i * (1 << shift):min((i + 1) * (1 << shift), code_limit)]).digest()
                            == cd[hash_offset + i * hash_size:hash_offset + (i + 1) * hash_size] for i in range(slots))
                facts["signature"] = {"ad_hoc": bool(cd_flags & 2), "flags": cd_flags, "page_size": 1 << shift,
                                      "all_code_hashes_valid": valid, "native_os_acceptance": "not_run"}
        offset += size
    if offset != 32 + total or facts["dylibs"] != ["/usr/lib/libSystem.B.dylib"] or not facts["signature"] or not facts["signature"]["ad_hoc"] or not facts["signature"]["all_code_hashes_valid"]:
        raise BuildError("Mach-O dependencies/signature do not meet helper contract")
    return facts


def build(args, report, directory, lock):
    archive = check_archive(args, lock)
    if platform.system() != "Linux" or platform.freedesktop_os_release().get("ID") != "ubuntu" or report["host"]["execution_arch"] != "x86_64":
        raise BuildError("Loader builds require Ubuntu x86_64; other hosts only validate supplied bytes", "blocked")
    tree = verify_tree(args, lock, archive)
    nightly = read_json(ROOT / "toolchains/cosmocc.lock.json")
    if nightly["archive"]["sha256"] != lock["source"]["nightly_archive_sha256"]:
        raise BuildError("Loader source Nightly archive differs from lock")
    original = args.toolchain_cache.resolve() / nightly["archive"]["sha256"] / "tree"
    files = {entry["path"]: entry for entry in nightly["files"]}
    inputs = []
    for name in ("ape-x86_64.elf", "ape-aarch64.elf", "ape-x86_64.macho", "ape-m1.c"):
        path = original / "bin" / name
        if not path.is_file():
            raise BuildError("Locked Nightly loader input missing: " + name, "blocked")
        observed = identity(path)
        if observed["sha256"] != files["bin/" + name]["sha256"] or observed["size_bytes"] != files["bin/" + name]["size_bytes"]:
            raise BuildError("Nightly loader input identity mismatch: " + name)
        inputs.append(observed)
    source = original / lock["source"]["path"]
    if sha256(source) != lock["source"]["sha256"]:
        raise BuildError("Mac loader source differs from helper lock")
    output = args.output_dir.resolve()
    if output.exists() and any(output.iterdir()):
        raise BuildError("Loader output directory must be new or empty")
    output.mkdir(parents=True, exist_ok=True)
    work = args.build_dir.resolve() / directory.name
    work.mkdir(parents=True)
    env = os.environ.copy()
    env.update(ZIG_GLOBAL_CACHE_DIR=str(args.zig_cache.resolve() / "global-cache"),
               ZIG_LOCAL_CACHE_DIR=str(work / "zig-cache"), TMPDIR=str(work))
    compiler = tree / "zig"
    run(report, directory, "zig-version", [compiler, "version"], 30)
    helper = output / "ape-aarch64.macho"
    argv = [compiler, "cc", "-target", lock["recipe"]["target"], *lock["recipe"]["flags"], "-o", helper, source]
    run(report, directory, "build-native-mac-loader", argv, 300, cwd=work, environment=env)
    for name, system, arch in (("ape-x86_64.elf", "Linux", "x86_64"), ("ape-aarch64.elf", "Linux", "aarch64"),
                               ("ape-x86_64.macho", "Darwin", "x86_64")):
        path = output / name
        shutil.copyfile(original / "bin" / name, path)
        path.chmod(0o555)
        report["helpers"].append({"os": system, "arch": arch, "file": name, **identity(path), "origin": "unchanged official Nightly loader"})
    helper.chmod(0o555)
    report["helpers"].append({"os": "Darwin", "arch": "aarch64", "file": helper.name, **identity(helper),
                              "origin": "Ubuntu Zig cross-build of unchanged locked ape-m1.c", "inspection": macho_facts(helper)})
    report["source"] = {"inputs": inputs, "nightly_lock": identity(ROOT / "toolchains/cosmocc.lock.json"), "helper_lock": identity(args.lock)}
    report["toolchain"] = {"zig": identity(compiler), "archive": identity(archive), "version": lock["zig"]["version"],
                           "tree_verification": "every file/type/path against pinned original archive"}
    report["build"] = {"argv": list(map(str, argv)), "cwd": str(work), "host": report["host"], "recipe": lock["recipe"]}
    # Source and compiler inputs are independently rechecked after compilation.
    verify_tree(args, lock, archive)
    if any(sha256(entry["path"]) != entry["sha256"] for entry in inputs):
        raise BuildError("Nightly loader input changed during helper build")
    report.update(status="pass", reason="Separate loaders distributed; Mac ARM64 native acceptance still requires host run")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("operation", choices=("fetch", "verify", "build"))
    parser.add_argument("--lock", type=Path, default=ROOT / "toolchains/startup-loaders.lock.json")
    parser.add_argument("--zig-cache", type=Path, default=ROOT / ".cache/tools/zig/0.15.2")
    parser.add_argument("--toolchain-cache", type=Path, default=ROOT / ".cache/toolchains/cosmocc")
    parser.add_argument("--build-dir", type=Path, default=ROOT / ".cache/build/startup-loaders")
    parser.add_argument("--output-dir", type=Path, default=ROOT / "out/m0/loaders")
    parser.add_argument("--report", type=Path, default=ROOT / "out/m0/loaders/helpers.json")
    args = parser.parse_args()
    args.report = args.report.resolve()
    directory = args.report.parent.parent / "loader-evidence" / str(uuid.uuid4())
    directory.mkdir(parents=True)
    report = {"schema_version": 1, "report_kind": "ape-loaders", "report_id": directory.name, "operation": args.operation,
              "status": "not_run", "reason": "", "started_at": now(), "finished_at": None,
              "host": host_facts(), "ci": ci_facts(), "helpers": [], "commands": [], "source": None, "toolchain": None, "build": None}
    try:
        lock = read_json(args.lock)
        if args.operation == "fetch":
            fetch(args, report, directory, lock)
        elif args.operation == "verify":
            archive = check_archive(args, lock)
            verify_tree(args, lock, archive)
            report.update(status="pass", reason="Pinned Zig original archive and complete tree match")
        else:
            build(args, report, directory, lock)
    except BuildError as error:
        report.update(status=error.status, reason=str(error))
    except (OSError, ValueError, KeyError, TypeError, tarfile.TarError, struct.error) as error:
        report.update(status="fail", reason=str(error))
    report["finished_at"] = now()
    write_json(args.report, report)
    print(json.dumps({"status": report["status"], "reason": report["reason"], "report": str(args.report)}))
    return {"pass": 0, "blocked": 2}.get(report["status"], 1)


if __name__ == "__main__":
    sys.exit(main())
