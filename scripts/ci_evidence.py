#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Acquire or verify explicit CI archives and emit small, typed report summaries."""
import argparse
from collections import Counter
from datetime import datetime, timezone
import hashlib
import ipaddress
import json
import os
from pathlib import Path, PurePosixPath
import re
import shutil
import stat
import time
import urllib.parse
import urllib.request
import urllib.error
import uuid
import zipfile

from workflow_support import identity, local_path, now, read_json, workspace, write_json

ROOT = Path(__file__).resolve().parents[1]


def guard_report_target(args):
    target = args.report.resolve()
    if args.operation == "efficiency-actions":
        if target.exists() or target.name in ("run.json", "jobs.json") or not args.output_dir.resolve().is_relative_to(ROOT / "out") or not target.is_relative_to(ROOT / "out"):
            raise ValueError("Use a new efficiency report/output under checkout out/")
        return
    if args.operation == "reuse" and target.exists():
        raise ValueError("Use a new reuse report; original evidence is immutable")
    if args.operation == "fetch-actions" and target.exists():
        raise ValueError("A NEW collection report is required; prior attempts are retained")
    if args.operation in ("summarize", "runs", "efficiency", "reuse"):
        source = args.input_report if args.operation in ("summarize", "reuse") else args.input_response
        sources = [source, *getattr(args, "jobs_response", [])]
        if any(target == path.resolve() for path in sources):
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


def fresh_reference(endpoint, timeout=45):
    headers = {"Accept": "application/vnd.github+json", "X-GitHub-Api-Version": "2022-11-28"}
    # Only the fixed API host receives an existing token. Redirect download is unauthenticated.
    token = os.environ.get("GITHUB_TOKEN") if endpoint.startswith("https://api.github.com/") else None
    if token:
        if "\n" in token or "\r" in token:
            raise ValueError("Invalid provider read token")
        headers["Authorization"] = "Bearer " + token
    request = urllib.request.Request(endpoint, headers=headers)
    try:
        with urllib.request.build_opener(NoReferenceRedirect()).open(request, timeout=timeout):
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


def request_timeout(deadline, maximum):
    if deadline is None:
        return maximum
    remaining = deadline - time.monotonic()
    if remaining <= 0:
        raise ValueError("Successful-result discovery budget exhausted; execute fresh")
    return min(maximum, remaining)


def acquire(item, cache, manifest, allow_fetch, attempts=None, deadline=None):
    expected = item["archive"]
    # Provider IDs distinguish repeated artifacts with the same name/SHA. Older
    # receipts still point to their immutable original file and remain valid.
    target = cache / ((str(item["id"]) + "-" if expected.get("reference") else "") + item["name"] + ".zip")
    if expected.get("path"):
        retained = local_path(expected["path"], manifest.resolve().parent)
        if retained.resolve().is_relative_to(cache.resolve()):
            target = retained
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
                    fresh = fresh_reference(reference, request_timeout(deadline, 10 if deadline else 45)) if reference else check_url(url)
                    try:
                        opener = urllib.request.build_opener(CheckedRedirect())
                        with opener.open(fresh, timeout=request_timeout(deadline, 10 if deadline else 45)) as incoming, temporary.open("xb") as output:
                            total = 0
                            while block := incoming.read(1024 * 1024):
                                request_timeout(deadline, 45)
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
        archive = acquire(item, cache, args.manifest, args.operation in ("fetch", "fetch-actions"), getattr(args, "attempts", None), getattr(args, "deadline", None))
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


FAILURE_CLASSES = ("Product Regression", "Platform ABI", "Build / Toolchain", "Environment / Runner",
                   "Missing Resource", "Evidence / Aggregation", "Cancelled / Superseded", "Unknown")


def reuse(args):
    """Replay current local contracts and original bytes, without new OS execution."""
    import local_check
    import sdk
    from core_gate import require
    from sdk_matrix import catalog

    mappings = getattr(args, "path_mappings", [])
    def resolve(path):
        normalized = str(path).replace("\\", "/")
        matches = [(source, target) for source, target in mappings
                   if normalized == source or normalized.startswith(source + "/")]
        if matches:
            source, target = max(matches, key=lambda row: len(row[0]))
            result = (Path(target) / normalized[len(source):].lstrip("/")).resolve()
            require(result.is_relative_to(Path(target).resolve()), "Reuse mapping escaped archive root")
            return result
        return Path(path)

    def verify_ref(ref):
        path = resolve(ref["path"])
        observed = identity(path)
        require(all(observed[key] == ref[key] for key in ("sha256", "size_bytes")), "Original evidence bytes changed")
        return path

    value = read_json(args.input_report)
    require(value.get("report_kind") == "qaplet-local-check" and value.get("status") == "pass" and
            value.get("execution_status") == "executed" and value.get("scope") == "development-only",
            "Only a successful actual local execution can be reused")
    require(value.get("commit") == args.commit and re.fullmatch(r"[0-9a-f]{40}", args.commit), "Reuse requires the same exact commit")
    require(value.get("profile") == args.profile and args.profile != "ci", "Reuse requires the same product contract profile")
    require(value.get("contract_sha256") == local_check.contract_identity(args.profile)["sha256"], "Test contract changed")
    expected = local_check.current_recipe()
    require(value.get("current_recipe_sha256") == expected["sha256"], "Product recipe changed")
    elapsed = (datetime.now(timezone.utc) - datetime.fromisoformat(value["finished_at"].replace("Z", "+00:00"))).total_seconds()
    require(0 <= elapsed <= args.max_age_hours * 3600, "Successful execution is stale or in the future")
    supplier = value["supplier"]
    reference = supplier["producer_report"]
    producer_path = verify_ref(reference)
    artifacts = args.supplier_dir or resolve(supplier["artifacts_directory"])
    producer = local_check.validate_supplier(producer_path, artifacts.resolve(), expected)
    checked, original_reports = {}, []
    for key in ("runtime", "public_c"):
        section = value[key]
        if key == "public_c" and section["status"] == "not_applicable":
            continue
        require(section["status"] == "pass", "Selected execution section failed")
        ref = section["report"]
        incoming = read_json(verify_ref(ref))
        require(incoming["status"] == "pass", "Original execution failed")
        if key == "public_c":
            require(incoming.get("report_kind") == "qaplet-local-c-compile" and
                    incoming.get("scope") == "public-header-compile" and incoming.get("commands") and
                    incoming["inputs_before"] == incoming["inputs_after"], "Actual stable public C compilation required")
            for item in [*incoming["inputs_after"], *incoming["compiler"], incoming["object"], *incoming["dependency_files"]]:
                observed = identity(resolve(item["path"]))
                require(all(observed[field] == item[field] for field in ("sha256", "size_bytes")), "Public C input/compiler/object bytes changed")
                checked[item["path"]] = observed
            require(incoming["object"].get("elf_class") == 64 and incoming["object"].get("elf_type") == "REL" and
                    incoming["object"].get("machine") in (62, 183), "Measured ordinary C object required")
            observed_object = local_check.verify_object(resolve(incoming["object"]["path"]), incoming["host"]["execution_arch"])
            require(all(observed_object[key] == incoming["object"][key] for key in ("sha256", "size_bytes", "elf_class", "elf_type", "machine")), "Ordinary C object format differs from its measured record")
            for command in incoming["commands"]:
                require(command["exit_code"] == 0 and not command["timed_out"], "Public C compile command failed")
                for stream in ("stdout", "stderr"):
                    checked[command[stream + "_log"]] = identity(resolve(command[stream + "_log"]))
            original_reports.append(ref)
            continue
        if key == "runtime":
            host = incoming["host"]
            require((host["os"], host["execution_arch"]) == (args.expected_os, args.expected_arch) and
                    host["execution_mode"] == "native" and host.get("cpu", host["execution_arch"]) == host["execution_arch"],
                    "Original execution does not cover the requested native platform")
            require(host == value["runtime"]["host"] and incoming.get("ci") == value.get("ci"), "Original execution host/CI identity differs")
        with local_check.selected_catalog(local_check.select_cases(args.profile)):
            wanted = {row[0]: row for row in catalog(value["runtime"]["host"])}
        require(len(incoming["cases"]) == len(wanted) and {case["case_id"] for case in incoming["cases"]} == set(wanted), "Incomplete selected test catalog")
        for case in incoming["cases"]:
            _, _, _, code, output, error, contains = wanted[case["case_id"]]
            require(case["status"] == "pass" and len(case["commands"]) == 1, "Nonpassing/missing actual test command")
            command = case["commands"][0]
            raw = []
            for stream in ("stdout", "stderr"):
                log = resolve(command[stream + "_log"])
                checked[str(log)] = identity(log)
                raw.append(log.read_bytes())
            require(not command["timed_out"] and command["exit_code"] == code and
                    raw[0] == output.encode("utf-8") and (error.encode("utf-8") in raw[1] if contains and error else raw[1] == error.encode("utf-8")),
                    "Original raw process result contradicts the current test contract")
            artifact = case["artifact"]
            supplied = [row for row in producer["artifacts"] if row["name"] == case["case_id"].split(".", 1)[0]]
            require(len(supplied) == 1 and all(artifact[field] == supplied[0][field] for field in ("sha256", "size_bytes")),
                    "Executed artifact differs from the verified supplier")
            observed = identity(resolve(artifact["path"]))
            require(all(observed[field] == artifact[field] for field in ("sha256", "size_bytes")), "Executed artifact changed")
        original_reports.append(ref)
    return {"status": "pass", "scope": "verified-local-result-reuse", "execution_status": "reused_previous",
        "commit": args.commit, "profile": args.profile, "recipe_sha256": expected["sha256"],
        "contract_sha256": value["contract_sha256"], "original_execution": identity(args.input_report),
        "original_ci": value.get("ci"), "original_host": value["runtime"]["host"], "original_finished_at": value["finished_at"],
        "original_reports": original_reports, "original_supplier": supplier, "checked_logs": list(checked.values()),
        "evidence_verified": False, "cross_verified": False, "full_verified": False, "release_ready": False,
        "network_requests": 0, "new_product_executions": 0, "reason": "Verified reuse of the same development contract; no new platform/CI or release pass"}


def actions_reuse_candidates(args, directory):
    """Bounded read-only discovery; original provider/ZIP/report bytes stay intact."""
    from types import SimpleNamespace
    repository = args.reuse_repository or os.environ.get("GITHUB_REPOSITORY", "")
    base = args.reuse_api_base.rstrip("/")
    if not re.fullmatch(r"[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+", repository):
        raise ValueError("Actions reuse requires an exact repository")
    if base != "https://api.github.com" and not re.fullmatch(r"http://127\.0\.0\.1:[0-9]+", base):
        raise ValueError("Reuse API must be GitHub or explicit unauthenticated literal loopback")
    directory.mkdir(parents=True, exist_ok=False)
    deadline = time.monotonic() + args.reuse_timeout_seconds

    def metadata(suffix, target):
        headers = {"Accept": "application/vnd.github+json", "User-Agent": "qaplet-ci-reuse"}
        token = os.environ.get("GITHUB_TOKEN") if base == "https://api.github.com" else None
        if token:
            if "\n" in token or "\r" in token:
                raise ValueError("Invalid provider read token")
            headers["Authorization"] = "Bearer " + token
        request = urllib.request.Request(base + "/repos/" + repository + "/actions/" + suffix, headers=headers)
        try:
            with urllib.request.build_opener(NoReferenceRedirect()).open(request, timeout=request_timeout(deadline, 5)) as response:
                data = response.read(4 * 1024 * 1024 + 1)
        except urllib.error.HTTPError as error:
            raise ValueError("Actions reuse metadata failed: HTTP " + str(error.code)) from None
        if len(data) > 4 * 1024 * 1024:
            raise ValueError("Actions reuse metadata exceeds bounded response")
        target.write_bytes(data)
        return read_json(target)

    response = directory / "runs.json"
    metadata("workflows/m0.yml/runs?head_sha=" + args.commit + "&status=success&per_page=10", response)
    selected_runs = runs(response, args.commit)["runs"] if read_json(response).get("workflow_runs") else []
    # A rerun changes current-run status to in_progress. Its last successful
    # attempt is still eligible, with its original attempt identity preserved.
    attempt, current = os.environ.get("GITHUB_RUN_ATTEMPT", "1"), os.environ.get("GITHUB_RUN_ID", "")
    if attempt.isdigit() and int(attempt) > 1 and current.isdigit():
        previous = metadata("runs/" + current + "/attempts/" + str(int(attempt) - 1), directory / "previous-attempt.json")
        if previous.get("head_sha") == args.commit:
            selected_runs.insert(0, previous)
    for run in selected_runs[:3]:
        request_timeout(deadline, 5)
        if run.get("status") != "completed" or run.get("conclusion") != "success":
            continue
        run_id = run["id"]
        if type(run_id) is not int or run_id <= 0:
            continue
        work = directory / (str(run_id) + "-" + uuid.uuid4().hex)
        work.mkdir()
        try:
            catalog_path = work / "provider.json"
            provider = metadata("runs/" + str(run_id) + "/artifacts?per_page=100", catalog_path)
            if provider.get("total_count", 0) > 100:
                raise ValueError("Actions artifact catalog exceeds bounded first page")
            matched = [row for row in provider["artifacts"] if row.get("name") == "qaplet-fast-" + args.commit]
            expired = [row for row in matched if row.get("expired") is not False]
            if expired:
                write_json(work / "expired.json", {"status": "fail", "reason": "Provider artifact expired or expiry unknown",
                    "provider_artifact_ids": [row.get("id") for row in expired]})
                for row in expired:
                    args.reuse_attempts.append({"provider_run_id": run_id, "provider_artifact_id": row.get("id"),
                        "status": "fail", "reason": "Provider artifact expired or expiry unknown", "rejection": identity(work / "expired.json")})
            rows = [row for row in matched if row.get("expired") is False]
            if not rows:
                raise ValueError("No nonexpired original Fast artifact available")
            # A rerun can upload several same-name artifacts under one run ID.
            # Each is an independent provider/ZIP candidate, never an alias.
            for index, row in enumerate(rows[:3]):
                request_timeout(deadline, 5)
                candidate = work / ("artifact-" + str(index))
                candidate.mkdir()
                try:
                    options = SimpleNamespace(operation="fetch-actions", repo=ROOT, manifest=catalog_path, repository=repository,
                        commit=args.commit, run_id=run_id, artifact_id=[row["id"]], api_base=base,
                        output_dir=candidate / "collected", max_unpacked_bytes=2 * 1024**3, attempts=[], deadline=deadline)
                    try:
                        collection = collect(options)
                    finally:
                        write_json(candidate / "acquisition-attempts.json", options.attempts)
                    write_json(candidate / "collection.json", collection)
                    # Repeat the same archive and inventory verification completely offline.
                    write_json(candidate / "local-manifest.json", collection["local_manifest"])
                    options.operation, options.manifest = "verify", candidate / "local-manifest.json"
                    write_json(candidate / "verify.json", collect(options))
                    archive = collection["artifacts"][0]
                    shutil.copyfile(archive["archive"]["path"], candidate / "original.zip")
                    root = Path(archive["root"])
                    reports = [root / "out/fast/check.json", root / "fast/check.json", root / "check.json"]
                    reports = [path for path in reports if path.is_file()]
                    if len(reports) != 1:
                        raise ValueError("Original actual Fast check report missing or ambiguous")
                    value = read_json(reports[0])
                    ci = value.get("ci") or {}
                    if str(ci.get("run_id")) != str(run_id) or ci.get("commit") != args.commit:
                        raise ValueError("Original execution CI identity differs from selected provider run")
                    original = value["supplier"]["artifacts_directory"].replace("\\", "/")
                    marker = "/.cache/build/local-check/"
                    if marker not in original:
                        raise ValueError("Original Fast artifact has no unambiguous checkout root")
                    original_root = original.split(marker, 1)[0]
                    # Multi-path upload retains the repository common ancestor. Legacy
                    # flattened artifacts cannot prove all references and fall back.
                    if not (root / ".cache/build/local-check").is_dir():
                        raise ValueError("Original Fast artifact omits supplier bytes")
                    checked = len(args.reuse_attempts)
                    yield reports[0], [(original_root, str(root))], {"run": run, "artifact": row,
                        "collection": identity(candidate / "collection.json"), "archive": identity(candidate / "original.zip")}
                    # The caller resumes discovery after rejecting a candidate's
                    # execution contract. Keep that refusal with this provider ID.
                    for attempt in args.reuse_attempts[checked:]:
                        if attempt.get("status") == "fail":
                            attempt.update(provider_run_id=run_id, provider_artifact_id=row.get("id"))
                            write_json(candidate / "rejected.json", attempt)
                            attempt["rejection"] = identity(candidate / "rejected.json")
                except (OSError, ValueError, KeyError, TypeError, zipfile.BadZipFile) as error:
                    write_json(candidate / "rejected.json", {"status": "fail", "reason": str(error)[:500]})
                    args.reuse_attempts.append({"provider_run_id": run_id, "provider_artifact_id": row.get("id"),
                        "status": "fail", "reason": str(error)[:500], "rejection": identity(candidate / "rejected.json")})
        except (OSError, ValueError, KeyError, TypeError, zipfile.BadZipFile) as error:
            write_json(work / "rejected.json", {"status": "fail", "reason": str(error)[:500]})
            args.reuse_attempts.append({"provider_run_id": run_id, "status": "fail", "reason": str(error)[:500],
                                       "rejection": identity(work / "rejected.json")})


def efficiency(args):
    """Only retained API observations; missing timestamps/cache facts stay null."""
    def instant(value):
        return datetime.fromisoformat(value.replace("Z", "+00:00")) if value else None

    def seconds(start, end):
        a, b = instant(start), instant(end)
        if a is None or b is None:
            return None
        if b < a:
            raise ValueError("Reversed API timing interval")
        return (b - a).total_seconds()

    run = read_json(args.input_response)
    if not re.fullmatch(r"[0-9a-f]{40}", run.get("head_sha", "")) or not run.get("id"):
        raise ValueError("Exact retained Actions run identity required")
    start = run.get("run_started_at") or run.get("created_at")
    rows, seen, ends = [], set(), []
    for source in args.jobs_response:
        for job in read_json(source)["jobs"]:
            if job.get("run_id") != run["id"] or job["id"] in seen:
                raise ValueError("Duplicate job or job from another run")
            seen.add(job["id"])
            inherited = bool(start and job.get("completed_at") and instant(job["completed_at"]) < instant(start))
            actual = bool(job.get("runner_id") and job.get("steps"))
            phases = dict.fromkeys(("build_and_verify", "test", "evidence", "other"), 0)
            steps = []
            for step in job.get("steps", []):
                name = step["name"].lower()
                phase = ("evidence" if any(word in name for word in ("upload", "download", "retain", "collect", "seal", "aggregate")) else
                         "build_and_verify" if any(word in name for word in ("build", "compile", "acquire", "populate", "fetch")) else
                         "test" if any(word in name for word in ("execute", "run ", "test", "contract", "check")) else "other")
                duration = seconds(step.get("started_at"), step.get("completed_at")) if actual else None
                steps.append({"name": step["name"], "phase": phase, "seconds": duration, "conclusion": step.get("conclusion")})
                if duration is not None:
                    phases[phase] += duration
            if not inherited and job.get("completed_at"):
                ends.append(job["completed_at"])
            rows.append({"id": job["id"], "workflow": run.get("name"), "job": job["name"],
                "platform": None, "architecture": None, "runner_name": job.get("runner_name"), "labels": job.get("labels"),
                "conclusion": job.get("conclusion"), "inherited_execution": inherited,
                "execution_seconds": seconds(job.get("started_at"), job.get("completed_at")) if actual else None,
                "queue_seconds": seconds(job.get("created_at"), job.get("started_at")) if actual and not inherited else None,
                "unexecuted_observation_seconds": seconds(job.get("started_at"), job.get("completed_at")) if not actual and not inherited else None,
                **{key + "_seconds": value if actual else None for key, value in phases.items()}, "steps": steps})
    return {"status": "pass", "scope": "ci-efficiency-observation", "evidence_verified": False,
        "commit": run["head_sha"], "tier": args.tier, "run_id": run["id"], "run_attempt": run.get("run_attempt"),
        "workflow": run.get("name"), "run_status": run.get("status"), "conclusion": run.get("conclusion"),
        "workflow_span_seconds": seconds(start, max(ends, key=instant)) if ends else None,
        "api_state_span_seconds": seconds(run.get("created_at"), run.get("updated_at")),
        "runner_seconds": sum(row["execution_seconds"] or 0 for row in rows if not row["inherited_execution"]),
        "queue_seconds": sum(row["queue_seconds"] or 0 for row in rows if not row["inherited_execution"]) if any(
            row["queue_seconds"] is not None and not row["inherited_execution"] for row in rows) else None,
        "cache_hit": None, "verified_artifact_reuse": None,
        "failure_class": None if run.get("conclusion") == "success" else args.failure_class,
        "failure_detail": args.failure_detail, "attribution_source": "explicit input; Unknown without diagnosis",
        "phase_method": "step-name classification; build includes validation, other remains separate; sums are not critical-path subtraction",
        "sources": [identity(path) for path in [args.input_response, *args.jobs_response]], "jobs": rows,
        "network_requests": 0, "p50_seconds": None, "p95_seconds": None}


def efficiency_actions(args):
    """Bounded, read-only current-run metadata; tokens never leave api.github.com."""
    run_id = os.environ.get("GITHUB_RUN_ID", "")
    repository = os.environ.get("GITHUB_REPOSITORY", "")
    if not run_id.isdigit() or not re.fullmatch(r"[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+", repository):
        raise ValueError("Actual Actions run/repository environment required")
    args.output_dir.mkdir(parents=True, exist_ok=False)
    headers = {"Accept": "application/vnd.github+json", "User-Agent": "qaplet-ci-efficiency"}
    if os.environ.get("GITHUB_TOKEN"):
        headers["Authorization"] = "Bearer " + os.environ["GITHUB_TOKEN"]
    for name, suffix in (("run", ""), ("jobs", "/jobs?per_page=100")):
        request = urllib.request.Request("https://api.github.com/repos/" + repository + "/actions/runs/" + run_id + suffix, headers=headers)
        with urllib.request.build_opener(NoReferenceRedirect()).open(request, timeout=30) as response:
            data = response.read(4 * 1024 * 1024 + 1)
        if len(data) > 4 * 1024 * 1024:
            raise ValueError("Efficiency metadata exceeds bounded response")
        (args.output_dir / (name + ".json")).write_bytes(data)
    args.input_response, args.jobs_response = args.output_dir / "run.json", [args.output_dir / "jobs.json"]
    if read_json(args.jobs_response[0]).get("total_count", 0) > 100:
        raise ValueError("Jobs metadata exceeds bounded first page")
    result = efficiency(args)
    result.update(network_requests=2, observation="Current-run snapshot; reporting job/final conclusion may still be unfinished")
    return result


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
    measured = commands.add_parser("efficiency", help="Measure retained run/jobs API JSON; never trigger CI")
    measured.add_argument("--input-response", type=Path, required=True)
    measured.add_argument("--jobs-response", type=Path, action="append", required=True)
    measured.add_argument("--tier", choices=("fast", "cross", "full", "release", "unknown"), required=True)
    measured.add_argument("--failure-class", choices=FAILURE_CLASSES, default="Unknown")
    measured.add_argument("--failure-detail", default="")
    measured.add_argument("--report", type=Path, required=True)
    automatic = commands.add_parser("efficiency-actions", help="Capture bounded current-run API metadata using the existing Actions token")
    automatic.add_argument("--output-dir", type=Path, required=True)
    automatic.add_argument("--report", type=Path, required=True)
    automatic.add_argument("--tier", choices=("fast", "cross", "full", "release", "unknown"), required=True)
    automatic.set_defaults(failure_class="Unknown", failure_detail="")
    reusable = commands.add_parser("reuse", help="Verify an existing successful same-SHA/recipe/contract development result")
    reusable.add_argument("--input-report", type=Path, required=True)
    reusable.add_argument("--commit", required=True)
    reusable.add_argument("--profile", choices=("all", "engine", "loop", "work", "process"), required=True)
    reusable.add_argument("--expected-os", choices=("Linux", "Windows", "Darwin"), required=True)
    reusable.add_argument("--expected-arch", choices=("x86_64", "aarch64"), required=True)
    reusable.add_argument("--supplier-dir", type=Path)
    reusable.add_argument("--max-age-hours", type=int, choices=range(1, 169), default=24)
    reusable.add_argument("--report", type=Path, required=True)
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
        elif args.operation == "efficiency":
            result = efficiency(args)
        elif args.operation == "reuse":
            result = reuse(args)
        elif args.operation == "efficiency-actions":
            result = efficiency_actions(args)
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
