#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Read-only M2 backend/deep ABI replay over the strict full-system transport."""
import argparse
import importlib
import json
from pathlib import Path
import sys
import uuid

from build_ape import BuildError, headers, identity
from core_gate import Evidence, interval, require, same, verify_guest_transport, without_paths
from foundation import ROOT, now
from input_lock import consume_identity
from startup import check_ci_commit
from workflow_support import guard_evidence_outputs, write_evidence_report
import pages64k


def evaluate(args, report):
    evidence = Evidence({}, ROOT)
    try:
        guest = evidence.report(str(args.guest_report.resolve()), "arm64-guest")
        files, cases = verify_guest_transport(evidence, guest, report)
        require(guest["guest"]["page_size"] == 65536, "Actual ARM64/65536 backend kernel required")
        execution = {"os": "Linux", "execution_arch": "aarch64", "page_size": 65536,
                     "kernel": guest["guest"]["kernel"], "execution_mode": "emulated-system"}
        all_cases, results = [], {}
        for name in ("waker", "process", "stdio", "tty", "pages64k"):
            root = args.supply_root.resolve() / ("backend-" + name)
            supply = evidence.json(str(root / "supply.json"))
            require(supply["status"] == "pass" and supply["probe"] == name, "Passing exact backend supply receipt required")
            producer = evidence.json(supply["producer"])
            require(producer["status"] == "pass", "Backend producer failed")
            consume_identity(producer, report)
            check_ci_commit(producer, guest)
            for row in supply["inputs"]:
                require(same(row["original"], row["retained"]), "First-compile backend input bytes conflict")
                evidence.read(row["retained"])
            require(supply["packing"] and all(row.get("original_packer_output") for row in supply["packing"]),
                    "Original backend packer diagnostics required")
            for row in supply["packing"]:
                evidence.read(row["original_packer_output"])
            artifact, = producer["artifacts"]
            evidence.read(artifact)
            sidecars = artifact["headers"]["sidecars"]
            for entry in sidecars:
                evidence.read(entry)
            require(without_paths(headers(evidence.resolve(artifact["path"]), [evidence.resolve(row["path"]) for row in sidecars])) ==
                    without_paths(artifact["headers"]), "Backend fat APE headers differ from supplied bytes")
            for row in artifact["audit"]["maps"]:
                evidence.read(row)
            require(artifact["audit"]["status"] == "pass" and not artifact["audit"]["forbidden_service_symbols"], "Audited backend APE required")
            for command in artifact["audit"]["commands"]:
                evidence.command(command, 0)
            matches = [row for row in files.values() if same(row, artifact)]
            require(len(matches) == 1, "Guest must transfer exactly one selected backend APE")
            transferred = matches[0]["guest_path"]
            selected = [row for row in cases.values() if len(row["argv"]) >= 2 and row["argv"][1] == transferred]
            required_ids = {"pages64k.facts", "pages64k.suite"} if name == "pages64k" else {"m2-" + name + ".suite"}
            require({row["case_id"] for row in selected} == required_ids, "Backend suite/facts invocation inventory differs")
            module = None if name == "pages64k" else importlib.import_module("m2_" + name)
            actual_result = {"cases": []}
            for case in selected:
                start, end = interval(case)
                require((end - start).total_seconds() <= case["timeout_seconds"] and case["exit_code"] == 0 and
                        not evidence.read(case["stderr_log"]), "Backend failed actual exit/output/deadline")
                require(case["argv"][0] in files and same(files[case["argv"][0]], producer["portable_helpers"]["loader"]),
                        "Backend executed with another ARM loader")
                raw = evidence.read(case["stdout_log"])
                if name == "pages64k":
                    if case["case_id"] == "pages64k.facts":
                        require(json.loads(raw) == {"probe": "pages64k", "arch": "aarch64", "page_size": 65536}, "Deep ABI facts disagree")
                    else:
                        rows = [json.loads(line) for line in raw.splitlines()]
                        require(len(rows) == len(pages64k.REQUIRED) and {row["case_id"] for row in rows} ==
                                {"pages64k." + key for key in pages64k.REQUIRED} and
                                all(row["status"] == "pass" and row["actual"] == row["expected"] for row in rows),
                                "Deep ABI missing/duplicate/failed observations")
                        actual_result["cases"] = rows
                else:
                    event = {key: case[key] for key in ("argv", "started_at", "finished_at", "exit_code")}
                    event["timed_out"] = False
                    rows = [json.loads(line) for line in raw.splitlines()]
                    if name == "stdio":
                        module.collect(actual_result, rows, event, module.COMMON | module.TTY, execution)
                    else:
                        module.collect_observations(actual_result, rows, event, artifact, execution, case["timeout_seconds"])
            if name == "pages64k":
                shell = cases["pages64k.shell-header"]
                require(shell["argv"][1:] == ["sh", transferred, "--facts"] and shell["exit_code"] == 0 and
                        json.loads(evidence.read(shell["stdout_log"])) == {"probe": "pages64k", "arch": "aarch64", "page_size": 65536} and
                        not evidence.read(shell["stderr_log"]), "Actual shell/header deep ABI startup failed")
            for row in actual_result["cases"]:
                all_cases.append({**row, "case_id": ("m2-" + name + "." + row["case_id"]) if name != "pages64k" else row["case_id"]})
            results[name] = {"supply": identity(root / "supply.json"), "producer": supply["producer"],
                             "actual_observations": len(actual_result["cases"]), "status": "pass"}
        require(all(row["status"] == "pass" for row in all_cases), "Nonpassing backend observation")
        report.update(status="pass", reason="Current four OS backends and deep ABI verified against strict ARM64/65536 transport",
                      evidence_verified=True, cases=all_cases, supplies=results, guest=guest["guest"],
                      guest_report=identity(args.guest_report), execution_mode="emulated-system")
    finally:
        report["evidence_index"] = list(evidence.index.values())


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--guest-report", type=Path, required=True)
    p.add_argument("--supply-root", type=Path, required=True)
    p.add_argument("--report", type=Path, required=True)
    args = p.parse_args()
    guard_evidence_outputs([args.report], [args.guest_report],
                           [args.supply_root / ("backend-" + name) for name in ("waker", "process", "stdio", "tty", "pages64k")])
    require(not args.report.exists(), "Existing evidence output is immutable; use a new path")
    report = {"schema_version": 1, "report_kind": "m2-guest-backend", "report_id": str(uuid.uuid4()),
              "started_at": now(), "status": "not_run", "evidence_verified": False, "release_ready": False,
              "evaluator": identity(Path(__file__))}
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
