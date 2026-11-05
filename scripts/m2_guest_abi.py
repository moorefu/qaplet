#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Actual ARM64/64K waitid thunk probe over the fixed public SDK fixture."""
import argparse
import json
from pathlib import Path
import sys
import uuid
from build_ape import BuildError, identity
from core_gate import Evidence, interval, require, same, unique, verify_guest_transport
from foundation import ROOT, now
from nightly_toolchain import write_json
from startup import check_ci_commit
from workflow_support import guard_evidence_outputs, write_evidence_report
import sdk_guest
from sdk_m2_guest import supplied_consumers


def evaluate(args, report):
    evidence = Evidence({}, ROOT)
    try:
        selected = evidence.json(str(args.special_selection.resolve()))
        require(selected["profile"] == "m2-special-guest", "Current special supplier selection required")
        product = sdk_guest.producer(evidence, selected["producer_report"], evidence.resolve(selected["distribution_root"]), report)
        product = {**product, "_reference": selected["producer_report"]["path"]}
        artifacts = supplied_consumers(evidence, selected["consumer_reports"], product, report)
        artifact = artifacts["qaplet-process-faults"]
        loader = evidence.resolve(selected["distribution_root"]) / "ape-aarch64.elf"
        argv = ["/payload/ape-aarch64.elf", "/payload/qaplet-process-faults.com", "--waitid"]
        expected = {"schema_version": 1, "page_size": 65536, "files": [
            {**identity(loader), "guest_path": argv[0]},
            {key: artifact[key] for key in ("path", "sha256", "size_bytes")} | {"guest_path": argv[1]}],
            "commands": [{"case_id": "m2-abi.waitid", "argv": argv, "expected_exit_code": 0, "expected_stderr": ""}]}
        if args.operation == "export":
            require(args.manifest and not args.manifest.exists(), "New ABI manifest required")
            write_json(args.manifest, expected)
            report.update(status="pass", reason="Related pinned waitid thunk ABI exported; actual guest execution remains required", manifest=identity(args.manifest))
            return
        require(args.guest_report is not None, "Actual ABI guest report required")
        guest = evidence.report(str(args.guest_report.resolve()), "arm64-guest")
        check_ci_commit(product, guest)
        files, cases = verify_guest_transport(evidence, guest, report)
        require(guest["guest"]["page_size"] == 65536 and {key for key in cases if key.startswith("m2-abi.")} == {"m2-abi.waitid"}, "Actual ABI catalog/kernel differs")
        executed = evidence.json(guest["manifest"])
        require(unique(executed["commands"])["m2-abi.waitid"] == expected["commands"][0], "Actual ABI manifest command differs")
        for item in expected["files"]:
            require(same(files[item["guest_path"]], item), "ABI payload transport differs")
            evidence.read(item)
        case = cases["m2-abi.waitid"]
        start, end = interval(case)
        require(case["argv"] == argv and case["cwd"] == "/" and case["exit_code"] == 0 and
                (end-start).total_seconds() <= case["timeout_seconds"] and not evidence.read(case["stderr_log"]), "Actual waitid ABI command failed")
        lines = evidence.read(case["stdout_log"]).splitlines()
        actual = json.loads(lines[0])
        require(len(lines) == 2 and lines[1] == b"M2 process waitid: actual pinned thunk; WNOWAIT; EINTR/EINVAL/ECHILD; exit/kill orders; real reap: pass" and
                actual == {"invalid_errno": 22, "interrupted_errno": 4, "reaped_errno": 10, "exit_first": 37,
                           "kill_first_signal": 9, "waitnowait_twice": True, "WEXITED": 4, "WNOWAIT": 16777216, "opaque_siginfo_bytes": 128},
                "Actual pinned waitid thunk options/opaque buffer/errno/reap observations differ")
        report.update(status="pass", reason="Actual waitid WNOWAIT/EINTR/EINVAL/ECHILD and exit/kill/reap orders verified on ARM64/65536",
                      evidence_verified=True, sdk_sha256=product["sdk"]["sha256"], guest=guest["guest"],
                      guest_report=identity(args.guest_report), special_selection=identity(args.special_selection),
                      cases=[{"case_id": "m2-abi.waitid", "status": "pass", "actual": actual, "native_cleanup_complete": True}])
    finally:
        report["evidence_index"] = list(evidence.index.values())


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("operation", choices=("export","verify"))
    parser.add_argument("--special-selection",type=Path,required=True)
    parser.add_argument("--manifest",type=Path)
    parser.add_argument("--guest-report",type=Path)
    parser.add_argument("--report",type=Path,required=True)
    args=parser.parse_args()
    outputs=[args.report]+([args.manifest] if args.operation=="export" else [])
    require(all(path is not None and not path.exists() for path in outputs),"New ABI output paths required")
    guard_evidence_outputs(outputs,[path for path in (args.special_selection,args.guest_report) if path])
    report={"schema_version":1,"report_kind":"m2-guest-related-abi","report_id":str(uuid.uuid4()),"started_at":now(),
            "status":"not_run","evidence_verified":False,"release_ready":False,"evaluator":identity(Path(__file__))}
    try:evaluate(args,report)
    except BuildError as error:report.update(status=error.status,reason=str(error))
    except (OSError,ValueError,KeyError,TypeError,IndexError) as error:report.update(status="fail",reason=str(error))
    report["finished_at"]=now();write_evidence_report(args.report,report)
    print(json.dumps({key:report[key] for key in ("status","reason","evidence_verified")}))
    return report["status"]!="pass"


if __name__=="__main__":sys.exit(main())
