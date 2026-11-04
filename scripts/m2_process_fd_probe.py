#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Narrow Ubuntu-built Darwin FD-enumeration experiment; not an SDK gate."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import shutil
import subprocess
import sys
import uuid

import ape_helpers
from build_ape import BuildError, audit, identity, link_portable, native_toolchain, prepare_portable_helpers, publish_ape, run, verify_inputs
from foundation import ROOT, host_facts, now
from input_lock import component
from native_toolchain import recheck
from nightly_toolchain import write_json
from startup import check_ci_commit, ci_facts, measured_host, select_loader

SOURCE = ROOT / "tests/native/m2_process_fd_probe.c"


def source_inputs():
    paths = [SOURCE, Path(__file__), ROOT / ".github/workflows/m2-process-fd-probe.yml"]
    result = []
    for path in paths:
        relative = path.relative_to(ROOT).as_posix()
        blob = subprocess.run(["git", "show", "HEAD:" + relative], cwd=ROOT, capture_output=True, check=True).stdout
        raw = path.read_bytes()
        if raw != blob or b"\r" in raw:
            raise BuildError("Experiment source must equal its Git blob with LF bytes: " + relative)
        result.append({"file": relative, **identity(path), "git_blob_sha256": hashlib.sha256(blob).hexdigest(), "raw_equals_git_blob": True, "lf_only": True})
    return result


def build(args, report, evidence):
    if report["host"]["os"] != "Linux" or platform.freedesktop_os_release().get("ID") != "ubuntu":
        raise BuildError("Probe builds require Ubuntu")
    original = verify_inputs(args, report, evidence, source=False)
    tree = native_toolchain(args, report, evidence, original)
    helpers = prepare_portable_helpers(args, report, evidence, tree)
    output = args.output_dir.resolve()
    if output.exists() and any(output.iterdir()):
        raise BuildError("Probe output directory must be new")
    output.mkdir(parents=True)
    work = args.build_dir.resolve() / report["report_id"]
    work.mkdir(parents=True)
    (work / "tmp").mkdir()
    environment = dict(os.environ, TMPDIR=str(work / "tmp"), BUILDLOG=str(output / "probe.build-commands.log"), SOURCE_DATE_EPOCH="0", LC_ALL="C", TZ="UTC")
    run(report, evidence, "probe-compile", [tree / "bin/cosmocc", "-Os", "-D_GNU_SOURCE", "-c", SOURCE, "-o", work / "probe.o"], 300, work, environment)
    link_portable(args, report, evidence, tree, helpers, "fd-probe", [work / "probe.o"], work / "fd-probe.com", work, environment)
    publish_ape(work, output, "fd-probe", helpers, ape_helpers.source_cache(args.helper_source_cache.resolve()))
    report["artifacts"].append(audit(args, report, evidence, tree, "fd-probe", output))
    report["official_loaders"] = []
    for name in ("ape-x86_64.macho", "ape-aarch64.macho"):
        entry = next(item for item in component("cosmocc")["files"] if item["path"] == "bin/" + name)
        source = original / "bin" / name
        if identity(source)["sha256"] != entry["sha256"] or source.stat().st_size != entry["size_bytes"]:
            raise BuildError("Official loader differs from pinned archive")
        shutil.copyfile(source, output / name)
        report["official_loaders"].append({**identity(output / name), "locked_path": entry["path"], "origin": "unchanged official Nightly loader"})
    verify_inputs(args, report, evidence, source=False, stage="after")
    recheck(report["native_toolchain"], original, component("cosmocc"))
    report["cleanup"] = {"no_native_probe_executed_on_Ubuntu": True, "build_cache_retained": True}
    report["status"] = "pass"


def consume(args, report, evidence):
    producer = json.loads(args.build_report.read_text(encoding="utf-8"))
    if producer["status"] != "pass" or producer["scope"] != "build":
        raise BuildError("Successful exact producer required")
    check_ci_commit(producer, report)
    report["input_lock"] = producer["input_lock"]
    if [{k: v for k, v in item.items() if k != "path"} for item in producer["source_inputs"]] != [{k: v for k, v in item.items() if k != "path"} for item in report["source_inputs"]]:
        raise BuildError("Consumer Git/source bytes differ from producer")
    report["producer_report"] = identity(args.build_report)
    artifact = args.artifact.resolve()
    supplied = next(item for item in producer["artifacts"] if Path(item["path"]).name == artifact.name)
    before = identity(artifact)
    if before["sha256"] != supplied["sha256"] or before["size_bytes"] != supplied["size_bytes"]:
        raise BuildError("Supplied APE differs from producer")
    measured_host(args, report, evidence)
    if report["host"]["os"] != "Darwin" or report["host"]["execution_mode"] != "native":
        raise BuildError("This experiment requires native Darwin")
    loader = select_loader(args, report)
    artifact.chmod(artifact.stat().st_mode | 0o111)
    record, stdout, stderr = run(report, evidence, "Darwin-actual-FD-table", [*loader, artifact], 30, expected=None)
    facts = json.loads(stdout) if stdout.strip().startswith("{") else None
    passed = record["exit_code"] == 0 and not record["timed_out"] and not stderr and facts is not None
    if facts is not None:
        passed = passed and facts["sweep_error"] == 0 and facts["high_fd"] >= 8192 and facts["highest"] >= facts["high_fd"] and facts["pages"] > 1 and facts["high_closed"] == facts["low_closed"] == facts["parent_unchanged"] == 1 and facts["wait_status"] == 0
        passed = passed and facts["arch"] == report["host"]["execution_arch"] and facts["page_size"] == report["host"]["page_size"]
    report["cases"] = [{"case_id": "Darwin-inherited-FD-table", "status": "pass" if passed else "fail", "command": record, "stdout": stdout, "stderr": stderr, "facts": facts}]
    report["artifact_before"] = before
    report["artifact_after"] = identity(artifact)
    report["loader_after"] = identity(Path(loader[0]))
    if before != report["artifact_after"] or report["loader_after"]["sha256"] != report["loader"]["sha256"]:
        raise BuildError("Executable/loader bytes changed")
    report["cleanup"] = {"child_reaped": bool(facts and facts["wait_status"] == 0), "parent_FDs_and_limit_restored": bool(facts and facts["parent_unchanged"] == 1), "native_cleanup_completed": passed}
    report["status"] = "pass" if passed else "fail"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("operation", choices=("build", "run"))
    parser.add_argument("--report", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, default=ROOT / "out/fd-probe/artifacts")
    parser.add_argument("--build-report", type=Path)
    parser.add_argument("--artifact", type=Path)
    parser.add_argument("--loader-dir", type=Path)
    parser.add_argument("--loader-manifest", type=Path)
    parser.add_argument("--expected-os", default="Darwin")
    parser.add_argument("--expected-arch")
    parser.add_argument("--runner-label", default="local")
    parser.add_argument("--toolchain-cache", type=Path, default=ROOT / ".cache/toolchains/cosmocc")
    parser.add_argument("--helper-source-cache", type=Path, default=ROOT / ".cache/sources/ape-loader")
    parser.add_argument("--build-dir", type=Path, default=ROOT / ".cache/build/fd-probe")
    parser.add_argument("--compile-timeout", type=int, default=300)
    parser.add_argument("--timeout", type=int, default=30)
    args = parser.parse_args()
    evidence = args.report.resolve().parent / "evidence" / str(uuid.uuid4())
    evidence.mkdir(parents=True)
    report = {"schema_version": 1, "report_kind": "Darwin-FD-ABI-experiment", "report_id": evidence.name, "scope": args.operation, "status": "not_run", "started_at": now(), "host": host_facts(), "ci": ci_facts(), "commands": [], "artifacts": [], "input_reports": {}, "boundary": "OS ABI experiment only; no SDK or ticket13 acceptance"}
    try:
        report["source_inputs"] = source_inputs()
        tracked = subprocess.run(["git", "ls-files", "scripts/*.py"], cwd=ROOT, capture_output=True, check=True).stdout.decode("utf-8").splitlines()
        report["static_python_inputs_before"] = [{"file": name, **identity(ROOT / name)} for name in tracked]
        {"build": build, "run": consume}[args.operation](args, report, evidence)
        if report["source_inputs"] != source_inputs():
            raise BuildError("Source changed during execution")
        report["static_python_inputs_after"] = [{"file": name, **identity(ROOT / name)} for name in tracked]
        if report["static_python_inputs_before"] != report["static_python_inputs_after"]:
            raise BuildError("Tracked Python inputs changed during execution")
    except Exception as error:
        report.update(status="fail", reason=str(error))
    report["finished_at"] = now()
    write_json(args.report, report)
    print(json.dumps({"status": report["status"], "report": str(args.report), "reason": report.get("reason")}))
    return report["status"] != "pass"


if __name__ == "__main__":
    sys.exit(main())
