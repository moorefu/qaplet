#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Run the same Ubuntu-built Hello/eval bytes and collect actual host evidence."""
import argparse
import json
import os
from pathlib import Path
import stat
import sys
import uuid

from build_ape import BuildError, headers, identity
from evidence import read_json
from foundation import ROOT, command, host_facts, now, sha256
from nightly_toolchain import write_json

NAMES = ("hello", "quickjs-eval")
CASES = (("hello.facts", "hello", [], "facts"),
         ("quickjs-eval.facts", "quickjs-eval", ["--facts"], "facts"),
         ("quickjs-eval.value", "quickjs-eval", [], "value"),
         ("quickjs-eval.exception", "quickjs-eval", ["throw new Error('m0-probe')"], "Error: m0-probe"),
         ("quickjs-eval.syntax-error", "quickjs-eval", ["("], "SyntaxError:"))


def ci_facts():
    if os.environ.get("GITHUB_ACTIONS") != "true":
        return None
    return {key: os.environ.get(value) for key, value in {
        "run_id": "GITHUB_RUN_ID", "run_attempt": "GITHUB_RUN_ATTEMPT", "workflow": "GITHUB_WORKFLOW",
        "commit": "GITHUB_SHA", "runner_name": "RUNNER_NAME", "runner_os": "RUNNER_OS",
        "runner_arch": "RUNNER_ARCH", "image_os": "ImageOS", "image_version": "ImageVersion"}.items()}


def measured_host(args, report, directory):
    host = report["host"]
    host["runner_label"] = args.runner_label
    if host["os"] == "Darwin":
        host["translation"] = {}
        for key in ("sysctl.proc_translated", "hw.optional.arm64"):
            record, output, errors = command(["/usr/sbin/sysctl", "-n", key], directory, key, args.timeout)
            report["commands"].append(record)
            host["translation"][key] = {"exit_code": record["exit_code"], "stdout": output, "stderr": errors}
        if host["translation"]["sysctl.proc_translated"]["stdout"].strip() == "1":
            host["execution_mode"] = "translated"
        if host["translation"]["hw.optional.arm64"]["stdout"].strip() == "1":
            host["cpu"] = "aarch64"
    if args.expected_os and host["os"] != args.expected_os:
        raise BuildError("Measured OS differs from requested host")
    if args.expected_arch and host["execution_arch"] != args.expected_arch:
        raise BuildError("Measured execution architecture differs from requested host")
    if host["cpu"] != host["execution_arch"] or host["execution_mode"] != "native":
        raise BuildError("Native execution required; translated or unknown host architecture")
    if host["page_size"] is None or not host["kernel"]:
        raise BuildError("Kernel and OS API page-size measurement are required", "blocked")


def select_loader(args, report):
    host = report["host"]
    if host["os"] == "Windows":
        report["loader"] = {"status": "not_applicable", "route": "Windows PE CreateProcess"}
        return []
    if not args.loader_dir:
        raise BuildError("Explicit distributed native loader directory required", "blocked")
    manifest = args.loader_manifest or args.loader_dir / "helpers.json"
    if manifest.is_file():
        helpers = read_json(manifest)
        if helpers.get("schema_version") != 1 or helpers.get("report_kind") != "ape-loaders" or helpers.get("status") != "pass":
            raise BuildError("Successful ape-loaders v1 manifest required")
        entries = [entry for entry in helpers["helpers"] if entry["os"] == host["os"] and entry["arch"] == host["execution_arch"]]
        report["loader_manifest"] = identity(manifest)
    else:
        # Exact official loader identities are already in the committed Nightly lock.
        suffix = "elf" if host["os"] == "Linux" else "macho"
        name = "ape-" + host["execution_arch"] + "." + suffix
        locked = read_json(ROOT / "toolchains/cosmocc.lock.json")
        entries = [{"file": name, **entry, "origin": "official Nightly lock"} for entry in locked["files"] if entry["path"] == "bin/" + name]
    if len(entries) != 1:
        raise BuildError("Exactly one native loader matching actual OS/architecture required", "blocked")
    entry = entries[0]
    if Path(entry["file"]).name != entry["file"]:
        raise BuildError("Loader filename must be a basename")
    path = args.loader_dir.resolve() / entry["file"]
    if not path.is_file():
        raise BuildError("Distributed loader missing", "blocked")
    observed = identity(path)
    if observed["sha256"] != entry["sha256"] or observed["size_bytes"] != entry["size_bytes"]:
        raise BuildError("Native loader SHA-256/size differs from manifest")
    path.chmod(path.stat().st_mode | 0o111)
    report["loader"] = {**observed, "os": host["os"], "arch": host["execution_arch"], "origin": entry["origin"]}
    return [str(path)]


def evaluate_case(case_id, name, kind, host, record, output, errors, artifact):
    actual = {"stdout": output, "stderr": errors, "exit_code": record["exit_code"], "timed_out": record["timed_out"]}
    expected = {"exit_code": 0, "output": "JSON facts matching actual OS architecture/page size" if kind == "facts" else "42\\n"}
    passed = record["exit_code"] == 0 and not record["timed_out"]
    if kind == "facts":
        try:
            actual["facts"] = json.loads(output)
        except ValueError:
            actual["facts"] = None
        passed = passed and actual["facts"] == {"probe": name, "arch": host["execution_arch"], "page_size": host["page_size"]}
    elif kind == "value":
        passed = passed and output == "42\n" and not errors
    else:
        expected = {"exit_code": "nonzero", "stderr_contains": kind}
        passed = record["exit_code"] is not None and record["exit_code"] != 0 and not record["timed_out"] and kind in errors
    return {"case_id": case_id, "status": "pass" if passed else "fail", "reason": "Actual process output/status compared with contract",
            "started_at": record["started_at"], "finished_at": record["finished_at"], "expected": expected,
            "actual": actual, "artifact": artifact, "commands": [record]}


def native_run(args, report, directory, artifacts):
    measured_host(args, report, directory)
    if os.name != "nt":
        # Artifact transport drops Unix execute bits. Both product identities
        # and headers have already passed producer_inputs before this mutation.
        for artifact in report["artifacts"]:
            path = Path(artifact["path"])
            before = stat.S_IMODE(path.stat().st_mode)
            path.chmod(before | 0o111)
            artifact["execute_permissions"] = {"mode_before": oct(before),
                "mode_after": oct(stat.S_IMODE(path.stat().st_mode)), "x_ok": os.access(path, os.X_OK)}
            if not artifact["execute_permissions"]["x_ok"]:
                raise BuildError("Artifact is not executable after restoring verified file permissions; filesystem or access policy may forbid execution", "blocked")
    loader = select_loader(args, report)
    if report["host"]["os"] == "Darwin" and report["host"]["execution_arch"] == "aarch64":
        record, output, errors = command(["/usr/bin/codesign", "--verify", "--strict", loader[0]], directory, "loader-signature", args.timeout)
        report["commands"].append(record)
        report["loader"]["signature_verification"] = {"exit_code": record["exit_code"], "stdout": output, "stderr": errors}
        if record["exit_code"] != 0:
            raise BuildError("Native macOS loader signature verification failed")
    for case_id, name, arguments, kind in CASES:
        record, output, errors = command([*loader, artifacts[name]["path"], *arguments], directory, case_id, args.timeout)
        report["commands"].append(record)
        report["cases"].append(evaluate_case(case_id, name, kind, report["host"], record, output, errors, artifacts[name].copy()))
    for artifact in report["artifacts"]:
        artifact["sha256_after"] = sha256(artifact["path"])
        if artifact["sha256_after"] != artifact["sha256"]:
            raise BuildError("Artifact bytes changed during execution")
    if loader:
        report["loader"]["sha256_after"] = sha256(loader[0])
        if report["loader"]["sha256_after"] != report["loader"]["sha256"]:
            raise BuildError("Native loader bytes changed during execution")
    report.update(status="pass" if all(case["status"] == "pass" for case in report["cases"]) else "fail",
                  reason="Five real startup/eval cases recorded; full six-host coverage requires matrix validation")


TARGETS = (("linux-x86_64", "Linux", "x86_64", None), ("windows-x86_64", "Windows", "x86_64", None),
           ("macos-x86_64", "Darwin", "x86_64", None), ("macos-aarch64", "Darwin", "aarch64", None),
           ("linux-aarch64-4k", "Linux", "aarch64", 4096), ("linux-aarch64-64k", "Linux", "aarch64", 65536))


def target_key(host):
    for name, system, arch, page in TARGETS:
        if host["os"] == system and host["execution_arch"] == arch and (page is None or host["page_size"] == page):
            if system == "Darwin" and arch == "aarch64" and host["execution_mode"] != "native":
                return None
            return name
    return None


def validate_startup(item, artifacts):
    if item["schema_version"] != 1 or item["report_kind"] != "ape-startup":
        raise BuildError("Unsupported startup report contract")
    host = item["host"]
    if not host["kernel"] or not isinstance(host["page_size"], int) or host["page_size"] <= 0:
        raise BuildError("Startup report lacks actual kernel/page-size facts")
    if host["execution_mode"] not in ("native", "emulated-system"):
        raise BuildError("Startup execution mode must be native or full-system guest")
    if host["execution_mode"] == "native":
        if host["cpu"] != host["execution_arch"] or item["qemu"] != {"status": "not_applicable"}:
            raise BuildError("Native startup report has contradictory architecture/QEMU identity")
    elif not item.get("qemu") or item["qemu"].get("status") == "not_applicable":
        raise BuildError("Full-system guest requires QEMU/kernel/rootfs identity")
    observed = {entry["name"]: entry for entry in item["artifacts"]}
    if set(observed) != set(NAMES):
        raise BuildError("Startup report must retain both distributed artifact identities")
    for name in NAMES:
        for key in ("sha256", "size_bytes"):
            if observed[name][key] != artifacts[name][key]:
                raise BuildError("Startup artifact differs from selected producer: " + name)
        if observed[name].get("sha256_after") != artifacts[name]["sha256"]:
            raise BuildError("Startup report lacks unchanged artifact proof: " + name)
    if item["status"] == "pass":
        cases = {case["case_id"]: case for case in item["cases"]}
        if len(item["cases"]) != 5 or set(cases) != {entry[0] for entry in CASES}:
            raise BuildError("Passing startup report must contain exactly five cases")
        for case_id, name, arguments, kind in CASES:
            case = cases[case_id]
            if case["status"] != "pass" or len(case["commands"]) != 1:
                raise BuildError("Passing report contains non-passing/missing actual command")
            record = case["commands"][0]
            if not all(record.get(key) for key in ("argv", "started_at", "finished_at", "stdout_log", "stderr_log")):
                raise BuildError("Command lacks required raw evidence fields")
            compared = evaluate_case(case_id, name, kind, host, record, case["actual"]["stdout"], case["actual"]["stderr"], observed[name])
            if compared["status"] != "pass" or record["exit_code"] != case["actual"]["exit_code"]:
                raise BuildError("Passing report contradicts actual output/status: " + case_id)


def matrix(args, report, artifacts):
    report["report_kind"] = "ape-startup-matrix"
    report["inputs"] = []
    coverage = {name: {"target": name, "status": "blocked", "reason": "No actual startup report", "reports": []} for name, *_ in TARGETS}
    report["coverage"] = list(coverage.values())
    for path in args.input_report:
        item = read_json(path)
        validate_startup(item, artifacts)
        report["inputs"].append(identity(path))
        key = target_key(item["host"])
        if key is None:
            raise BuildError("Report does not cover a required measured target (including actual ARM64 page size)")
        slot = coverage[key]
        slot["reports"].append({**identity(path), "report_id": item["report_id"], "host": item["host"], "status": item["status"]})
        if item["status"] != "pass":
            slot.update(status="fail" if item["status"] == "fail" else "blocked", reason=item["reason"])
        elif all(entry["status"] == "pass" for entry in slot["reports"]):
            slot.update(status="pass", reason="Five actual cases on unchanged producer bytes")
    statuses = [entry["status"] for entry in coverage.values()]
    report.update(status="fail" if "fail" in statuses else "pass" if all(value == "pass" for value in statuses) else "blocked",
                  reason="Six required measured targets, including failures and missing reports, retained")


def guest_manifest(args, report, artifacts):
    if args.page_size not in (4096, 65536):
        raise BuildError("Guest manifest requires explicit 4096/65536 page size")
    report["host"] = {"os": "Linux", "execution_arch": "aarch64", "cpu": "aarch64", "page_size": args.page_size}
    loader = select_loader(args, report)[0]
    files = [{"path": entry["path"], "guest_path": "/payload/" + name + ".com", "sha256": entry["sha256"]} for name, entry in artifacts.items()]
    files.append({"path": loader, "guest_path": "/payload/ape-aarch64.elf", "sha256": sha256(loader)})
    manifest = {"schema_version": 1, "page_size": args.page_size, "files": files, "commands": [
        {"case_id": case_id, "argv": ["/payload/ape-aarch64.elf", "/payload/" + name + ".com", *arguments],
         "expected_exit_code": 0 if kind in ("facts", "value") else 1} for case_id, name, arguments, kind in CASES]}
    write_json(args.manifest, manifest)
    report["report_kind"] = "ape-startup-manifest"
    report.update(status="pass", reason="Guest manifest exported; no guest execution claimed", manifest=identity(args.manifest))


def producer_inputs(args, report):
    producer = read_json(args.build_report)
    if producer.get("report_kind") != "ape-build" or producer.get("schema_version") != 1 or producer.get("status") != "pass":
        raise BuildError("A successful ape-build v1 producer report is required")
    report["producer_report"] = identity(args.build_report)
    for key in ("source", "toolchain", "build"):
        report[key] = producer[key]
    artifacts = {entry["name"]: entry for entry in producer["artifacts"]}
    if set(artifacts) != set(NAMES):
        raise BuildError("Producer must identify exactly Hello and eval")
    for name in NAMES:
        path = args.artifact_dir.resolve() / (name + ".com")
        if not path.is_file():
            raise BuildError("Missing distributed artifact: " + name, "blocked")
        observed = identity(path)
        if observed["sha256"] != artifacts[name]["sha256"] or observed["size_bytes"] != artifacts[name]["size_bytes"]:
            raise BuildError("Distributed artifact SHA-256/size differs from producer: " + name)
        observed["headers"] = headers(path, [path.with_suffix(".com.dbg"), path.with_suffix(".aarch64.elf")])
        report["artifacts"].append({"name": name, **observed})
    return {entry["name"]: entry for entry in report["artifacts"]}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("operation", choices=("run", "matrix", "guest-manifest"))
    parser.add_argument("--build-report", type=Path, required=True)
    parser.add_argument("--artifact-dir", type=Path, required=True)
    parser.add_argument("--report", type=Path, required=True)
    parser.add_argument("--loader-dir", type=Path)
    parser.add_argument("--loader-manifest", type=Path)
    parser.add_argument("--expected-os", choices=("Linux", "Windows", "Darwin"))
    parser.add_argument("--expected-arch", choices=("x86_64", "aarch64"))
    parser.add_argument("--runner-label")
    parser.add_argument("--timeout", type=int, default=30)
    parser.add_argument("--input-report", type=Path, action="append", default=[])
    parser.add_argument("--manifest", type=Path)
    parser.add_argument("--page-size", type=int)
    args = parser.parse_args()
    if not 1 <= args.timeout <= 300:
        parser.error("timeout must be 1–300 seconds")
    args.report = args.report.resolve()
    directory = args.report.parent / "evidence" / str(uuid.uuid4())
    directory.mkdir(parents=True)
    report = {"schema_version": 1, "report_kind": "ape-startup", "report_id": directory.name,
              "started_at": now(), "finished_at": None, "status": "not_run", "reason": "",
              "m0_core_status": "not_run", "source": None, "toolchain": None, "build": None,
              "host": host_facts(), "ci": ci_facts(), "qemu": {"status": "not_applicable"},
              "commands": [], "cases": [], "artifacts": [], "evidence_directory": str(directory)}
    try:
        artifacts = producer_inputs(args, report)
        if args.operation == "run":
            native_run(args, report, directory, artifacts)
        elif args.operation == "matrix":
            matrix(args, report, artifacts)
        else:
            if not args.manifest:
                raise BuildError("guest-manifest requires --manifest")
            guest_manifest(args, report, artifacts)
    except BuildError as error:
        report.update(status=error.status, reason=str(error))
    except (OSError, ValueError, TypeError, KeyError) as error:
        report.update(status="fail", reason=str(error))
    report["finished_at"] = now()
    write_json(args.report, report)
    print(json.dumps({"status": report["status"], "reason": report["reason"], "report": str(args.report)}))
    return {"pass": 0, "blocked": 2}.get(report["status"], 1)


if __name__ == "__main__":
    sys.exit(main())
