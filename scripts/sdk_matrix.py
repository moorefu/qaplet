#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Read-only native SDK matrix using the supplied product catalogs and resolver."""
from datetime import datetime
import json
from pathlib import Path
import posixpath

from build_ape import BuildError, identity
from core_gate import Evidence, interval, normalized, require, same, unique
from input_lock import component, consume_identity
from startup import check_ci_commit, loader_bundle
import sdk

TARGETS = (("linux-x86_64", "Linux", "x86_64"), ("windows-x86_64", "Windows", "x86_64"),
           ("macos-x86_64", "Darwin", "x86_64"), ("macos-aarch64", "Darwin", "aarch64"),
           ("linux-aarch64", "Linux", "aarch64"))


def catalog(host):
    """Expected behavior comes from the same public-process contract as run."""
    result = []
    for suffix, arguments, output in sdk.HOST_CASES:
        if suffix == "facts":
            output = json.dumps({"probe": "qaplet-host", "arch": host["execution_arch"],
                                 "page_size": host["page_size"]}, separators=(",", ":")) + "\n"
        result.append(("qaplet-host." + suffix, "qaplet-host", arguments, 0, output, "", False))
    for name, cases in (("qaplet", sdk.CLI_CASES), ("qaplet-example", sdk.EXAMPLE_CASES)):
        result.extend((name + "." + suffix, name, arguments, code, output, error, True)
                      for suffix, arguments, code, output, error in cases)
    return result


def original_path(value, cwd):
    value = normalized(value)
    if not value.startswith("/") and not (len(value) > 2 and value[1:3] == ":/"):
        value = normalized(cwd) + "/" + value
    return posixpath.normpath(value)


def validate_loader(args, incoming, evidence):
    """Inspect distributed loader bytes without the executor's chmod side effect."""
    host, loader = incoming["host"], incoming["loader"]
    if host["os"] == "Windows":
        require(loader == {"status": "not_applicable", "route": "Windows PE CreateProcess"},
                "Windows must use actual native PE execution")
        return
    helpers = getattr(args, "loader_helpers", None)
    if helpers is None:
        require(args.loader_dir, "Explicit distributed native loader directory required")
        helpers = loader_bundle(args, {"host": host})
    if helpers is not None:
        entries = [entry for entry in helpers["helpers"]
                   if (entry["os"], entry["arch"]) == (host["os"], host["execution_arch"])]
    else:
        suffix = "elf" if host["os"] == "Linux" else "macho"
        name = "ape-" + host["execution_arch"] + "." + suffix
        entries = [{**entry, "file": name} for entry in component("cosmocc")["files"] if entry["path"] == "bin/" + name]
    require(len(entries) == 1 and Path(entries[0]["file"]).name == entries[0]["file"],
            "Exactly one safe distributed native loader required")
    entry = entries[0]
    require(same(loader, entry) and loader.get("sha256_after", loader["sha256"]) == loader["sha256"],
            "Native loader differs from selected distributed loader")
    reference = loader if getattr(args, "loader_helpers", None) is not None else {
        **entry, "path": str(args.loader_dir.resolve() / entry["file"])}
    evidence.read(reference)


def validate_native(args, incoming, producer, selected, evidence, directory):
    consume_identity(incoming, {})
    check_ci_commit(producer, incoming)
    require(same(incoming.get("producer_report", {}), selected), "Native producer report differs from selected SDK")
    for field in ("source", "toolchain", "build", "sdk", "sdk_recipe", "package", "distribution"):
        require(incoming.get(field) == producer[field], "Native context differs from selected producer: " + field)
    host = incoming["host"]
    require(host["cpu"] == host["execution_arch"] and host["execution_mode"] == "native",
            "Cross requires native execution; guest/translated reports cannot replace a target")
    require(type(host["page_size"]) is int and host["page_size"] > 0 and host["kernel"], "Measured native kernel/page size required")
    if host["os"] == "Darwin":
        translated = host["translation"]["sysctl.proc_translated"]
        require(translated["stdout"].strip() != "1", "Translated macOS cannot provide native evidence")
        require(host["translation"]["hw.optional.arm64"]["stdout"].strip() != "1" or host["cpu"] == "aarch64",
                "macOS hardware/execution architecture disagrees")
    loader = incoming["loader"]
    validate_loader(args, incoming, evidence)
    artifacts = unique(incoming["artifacts"], "name")
    supplied = unique(producer["artifacts"], "name")
    require(set(artifacts) == set(supplied), "All three supplied SDK APE identities required")
    for name, artifact in artifacts.items():
        require(same(artifact, supplied[name]) and artifact.get("sha256_after") == artifact["sha256"],
                "Native APE bytes changed/differ from supplied producer: " + name)
    cases = unique(incoming["cases"], "case_id")
    expected = catalog(host)
    require(set(cases) == {item[0] for item in expected}, "Complete authoritative SDK/CLI/application catalog required")
    required_cases, unavailable = [], []
    for case_id, name, arguments, code, output, error, contains in expected:
        case = cases[case_id]
        optional = case_id == "qaplet-host.module-symlinks" and host["os"] == "Windows"
        require(case.get("required") is (not optional), "Required case declaration contradicts native catalog")
        if not optional: required_cases.append(case_id)
        require(same(case["artifact"], supplied[name]) and case["sdk_sha256"] == producer["sdk"]["sha256"],
                "Per-case SDK/APE identity differs: " + case_id)
        interval(case)
        require(len(case["commands"]) == 1, "Exactly one actual process per native case required")
        command = case["commands"][0]
        interval(command)
        require(case["started_at"] == command["started_at"] and case["finished_at"] == command["finished_at"],
                "Native case/command timestamps disagree")
        require(interval(incoming)[0] <= interval(case)[0] <= interval(case)[1] <= interval(incoming)[1],
                "Native command lies outside report interval")
        require(type(command.get("timeout_seconds")) in (int, float) and 0 < command["timeout_seconds"] <= 300 and
                command["timeout_seconds"] == case["timeout_seconds"], "Actual per-case execution deadline required")
        require(command.get("timed_out") is False and type(command.get("exit_code")) is int,
                "Supervisor timeout/unexecuted native case cannot pass")
        require(isinstance(command.get("argv"), list) and all(isinstance(value, str) for value in command["argv"]) and command.get("cwd"),
                "Native argv/cwd required")
        artifact_path = original_path(artifacts[name]["path"], command["cwd"])
        modules = posixpath.dirname(artifact_path) + "/sdk/modules"
        module_arguments = {i for i, value in enumerate(arguments) if any(token in value for token in ("{modules}", "{self}", "{launcher}"))}
        arguments = [value.replace("{modules}", modules).replace("{self}", artifact_path).replace("{launcher}", "-" if host["os"] == "Windows" else normalized(loader["path"])) for value in arguments]
        wanted = ([] if host["os"] == "Windows" else [normalized(loader["path"])]) + [artifact_path, *arguments]
        actual_argv = list(command["argv"])
        prefix = 1 if host["os"] == "Windows" else 2
        actual_argv[:prefix] = [original_path(value, command["cwd"]) for value in actual_argv[:prefix]]
        # Module paths are native path arguments (Windows backslashes included).
        for i in range(prefix, len(actual_argv)):
            if i - prefix in module_arguments:
                actual_argv[i] = normalized(actual_argv[i])
        require(actual_argv == wanted, "Actual invocation contradicts SDK catalog: " + case_id)
        require(all(normalized(command[key + "_log"]).startswith(normalized(incoming["evidence_directory"]) + "/")
                    for key in ("stdout", "stderr")), "Native raw logs must belong to the selected evidence root")
        raw = [evidence.read(command[key + "_log"]) for key in ("stdout", "stderr")]
        require(raw == [case["actual"][key].encode("utf-8") for key in ("stdout", "stderr")],
                "Native actual output differs from raw logs: " + case_id)
        require(command["exit_code"] == case["actual"]["exit_code"] and case["actual"]["timed_out"] is False,
                "Native actual exit/timeout differs from command")
        expected_fields = {"exit_code": code, "stdout": output, "stderr_contains" if contains else "stderr": error}
        immediate = case_id in ("qaplet.eval-unsettled", "qaplet.run-unsettled")
        if name == "qaplet": expected_fields["maximum_elapsed_seconds"] = 2 if immediate else None
        require(case["expected"] == expected_fields, "Declared expected output contradicts authoritative contract: " + case_id)
        if optional and case["status"] == "blocked":
            require(command["exit_code"] in (77, 77 << 8) and raw[0] == b"" and raw[1].startswith(b"file-symlink fixture unavailable: errno="),
                    "Optional symlink limitation lacks actual fixture refusal")
            unavailable.append(case_id)
        else:
            require(case["status"] == "pass" and command["exit_code"] == code and raw[0] == output.encode("utf-8") and
                    (error.encode("utf-8") in raw[1] if contains and error else raw[1] == error.encode("utf-8")),
                    "Required native case did not satisfy public contract: " + case_id)
        elapsed = (datetime.fromisoformat(command["finished_at"]) - datetime.fromisoformat(command["started_at"])).total_seconds()
        require(elapsed <= command["timeout_seconds"], "Native command exceeded its recorded deadline: " + case_id)
        require(not immediate or elapsed < 2, "CLI unsettled entry did not fail immediately")
    require(incoming["required_cases"] == required_cases and incoming["unavailable_cases"] == unavailable,
            "Native required/unavailable catalog differs")
    minimum = 1000 if (host["os"], host["execution_arch"]) == ("Linux", "x86_64") else 100
    lifecycle = incoming["lifecycle"]
    require(lifecycle["case_id"] == "qaplet-host.lifetime" and lifecycle["status"] == "pass" and
            lifecycle["minimum_cycles"] == minimum and lifecycle["completed_cycles"] == 1000 and
            lifecycle["completed_cycles"] >= minimum, "Insufficient actual create/close/destroy lifecycle evidence")
    return lifecycle


def matrix(args, report, directory):
    targets = TARGETS[:1] if args.profile == "fast" else TARGETS
    if args.target_plan:
        from ci_tiers import native_targets
        require(args.profile == "cross", "Target plan requires Cross profile")
        selected_targets = native_targets(args.target_plan)
        targets = tuple(row for row in TARGETS if (row[1], row[2]) in selected_targets)
        report["target_plan"] = identity(args.target_plan)
    report.update(report_kind="qaplet-sdk-matrix", profile=args.profile, release_ready=False,
                  evaluator_sources=[identity(sdk.ROOT / "scripts" / name) for name in ("sdk_matrix.py", "core_gate.py")],
                  coverage=[{"target": name, "status": "blocked", "reason": "No actual native SDK report", "reports": []}
                            for name, _, _ in targets],
                  inputs=[], path_mappings=[], evidence_index=[])
    if len(targets) not in (1, 5) or args.target_plan and len(targets) < 5:
        report["profile"] = "targeted"
    if not args.build_report or not args.artifact:
        raise BuildError("Actual supplied Ubuntu SDK producer/artifact and native reports required", "blocked")
    selected = identity(args.build_report)
    producer = sdk.read_json(args.build_report)
    consume_identity(producer, report)
    require(producer["report_kind"] == "qaplet-sdk" and producer["scope"] == "build" and producer["status"] == "pass",
            "Successful actual SDK producer required")
    interval(producer)
    sdk.check_package(producer, args.artifact.resolve().parent)
    report.update(producer_report=selected, sdk=producer["sdk"], sdk_recipe=producer["sdk_recipe"],
                  package=producer["package"], source=producer["source"], toolchain=producer["toolchain"], build=producer["build"])
    paths = [*args.input_report, *(path for root in args.input_dir for path in sorted(root.rglob("run.json")))]
    mappings = {}
    incoming_reports = []
    for path in paths:
        incoming = sdk.read_json(path)
        root = path.resolve().parent / "evidence" / incoming["report_id"]
        source = normalized(incoming["evidence_directory"])
        require(source.endswith("/evidence/" + incoming["report_id"]), "Explicit report evidence root required")
        require(source not in mappings or mappings[source] == str(root), "Conflicting evidence-root mapping")
        mappings[source] = str(root)
        incoming_reports.append((path, incoming))
    report["path_mappings"] = [{"source": source, "root": root} for source, root in mappings.items()]
    evidence = Evidence({"path_mappings": report["path_mappings"]}, args.report.resolve().parent)
    try:
        evidence.report(str(args.build_report.resolve()), "qaplet-sdk", {"build"})
        for path, incoming in incoming_reports:
            evidence.report(str(path.resolve()), "qaplet-sdk", {"run"})
            host = incoming["host"]
            target = next((name for name, system, arch in TARGETS if (system, arch) == (host["os"], host["execution_arch"])), None)
            slot = next((slot for slot in report["coverage"] if slot["target"] == target), None)
            require(slot is not None, "Native report does not cover a required matrix target")
            lifecycle = validate_native(args, incoming, producer, selected, evidence, directory)
            slot["reports"].append({**identity(path), "report_id": incoming["report_id"], "host": host,
                                    "loader": incoming["loader"], "unavailable_cases": incoming["unavailable_cases"]})
            slot.update(status="pass", reason="Complete actual SDK/CLI/application contracts and lifecycle verified", lifecycle=lifecycle,
                        required_cases=len(incoming["required_cases"]))
            report["inputs"].append(identity(path))
        report.update(status="pass" if all(slot["status"] == "pass" for slot in report["coverage"]) else "blocked",
                      reason="Actual native SDK matrix; missing targets remain blocked; M1 Full/strict and release gate are separate")
    finally:
        report["evidence_index"] = list(evidence.index.values())
