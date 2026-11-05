#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Mixed lifetime consumers: Ubuntu compiles the package; other hosts replay it."""
import argparse
import json
import os
from pathlib import Path
import shutil
import sys
import uuid

import ape_helpers
from build_ape import BuildError, audit, identity, link_portable, publish_ape, run
from foundation import host_facts, now, sha256
from input_lock import consumption_purpose
from startup import measured_host, select_loader
from workflow_support import guard_evidence_outputs, write_evidence_report

STOP = "M2 mixed stop: DONE/IDLE distinct; full queue stop only publishes; owner cancels work/timer/IO and force-reaps child before close; no user jobs; CLOSED retains work through retry/BUSY; other resources retire; actual quiescence and joined producers: pass\n"
PENDING = "M2 mixed nonreturning: work remains physically held at process exit; CLOSED engine zero; repeated zero/finite quiesce pending and destroy BUSY; child reaped, stdio/TTY restored and other IO retired; no native cleanup success claimed: pass\n"
UNKNOWN = "M2 mixed uncertain close: stop remains first cause across CLOSED; replacement identity survives repeated cleanup; unknown handle retains BUSY; child/stdio/TTY/IO and work actually retire independently; no cleanup success claimed: pass\n"
NORMAL = ("stop", "races", "nonreturning", "tty", "tty-races", "tty-nonreturning")
FAULT = ("notify", "uncertain", "initialization", "wait-budget", "tty-notify", "tty-uncertain")


def compile_consumers(args, report, directory, tree, helpers, sdk, work, output):
    for fault in (False, True):
        build = work / ("fault" if fault else "normal")
        (build / "tmp").mkdir(parents=True)
        name = "qaplet-mixed-faults" if fault else "qaplet-mixed-host"
        source = sdk / ("mixed-faults.c" if fault else "host-mixed.c")
        symbols = ("socket", "close", "write", "pthread_cond_timedwait") if fault else ()
        environment = dict(os.environ, TMPDIR=str(build / "tmp"), SOURCE_DATE_EPOCH="0", LC_ALL="C", TZ="UTC",
                           BUILDLOG=str(output / (name + ".build-commands.log")))
        report.setdefault("external_mixed_consumers", []).append({
            "source": identity(source), "include_directories": [str(sdk / "include")],
            "archive": identity(sdk / "lib/libqaplet.a"), "private_headers": False,
            "wrapped_symbols": list(symbols), "controlled_system_fixture": fault})
        run(report, directory, name + "-compile", [tree / "bin/cosmocc", "-Os", "-D_GNU_SOURCE",
            *([] if fault else ["-DSDK_MIXED_STANDALONE"]), "-I" + str(sdk / "include"),
            "-ffile-prefix-map=" + str(sdk) + "=sdk", "-c", source, "-o", build / "host.o"],
            args.compile_timeout, build, environment)
        link_portable(args, report, directory, tree, helpers, name, [build / "host.o", sdk / "lib/libqaplet.a"],
            build / (name + ".com"), build, environment,
            libraries=(*("-Wl,--wrap=" + s for s in symbols), "-lm", "-lpthread"))
        publish_ape(build, output, name, helpers, ape_helpers.source_cache(args.helper_source_cache.resolve(),
                    consumption_purpose("build", args.controlled_probe)))
        artifact = audit(args, report, directory, tree, name, output)
        artifact["build_commands"] = identity(output / (name + ".build-commands.log"))
        report["artifacts"].append(artifact)
    shutil.copyfile(sdk / "process-child.exe", output / "process-child.exe")
    report["mixed_native_child"] = identity(output / "process-child.exe")


def terminal_readback(work):
    path = work / "mixed-terminal.ndjson"
    rows = [json.loads(line) for line in path.read_text(encoding="utf-8").splitlines()]
    values = {row["stage"]: {k: v for k, v in row.items() if k != "stage"} for row in rows}
    if len(rows) != 3 or set(values) != {"before", "modified", "restored"} or values["before"] != values["restored"] or values["before"] == values["modified"]:
        raise BuildError("Mixed terminal full native mode/flags restoration is not proven")
    return {"artifact": identity(path), "rows": rows, "complete_restore": True}


def run_supplied(args, report, directory, artifacts, loader):
    windows = report["host"]["os"] == "Windows"
    for fault, cases in ((False, NORMAL), (True, FAULT)):
        artifact = artifacts / ("qaplet-mixed-faults.com" if fault else "qaplet-mixed-host.com")
        before = identity(artifact)
        if not windows:
            artifact.chmod(artifact.stat().st_mode | 0o111)
        for suffix in cases:
            name = artifact.stem + "." + suffix
            work = directory / name
            work.mkdir()
            mode = "--m2-mixed-" + suffix
            expected = PENDING if "nonreturning" in suffix else UNKNOWN if "uncertain" in suffix else STOP
            argv = [*loader, artifact, mode, artifact, loader[0] if loader else "-", artifacts / "process-child.exe"]
            native = None
            if windows and suffix.startswith("tty"):
                from m2_tty_terminal import conpty
                native, raw = conpty(argv, work, args.timeout)
                (work / "console.json").write_text(json.dumps(native, indent=2) + "\n", encoding="utf-8")
                executed = native
                report["commands"].append(native)
                code, timed_out = native["exit_code"], native["timed_out"]
                cleanup = not timed_out and not native["error"] and all(native["cleanup"].values())
            else:
                executed, stdout, stderr = run(report, directory, name, argv, args.timeout, work, expected=None)
                raw = Path(executed["stdout_log"]).read_bytes()
                code, timed_out = executed["exit_code"], executed["timed_out"]
                cleanup = not Path(executed["stderr_log"]).read_bytes()
            marker = work / "mixed-result.txt"
            correct_marker = marker.is_file() and marker.read_bytes() == expected.encode()
            passed = code == 0 and not timed_out and cleanup and correct_marker
            terminal = terminal_readback(work) if passed and suffix.startswith("tty") else None
            waits = work / "mixed-waits.ndjson"
            records = [json.loads(line) for line in waits.read_text(encoding="utf-8").splitlines()] if waits.is_file() else []
            if [row["requested_active_wait_ms"] for row in records] != [0, 5, 5]:
                passed = False
            report["cases"].append({
                "case_id": name, "required": True, "status": "pass" if passed else "fail", "command": executed,
                "artifact": before, "sdk_sha256": report["sdk"]["sha256"], "controlled_system_fixture": fault,
                "expected_marker": expected, "marker": identity(marker) if marker.is_file() else None,
                "actual": {"exit_code": code, "timed_out": timed_out, "stdout_hex": raw.hex()},
                "native_console": native, "terminal_readback": terminal, "wait_observations": records,
                "native_cleanup_complete": not any(s in suffix for s in ("nonreturning", "uncertain")),
                "scope": "Public SDK/JS mixed resources; finite active wait is not whole-call hard duration; supervisor timeout never proves native cleanup"})
        if identity(artifact) != before:
            raise BuildError("Mixed consumer artifact changed during execution")


def main():
    import sdk
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--producer", type=Path, required=True)
    parser.add_argument("--consumer", type=Path, required=True)
    parser.add_argument("--supplied-host", type=Path, required=True)
    parser.add_argument("--artifact-dir", type=Path, required=True)
    parser.add_argument("--report", type=Path, required=True)
    parser.add_argument("--loader-dir", type=Path)
    parser.add_argument("--loader-manifest", type=Path)
    parser.add_argument("--expected-os")
    parser.add_argument("--expected-arch")
    parser.add_argument("--runner-label", default="local")
    parser.add_argument("--timeout", type=int, default=30)
    args = parser.parse_args()
    directory = args.report.resolve().parent / "evidence" / str(uuid.uuid4())
    guard_evidence_outputs([args.report, directory], [args.producer, args.consumer, args.loader_manifest],
                           [args.artifact_dir, args.supplied_host.parent])
    directory.mkdir(parents=True)
    producer = json.loads(args.producer.read_text(encoding="utf-8"))
    consumer = json.loads(args.consumer.read_text(encoding="utf-8"))
    report = {"schema_version": 1, "report_kind": "qaplet-sdk-mixed", "scope": "run", "status": "not_run",
              "started_at": now(), "host": host_facts(), "cases": [], "commands": [], "sdk": producer["sdk"],
              "sdk_recipe": producer["sdk_recipe"], "producer_report": identity(args.producer),
              "consumer_report": identity(args.consumer), "artifacts": [], "ci": None}
    try:
        if producer["status"] != "pass" or consumer["status"] != "pass":
            raise BuildError("Successful Ubuntu SDK and external mixed consumer producers required")
        sdk.check_package(producer, args.supplied_host.parent.resolve())
        for artifact in consumer["artifacts"]:
            path = args.artifact_dir / Path(artifact["path"]).name
            if sha256(path) != artifact["sha256"] or path.stat().st_size != artifact["size_bytes"] or artifact["audit"]["status"] != "pass":
                raise BuildError("Supplied mixed consumer differs from audited Ubuntu producer")
            report["artifacts"].append(identity(path))
        if sha256(args.artifact_dir / "process-child.exe") != consumer["mixed_native_child"]["sha256"]:
            raise BuildError("Mixed native child differs from Ubuntu package consumer")
        measured_host(args, report, directory)
        loader = select_loader(args, report)
        run_supplied(args, report, directory, args.artifact_dir.resolve(), loader)
        if sdk.recipe() != producer["sdk_recipe"]["files"]:
            raise BuildError("SDK recipe changed during mixed replay")
        report.update(status="pass" if all(c["status"] == "pass" for c in report["cases"]) else "fail",
                      reason="Same Ubuntu-built external mixed consumers; expected physical retention remains explicit")
    except (OSError, ValueError, KeyError, BuildError) as error:
        report.update(status="fail", reason=str(error))
    report["finished_at"] = now()
    write_evidence_report(args.report, report)
    print(json.dumps({"status": report["status"], "reason": report["reason"], "report": str(args.report)}))
    return 0 if report["status"] == "pass" else 1


if __name__ == "__main__":
    sys.exit(main())
