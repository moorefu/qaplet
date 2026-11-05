#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Explicit M2 native inventory and read-only join of current supplied evidence."""
import argparse
import json
from pathlib import Path
import shutil
import sys
from types import SimpleNamespace
import uuid

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))

TARGETS = (("linux-x86_64", "Linux", "x86_64"), ("windows-x86_64", "Windows", "x86_64"),
           ("macos-x86_64", "Darwin", "x86_64"), ("macos-aarch64", "Darwin", "aarch64"),
           ("linux-aarch64", "Linux", "aarch64"))
MANDATORY = ("normal", "examples", "cli", "signal-sender", "signal-strong", "signal-queue", "signal-mixed", "signal-budget", "signal-late", "fault", "tty", "mixed",
             "backend-waker", "backend-process", "backend-stdio", "backend-tty", "m2-lifecycle", "m2-mixed-cycles")
ACCEPTED_23 = "14e3ba4cb74bc08a1a4ca28297c975764fc12785ad3df52fca56345240f2825a"
SCOPES = {
    "normal": {("qaplet-sdk", "run")},
    "examples": {("qaplet-sdk", "example-consume"), ("qaplet-sdk", "run")},
    "cli": {("qaplet-sdk", "cli-consume"), ("qaplet-sdk-cli", "run")},
    "fault": {("qaplet-sdk", "fault-consume"), ("qaplet-m2-native-matrix", "fault-run")},
    "tty": {("qaplet-sdk", "tty-consume"), ("qaplet-sdk-tty", "run")},
    "mixed": {("qaplet-sdk", "mixed-consume"), ("qaplet-sdk-mixed", "run")},
    "m2-lifecycle": {("qaplet-m2-cycles", "run")},
    "m2-mixed-cycles": {("qaplet-m2-cycles", "run")}}


def inventory():
    import sdk_matrix
    import m2_waker
    import m2_process
    import m2_stdio
    import m2_tty
    names = json.loads((ROOT / "scripts/m2_native_catalog.json").read_text(encoding="utf-8"))
    rows = []
    for name, system, arch in TARGETS:
        windows = system == "Windows"
        catalogs = {"normal": [row[0] for row in sdk_matrix.catalog({"execution_arch": arch, "page_size": None})],
                    "fault": sorted(fault_catalog(windows)), "tty": sorted(tty_catalog(windows)),
                    "mixed": ["qaplet-mixed-" + stem + "." + suffix for stem, suffixes in
                              (("host", ("stop", "races", "nonreturning", "tty", "tty-races", "tty-nonreturning")),
                               ("faults", ("notify", "uncertain", "initialization", "wait-budget", "tty-notify", "tty-uncertain"))) for suffix in suffixes],
                    "backend-waker": sorted(m2_waker.REQUIRED),
                    "backend-process": sorted(m2_process.REQUIRED | (m2_process.WINDOWS_REQUIRED if windows else set())),
                    "backend-stdio": sorted(m2_stdio.COMMON | m2_stdio.TTY),
                    "backend-tty": sorted(m2_tty.REQUIRED | ({"stdin.console", "text.supplementary-input"} if windows else set())),
                    "m2-lifecycle": ["m2-lifecycle"], "m2-mixed-cycles": ["m2-mixed"]}
        catalogs["examples"] = [value for value in catalogs["normal"] if value.startswith(("qaplet-example.", "qaplet-m2-example."))]
        catalogs.update(names["platforms"]["Windows" if windows else "POSIX"])
        scopes = {}
        for scope in MANDATORY:
            optional = ["qaplet-host.module-symlinks"] if windows and scope == "normal" else []
            scopes[scope] = {"status": "not_run", "reason": "No selected actual current evidence", "case_ids": catalogs[scope],
                             "optional_case_ids": optional, "required_case_ids": [value for value in catalogs[scope] if value not in optional]}
        rows.append({"target": name, "os": system, "arch": arch, "status": "not_run", "mandatory": scopes})
    return rows


def fault_catalog(windows):
    import sdk
    common = {
        "qaplet-m2-faults.system-boundary":
            "controlled HostNotify: accepted event/close and CLOSED wake overlap; JS released; inflight retained; finite quiesce retry; destroy BUSY; producer joined; drained cleanup: pass\n"
            "controlled HostNotify reentry: deliberately invalid hook receives BUSY for SDK queries/control/drives/close; no JS or stop mutation: pass\n"
            "controlled M2: four system initialization refusals; resources balanced; PENDING/CLOSED whole-drive guard: pass\n"
            "controlled M2 wait: pre-sleep handoff; accepted send failure; poll/recv/clock faults; first cause; socket rollback; uncertain close retained; reused fd untouched: pass\n"
            "controlled M2 diagnostics: original setup/cleanup errors; incomplete unpublished rollback; fd reuse; independent published first cause: pass\n"
            "controlled M2 timers: deadline overflow; max-wait/timer/entry minimum; clock failure/backwards; terminal cleanup: pass\n"
            "controlled M2 scheduling: same deadline acceptance FIFO; interval skips late periods on original schedule without burst; self cancel: pass\n",
        "qaplet-work-faults.system-boundary":
            "controlled M2 work JS allocation: actual SDK calloc refusal from caught JS submit remains permanent; no accepted task; first cause preserved; native and engine cleanup: pass\n" +
            next(output for name, _, output in sdk.HOST_CASES if name == "m2-work-file") +
            "controlled M2 work: pthread create errno refuses atomically and recovers; actual calloc refusal permanent; accepted submit/completion/cancel survives wake failure; join error independently retained CLOSED/BUSY without identity retry or false cleanup: pass\n",
        "qaplet-aggregate-faults.system-boundary": sdk.AGGREGATE_FAULT_STDOUT}
    platform = "Windows" if windows else "POSIX"
    for stem, cases in (("qaplet-process-faults", sdk.PROCESS_FAULT_CASES),
                        ("qaplet-io-faults", sdk.IO_FAULT_CASES),
                        ("qaplet-js-process-faults", sdk.JS_PROCESS_FAULT_CASES)):
        common.update({stem + "." + mode: output for mode, output, platforms in cases if platform in platforms})
    common.update({"qaplet-stdio-faults." + suffix: output for suffix, _, output in sdk.STDIO_FAULT_CASES})
    return common


def tty_catalog(windows):
    import sdk_tty
    result = {}
    for fault, cases in ((False, sdk_tty.NORMAL + (sdk_tty.WINDOWS if windows else sdk_tty.POSIX)),
                         (True, sdk_tty.FAULT + (sdk_tty.WINDOWS_FAULT if windows else sdk_tty.POSIX_FAULT))):
        stem = "qaplet-tty-faults" if fault else "qaplet-tty-host"
        result.update({stem + "." + mode.removeprefix("--"): output for mode, output in cases})
    return result


def command_for(case):
    return case.get("command") or case.get("commands", [None])[0]


def auxiliary_units(group):
    def unit(*names, wraps=()):
        return {"units": ["sdk/" + name for name in names], "wrapped_symbols": list(wraps), "sdk_archive": True}
    if group == "mixed":
        return {"qaplet-mixed-host": unit("host-mixed.c"), "qaplet-mixed-faults": unit("mixed-faults.c", wraps=("socket", "close", "write", "pthread_cond_timedwait"))}
    return {
        "qaplet-m2-faults": unit("m2-faults.c", wraps=("pthread_mutex_init", "pthread_mutex_destroy", "pthread_condattr_init", "pthread_condattr_destroy", "pthread_condattr_setclock", "pthread_cond_init", "pthread_cond_destroy", "clock_gettime", "socket", "accept", "close", "poll", "send", "recv")),
        "qaplet-work-faults": unit("work-faults.c", "host-work.c", wraps=("calloc", "pthread_create", "pthread_tryjoin_np", "send", "mkstemp")),
        "qaplet-process-faults": unit("process-faults.c", wraps=("calloc", "pthread_create", "fork", "sys_waitid", "kill", "read", "clock_gettime", "mkntcmdline", "TerminateProcess", "CloseHandle", "GetExitCodeProcess", "send")),
        "qaplet-io-faults": unit("io-faults.c", wraps=("read", "ReadFile", "write", "WriteFile", "close", "CloseHandle", "GetExitCodeProcess", "calloc", "pthread_create", "send", "WaitForSingleObject")),
        "qaplet-js-process-faults": unit("host-process.c", wraps=("pthread_create", "sys_waitid")),
        "qaplet-stdio-faults": unit("stdio-faults.c", wraps=("read", "ReadFile", "write", "WriteFile", "pthread_create", "calloc")),
        "qaplet-aggregate-faults": unit("aggregate-faults.c", wraps=("calloc", "malloc", "send", "clock_gettime", "poll"))}


def local_mappings():
    import os
    from workflow_support import workspace
    main = str(Path(workspace(ROOT)["main_checkout"]).resolve()).replace("\\", "/")
    if os.name == "nt" and main[1:3] == ":/":
        return [{"source": "/mnt/" + main[0].lower() + main[2:], "root": main}]
    if main.startswith("/mnt/") and len(main) > 7 and main[6] == "/":
        return [{"source": main[5].upper() + ":" + main[6:], "root": main}]
    return []


def bind_for_run(args, producer, consumer, group, report):
    from build_ape import identity
    from core_gate import Evidence
    from m2_fixture_binding import bind_auxiliary
    evidence = Evidence({"path_mappings": local_mappings()}, ROOT)
    artifacts = bind_auxiliary(evidence, consumer, producer, auxiliary_units(group), report)
    report["target_component_binding"] = {"origin_auxiliary_report": identity(args.consumer),
        "target_producer": identity(args.producer), "artifacts": artifacts,
        "scope": "Exact actual compiler/link/packing closure equality; unchanged origin producer, new native execution"}
    report["binding_evidence_index"] = list(evidence.index.values())


def verify_static_cases(incoming, expected, evidence):
    from core_gate import interval, require, unique
    cases = unique(incoming["cases"], "case_id")
    require(set(cases) == set(expected), "Complete mandatory special catalog required")
    for name, output in expected.items():
        case, command = cases[name], command_for(cases[name])
        require(case["status"] == "pass" and case["required"] is True and command,
                "Required special case did not pass: " + name)
        interval(command)
        require(command["exit_code"] == 0 and command["timed_out"] is False and command["timeout_seconds"] > 0,
                "Incomplete special native command: " + name)
        require(evidence.read(command["stdout_log"]) == output.encode("utf-8") and
                evidence.read(command["stderr_log"]) == b"", "Special raw public output differs: " + name)
        require(case["expected"] == {"exit_code": 0, "stdout": output, "stderr": ""},
                "Special declared expectation differs from current catalog")
        if case.get("native_console"):
            native = case["native_console"]
            require(native["exit_code"] == 0 and native["timed_out"] is False and not native["error"] and
                    all(native["cleanup"].values()), "Native Console controller cleanup incomplete")
        if case.get("full_mode_readback"):
            native = case["full_mode_readback"]
            rows = [json.loads(line) for line in evidence.read(native["artifact"]).decode("utf-8").splitlines()]
            require(rows == native["rows"] and all(row["ok"] for row in rows), "TTY native mode raw readback differs")
            groups = {}
            for row in rows:
                prefix, stage = row["stage"].rsplit(".", 1) if "." in row["stage"] else ("terminal", row["stage"])
                require(stage not in groups.setdefault(prefix, {}), "Duplicate TTY mode readback stage")
                groups[prefix][stage] = {key: value for key, value in row.items() if key != "stage"}
            require(all(set(group) == {"before", "modified", "restored"} and group["before"] == group["restored"]
                        for group in groups.values()) and any(group["before"] != group["modified"] for group in groups.values()),
                    "TTY complete native mode/flags restoration differs")
    return len(cases)


def validate_examples(incoming, evidence):
    import sdk
    import sdk_m2_example
    from core_gate import require, unique
    selected_cases = incoming["cases"]
    if incoming.get("scope") == "run":
        selected_cases = [row for row in selected_cases if row["case_id"].startswith(("qaplet-example.", "qaplet-m2-example."))]
    cases = unique(selected_cases, "case_id")
    expected = {"qaplet-example." + name: (code, output, error)
                for name, _, code, output, error in sdk.EXAMPLE_CASES}
    require(set(cases) == set(expected) | {"qaplet-m2-example." + name for name in sdk_m2_example.CASES},
            "Both public package examples and all fourteen cases required")
    for name, row in cases.items():
        command = command_for(row)
        require(row["status"] == "pass" and not command["timed_out"], "Public example is incomplete")
        stdout, stderr = (evidence.read(command[key + "_log"]) for key in ("stdout", "stderr"))
        if name.startswith("qaplet-m2-example."):
            suffix = name.split(".", 1)[1]
            observed = sdk_m2_example.inspect(suffix, stdout, stderr, command["exit_code"], sdk.M2_RUNTIME_METADATA)
            require(observed == row["observations"] and row["native_cleanup_complete"] is
                    (None if suffix in ("metadata", "arguments") else suffix != "cleanup-pending"),
                    "M2 example public/cleanup observations differ")
        else:
            code, output, error = expected[name]
            require(command["exit_code"] == code and stdout == output.encode("utf-8") and
                    (error.encode("utf-8") in stderr if error else not stderr), "M1 example compatibility differs")
    return len(cases)


def validate_cli(incoming, oracle, evidence):
    import ast
    from hashlib import sha256
    from core_gate import require, unique
    actual, expected = unique(incoming["cases"], "case_id"), unique(oracle["cases"], "case_id")
    require(set(actual) == set(expected), "Complete public CLI case inventory required")
    needles = set()
    # The existing public CLI executor records digests for expected byte needles.
    # Recover only literal public expectations from its pinned calling sources;
    # no old execution score is treated as a new product result.
    for name in ("sdk.py", "sdk_cli.py", "m2_cli_output.py"):
        tree = ast.parse((ROOT / "scripts" / name).read_text(encoding="utf-8"))
        needles.update(node.value if isinstance(node.value, bytes) else node.value.encode("utf-8")
                       for node in ast.walk(tree) if isinstance(node, ast.Constant) and isinstance(node.value, (str, bytes)))
    def digest(raw):
        return {"sha256": sha256(raw).hexdigest(), "size_bytes": len(raw)}
    for name, row in actual.items():
        require(row["required"] is True and row["status"] == "pass", "Required CLI case is incomplete")
        if "expected" in row:
            require(row["expected"] == expected[name]["expected"], "CLI public expectation differs from accepted contract oracle")
            command = command_for(row)
            require(not command["timed_out"] and command["exit_code"] == row["expected"]["exit_code"], "CLI native exit differs")
            stdout, stderr = (evidence.read(command[key + "_log"]) for key in ("stdout", "stderr"))
            require(digest(stdout) == row["expected"]["stdout"] and digest(stdout) == row["actual"]["stdout"] and
                    digest(stderr) == row["actual"]["stderr"], "CLI original bytes/digests differ")
            if row["expected"]["stderr_contains"]:
                require(any(digest(needle) == row["expected"]["stderr"] and needle in stderr for needle in needles),
                        "CLI expected diagnostic byte needle is absent")
            else:
                require(digest(stderr) == row["expected"]["stderr"], "CLI exact stderr differs")
            if row.get("stdin"):
                evidence.read(row["stdin"])
        elif "terminal_readback" in row:
            rows = [json.loads(line) for line in evidence.read(row["terminal_readback"]).decode("utf-8").splitlines()]
            require(rows == row["rows"] and row["full_modes_and_flags_restored"] is True, "CLI terminal raw readback differs")
            groups = {stage: [{key: value for key, value in item.items() if key != "stage"}
                              for item in rows if item["stage"] == stage] for stage in ("before", "modified", "restored")}
            require(groups["before"] == groups["restored"] and groups["before"] != groups["modified"],
                    "CLI full native modes/flags restoration differs")
            child = row["child_status"]
            require(child["exited"] == child["reaped"] == 1 and not child["helper_active"], "CLI child retirement is incomplete")
            require(child["exit_code"] == (0 if name.endswith(("-normal", "-unicode")) else 1), "CLI terminal child exit differs")
            command = command_for(row)
            require(command["exit_code"] == 0 and not command["timed_out"], "CLI terminal controller incomplete")
            evidence.read(command["stdout_log"]);evidence.read(command["stderr_log"])
            if row.get("startup_observations"):
                evidence.read(row["startup_observations"])
            if name.endswith("-unicode") and incoming["host"]["os"] == "Windows":
                codepage = row["codepage"]
                require(codepage["requested"] == codepage["after"] == 437 and codepage["restored"] == codepage["before"],
                        "CLI Console output code page changed")
        elif "native_record" in row:
            native = json.loads(evidence.read(row["native_record"]))
            require(native == row["actual"] and not native["timed_out"] and native["reader_threads_finished"],
                    "CLI overlapped original native record differs")
            stdout, stderr = (evidence.read(native[key + "_log"]) for key in ("stdout", "stderr"))
            exits = [item for item in native["events"] if item.get("operation") == "ExitProcess"]
            if name.endswith("unknown-completion"):
                require(native["exit_code"] == 1 and exits and exits[-1]["unconfirmed_operations"] >= 1 and
                        exits[-1]["fault_injected"] and not any(exits[-1][key] for key in
                        ("buffer_freed_while_pending", "event_closed_while_pending", "identity_reused_while_pending")) and
                        b"Host output native cleanup incomplete" in stderr and row["native_cleanup_complete"] is False,
                        "CLI unknown completion must retain its native borrow")
            else:
                value = native["mode"] == "value"
                require(native["exit_code"] == (0 if value else 1) and native["exact_data"] and not native["errors"] and
                        stdout == (b"B\0Z" + b"X" * 200000 + b"\n" if value else b"B\0Z") and
                        (not stderr if value else stderr.startswith(b"E" * 3584 + b"qaplet: code=7 ") and b"D" * 900 in stderr),
                        "CLI real overlapped data/diagnostic differs")
                if row["controlled_system_fixture"]:
                    writes = [item for item in native["events"] if item.get("operation") == "WriteFile" and item.get("asynchronous_pipe")]
                    gets = [item for item in native["events"] if item.get("operation") == "GetOverlappedResult"]
                    require(writes and gets and exits and any(item["pending"] for item in writes) and
                            all(item["overlapped_nonnull"] and item["event_nonnull"] and item["buffer_allocation_known"] for item in writes) and
                            all(item["physical_completion_confirmed"] for item in gets) and
                            all(not any(item[key] for key in ("buffer_freed_while_pending", "event_closed_while_pending",
                                    "identity_reused_while_pending", "unconfirmed_operations")) for item in exits),
                            "CLI real native overlapped retirement protocol differs")
        else:
            raise ValueError("Unknown public CLI case schema")
    return len(actual)


def validate_signal(incoming, strong, windows, evidence, names):
    from core_gate import require, unique
    cases = unique(incoming["cases"], "case")
    require(set(cases) == set(names), "Complete actual signal case inventory required")
    for row in cases.values():
        ready = row.get("ready_accepted", row.get("native", {}).get("ready_accepted"))
        require(row["status"] == "pass" and not row["supervisor_timeout"] and ready, "Signal gate/exit incomplete")
        if strong:
            require(row["public_spawn_not_awaited"] and row["same_js_stack_never_suspends"] and
                    row["identity_verified_before_READY"] and row["observed_child_exit_complete"] and
                    row["exit_code"] == 1 and not row["failure"] and not row["forced_fixture_cleanup"] and
                    row["native_cleanup_complete"] is None, "Strong signal child proof or whole-SDK scope differs")
            require(evidence.read(row["source"]).decode("utf-8") == row["source_text"], "Strong same-stack source bytes differ")
            stderr = evidence.read(str(Path(row["cwd"]) / "cli.stderr"))
            require(b"EXECUTION_STOPPED" in stderr and b"cleanup incomplete" not in stderr, "Strong signal diagnostic differs")
            retired = row["child_retirement"]
            if windows:
                require(retired["actual_native_wait_result"] == 0 and retired["physical_exit_observed"] and
                        retired["exit_query_ok"] and retired["exit_code"] == 53261, "Known native arm child did not exit")
            else:
                require(retired.get("subreaper_waitpid_errno") == "ECHILD" and retired["child_proc_absent"] and
                        retired["SDK_reaped_before_owner_exit"] and not row["signal_delivery"]["errno"],
                        "Strong POSIX child was not demonstrably reaped before owner exit")
        elif windows:
            native = row["native"]
            require(row["launcher_exit"] == 0 and native["exit_code"] == 1 and native["sender_received"] == 1 and
                    native["target_in_same_console"] and native["group"] == 0 and not native["supervisor_timeout"] and
                    row["native_cleanup_complete"] is True, "Actual Ctrl+C sender/target/exit differs")
        else:
            stdout, stderr = evidence.read(row["stdout"]), evidence.read(row["stderr"])
            require(row["exit_code"] == 1 and not row["signal_delivery"]["errno"] and row["real_os_signal"] in (2, 15) and
                    b"EXECUTION_STOPPED" in stderr and b"AFTER" not in stdout and b"cleanup incomplete" not in stderr and
                    row["native_cleanup_complete"] is True, "Actual POSIX sender/target/exit differs")
    return len(cases)


def signal_auxiliary_units():
    import sdk_signal
    return {name: {"units": ([] if name in ("signal-arm", "signal-host") else ["recipe:src/cli.c"]) + ["sdk/" + source],
                   "wrapped_symbols": list(wraps), "sdk_archive": name != "signal-arm"}
            for name, (source, wraps) in sdk_signal.MODES.items()}


def validate_signal_special(incoming, mode, windows, evidence, path, names):
    from core_gate import require, unique
    root = path.parent / (path.stem + "-raw")
    rows = incoming.get("cases", [incoming])
    expected = ({"queue": ["windows-Ctrl+C-queue"], "mixed": ["windows-Ctrl+C-mixed"],
                 "budget": ["windows-Ctrl+C-wait"], "late": ["windows-Ctrl+C-wait"]}[mode] if windows else
                {"queue": ["linux-SIGINT-queue", "linux-SIGTERM-queue"],
                 "mixed": ["SIGINT-mixed", "SIGTERM-mixed"], "budget": [None], "late": [None]}[mode])
    require(len(rows) == len(names) and [row.get("case") for row in rows] == expected,
            "Complete current signal-special inventory required")
    for row in rows:
        require(row["status"] == "pass" and not row["supervisor_timeout"] and
                row.get("ready_accepted", row.get("native", {}).get("ready_accepted")), "Signal-special ready/exit incomplete")
        if windows:
            native = row["native"]
            directory = root / ("wait" if mode in ("budget", "late") else mode)
            require(json.loads(evidence.read(str(directory / "ctrl-result.json"))) == native and
                    row["launcher_exit"] == 0 and native["exit_code"] == 1 and native["target_in_same_console"] and
                    native["sender_pid"] != native["target_pid"] and native["group"] == 0 and native["event"] == 0 and
                    not native["supervisor_timeout"] and native["sender_received"] >= 1,
                    "Real Console Ctrl+C target/retirement differs")
            stderr = evidence.read(str(directory / "cli.stderr"))
            evidence.read(str(directory / "host.stdout"));evidence.read(str(directory / "host.stderr"))
        else:
            directory = root / (str(row["real_os_signal"]) if mode == "mixed" else
                                str(row["real_os_signal"]) + "-queue") if mode in ("queue", "mixed") else root
            if mode == "mixed":
                # Python 3.10 IntEnum str differs from 3.11; accept only the two exact declared forms.
                candidates = [directory, root / ("Signals.SIGINT" if row["real_os_signal"] == 2 else "Signals.SIGTERM")]
                available = [item for item in candidates if item.is_dir()]
                require(len(available) == 1, "Unique actual mixed raw directory required")
                directory = available[0]
            require(row["exit_code"] == 1, "Signal-special owner exit differs")
            deliveries = row.get("signals", [row.get("signal_delivery")])
            require(deliveries and all(item and item["errno"] == 0 and item["target_pid"] == row["pid"] and
                    item["sender_pid"] != row["pid"] and item["signal"] in (2, 15) for item in deliveries),
                    "Actual process-directed signal differs")
            stderr = evidence.read(row["stderr"]) if "stderr" in row else evidence.read(str(directory / "terminal.bin"))
            if "stdout" in row:
                require(b"AFTER" not in evidence.read(row["stdout"]), "JS continued after accepted stop")
        if not (windows and mode == "mixed"):
            require(b"EXECUTION_STOPPED" in stderr, "Actual signal diagnostic missing")
        if mode == "queue":
            queue = json.loads(evidence.read(str(directory / "queue-result.json")))
            require(queue == row["queue_result"] and queue["queue_full"] and queue["owner_stopped"] and
                    queue["sdk_destroyed"] and queue["user_marks"] == 0 and queue["events_dropped"] == 2 and
                    (windows or queue["sdk_unix_dispositions_preserved"]) and row["native_cleanup_complete"] is True,
                    "C Host queue boundary differs; this is not a stock CLI claim")
        else:
            filename = {"mixed": "signal-native.ndjson", "budget": "signal-fault.ndjson", "late": "signal-late.ndjson"}[mode]
            records = [json.loads(line) for line in evidence.read(str(directory / filename)).decode().splitlines()]
            require(records == row["raw_records"], "Signal-special raw native facts differ")
            if mode == "mixed":
                groups = {}
                for record in records:groups.setdefault(record["operation"], []).append(record)
                child = json.loads(evidence.read(str(directory / "direct-child.json")))
                require(child == row["direct_child"] and len(groups.get("native-child-reaped", [])) == 1 and
                        groups["native-child-reaped"][0]["pid"] == child["pid"] and
                        groups["mixed-tty-read-accepted"][-1]["processes"] == 1 and
                        groups["mixed-tty-read-accepted"][-1]["operations"] >= 1 and
                        groups["mixed-tty-read-accepted"][-1]["io_endpoints"] >= 4 and
                        groups["owner-close"][-1]["code"] == 0 and groups["owner-close"][-1]["termination_reason"] == 23 and
                        groups["native-quiesce"][-1]["code"] == 0 and groups["native-quiesce"][-1]["reason"] == 6 and
                        groups["runtime-destroy"][-1]["code"] == 0 and not evidence.resolve(str(directory / "forbidden-js")).exists() and
                        row["native_cleanup_complete"] is True, "Mixed stop physical reap/quiesce/destroy differs")
                if windows:
                    modes = json.loads(evidence.read(str(directory / "mixed-modes.json")))
                    require(modes == row["terminal_modes"] and modes["full_modes_restored"] and
                            modes["actual_entry_baseline"] == modes["restored"] != modes["modified"] and
                            modes["pre_CRT_restore_claimed"] is False and "native-force-accepted" in groups and
                            groups["native-child-reaped"][0]["exit_code"] == 53261, "Native Console entry mode/reap differs")
                else:
                    require(row["full_modes_and_flags_restored"] and row["before"] == row["restored"] != row["modified"] and
                            row["flags"][0] == row["flags"][2] != row["flags"][1] and
                            groups["native-child-reaped"][0]["signal"] == 9, "PTY full modes/flags or force reap differs")
            elif mode == "budget":
                waits = [record["duration_ns"] / 1e9 for record in records if record["operation"] == "active-cond-wait"]
                require(waits == row["active_condition_waits_seconds"] and abs(sum(waits) - row["cumulative_observed_active_wait_seconds"]) < 1e-9 and
                        4.5 <= sum(waits) < 5.5 and sum(value > .5 for value in waits) >= 2 and
                        4.5 <= (native["elapsed_seconds"] if windows else row["elapsed_seconds"]) < 6.5 and
                        (native["native_thread_exit_held"] and native["sender_received"] >= 6 and native["repeated_after_held"] == 5
                         if windows else row["native_thread_exit_held"] and len(row["signals"]) == 6) and
                        row["native_cleanup_complete"] is False and b"native cleanup incomplete" in stderr,
                        "Cumulative SDK+relay five-second active budget or retained-negative fact differs")
            else:
                late = [record for record in records if record["operation"] == "post-destroy-late-entry"]
                require(len(late) == 1 and late[0]["destroy_code"] == 0 and late[0]["original_target_identity_preserved"] and
                        late[0]["replacement_identity_reused"] is False and late[0]["late_entry_completed"] and
                        late[0]["replacement_objects_untouched"] and late[0]["future_entry_delivered"] and
                        row["native_cleanup_complete"] is True, "Post-destroy asynchronous target identity differs")
        if mode != "budget":require(b"cleanup incomplete" not in stderr, "Unexpected native incomplete diagnostic")
    return len(rows)


def validate_mixed(incoming, evidence):
    import sdk_mixed
    from core_gate import require, unique
    expected = {}
    for fault, cases in ((False, sdk_mixed.NORMAL), (True, sdk_mixed.FAULT)):
        stem = "qaplet-mixed-faults" if fault else "qaplet-mixed-host"
        expected.update({stem + "." + name: sdk_mixed.PENDING if "nonreturning" in name else
                         sdk_mixed.UNKNOWN if "uncertain" in name else sdk_mixed.STOP for name in cases})
    rows = unique(incoming["cases"], "case_id")
    require(set(rows) == set(expected), "Complete twelve-case mixed catalog required")
    for name, output in expected.items():
        case, command = rows[name], command_for(rows[name])
        require(case["status"] == "pass" and command["exit_code"] == 0 and command["timed_out"] is False,
                "Mixed command is incomplete")
        require(evidence.read(case["marker"]) == output.encode("utf-8"), "Mixed native marker differs")
        require(case["expected_marker"] == output, "Mixed expected marker differs from current catalog")
        require(case["native_cleanup_complete"] is not any(x in name for x in ("nonreturning", "uncertain")),
                "Mixed retained-negative cleanup fact differs")
        require([row["requested_active_wait_ms"] for row in case["wait_observations"]] == [0, 5, 5],
                "Mixed finite public quiesce observations differ")
        waits = str(evidence.resolve(case["marker"]["path"]).parent / "mixed-waits.ndjson")
        require([json.loads(line) for line in evidence.read(waits).decode().splitlines()] == case["wait_observations"],
                "Mixed active waits differ from actual original records")
        if "tty" in name:
            terminal = case["terminal_readback"]
            require(terminal["complete_restore"] is True, "Mixed terminal readback missing")
            records = [json.loads(line) for line in evidence.read(terminal["artifact"]).decode("utf-8").splitlines()]
            require(records == terminal["rows"] and len(records) == 3, "Mixed terminal raw observations differ")
            before, modified, restored = ({key: value for key, value in row.items() if key != "stage"} for row in records)
            require(before == restored and before != modified, "Mixed complete native mode restoration differs")
    return len(rows)


def fault_run(args, report):
    import sdk
    from build_ape import BuildError, identity, run
    from foundation import host_facts, sha256
    from startup import measured_host, select_loader
    producer = json.loads(args.producer.read_text(encoding="utf-8"))
    consumer = json.loads(args.consumer.read_text(encoding="utf-8"))
    sdk.check_package(producer, args.supplied.resolve())
    if consumer["status"] != "pass" or consumer["scope"] != "fault-consume":
        raise BuildError("Actual current Ubuntu fault-consumer producer required")
    report.update(host=host_facts(), ci=None, scope="fault-run", sdk=producer["sdk"], sdk_recipe=producer["sdk_recipe"],
                  producer_report=identity(args.producer), consumer_report=identity(args.consumer), commands=[], cases=[], artifacts=[])
    bind_for_run(args, producer, consumer, "fault", report)
    directory = args.report.resolve().parent / "evidence" / report["report_id"]
    directory.mkdir(parents=True)
    measured_host(args, report, directory)
    loader = select_loader(args, report)
    root = args.artifact_dir.resolve()
    artifacts = {}
    for row in consumer["artifacts"]:
        path = root / Path(row["path"]).name
        if sha256(path) != row["sha256"] or path.stat().st_size != row["size_bytes"] or row["audit"]["status"] != "pass":
            raise BuildError("Same-byte audited Ubuntu fault artifacts required")
        artifacts[row["name"]] = path
        report["artifacts"].append(identity(path))
    windows = report["host"]["os"] == "Windows"
    expected = fault_catalog(windows)
    child = root / "sdk/process-child.exe"
    if windows:
        from zipfile import ZipFile
        # Supply the exact public SDK child in this consumer's own directory.
        child = directory / "process-child.exe"
        with ZipFile(args.supplied / producer["package"]["file"]) as archive:
            child.write_bytes(archive.read("sdk/process-child.exe"))
    launcher = loader[0] if loader else "-"
    for case_id, output in expected.items():
        stem, suffix = case_id.split(".", 1)
        artifact = artifacts[stem]
        arguments = []
        if stem == "qaplet-process-faults":
            arguments = [suffix, str(child) if windows else "/bin/true"]
        elif stem == "qaplet-io-faults":
            arguments = [suffix, str(artifact), str(launcher), str(child)]
        elif stem == "qaplet-js-process-faults":
            arguments = [suffix, str(artifact), str(launcher)]
        elif stem == "qaplet-stdio-faults":
            arguments = next(argv for name, argv, _ in sdk.STDIO_FAULT_CASES if name == suffix)
        work = directory / case_id
        work.mkdir()
        command, _, _ = run(report, directory, case_id, [*loader, artifact, *arguments], args.timeout, work, expected=None)
        stdout, stderr = (Path(command[key + "_log"]).read_bytes() for key in ("stdout", "stderr"))
        passed = command["exit_code"] == 0 and not command["timed_out"] and stdout == output.encode("utf-8") and not stderr
        report["cases"].append({"case_id": case_id, "status": "pass" if passed else "fail", "required": True,
            "controlled_system_fixture": True, "command": command, "artifact": identity(artifact),
            "sdk_sha256": producer["sdk"]["sha256"], "expected": {"exit_code": 0, "stdout": output, "stderr": ""},
            "native_cleanup_complete": None,
            "cleanup_scope": "Per-contract physical retirement/retained-negative facts are in the authoritative raw output; no blanket whole-process cleanup claim"})
    report.update(status="pass" if all(row["status"] == "pass" for row in report["cases"]) else "fail",
                  reason="Native same-byte replay of all platform-applicable current public controlled-system contracts")


def mixed_run(args, report):
    import sdk
    import sdk_mixed
    from build_ape import identity, BuildError
    from foundation import host_facts, sha256
    from startup import measured_host, select_loader
    producer = json.loads(args.producer.read_text(encoding="utf-8"))
    consumer = json.loads(args.consumer.read_text(encoding="utf-8"))
    sdk.check_package(producer, args.supplied.resolve())
    if consumer["status"] != "pass" or consumer["scope"] != "mixed-consume":
        raise BuildError("Actual mixed auxiliary producer required")
    report.update(report_kind="qaplet-sdk-mixed", scope="run", host=host_facts(), ci=None,
        sdk=producer["sdk"], sdk_recipe=producer["sdk_recipe"], producer_report=identity(args.producer),
        consumer_report=identity(args.consumer), cases=[], commands=[], artifacts=[])
    bind_for_run(args, producer, consumer, "mixed", report)
    directory = args.report.resolve().parent / "evidence" / report["report_id"]
    directory.mkdir(parents=True)
    root = args.artifact_dir.resolve()
    for artifact in consumer["artifacts"]:
        current = root / Path(artifact["path"]).name
        if sha256(current) != artifact["sha256"] or artifact["audit"]["status"] != "pass":
            raise BuildError("Actual mixed artifact differs from its audited original producer")
        report["artifacts"].append(identity(current))
    if sha256(root / "process-child.exe") != consumer["mixed_native_child"]["sha256"]:
        raise BuildError("Mixed native child differs")
    measured_host(args, report, directory);loader = select_loader(args, report)
    sdk_mixed.run_supplied(args, report, directory, root, loader)
    report.update(status="pass" if all(row["status"] == "pass" for row in report["cases"]) else "fail",
                  reason="New target SDK native mixed execution; exact auxiliary origin closure retained separately")


def evaluate(args, report):
    import sdk
    import sdk_matrix
    import sdk_m2_cycles
    from build_ape import identity
    from core_gate import Evidence, require, same, interval
    selection = json.loads(args.selection.read_text(encoding="utf-8"))
    require(selection.get("schema_version") == 1 and selection.get("profile") == "m2-native-matrix",
            "Explicit M2 native selection required")
    evidence = Evidence(selection, args.selection.resolve().parent)
    from workflow_support import guard_evidence_outputs
    protected = [args.selection.resolve()]
    for entry in selection.get("native", []):
        for reference in entry["reports"].values():
            ref = reference["report"]
            selected_path = evidence.resolve(ref["path"] if isinstance(ref, dict) else ref)
            protected.append(selected_path)
            document = json.loads(selected_path.read_text(encoding="utf-8"))
            def protect_raw(node):
                if isinstance(node, list):
                    for value in node:
                        protect_raw(value)
                elif isinstance(node, dict):
                    for key, value in node.items():
                        if key.endswith("_log") and isinstance(value, str):
                            protected.append(evidence.resolve(value))
                        elif key in ("progress", "marker", "artifact", "waits") and isinstance(value, dict) and "path" in value:
                            protected.append(evidence.resolve(value["path"]))
                        protect_raw(value)
            protect_raw(document)
    directory = args.report.resolve().parent / "evidence" / report["report_id"]
    guard_evidence_outputs([args.report, directory], [], files=protected,
                           roots=[evidence.resolve(selection["distribution_root"])])
    directory.mkdir(parents=True)
    snapshot = directory / "selection.json"
    shutil.copyfile(args.selection, snapshot)
    declared = [*sorted((ROOT / "scripts").rglob("*.py")), ROOT / "scripts/m2_native_catalog.json",
                ROOT / "tests/native/sdk_m2_cycles.c", ROOT / "tests/contracts/test_m2_native_matrix.py"]
    evaluator_inputs = []
    for source in declared:
        target = directory / "evaluator-inputs" / source.relative_to(ROOT)
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(source, target)
        evaluator_inputs.append({"file": source.relative_to(ROOT).as_posix(), "original": identity(source), "retained": identity(target)})
    report.update(coverage=inventory(), release_ready=False, selected_inputs=[], evidence_index=[],
                  selection=identity(snapshot), evaluator_sources=evaluator_inputs,
                  evaluator_source_scope="Explicit repository-script over-approximation, catalog, cycle C and matrix test; not an invented child import trace")
    try:
        producer_ref = selection["producer_report"]
        producer_path = evidence.resolve(producer_ref["path"] if isinstance(producer_ref, dict) else producer_ref)
        producer = evidence.report(selection["producer_report"], "qaplet-sdk", {"build"})
        sdk.check_package(producer, evidence.resolve(selection["distribution_root"]))
        report.update(sdk=producer["sdk"], sdk_recipe=producer["sdk_recipe"], producer_report=identity(producer_path))
        accepted = None
        accepted_files = {}
        verified_accepted_files = set()
        if selection.get("accepted_ticket23"):
            raw = evidence.read(selection["accepted_ticket23"])
            from hashlib import sha256
            require(sha256(raw).hexdigest() == ACCEPTED_23, "Selected accepted ticket23 receipt differs")
            accepted = json.loads(raw)
            require(same(accepted["producer"], identity(producer_path)), "Accepted producer differs")
            accepted_inventory = json.loads(evidence.read(accepted["supplier_raw_inventory"]))
            from core_gate import normalized
            accepted_files = {normalized(row["path"]): row for row in accepted_inventory["files"]}
        seen = set()
        signal_supply = None
        bindings = {}
        for entry in selection.get("native", []):
            target = entry["target"]
            require(target not in seen, "Duplicate selected native target")
            seen.add(target)
            slot = next(row for row in report["coverage"] if row["target"] == target)
            require(set(entry["reports"]) <= set(MANDATORY), "Unknown mandatory native scope")
            host = None
            for scope, reference in entry["reports"].items():
                ref = reference["report"]
                path = evidence.resolve(ref["path"] if isinstance(ref, dict) else ref)
                incoming = json.loads(evidence.read(reference["report"]))
                slot["mandatory"][scope].update(report=identity(path), status=incoming["status"])
                require(incoming["status"] == "pass", "Selected incomplete report cannot pass: " + scope)
                if scope in SCOPES:
                    require((incoming.get("report_kind"), incoming.get("scope")) in SCOPES[scope],
                            "Selected report cannot substitute another execution scope: " + scope)
                elif scope.startswith("backend-"):
                    require((incoming.get("report_kind"), incoming.get("scope")) ==
                            ("m2-" + scope.removeprefix("backend-"), "host"), "Guest/build backend cannot replace native execution")
                observed = incoming["host"]
                signal = scope.startswith("signal-")
                signal_host = observed if signal and "os" not in observed else None
                if signal_host is not None:
                    uname = signal_host.get("uname")
                    measured_machine = signal_host.get("machine", signal_host.get("architecture", uname[-1] if uname else None))
                    machine = {"AMD64": "x86_64", "arm64": "aarch64"}.get(measured_machine, measured_machine)
                    platform = signal_host.get("platform", uname[0] if uname else "")
                    require(host is not None and machine == slot["arch"] and signal_host["page_size"] == host["page_size"] and
                            platform.lower().startswith(slot["os"].lower()) and (not uname or uname[2] == host["kernel"]),
                            "Signal report needs an actual matching native normal-report host anchor")
                    observed = host
                require((observed["os"], observed["cpu"], observed["execution_arch"], observed["execution_mode"]) ==
                        (slot["os"], slot["arch"], slot["arch"], "native") and observed["page_size"] > 0 and observed["kernel"],
                        "Actual native host differs from selected slot")
                require(host is None or all(host[key] == observed[key] for key in
                        ("os", "cpu", "execution_arch", "kernel", "page_size", "execution_mode")), "Contradictory native host facts")
                host = observed
                if not signal:
                    interval(incoming)
                if signal:
                    if signal_supply is None:
                        from m2_fixture_binding import bind_auxiliary
                        signal_supply = evidence.json(selection["signal_fixture_producer"])
                        require(signal_supply["status"] == "pass" and signal_supply["sdk"] == producer["sdk"] and
                                signal_supply["sdk_recipe"] == producer["sdk_recipe"], "Current signal fixture producer required")
                        report["signal_fixture_binding"] = bind_auxiliary(evidence, signal_supply, producer, signal_auxiliary_units(), report)
                    cli_name = {"signal-queue": "signal-host", "signal-mixed": "signal-observer",
                                "signal-budget": "signal-fault", "signal-late": "signal-late"}.get(scope)
                    cli = next(row for row in (signal_supply if cli_name else producer)["artifacts"] if row["name"] == (cli_name or "qaplet"))
                    require(same(incoming["cli"], cli), "Signal actual CLI/C Host/observer artifact differs from its declared scope")
                    evidence.read(incoming["cli"])
                    for retained in incoming["consumer_identity"]["files"]:
                        require(same(retained["original"], retained["retained"]), "Signal consumer source changed before capture")
                        evidence.read(retained["retained"])
                    if incoming.get("host_artifact"):
                        require(same(incoming["host_artifact"], next(row for row in signal_supply["artifacts"] if row["name"] == "signal-host")),
                                "Signal Console/direct-child Host differs")
                elif not scope.startswith("backend-"):
                    require(incoming["sdk"] == producer["sdk"] and incoming["sdk_recipe"] == producer["sdk_recipe"],
                            "Selected SDK/recipe differs: " + scope)
                count = len(incoming.get("cases", []))
                reuse = reference.get("accepted_ticket23", False)
                if reuse:
                    require(accepted is not None, "Explicit accepted receipt required for scoped reuse")
                    selected = [row for row in accepted["selected_reports"] if same(row["report"], identity(path))]
                    require(len(selected) == 1 and selected[0]["status"] == "pass", "Report was not independently accepted at ticket23")
                    require(scope in ("normal", "examples", "cli", "signal-sender", "signal-strong"), "Unsupported reuse scope")
                    from core_gate import normalized
                    def recheck_accepted_raw(node):
                        if isinstance(node, list):
                            for value in node:
                                recheck_accepted_raw(value)
                        elif isinstance(node, dict):
                            for key, value in node.items():
                                if key.endswith("_log") and isinstance(value, str):
                                    require(normalized(value) in accepted_files, "Accepted raw log lacks its original inventory binding")
                                    if normalized(value) not in verified_accepted_files:
                                        evidence.read(accepted_files[normalized(value)])
                                        verified_accepted_files.add(normalized(value))
                                elif key == "path" and isinstance(value, str) and normalized(value) in accepted_files:
                                    if normalized(value) not in verified_accepted_files:
                                        evidence.read(accepted_files[normalized(value)])
                                        verified_accepted_files.add(normalized(value))
                                recheck_accepted_raw(value)
                    recheck_accepted_raw(incoming)
                elif scope == "examples":
                    count = validate_examples(incoming, evidence)
                elif scope == "cli":
                    require(same(incoming["cli_artifact"], next(row for row in producer["artifacts"] if row["name"] == "qaplet")),
                            "CLI public artifact differs from current producer")
                    oracle_ref = reference.get("contract_oracle")
                    require(oracle_ref and selection.get("contract_oracle_receipt"), "Explicit accepted public CLI contract oracle required")
                    oracle_receipt = json.loads(evidence.read(selection["contract_oracle_receipt"]))
                    from hashlib import sha256
                    require(sha256(evidence.read(selection["contract_oracle_receipt"])).hexdigest() == ACCEPTED_23,
                            "CLI contract oracle receipt differs")
                    oracle_path = evidence.resolve(oracle_ref["path"] if isinstance(oracle_ref, dict) else oracle_ref)
                    require(any(same(row["report"], identity(oracle_path)) for row in oracle_receipt["selected_reports"]),
                            "Public CLI contract oracle was not accepted")
                    count = validate_cli(incoming, json.loads(evidence.read(oracle_ref)), evidence)
                elif signal and scope not in ("signal-sender", "signal-strong"):
                    count = validate_signal_special(incoming, scope.removeprefix("signal-"), slot["os"] == "Windows", evidence, path,
                                                    slot["mandatory"][scope]["case_ids"])
                elif signal:
                    count = validate_signal(incoming, scope == "signal-strong", slot["os"] == "Windows", evidence,
                                            slot["mandatory"][scope]["case_ids"])
                elif scope == "fault":
                    key = incoming["consumer_report"]["sha256"]
                    if key not in bindings:
                        from m2_fixture_binding import bind_auxiliary
                        bindings[key] = bind_auxiliary(evidence, evidence.json(incoming["consumer_report"]), producer, auxiliary_units("fault"), report)
                    require(bindings[key] == incoming["target_component_binding"]["artifacts"], "Actual auxiliary fault closure binding differs")
                    count = verify_static_cases(incoming, fault_catalog(slot["os"] == "Windows"), evidence)
                elif scope == "tty":
                    from m2_fixture_binding import bind_auxiliary
                    auxiliary = incoming if incoming.get("scope") == "tty-consume" else evidence.json(incoming["producer_report"])
                    key = "tty-" + auxiliary["report_id"]
                    if key not in bindings:
                        bindings[key] = bind_auxiliary(evidence, auxiliary, producer, {
                            "qaplet-tty-host": {"units": ["sdk/host-tty.c"], "wrapped_symbols": []},
                            "qaplet-tty-faults": {"units": ["sdk/tty-faults.c"], "wrapped_symbols":
                                ["tcsetattr", "SetConsoleMode", "WriteConsole", "poll", "pthread_create", "read"]}}, report)
                    report["tty_fixture_binding"] = bindings[key]
                    count = verify_static_cases(incoming, tty_catalog(slot["os"] == "Windows"), evidence)
                elif scope == "mixed":
                    key = incoming["consumer_report"]["sha256"]
                    if key not in bindings:
                        from m2_fixture_binding import bind_auxiliary
                        bindings[key] = bind_auxiliary(evidence, evidence.json(incoming["consumer_report"]), producer, auxiliary_units("mixed"), report)
                    require(bindings[key] == incoming["target_component_binding"]["artifacts"], "Actual auxiliary mixed closure binding differs")
                    count = validate_mixed(incoming, evidence)
                elif scope.startswith("backend-"):
                    module = __import__("m2_" + scope.removeprefix("backend-"))
                    cases = incoming["cases"]
                    required = set(incoming["required_cases"])
                    if scope == "backend-stdio":
                        required = module.COMMON | module.TTY
                    elif scope == "backend-process":
                        required = module.REQUIRED | (module.WINDOWS_REQUIRED if slot["os"] == "Windows" else set())
                    elif scope == "backend-tty":
                        required = module.REQUIRED | ({"stdin.console", "text.supplementary-input"} if slot["os"] == "Windows" else set())
                    else:
                        required = module.REQUIRED
                    require({row["case_id"] for row in cases} == set(required) and
                            len({row["case_id"] for row in cases}) == len(cases), "Backend mandatory inventory missing/duplicate/unexpected")
                    require(all(row["status"] == "pass" for row in cases), "Backend case incomplete")
                    if scope == "backend-stdio":
                        for group in (module.COMMON, module.TTY):
                            actual_cases = [row for row in cases if row["case_id"] in group]
                            checked = {"cases": []}
                            command = command_for(actual_cases[0])
                            module.collect(checked, actual_cases, command, group, observed)
                            require(all(row["status"] == "pass" for row in checked["cases"]), "Actual stdio backend differs")
                    else:
                        for row in cases:
                            require(module.matches_contract(row, observed), "Actual backend semantics differ")
                    for command in incoming["commands"]:
                        require(not command["timed_out"] and command["exit_code"] == 0, "Backend command incomplete")
                        evidence.read(command["stdout_log"]);evidence.read(command["stderr_log"])
                elif scope in ("m2-lifecycle", "m2-mixed-cycles"):
                    require(len(incoming["cases"]) == 1, "Exactly one actual cycle command required")
                    case = incoming["cases"][0];command = command_for(case)
                    require(case["case_id"] == ("m2-lifecycle" if scope == "m2-lifecycle" else "m2-mixed"),
                            "M2 lifecycle/mixed command identity differs")
                    fixture = json.loads(evidence.read(incoming["fixture_producer"]))
                    require(fixture["status"] == "pass" and fixture["report_kind"] == "qaplet-m2-cycles" and
                            fixture["scope"] == "build" and fixture["sdk"] == producer["sdk"] and
                            fixture["sdk_recipe"] == producer["sdk_recipe"] and same(case["artifact"], fixture["artifacts"][0]) and
                            fixture["artifacts"][0]["audit"]["status"] == "pass", "Cycle APE/SDK producer join differs")
                    evidence.read(case["artifact"])
                    source_rows = [row for row in fixture["compiler_inputs"] if Path(row["original"]["path"]).name == "cycles.c"]
                    require(len(source_rows) == 1 and same(source_rows[0]["retained"], identity(ROOT / "tests/native/sdk_m2_cycles.c")),
                            "Actual public M2 cycle source required; M1 lifetime cannot substitute")
                    for retained in fixture["compiler_inputs"]:
                        evidence.read(retained["retained"])
                    for packing in fixture["packing"]:
                        evidence.read(packing["original_packer_output"])
                    raw = evidence.read(case.get("progress", command["stdout_log"]))
                    errors = evidence.read(command["stderr_log"])
                    mode, minimum = ("lifecycle", 1000) if scope == "m2-lifecycle" else ("mixed", 100)
                    actual = sdk_m2_cycles.inspect(raw, errors, command["exit_code"], mode, case["observations"]["requested"])
                    require(actual == case["observations"] and actual["completed"] >= minimum and command["timed_out"] is False,
                            "Actual M2 cycle minimum/observations differ")
                elif scope == "normal":
                    loader_dir = evidence.resolve(selection["loader_dir"])
                    sdk_matrix.validate_native(SimpleNamespace(loader_dir=loader_dir, loader_manifest=None), incoming,
                                               producer, identity(producer_path), evidence, args.report.parent)
                else:
                    raise ValueError("Scope needs independently accepted current receipt: " + scope)
                slot["mandatory"][scope] = {"status": "pass", "report": identity(path), "cases": count,
                    "host": signal_host or observed, "native_host_anchor": host if signal_host else None,
                    "reuse": "accepted-ticket23-exact-scope" if reuse else None,
                    "case_ids": slot["mandatory"][scope]["case_ids"],
                    "required_case_ids": slot["mandatory"][scope]["required_case_ids"],
                    "optional_case_ids": slot["mandatory"][scope]["optional_case_ids"],
                    "cleanup_false": [row.get("case_id", row.get("case", scope)) for row in incoming.get("cases", [incoming])
                        if (scope != "examples" or row.get("case_id", "").startswith(("qaplet-example.", "qaplet-m2-example."))) and row.get("native_cleanup_complete", True) is False],
                    "cleanup_null": [row.get("case_id", row.get("case", scope)) for row in incoming.get("cases", [incoming])
                        if (scope != "examples" or row.get("case_id", "").startswith(("qaplet-example.", "qaplet-m2-example."))) and "native_cleanup_complete" in row and row["native_cleanup_complete"] is None]}
                report["selected_inputs"].append(identity(path))
            slot["host"] = host
            slot["status"] = "pass" if all(row["status"] == "pass" for row in slot["mandatory"].values()) else "blocked"
        sdk.check_package(producer, evidence.resolve(selection["distribution_root"]))
        for source in evaluator_inputs:
            require(same(identity(Path(source["original"]["path"])), source["retained"]), "Evaluator source changed during replay")
        for retained in list(evidence.index.values()):
            evidence.read({"path": retained["original_path"], "sha256": retained["sha256"], "size_bytes": retained["size_bytes"]})
        report["actual_parent_imports"] = [{"module": name, **identity(Path(module.__file__))} for name, module in sorted(sys.modules.items())
            if getattr(module, "__file__", None) and Path(module.__file__).is_file() and Path(module.__file__).resolve().parent == ROOT / "scripts"]
        report.update(status="pass" if all(slot["status"] == "pass" for slot in report["coverage"]) else "blocked",
                      local_status="pass" if all(slot["status"] == "pass" for slot in report["coverage"][:2]) else "blocked",
                      reason="Explicit native mandatory inventory; missing native slots/scopes remain not_run/blocked")
    finally:
        report["evidence_index"] = list(evidence.index.values())


def main():
    from build_ape import BuildError
    from foundation import now
    from workflow_support import guard_evidence_outputs, write_evidence_report
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("operation", choices=("inventory", "evaluate", "fault-run", "mixed-run"))
    parser.add_argument("--selection", type=Path)
    parser.add_argument("--producer", type=Path)
    parser.add_argument("--consumer", type=Path)
    parser.add_argument("--supplied", type=Path)
    parser.add_argument("--artifact-dir", type=Path)
    parser.add_argument("--loader-dir", type=Path)
    parser.add_argument("--loader-manifest", type=Path)
    parser.add_argument("--timeout", type=int, default=120)
    parser.add_argument("--expected-os")
    parser.add_argument("--expected-arch")
    parser.add_argument("--runner-label", default="local")
    parser.add_argument("--controlled-probe", action="store_true")
    parser.add_argument("--report", type=Path, required=True)
    args = parser.parse_args()
    guard_evidence_outputs([args.report], [path for path in (args.selection, args.producer, args.consumer) if path],
                           [path for path in (args.supplied, args.artifact_dir) if path])
    report = {"schema_version": 1, "report_kind": "qaplet-m2-native-matrix", "report_id": str(uuid.uuid4()),
              "started_at": now(), "status": "not_run", "release_ready": False}
    try:
        if args.operation == "inventory":
            report.update(status="blocked", coverage=inventory(), reason="No actual native reports selected")
        elif args.operation in ("fault-run", "mixed-run"):
            {"fault-run": fault_run, "mixed-run": mixed_run}[args.operation](args, report)
        else:
            if not args.selection:
                raise ValueError("Explicit selection required")
            evaluate(args, report)
    except (OSError, ValueError, KeyError, TypeError, StopIteration, BuildError) as error:
        report.update(status="fail", reason=str(error))
    report["finished_at"] = now()
    write_evidence_report(args.report, report)
    print(json.dumps({"status": report["status"], "reason": report["reason"], "report": str(args.report)}))
    return 0 if report["status"] == "pass" else 2 if report["status"] == "blocked" else 1


if __name__ == "__main__":
    sys.exit(main())
