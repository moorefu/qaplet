#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Explicit QuickJS source acquisition and offline verification (Python 3.10+)."""

import argparse
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import sys
import uuid

from foundation import command, host_facts


ROOT = Path(__file__).resolve().parents[1]
OFFICIAL = "https://github.com/bellard/quickjs.git"
# Git worktree metadata created on Windows contains Windows paths. Source supply
# commands operate only on their explicit upstream/cache repositories.
GIT_CWD = Path(ROOT.anchor)


class SourceError(Exception):
    def __init__(self, reason, status="fail"):
        super().__init__(reason)
        self.status = status


def now():
    return datetime.now(timezone.utc).isoformat()


def digest(data):
    return hashlib.sha256(data).hexdigest()


def canonical(value):
    return json.dumps(value, sort_keys=True, separators=(",", ":")).encode("utf-8")


def write_bytes(path, data):
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_name(path.name + "." + uuid.uuid4().hex + ".tmp")
    try:
        temporary.write_bytes(data)
        os.replace(temporary, path)
    finally:
        temporary.unlink(missing_ok=True)


def write_json(path, value):
    write_bytes(path, (json.dumps(value, indent=2, ensure_ascii=False) + "\n").encode("utf-8"))


def run(report, argv, cwd, timeout):
    directory = Path(report["evidence_directory"])
    label = str(len(report["commands"]))
    env = dict(os.environ, GIT_TERMINAL_PROMPT="0", GIT_CONFIG_NOSYSTEM="1")
    record, _, _ = command(argv, directory, label, timeout, cwd=cwd, env=env)
    report["commands"].append(record)
    if record["exit_code"] != 0 or record["timed_out"]:
        raise SourceError("Command failed; see raw logs: " + " ".join(map(str, argv[:3])), "blocked")
    return Path(record["stdout_log"]).read_bytes()


def manifest(source):
    files = []
    for path in sorted(source.rglob("*"), key=lambda item: item.relative_to(source).as_posix()):
        relative = path.relative_to(source)
        if relative.parts[0] == ".git":
            continue
        if path.is_symlink():
            raise SourceError("Source symlink is unsupported: " + relative.as_posix())
        if path.is_file():
            data = path.read_bytes()
            files.append({"path": relative.as_posix(), "size": len(data), "sha256": digest(data)})
    return {"sha256": digest(canonical(files)), "files": files}


def cache_path(args, lock):
    return args.cache / (lock["commit"] + "-" + lock["patch_manifest_sha256"][:16]) / "source"


def repository(value):
    # The public lock is Bellard-only. Absolute local repositories are an explicit
    # fixture/mirror seam, and are always recorded as that local identity.
    if not isinstance(value, str) or (value != OFFICIAL and not Path(value).is_absolute()):
        raise SourceError("Repository must be the official Bellard URL or an absolute local path")
    return value


def validate_lock(lock):
    if not isinstance(lock, dict) or lock.get("schema_version") != 1:
        raise SourceError("Unsupported source lock schema version")
    repository(lock["repository"])
    if lock["ref"] != "refs/heads/master" or not re.fullmatch(r"[0-9a-f]{40}", lock["commit"]):
        raise SourceError("Source lock must contain the resolved master commit")
    acquired = datetime.fromisoformat(lock["fetched_at"])
    if acquired.tzinfo is None or acquired.utcoffset() is None:
        raise SourceError("Acquisition time must contain a timezone")
    if not re.fullmatch(r"[0-9a-f]{64}", lock["patch_manifest_sha256"]):
        raise SourceError("Invalid patch manifest digest")
    for label in ("upstream", "source"):
        content = lock[label]
        entries = content["files"]
        if not isinstance(entries, list) or not entries or digest(canonical(entries)) != content["sha256"]:
            raise SourceError("Invalid " + label + " content identity")
        names = [entry["path"] for entry in entries]
        if names != sorted(set(names)):
            raise SourceError("Source paths must be unique and portably ordered")
        for entry in entries:
            if not isinstance(entry["path"], str) or not entry["path"]:
                raise SourceError("Invalid source manifest path")
            path = Path(entry["path"])
            parts = entry["path"].split("/")
            if (path.is_absolute() or any(part in ("", ".", "..") for part in parts)
                    or "\\" in entry["path"] or ":" in entry["path"] or parts[0] == ".git"):
                raise SourceError("Invalid source manifest path")
            if not re.fullmatch(r"[0-9a-f]{64}", entry["sha256"]) or type(entry["size"]) is not int or entry["size"] < 0:
                raise SourceError("Invalid source file identity")
    licenses = [entry for entry in lock["upstream"]["files"] if entry["path"] == "LICENSE"]
    if len(licenses) != 1 or lock["license"] != {"path": "LICENSE", "upstream_path": "LICENSE", "sha256": licenses[0]["sha256"]}:
        raise SourceError("Original license identity does not match upstream source")


def patch_manifest(args):
    path = args.lock.parent / "patches.json"
    data = path.read_bytes() if path.is_file() else b'{\n  "schema_version": 1,\n  "patches": []\n}\n'
    value = json.loads(data)
    if not isinstance(value, dict) or value.get("schema_version") != 1 or not isinstance(value.get("patches"), list):
        raise SourceError("Invalid patch manifest")
    paths = set()
    for patch in value["patches"]:
        if not isinstance(patch, dict) or not isinstance(patch.get("path"), str) or "\\" in patch["path"]:
            raise SourceError("Invalid patch entry")
        relative = Path(patch["path"])
        if relative.is_absolute() or ".." in relative.parts or relative.parts[:1] != ("patches",):
            raise SourceError("Patch path must be below metadata patches/")
        candidate = args.lock.parent / relative
        if candidate.is_symlink() or not candidate.resolve().is_relative_to(args.lock.parent):
            raise SourceError("Patch path escapes metadata directory")
        if patch["path"] in paths:
            raise SourceError("Duplicate patch path")
        paths.add(patch["path"])
        if not patch.get("origin") or not patch.get("reason"):
            raise SourceError("Patch must record origin and reason")
        if not candidate.is_file() or digest(candidate.read_bytes()) != patch["sha256"]:
            raise SourceError("Patch is missing or its digest differs: " + patch["path"])
    return value["patches"], data


def apply_patches(args, report, source, patches):
    git = shutil.which("git")
    for patch in patches:
        path = str(args.lock.parent / patch["path"])
        run(report, [git, "-C", str(source), "apply", "--check", path], GIT_CWD, args.timeout)
        run(report, [git, "-C", str(source), "apply", path], GIT_CWD, args.timeout)


def verify(args, lock):
    patch_file = args.lock.parent / "patches.json"
    if not patch_file.is_file() or digest(patch_file.read_bytes()) != lock["patch_manifest_sha256"]:
        raise SourceError("Ordered patch manifest is missing or modified")
    patches, _ = patch_manifest(args)
    if patches != lock["patches"]:
        raise SourceError("Patch order or metadata differs from the lock")
    source = cache_path(args, lock)
    if not source.is_dir():
        raise SourceError("Locked source cache is missing; run explicit fetch", "blocked")
    if manifest(source) != lock["source"]:
        raise SourceError("Source content differs from the locked clean snapshot")
    head = source / ".git/HEAD"
    if not head.is_file() or head.read_text(encoding="ascii").strip() != lock["commit"]:
        raise SourceError("Cached checkout commit differs from the lock")
    license_path = args.lock.parent / "LICENSE"
    if not license_path.is_file() or digest(license_path.read_bytes()) != lock["license"]["sha256"]:
        raise SourceError("Original license is missing or modified")
    return {"repository": lock["repository"], "ref": lock["ref"], "commit": lock["commit"],
            "sha256": lock["source"]["sha256"], "path": str(source),
            "clean": True, "patches": lock["patches"], "lock_sha256": digest(args.lock.read_bytes())}


def acquire(args, report, commit):
    git = shutil.which("git")
    if not git:
        raise SourceError("Git is required for explicit acquisition", "blocked")
    source = args.cache / "candidates" / uuid.uuid4().hex / "source"
    source.mkdir(parents=True)
    run(report, [git, "init", str(source)], GIT_CWD, args.timeout)
    for key, value in (("core.autocrlf", "false"), ("core.eol", "lf")):
        run(report, [git, "-C", str(source), "config", key, value], GIT_CWD, args.timeout)
    run(report, [git, "-C", str(source), "fetch", "--depth=1", args.repository, commit], GIT_CWD, args.timeout)
    run(report, [git, "-C", str(source), "checkout", "--detach", "FETCH_HEAD"], GIT_CWD, args.timeout)
    actual = run(report, [git, "-C", str(source), "rev-parse", "HEAD"], GIT_CWD, args.timeout).decode().strip()
    if actual != commit:
        raise SourceError("Fetched commit differs from the resolved commit")
    clean = run(report, [git, "-C", str(source), "status", "--porcelain", "--untracked-files=all"], GIT_CWD, args.timeout)
    if clean:
        raise SourceError("Fresh upstream checkout is dirty")
    return source


def bump(args, report):
    repository(args.repository)
    patches, patch_bytes = patch_manifest(args)
    git = shutil.which("git")
    if not git:
        raise SourceError("Git is required for explicit bump", "blocked")
    resolved = run(report, [git, "ls-remote", args.repository, "refs/heads/master"], GIT_CWD, args.timeout).decode().split()
    if len(resolved) != 2 or not re.fullmatch(r"[0-9a-f]{40}", resolved[0]) or resolved[1] != "refs/heads/master":
        raise SourceError("Upstream master did not resolve to exactly one commit")
    source = acquire(args, report, resolved[0])
    license_path = source / "LICENSE"
    if not license_path.is_file() or not license_path.stat().st_size:
        raise SourceError("Upstream original LICENSE is missing")
    license_bytes = license_path.read_bytes()
    identity = manifest(source)
    apply_patches(args, report, source, patches)
    lock = {"schema_version": 1, "repository": args.repository, "ref": "refs/heads/master",
            "commit": resolved[0], "fetched_at": now(), "upstream": identity, "source": manifest(source),
            "patch_manifest_sha256": digest(patch_bytes), "patches": patches,
            "license": {"upstream_path": "LICENSE", "path": "LICENSE", "sha256": digest(license_bytes)}}
    validate_lock(lock)
    target = cache_path(args, lock)
    target.parent.mkdir(parents=True, exist_ok=True)
    if target.exists():
        head = target / ".git/HEAD"
        if manifest(target) != lock["source"] or not head.is_file() or head.read_text().strip() != lock["commit"]:
            raise SourceError("Existing cache is modified; candidate retained and prior lock preserved")
    else:
        source.rename(target)
    args.lock.parent.mkdir(parents=True, exist_ok=True)
    metadata = {args.lock.parent / "LICENSE": license_bytes,
                args.lock.parent / "patches.json": patch_bytes}
    previous = {path: path.read_bytes() if path.exists() else None for path in metadata}
    published = []
    try:
        for path, data in metadata.items():
            if previous[path] != data:
                write_bytes(path, data)
                published.append(path)
        write_json(args.lock, lock)
    except OSError:
        # The lock is the commit point. Failed publication must leave the old
        # lock's license/patch bytes usable, including Windows read-only locks.
        for path in reversed(published):
            if previous[path] is None:
                path.unlink()
            else:
                write_bytes(path, previous[path])
        raise
    return verify(args, lock)


def fetch(args, report, lock):
    patch_file = args.lock.parent / "patches.json"
    if not patch_file.is_file():
        raise SourceError("Ordered patch manifest is missing")
    license_path = args.lock.parent / "LICENSE"
    if not license_path.is_file() or digest(license_path.read_bytes()) != lock["license"]["sha256"]:
        raise SourceError("Original license is missing or modified")
    patches, data = patch_manifest(args)
    if digest(data) != lock["patch_manifest_sha256"] or patches != lock["patches"]:
        raise SourceError("Ordered patch manifest differs from the lock")
    target = cache_path(args, lock)
    if not target.exists():
        args.repository = lock["repository"]
        source = acquire(args, report, lock["commit"])
        if manifest(source) != lock["upstream"]:
            raise SourceError("Fetched upstream content differs from the lock")
        apply_patches(args, report, source, patches)
        if manifest(source) != lock["source"]:
            raise SourceError("Patched source differs from the lock")
        target.parent.mkdir(parents=True, exist_ok=True)
        source.rename(target)
    return verify(args, lock)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("operation", choices=("verify", "fetch", "bump"))
    parser.add_argument("--lock", type=Path, default=ROOT / "third_party/quickjs/source.lock.json")
    parser.add_argument("--cache", type=Path, default=ROOT / ".cache/quickjs")
    parser.add_argument("--report", type=Path, default=ROOT / "out/quickjs-source.json")
    parser.add_argument("--repository", default=OFFICIAL)
    parser.add_argument("--timeout", type=int, default=120)
    args = parser.parse_args()
    if not 1 <= args.timeout <= 300:
        parser.error("--timeout must be between 1 and 300 seconds")
    report = {"schema_version": 1, "scope": "quickjs-source", "report_id": str(uuid.uuid4()),
              "operation": args.operation, "started_at": now(), "commands": [],
              "status": "blocked", "reason": "Source lock is missing", "source": None,
              "host": host_facts(), "m0_core_status": "not_run", "ci": None, "qemu": None,
              "toolchain": None, "build": None, "artifact": None,
              "runner": {"path": str(Path(__file__).resolve()), "sha256": digest(Path(__file__).read_bytes())}}
    args.lock, args.cache, args.report = args.lock.resolve(), args.cache.resolve(), args.report.resolve()
    evidence = args.report.parent / "evidence" / report["report_id"]
    evidence.mkdir(parents=True, exist_ok=True)
    report["evidence_directory"] = str(evidence)
    try:
        if args.operation == "bump":
            report["source"] = bump(args, report)
        elif args.lock.is_file():
            lock = json.loads(args.lock.read_text(encoding="utf-8"))
            validate_lock(lock)
            report["source"] = fetch(args, report, lock) if args.operation == "fetch" else verify(args, lock)
        else:
            raise SourceError("Source lock is missing", "blocked")
        report.update(status="pass", reason="Locked QuickJS source verified; no compilation or runtime claim")
    except SourceError as error:
        report.update(status=error.status, reason=str(error))
    except (OSError, ValueError, KeyError, TypeError) as error:
        report.update(status="fail", reason="Invalid source input: " + str(error))
    report["finished_at"] = now()
    args.report.parent.mkdir(parents=True, exist_ok=True)
    report["cases"] = [{"case_id": "quickjs-source." + args.operation,
                        "started_at": report["started_at"], "finished_at": report["finished_at"],
                        "status": report["status"], "reason": report["reason"],
                        "expected": "Exact locked source, original license, and ordered patches",
                        "actual": report["source"], "exit_code": {"pass": 0, "fail": 1, "blocked": 2}[report["status"]],
                        "timeout_seconds": args.timeout, "commands": report["commands"],
                        "toolchain": None, "build": None, "artifact": None}]
    write_json(args.report, report)
    print(json.dumps({"status": report["status"], "reason": report["reason"], "report": str(args.report)}))
    return {"pass": 0, "fail": 1, "blocked": 2}[report["status"]]


if __name__ == "__main__":
    sys.exit(main())
