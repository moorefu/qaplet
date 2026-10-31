#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Explicit Nightly acquisition and offline, fail-closed toolchain resolution."""

import argparse
from datetime import datetime
import json
import os
from pathlib import Path
from pathlib import PurePosixPath
import shutil
import stat
import sys
import tempfile
import uuid
import zipfile
import re
from urllib.request import getproxies

from evidence import read_json
from foundation import ROOT, command, host_facts, now, sha256


NIGHTLY_URL = "https://cosmo.zip/pub/cosmocc/cosmocc.zip"
TOOLS = ("bin/cosmocc", "bin/x86_64-linux-cosmo-gcc", "bin/aarch64-linux-cosmo-gcc")


class Unavailable(Exception):
    pass


def write_json(path, value):
    path.parent.mkdir(parents=True, exist_ok=True)
    staged = path.with_name(path.name + "." + uuid.uuid4().hex + ".tmp")
    staged.write_text(json.dumps(value, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    os.replace(staged, path)


def safe_name(name):
    return (isinstance(name, str) and bool(name) and "\\" not in name and ":" not in name
            and "\x00" not in name and not PurePosixPath(name).is_absolute()
            and ".." not in PurePosixPath(name).parts
            and PurePosixPath(name).as_posix() == name.rstrip("/"))


def validate_lock(lock):
    try:
        if lock["schema_version"] != 1 or lock["kind"] != "cosmocc-nightly":
            raise ValueError("unsupported schema/kind")
        source = lock["source"]
        if source["kind"] not in {"official-nightly", "local-import"}:
            raise ValueError("unknown source kind")
        if source["kind"] == "official-nightly" and source["url"] != NIGHTLY_URL:
            raise ValueError("official Nightly URL differs")
        if source["kind"] == "local-import" and not source["url"].startswith("file:"):
            raise ValueError("local import requires a file URI")
        if datetime.fromisoformat(source["retrieved_at"].replace("Z", "+00:00")).tzinfo is None:
            raise ValueError("retrieval timestamp has no timezone")
        identities = [lock["archive"], *lock["tools"], *lock["files"]]
        for identity in identities:
            digest = identity["sha256"]
            if not isinstance(digest, str) or not re.fullmatch("[0-9a-f]{64}", digest) or len(set(digest)) == 1:
                raise ValueError("missing, malformed or placeholder SHA-256")
            if type(identity["size_bytes"]) is not int or identity["size_bytes"] <= 0:
                # Empty non-compiler files are valid ZIP members.
                if identity not in lock["files"] or identity["size_bytes"] != 0:
                    raise ValueError("invalid measured length")
        if [tool["path"] for tool in lock["tools"]] != list(TOOLS):
            raise ValueError("required compiler identities differ")
        for tool in lock["tools"]:
            if not isinstance(tool["version"], str) or not tool["version"].strip() or tool["version"].lower() in {"unknown", "unproven", "placeholder"}:
                raise ValueError("unknown compiler version")
        files = lock["files"]
        if not files or len({entry["path"] for entry in files}) != len(files):
            raise ValueError("empty or duplicated file manifest")
        for entry in files:
            if not safe_name(entry["path"]) or type(entry["executable"]) is not bool:
                raise ValueError("unsafe file manifest")
        for tool in lock["tools"]:
            entry = next(item for item in files if item["path"] == tool["path"])
            if (entry["sha256"], entry["size_bytes"], entry["executable"]) != (tool["sha256"], tool["size_bytes"], True):
                raise ValueError("compiler differs from file manifest")
        if lock["libc_source"]["status"] != "unproven" or lock["libc_source"]["commit"] is not None:
            raise ValueError("unsupported libc source mapping; separate provenance proof is required")
        if lock["source_head"] is not None:
            raise ValueError("source-head observation is unsupported by this lock version")
    except (KeyError, TypeError, ValueError, StopIteration) as error:
        raise ValueError("Invalid Nightly lock: " + str(error)) from error


def manifest(tree):
    entries = []
    for path in sorted(tree.rglob("*")):
        if path.is_symlink():
            raise ValueError("Unexpected symlink in extracted cache: " + str(path))
        if path.is_file():
            entries.append({"path": path.relative_to(tree).as_posix(), "sha256": sha256(path),
                            "size_bytes": path.stat().st_size,
                            "executable": bool(path.stat().st_mode & 0o111)})
        elif not path.is_dir():
            raise ValueError("Unexpected special file in extracted cache")
    return entries


def measure_tools(tree, directory, report, timeout):
    tools = []
    for relative in TOOLS:
        path = tree / relative
        if not path.is_file() or not os.access(path, os.X_OK):
            raise ValueError("Required compiler is missing or not executable: " + relative)
        with path.open("rb") as stream:
            magic = stream.read(8)
        # Upstream documents the shell entry route for APE from Python subprocess.
        argv = (["sh", str(path)] if magic.startswith((b"MZ", b"jartsr")) else [str(path)]) + ["--version"]
        executed, stdout, stderr = command(argv, directory, "version-" + path.name, timeout)
        report["commands"].append(executed)
        version = (stdout or stderr).strip()
        if executed["exit_code"] != 0 or executed["timed_out"] or not version:
            raise ValueError("Compiler version query failed: " + relative)
        tools.append({"path": relative, "sha256": sha256(path), "size_bytes": path.stat().st_size,
                      "version": version, "version_argv": [*argv[:-2], relative, "--version"] if len(argv) > 2 else [relative, "--version"]})
    return tools


def extract(archive, tree):
    with zipfile.ZipFile(archive) as bundle:
        for item in bundle.infolist():
            path = PurePosixPath(item.filename)
            mode = item.external_attr >> 16
            if (not safe_name(item.filename)
                    or stat.S_ISLNK(mode) or (stat.S_IFMT(mode) not in (0, stat.S_IFREG, stat.S_IFDIR))):
                raise ValueError("Unsafe archive member: " + item.filename)
            target = tree.joinpath(*path.parts)
            if item.is_dir():
                target.mkdir(parents=True, exist_ok=True)
            else:
                target.parent.mkdir(parents=True, exist_ok=True)
                with bundle.open(item) as source, target.open("xb") as output:
                    shutil.copyfileobj(source, output)
                target.chmod(0o755 if mode & 0o111 else 0o644)


def freeze(cache):
    for path in cache.rglob("*"):
        path.chmod(0o555 if path.is_dir() or path.stat().st_mode & 0o111 else 0o444)
    cache.chmod(0o555)


def check_space(args, report, minimum):
    paths = [ROOT, args.cache_dir] + ([args.backing_dir] if args.backing_dir else [])
    measurements = [{"path": str(path.resolve()), "free_bytes": shutil.disk_usage(path).free,
                     "minimum_free_bytes": minimum} for path in paths]
    report.setdefault("disk_checks", []).append(measurements)
    if any(item["free_bytes"] < minimum for item in measurements):
        raise Unavailable("Insufficient free disk space; see measured source/cache/backing filesystems")


def download(args, report, directory, archive):
    # Windows curl does not automatically inherit the user's Internet Settings
    # proxy. Do not put the proxy (which may contain credentials) in argv/logs.
    previous = os.environ.get("HTTPS_PROXY")
    proxy = getproxies().get("https") if os.name == "nt" and previous is None else None
    if proxy:
        os.environ["HTTPS_PROXY"] = proxy
        report["network_route"] = "configured Windows HTTPS proxy"
    try:
        executed, _, _ = command(["curl", "--fail", "--location", "--silent", "--show-error",
            "--proto", "=https", "--proto-redir", "=https", "--connect-timeout", "15",
            "--max-time", str(args.download_timeout), "--output", str(archive), NIGHTLY_URL],
            directory, "nightly-download", args.download_timeout + 5)
    finally:
        if proxy:
            os.environ.pop("HTTPS_PROXY", None)
    report["commands"].append(executed)
    if executed["exit_code"] != 0 or executed["timed_out"]:
        raise Unavailable("Official Nightly download failed; see original logs")
    if not archive.is_file() or archive.stat().st_size == 0:
        raise ValueError("Official download produced no nonempty archive")
    return {"kind": "official-nightly", "url": NIGHTLY_URL, "retrieved_at": now(),
            "note": "Fetched over authenticated HTTPS; digest is locally measured, not an upstream signature"}


def import_receipt(args, identity):
    try:
        receipt = read_json(args.acquisition_report)
        if (receipt["report_kind"], receipt["schema_version"], receipt["operation"], receipt["status"]) != ("nightly-toolchain", 1, "download", "pass"):
            raise ValueError("receipt is not a successful official download")
        if receipt["downloaded_archive"] != identity:
            raise ValueError("archive length/SHA-256 differs from acquisition receipt")
        if receipt["source"]["kind"] != "official-nightly" or receipt["source"]["url"] != NIGHTLY_URL:
            raise ValueError("receipt source is not the official Nightly channel")
        if not any(item["exit_code"] == 0 and not item["timed_out"] and item["argv"][-1] == NIGHTLY_URL
                   for item in receipt["commands"]):
            raise ValueError("receipt has no successful official HTTPS command")
        return {**receipt["source"], "acquisition_report_sha256": sha256(args.acquisition_report)}
    except (OSError, KeyError, TypeError, ValueError) as error:
        raise ValueError("Invalid acquisition receipt: " + str(error)) from error


def acquire(args, report, directory):
    locked = None
    if args.operation == "fetch" and args.lock.exists():
        locked = read_json(args.lock)
        validate_lock(locked)
        if (args.cache_dir / locked["archive"]["sha256"]).exists():
            return resolve(args, report, directory)
    if args.operation == "bump" and not args.lock.is_file():
        raise Unavailable("No existing lock to bump; use explicit fetch")
    if args.operation == "bump":
        validate_lock(read_json(args.lock))
    if not args.archive and "wsl" in report["host"]["kernel"].lower() and args.backing_dir is None:
        raise Unavailable("WSL download requires explicit --backing-dir for the VHD physical volume (for this machine /mnt/c)")
    args.cache_dir.mkdir(parents=True, exist_ok=True)
    check_space(args, report, args.min_free_bytes)
    with tempfile.TemporaryDirectory(prefix=".candidate-", dir=args.cache_dir) as scratch:
        candidate = Path(scratch)
        archive = candidate / "archive.zip"
        if args.archive:
            shutil.copyfile(args.archive, archive)
            source = {"kind": "local-import", "url": args.archive.resolve().as_uri(), "retrieved_at": now(),
                      "note": "Explicit local import; official origin is not authenticated"}
        else:
            source = download(args, report, directory, archive)
        identity = {"sha256": sha256(archive), "size_bytes": archive.stat().st_size}
        report["downloaded_archive"] = identity
        if args.acquisition_report:
            source = import_receipt(args, identity)
        if locked and identity != locked["archive"]:
            raise ValueError("Downloaded/imported archive differs from locked SHA-256/length; only explicit bump may change inputs")
        with zipfile.ZipFile(archive) as bundle:
            expanded_bytes = sum(item.file_size for item in bundle.infolist())
        check_space(args, report, max(args.min_free_bytes, expanded_bytes + 512 * 1024**2))
        tree = candidate / "tree"
        tree.mkdir()
        extract(archive, tree)
        tools = measure_tools(tree, directory, report, args.timeout)
        lock = {"schema_version": 1, "kind": "cosmocc-nightly", "source": source, "archive": identity,
                "tools": tools, "files": manifest(tree), "source_head": None,
                "libc_source": {"status": "unproven", "commit": None,
                    "reason": "No verified mapping from this Nightly archive to a Cosmopolitan source commit"}}
        validate_lock(lock)
        if locked:
            if tools != locked["tools"] or lock["files"] != locked["files"]:
                raise ValueError("Restored compiler/extracted content differs from lock")
            report["retrieval"] = source
            lock = locked
        destination = args.cache_dir / identity["sha256"]
        if destination.exists():
            check_cache(lock, destination)
        else:
            os.rename(candidate, destination)
            freeze(destination)
        if not locked:
            write_json(args.lock, lock)
        return lock, destination / "tree"


def check_cache(lock, cache):
    archive = cache / "archive.zip"
    tree = cache / "tree"
    if cache.is_symlink() or archive.is_symlink() or tree.is_symlink():
        raise ValueError("Locked cache roots must not be symlinks")
    if not archive.is_file() or not tree.is_dir():
        raise Unavailable("Locked cache is missing; no implicit download is allowed")
    if os.name != "nt" and any(path.stat().st_mode & 0o222 for path in [cache, archive, tree, *tree.rglob("*")]):
        raise ValueError("Locked cache must remain read-only; writable content is refused")
    if archive.stat().st_size != lock["archive"]["size_bytes"] or sha256(archive) != lock["archive"]["sha256"]:
        raise ValueError("Locked archive length/SHA-256 mismatch")
    if manifest(tree) != lock["files"]:
        raise ValueError("Extracted cache manifest mismatch")


def resolve(args, report, directory):
    if not args.lock.is_file():
        raise Unavailable("Nightly lock is missing; run explicit fetch")
    lock = read_json(args.lock)
    validate_lock(lock)
    cache = args.cache_dir / lock["archive"]["sha256"]
    check_cache(lock, cache)
    measured = measure_tools(cache / "tree", directory, report, args.timeout)
    if measured != lock["tools"]:
        raise ValueError("Compiler identity/version differs from locked measurements")
    if lock["source"]["kind"] != "official-nightly" and not args.allow_local_import:
        raise ValueError("Local-import lock is not official Nightly; fixture use requires --allow-local-import")
    return lock, cache / "tree"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("operation", choices=("download", "fetch", "bump", "verify", "path"))
    parser.add_argument("--lock", type=Path, default=ROOT / "toolchains/cosmocc.lock.json")
    parser.add_argument("--cache-dir", type=Path, default=Path.home() / ".cache/qaplet/toolchains/cosmocc")
    parser.add_argument("--report", type=Path, default=ROOT / "out/nightly-toolchain.json")
    parser.add_argument("--archive", type=Path, help="explicit local ZIP import; cannot authenticate official origin")
    parser.add_argument("--acquisition-report", type=Path, help="successful official download receipt, rechecked against imported bytes")
    parser.add_argument("--download-to", type=Path, help="output ZIP for explicit download-only operation")
    parser.add_argument("--allow-local-import", action="store_true", help="permit synthetic/local locks for explicit testing only")
    parser.add_argument("--timeout", type=int, default=30)
    parser.add_argument("--download-timeout", type=int, default=300)
    parser.add_argument("--min-free-bytes", type=int, default=4 * 1024**3,
                        help="free-space safety floor; ZIP expansion is checked separately")
    parser.add_argument("--backing-dir", type=Path, help="mounted physical volume containing the WSL VHD, e.g. /mnt/c")
    args = parser.parse_args()
    if not 1 <= args.timeout <= 300 or not 1 <= args.download_timeout <= 3600 or args.min_free_bytes < 1:
        parser.error("timeout must be 1–300, download-timeout 1–3600 and min-free-bytes positive")
    if args.acquisition_report and not args.archive:
        parser.error("--acquisition-report requires --archive")
    directory = args.report.resolve().parent / "evidence" / str(uuid.uuid4())
    directory.mkdir(parents=True)
    report = {"report_kind": "nightly-toolchain", "schema_version": 1,
              "started_at": now(), "finished_at": None, "host": host_facts(),
              "status": "not_run", "reason": "", "operation": args.operation,
              "source": {"script_sha256": sha256(Path(__file__))}, "commands": [], "toolchain": None,
              "lock_path": str(args.lock.resolve()), "cache_root": str(args.cache_dir.resolve())}
    try:
        if args.operation == "download":
            if not args.download_to or args.download_to.exists():
                raise ValueError("download requires a new --download-to path; existing archives are never overwritten")
            args.download_to.parent.mkdir(parents=True, exist_ok=True)
            args.cache_dir.mkdir(parents=True, exist_ok=True)
            check_space(args, report, args.min_free_bytes)
            if shutil.disk_usage(args.download_to.parent).free < args.min_free_bytes:
                raise Unavailable("Insufficient output disk space")
            staged = args.download_to.with_name(args.download_to.name + "." + uuid.uuid4().hex + ".partial")
            try:
                report["source"] = download(args, report, directory, staged)
                report["downloaded_archive"] = {"sha256": sha256(staged), "size_bytes": staged.stat().st_size}
                os.rename(staged, args.download_to)
            finally:
                staged.unlink(missing_ok=True)
            report.update(status="pass", reason="Official HTTPS bytes measured only; compiler/cache validation has not run",
                          download_path=str(args.download_to.resolve()))
        else:
            lock, tree = (acquire(args, report, directory) if args.operation in {"fetch", "bump"}
                          else resolve(args, report, directory))
            report.update(status="pass", reason="Archive, full extracted content and compiler versions measured; product probes remain unexecuted",
                          toolchain=lock, resolved_path=str(tree.resolve()))
    except Unavailable as error:
        report.update(status="blocked", reason=str(error))
    except (OSError, ValueError, KeyError, TypeError, zipfile.BadZipFile) as error:
        report.update(status="fail", reason=str(error))
    report["finished_at"] = now()
    write_json(args.report, report)
    if report["status"] == "pass" and args.operation == "path":
        print(report["resolved_path"])
    else:
        print(report["reason"], file=sys.stderr)
    return {"pass": 0, "fail": 1, "blocked": 2}[report["status"]]


if __name__ == "__main__":
    sys.exit(main())
