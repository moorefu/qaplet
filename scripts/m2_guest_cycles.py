#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Verify actual M2 guest cycles and exact public compiler-input bindings."""
import argparse
import json
from pathlib import Path
import sys
import uuid

from build_ape import BuildError, identity
from core_gate import Evidence, interval, require, same, unique, verify_guest_transport
from foundation import ROOT, now
from input_lock import consume_identity
from startup import check_ci_commit
from workflow_support import guard_evidence_outputs, normalized_evidence_path, write_evidence_report
import sdk
import sdk_m2_cycles
from sdk_m2_guest import inspect_capture
from m2_fixture_binding import verify_link_inputs


def evaluate(args, report):
    evidence = Evidence({}, ROOT)
    try:
        product = evidence.json(str(args.sdk_build.resolve()))
        require(product["report_kind"] == "qaplet-sdk" and product["scope"] == "build" and product["status"] == "pass", "Successful target SDK producer required")
        consume_identity(product, report)
        supplied = args.distribution.resolve()
        sdk.check_package(product, supplied)
        producer = evidence.json(str(args.producer.resolve()))
        require(producer["report_kind"] == "qaplet-m2-cycles" and producer["scope"] == "build" and producer["status"] == "pass", "Successful independent cycle producer required")
        consume_identity(producer, report)
        members = unique(product["package"]["members"], "file")
        used = {"include/qaplet.h", "application-m2.c", "lib/libqaplet.a", "lib/.aarch64/libqaplet.a"}
        actual = {}
        for row in producer["compiler_inputs"]:
            require(same(row["original"], row["retained"]), "Retained cycle input differs from its original compiler bytes")
            evidence.read(row["retained"])
            original = normalized_evidence_path(row["original"]["path"])
            require("/sdk/" in original, "Unexpected external cycle compiler-input path")
            name = original.rsplit("/sdk/", 1)[1]
            require(name not in actual, "Duplicate cycle compiler input")
            actual[name] = row
        require(used <= set(actual) and "cycles.c" in actual, "Complete actual public compiler-input closure required")
        binding = []
        for name in sorted(used):
            require(same(actual[name]["retained"], members["sdk/" + name]), "Cycle C/header/target archive differs from selected target SDK: " + name)
            evidence.read({**members["sdk/" + name], "path": str(supplied / "sdk" / name)})
            binding.append({"file": "sdk/" + name, "actual_compiler_input": actual[name]["retained"], "target_package_member": members["sdk/" + name]})
        require(same(actual["cycles.c"]["retained"], identity(ROOT / "tests/native/sdk_m2_cycles.c")), "Cycle fixture differs from shared fixed source")
        for row in producer["evaluator_inputs"]:
            evidence.read(row["retained"])
        for row in producer["packing"]:
            evidence.read(row["original_packer_output"])
        for command in producer["commands"]:
            evidence.command(command, 0)
        artifact, = producer["artifacts"]
        compiler=producer["native_toolchain"]["tree"]+"/bin/cosmocc"
        compiles=[row for row in producer["commands"] if row["argv"][0]==compiler and "-c" in row["argv"]]
        require(len(compiles)==1,"One exact cycle C compiler command required")
        compile=compiles[0];argv=compile["argv"];source=actual["cycles.c"]["original"]["path"]
        sdk_root=str(Path(source).parent);external=producer["external_directory"]
        obj=argv[argv.index("-o")+1]
        require(argv==[compiler,"-Os","-D_GNU_SOURCE","-I"+sdk_root+"/include","-I"+sdk_root,
                       "-ffile-prefix-map="+external+"=external","-c",source,"-o",obj],"Cycle compiler inputs/flags differ")
        links=[row for row in producer["commands"] if row["argv"][0]==compiler and "-o" in row["argv"] and
               row["argv"][row["argv"].index("-o")+1].endswith("/qaplet-m2-cycles.com")]
        require(len(links)==1,"One exact cycle archive/object link required")
        require(actual["lib/libqaplet.a"]["original"]["path"]==sdk_root+"/lib/libqaplet.a" and
                actual["lib/.aarch64/libqaplet.a"]["original"]["path"]==sdk_root+"/lib/.aarch64/libqaplet.a",
                "Actual cycle dual archive protocol differs")
        verify_link_inputs(links[0]["argv"],compiler,[obj],sdk_root+"/lib/libqaplet.a",())
        report["actual_compiler_link_binding"]={"compile":compile,"link":links[0],"implicit_arm64_archive":actual["lib/.aarch64/libqaplet.a"]}
        evidence.read(artifact)
        for row in artifact["headers"]["sidecars"] + artifact["audit"]["maps"]:
            evidence.read(row)
        require(artifact["audit"]["status"] == "pass", "Audited fat cycle APE required")
        guest = evidence.report(str(args.guest_report.resolve()), "arm64-guest")
        check_ci_commit(producer, guest)
        files, cases = verify_guest_transport(evidence, guest, report)
        require(guest["guest"]["page_size"] == 65536, "Actual 65536 kernel pages required for cycles")
        require(same(files["/payload/qaplet-m2-cycles.com"], artifact) and
                same(files["/payload/ape-aarch64.elf"], producer["portable_helpers"]["loader"]) and
                same(files["/payload/ape-aarch64.elf"], product["portable_helpers"]["loader"]), "Cycle APE or ARM loader differs from explicit supplied bytes")
        require(evidence.read(files["/payload/input.bin"]) == bytes.fromhex("4900ff4e0a"), "Mixed actual binary input differs")
        selected = {}
        required = {"m2-cycles.lifecycle1000": ("lifecycle", 1000), "m2-cycles.mixed100": ("mixed", 100)}
        require({name for name in cases if name.startswith("m2-cycles.")} == set(required), "Mandatory exact cycle catalog incomplete or unknown")
        for name, (mode, requested) in required.items():
            case = cases[name]
            expected = ["/payload/m2-guest-capture.sh", "/payload/ape-aarch64.elf", "/payload/qaplet-m2-cycles.com", "--" + mode, str(requested)]
            if mode == "mixed":
                expected += ["/payload/qaplet-m2-cycles.com", "/payload/ape-aarch64.elf", "/payload/input.bin"]
            require(case["argv"] == expected and case["cwd"] == "/", "Cycle invocation differs from exact requested count/source")
            start, end = interval(case)
            require((end-start).total_seconds() <= case["timeout_seconds"] and not evidence.read(case["stderr_log"]), "Cycle deadline/capture failed")
            capture = inspect_capture(evidence.read(case["stdout_log"]))
            require(capture["cwd"] == "/tmp/command-" + name and capture["exit_code"] == 0 and not capture["stderr"], "Cycle did not complete in its owned cwd")
            require(set(capture["sidecars"]) == {"cycles-progress.ndjson"}, "Missing or unknown actual cycle progress sidecar")
            progress = capture["sidecars"]["cycles-progress.ndjson"]
            require(progress == capture["stdout"], "Authoritative flushed progress differs from returned original stdout")
            selected[mode] = sdk_m2_cycles.inspect(progress, capture["stderr"], capture["exit_code"], mode, requested)
        report.update(status="pass", reason="Actual 1000 M2 lifecycles and 100 mixed cycles verified on ARM64/65536",
                      evidence_verified=True, cycles=selected, sdk_sha256=product["sdk"]["sha256"],
                      source_sdk_sha256=producer["sdk"]["sha256"], public_input_binding=binding,
                      producer=identity(args.producer), sdk_producer=identity(args.sdk_build), guest_report=identity(args.guest_report),
                      guest=guest["guest"], execution_mode="emulated-system",
                      scope_notes="Original cycle producer identity is preserved; exact used source/header/dual archives bind target SDK. No native target or TCG physical performance claim")
    finally:
        report["evidence_index"] = list(evidence.index.values())


def main():
    p=argparse.ArgumentParser(description=__doc__)
    for name in ("producer", "sdk-build", "distribution", "guest-report", "report"):
        p.add_argument("--"+name,type=Path,required=True)
    args=p.parse_args()
    guard_evidence_outputs([args.report],[args.producer,args.sdk_build,args.guest_report],[args.distribution])
    require(not args.report.exists(), "Existing evidence output is immutable; use a new path")
    report={"schema_version":1,"report_kind":"m2-guest-cycles","report_id":str(uuid.uuid4()),"started_at":now(),
            "status":"not_run","evidence_verified":False,"release_ready":False,"evaluator":identity(Path(__file__))}
    try:
        evaluate(args,report)
    except BuildError as error:
        report.update(status=error.status,reason=str(error))
    except (OSError,ValueError,TypeError,KeyError) as error:
        report.update(status="fail",reason=str(error))
    report["finished_at"]=now();write_evidence_report(args.report,report)
    print(json.dumps({key:report[key] for key in ("status","reason","evidence_verified")}))
    return report["status"]!="pass"


if __name__=="__main__":
    sys.exit(main())
