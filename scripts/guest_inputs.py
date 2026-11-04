#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Explicit same-repository transport of the unchanged locked guest inputs."""
import argparse
import json
import os
from pathlib import Path
import re
import shutil
import sys
import tempfile
from types import SimpleNamespace
import uuid

import arm64_guest
from build_ape import BuildError, identity, run
from foundation import host_facts, now
from startup import ci_facts
from workflow_support import guard_evidence_outputs, read_json, write_json

ROOT = Path(__file__).resolve().parents[1]


def plan(args, report):
    config = read_json(args.config)
    current = identity(arm64_guest.LOCK)
    if (config.get("schema_version") != 1 or config.get("page_size") != 65536 or
            config.get("guest_lock") != {key: current[key] for key in ("sha256", "size_bytes")} or
            not isinstance(config.get("source_commit"), str) or
            not re.fullmatch(r"[0-9a-f]{40}", config["source_commit"])):
        raise BuildError("CI supply configuration requires the current guest lock and a fixed complete source commit")
    report.update(config=identity(args.config), source_commit=config["source_commit"], execution_status="not_run")
    if args.github_output:
        with args.github_output.open("a", encoding="utf-8") as output:
            output.write("source_commit=" + config["source_commit"] + "\n")


def locked_manifest(source, page_size):
    manifest = read_json(source / "manifest.json")
    lock = read_json(arm64_guest.LOCK)
    profile = lock["profiles"][str(page_size)]
    expected = []
    for name in [*lock["metadata"], *lock["common_packages"], *profile["packages"]]:
        item = lock["metadata"].get(name) or lock["packages"][name]
        expected.append({"name": name, "size_bytes": item["size_bytes"], "sha256": item["sha256"],
                         "url": item.get("url") or lock["archive_base_url"] + item["archive_path"]})
    current_lock = identity(arm64_guest.LOCK)
    if (set(manifest) != {"schema_version", "page_size", "profile", "guest_lock", "provenance", "inputs"} or
            type(manifest.get("schema_version")) is not int or manifest["schema_version"] != 1 or manifest.get("page_size") != page_size or
            manifest.get("profile") != profile["id"] or
            manifest.get("guest_lock") != {key: current_lock[key] for key in ("sha256", "size_bytes")} or
            manifest.get("inputs") != expected or not isinstance(manifest.get("provenance"), str) or
            not manifest["provenance"].strip()):
        raise BuildError("Supply manifest must match the exact current guest lock and complete selected profile")
    return manifest


def unaliased(path, regular=False):
    path = path.absolute()
    if path != path.resolve() or any(parent.is_symlink() for parent in (path, *path.parents)):
        raise BuildError("Filesystem alias is not allowed for supplied or cached inputs: " + str(path))
    if regular and (not path.is_file() or path.stat().st_nlink != 1):
        raise BuildError("Input must be a single regular file without hardlink aliases: " + str(path))


def source_layout(source, target, manifest):
    if source.is_relative_to(target) or target.is_relative_to(source):
        raise BuildError("Supply and canonical input directories must not overlap")
    unaliased(source)
    unaliased(target)
    if {path.name for path in source.iterdir()} - {".git", "manifest.json", "inputs"}:
        raise BuildError("Unexpected member in supplied input root")
    unaliased(source / "manifest.json", regular=True)
    unaliased(source / "inputs")
    if {path.name for path in (source / "inputs").iterdir()} != {item["name"] for item in manifest["inputs"]}:
        raise BuildError("Supplied input directory must contain exactly the locked profile members")
    for item in manifest["inputs"]:
        unaliased(source / "inputs" / item["name"], regular=True)


def import_inputs(args, report, directory):
    if not re.fullmatch(r"[0-9a-f]{40}", args.source_commit):
        raise BuildError("Source commit must be a complete lowercase Git commit SHA")
    source = args.source_dir.absolute()
    if not source.is_dir() or not (source / "manifest.json").is_file():
        raise BuildError("missing_resource: pinned same-repository guest input checkout is unavailable", "blocked")
    manifest = locked_manifest(source, args.page_size)
    target = args.cache / "inputs"
    source_layout(source, target, manifest)
    _, head, _ = run(report, directory, "supply-commit", ["git", "-C", source, "rev-parse", "HEAD"], 30)
    if head.strip() != args.source_commit:
        raise BuildError("Supplied checkout commit differs from the pinned source commit")
    retained_manifest = directory / "supplied-manifest.json"
    shutil.copyfile(source / "manifest.json", retained_manifest)
    report["supply"] = {"commit": head.strip(), "manifest": identity(source / "manifest.json"),
                        "manifest_copy": identity(retained_manifest),
                        "provenance": manifest["provenance"], "inputs": []}
    args.cache.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="guest-input-import-", dir=args.cache.parent) as temporary:
        stage = Path(temporary)
        (stage / "inputs").mkdir()
        for item in manifest["inputs"]:
            shutil.copyfile(source / "inputs" / item["name"], stage / "inputs" / item["name"])
        arm64_guest.verify(SimpleNamespace(cache=stage, page_size=args.page_size, keyring=args.keyring), report, directory)
        # All supplied bytes and the complete signature/index chain pass before
        # any canonical input is created. Existing bad bytes are never repaired.
        existing = set()
        for item in manifest["inputs"]:
            name = item["name"]
            arm64_guest.checked(source / "inputs" / name, item)
            if (target / name).exists() or (target / name).is_symlink():
                unaliased(target / name, regular=True)
                arm64_guest.checked(target / name, item)
                existing.add(name)
        created = []
        if not target.exists():
            args.cache.mkdir(parents=True, exist_ok=True)
            (stage / "inputs").rename(target)
        else:
            try:
                for item in manifest["inputs"]:
                    name = item["name"]
                    if name not in existing:
                        # Exclusive publication cannot replace a concurrently
                        # created or corrupt canonical file.
                        os.link(stage / "inputs" / name, target / name)
                        created.append(target / name)
            except OSError:
                for path in created:
                    path.unlink()
                raise
        for item in manifest["inputs"]:
            name = item["name"]
            report["supply"]["inputs"].append({"name": name, "source": identity(source / "inputs" / name),
                "destination": identity(target / name), "url": item["url"],
                "cache_status": "existing-verified" if name in existing else "imported"})
        # Keep the verified identities at their final locations, not the removed staging paths.
        report["inputs"] = [identity(target / item["name"]) for item in manifest["inputs"]]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("operation", choices=("import", "plan"))
    parser.add_argument("--source-dir", type=Path)
    parser.add_argument("--source-commit")
    parser.add_argument("--config", type=Path, default=ROOT / "guests/ci-inputs.json")
    parser.add_argument("--github-output", type=Path)
    parser.add_argument("--cache", type=Path, default=ROOT / ".cache/guests")
    parser.add_argument("--page-size", type=int, choices=(4096, 65536), default=65536)
    parser.add_argument("--keyring", type=Path, default=Path("/usr/share/keyrings/ubuntu-archive-keyring.gpg"))
    parser.add_argument("--report", type=Path, required=True)
    args = parser.parse_args()
    if args.operation == "import" and (not args.source_dir or not args.source_commit):
        parser.error("import requires --source-dir and --source-commit")
    args.cache, args.report = args.cache.absolute(), args.report.absolute()
    directory = args.report.parent / "evidence" / str(uuid.uuid4())
    try:
        roots = [args.cache / "inputs"]
        protected = [arm64_guest.LOCK, args.keyring, args.config, Path(__file__), Path(arm64_guest.__file__)]
        lock = read_json(arm64_guest.LOCK)
        names = [*lock["metadata"], *lock["packages"]]
        protected.extend(args.cache / "inputs" / name for name in names)
        if args.source_dir:
            roots.append(args.source_dir)
            protected.append(args.source_dir / "manifest.json")
            protected.extend(args.source_dir / "inputs" / name for name in names)
        guard_evidence_outputs([args.report, directory, args.github_output], [], roots=roots,
                               files=protected)
    except (OSError, ValueError) as error:
        print(json.dumps({"status": "fail", "reason": str(error), "report_written": False}))
        return 1
    directory.mkdir(parents=True)
    report = {"schema_version": 1, "report_kind": "arm64-guest-input-supply", "report_id": directory.name,
              "started_at": now(), "finished_at": None, "status": "not_run", "reason": "", "host": host_facts(),
              "ci": ci_facts(), "guest": None, "qemu": None, "cases": [], "commands": [], "inputs": [],
              "invocation": {"argv": [sys.executable, str(Path(__file__).resolve()), *sys.argv[1:]], "cwd": str(Path.cwd())},
              "source": {"runner": identity(Path(__file__)), "verifier": identity(Path(arm64_guest.__file__)),
                         "lock": identity(arm64_guest.LOCK)}}
    try:
        if args.operation == "plan":
            plan(args, report)
        else:
            import_inputs(args, report, directory)
        report.update(status="pass", reason="Locked input supply " + args.operation + " completed; no HTTP or guest execution")
    except BuildError as error:
        report.update(status=error.status, reason=str(error))
    except (OSError, ValueError, KeyError, TypeError, AttributeError) as error:
        report.update(status="fail", reason=str(error))
    report["finished_at"] = now()
    write_json(args.report, report)
    print(json.dumps({"status": report["status"], "reason": report["reason"], "report": str(args.report)}))
    return {"pass": 0, "fail": 1, "blocked": 2}[report["status"]]


if __name__ == "__main__":
    sys.exit(main())
