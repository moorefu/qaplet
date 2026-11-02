#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Temporary bounded M1 native diagnosis; never signs Fast/Cross acceptance."""
import argparse
import json
import os
from pathlib import Path
import sys
import uuid

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from build_ape import BuildError, identity, run
from core_gate import Evidence
from foundation import ROOT, host_facts, now
from nightly_toolchain import write_json
import sdk
import sdk_guest
from startup import ci_facts, check_ci_commit, measured_host, select_loader


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-report", type=Path, required=True)
    parser.add_argument("--artifact", type=Path, required=True)
    parser.add_argument("--loader-dir", type=Path, required=True)
    parser.add_argument("--expected-os", default="Darwin")
    parser.add_argument("--expected-arch", required=True)
    parser.add_argument("--runner-label", default="diagnostic")
    parser.add_argument("--repeat", type=int, default=5)
    parser.add_argument("--timeout", type=int, default=15)
    parser.add_argument("--force-cpu-lower-bound", action="store_true")
    parser.add_argument("--report", type=Path, required=True)
    args = parser.parse_args()
    if not 1 <= args.repeat <= 10 or not 1 <= args.timeout <= 30:
        parser.error("Native diagnostic repeat/deadline exceeds its bounded scope")
    args.loader_manifest = None
    args.controlled_probe = False
    directory = args.report.resolve().parent / "evidence" / str(uuid.uuid4())
    directory.mkdir(parents=True)
    report = {"schema_version": 1, "report_kind": "qaplet-sdk-diagnostic", "scope": "native-repeat",
              "report_id": directory.name, "started_at": now(), "status": "not_run", "reason": "",
              "host": host_facts(), "ci": ci_facts(), "commands": [], "cases": [],
              "release_ready": False, "acceptance_claimed": False, "evidence_directory": str(directory)}
    try:
        measured_host(args, report, directory)
        producer = sdk_guest.producer(Evidence({}, ROOT), str(args.build_report.resolve()), args.artifact.resolve().parent, report)
        check_ci_commit(producer, report)
        loader = select_loader(args, report)
        report.update(producer_report=identity(args.build_report), sdk_sha256=producer["sdk"]["sha256"],
                      sdk_recipe_sha256=producer["sdk_recipe"]["sha256"], artifact=identity(args.artifact))
        environment = dict(os.environ, QAPLET_M1_NATIVE_DIAG="1")
        if args.force_cpu_lower_bound:
            environment["QAPLET_M1_FORCE_CPU_LOWER_BOUND"] = "1"
        for suffix, arguments, output in sdk.HOST_CASES:
            if suffix not in ("stop-running", "module-aliases"):
                continue
            arguments = [value.replace("{modules}", str(args.artifact.resolve().parent / "sdk/modules")) for value in arguments]
            for iteration in range(args.repeat):
                name = "qaplet-host." + suffix + "." + str(iteration + 1)
                command, stdout, stderr = run(report, directory, name,
                    [*loader, args.artifact.resolve(), *arguments], args.timeout, environment=environment, expected=None)
                passed = command["exit_code"] == 0 and not command["timed_out"] and stdout == output and all(
                    line.startswith("[DEBUG-m1-14-") for line in stderr.splitlines())
                report["cases"].append({"case_id": name, "status": "pass" if passed else "fail", "command": command,
                    "expected": {"exit_code": 0, "stdout": output, "stderr": "Only tagged diagnostic facts; complete native run still requires empty stderr"},
                    "actual": {"exit_code": command["exit_code"], "timed_out": command["timed_out"], "stdout": stdout, "stderr": stderr}})
        report.update(status="pass" if all(case["status"] == "pass" for case in report["cases"]) else "fail",
                      reason="Bounded supplied-Host diagnostic repetitions; required complete native catalog remains separate")
    except BuildError as error:
        report.update(status=error.status, reason=str(error))
    except (OSError, ValueError, KeyError, TypeError) as error:
        report.update(status="fail", reason=str(error))
    report["finished_at"] = now()
    write_json(args.report, report)
    print(json.dumps({key: report[key] for key in ("status", "reason", "acceptance_claimed")}))
    return 0 if report["status"] == "pass" else 1


if __name__ == "__main__":
    sys.exit(main())
