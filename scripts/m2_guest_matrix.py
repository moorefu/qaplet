#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Index completed ticket25 groups without occupying a native ARM64 slot.

This local matrix joins explicit strict consumer reports and checks their raw
byte indices and full-system inputs. The independent final evidence gate is a
separate ticket; this index is not a release or native-performance assertion.
"""
import argparse
import json
from pathlib import Path
import sys
import uuid
from build_ape import BuildError, identity
from core_gate import Evidence, require, same, verify_guest_transport
from foundation import ROOT, now
from workflow_support import guard_evidence_outputs, write_evidence_report
import sdk_guest


GROUPS = {"ordinary": ("qaplet-sdk-guest", 229), "special": ("m2-special-guest", 50),
          "cli": ("m2-cli-guest", 30), "backend": ("m2-guest-backend", 82),
          "abi": ("m2-guest-related-abi", 1), "cycles": ("m2-guest-cycles", 2)}


def evaluate(args, report):
    evidence = Evidence({}, ROOT)
    try:
        product = sdk_guest.producer(evidence, str(args.sdk_build.resolve()), args.distribution.resolve(), report)
        target = product["sdk"]["sha256"]
        groups, checked, commands = {}, set(), 0
        for name, (kind, expected) in GROUPS.items():
            path = getattr(args, name)
            selected = evidence.report(str(path.resolve()), kind)
            require(selected.get("evidence_verified") is True and selected.get("release_ready") is False,
                    "Mandatory strict local consumer required: " + name)
            if name != "backend":
                require(selected["sdk_sha256"] == target, "Consumer target SDK differs: " + name)
            require(selected["guest"]["os"] == "Linux" and selected["guest"]["architecture"] == "aarch64" and
                    selected["guest"]["page_size"] == 65536 and selected["guest"]["execution_mode"] == "emulated-system",
                    "Current actual ARM64/65536 full-system group required: " + name)
            for item in selected["evidence_index"]:
                key = (item["resolved_path"], item["sha256"], item["size_bytes"])
                if key not in checked:
                    evidence.read({"path": key[0], "sha256": key[1], "size_bytes": key[2]})
                    checked.add(key)
            actual_guest = evidence.report(selected["guest_report"], "arm64-guest")
            _, cases = verify_guest_transport(evidence, actual_guest, report)
            # Read the original full RAM rootfs, kernel/config, QEMU and signed
            # package inputs too. A digest string alone is not a retained input.
            for key in ("rootfs", "kernel", "config", "executable", "compressed_kernel", "init"):
                evidence.read(actual_guest["qemu"][key])
            for item in actual_guest["inputs"]:
                evidence.read(item)
            require(all(item["status"] == "pass" for item in cases.values()), "Missing/failed/skip guest command")
            commands += len(cases)
            if name == "cycles":
                require(set(selected["cycles"]) == {"lifecycle", "mixed"}, "Missing mandatory cycle group")
                for mode, count in (("lifecycle", 1000), ("mixed", 100)):
                    actual = selected["cycles"][mode]
                    require(actual["completed"] == actual["requested"] == actual["created"] == actual["closed"] ==
                            actual["quiescent"] == actual["destroyed"] == count and actual["warmup_instances"] == 3,
                            "Mandatory actual cycle counts/retirement differ")
                mixed = selected["cycles"]["mixed"]
                require((mixed["normal"], mixed["cancelled"], mixed["errors"]) == (34, 33, 33) and
                        mixed["child_reaped"] == mixed["producer_joined"] == mixed["tty"] == 100 and
                        mixed["quiescent_helpers"] == mixed["quiescent_workers"] == mixed["quiescent_keepalive_tokens"] == 0,
                        "Actual mixed cycle resources/phase counts differ")
                observation_count = 2
            else:
                require(len(selected["cases"]) == expected and len({item["case_id"] for item in selected["cases"]}) == expected and
                        all(item["status"] == "pass" for item in selected["cases"]), "Mandatory semantic catalog incomplete: " + name)
                observation_count = len(selected["cases"])
            groups[name] = {"strict_consumer": identity(path), "guest_report": selected["guest_report"],
                            "transport_commands": len(cases), "semantic_observations": observation_count,
                            "sdk_link_scope": "independent OS/libc probes; no SDK archive linked" if name == "backend" else "explicit target SDK",
                            "status": "pass"}
        require(commands == 319, "Unexpected actual complete command count")
        ordinary = evidence.json(str(args.ordinary.resolve()))
        basic_cli = [item["case_id"] for item in ordinary["cases"] if item["case_id"].startswith("qaplet.")]
        require(len(basic_cli) == 53 and any(item["case_id"] == "qaplet-host.m2-process-inherit" for item in ordinary["cases"]),
                "Basic CLI53 or mandatory high-FD inheritance missing")
        report.update(status="pass", reason="Current mandatory ARM64/65536 ticket25 groups completed and retained raw inputs verified",
                      evidence_verified=True, sdk_sha256=target, sdk_producer=identity(args.sdk_build), groups=groups,
                      transport_commands=commands, semantic_observations=394,
                      cycles={"lifecycle":1000,"mixed":100,"warmups_per_group":3},
                      cli_mapping={"basic_cli_in_ordinary":basic_cli,"additional_io_and_terminal":14,"signal_and_cleanup":16,
                                   "queue_scope":"two public SDK relay Host contracts; not CLI end-to-end"},
                      native_arm64_slot=False, execution_mode="emulated-system",
                      scope_notes="TCG timing is observed guest duration, not physical performance. Expected false/null native cleanup scopes remain in selected raw consumer reports. No missing applicable ticket25 contracts; native host matrix, hour soak, formal two-axis review and final gate remain separately owned.")
    finally:
        report["evidence_index"]=list(evidence.index.values())


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    for name in ("sdk-build","distribution",*GROUPS,"report"):
        parser.add_argument("--"+name,type=Path,required=True)
    args=parser.parse_args()
    inputs=[args.sdk_build,*[getattr(args,name) for name in GROUPS]]
    guard_evidence_outputs([args.report],inputs,[args.distribution])
    require(not args.report.exists(),"Existing matrix output is immutable; use a new path")
    report={"schema_version":1,"report_kind":"m2-actual64k-matrix","report_id":str(uuid.uuid4()),"started_at":now(),
            "status":"not_run","evidence_verified":False,"release_ready":False,"evaluator":identity(Path(__file__))}
    try:evaluate(args,report)
    except BuildError as error:report.update(status=error.status,reason=str(error))
    except (OSError,ValueError,KeyError,TypeError) as error:report.update(status="fail",reason=str(error))
    report["finished_at"]=now();write_evidence_report(args.report,report)
    print(json.dumps({key:report[key] for key in ("status","reason","evidence_verified")}))
    return report["status"]!="pass"


if __name__=="__main__":sys.exit(main())
