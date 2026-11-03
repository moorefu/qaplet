#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Acquire or verify explicit CI archives and emit small, typed report summaries."""
import argparse
from collections import Counter
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
import urllib.error
import uuid
import zipfile

from workflow_support import identity, local_path, now, read_json, workspace, write_json

ROOT = Path(__file__).resolve().parents[1]


def guard_report_target(args):
    target = args.report.resolve()
    if args.operation == "fetch-actions" and target.exists():
        raise ValueError("A NEW collection report is required; prior attempts are retained")
    if args.operation in ("summarize", "runs"):
        source = args.input_report if args.operation == "summarize" else args.input_response
        if target == source.resolve():
            raise ValueError("Report target collides with its source input")
        return
    if target == args.manifest.resolve() or target.is_relative_to(args.output_dir.resolve()):
        raise ValueError("Report target collides with input or extracted evidence; use a separate report file")
    ctx = workspace(args.repo)
    if not target.is_relative_to((Path(ctx["checkout"]) / "out").resolve()):
        raise ValueError("Collection reports must be written below checkout out/, outside the extraction directory")
    for item in selected(args):
        source = item["archive"].get("path")
        if source and local_path(source, args.manifest.resolve().parent) == target:
            raise ValueError("Report target collides with an original archive")


def selection(path, data=None):
    data = read_json(path) if data is None else data
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


def selected(args):
    if args.operation != "fetch-actions":
        return selection(args.manifest)
    if not re.fullmatch(r"[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+", args.repository) or not re.fullmatch(r"[0-9a-f]{40}", args.commit):
        raise ValueError("Exact repository and commit required")
    base = args.api_base.rstrip("/")
    check_url(base)
    host = urllib.parse.urlparse(base)
    if base != "https://api.github.com" and not (
            host.scheme == "http" and host.hostname in ("127.0.0.1", "::1") and host.path == ""):
        raise ValueError("Reference API must be GitHub or an explicit unauthenticated loopback test server")
    rows = read_json(args.manifest)["artifacts"]
    if args.artifact_id:
        rows = [r for r in rows if r["id"] in args.artifact_id]
        if {r["id"] for r in rows} != set(args.artifact_id):
            raise ValueError("Selected provider artifact ID missing")
    result = []
    for row in rows:
        if (row["expired"] is not False or row["workflow_run"]["head_sha"] != args.commit or
                str(row["workflow_run"]["id"]) != str(args.run_id) or not row["name"].endswith("-" + args.commit) or
                not re.fullmatch(r"sha256:[0-9a-f]{64}", row.get("digest", ""))):
            raise ValueError("Provider run/commit/digest/expiry mismatch")
        result.append({"id": row["id"], "name": row["name"], "archive": {
            "sha256": row["digest"].removeprefix("sha256:"), "size_bytes": row["size_in_bytes"],
            "reference": base + "/repos/" + args.repository + "/actions/artifacts/" + str(row["id"]) + "/zip"}})
    return selection(args.manifest, {"schema_version": 1, "artifacts": result})


class NoReferenceRedirect(urllib.request.HTTPRedirectHandler):
    def redirect_request(self, *_):
        return None


def fresh_reference(endpoint):
    headers = {"Accept": "application/vnd.github+json", "X-GitHub-Api-Version": "2022-11-28"}
    # Only the fixed API host receives an existing token. Redirect download is unauthenticated.
    token = os.environ.get("GITHUB_TOKEN") if endpoint.startswith("https://api.github.com/") else None
    if token:
        if "\n" in token or "\r" in token:
            raise ValueError("Invalid provider read token")
        headers["Authorization"] = "Bearer " + token
    request = urllib.request.Request(endpoint, headers=headers)
    try:
        with urllib.request.build_opener(NoReferenceRedirect()).open(request, timeout=45):
            raise ValueError("Provider did not return a download reference")
    except urllib.error.HTTPError as redirect:
        if redirect.code not in (301, 302, 303, 307, 308):
            raise ValueError("Provider reference request failed: HTTP " + str(redirect.code)) from None
        return check_url(redirect.headers["Location"])


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


def acquire(item, cache, manifest, allow_fetch, attempts=None):
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
    reference = expected.get("reference")
    if sum(bool(v) for v in (source, url, reference)) != 1:
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
            try:
                for attempt in range(1, 3 if reference else 2):
                    # Resolve each reference immediately before its download, in this process.
                    fresh = fresh_reference(reference) if reference else check_url(url)
                    try:
                        opener = urllib.request.build_opener(CheckedRedirect())
                        with opener.open(fresh, timeout=45) as incoming, temporary.open("xb") as output:
                            total = 0
                            while block := incoming.read(1024 * 1024):
                                total += len(block)
                                if total > expected["size_bytes"]:
                                    raise ValueError("Downloaded archive exceeds declared length")
                                output.write(block)
                        if attempts is not None:
                            attempts.append({"artifact_id": item["id"], "attempt": attempt, "status": "downloaded"})
                        break
                    except urllib.error.HTTPError as error:
                        if attempts is not None:
                            attempts.append({"artifact_id": item["id"], "attempt": attempt, "status": "fail", "http_status": error.code})
                        if not reference or attempt == 2 or error.code not in (401, 403, 410):
                            raise
                        temporary.unlink(missing_ok=True)
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
    items = selected(args)
    output.mkdir(parents=True, exist_ok=True)
    records = []
    for item in items:
        archive = acquire(item, cache, args.manifest, args.operation in ("fetch", "fetch-actions"), getattr(args, "attempts", None))
        destination = output / item["name"]
        files = unpack(archive, destination, args.max_unpacked_bytes)
        records.append({"id": item["id"], "name": item["name"], "archive": identity(archive),
                        "root": str(destination), "files": files})
    result = {"status": "pass", "scope": "archive-verification", "artifacts": records, "context": ctx}
    if args.operation == "fetch-actions":
        # The durable receipt is itself a URL-free, reusable local fetch/verify manifest.
        result["local_manifest"] = {"schema_version": 1, "artifacts": [
            {k: r[k] for k in ("id", "name", "archive")} for r in records]}
    return result


def summarize(path):
    data = read_json(path)
    known = {"ape-build", "ape-startup", "ape-page-plan", "ape-loaders", "native-io", "pages64k-build",
             "pages64k-validation", "arm64-guest", "arm64-guest-inputs", "offline-integrity",
             "svc-job-build", "svc-job-run", "svc-scm-build", "svc-scm-run", "m0-core-gate",
             "m0-guest-validation", "evidence-regression", "workflow-command", "qaplet-sdk", "qaplet-m1-evidence"}
    kind = data.get("report_kind")
    if data.get("schema_version") != 1 or kind not in known:
        raise ValueError("Unsupported report kind or schema version")
    summary = {"kind": kind, "source_status": data.get("status"), "ci": data.get("ci"),
               "case_count": len(data.get("cases", []))}
    if kind in ("qaplet-sdk", "qaplet-m1-evidence", "evidence-regression"):
        cases = data.get("cases", [])
        summary.update(scope=data.get("scope"), reason=str(data.get("reason", ""))[:300],
                       case_statuses=dict(Counter(c.get("status", "unknown") if c.get("status") in
                           ("pass", "fail", "skip", "blocked", "not_run") else "unknown" for c in cases)),
                       failures=[{"case_id": str(c.get("case_id", ""))[:160],
                                  "status": str(c.get("status", ""))[:20], "reason": str(c.get("reason", ""))[:240]}
                                 for c in cases if c.get("status") not in ("pass", "skip")][:8])
        def field(section, key):
            value = data.get(section)
            return value.get(key) if isinstance(value, dict) else None
        summary["ci"] = {k: str(data["ci"].get(k, ""))[:160] for k in ("run_id", "attempt", "commit", "workflow")} if data.get("ci") else None
        summary["product_commit"] = str(data.get("selected_product_commit") or "")[:40] or None
        summary["sdk_sha256"] = str(field("sdk", "sha256") or field("selected_product", "sdk_sha256") or "")[:64] or None
        summary["recipe_sha256"] = str(field("sdk_recipe", "sha256") or field("selected_product", "sdk_recipe_sha256") or "")[:64] or None
        if isinstance(data.get("source"), dict):
            summary["source_commit"] = str(data["source"].get("commit") or "")[:40] or None
        summary["m1_status"] = field("m1", "status")
        summary["m0_status"] = data.get("m0_core_status")
        for key in ("required_gaps", "optional_gaps"):
            gaps = data.get(key, [])
            summary[key + "_count"] = len(gaps)
            summary[key] = [str(g)[:240] for g in gaps[:8]]
        summary["tests"] = {k: data.get("tests", {}).get(k) for k in
                            ("tests_run", "expected", "run", "failures", "errors", "skips", "required", "completed")
                            if isinstance(data.get("tests", {}).get(k), (int, bool))}
        if isinstance(data.get("tests", {}).get("skipped"), list):
            summary["tests"]["skips"] = len(data["tests"]["skipped"])
        summary["evidence_items"] = len(data.get("evidence_index", []))
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
        summary["artifact_count"] = len(data["artifacts"])
        summary["artifacts"] = [{key: str(artifact.get(key, ""))[:200] for key in ("name", "sha256", "size_bytes")}
                                for artifact in data["artifacts"][:8]]
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
    for name in ("fetch", "verify", "fetch-actions"):
        child = commands.add_parser(name)
        child.add_argument("--repo", type=Path, default=ROOT)
        child.add_argument("--manifest", type=Path, required=True)
        child.add_argument("--output-dir", type=Path, required=True)
        child.add_argument("--report", type=Path, required=True)
        child.add_argument("--max-unpacked-bytes", type=int, default=512 * 1024**2)
        if name == "fetch-actions":
            child.add_argument("--repository", required=True)
            child.add_argument("--commit", required=True)
            child.add_argument("--run-id", type=int, required=True)
            child.add_argument("--artifact-id", type=int, action="append")
            child.add_argument("--api-base", default="https://api.github.com")
    overview = commands.add_parser("summarize")
    overview.add_argument("--input-report", type=Path, required=True)
    overview.add_argument("--report", type=Path, required=True)
    run_list = commands.add_parser("runs", help="Filter a retained API response; never repoll a completed run")
    run_list.add_argument("--input-response", type=Path, required=True)
    run_list.add_argument("--commit", required=True)
    run_list.add_argument("--report", type=Path, required=True)
    args = parser.parse_args()
    started = now()
    target_validated = False
    args.attempts = []
    try:
        guard_report_target(args)
        target_validated = True
        if args.operation == "summarize":
            result = summarize(args.input_report)
        elif args.operation == "runs":
            result = runs(args.input_response, args.commit)
        else:
            result = collect(args)
    except (OSError, ValueError, KeyError, TypeError, zipfile.BadZipFile) as error:
        result = {"status": "fail", "reason": str(error)[:500]}
    result.update(schema_version=1, report_kind="ci-evidence", started_at=started, finished_at=now())
    if args.operation == "fetch-actions":
        result["attempts"] = args.attempts
    if target_validated:
        write_json(args.report, result)
    summary = result.get("summary", {})
    terminal_summary = {key: summary[key] for key in ("kind", "source_status", "case_count", "evidence_items",
                       "product_commit", "sdk_sha256", "recipe_sha256", "tests", "m1_status", "m0_status",
                       "required_gaps_count", "optional_gaps_count", "reason") if key in summary}
    if summary.get("failures"):
        terminal_summary["first_failure"] = summary["failures"][0]
    print(json.dumps({"status": result["status"], "report": str(args.report) if target_validated else None,
                      "artifacts": len(result.get("artifacts", [])), "runs": len(result.get("runs", [])),
                      "summary": terminal_summary,
                      "reason": result.get("reason", "")[:300]}))
    return 0 if result["status"] == "pass" else 1


if __name__ == "__main__":
    raise SystemExit(main())
