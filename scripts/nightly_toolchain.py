#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Explicit Nightly acquisition and offline, fail-closed toolchain resolution."""

import argparse
from datetime import datetime
import hashlib
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
from foundation import ROOT, case_record, command, host_facts, now, sha256


NIGHTLY_URL = "https://cosmo.zip/pub/cosmocc/cosmocc.zip"
UPSTREAM_REPOSITORY = "jart/cosmopolitan"
UPSTREAM_WORKFLOW = ".github/workflows/nightly-cosmocc.yml"
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


def validate_official_source(source, archive):
    provider = source.get("provider", "cosmo.zip")
    if provider == "cosmo.zip":
        if source["url"] != NIGHTLY_URL:
            raise ValueError("official Nightly URL differs")
    elif provider == "github-actions":
        github = source["github"]
        if github["repository"] != UPSTREAM_REPOSITORY or github["workflow_path"] != UPSTREAM_WORKFLOW:
            raise ValueError("GitHub repository/workflow is not the official Nightly")
        if any(type(github[key]) is not int or github[key] <= 0 for key in ("run_id", "artifact_id")):
            raise ValueError("invalid GitHub run/artifact identity")
        if (not re.fullmatch("[0-9a-f]{40}", github["head_sha"])
                or len(set(github["head_sha"])) == 1
                or github["artifact_head_sha"] != github["head_sha"]
                or github["artifact_workflow_run_id"] != github["run_id"]):
            raise ValueError("GitHub artifact does not match its measured workflow run")
        if ((github["run_status"], github["run_conclusion"], github["artifact_name"]) != ("completed", "success", "cosmocc")
                or github["artifact_expired"] is not False):
            raise ValueError("GitHub artifact is not a successful available cosmocc Nightly")
        if {"sha256": github["artifact_sha256"], "size_bytes": github["artifact_size_bytes"]} != archive:
            raise ValueError("GitHub API digest/length differs from original archive")
        if source["url"] != f"https://api.github.com/repos/{UPSTREAM_REPOSITORY}/actions/artifacts/{github['artifact_id']}/zip":
            raise ValueError("GitHub artifact URL differs from its identity")
    else:
        raise ValueError("unsupported official Nightly provider")


def validate_lock(lock):
    try:
        if type(lock["schema_version"]) is not int or lock["schema_version"] != 1 or lock["kind"] != "cosmocc-nightly":
            raise ValueError("unsupported schema/kind")
        source = lock["source"]
        if source["kind"] not in {"official-nightly", "local-import"}:
            raise ValueError("unknown source kind")
        if not isinstance(source["url"], str) or not isinstance(source["retrieved_at"], str):
            raise ValueError("source URL/retrieval timestamp must be strings")
        if source["kind"] == "official-nightly":
            validate_official_source(source, lock["archive"])
        if source["kind"] == "local-import" and not source["url"].startswith("file:"):
            raise ValueError("local import requires a file URI")
        # Python 3.10 accepts microseconds only; PowerShell receipts retain 100ns
        # precision. Validate a microsecond view while preserving original text.
        timestamp = re.sub(r"(\.\d{6})\d+", r"\1", source["retrieved_at"])
        if datetime.fromisoformat(timestamp.replace("Z", "+00:00")).tzinfo is None:
            raise ValueError("retrieval timestamp has no timezone")
        identities = [lock["archive"], *lock["tools"], *lock["files"]]
        for identity in identities:
            digest = identity["sha256"]
            if not isinstance(digest, str) or not re.fullmatch("[0-9a-f]{64}", digest) or len(set(digest)) == 1:
                raise ValueError("missing, malformed or placeholder SHA-256")
            size = identity["size_bytes"]
            if type(size) is not int or size < 0 or (size == 0 and identity not in lock["files"]):
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
            if (not safe_name(entry["path"]) or type(entry["executable"]) is not bool
                    or entry["kind"] not in {"file", "symlink"}):
                raise ValueError("unsafe file manifest")
            if entry["kind"] == "symlink" and not safe_link_target(entry["target"]):
                raise ValueError("unsafe symlink target in manifest")
        for tool in lock["tools"]:
            entry = next(item for item in files if item["path"] == tool["path"])
            if (entry["sha256"], entry["size_bytes"], entry["executable"]) != (tool["sha256"], tool["size_bytes"], True):
                raise ValueError("compiler differs from file manifest")
        extraction = lock["extraction"]
        if extraction["execute_bit_policy"] != "archive-unix-modes-v1":
            raise ValueError("unsupported extraction execute-bit policy")
        normalized = extraction["normalized_executable_paths"]
        if not isinstance(normalized, list) or len(set(normalized)) != len(normalized):
            raise ValueError("invalid normalized executable manifest")
        for name in normalized:
            if not safe_name(name) or not any(entry["path"] == name and entry["executable"] for entry in files):
                raise ValueError("normalized executable differs from file manifest")
        if lock["libc_source"]["status"] != "unproven" or lock["libc_source"]["commit"] is not None:
            raise ValueError("unsupported libc source mapping; separate provenance proof is required")
        if lock["source_head"] is not None:
            raise ValueError("source-head observation is unsupported by this lock version")
    except (KeyError, TypeError, ValueError, StopIteration) as error:
        raise ValueError("Invalid Nightly lock: " + str(error)) from error


def safe_link_target(target):
    return (isinstance(target, str) and bool(target) and "\\" not in target
            and ":" not in target and "\x00" not in target and not PurePosixPath(target).is_absolute())


def validate_links(tree):
    root = tree.resolve()
    for path in tree.rglob("*"):
        if not path.is_symlink():
            continue
        target = os.readlink(path)
        try:
            if not safe_link_target(target) or not path.resolve(strict=True).is_relative_to(root):
                raise ValueError("Unsafe cache symlink: " + str(path))
        except (OSError, RuntimeError) as error:
            raise ValueError("Dangling/cyclic/escaping cache symlink: " + str(path)) from error


def manifest(tree, executable_modes=None):
    validate_links(tree)
    entries = []
    root = tree.resolve()

    def executable(path):
        if executable_modes is None:
            return bool(path.stat().st_mode & 0o111)
        # On measured DrvFS without metadata, archive modes remain the source
        # of execute-bit identity. Link identity follows its validated target.
        if not path.is_symlink():
            return executable_modes[path.relative_to(tree).as_posix()]
        target = path.resolve()
        return True if target.is_dir() else executable_modes[target.relative_to(root).as_posix()]

    for path in sorted(tree.rglob("*")):
        if path.is_symlink():
            target = os.readlink(path)
            entries.append({"path": path.relative_to(tree).as_posix(), "kind": "symlink", "target": target,
                            "sha256": hashlib.sha256(target.encode("utf-8")).hexdigest(),
                            "size_bytes": len(target.encode("utf-8")),
                            "executable": executable(path)})
        elif path.is_file():
            entries.append({"path": path.relative_to(tree).as_posix(), "kind": "file", "sha256": sha256(path),
                            "size_bytes": path.stat().st_size,
                            "executable": executable(path)})
        elif not path.is_dir():
            raise ValueError("Unexpected special file in extracted cache")
    return entries


def measure_tools(tree, directory, report, timeout):
    tools = []
    native_loader = None
    if report["host"]["os"] == "Linux" and report["host"]["execution_arch"] == "x86_64":
        candidate = tree / "bin/ape-x86_64.elf"
        if candidate.is_file() and os.access(candidate, os.X_OK):
            with candidate.open("rb") as stream:
                header = stream.read(20)
            if header[:6] == b"\x7fELF\x02\x01" and header[18:20] == b"\x3e\x00":
                native_loader = candidate
    for relative in TOOLS:
        path = tree / relative
        if not path.is_file() or not os.access(path, os.X_OK):
            raise ValueError("Required compiler is missing or not executable: " + relative)
        with path.open("rb") as stream:
            magic = stream.read(8)
        # Upstream documents the shell entry route for APE from Python subprocess.
        portable = magic.startswith((b"MZ", b"jartsr"))
        if portable and native_loader:
            argv = [str(native_loader), str(path), "--version"]
        else:
            argv = (["sh", str(path)] if portable else [str(path)]) + ["--version"]
        executed, stdout, stderr = command(argv, directory, "version-" + path.name, timeout)
        report["commands"].append(executed)
        version = (stdout or stderr).strip()
        if executed["exit_code"] != 0 or executed["timed_out"] or not version:
            raise ValueError("Compiler version query failed: " + relative)
        tools.append({"path": relative, "sha256": sha256(path), "size_bytes": path.stat().st_size,
                      "version": version,
                      "version_argv": [relative if arg == str(path) else "bin/ape-x86_64.elf"
                                       if native_loader and arg == str(native_loader) else arg for arg in argv],
                      "version_launcher": ({"path": "bin/ape-x86_64.elf", "sha256": sha256(native_loader),
                                            "kind": "native-linux-x86_64-elf"} if portable and native_loader else None)})
    return tools


def extract(archive, tree):
    # Canonicalize the extraction parent too (macOS /var aliases /private/var).
    # Compare link destinations and the allowed root in the same namespace.
    tree = tree.resolve()
    with zipfile.ZipFile(archive) as bundle:
        items = bundle.infolist()
        seen, links = set(), {}
        for item in items:
            if item.flag_bits & 1:
                raise ValueError("Encrypted archive member is unsupported: " + item.filename)
            mode = item.external_attr >> 16
            name = item.filename.rstrip("/")
            if (not safe_name(item.filename) or name in seen
                    or stat.S_IFMT(mode) not in (0, stat.S_IFREG, stat.S_IFDIR, stat.S_IFLNK)):
                raise ValueError("Unsafe archive member: " + item.filename)
            seen.add(name)
            if stat.S_ISLNK(mode):
                target = bundle.read(item).decode("utf-8")
                if not safe_link_target(target):
                    raise ValueError("Unsafe archive symlink: " + item.filename)
                links[name] = target
        for item in items:
            path = PurePosixPath(item.filename)
            if any(PurePosixPath(*path.parts[:index]).as_posix() in links for index in range(1, len(path.parts))):
                raise ValueError("Archive symlink cannot be an extraction parent: " + item.filename)
        for item in items:
            path = PurePosixPath(item.filename)
            mode = item.external_attr >> 16
            target = tree.joinpath(*path.parts)
            if stat.S_ISLNK(mode):
                continue
            if item.is_dir():
                target.mkdir(parents=True, exist_ok=True)
            else:
                target.parent.mkdir(parents=True, exist_ok=True)
                with bundle.open(item) as source, target.open("xb") as output:
                    shutil.copyfileobj(source, output)
                target.chmod(0o755 if mode & 0o111 else 0o644)
        root = tree.resolve()
        for name, value in links.items():
            path = tree / name
            path.parent.mkdir(parents=True, exist_ok=True)
            if not Path(os.path.abspath(path.parent / value)).is_relative_to(root):
                raise ValueError("Archive symlink escapes extraction: " + name)
            path.symlink_to(value)
        validate_links(tree)


def freeze(cache):
    for path in cache.rglob("*"):
        if path.is_symlink():
            continue
        path.chmod(0o555 if path.is_dir() or path.stat().st_mode & 0o111 else 0o444)
    cache.chmod(0o555)


def mounted_filesystem(path):
    mounted = []
    try:
        for line in Path("/proc/self/mountinfo").read_text().splitlines():
            left, right = line.split(" - ", 1)
            mount = re.sub(r"\\([0-7]{3})", lambda match: chr(int(match[1], 8)), left.split()[4])
            fields = right.split()
            if path.resolve().is_relative_to(Path(mount)):
                mounted.append((mount, fields[0], "aname=drvfs" in fields[2]))
    except OSError:
        return None
    if not mounted:
        return None
    mount, kind, drvfs = max(mounted, key=lambda item: len(item[0]))
    return {"mount_point": mount, "type": kind, "drvfs": kind == "9p" and drvfs}


def filesystem_permissions(cache_root, report):
    if "filesystem_permissions" in report:
        return report["filesystem_permissions"]
    permissions = {"policy": "posix-mode-v1", "file_execute_bits_enforced": True,
                   "directory_readonly_enforced": True, "regular_file_readonly_enforced": True,
                   "limits": []}
    report["filesystem_permissions"] = permissions
    if os.name == "nt":
        permissions.update(policy="windows-native-v1", file_execute_bits_enforced=False,
                           directory_readonly_enforced=False, regular_file_readonly_enforced=False,
                           limits=["Native Windows does not expose the POSIX mode checks; tool execution is still required"])
        return permissions
    # Only recognized DrvFS may use a measured mode-representation policy.
    # Other POSIX filesystems retain all existing strict mode checks.
    filesystem = mounted_filesystem(cache_root)
    permissions["filesystem"] = filesystem
    if not filesystem or not filesystem["drvfs"]:
        return permissions
    with tempfile.TemporaryDirectory(prefix=".permission-probe-", dir=cache_root) as scratch:
        probe = Path(scratch) / "file"
        directory = Path(scratch) / "directory"
        probe.write_bytes(b"Qaplet filesystem mode probe\n")
        directory.mkdir()
        modes = []
        try:
            for path, requested in ((probe, 0o755), (probe, 0o555), (probe, 0o444), (directory, 0o555)):
                path.chmod(requested)
                modes.append({"kind": "directory" if path == directory else "file",
                              "requested": oct(requested), "actual": oct(stat.S_IMODE(path.stat().st_mode))})
            file_mode, directory_mode = probe.stat().st_mode, directory.stat().st_mode
        finally:
            probe.chmod(0o644)
            directory.chmod(0o755)
    permissions["chmod_probe"] = modes
    permissions["file_execute_bits_enforced"] = not bool(file_mode & 0o111)
    permissions["directory_readonly_enforced"] = not bool(directory_mode & 0o222)
    permissions["regular_file_readonly_enforced"] = not bool(file_mode & 0o222)
    if not permissions["regular_file_readonly_enforced"]:
        raise Unavailable("DrvFS cannot enforce regular-file no-write; cache verification is blocked")
    if not permissions["file_execute_bits_enforced"] or not permissions["directory_readonly_enforced"]:
        permissions["policy"] = "drvfs-no-metadata-v1"
        if not permissions["file_execute_bits_enforced"]:
            permissions["limits"].append("File execute bits use original ZIP metadata because this mount cannot represent them")
        if not permissions["directory_readonly_enforced"]:
            permissions["limits"].append("Directory no-write is not representable; full entry/content/link verification remains enforced")
    return permissions


def archive_executable_modes(archive):
    with zipfile.ZipFile(archive) as bundle:
        return {item.filename: bool(item.external_attr >> 16 & 0o111) for item in bundle.infolist()
                if not item.is_dir() and not stat.S_ISLNK(item.external_attr >> 16)}


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
        executed, _, _ = command(["curl", "--disable", "--fail", "--location", "--silent", "--show-error",
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
    return {"kind": "official-nightly", "provider": "cosmo.zip", "url": NIGHTLY_URL, "retrieved_at": now(),
            "note": "Fetched over authenticated HTTPS; digest is locally measured, not an upstream signature"}


def official_download_argv(argv):
    # Accept the published download command grammar, including the older form
    # before --disable was added. Never interpret curl's abbreviations/clusters:
    # an unknown option, repeated option or extra URL is not trusted evidence.
    if (not isinstance(argv, list) or not all(isinstance(arg, str) for arg in argv)
            or len(argv) < 2 or argv[0] not in {"curl", "curl.exe"} or argv[-1] != NIGHTLY_URL):
        return False
    flags = {"--fail", "--location", "--silent", "--show-error"}
    parameters = {"--proto", "--proto-redir", "--connect-timeout", "--max-time", "--output"}
    seen = set()
    index = 2 if argv[1] == "--disable" else 1
    while index < len(argv) - 1:
        option = argv[index]
        if option in seen or option not in flags | parameters:
            return False
        seen.add(option)
        index += 1
        if option in parameters:
            if index >= len(argv) - 1:
                return False
            value = argv[index]
            index += 1
            if option in {"--proto", "--proto-redir"} and value != "=https":
                return False
            if option in {"--connect-timeout", "--max-time"} and not re.fullmatch(r"[1-9][0-9]*", value):
                return False
            if option == "--output" and (not value or "\x00" in value):
                return False
    return seen == flags | parameters


def import_receipt(args, identity):
    try:
        receipt = read_json(args.acquisition_report)
        if (receipt["report_kind"], receipt["schema_version"], receipt["operation"], receipt["status"]) != ("nightly-toolchain", 1, "download", "pass"):
            raise ValueError("receipt is not a successful official download")
        if receipt["downloaded_archive"] != identity:
            raise ValueError("archive length/SHA-256 differs from acquisition receipt")
        if receipt["source"]["kind"] != "official-nightly":
            raise ValueError("receipt source is not the official Nightly channel")
        if receipt["source"].get("provider") == "github-actions":
            run, artifact = receipt["github_run"], receipt["github_artifact"]
            if not isinstance(artifact["digest"], str) or not artifact["digest"].startswith("sha256:"):
                raise ValueError("GitHub API has no SHA-256 archive digest")
            github = {"repository": run["repository"]["full_name"], "workflow_path": run["path"],
                "run_id": run["id"], "head_sha": run["head_sha"], "run_status": run["status"],
                "run_conclusion": run["conclusion"], "artifact_id": artifact["id"],
                "artifact_name": artifact["name"], "artifact_expired": artifact["expired"],
                "artifact_sha256": artifact["digest"].removeprefix("sha256:"),
                "artifact_size_bytes": artifact["size_in_bytes"],
                "artifact_workflow_run_id": artifact["workflow_run"]["id"],
                "artifact_head_sha": artifact["workflow_run"]["head_sha"],
                "run_metadata_sha256": hashlib.sha256(json.dumps(run, sort_keys=True).encode()).hexdigest(),
                "artifact_metadata_sha256": hashlib.sha256(json.dumps(artifact, sort_keys=True).encode()).hexdigest()}
            source = {**receipt["source"], "github": github, "acquisition_report_sha256": sha256(args.acquisition_report)}
            validate_official_source(source, identity)
            return source
        if receipt["source"]["url"] != NIGHTLY_URL:
            raise ValueError("receipt source is not the official Nightly channel")
        validate_official_source(receipt["source"], identity)
        successful = []
        for item in receipt["commands"]:
            argv = item["argv"]
            if not official_download_argv(argv):
                continue
            successful.append(type(item["exit_code"]) is int and item["exit_code"] == 0 and item["timed_out"] is False)
        if not any(successful):
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
        if locked["source"].get("provider") == "github-actions" and not args.archive:
            raise Unavailable("GitHub Nightly cache missing: explicitly obtain the locked artifact URL and import with --archive and its acquisition receipt; canonical rolling-channel fallback is forbidden")
    if args.operation == "bump" and not args.lock.is_file():
        raise Unavailable("No existing lock to bump; use explicit fetch")
    if args.operation == "bump":
        validate_lock(read_json(args.lock))
        report["previous_lock_sha256"] = sha256(args.lock)
    official_input = not args.archive or args.acquisition_report or (locked and locked["source"]["kind"] == "official-nightly")
    filesystem = mounted_filesystem(args.cache_dir)
    if (official_input and "wsl" in report["host"]["kernel"].lower() and args.backing_dir is None
            and not (filesystem and filesystem["drvfs"])):
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
        extraction = {"execute_bit_policy": "archive-unix-modes-v1", "normalized_executable_paths": []}
        permissions = filesystem_permissions(args.cache_dir, report)
        modes = archive_executable_modes(archive) if not permissions["file_execute_bits_enforced"] else None
        files = manifest(tree, modes)
        if any(not next((entry["executable"] for entry in files if entry["path"] == tool), False) for tool in TOOLS):
            raise ValueError("Required compiler is missing or not executable in archive metadata")
        tools = measure_tools(tree, directory, report, args.timeout)
        lock = {"schema_version": 1, "kind": "cosmocc-nightly", "source": source, "archive": identity,
                "tools": tools, "files": files, "source_head": None, "extraction": extraction,
                "libc_source": {"status": "unproven", "commit": None,
                    "reason": "No verified mapping from this Nightly archive to a Cosmopolitan source commit"}}
        if locked:
            if tools != locked["tools"] or lock["files"] != locked["files"] or extraction != locked["extraction"]:
                raise ValueError("Restored compiler/extracted content differs from lock")
            report["retrieval"] = source
            lock = locked
        validate_lock(lock)
        destination = args.cache_dir / identity["sha256"]
        if destination.exists():
            check_cache(lock, destination, report)
        else:
            os.rename(candidate, destination)
            freeze(destination)
            check_cache(lock, destination, report)
        if not locked:
            write_json(args.lock, lock)
        return lock, destination / "tree"


def check_cache(lock, cache, report):
    archive = cache / "archive.zip"
    tree = cache / "tree"
    if cache.is_symlink() or archive.is_symlink() or tree.is_symlink():
        raise ValueError("Locked cache roots must not be symlinks")
    if not archive.is_file() or not tree.is_dir():
        raise Unavailable("Locked cache is missing; no implicit download is allowed")
    if archive.stat().st_size != lock["archive"]["size_bytes"] or sha256(archive) != lock["archive"]["sha256"]:
        raise ValueError("Locked archive length/SHA-256 mismatch")
    permissions = filesystem_permissions(cache.parent, report)
    modes = archive_executable_modes(archive) if not permissions["file_execute_bits_enforced"] else None
    if manifest(tree, modes) != lock["files"]:
        raise ValueError("Extracted cache manifest mismatch")
    if os.name != "nt" and any(path.stat().st_mode & 0o222 for path in [cache, archive, tree, *tree.rglob("*")]
                              if not path.is_symlink() and (not path.is_dir() or permissions["directory_readonly_enforced"])):
        raise ValueError("Locked cache must remain read-only; writable content is refused")


def resolve(args, report, directory):
    if not args.lock.is_file():
        raise Unavailable("Nightly lock is missing; run explicit fetch")
    lock = read_json(args.lock)
    validate_lock(lock)
    if lock["source"]["kind"] != "official-nightly" and not args.allow_local_import:
        raise ValueError("Local-import lock is not official Nightly; fixture use requires --allow-local-import")
    cache = args.cache_dir / lock["archive"]["sha256"]
    check_cache(lock, cache, report)
    measured = measure_tools(cache / "tree", directory, report, args.timeout)
    if measured != lock["tools"]:
        raise ValueError("Compiler identity/version differs from locked measurements")
    return lock, cache / "tree"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("operation", choices=("download", "fetch", "bump", "verify", "path"))
    parser.add_argument("--lock", type=Path, default=ROOT / "toolchains/cosmocc.lock.json")
    parser.add_argument("--cache-dir", type=Path, default=ROOT / ".cache/toolchains/cosmocc")
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
              "report_id": directory.name, "m0_core_status": "not_run",
              "started_at": now(), "finished_at": None, "host": host_facts(),
              "status": "not_run", "reason": "", "operation": args.operation,
              "implementation": {"script_sha256": sha256(Path(__file__))},
              "source": None, "ci": None, "qemu": None, "commands": [], "toolchain": None,
              "lock_path": str(args.lock.resolve()), "cache_root": str(args.cache_dir.resolve())}
    if os.environ.get("GITHUB_ACTIONS") == "true":
        report["ci"] = {key: os.environ.get(value) for key, value in {
            "run_id": "GITHUB_RUN_ID", "run_attempt": "GITHUB_RUN_ATTEMPT",
            "workflow": "GITHUB_WORKFLOW", "commit": "GITHUB_SHA",
            "runner_name": "RUNNER_NAME", "runner_os": "RUNNER_OS",
            "runner_arch": "RUNNER_ARCH", "image_os": "ImageOS", "image_version": "ImageVersion",
        }.items()}
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
    report["cases"] = [case_record("nightly." + args.operation, report["status"], report["reason"],
        started_at=report["started_at"], finished_at=report["finished_at"],
        commands=report["commands"], toolchain=report["toolchain"],
        expected={"official_url": NIGHTLY_URL, "operation": args.operation},
        actual={key: report[key] for key in ("downloaded_archive", "resolved_path") if key in report},
        artifact=({**report["downloaded_archive"], "path": report.get("download_path"), "kind": "cosmocc-original-zip"}
                  if "downloaded_archive" in report else None),
        exit_code=(report["commands"][-1]["exit_code"] if report["commands"] else None),
        timeout_seconds=args.timeout)]
    write_json(args.report, report)
    if report["status"] == "pass" and args.operation == "path":
        print(report["resolved_path"])
    else:
        print(report["reason"], file=sys.stderr)
    return {"pass": 0, "fail": 1, "blocked": 2}[report["status"]]


if __name__ == "__main__":
    sys.exit(main())
