#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Acquire or verify explicit CI archives and emit small, typed report summaries."""
import argparse
import hashlib
import ipaddress
import json
import os
from pathlib import Path, PurePosixPath
import re
import shutil
import stat
import urllib.parse
import urllib.request
import uuid
import zipfile

from workflow_support import identity, local_path, now, read_json, workspace, write_json

ROOT = Path(__file__).resolve().parents[1]


class ReportCollision(ValueError):
    """An invalid output target must never receive even a failure report."""


def guard_report_target(args):
    target = args.report.resolve()
    if args.operation in ("summarize", "runs"):
        source = args.input_report if args.operation == "summarize" else args.input_response
        if target == source.resolve():
            raise ReportCollision("Report target collides with its source input")
        return
    if target == args.manifest.resolve() or target.is_relative_to(args.output_dir.resolve()):
        raise ReportCollision("Report target collides with input or extracted evidence; use a separate report file")
    ctx = workspace(args.repo)
    if not target.is_relative_to((Path(ctx["checkout"]) / "out").resolve()):
        raise ReportCollision("Collection reports must be written below checkout out/, outside the extraction directory")
    for item in selection(args.manifest):
        source = item["archive"].get("path")
        if source and local_path(source, args.manifest.resolve().parent) == target:
            raise ReportCollision("Report target collides with an original archive")


def selection(path):
    data = read_json(path)
    if data.get("schema_version") != 1 or not isinstance(data.get("artifacts"), list) or not data["artifacts"]:
        raise ValueError("Expected schema_version 1 with nonempty artifacts")
    names, ids, records = set(), set(), []
    for item in data["artifacts"]:
        name, number, archive = item["name"], item["id"], item["archive"]
        if not isinstance(name, str) or not re.fullmatch(r"[A-Za-z0-9][A-Za-z0-9._-]{0,199}", name):
            raise ValueError("Unsafe artifact name")
        if not isinstance(number, int) or isinstance(number, bool) or number <= 0 or number in ids or name.casefold() in names:
            raise ValueError("Duplicate or invalid artifact identity")
        if not isinstance(archive["sha256"], str) or not re.fullmatch(r"[0-9a-f]{64}", archive["sha256"]):
            raise ValueError("Expected an exact lowercase SHA-256")
        size = archive["size_bytes"]
        if not isinstance(size, int) or isinstance(size, bool) or not 0 < size <= 2 * 1024**3:
            raise ValueError("Invalid compressed archive size")
        names.add(name.casefold())
        ids.add(number)
        records.append({"id": number, "name": name, "archive": archive})
    return records


def check_url(value):
    parsed = urllib.parse.urlparse(value)
    if parsed.username or parsed.password or not parsed.hostname:
        raise ValueError("Artifact URLs must omit user information")
    loopback = False
    try:
        loopback = ipaddress.ip_address(parsed.hostname).is_loopback
    except ValueError:
        pass
    if parsed.scheme != "https" and not (parsed.scheme == "http" and loopback):
        raise ValueError("Artifact acquisition requires HTTPS or a literal loopback HTTP address")
    return value


class CheckedRedirect(urllib.request.HTTPRedirectHandler):
    def redirect_request(self, req, fp, code, msg, headers, newurl):
        check_url(newurl)
        return super().redirect_request(req, fp, code, msg, headers, newurl)


def matches(path, expected):
    actual = identity(path)
    return all(actual[key] == expected[key] for key in ("sha256", "size_bytes"))


def acquire(item, cache, manifest, allow_fetch):
    target = cache / (item["name"] + ".zip")
    expected = item["archive"]
    if target.exists():
        if target.is_symlink() or not matches(target, expected):
            raise ValueError("Cached archive identity mismatch: " + item["name"])
        return target
    if not allow_fetch:
        raise ValueError("Required archive is absent from cache: " + item["name"])
    source = expected.get("path")
    url = expected.get("url")
    if bool(source) == bool(url):
        raise ValueError("Explicit fetch requires exactly one archive.path or archive.url")
    cache.mkdir(parents=True, exist_ok=True)
    temporary = cache / (".partial-" + uuid.uuid4().hex)
    try:
        if source:
            original = local_path(source, manifest.resolve().parent)
            if original.stat().st_size != expected["size_bytes"]:
                raise ValueError("Source archive length mismatch")
            shutil.copyfile(original, temporary)
        else:
            check_url(url)
            try:
                opener = urllib.request.build_opener(CheckedRedirect())
                with opener.open(url, timeout=45) as incoming, temporary.open("xb") as output:
                    total = 0
                    while block := incoming.read(1024 * 1024):
                        total += len(block)
                        if total > expected["size_bytes"]:
                            raise ValueError("Downloaded archive exceeds declared length")
                        output.write(block)
            except Exception as error:
                raise ValueError("Artifact acquisition failed for " + item["name"] + ": " + type(error).__name__) from None
        if not matches(temporary, expected):
            raise ValueError("Acquired archive identity mismatch: " + item["name"])
        temporary.replace(target)
        return target
    finally:
        temporary.unlink(missing_ok=True)


def members(bundle, maximum):
    seen, total = set(), 0
    items = bundle.infolist()
    if len(items) > 100000:
        raise ValueError("Archive member limit exceeded")
    for item in items:
        name = item.filename
        path = PurePosixPath(name)
        if (not path.parts or path.is_absolute() or ".." in path.parts or "\\" in name or ":" in name or
                "\x00" in name or path.as_posix().casefold() in seen):
            raise ValueError("Unsafe or duplicate archive member: " + repr(name))
        kind = stat.S_IFMT(item.external_attr >> 16)
        if kind not in (0, stat.S_IFREG, stat.S_IFDIR):
            raise ValueError("Archive symlink or special member rejected")
        if item.is_dir() and (item.file_size or item.CRC):
            raise ValueError("Archive directory payload or CRC rejected")
        seen.add(path.as_posix().casefold())
        total += item.file_size
        if total > maximum:
            raise ValueError("Archive expansion limit exceeded")
    return items


def extracted_files(root):
    files = set()
    for parent, directories, names in os.walk(root, followlinks=False):
        for name in [*directories, *names]:
            path = Path(parent) / name
            if path.is_symlink():
                raise ValueError("Extracted aliases are rejected")
        files.update((Path(parent) / name).relative_to(root).as_posix() for name in names)
    return files


def unpack(archive, destination, maximum):
    if destination.is_symlink():
        raise ValueError("Extraction target is an alias")
    existing = destination.exists()
    temporary = destination.parent / (".candidate-" + uuid.uuid4().hex)
    root = destination if existing else temporary
    records = []
    try:
        with zipfile.ZipFile(archive) as bundle:
            items = members(bundle, maximum)
            if not existing:
                root.mkdir(parents=True)
            for item in items:
                name = PurePosixPath(item.filename).as_posix()
                target = root / name
                if not target.resolve().is_relative_to(root.resolve()):
                    raise ValueError("Archive member escaped extraction root")
                if item.is_dir():
                    if not existing:
                        target.mkdir(parents=True, exist_ok=True)
                    continue
                value, size = hashlib.sha256(), 0
                if not existing:
                    target.parent.mkdir(parents=True, exist_ok=True)
                with bundle.open(item) as incoming:
                    output = None if existing else target.open("xb")
                    try:
                        while block := incoming.read(1024 * 1024):
                            value.update(block)
                            size += len(block)
                            if output:
                                output.write(block)
                    finally:
                        if output:
                            output.close()
                record = {"member": name, "size_bytes": size, "sha256": value.hexdigest()}
                if existing and (target.is_symlink() or not target.is_file() or not matches(target, record)):
                    raise ValueError("Extracted byte mismatch: " + name)
                records.append(record)
        if extracted_files(root) != {r["member"] for r in records}:
            raise ValueError("Extracted file inventory differs from archive")
        if not existing:
            root.replace(destination)
        return records
    finally:
        if temporary.exists():
            # Only the unique staging directory created below this explicit output
            # parent is removed; ZIP symlinks were rejected before writing files.
            if temporary.parent.resolve() != destination.parent.resolve() or temporary.is_symlink():
                raise ValueError("Unsafe extraction cleanup target")
            shutil.rmtree(temporary)


def collect(args):
    ctx = workspace(args.repo)
    repo, cache = Path(ctx["checkout"]), Path(ctx["cache_root"]) / "downloads/ci"
    if cache.resolve() != cache:
        raise ValueError("Archive cache must remain within the physical main .cache")
    output = args.output_dir.resolve()
    if not output.is_relative_to((repo / "out").resolve()) or output == (repo / "out").resolve():
        raise ValueError("Extracted evidence must use a directory below checkout out/")
    if not 0 < args.max_unpacked_bytes <= 8 * 1024**3:
        raise ValueError("Invalid archive expansion bound")
    selected = selection(args.manifest)
    output.mkdir(parents=True, exist_ok=True)
    records = []
    for item in selected:
        archive = acquire(item, cache, args.manifest, args.operation == "fetch")
        destination = output / item["name"]
        files = unpack(archive, destination, args.max_unpacked_bytes)
        records.append({"id": item["id"], "name": item["name"], "archive": identity(archive),
                        "root": str(destination), "files": files})
    return {"status": "pass", "scope": "archive-verification", "artifacts": records, "context": ctx}


def summarize(path):
    data = read_json(path)
    known = {"ape-build", "ape-startup", "ape-page-plan", "ape-loaders", "native-io", "pages64k-build",
             "pages64k-validation", "arm64-guest", "arm64-guest-inputs", "offline-integrity",
             "svc-job-build", "svc-job-run", "svc-scm-build", "svc-scm-run", "m0-core-gate",
             "m0-guest-validation", "evidence-regression", "workflow-command"}
    kind = data.get("report_kind")
    if data.get("schema_version") != 1 or kind not in known:
        raise ValueError("Unsupported report kind or schema version")
    summary = {"kind": kind, "source_status": data.get("status"), "ci": data.get("ci"),
               "case_count": len(data.get("cases", []))}
    if kind == "m0-core-gate":
        cases = data["core"]["case_index"]
        if not isinstance(cases, list):
            raise ValueError("Core case_index must be an array")
        summary.update(source_status=data["core"]["status"], case_count=len(cases),
                       evidence_items=len(data["evidence_index"]))
    elif kind == "m0-guest-validation":
        summary["source_status"] = data["guest_validation"]["status"]
    elif kind == "svc-job-run":
        summary["cleanup"] = data.get("cleanup")
        summary["supervisors"] = {}
        for label, supervisor in data["supervisors"].items():
            facts = supervisor["facts"]
            summary["supervisors"][label] = {"cleanup": facts["cleanup"], "errors": facts.get("errors", [])}
    elif kind == "svc-scm-run":
        summary["cleanup"] = data["cleanup"]
        summary["callbacks"] = data["callback_result"]["actual"]
    if "artifacts" in data:
        summary["artifacts"] = [{key: artifact.get(key) for key in ("name", "sha256", "size_bytes")}
                                for artifact in data["artifacts"]]
    return {"status": "pass", "scope": "report-summary", "evidence_verified": False,
            "input_report": identity(path), "summary": summary}


def runs(path, commit):
    if not re.fullmatch(r"[0-9a-f]{40}", commit):
        raise ValueError("Run overview requires an exact commit SHA")
    response = read_json(path)
    if not isinstance(response.get("workflow_runs"), list):
        raise ValueError("Expected a workflow_runs API response")
    selected, seen = [], set()
    for run in response["workflow_runs"]:
        if run.get("head_sha") != commit:
            continue
        if run["id"] in seen:
            raise ValueError("Duplicate workflow run identity")
        seen.add(run["id"])
        selected.append({key: run.get(key) for key in ("id", "name", "head_sha", "status", "conclusion", "html_url")})
    if not selected:
        raise ValueError("No workflow runs for the selected exact commit")
    return {"status": "pass", "scope": "ci-run-overview", "commit": commit, "runs": selected,
            "all_success": all(r["status"] == "completed" and r["conclusion"] == "success" for r in selected),
            "input_response": identity(path), "network_requests": 0}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="operation", required=True)
    for name in ("fetch", "verify"):
        child = commands.add_parser(name)
        child.add_argument("--repo", type=Path, default=ROOT)
        child.add_argument("--manifest", type=Path, required=True)
        child.add_argument("--output-dir", type=Path, required=True)
        child.add_argument("--report", type=Path, required=True)
        child.add_argument("--max-unpacked-bytes", type=int, default=512 * 1024**2)
    overview = commands.add_parser("summarize")
    overview.add_argument("--input-report", type=Path, required=True)
    overview.add_argument("--report", type=Path, required=True)
    run_list = commands.add_parser("runs", help="Filter a retained API response; never repoll a completed run")
    run_list.add_argument("--input-response", type=Path, required=True)
    run_list.add_argument("--commit", required=True)
    run_list.add_argument("--report", type=Path, required=True)
    args = parser.parse_args()
    started = now()
    collision = False
    try:
        guard_report_target(args)
        if args.operation == "summarize":
            result = summarize(args.input_report)
        elif args.operation == "runs":
            result = runs(args.input_response, args.commit)
        else:
            result = collect(args)
    except ReportCollision as error:
        collision = True
        result = {"status": "fail", "reason": str(error)}
    except (OSError, ValueError, KeyError, TypeError, zipfile.BadZipFile) as error:
        result = {"status": "fail", "reason": str(error)[:500]}
    result.update(schema_version=1, report_kind="ci-evidence", started_at=started, finished_at=now())
    if not collision:
        write_json(args.report, result)
    summary = result.get("summary", {})
    print(json.dumps({"status": result["status"], "report": None if collision else str(args.report),
                      "artifacts": len(result.get("artifacts", [])), "runs": len(result.get("runs", [])),
                      "summary": {key: summary[key] for key in ("kind", "source_status", "case_count", "evidence_items") if key in summary},
                      "reason": result.get("reason", "")[:300]}))
    return 0 if result["status"] == "pass" else 1


if __name__ == "__main__":
    raise SystemExit(main())
