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
import subprocess
import sys
import uuid


ROOT = Path(__file__).resolve().parents[1]
OFFICIAL = "https://github.com/bellard/quickjs.git"


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


def write_json(path, value):
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_name(path.name + "." + uuid.uuid4().hex + ".tmp")
    temporary.write_bytes((json.dumps(value, indent=2, ensure_ascii=False) + "\n").encode("utf-8"))
    os.replace(temporary, path)


def run(report, argv, cwd, timeout):
    directory = Path(report["evidence_directory"])
    label = str(len(report["commands"]))
    record = {"argv": [str(arg) for arg in argv], "cwd": str(cwd), "started_at": now(),
              "exit_code": None, "timed_out": False, "timeout_seconds": timeout,
              "stdout_log": str(directory / (label + ".stdout.log")),
              "stderr_log": str(directory / (label + ".stderr.log"))}
    output, errors = b"", b""
    env = dict(os.environ, GIT_TERMINAL_PROMPT="0", GIT_CONFIG_NOSYSTEM="1")
    try:
        result = subprocess.run(argv, cwd=cwd, env=env, capture_output=True, timeout=timeout)
        record["exit_code"] = result.returncode
        output, errors = result.stdout, result.stderr
    except subprocess.TimeoutExpired as error:
        record["timed_out"] = True
        output, errors = error.stdout or b"", error.stderr or b""
    except OSError as error:
        errors = str(error).encode("utf-8")
    record["finished_at"] = now()
    Path(record["stdout_log"]).write_bytes(output)
    Path(record["stderr_log"]).write_bytes(errors)
    report["commands"].append(record)
    if record["exit_code"] != 0 or record["timed_out"]:
        raise SourceError("Command failed; see raw logs: " + " ".join(map(str, argv[:3])), "blocked")
    return output


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


def patch_manifest(args):
    path = args.lock.parent / "patches.json"
    data = path.read_bytes() if path.is_file() else b'{\n  "schema_version": 1,\n  "patches": []\n}\n'
    value = json.loads(data)
    if value.get("schema_version") != 1 or not isinstance(value.get("patches"), list):
        raise SourceError("Invalid patch manifest")
    paths = set()
    for patch in value["patches"]:
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
        run(report, [git, "-C", str(source), "apply", "--check", path], ROOT, args.timeout)
        run(report, [git, "-C", str(source), "apply", path], ROOT, args.timeout)


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
    run(report, [git, "init", str(source)], ROOT, args.timeout)
    for key, value in (("core.autocrlf", "false"), ("core.eol", "lf")):
        run(report, [git, "-C", str(source), "config", key, value], ROOT, args.timeout)
    run(report, [git, "-C", str(source), "fetch", "--depth=1", args.repository, commit], ROOT, args.timeout)
    run(report, [git, "-C", str(source), "checkout", "--detach", "FETCH_HEAD"], ROOT, args.timeout)
    actual = run(report, [git, "-C", str(source), "rev-parse", "HEAD"], ROOT, args.timeout).decode().strip()
    if actual != commit:
        raise SourceError("Fetched commit differs from the resolved commit")
    clean = run(report, [git, "-C", str(source), "status", "--porcelain", "--untracked-files=all"], ROOT, args.timeout)
    if clean:
        raise SourceError("Fresh upstream checkout is dirty")
    return source


def bump(args, report):
    patches, patch_bytes = patch_manifest(args)
    git = shutil.which("git")
    if not git:
        raise SourceError("Git is required for explicit bump", "blocked")
    resolved = run(report, [git, "ls-remote", args.repository, "refs/heads/master"], ROOT, args.timeout).decode().split()
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
    target = cache_path(args, lock)
    target.parent.mkdir(parents=True, exist_ok=True)
    if target.exists():
        head = target / ".git/HEAD"
        if manifest(target) != lock["source"] or not head.is_file() or head.read_text().strip() != lock["commit"]:
            raise SourceError("Existing cache is modified; candidate retained and prior lock preserved")
    else:
        source.rename(target)
    args.lock.parent.mkdir(parents=True, exist_ok=True)
    (args.lock.parent / "LICENSE").write_bytes(license_bytes)
    (args.lock.parent / "patches.json").write_bytes(patch_bytes)
    write_json(args.lock, lock)
    return verify(args, lock)


def fetch(args, report, lock):
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
    parser.add_argument("--cache", type=Path, default=ROOT / "out/quickjs")
    parser.add_argument("--report", type=Path, default=ROOT / "out/quickjs-source.json")
    parser.add_argument("--repository", default="https://github.com/bellard/quickjs.git")
    parser.add_argument("--timeout", type=int, default=120)
    args = parser.parse_args()
    report = {"schema_version": 1, "scope": "quickjs-source", "report_id": str(uuid.uuid4()),
              "operation": args.operation, "started_at": now(), "commands": [],
              "status": "blocked", "reason": "Source lock is missing", "source": None}
    args.lock, args.cache, args.report = args.lock.resolve(), args.cache.resolve(), args.report.resolve()
    evidence = args.report.parent / "evidence" / report["report_id"]
    evidence.mkdir(parents=True, exist_ok=True)
    report["evidence_directory"] = str(evidence)
    try:
        if args.operation == "bump":
            report["source"] = bump(args, report)
        elif args.lock.is_file():
            lock = json.loads(args.lock.read_text(encoding="utf-8"))
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
    args.report.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({"status": report["status"], "reason": report["reason"], "report": str(args.report)}))
    return {"pass": 0, "fail": 1, "blocked": 2}[report["status"]]


if __name__ == "__main__":
    sys.exit(main())
