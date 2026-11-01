#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Run the same Ubuntu-built Hello/eval bytes and collect actual host evidence."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import stat
import sys
import uuid

from build_ape import BuildError, M0A_CASES, OBJECT_LIFECYCLE_EXPRESSION, headers, identity
from ci_identity import validate_ci_identity
from input_lock import component, consume_identity, check_loader_bundle, check_portable_helper, consumption_purpose
from evidence import read_json
from foundation import ROOT, command, host_facts, now, sha256
from nightly_toolchain import write_json

NAMES = ("hello", "quickjs-eval")
CASES = (("hello.facts", "hello", [], "facts"),
         ("quickjs-eval.facts", "quickjs-eval", ["--facts"], "facts"),
         ("quickjs-eval.value", "quickjs-eval", [], "value"),
         ("quickjs-eval.pointer-roundtrip", "quickjs-eval", ["--pointer-check"], "pointer-roundtrip"),
         ("quickjs-eval.objects-lifecycle", "quickjs-eval", [OBJECT_LIFECYCLE_EXPRESSION], "objects-lifecycle"),
         ("quickjs-eval.exception", "quickjs-eval", ["throw new Error('m0-probe')"], "Error: m0-probe"),
         ("quickjs-eval.syntax-error", "quickjs-eval", ["("], "SyntaxError:")) + tuple(
             ("quickjs-eval." + suffix, "quickjs-eval", arguments, error or suffix)
             for suffix, arguments, _, error in M0A_CASES)
EXPECTED_STDOUT = {"value": "42\n", "pointer-roundtrip": "JSValue pointer round-trip: pass\n", "objects-lifecycle": "9990000\n"}
EXPECTED_STDOUT.update({suffix: output for suffix, _, output, error in M0A_CASES if not error})


def ci_facts():
    if os.environ.get("GITHUB_ACTIONS") != "true":
        return None
    return {key: os.environ.get(value) for key, value in {
        "run_id": "GITHUB_RUN_ID", "run_attempt": "GITHUB_RUN_ATTEMPT", "workflow": "GITHUB_WORKFLOW",
        "commit": "GITHUB_SHA", "runner_name": "RUNNER_NAME", "runner_os": "RUNNER_OS",
        "runner_arch": "RUNNER_ARCH", "image_os": "ImageOS", "image_version": "ImageVersion"}.items()}


def check_ci_commit(producer, consumer):
    """Join actual execution identities, independently of where reports are read."""
    identities = [producer.get("ci"), consumer.get("ci")]
    if identities == [None, None]:
        return
    for ci in identities:
        try:
            validate_ci_identity(ci)
        except ValueError as error:
            raise BuildError(str(error)) from error
    if identities[0]["commit"] != identities[1]["commit"]:
        raise BuildError("CI consumer commit differs from selected producer commit")


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


def loader_bundle(args, report):
    if not args.loader_dir:
        return None
    manifest = args.loader_manifest or args.loader_dir / "helpers.json"
    if manifest.is_file():
        helpers = read_json(manifest)
        if helpers.get("schema_version") != 1 or helpers.get("report_kind") != "ape-loaders" or helpers.get("status") != "pass":
            raise BuildError("Successful ape-loaders v1 manifest required")
        locked = consume_identity(helpers, report, consumption_purpose("build", getattr(args, "controlled_probe", False)))
        check_loader_bundle(helpers, locked)
        report["loader_manifest"] = identity(manifest)
        return helpers
    return None


def select_loader(args, report):
    host = report["host"]
    if host["os"] == "Windows":
        report["loader"] = {"status": "not_applicable", "route": "Windows PE CreateProcess"}
        return []
    if not args.loader_dir:
        raise BuildError("Explicit distributed native loader directory required", "blocked")
    helpers = loader_bundle(args, report)
    if helpers is not None:
        entries = [entry for entry in helpers["helpers"] if entry["os"] == host["os"] and entry["arch"] == host["execution_arch"]]
    elif getattr(args, "controlled_probe", False) and host["os"] == "Linux" and host["execution_arch"] == "aarch64":
        producer = read_json(args.build_report)
        locked = consume_identity(producer, report, "probe")
        portable = producer["portable_helpers"]
        check_portable_helper(portable, locked)
        entries = [{**portable["loader"], "file": "ape-aarch64.elf", "origin": "controlled source-built ARM64 loader"}]
    else:
        # Exact official loader identities are already in the committed Nightly lock.
        suffix = "elf" if host["os"] == "Linux" else "macho"
        name = "ape-" + host["execution_arch"] + "." + suffix
        locked = component("cosmocc", purpose=consumption_purpose("build", getattr(args, "controlled_probe", False)))
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
    expected = {"exit_code": 0, "output": "JSON facts matching actual OS architecture/page size" if kind == "facts" else EXPECTED_STDOUT.get(kind)}
    passed = record["exit_code"] == 0 and not record["timed_out"]
    if kind == "facts":
        try:
            actual["facts"] = json.loads(output)
        except ValueError:
            actual["facts"] = None
        passed = passed and not errors and actual["facts"] == {"probe": name, "arch": host["execution_arch"], "page_size": host["page_size"]}
    elif kind in EXPECTED_STDOUT:
        passed = passed and output == EXPECTED_STDOUT[kind] and not errors
    else:
        expected = {"exit_code": "nonzero", "stderr_contains": kind}
        passed = record["exit_code"] is not None and record["exit_code"] != 0 and not record["timed_out"] and not output and kind in errors
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
                  reason="17 real startup/eval/ESM/jobs cases recorded; full six-host coverage requires matrix validation")


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
        if len(item["cases"]) != len(CASES) or set(cases) != {entry[0] for entry in CASES}:
            raise BuildError("Passing startup report must contain exactly the required startup/eval/ESM/jobs cases")
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
    report["profile"] = args.profile
    report["inputs"] = []
    report["additional_reports"] = []
    targets = [name for name, *_ in TARGETS] if args.profile == "full" else [
        "linux-x86_64", "windows-x86_64", "macos-x86_64", "macos-aarch64", "linux-aarch64"]
    coverage = {name: {"target": name, "status": "blocked", "reason": "No actual startup report", "reports": []} for name in targets}
    report["coverage"] = list(coverage.values())
    paths = [*args.input_report, *(path for root in args.input_dir for path in sorted(root.rglob("startup.json")))]
    producer = read_json(args.build_report)
    selected = identity(args.build_report)
    report_ids = set()
    for path in paths:
        item = read_json(path)
        if not item.get("report_id") or item["report_id"] in report_ids:
            raise BuildError("Duplicate/missing selected startup report identity")
        report_ids.add(item["report_id"])
        consume_identity(item, report)
        check_ci_commit(producer, item)
        if any(item.get("producer_report", {}).get(key) != selected[key] for key in ("sha256", "size_bytes")) or any(
                item.get(field) != producer[field] for field in ("source", "toolchain", "build")):
            raise BuildError("Startup report differs from selected producer report/context")
        validate_startup(item, artifacts)
        report["inputs"].append(identity(path))
        key = target_key(item["host"])
        if args.profile == "cross":
            host = item["host"]
            if host["execution_mode"] != "native":
                raise BuildError("Cross requires five actual native hosts; a guest cannot replace a native target")
            if host["os"] == "Linux" and host["execution_arch"] == "aarch64":
                key = "linux-aarch64"
        if key is None:
            host = item["host"]
            if host["os"] == "Linux" and host["execution_arch"] == "aarch64" and host["execution_mode"] == "native":
                report["additional_reports"].append({**identity(path), "report_id": item["report_id"],
                    "host": host, "status": item["status"], "reason": "Measured native page size covers neither required ARM64 target"})
                continue
            raise BuildError("Report does not cover a required measured target (including actual ARM64 page size)")
        slot = coverage[key]
        slot["reports"].append({**identity(path), "report_id": item["report_id"], "host": item["host"], "status": item["status"]})
        if item["status"] == "fail":
            slot.update(status="fail", reason=item["reason"])
        elif item["status"] != "pass" and slot["status"] != "fail":
            slot.update(status="blocked", reason=item["reason"])
        elif all(entry["status"] == "pass" for entry in slot["reports"]):
            slot.update(status="pass", reason="17 actual startup/eval/ESM/jobs cases on unchanged producer bytes")
    statuses = [entry["status"] for entry in coverage.values()] + [entry["status"] for entry in report["additional_reports"]]
    report.update(status="fail" if "fail" in statuses else "pass" if all(value == "pass" for value in statuses) else "blocked",
                  reason=("Six required measured targets" if args.profile == "full" else "Five required native hosts") +
                         ", including failures and missing reports, retained; complete Core remains separate")


def guest_manifest(args, report, artifacts):
    if args.page_size not in (4096, 65536):
        raise BuildError("Guest manifest requires explicit 4096/65536 page size")
    report["host"] = {"os": "Linux", "execution_arch": "aarch64", "cpu": "aarch64", "page_size": args.page_size}
    loader = select_loader(args, report)[0]
    expected_loader = read_json(args.build_report)["portable_helpers"]["loader"]
    if sha256(loader) != expected_loader["sha256"] or Path(loader).stat().st_size != expected_loader["size_bytes"]:
        raise BuildError("Exported guest loader differs from selected portable producer")
    files = [{"path": entry["path"], "guest_path": "/payload/" + name + ".com", "sha256": entry["sha256"]} for name, entry in artifacts.items()]
    files.append({"path": loader, "guest_path": "/payload/ape-aarch64.elf", "sha256": sha256(loader)})
    commands = []
    for case_id, name, arguments, kind in CASES:
        item = {"case_id": case_id, "argv": ["/payload/ape-aarch64.elf", "/payload/" + name + ".com", *arguments],
                "expected_exit_code": 0 if kind == "facts" or kind in EXPECTED_STDOUT else 1}
        if kind == "facts":
            item.update(expected_stdout=json.dumps({"probe": name, "arch": "aarch64", "page_size": args.page_size}, separators=(",", ":")) + "\n", expected_stderr="")
        elif kind in EXPECTED_STDOUT:
            item.update(expected_stdout=EXPECTED_STDOUT[kind], expected_stderr="")
        else:
            item.update(expected_stdout="", stderr_contains=kind)
        commands.append(item)
    manifest = {"schema_version": 1, "page_size": args.page_size, "files": files, "commands": commands}
    write_json(args.manifest, manifest)
    report["report_kind"] = "ape-startup-manifest"
    report.update(status="pass", reason="Guest manifest exported; no guest execution claimed", manifest=identity(args.manifest))


def import_guest(args, report, artifacts):
    if not args.guest_report or not args.guest_report.is_file():
        raise BuildError("Actual full-system guest report required", "blocked")
    incoming = read_json(args.guest_report)
    check_ci_commit(read_json(args.build_report), incoming)
    report["guest_report"] = identity(args.guest_report)
    if incoming.get("schema_version") != 1 or incoming.get("report_kind") != "arm64-guest":
        raise BuildError("Supported arm64-guest v1 evidence required")
    facts = incoming["guest"]
    report["validation_host"] = report["host"]
    report["execution_host"] = incoming["host"]
    report.update(guest=facts, qemu=incoming["qemu"], ci=incoming.get("ci"), cleanup=incoming["cleanup"],
                  commands=incoming["commands"], guest_status=incoming["status"], guest_reason=incoming["reason"])
    if not isinstance(facts, dict):
        report["host"] = None
        raise BuildError("Full-system guest did not yield measured startup facts: " + incoming["reason"],
                         "blocked" if incoming["status"] == "blocked" else "fail")
    report["host"] = {"os": facts["os"], "cpu": facts["architecture"], "execution_arch": facts["architecture"],
        "kernel": facts["kernel"], "page_size": facts["page_size"], "execution_mode": facts["execution_mode"],
        "measurement": "Actual full-system guest uname/getconf/smaps; see original serial evidence"}
    if facts["os"] != "Linux" or facts["architecture"] != "aarch64" or facts["execution_mode"] != "emulated-system" or facts["page_size"] not in (4096, 65536):
        raise BuildError("Actual Linux ARM64 full-system guest with measured 4096/65536 pages required")
    if facts["page_size_alias"] != facts["page_size"] or facts["smaps_kib"] != facts["page_size"] // 1024 or not facts["kernel"]:
        raise BuildError("Guest page-size/kernel measurements disagree")
    qemu = incoming["qemu"]
    if not qemu["acceleration"].startswith("tcg") or qemu["guest_arch"] != "aarch64" or qemu["host_arch"] != incoming["host"]["execution_arch"] or not all(qemu[key] for key in ("version", "machine", "cpu")):
        raise BuildError("Complete actual QEMU TCG/machine/CPU/host identity required")
    for key in ("kernel", "config", "rootfs", "executable", "serial_log", "stderr_log"):
        entry = qemu[key]
        if len(entry["sha256"]) != 64 or entry["size_bytes"] < 0:
            raise BuildError("Missing QEMU input/raw-log identity: " + key)
    for key in ("serial_log", "stderr_log"):
        checked_raw_log(qemu[key], args.guest_report, incoming["report_id"])
    transports = [entry for entry in incoming["commands"] if entry["stdout_log"] == qemu["serial_log"]["path"]]
    if len(transports) != 1 or transports[0]["exit_code"] != 0 or transports[0]["timed_out"]:
        raise BuildError("Actual QEMU serial command did not finish cleanly")
    payloads = {entry["guest_path"]: entry for entry in incoming["files"]}
    if len(payloads) != len(incoming["files"]):
        raise BuildError("Duplicate guest payload identity")
    before = facts["file_sha256"]
    after = facts["file_sha256_after"]
    if before != after or before != {path: item["sha256"] for path, item in payloads.items()}:
        raise BuildError("Guest before/after transferred payload SHA-256 proof differs")
    for name, artifact in artifacts.items():
        path = "/payload/" + name + ".com"
        if before.get(path) != artifact["sha256"] or payloads[path]["size_bytes"] != artifact["size_bytes"]:
            raise BuildError("Guest startup artifact differs from selected producer: " + name)
        artifact["sha256_after"] = after[path]
    producer = read_json(args.build_report)
    consume_identity(producer, report, consumption_purpose("build", args.controlled_probe))
    expected_loader = producer["portable_helpers"]["loader"]
    guest_loader = "/payload/ape-aarch64.elf"
    if before.get(guest_loader) != expected_loader["sha256"] or payloads[guest_loader]["size_bytes"] != expected_loader["size_bytes"]:
        raise BuildError("Guest native loader differs from selected producer")
    report["loader"] = {**payloads[guest_loader], "sha256_after": after[guest_loader],
                        "origin": "Selected producer's source-built Linux ARM64 loader"}
    cases = {case["case_id"]: case for case in incoming["cases"]}
    if len(cases) != len(incoming["cases"]):
        raise BuildError("Duplicate guest command result")
    for case_id, name, arguments, kind in CASES:
        raw = cases[case_id]
        expected_argv = [guest_loader, "/payload/" + name + ".com", *arguments]
        if raw["argv"] != expected_argv or raw["exit_code"] != raw["actual"]["exit_code"]:
            raise BuildError("Actual guest command differs from required startup invocation: " + case_id)
        paths = {}
        for key in ("stdout", "stderr"):
            data, paths[key] = checked_raw_log(raw[key + "_log"], args.guest_report, incoming["report_id"])
            if data.decode("utf-8", "replace") != raw["actual"][key]:
                raise BuildError("Guest actual output differs from raw log bytes: " + case_id)
        if raw["serial_log"] != qemu["serial_log"]:
            raise BuildError("Guest case refers to different raw serial evidence")
        record = {"argv": raw["argv"], "cwd": raw.get("cwd"), "started_at": raw["started_at"],
            "finished_at": raw["finished_at"], "timeout_seconds": raw["timeout_seconds"],
            "exit_code": raw["exit_code"], "timed_out": raw["exit_code"] == 137,
            "stdout_log": paths["stdout"], "stderr_log": paths["stderr"],
            "serial_log": raw["serial_log"], "guest_case": raw}
        compared = evaluate_case(case_id, name, kind, report["host"], record,
                                raw["actual"]["stdout"], raw["actual"]["stderr"], artifacts[name].copy())
        if raw["status"] != "pass":
            compared.update(status="fail", reason="Original guest case did not pass")
        report["cases"].append(compared)
    clean = incoming["cleanup"]
    passed = (incoming["status"] == "pass" and all(case["status"] == "pass" for case in report["cases"])
              and clean["qemu_exited"] and clean["ram_only"] and not clean["timed_out"] and not clean["host_global_changes"]
              and clean["disks"] == [] and clean["network"] == "none")
    report.update(status="pass" if passed else "fail", reason="Actual guest startup semantics and unchanged producer bytes checked; full matrix remains separate")


def checked_raw_log(entry, guest_report, report_id):
    # Downloaded evidence keeps the report/evidence layout but belongs to a
    # different checkout path. Validate the transported bytes before use.
    path = guest_report.parent / "evidence" / report_id / Path(entry["path"]).name
    if not path.is_file():
        path = Path(entry["path"])
    data = path.read_bytes()
    if len(data) != entry["size_bytes"] or hashlib.sha256(data).hexdigest() != entry["sha256"]:
        raise BuildError("Guest raw log SHA-256/size differs from evidence identity")
    return data, str(path.resolve())


def page_gaps(args, report):
    """Plan scheduling from OS measurements; this is never a coverage pass."""
    report["report_kind"] = "ape-page-plan"
    observations, measured, covered = [], set(), set()
    for path in args.input_report:
        if not path.is_file():
            observations.append({"path": str(path), "status": "not_run", "reason": "Native report unavailable"})
            continue
        incoming = read_json(path)
        if incoming.get("report_kind") != "ape-startup" or incoming.get("schema_version") != 1:
            raise BuildError("Page planning requires a supported actual startup report")
        host = incoming["host"]
        page = host["page_size"]
        if not isinstance(page, int) or page <= 0 or not host["kernel"]:
            raise BuildError("Actual kernel/page-size measurements required for page planning")
        eligible = (host["os"] == "Linux" and host["cpu"] == host["execution_arch"] == "aarch64"
                    and host["execution_mode"] == "native" and incoming["qemu"] == {"status": "not_applicable"})
        observations.append({**identity(path), "host": host, "status": incoming["status"], "eligible_native_arm64": eligible})
        if eligible:
            measured.add(page)
            if page in (4096, 65536):
                covered.add(page)
    missing = [page for page in (4096, 65536) if page not in covered]
    report.update(status="pass", reason="Scheduling plan only; native startup failures and full matrix acceptance remain separate",
                  observations=observations, measured_native_pages=sorted(measured), covered_native_pages=sorted(covered),
                  missing_pages=missing, matrix={"include": [{"page_size": page} for page in missing]})
    if args.github_output:
        with args.github_output.open("a", encoding="utf-8") as stream:
            stream.write("matrix=" + json.dumps(report["matrix"], separators=(",", ":")) + "\n")
            stream.write("needs_guest=" + ("true" if missing else "false") + "\n")
            stream.write("native_page_size=" + (str(next(iter(measured))) if len(measured) == 1 else "") + "\n")


def producer_inputs(args, report):
    producer = read_json(args.build_report)
    consume_identity(producer, report, consumption_purpose("build", args.controlled_probe))
    if report["host"]["os"] != "Windows":
        loader_bundle(args, report)
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
    cases = producer.get("cases", [])
    if len(cases) != len(CASES) or {case.get("case_id") for case in cases} != {entry[0] for entry in CASES} or any(
            case.get("status") != "pass" for case in cases):
        raise BuildError("Producer startup/ESM/jobs catalog incomplete or nonpassing")
    if args.operation == "run":
        check_ci_commit(producer, report)
    return {entry["name"]: entry for entry in report["artifacts"]}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("operation", choices=("run", "matrix", "guest-manifest", "import-guest", "page-gaps"))
    parser.add_argument("--controlled-probe", action="store_true", help="consume candidate probes without issuing ordinary or full-matrix acceptance")
    parser.add_argument("--profile", choices=("full", "cross"), default="full", help="matrix coverage: six page targets or five native hosts")
    parser.add_argument("--build-report", type=Path)
    parser.add_argument("--artifact-dir", type=Path)
    parser.add_argument("--report", type=Path, required=True)
    parser.add_argument("--loader-dir", type=Path)
    parser.add_argument("--loader-manifest", type=Path)
    parser.add_argument("--expected-os", choices=("Linux", "Windows", "Darwin"))
    parser.add_argument("--expected-arch", choices=("x86_64", "aarch64"))
    parser.add_argument("--runner-label")
    parser.add_argument("--timeout", type=int, default=30)
    parser.add_argument("--input-report", type=Path, action="append", default=[])
    parser.add_argument("--input-dir", type=Path, action="append", default=[])
    parser.add_argument("--manifest", type=Path)
    parser.add_argument("--page-size", type=int)
    parser.add_argument("--guest-report", type=Path)
    parser.add_argument("--github-output", type=Path)
    args = parser.parse_args()
    if not 1 <= args.timeout <= 300:
        parser.error("timeout must be 1–300 seconds")
    if args.operation != "page-gaps" and (not args.build_report or not args.artifact_dir):
        parser.error(args.operation + " requires --build-report and --artifact-dir")
    args.report = args.report.resolve()
    directory = args.report.parent / "evidence" / str(uuid.uuid4())
    directory.mkdir(parents=True)
    report = {"schema_version": 1, "report_kind": "ape-startup", "report_id": directory.name,
              "started_at": now(), "finished_at": None, "status": "not_run", "reason": "",
              "m0_core_status": "not_run", "source": None, "toolchain": None, "build": None,
              "host": host_facts(), "ci": ci_facts(), "qemu": {"status": "not_applicable"},
              "commands": [], "cases": [], "artifacts": [], "evidence_directory": str(directory)}
    try:
        report["controlled_probe"] = args.controlled_probe
        if args.controlled_probe and args.operation in ("matrix", "page-gaps"):
            raise BuildError("Controlled probes cannot issue ordinary matrix or scheduling evidence")
        artifacts = producer_inputs(args, report) if args.operation != "page-gaps" else None
        if args.operation == "page-gaps":
            page_gaps(args, report)
        elif args.operation == "run":
            native_run(args, report, directory, artifacts)
        elif args.operation == "matrix":
            matrix(args, report, artifacts)
        elif args.operation == "import-guest":
            import_guest(args, report, artifacts)
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
