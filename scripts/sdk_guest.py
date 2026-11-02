#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Export current supplied SDK contracts and consume locked full-system evidence."""
import argparse
import json
from pathlib import Path, PurePosixPath
import re
import sys
import uuid
import zipfile

from build_ape import BuildError, headers, identity
from core_gate import Evidence, interval, require, same, unique, verify_guest_transport
from evidence import read_json
from foundation import ROOT, now
from input_lock import consume_identity, check_portable_helper
from nightly_toolchain import write_json
import sdk
from startup import ci_facts, check_ci_commit
from workflow_support import guard_evidence_outputs


def catalog(page):
    """One required public process catalog; no optional guest cases."""
    result = []
    loader, modules = "/payload/ape-aarch64.elf", "/payload/sdk/modules"
    for suffix, arguments, expected in sdk.HOST_CASES:
        if suffix == "facts":
            expected = json.dumps({"probe": "qaplet-host", "arch": "aarch64", "page_size": page}, separators=(",", ":")) + "\n"
        result.append({"case_id": "qaplet-host." + suffix,
            "argv": [loader, "/payload/qaplet-host.com", *[value.replace("{modules}", modules) for value in arguments]],
            "expected_exit_code": 0, "expected_stdout": expected, "expected_stderr": ""})
    for name, cases in (("qaplet", sdk.CLI_CASES), ("qaplet-example", sdk.EXAMPLE_CASES)):
        for suffix, arguments, code, stdout, stderr in cases:
            result.append({"case_id": name + "." + suffix,
                "argv": [loader, "/payload/" + name + ".com", *[value.replace("{modules}", modules) for value in arguments]],
                "expected_exit_code": code, "expected_stdout": stdout,
                **({"stderr_contains": stderr} if stderr else {"expected_stderr": ""})})
    return result


def producer(evidence, reference, distribution, report, expected_commit=None):
    item = evidence.report(reference, "qaplet-sdk", ("build",))
    locked = consume_identity(item, report)
    check_portable_helper(item["portable_helpers"], locked)
    require(item["sdk_recipe"]["files"] == sdk.recipe(), "Consumer recipe differs from selected SDK producer")
    revision = item["sdk_recipe"]["git_commit"]
    require(re.fullmatch(r"[0-9a-f]{40}", revision or ""), "SDK producer requires exact Qaplet source commit")
    if expected_commit:
        require(revision == expected_commit, "SDK producer Qaplet source commit differs from required commit")
    artifacts = unique(item["artifacts"], "name")
    require(set(artifacts) == {"qaplet-host", "qaplet", "qaplet-example"} and
            all(row["audit"]["status"] == "pass" for row in artifacts.values()), "Three audited SDK executables required")
    components = unique(item["distribution"], "file")
    require(components, "Complete SDK distribution required")
    for name, entry in components.items():
        path = PurePosixPath(name)
        require(not path.is_absolute() and ".." not in path.parts and "\\" not in name and ":" not in name,
                "Unsafe SDK distribution path")
        evidence.read({**entry, "path": str(distribution / name)})
    sdk.check_package(item, distribution)
    for name, entry in artifacts.items():
        require(name + ".com" in components and same(entry, components[name + ".com"]), "SDK executable differs from distribution")
        path = distribution / (name + ".com")
        headers(path, [path.with_suffix(".com.dbg"), path.with_suffix(".aarch64.elf")])
    require(same(components["ape-aarch64.elf"], item["portable_helpers"]["loader"]), "SDK guest loader differs from producer")
    require(item["sdk"]["recipe_id"] == item["sdk_recipe"]["sha256"], "SDK recipe aggregate mismatch")
    return item


def payload(item, distribution, page):
    names = ["ape-aarch64.elf", "qaplet-host.com", "qaplet.com", "qaplet-example.com"]
    names += [entry["file"] for entry in item["sdk"]["components"] if entry["file"].startswith("sdk/modules/")]
    delivered = unique(item["distribution"], "file")
    return {"schema_version": 1, "page_size": page, "files": [
        {"guest_path": "/payload/" + name, "path": str(distribution / name),
         "sha256": delivered[name]["sha256"], "size_bytes": delivered[name]["size_bytes"]} for name in names],
        "commands": catalog(page)}


def export(args, report):
    require(args.build_report and args.distribution and args.manifest and args.selection,
            "export requires --build-report, --distribution, --manifest and --selection")
    paths = [args.report, args.manifest, args.selection, args.build_report]
    require(len({path.resolve() for path in paths}) == len(paths), "Export outputs must be distinct from inputs")
    for path in (args.manifest, args.selection):
        require(not path.exists(), "Export preserves existing selections; use a new output path")
    evidence = Evidence({}, ROOT)
    supplied = args.distribution.resolve()
    item = producer(evidence, str(args.build_report.resolve()), supplied, report, args.expected_commit)
    check_ci_commit(item, report)
    data = payload(item, supplied, args.page_size)
    write_json(args.manifest, data)
    selection = {"schema_version": 1, "report_kind": "qaplet-sdk-guest-selection", "page_size": args.page_size,
        "producer_report": identity(args.build_report), "distribution_root": str(supplied),
        "input_lock": item["input_lock"], "sdk_sha256": item["sdk"]["sha256"],
        "sdk_recipe_sha256": item["sdk_recipe"]["sha256"], "qaplet_commit": item["sdk_recipe"]["git_commit"],
        "package": item["package"], "manifest": identity(args.manifest)}
    write_json(args.selection, selection)
    report.update(status="pass", reason="Verified supplied SDK payload exported; guest execution remains required",
                  selection=identity(args.selection), manifest=identity(args.manifest), command_count=len(data["commands"]),
                  evidence_verified=False)


def consume(manifest, base, report, expected_commit=None, evidence=None, verified_producer=None):
    """Read-only semantic import; also reused by the final M1 evidence gate."""
    required = {"schema_version", "profile", "selection", "guest_report"}
    require(required <= set(manifest) <= required | {"transports", "path_mappings"}, "SDK guest selections have missing/unknown fields")
    require(manifest["schema_version"] == 1 and manifest["profile"] == "sdk-guest", "Versioned SDK guest profile required")
    evidence = evidence if evidence is not None else Evidence(manifest, base)
    try:
        selected = evidence.json(manifest["selection"])
        require(selected["schema_version"] == 1 and selected["report_kind"] == "qaplet-sdk-guest-selection", "Versioned SDK guest selection required")
        supplied = evidence.resolve(selected["distribution_root"])
        if verified_producer is None:
            item = producer(evidence, selected["producer_report"], supplied, report, expected_commit)
        else:
            item, reference, distribution = verified_producer
            evidence.read(selected["producer_report"])
            require(evidence.resolve(selected["producer_report"]["path"]) == evidence.resolve(reference) and
                    supplied == distribution, "SDK guest selected a different final producer/distribution")
            require(item["sdk_recipe"]["git_commit"] == expected_commit, "SDK guest final product commit differs")
        require(selected["input_lock"] == item["input_lock"] and selected["sdk_sha256"] == item["sdk"]["sha256"] and
                selected["sdk_recipe_sha256"] == item["sdk_recipe"]["sha256"] and selected["qaplet_commit"] == item["sdk_recipe"]["git_commit"] and
                selected["package"] == item["package"], "SDK guest selection differs from selected producer identities")
        page = selected["page_size"]
        require(type(page) is int and page in (4096, 65536), "SDK guest selection requires actual ARM64 page target")
        original_payload = evidence.json(selected["manifest"])
        expected_payload = payload(item, supplied, page)
        # Original paths remain immutable when evidence moves; compare resolved bytes.
        require(original_payload["schema_version"] == 1 and original_payload["page_size"] == page and
                original_payload["commands"] == expected_payload["commands"], "SDK guest exported invocation/expected catalog mismatch")
        expected_files = unique(expected_payload["files"], "guest_path")
        original_files = unique(original_payload["files"], "guest_path")
        require(set(original_files) == set(expected_files), "SDK guest exported payload inventory mismatch")
        for name, entry in original_files.items():
            require(same(entry, expected_files[name]), "SDK guest exported payload differs from producer")
            evidence.read(entry)
            require(evidence.resolve(entry["path"]) == Path(expected_files[name]["path"]), "SDK guest payload path differs from selected distribution")
        guest = evidence.report(manifest["guest_report"], "arm64-guest")
        check_ci_commit(item, guest)
        require(same(guest["source"]["runner"], identity(ROOT / "scripts/arm64_guest.py")), "SDK guest runner source differs from current protocol")
        files, cases = verify_guest_transport(evidence, guest, report)
        require(guest["guest"]["page_size"] == page, "SDK guest measured page differs from selected target")
        executed_payload = evidence.json(guest["manifest"])
        require(same(guest["manifest"], selected["manifest"]) or selected["manifest"] in executed_payload.get("input_manifests", []),
                "SDK guest did not execute the selected exported manifest")
        require(executed_payload["page_size"] == page, "SDK guest executed manifest page mismatch")
        all_commands = unique(executed_payload["commands"])
        require(set(all_commands) == set(cases), "SDK guest executed manifest differs from serial case coverage")
        require(unique(executed_payload["files"], "guest_path") == files, "SDK guest executed manifest differs from reported payload")
        for name, entry in expected_files.items():
            require(name in files and same(files[name], entry), "SDK guest lacks selected SDK/loader/module bytes")
            evidence.read(files[name])
        required_cases = unique(catalog(page))
        require({name for name in cases if name.startswith(("qaplet-host.", "qaplet.", "qaplet-example."))} == set(required_cases),
                "SDK guest required case catalog is incomplete or has unknown SDK cases")
        selected_cases = []
        for name, expected in required_cases.items():
            case = cases[name]
            require(all_commands[name] == expected and case["argv"] == expected["argv"] and case["cwd"] == "/",
                    "SDK guest actual invocation differs from required public contract: " + name)
            require(case["expected"] == {key: value for key, value in expected.items() if key.startswith("expected_") or key == "stderr_contains"},
                    "SDK guest expected contract differs from authoritative catalog: " + name)
            require(case["exit_code"] == expected["expected_exit_code"] and case["exit_code"] != 137,
                    "SDK guest process exit failed public contract: " + name)
            stdout, stderr = (evidence.read(case[field + "_log"]) for field in ("stdout", "stderr"))
            require(stdout == expected["expected_stdout"].encode("utf-8") and
                    (expected["stderr_contains"].encode("utf-8") in stderr if "stderr_contains" in expected else stderr == expected["expected_stderr"].encode("utf-8")),
                    "SDK guest raw output failed public contract: " + name)
            start, end = interval(case)
            require((end - start).total_seconds() <= case["timeout_seconds"], "SDK guest command exceeded deadline: " + name)
            if name in ("qaplet.eval-unsettled", "qaplet.run-unsettled"):
                require((end - start).total_seconds() < 2, "SDK guest unsettled CLI failed immediate-exit contract")
            selected_cases.append({"case_id": name, "status": "pass", "required": True,
                "artifact_sha256": expected_files[expected["argv"][1]]["sha256"], "sdk_sha256": item["sdk"]["sha256"],
                "started_at": case["started_at"], "finished_at": case["finished_at"], "timeout_seconds": case["timeout_seconds"]})
        report.update(status="pass", reason="Actual supplied SDK public contracts verified against locked ARM64 kernel and raw serial evidence",
            selection=manifest["selection"], producer_report=selected["producer_report"], guest_report=manifest["guest_report"],
            sdk_sha256=item["sdk"]["sha256"], sdk_recipe_sha256=item["sdk_recipe"]["sha256"], qaplet_commit=item["sdk_recipe"]["git_commit"],
            package=item["package"], page_size=page, execution_mode="emulated-system", guest=guest["guest"], cases=selected_cases,
            required_cases=list(required_cases), evidence_verified=True,
            scope_notes="ARM64 kernel supplementation only; native Cross, full M1/Core/offline and release gates remain independent")
    finally:
        report["evidence_index"] = list(evidence.index.values())
        report["evaluator_sources"] = [identity(ROOT / "scripts" / name) for name in ("sdk_guest.py", "core_gate.py", "arm64_guest.py", "sdk.py")]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("operation", choices=("export", "import", "verify"))
    for name in ("build-report", "distribution", "manifest", "selection", "input-manifest", "guest-report"):
        parser.add_argument("--" + name, type=Path)
    parser.add_argument("--page-size", type=int, choices=(4096, 65536), default=65536)
    parser.add_argument("--expected-commit")
    parser.add_argument("--report", type=Path, required=True)
    args = parser.parse_args()
    try:
        guard_evidence_outputs([args.report, *([args.manifest, args.selection] if args.operation == "export" else [])],
            [args.build_report, args.input_manifest, args.guest_report, *([args.selection] if args.operation != "export" else [])],
            [args.distribution] if args.distribution else [])
    except ValueError as error:
        parser.error(str(error))
    protected = [path.resolve() for path in (args.build_report, args.input_manifest, args.guest_report) if path]
    if args.operation != "export" and args.selection:
        protected.append(args.selection.resolve())
    if args.report.resolve() in protected or args.distribution and args.report.resolve().is_relative_to(args.distribution.resolve()):
        parser.error("Report may not replace selected immutable inputs or supplied distribution bytes")
    if args.distribution and any(path and path.resolve().is_relative_to(args.distribution.resolve()) for path in (args.manifest, args.selection)):
        parser.error("Export selections may not modify the supplied distribution")
    report = {"schema_version": 1, "report_kind": "qaplet-sdk-guest", "scope": args.operation,
              "report_id": str(uuid.uuid4()), "started_at": now(), "status": "not_run", "ci": ci_facts(),
              "evidence_verified": False, "release_ready": False}
    try:
        if args.operation == "export":
            export(args, report)
        else:
            require(args.input_manifest or args.selection and args.guest_report,
                    "import/verify requires --input-manifest or --selection and --guest-report")
            manifest = read_json(args.input_manifest) if args.input_manifest else {
                "schema_version": 1, "profile": "sdk-guest", "selection": str(args.selection.resolve()), "guest_report": str(args.guest_report.resolve())}
            consume(manifest, args.input_manifest.resolve().parent if args.input_manifest else ROOT, report, args.expected_commit)
    except BuildError as error:
        report.update(status=error.status, reason=str(error))
    except (OSError, ValueError, KeyError, TypeError, zipfile.BadZipFile) as error:
        report.update(status="fail", reason=str(error))
    report["finished_at"] = now()
    if any(args.report.resolve() == Path(entry["resolved_path"]) for entry in report.get("evidence_index", [])):
        print(json.dumps({"status": "fail", "reason": "Report may not replace selected immutable evidence", "evidence_verified": False}))
        return 1
    write_json(args.report, report)
    print(json.dumps({key: report[key] for key in ("status", "reason", "scope", "evidence_verified", "release_ready")}))
    return {"pass": 0, "fail": 1, "blocked": 2}[report["status"]]


if __name__ == "__main__":
    sys.exit(main())
