#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""M2-only capture and read-only semantics for the fixed full-system guest."""
import base64
import argparse
import json
from pathlib import Path
import sys
import uuid
import shutil

from build_ape import BuildError, headers, identity
from core_gate import Evidence, interval, require, same, unique, verify_guest_transport, without_paths
from foundation import ROOT, now
from nightly_toolchain import write_json
from startup import check_ci_commit, ci_facts
from workflow_support import guard_evidence_outputs, write_evidence_report

import sdk
import sdk_mixed
import sdk_tty
import sdk_guest
from m2_fixture_binding import bind_auxiliary


def special_build_units(group):
    def unit(*names,wraps=()):return {"units":["sdk/"+name for name in names],"wrapped_symbols":list(wraps)}
    if group=="fault":return {
        "qaplet-m2-faults":unit("m2-faults.c",wraps=("pthread_mutex_init","pthread_mutex_destroy","pthread_condattr_init","pthread_condattr_destroy","pthread_condattr_setclock","pthread_cond_init","pthread_cond_destroy","clock_gettime","socket","accept","close","poll","send","recv")),
        "qaplet-work-faults":unit("work-faults.c","host-work.c",wraps=("calloc","pthread_create","pthread_tryjoin_np","send","mkstemp")),
        "qaplet-process-faults":unit("process-faults.c",wraps=("calloc","pthread_create","fork","sys_waitid","kill","read","clock_gettime","mkntcmdline","TerminateProcess","CloseHandle","GetExitCodeProcess","send")),
        "qaplet-io-faults":unit("io-faults.c",wraps=("read","ReadFile","write","WriteFile","close","CloseHandle","GetExitCodeProcess","calloc","pthread_create","send","WaitForSingleObject")),
        "qaplet-js-process-faults":unit("host-process.c",wraps=("pthread_create","sys_waitid")),
        "qaplet-stdio-faults":unit("stdio-faults.c",wraps=("read","ReadFile","write","WriteFile","pthread_create","calloc")),
        "qaplet-aggregate-faults":unit("aggregate-faults.c",wraps=("calloc","malloc","send","clock_gettime","poll"))}
    if group=="tty":return {"qaplet-tty-host":unit("host-tty.c"),"qaplet-tty-faults":unit("tty-faults.c",wraps=("tcsetattr","SetConsoleMode","WriteConsole","poll","pthread_create","read"))}
    if group=="mixed":return {"qaplet-mixed-host":unit("host-mixed.c"),"qaplet-mixed-faults":unit("mixed-faults.c",wraps=("socket","close","write","pthread_cond_timedwait"))}
    raise ValueError("Unknown special fixture group")


M2_FAULT_STDOUT = (
    "controlled HostNotify: accepted event/close and CLOSED wake overlap; JS released; inflight retained; finite quiesce retry; destroy BUSY; producer joined; drained cleanup: pass\n"
    "controlled HostNotify reentry: deliberately invalid hook receives BUSY for SDK queries/control/drives/close; no JS or stop mutation: pass\n"
    "controlled M2: four system initialization refusals; resources balanced; PENDING/CLOSED whole-drive guard: pass\n"
    "controlled M2 wait: pre-sleep handoff; accepted send failure; poll/recv/clock faults; first cause; socket rollback; uncertain close retained; reused fd untouched: pass\n"
    "controlled M2 diagnostics: original setup/cleanup errors; incomplete unpublished rollback; fd reuse; independent published first cause: pass\n"
    "controlled M2 timers: deadline overflow; max-wait/timer/entry minimum; clock failure/backwards; terminal cleanup: pass\n"
    "controlled M2 scheduling: same deadline acceptance FIFO; interval skips late periods on original schedule without burst; self cancel: pass\n")
WORK_FAULT_STDOUT = (
    "controlled M2 work JS allocation: actual SDK calloc refusal from caught JS submit remains permanent; no accepted task; first cause preserved; native and engine cleanup: pass\n"
    + next(output for suffix, _, output in sdk.HOST_CASES if suffix == "m2-work-file") +
    "controlled M2 work: pthread create errno refuses atomically and recovers; actual calloc refusal permanent; accepted submit/completion/cancel survives wake failure; join error independently retained CLOSED/BUSY without identity retry or false cleanup: pass\n")


def special_catalog():
    """Applicable POSIX SDK contracts; controlled retention stays explicit."""
    loader = "/payload/ape-aarch64.elf"
    rows = []
    def add(group, artifact, name, arguments, expected, cleanup=True, **extra):
        rows.append({"group": group, "artifact": artifact, "case_id": artifact + "." + name,
                     "arguments": arguments, "expected_stdout": expected,
                     "native_cleanup_complete": cleanup,
                     "controlled_system_fixture": "faults" in artifact, **extra})
    for artifact, expected in (("qaplet-m2-faults", M2_FAULT_STDOUT), ("qaplet-work-faults", WORK_FAULT_STDOUT)):
        add("fault", artifact, "system-boundary", [], expected, False,
            cleanup_scope="Combined fixture includes deliberately uncertain bounded native retention")
    for mode, expected, platforms in sdk.PROCESS_FAULT_CASES:
        if "POSIX" in platforms:
            add("fault", "qaplet-process-faults", mode.removeprefix("--"),
                [mode, "/payload/m2-guest-true.sh"], expected, mode != "--lost-pid")
    for mode, expected, platforms in sdk.IO_FAULT_CASES:
        if "POSIX" in platforms:
            add("fault", "qaplet-io-faults", mode.removeprefix("--"),
                [mode, "/payload/qaplet-io-faults.com", loader, "/payload/sdk/process-child.exe"], expected)
    for mode, expected, platforms in sdk.JS_PROCESS_FAULT_CASES:
        if "POSIX" in platforms:
            add("fault", "qaplet-js-process-faults", mode.removeprefix("--"),
                [mode, "/payload/qaplet-js-process-faults.com", loader], expected, False)
    for name, arguments, expected in sdk.STDIO_FAULT_CASES:
        add("fault", "qaplet-stdio-faults", name, arguments, expected)
    add("fault", "qaplet-aggregate-faults", "system-boundary", [], sdk.AGGREGATE_FAULT_STDOUT)
    for fault, cases in ((False, sdk_tty.NORMAL + sdk_tty.POSIX), (True, sdk_tty.FAULT + sdk_tty.POSIX_FAULT)):
        artifact = "qaplet-tty-faults" if fault else "qaplet-tty-host"
        for mode, expected in cases:
            add("tty", artifact, mode.removeprefix("--"), [mode], expected,
                terminal="tty-native.ndjson" if mode in ("--m2-tty-lease", "--m2-tty-join-js") else None,
                alias=mode == "--m2-tty-controlling-alias")
    for fault, names in ((False, sdk_mixed.NORMAL), (True, sdk_mixed.FAULT)):
        artifact = "qaplet-mixed-faults" if fault else "qaplet-mixed-host"
        for name in names:
            expected = sdk_mixed.PENDING if "nonreturning" in name else sdk_mixed.UNKNOWN if "uncertain" in name else sdk_mixed.STOP
            add("mixed", artifact, name, ["--m2-mixed-" + name, "/payload/" + artifact + ".com", loader,
                                        "/payload/sdk/process-child.exe"], None,
                not any(word in name for word in ("nonreturning", "uncertain")),
                expected_marker=expected, terminal="mixed-terminal.ndjson" if name.startswith("tty") else None)
    return rows


def inspect_capture(raw, terminal=None):
    """Decode a completed capture; an exit code never replaces native facts."""
    lines = raw.decode("ascii").splitlines()
    if not lines or lines[0] != "M2G 1" or lines[-1] != "END":
        raise ValueError("Incomplete M2 guest capture")
    values, sidecars = {}, {}
    for line in lines[1:-1]:
        key, value = line.split(" ", 1)
        if key == "SIDE":
            name, data = value.split(" ", 1)
            if name in sidecars:
                raise ValueError("Duplicate guest sidecar")
            sidecars[name] = base64.b64decode(data, validate=True)
        else:
            if key not in ("CWD", "EXIT", "STDOUT", "STDERR") or key in values:
                raise ValueError("Unknown or duplicate M2 guest capture field")
            values[key] = value
    if set(values) != {"CWD", "EXIT", "STDOUT", "STDERR"}:
        raise ValueError("Incomplete M2 guest capture fields")
    result = {"cwd": values["CWD"], "exit_code": int(values["EXIT"]),
              "stdout": base64.b64decode(values["STDOUT"], validate=True),
              "stderr": base64.b64decode(values["STDERR"], validate=True),
              "sidecars": sidecars}
    if terminal:
        if terminal not in sidecars:
            raise ValueError("Missing terminal restoration readback")
        groups = {}
        for row in (json.loads(line) for line in sidecars[terminal].splitlines()):
            prefix, stage = row["stage"].rsplit(".", 1) if "." in row["stage"] else ("terminal", row["stage"])
            group = groups.setdefault(prefix, {})
            if stage in group or row.get("ok", True) is not True:
                raise ValueError("Duplicate or failed terminal restoration stage")
            group[stage] = {key: value for key, value in row.items() if key != "stage"}
        if not groups or any(set(group) != {"before", "modified", "restored"} or
                             group["before"] != group["restored"] or
                             group["before"] == group["modified"] for group in groups.values()):
            raise ValueError("Full terminal restoration is not proven")
        result["terminal_restored"] = True
    return result


def supplied_consumers(evidence, references, product, report):
    """Rehash exact retained first-compile inputs and both audited APE slices."""
    result = {}
    reports = {}
    for reference in references:
        consumer = evidence.report(reference, "qaplet-sdk", ("fault-consume", "tty-consume", "mixed-consume"))
        scope = consumer["scope"].removesuffix("-consume")
        require(scope not in reports, "Duplicate M2 special consumer scope")
        original=evidence.json(consumer["producer_report"])
        require(consumer["sdk"]==original["sdk"] and consumer["sdk_recipe"]==original["sdk_recipe"] and consumer["package"]==original["package"],
                "Original special producer identity must not be retagged")
        contract = consumer["consumer_identity"]["contract"]
        require(contract["sha256"]==sdk.digest({key:value for key,value in contract.items() if key!="sha256"}), "Special source/helper contract digest differs")
        for row in consumer["consumer_identity"]["inputs"]:
            require(same(row["original"], row["retained"]), "Retained consumer helper differs from original")
            evidence.read(row["retained"])
        expected = {row["file"]: row for row in original["package"]["members"] if row["file"].startswith("sdk/")}
        retained = consumer["external_consumer"]["retained_inputs"]
        require(len(retained) == len(expected) and {row["package_member"]["file"] for row in retained} == set(expected),
                "Complete first-compile public source/header/dual archive inputs required")
        for row in retained:
            member = expected[row["package_member"]["file"]]
            require(row["package_member"] == member and same(row["original"], member) and same(row["retained"], member),
                    "Special consumer compile input differs from sealed SDK ZIP member")
            evidence.read(row["retained"])
        for command in consumer["commands"]:
            evidence.command(command, 0)
        packing = unique(consumer["packing"], "name")
        for artifact in consumer["artifacts"]:
            name = artifact["name"]
            require(name not in result and name in packing, "Duplicate or unpacked special artifact")
            original = packing[name]["original_packer_output"]
            evidence.read(original)
            evidence.read(artifact)
            sidecars = artifact["headers"]["sidecars"]
            require(len(sidecars) == 2, "Both APE CPU sidecars required")
            for entry in sidecars:
                evidence.read(entry)
            measured = headers(evidence.resolve(artifact["path"]), [evidence.resolve(entry["path"]) for entry in sidecars])
            require(without_paths(measured) == without_paths(artifact["headers"]), "Special APE headers differ from actual supplied bytes")
            require(artifact["audit"]["status"] == "pass" and not artifact["audit"]["forbidden_service_symbols"],
                    "Audited special APE required")
            for entry in artifact["audit"]["maps"]:
                evidence.read(entry)
            for command in artifact["audit"]["commands"]:
                evidence.command(command, 0)
            result[name] = artifact
        report.setdefault("auxiliary_target_bindings",{}).update(bind_auxiliary(evidence,consumer,product,special_build_units(scope),report))
        reports[scope] = consumer
    require(set(reports) == {"fault", "tty", "mixed"}, "All fault/TTY/mixed consumer supplies required")
    required_artifacts = {row["artifact"] for row in special_catalog()}
    require(set(result) == required_artifacts, "Special consumer artifact inventory differs from mandatory catalog")
    return result


def special_payload(product, distribution, artifacts, fixture_root=None):
    fixture_root=fixture_root or ROOT/"tests/fixtures/sdk"
    files = [{**identity(fixture_root / name), "guest_path": "/payload/" + name}
             for name in ("m2-guest-capture.sh", "m2-guest-true.sh")]
    for name in ("ape-aarch64.elf", "sdk/process-child.exe"):
        files.append({**identity(distribution / name), "guest_path": "/payload/" + name})
    for name, artifact in sorted(artifacts.items()):
        files.append({key: artifact[key] for key in ("path", "sha256", "size_bytes")} | {"guest_path": "/payload/" + name + ".com"})
    commands = [{"case_id": "m2-special." + row["case_id"],
                 "argv": ["/payload/m2-guest-capture.sh", "/payload/ape-aarch64.elf",
                          "/payload/" + row["artifact"] + ".com", *row["arguments"]],
                 "expected_exit_code": 0, "expected_stderr": ""} for row in special_catalog()]
    return {"schema_version": 1, "page_size": 65536, "files": files, "commands": commands}


def inspect_special(row, raw, case_id):
    capture = inspect_capture(raw, terminal=row.get("terminal"))
    require(capture["cwd"] == "/tmp/command-" + case_id, "Special capture ran outside its isolated guest cwd")
    require(capture["exit_code"] == 0 and not capture["stderr"], "Special public consumer failed or wrote stderr")
    allowed = set()
    if row["expected_stdout"] is not None:
        require(capture["stdout"] == row["expected_stdout"].encode(), "Special public consumer raw stdout differs from mandatory contract")
    if row.get("terminal"):
        allowed.add(row["terminal"])
    if row.get("alias"):
        allowed.add("tty-controlling-alias.json")
        require("tty-controlling-alias.json" in capture["sidecars"], "Missing actual controlling terminal identity")
        alias = json.loads(capture["sidecars"]["tty-controlling-alias.json"])
        a, b, c = (alias[key] for key in ("pts", "dev_tty", "unrelated"))
        require((a["dev"], a["ino"], a["rdev"]) != (b["dev"], b["ino"], b["rdev"]) and
                a["tcgetsid"] > 0 and a["tcgetsid"] == b["tcgetsid"] and c["tcgetsid"] != a["tcgetsid"] and
                alias["alias_conflict_code"] == 4 and alias["unrelated_lease_code"] == alias["same_terminal_join_code"] == 0 and
                alias["full_restore_readback"] is True, "Actual controlling alias ownership/restoration facts differ")
    if "expected_marker" in row:
        allowed.update(("mixed-result.txt", "mixed-waits.ndjson", "mixed-child.json"))
        require(capture["sidecars"].get("mixed-result.txt") == row["expected_marker"].encode(), "Missing or changed mixed cleanup marker")
        require("mixed-waits.ndjson" in capture["sidecars"], "Missing actual quiesce wait observations")
        require("mixed-child.json" in capture["sidecars"], "Missing independent native child READY identity")
        child = json.loads(capture["sidecars"]["mixed-child.json"])
        require(type(child["pid"]) is int and child["pid"] > 1 and type(child["ppid"]) is int and child["ppid"] > 1 and
                child["native_ready"] is True and child["owned_live_child_waitpid_nohang"] == 0 and child["stdout_ready_bytes"] == 0,
                "Native mixed child must remain owned/live after independent READY without ending stdout read")
        waits = [json.loads(line) for line in capture["sidecars"]["mixed-waits.ndjson"].splitlines()]
        require([item["requested_active_wait_ms"] for item in waits] == [0, 5, 5] and
                all(type(item["observed_call_ms"]) in (int, float) and item["observed_call_ms"] >= 0 for item in waits),
                "Mixed zero/finite/retry quiesce observations differ")
    require(set(capture["sidecars"]) == allowed, "Missing or unknown special native sidecar evidence")
    return {"case_id": row["case_id"], "status": "pass", "required": True,
            "native_cleanup_complete": row["native_cleanup_complete"],
            "controlled_system_fixture": row["controlled_system_fixture"],
            "terminal_restored": capture.get("terminal_restored"),
            "sidecars": {name: {"size_bytes": len(data), "sha256": __import__("hashlib").sha256(data).hexdigest()}
                         for name, data in capture["sidecars"].items()}}


def export(args, report):
    require(args.build_report and args.distribution and len(args.consumer_report) == 3 and args.manifest and args.selection,
            "export requires fixed producer/distribution, three consumer reports, manifest and selection")
    require(not args.manifest.exists() and not args.selection.exists(), "Preserve existing guest selections; use new paths")
    evidence = Evidence({}, ROOT)
    try:
        product = sdk_guest.producer(evidence, str(args.build_report.resolve()), args.distribution.resolve(), report)
        product = {**product, "_reference": str(args.build_report.resolve())}
        artifacts = supplied_consumers(evidence, list(map(lambda path: str(path.resolve()), args.consumer_report)), product, report)
        frozen=args.report.resolve().parent/"payload-inputs"/report["report_id"]
        frozen.mkdir(parents=True)
        fixture_inputs=[]
        for name in ("m2-guest-capture.sh","m2-guest-true.sh"):
            source=ROOT/"tests/fixtures/sdk"/name;retained=frozen/name;shutil.copyfile(source,retained)
            require(same(identity(source),identity(retained)),"Fixture changed during export snapshot")
            fixture_inputs.append({"source":identity(source),"retained":identity(retained)})
        exporter=frozen/"exporter.py";shutil.copyfile(Path(__file__),exporter)
        payload = special_payload(product, args.distribution.resolve(), artifacts, frozen)
        write_json(args.manifest, payload)
        selection = {"schema_version": 1, "profile": "m2-special-guest", "producer_report": identity(args.build_report),
                     "distribution_root": str(args.distribution.resolve()), "consumer_reports": [identity(path) for path in args.consumer_report],
                     "sdk_sha256": product["sdk"]["sha256"], "recipe_sha256": product["sdk_recipe"]["sha256"],
                     "manifest": identity(args.manifest), "fixture_inputs":fixture_inputs,
                     "exporter":{"original":identity(Path(__file__)),"retained":identity(exporter)}}
        write_json(args.selection, selection)
        report.update(status="pass", reason="50 current special contracts exported; actual guest execution remains required",
                      manifest=identity(args.manifest), selection=identity(args.selection), command_count=50)
    finally:
        report["evidence_index"] = list(evidence.index.values())


def consume(args, report):
    require(args.selection and args.guest_report, "verify requires selection and actual guest report")
    evidence = Evidence({}, ROOT)
    try:
        selected = evidence.json(str(args.selection.resolve()))
        require(selected["schema_version"] == 1 and selected["profile"] == "m2-special-guest", "Versioned M2 special guest selection required")
        require(same(selected["exporter"]["original"],selected["exporter"]["retained"]),"Export helper snapshot differs")
        evidence.read(selected["exporter"]["retained"])
        for row in selected["fixture_inputs"]:
            require(same(row["source"],row["retained"]),
                    "Exported guest fixture differs from its immutable pre-execution snapshot")
            evidence.read(row["retained"])
        product = sdk_guest.producer(evidence, selected["producer_report"], evidence.resolve(selected["distribution_root"]), report)
        product = {**product, "_reference": selected["producer_report"]["path"]}
        require(selected["sdk_sha256"] == product["sdk"]["sha256"] and selected["recipe_sha256"] == product["sdk_recipe"]["sha256"], "M2 special selection differs from product")
        artifacts = supplied_consumers(evidence, selected["consumer_reports"], product, report)
        frozen=evidence.resolve(selected["fixture_inputs"][0]["retained"]["path"]).parent
        expected = special_payload(product, evidence.resolve(selected["distribution_root"]), artifacts, frozen)
        original = evidence.json(selected["manifest"])
        require(original == expected, "Exported M2 special payload differs from authoritative fixed inputs/catalog")
        guest = evidence.report(str(args.guest_report.resolve()), "arm64-guest")
        check_ci_commit(product, guest)
        files, cases = verify_guest_transport(evidence, guest, report)
        require(guest["guest"]["page_size"] == 65536, "M2 requires actual 65536 kernel pages")
        executed = evidence.json(guest["manifest"])
        require(same(guest["manifest"], selected["manifest"]) or selected["manifest"] in executed.get("input_manifests", []),
                "Actual guest did not execute the selected M2 manifest")
        commands = unique(executed["commands"])
        required = unique(expected["commands"])
        require({key for key in cases if key.startswith("m2-special.")} == set(required), "Missing/unknown mandatory special cases")
        for entry in expected["files"]:
            require(entry["guest_path"] in files and same(files[entry["guest_path"]], entry), "Special payload transport bytes differ")
            evidence.read(entry)
        results = []
        for row in special_catalog():
            case_id = "m2-special." + row["case_id"]
            case = cases[case_id]
            require(commands[case_id] == required[case_id] and case["argv"] == required[case_id]["argv"] and
                    case["cwd"] == "/" and case["exit_code"] == 0, "Special invocation/exit differs from selected catalog")
            require(case["expected"] == {"expected_exit_code": 0, "expected_stderr": ""}, "Special transport expected contract differs")
            start, end = interval(case)
            require((end - start).total_seconds() <= case["timeout_seconds"], "Special command exceeded deadline")
            result = inspect_special(row, evidence.read(case["stdout_log"]), case_id)
            require(not evidence.read(case["stderr_log"]), "Guest capture wrapper failed")
            results.append(result)
        report.update(status="pass", reason="50 mandatory special contracts and their native sidecars verified in actual ARM64/65536 guest",
                      evidence_verified=True, cases=results, guest=guest["guest"], execution_mode="emulated-system",
                      sdk_sha256=product["sdk"]["sha256"], producer_report=selected["producer_report"],
                      guest_report=identity(args.guest_report), selection=identity(args.selection),
                      scope_notes="Special M2 scope only; ordinary/backend/deep ABI/cycles and native targets remain separate")
    finally:
        report["evidence_index"] = list(evidence.index.values())


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("operation", choices=("export", "verify"))
    for name in ("build-report", "distribution", "manifest", "selection", "guest-report"):
        parser.add_argument("--" + name, type=Path)
    parser.add_argument("--consumer-report", type=Path, action="append", default=[])
    parser.add_argument("--report", type=Path, required=True)
    args = parser.parse_args()
    outputs = [args.report, *([args.manifest, args.selection] if args.operation == "export" else [])]
    inputs = [args.build_report, *args.consumer_report, *([args.selection, args.guest_report] if args.operation == "verify" else [])]
    try:
        guard_evidence_outputs(outputs, [path for path in inputs if path], [args.distribution] if args.distribution else [])
        require(all(not path.exists() for path in outputs), "Existing evidence outputs are immutable; choose new paths")
    except ValueError as error:
        parser.error(str(error))
    report = {"schema_version": 1, "report_kind": "m2-special-guest", "scope": args.operation,
              "report_id": str(uuid.uuid4()), "started_at": now(), "status": "not_run", "ci": ci_facts(),
              "evidence_verified": False, "release_ready": False,"evaluator":identity(Path(__file__))}
    try:
        (export if args.operation == "export" else consume)(args, report)
    except BuildError as error:
        report.update(status=error.status, reason=str(error))
    except (OSError, ValueError, KeyError, TypeError) as error:
        report.update(status="fail", reason=str(error))
    report["finished_at"] = now()
    write_evidence_report(args.report, report)
    print(json.dumps({key: report[key] for key in ("status", "reason", "evidence_verified")}))
    return 0 if report["status"] == "pass" else 1


if __name__ == "__main__":
    sys.exit(main())
