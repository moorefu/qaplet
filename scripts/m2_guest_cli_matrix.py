#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Export and verify the additional POSIX CLI contracts in the locked 64K guest.

The ordinary SDK manifest already owns the 53 basic CLI cases. This module owns
14 IO/terminal cases and 16 signals/cleanup cases, including the two public C
relay Host cases whose scope deliberately is not CLI end-to-end.
"""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import sys
import uuid

from build_ape import BuildError, headers, identity
from core_gate import Evidence, interval, require, same, unique, verify_guest_transport, without_paths
from foundation import ROOT, now
from input_lock import consume_identity
from nightly_toolchain import write_json
from startup import check_ci_commit, ci_facts
from workflow_support import guard_evidence_outputs, write_evidence_report
from m2_fixture_binding import bind_auxiliary, verify_link_inputs
from sdk_m2_guest import inspect_capture
import sdk
import sdk_guest
import sdk_signal


LOADER = "/payload/ape-aarch64.elf"
ROUTES = ("file-entry-copy", "file-binary-background", "pipe-binary-background",
          "large-result-restored", "final-output-broken", "final-output-invalid",
          "process-dual-channel", "ordinary-process-error", "background-allocation", "sdk-results")


def catalog():
    rows = [{"case_id": "m2-cli.route." + mode, "kind": "route", "mode": mode,
             "native_cleanup_complete": True} for mode in ROUTES]
    rows += [{"case_id": "m2-cli.terminal." + mode, "kind": "terminal", "mode": mode,
              "native_cleanup_complete": True} for mode in ("normal", "error", "budget", "unicode")]
    for signal in ("SIGINT", "SIGTERM"):
        for mode in ("wait", "execute", "output", "help", "queue", "strong", "mixed"):
            rows.append({"case_id": "m2-cli.signal." + signal + "." + mode, "kind": "signal",
                         "mode": mode, "signal": signal,
                         "native_cleanup_complete": None if mode == "strong" else True,
                         "scope": "public SDK relay Host, not CLI end-to-end" if mode == "queue" else
                                  "source-compiled native cleanup observer" if mode == "mixed" else "supplied CLI"})
    rows += [{"case_id": "m2-cli.signal." + signal + "." + mode, "kind": "signal", "mode": mode,
              "signal": signal, "native_cleanup_complete": mode == "late",
              "scope": "controlled native thread retirement" if mode == "budget" else "controlled post-destroy late native hint"}
             for signal, mode in (("SIGINT", "budget"), ("SIGTERM", "late"))]
    return rows


def invocation(row):
    cli, host = "/payload/qaplet.com", "/payload/qaplet-cli-host.com"
    if row["kind"] == "terminal":
        return [LOADER, host, "--m2-cli-terminal", cli, host, LOADER,
                "/payload/sdk/modules/cli-terminal.mjs", row["mode"]]
    if row["kind"] == "route":
        arguments = ["--route", row["mode"], cli, LOADER, host]
    else:
        mode = row["mode"]
        if mode in ("budget", "late", "mixed"):
            cli = "/payload/signal-" + {"budget": "fault", "late": "late", "mixed": "observer"}[mode] + ".com"
        helper = "/payload/signal-host.com" if mode in ("queue", "mixed") else "/payload/signal-arm.com"
        arguments = ["--signal", cli, LOADER, helper, row["signal"], mode]
    return [LOADER, "/payload/sdk-guest-cli.com", *arguments]


def ndjson(raw):
    return [json.loads(line) for line in raw.splitlines()]


def terminal_readback(raw, streams=False):
    rows = ndjson(raw)
    groups = {}
    for row in rows:
        key = row.get("stream", 0) if streams else 0
        group = groups.setdefault(key, {})
        stage = row["stage"]
        require(stage in ("before", "modified", "restored") and stage not in group,
                "Duplicate or unknown full terminal readback stage")
        group[stage] = {key: value for key, value in row.items() if key != "stage"}
    require(set(groups) == ({0, 1, 2} if streams else {0}), "Missing terminal stream readback")
    require(all(set(group) == {"before", "modified", "restored"} and
                group["before"] == group["restored"] and group["before"] != group["modified"]
                for group in groups.values()), "Full terminal modes and flags were not restored")
    return rows


def native_identity(sidecars, prefix, actual):
    original = json.loads(sidecars[prefix + "-native-identity.json"])
    status = sidecars[prefix + "-native-status.txt"].decode()
    require(original["child_pid"] == actual["direct_pid"] > 1 and
            original["expected_owner_pid"] == original["actual_parent_pid"] == actual["owner_pid"] > 1 and
            original["native_exe"] == original["expected_loader"] == LOADER and
            original["kill_zero_return"] == original["kill_zero_errno"] == 0 and original["identity_matched"] is True,
            "Independent native child PID/PPid/loader/liveness identity differs")
    fields = dict(line.split(":", 1) for line in status.splitlines() if ":" in line)
    require(int(fields["Pid"]) == actual["direct_pid"] and int(fields["PPid"]) == actual["owner_pid"],
            "Original proc status does not bind native child identity")
    return original


def inspect_cli_capture(row, raw):
    capture = inspect_capture(raw)
    require(capture["cwd"] == "/tmp/command-" + row["case_id"] and capture["exit_code"] == 0 and not capture["stderr"],
            "CLI controller failed or ran outside its isolated guest cwd")
    side = capture["sidecars"]
    if row["kind"] == "terminal":
        require(not capture["stdout"], "Unexpected terminal Host stdout outside owned PTY")
        restored = terminal_readback(side["cli-terminal.ndjson"], streams=True)
        child = json.loads(side["cli-child-status.json"])
        require(child["reaped"] == 1 and child["exited"] == 1 and child["helper_active"] == 0 and
                child["signal"] == 0 and child["exit_code"] == (1 if row["mode"] in ("error", "budget") else 0),
                "Actual terminal CLI child retirement differs")
        require(side["cli-terminal-result.txt"] == b"CLI terminal: native modes/flags restored; actual child reaped; owner cleanup complete\n", "Terminal completion marker differs")
        output = side["cli-pty.stdout.log"]
        require(b"native cleanup incomplete" not in output and b"standard stream restoration incomplete" not in output,
                "Terminal CLI reported incomplete cleanup")
        if row["mode"] == "unicode":
            require("A中🙂Z".encode() in output, "Actual terminal Unicode round trip missing")
        if row["mode"] == "error":
            require(b"background terminal failure" in output, "Actual terminal script error missing")
        if row["mode"] == "budget":
            require(b"qaplet: code=18 DISPATCH_LIMIT" in output, "Actual terminal dispatch budget missing")
        require(set(side) <= {"cli-terminal.ndjson", "cli-child-status.json", "cli-terminal-result.txt", "cli-pty.stdout.log", "cli-startup.ndjson", "text.ready"},
                "Unknown terminal evidence sidecar")
        return {**row, "status": "pass", "terminal_readback": restored, "child_status": child}
    controller = json.loads(capture["stdout"])
    require(controller["case_id"] == row["case_id"].removeprefix("m2-") and controller["status"] == "pass",
            "Controller identity or completion failed")
    actual = controller["actual"]
    require(actual["supervisor_timeout"] is False, "Controller supervisor killed an incomplete case")
    output, error = side["cli.stdout"], side.get("cli.stderr", b"")
    mode = row["mode"]
    allowed = {"cli.stdout", "cli.stderr"}
    if row["kind"] == "route":
        expected_code = 1 if mode in ("final-output-broken", "final-output-invalid", "background-allocation") else 0
        require(actual["exit_code"] == expected_code and actual["full_flags_restored"] is True and
                actual["before_flags"] == actual["restored_flags"], "Actual CLI exit or borrowed stdio flags differ")
        if mode in ("file-binary-background", "pipe-binary-background", "process-dual-channel"):
            data = bytes(range(256)) * (512 if mode == "process-dual-channel" else 257)
            require(output == data + b"ENTRY\n" and error == data, "Actual binary stdin/stdout/stderr bytes differ")
        elif mode == "file-entry-copy":
            require(output == "B\0Zentry\0中\n".encode() and not error, "Entry copy/embedded NUL/Unicode bytes differ")
        elif mode == "large-result-restored":
            require(output == b"X" * 200000 + b"\n" and not error, "Large asynchronous final output differs")
        elif mode == "ordinary-process-error":
            require(output == b"42\n" and not error, "Ordinary recoverable process refusal differs")
        elif mode == "sdk-results":
            require(output == next(value for name, _, value in sdk.HOST_CASES if name == "m2-cli-results").encode() and not error,
                    "Public SDK entry result contrast differs")
        else:
            require(not output and (b"qaplet: code=11 MEMORY_LIMIT" if mode == "background-allocation" else b"qaplet: code=30 IO_ERROR") in error,
                    "Controlled ordinary CLI IO/allocation error differs")
        if mode in ("pipe-binary-background", "large-result-restored"):
            require(actual["producer_and_readers_joined"] is True, "Actual pipe writer/readers not joined")
        require(b"cleanup incomplete" not in error and b"standard stream restoration incomplete" not in error,
                "CLI route cleanup incomplete")
    else:
        number = 2 if row["signal"] == "SIGINT" else 15
        require("controller-signals.ndjson" in side, "Missing original real OS signal trace")
        signals = ndjson(side["controller-signals.ndjson"])
        count = 6 if mode == "budget" else 1
        expected_signals = [2, 2, 15, 2, 15, 2] if mode == "budget" else [number]
        require(len(signals) == count and [item["signal"] for item in signals] == expected_signals and
                all(item["sender_pid"] > 1 and item["sender_pid"] != actual["owner_pid"] and
                item["target_pid"] == actual["owner_pid"] > 1 and
                item["return_value"] == item["errno"] == 0 and item["monotonic_ms"] > 0 for item in signals),
                "Real OS signal sender/owner/delivery facts differ")
        allowed.add("controller-signals.ndjson")
        require(actual["ready"] is True and actual["signal"] == number and actual["signal_return"] == 0 and actual["exit_code"] == 1,
                "Actual ready-before-signal/first cause/exit differs")
        require(b"AFTER" not in output and b"EXECUTION_STOPPED" in output + error, "Interrupted JS continued or first cause differs")
        if mode in ("wait", "execute", "queue", "strong", "budget", "late"):
            require(output == b"READY\n", "Signal ready bytes differ")
        if mode == "output":
            require(0 < len(output) < 200001 and output == b"X" * len(output), "Final result was not interrupted during actual partial output")
        if mode in ("help", "output"):
            require(0 < actual["pipe_capacity_bytes"] <= 65536 and (mode == "help" or actual["first_read_bytes"] > 0) and
                    any(word in actual["sender_observed_wchan"] for word in ("poll", "pipe_write")),
                    "Signal was not sent during observed real blocked CLI output")
        if mode == "queue":
            allowed.add("queue-result.json")
            queue = json.loads(side["queue-result.json"])
            require(all(queue[key] is True for key in ("queue_full", "owner_stopped", "sdk_destroyed", "sdk_unix_dispositions_preserved")) and
                    queue["user_marks"] == 0 and queue["events_dropped"] == 2, "Public SDK full-queue relay/stop semantics differ")
        if mode == "strong":
            allowed.update(("strong-child.json", "strong-child-arm", "strong-native-status.txt", "strong-native-identity.json"))
            native_identity(side, "strong", actual)
            child = json.loads(side["strong-child.json"])
            require(child["native_pid"] == actual["direct_pid"] and child["native_ppid_posix_only"] == actual["owner_pid"] and
                    child["arm_is_blocked_until_host_verified_identity"] is True and
                    actual["strong_native_identity_before_ready"] is True and actual["strong_child_physically_exited"] is True and
                    actual["sdk_all_native_cleanup_directly_observed"] is None, "Strong-stop physical/native cleanup scope differs")
        if mode == "budget":
            allowed.add("signal-fault.ndjson")
            waits = ndjson(side["signal-fault.ndjson"])
            active = [item["duration_ns"] / 1e6 for item in waits if item["operation"] == "active-cond-wait"]
            require(any(item["operation"] == "thread-exit-held" for item in waits) and len(active) >= 2 and
                    sum(value > 500 for value in active) >= 2 and 4500 <= sum(active) <= 5500 and
                    abs(sum(active) - actual["cumulative_active_wait_ms"]) < 0.1 and
                    4500 <= actual["elapsed_after_signal_ms"] <= 6500 and actual["signals_attempted"] == 6 and
                    actual["native_entry_or_exit_held"] is True and actual["sdk_all_native_cleanup_directly_observed"] is False and
                    b"native cleanup incomplete" in error, "Cumulative 5s native retirement budget/retention facts differ")
        elif mode == "late":
            allowed.update(("signal-late.ndjson", "late-arm"))
            late = ndjson(side["signal-late.ndjson"])
            final = [item for item in late if item["operation"] == "post-destroy-late-entry"]
            require(len(final) == 1 and final[0]["destroy_code"] == 0 and final[0]["replacement_identity_reused"] is False and
                    all(final[0][key] is True for key in ("original_target_identity_preserved", "late_entry_completed", "replacement_objects_untouched", "future_entry_delivered")) and
                    actual["native_entry_or_exit_held"] is True and actual["sdk_all_native_cleanup_directly_observed"] is True,
                    "Late native hint touched a replacement or failed to retire after destroy")
        elif mode == "mixed":
            allowed.discard("cli.stderr")
            allowed.update(("direct-child.json", "mixed-ready", "mixed-native-status.txt", "mixed-native-identity.json", "cli-mixed-terminal.ndjson", "signal-native.ndjson"))
            native_identity(side, "mixed", actual)
            terminal_readback(side["cli-mixed-terminal.ndjson"])
            facts = ndjson(side["signal-native.ndjson"])
            reaped = [item for item in facts if item["operation"] == "native-child-reaped"]
            require(len(reaped) == 1 and reaped[0]["pid"] == actual["direct_pid"] and reaped[0]["signal"] == 9 and
                    json.loads(side["direct-child.json"])["pid"] == actual["direct_pid"], "Mixed direct child was not uniquely SIGKILL-reaped by SDK")
            require(any(item["operation"] == "mixed-tty-read-accepted" and item["processes"] == 1 and item["operations"] >= 1 and item["io_endpoints"] >= 4 for item in facts) and
                    any(item["operation"] == "owner-close" and item["code"] == 0 and item["termination_reason"] == 23 for item in facts) and
                    any(item["operation"] == "native-quiesce" and item["code"] == 0 and item["reason"] == 6 for item in facts) and
                    facts[-1] == {"operation": "runtime-destroy", "code": 0} and
                    all(actual[key] is True for key in ("native_identity_before_stop", "full_modes_and_flags_restored", "terminal_reader_joined", "sdk_native_reap_close_quiesce_destroy_observed", "native_cleanup_complete")),
                    "Mixed accepted IO/terminal/child first cause and cleanup facts differ")
        if mode != "budget":
            require(b"cleanup incomplete" not in output + error and b"standard stream restoration incomplete" not in output + error,
                    "Signal cleanup incomplete")
    require(set(side) == allowed, "Missing or unknown raw CLI evidence sidecar: " + str(set(side) ^ allowed))
    return {**row, "status": "pass", "actual": actual,
            "raw_sidecars": {key: {"sha256": hashlib.sha256(value).hexdigest(), "size_bytes": len(value)} for key, value in side.items()}}


def verify_artifact(evidence, artifact):
    evidence.read(artifact)
    for item in artifact["headers"]["sidecars"] + artifact["audit"]["maps"]:
        evidence.read(item)
    measured = headers(evidence.resolve(artifact["path"]), [evidence.resolve(item["path"]) for item in artifact["headers"]["sidecars"]])
    require(without_paths(measured) == without_paths(artifact["headers"]) and artifact["audit"]["status"] == "pass" and
            not artifact["audit"]["forbidden_service_symbols"], "Actual CLI fixture fat APE audit differs")


def supplies(evidence, selected, product, report):
    controller = evidence.json(selected["controller_report"])
    require(controller["report_kind"] == "sdk-guest-cli-controller" and controller["status"] == "pass", "Successful independent controller producer required")
    consume_identity(controller, report)
    require(controller["build"]["flags"] == ["-Os", "-D_GNU_SOURCE"] and controller["build"]["public_sdk_archive"] == "not linked" and
            same(controller["source"]["original"], controller["source"]["retained"]) and
            same(controller["source"]["retained"], identity(ROOT / "tests/native/sdk_guest_cli.c")), "Controller actual retained C/flags/scope differ")
    evidence.read(controller["source"]["retained"])
    require(controller["toolchain"]["archive"] == product["toolchain"]["archive"] and
            same(controller["portable_helpers"]["loader"], product["portable_helpers"]["loader"]), "Controller toolchain/loader differs from fixed supply")
    for command in controller["commands"]:
        evidence.command(command, 0)
    compiler = controller["native_toolchain"]["tree"] + "/bin/cosmocc"
    compiles = [item for item in controller["commands"] if item["argv"][0] == compiler and "-c" in item["argv"]]
    require(len(compiles) == 1, "One actual controller compile required")
    argv = compiles[0]["argv"]
    obj = argv[-1]
    require(argv == [compiler, "-Os", "-D_GNU_SOURCE", "-c", controller["source"]["retained"]["path"], "-o", obj], "Unexpected controller C compiler inputs")
    links = [item for item in controller["commands"] if item["argv"][0] == compiler and "-o" in item["argv"] and item["argv"][item["argv"].index("-o") + 1].endswith("/sdk-guest-cli.com")]
    require(len(links) == 1, "One actual controller object link required")
    verify_link_inputs(links[0]["argv"], compiler, [obj], None, ())
    result = {"sdk-guest-cli": controller["artifacts"][0]}
    source = evidence.json(selected["controller_supply"])
    require(source["status"] == "pass" and same(source["producer"], selected["controller_report"]), "First-compile controller adapter receipt differs")
    for item in source["inputs"]:
        require(same(item["original"], item["retained"]), "First-compile controller input snapshot differs")
        evidence.read(item["retained"])
    for consumer_name in ("cli", "signals"):
        consumer = evidence.json(selected[consumer_name + "_report"])
        require(consumer["status"] == "pass", "Successful auxiliary CLI producer required")
        if consumer_name == "cli":
            units = {"qaplet-cli-host": {"units": ["sdk/host-cli.c"], "wrapped_symbols": []}}
        else:
            units = {name: {"units": (["recipe:src/cli.c"] if name not in ("signal-arm", "signal-host") else []) + ["sdk/" + source],
                            "wrapped_symbols": list(wraps), "sdk_archive": name != "signal-arm"}
                     for name, (source, wraps) in sdk_signal.MODES.items()}
        report.setdefault("auxiliary_target_bindings", {}).update(bind_auxiliary(evidence, consumer, product, units, report))
        for item in consumer["artifacts"]:
            require(item["name"] not in result, "Duplicate CLI fixture artifact")
            result[item["name"]] = item
        for item in consumer["packing"]:
            evidence.read(item["original_packer_output"])
    for item in controller["packing"]:
        evidence.read(item["original_packer_output"])
    for item in result.values():
        verify_artifact(evidence, item)
    return result


def payload(distribution, artifacts, fixture):
    files = [{**identity(fixture), "guest_path": "/payload/m2-guest-capture.sh"}]
    files += [{**identity(distribution / name), "guest_path": "/payload/" + name}
              for name in ("ape-aarch64.elf", "qaplet.com", "sdk/modules/cli-terminal.mjs")]
    files += [{key: item[key] for key in ("path", "sha256", "size_bytes")} | {"guest_path": "/payload/" + name + ".com"}
              for name, item in sorted(artifacts.items())]
    return {"schema_version": 1, "page_size": 65536, "files": files,
            "commands": [{"case_id": row["case_id"], "argv": ["/payload/m2-guest-capture.sh", *invocation(row)],
                          "expected_exit_code": 0, "expected_stderr": ""} for row in catalog()]}


def evaluate(args, report):
    evidence = Evidence({}, ROOT)
    try:
        if args.operation == "export":
            for path in (args.manifest, args.selection):
                require(path and not path.exists(), "New manifest/selection paths required")
            selected = {"profile": "m2-cli-guest", "producer_report": identity(args.build_report),
                        "distribution_root": str(args.distribution.resolve()),
                        **{name + "_report": identity(getattr(args, name + "_report")) for name in ("controller", "cli", "signals")},
                        "controller_supply": identity(args.controller_report.parent / "supply.json")}
        else:
            selected = evidence.json(str(args.selection.resolve()))
        require(selected["profile"] == "m2-cli-guest", "Explicit CLI guest selection required")
        product = sdk_guest.producer(evidence, selected["producer_report"], evidence.resolve(selected["distribution_root"]), report)
        artifacts = supplies(evidence, selected, product, report)
        if args.operation == "export":
            frozen = args.report.resolve().parent / "cli-payload-inputs" / report["report_id"]
            frozen.mkdir(parents=True)
            fixture = frozen / "m2-guest-capture.sh"
            shutil.copyfile(ROOT / "tests/fixtures/sdk/m2-guest-capture.sh", fixture)
            exporter = frozen / "exporter.py"
            shutil.copyfile(Path(__file__), exporter)
            selected["fixture"] = {"original": identity(ROOT / "tests/fixtures/sdk/m2-guest-capture.sh"), "retained": identity(fixture)}
            selected["exporter"] = {"original": identity(Path(__file__)), "retained": identity(exporter)}
        for key in ("fixture", "exporter"):
            require(same(selected[key]["original"], selected[key]["retained"]), "Retained CLI export input differs")
            evidence.read(selected[key]["retained"])
        expected = payload(evidence.resolve(selected["distribution_root"]), artifacts, evidence.resolve(selected["fixture"]["retained"]["path"]))
        if args.operation == "export":
            write_json(args.manifest, expected)
            selected["manifest"] = identity(args.manifest)
            write_json(args.selection, selected)
            report.update(status="pass", reason="30 additional applicable POSIX CLI contracts exported; actual guest execution remains required", manifest=identity(args.manifest), selection=identity(args.selection))
            return
        require(evidence.json(selected["manifest"]) == expected, "CLI payload differs from selected exact sources/catalog")
        guest = evidence.report(str(args.guest_report.resolve()), "arm64-guest")
        check_ci_commit(product, guest)
        files, cases = verify_guest_transport(evidence, guest, report)
        require(guest["guest"]["page_size"] == 65536, "Actual ARM64/65536 kernel pages required")
        executed = evidence.json(guest["manifest"])
        require(same(guest["manifest"], selected["manifest"]) or selected["manifest"] in executed.get("input_manifests", []), "Selected CLI manifest was not actually executed")
        commands = unique(executed["commands"])
        required = unique(expected["commands"])
        require({key for key in cases if key.startswith("m2-cli.")} == set(required), "Missing or unknown mandatory CLI cases")
        for item in expected["files"]:
            require(item["guest_path"] in files and same(files[item["guest_path"]], item), "CLI payload transport bytes differ")
            evidence.read(item)
        results = []
        for row in catalog():
            case = cases[row["case_id"]]
            require(commands[row["case_id"]] == required[row["case_id"]] and case["argv"] == required[row["case_id"]]["argv"] and
                    case["cwd"] == "/" and case["exit_code"] == 0 and case["expected"] == {"expected_exit_code": 0, "expected_stderr": ""},
                    "CLI invocation differs from selected source/catalog")
            start, end = interval(case)
            require((end - start).total_seconds() <= case["timeout_seconds"] and not evidence.read(case["stderr_log"]), "CLI capture/deadline failed")
            results.append(inspect_cli_capture(row, evidence.read(case["stdout_log"])))
        report.update(status="pass", reason="14 additional CLI IO/terminal and 16 actual signal/cleanup contracts verified on ARM64/65536",
                      evidence_verified=True, cases=results, guest_report=identity(args.guest_report), selection=identity(args.selection),
                      sdk_sha256=product["sdk"]["sha256"], guest=guest["guest"], execution_mode="emulated-system",
                      scope_notes="53 basic CLI cases are separate ordinary SDK guest evidence; queue2 covers public SDK relay Host, strong2 preserves unobserved SDK-internal cleanup as null")
    finally:
        report["evidence_index"] = list(evidence.index.values())


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("operation", choices=("export", "verify"))
    for name in ("build-report", "distribution", "controller-report", "cli-report", "signals-report", "manifest", "selection", "guest-report"):
        parser.add_argument("--" + name, type=Path)
    parser.add_argument("--report", type=Path, required=True)
    args = parser.parse_args()
    inputs = [args.build_report, args.controller_report, args.cli_report, args.signals_report]
    if args.operation == "verify":
        inputs += [args.selection, args.guest_report]
    outputs = [args.report] + ([args.manifest, args.selection] if args.operation == "export" else [])
    require(all(path is not None for path in outputs), "All output paths required")
    guard_evidence_outputs(outputs, [path for path in inputs if path], [args.distribution] if args.distribution else [])
    require(all(not path.exists() for path in outputs), "Existing evidence outputs are immutable; choose new paths")
    report = {"schema_version": 1, "report_kind": "m2-cli-guest", "scope": args.operation, "report_id": str(uuid.uuid4()),
              "started_at": now(), "status": "not_run", "ci": ci_facts(), "evidence_verified": False,
              "release_ready": False, "evaluator": identity(Path(__file__))}
    try:
        evaluate(args, report)
    except BuildError as error:
        report.update(status=error.status, reason=str(error))
    except (OSError, ValueError, TypeError, KeyError) as error:
        report.update(status="fail", reason=str(error))
    report["finished_at"] = now()
    write_evidence_report(args.report, report)
    print(json.dumps({key: report[key] for key in ("status", "reason", "evidence_verified")}))
    return report["status"] != "pass"


if __name__ == "__main__":
    sys.exit(main())
