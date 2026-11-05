#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Build an application-private SCM probe on Ubuntu; consume its exact bytes."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import re
import shutil
import subprocess
import sys
import uuid
import ape_helpers
from build_ape import publish_ape

from build_ape import (BuildError, headers, identity, link_portable, native_toolchain,
                       prepare_portable_helpers, run, verify_inputs)
from input_lock import component, consume_identity
from evidence import read_json
from foundation import ROOT, host_facts, now, sha256
from native_toolchain import recheck
from nightly_toolchain import write_json
from startup import ci_facts


def build(args, report, directory):
    if platform.system() != "Linux" or platform.freedesktop_os_release().get("ID") != "ubuntu":
        raise BuildError("SCM probe builds require Ubuntu", "blocked")
    original = verify_inputs(args, report, directory, source=False)
    tree = native_toolchain(args, report, directory, original)
    helpers = prepare_portable_helpers(args, report, directory, tree)
    output = args.output_dir.resolve()
    if output.exists() and any(output.iterdir()):
        raise BuildError("Output directory must be new or empty")
    output.mkdir(parents=True, exist_ok=True)
    work = (args.build_dir or ROOT / ".cache/build").resolve() / report["report_id"]
    (work / "tmp").mkdir(parents=True)
    sources = [ROOT / "apps/svc/probes" / name for name in ("scm_main.c", "scm_callbacks.c", "scm_abi.h")]
    for source in sources:
        shutil.copyfile(source, work / source.name)
    flags = ["-Os", "-fwrapv", "-fno-stack-protector", "-fno-instrument-functions", "-fno-profile-arcs",
             "-fno-test-coverage", "-fno-asynchronous-unwind-tables", "-Werror=incompatible-pointer-types",
             "-ffile-prefix-map=" + str(work) + "=svc-scm-build"]
    report["source"] = [identity(source) for source in sources]
    report["build"] = {"sources": report["source"], "flags": flags, "toolchain_lock": report["toolchain_lock"],
        "private_boundary": "apps/svc/probes; no Core/SDK recipe changes",
        "callback_policy": "Raw pre-resolved MS-ABI Win32 only; no JS, TLS, libc, pthreads or instrumentation",
        "recipe_files": [identity(ROOT / "scripts" / name) for name in
            ("svc_scm.py", "build_ape.py", "ape_helpers.py", "native_toolchain.py")]}
    env = dict(os.environ, TMPDIR=str(work / "tmp"), LC_ALL="C", TZ="UTC", SOURCE_DATE_EPOCH="0",
               BUILDLOG=str(output / "svc-scm.build-commands.log"))
    objects = []
    for source in sources[:2]:
        obj = work / (source.stem + ".o")
        run(report, directory, source.stem + "-compile", [tree / "bin/cosmocc", *flags, "-c", work / source.name, "-o", obj], args.compile_timeout, work, env)
        objects.append(obj)
    # The foreign-thread TU must not retain any undefined runtime/helper symbol.
    _, undefined, _ = run(report, directory, "callback-undefined", [tree / "bin/x86_64-linux-cosmo-nm", "-u", objects[1]], args.timeout, work, env)
    if undefined.strip():
        raise BuildError("Foreign-thread callback object has unexpected external helpers; inspect callback-undefined logs")
    _, assembly, _ = run(report, directory, "callback-assembly", [tree / "bin/x86_64-linux-cosmo-objdump", "-dr", objects[1]], args.timeout, work, env)
    if re.search(r"%(?:fs|gs):|__tls|__stack_chk|__atomic_", assembly):
        raise BuildError("Foreign-thread callback assembly contains TLS/instrumentation/atomic helpers")
    report["callback_audit"] = {"status": "pass", "undefined_symbols": [], "tls_segment_access": False,
        "abi": "Explicit __msabi on callback definitions and raw DLL function pointer types; x64 disassembly retained",
        "commands": report["commands"][-2:], "object": identity(objects[1])}
    destination = work / "svc-scm.com"
    link_portable(args, report, directory, tree, helpers, "svc-scm", objects, destination, work, env, libraries=())
    maps = publish_ape(work, output, "svc-scm", helpers, ape_helpers.source_cache(args.helper_source_cache.resolve()))
    artifact = {"name": "svc-scm", **identity(output / "svc-scm.com"),
        "headers": headers(output / "svc-scm.com", [output / "svc-scm.com.dbg", output / "svc-scm.aarch64.elf"]),
        "maps": maps, "build_commands": identity(output / "svc-scm.build-commands.log")}
    run(report, directory, "svc-scm-pe-imports", [tree / "bin/x86_64-linux-cosmo-objdump", "-p", output / "svc-scm.com"], args.timeout, work, env)
    # ARM slice has the portable entry only; no private Windows ABI code survives.
    _, arm_symbols, _ = run(report, directory, "svc-scm-arm-symbols", [tree / "bin/aarch64-linux-cosmo-nm", "-g", output / "svc-scm.aarch64.elf"], args.timeout, work, env)
    if re.search(r"appscm_|LoadLibrary|GetProcAddress", arm_symbols):
        raise BuildError("Windows private bridge leaked into the ARM slice")
    report["artifacts"] = [artifact]
    report["loaders"] = []
    for arch, source in (("x86_64", tree / "bin/ape-x86_64.elf"), ("aarch64", Path(helpers["loader"]["path"]))):
        target = output / ("ape-" + arch + ".elf")
        shutil.copyfile(source, target)
        target.chmod(0o755)
        report["loaders"].append({"os": "Linux", "arch": arch, **identity(target)})
    ordinary(args, report, directory, [output / "ape-x86_64.elf"], Path(artifact["path"]))
    verify_inputs(args, report, directory, source=False, stage="after")
    recheck(report["native_toolchain"], original, component("cosmocc"))
    report["build"]["sha256"] = hashlib.sha256(json.dumps(report["build"], sort_keys=True, separators=(",", ":")).encode()).hexdigest()


def native_case(args, report, directory, name, argv, expected, check):
    executed, output, errors = run(report, directory, name, argv, args.timeout, expected=None)
    actual = {"exit_code": executed["exit_code"], "timed_out": executed["timed_out"], "stdout": output, "stderr": errors}
    passed = not executed["timed_out"] and check(actual)
    report["cases"].append({"case_id": name, "status": "pass" if passed else "fail", "expected": expected,
        "actual": actual, "started_at": executed["started_at"], "finished_at": executed["finished_at"],
        "timeout_seconds": args.timeout, "commands": [executed], "reason": "Actual independent native probe output/status"})
    if not passed:
        raise BuildError("Native SCM assertion failed: " + name)
    return actual


def ordinary(args, report, directory, prefix, artifact):
    expected = {"probe": "svc-scm", "entry": "ordinary", "arch": report["host"]["execution_arch"],
                "page_size": report["host"]["page_size"], "windows": report["host"]["os"] == "Windows", "scm_calls": 0}
    native_case(args, report, directory, "scm.ordinary", [*prefix, artifact, "--ordinary"], expected,
        lambda actual: actual["exit_code"] == 0 and not actual["stderr"] and json.loads(actual["stdout"]) == expected)
    if report["host"]["os"] != "Windows":
        native_case(args, report, directory, "scm.nonwindows-rejected", [*prefix, artifact, "--resolve"],
            {"exit_code": "nonzero", "stderr_contains": "SCM is unavailable on this host"},
            lambda actual: actual["exit_code"] not in (None, 0) and "SCM is unavailable on this host" in actual["stderr"])


def consume(args, report, directory):
    producer = read_json(args.input_report)
    consume_identity(producer, report)
    if producer.get("schema_version") != 1 or producer.get("report_kind") != "svc-scm-build" or producer.get("status") != "pass":
        raise BuildError("A passing svc-scm-build v1 producer is required")
    entries = producer.get("artifacts", [])
    if len(entries) != 1 or entries[0].get("name") != "svc-scm":
        raise BuildError("Exactly one independent SCM artifact is required")
    artifact = identity(args.artifact.resolve())
    if any(artifact[key] != entries[0][key] for key in ("sha256", "size_bytes")):
        raise BuildError("Distributed SCM artifact SHA-256/size differs from producer")
    report.update(artifact=artifact, producer=identity(args.input_report), build=producer["build"], toolchain=producer["toolchain"])
    prefix = []
    if report["host"]["os"] != "Windows":
        if not args.loader:
            raise BuildError("Explicit distributed native loader required", "blocked")
        matches = [entry for entry in producer["loaders"] if entry["os"] == report["host"]["os"] and entry["arch"] == report["host"]["execution_arch"]]
        if len(matches) != 1:
            raise BuildError("No unique distributed loader for actual host", "blocked")
        report["loader"] = identity(args.loader)
        if any(report["loader"][key] != matches[0][key] for key in ("sha256", "size_bytes")):
            raise BuildError("Distributed native loader SHA-256/size differs from producer")
        args.loader.chmod(args.loader.stat().st_mode | 0o111)
        args.artifact.chmod(args.artifact.stat().st_mode | 0o111)
        prefix = [args.loader.resolve()]
    ordinary(args, report, directory, prefix, args.artifact.resolve())
    if report["host"]["os"] == "Windows":
        if not args.ordinary_only:
            windows(args, report, directory)
    if sha256(args.artifact) != artifact["sha256"]:
        raise BuildError("SCM artifact changed during validation")


def windows(args, report, directory):
    artifact = args.artifact.resolve()
    native_case(args, report, directory, "scm.resolve", [artifact, "--resolve"],
        {"exports": 8, "service_entry_size": 16, "status_size": 28, "status_process_size": 36, "abi": "ms-x64"},
        lambda actual: actual["exit_code"] == 0 and not actual["stderr"] and json.loads(actual["stdout"]) == {
            "probe": "svc-scm", "entry": "resolve", "status": "pass", "exports": 8,
            "service_entry_size": 16, "status_size": 28, "status_process_size": 36, "abi": "ms-x64"})
    native_case(args, report, directory, "scm.missing-export-rejected", [artifact, "--resolve-missing"],
        {"exit_code": "nonzero", "status": "unavailable", "winerror": 127},
        lambda actual: actual["exit_code"] not in (None, 0) and not actual["stderr"] and json.loads(actual["stdout"]) == {
            "probe": "svc-scm", "entry": "resolve", "status": "unavailable",
            "export": "QapletDeliberatelyMissingScmExport", "winerror": 127})
    if args.plan:
        plan_path = args.plan.resolve()
        plan = read_json(plan_path)
        if plan.get("artifact") != identity(artifact):
            raise BuildError("Reviewed SCM fixture plan differs from the distributed artifact")
        if not re.fullmatch(r"qaplet-m0-scm-[0-9a-f]{32}", plan.get("name", "")):
            raise BuildError("Reviewed SCM fixture plan needs one UUID service name")
        name, callback_log = plan["name"], Path(plan["callback_log"])
        if callback_log.exists():
            raise BuildError("Reviewed SCM callback evidence path must be new")
    else:
        name = "qaplet-m0-scm-" + uuid.uuid4().hex
        callback_log = directory / "service-callbacks.json"
        plan = {"schema_version": 1, "name": name, "artifact": identity(artifact), "callback_log": str(callback_log),
                "host_scope": "Current Windows host; CI runner identity is recorded separately; local run is not an isolated VM",
                "service_type": "own-process", "start_type": "demand", "service_watchdog_seconds": 20,
                "operations": ["connect", "permission check", "create", "start", "query RUNNING", "stop",
                               "query STOPPED", "repeat stop (1062)", "delete", "close handles", "post-delete query (1060)"]}
        plan["binary"] = subprocess.list2cmdline([str(artifact), "--service", name, str(callback_log)])
        plan_path = directory / "fixture-plan.json"
        write_json(plan_path, plan)
    worker_path = directory / "scm-worker.json"
    report["fixture_plan"] = {**identity(plan_path), "name": name, "binary": plan["binary"]}
    worker = ROOT / "scripts/svc_scm_windows.py"
    report["runner"] = [identity(ROOT / "scripts/svc_scm.py"), identity(worker)]
    executed, _, _ = run(report, directory, "scm-worker", [sys.executable, worker, "run", "--plan", plan_path,
                          "--report", worker_path], 90, expected=None)
    if worker_path.exists():
        report["scm"] = read_json(worker_path)
        report["worker_report"] = identity(worker_path)
    # Parent owns a second cleanup route if the worker crashes, hangs or leaves
    # an uncertain fixture. Only the exact plan command line can be cleaned.
    if executed["timed_out"] or not worker_path.exists() or not report["scm"].get("cleanup", {}).get("service_removed"):
        recovery = directory / "scm-recovery.json"
        run(report, directory, "scm-recovery", [sys.executable, worker, "cleanup", "--plan", plan_path,
            "--report", recovery], 60, expected=None)
        if recovery.exists():
            report["recovery"] = read_json(recovery)
            report["recovery_report"] = identity(recovery)
    events = "$ErrorActionPreference='Continue'; Get-WinEvent -FilterHashtable @{LogName='System'; ProviderName='Service Control Manager'; StartTime=(Get-Date).AddMinutes(-5)} -MaxEvents 100 | Where-Object { $_.Message -like '*" + name + "*' } | Select-Object TimeCreated,Id,LevelDisplayName,Message | ConvertTo-Json -Depth 3"
    run(report, directory, "scm-system-events", ["powershell.exe", "-NoProfile", "-NonInteractive", "-Command", events], 15, expected=None)
    if executed["timed_out"] or not worker_path.exists():
        report["capability"] = "failed"
        raise BuildError("SCM worker timed out or did not preserve its result; inspect recovery evidence")
    actual = report["scm"]
    report["cleanup"] = actual["cleanup"]
    report["cases"].append({"case_id": "scm.lifecycle", "status": actual["status"],
        "started_at": actual["started_at"], "finished_at": actual["finished_at"], "timeout_seconds": 90,
        "expected": plan["operations"], "actual": actual, "commands": [executed], "reason": actual["reason"]})
    if actual["status"] != "pass":
        report["capability"] = "blocked" if actual["status"] == "blocked" else "failed"
        raise BuildError(actual["reason"], actual["status"])
    if executed["exit_code"] != 0:
        report["capability"] = "failed"
        raise BuildError("SCM worker process status disagrees with its result")
    if not callback_log.is_file():
        report["capability"] = "failed"
        raise BuildError("No service callback evidence after process exit")
    callbacks = read_json(callback_log)
    report["callback_result"] = {"identity": identity(callback_log), "actual": callbacks}
    expected = {"probe": "svc-scm", "entry": "service", "dispatcher": 1, "dispatch_error": 0,
                "main_calls": 1, "stop_calls": 1, "status_mask": 30, "first_error": 0,
                "wait_result": 0, "event_closes": 1, "callback_js_calls": 0}
    if any(callbacks.get(key) != value for key, value in expected.items()) or callbacks.get("handler_calls", 0) < 1:
        report["capability"] = "failed"
        raise BuildError("Foreign-thread callback ABI/status/cleanup assertions failed")
    report["capability"] = "verified"


def cleanup(args, report, directory):
    if report["host"]["os"] != "Windows":
        raise BuildError("SCM cleanup requires Windows", "blocked")
    worker_report = directory / "cleanup-worker.json"
    executed, _, _ = run(report, directory, "scm-final-cleanup", [sys.executable, ROOT / "scripts/svc_scm_windows.py",
        "cleanup", "--plan", args.plan.resolve(), "--report", worker_report], 60, expected=None)
    if not worker_report.is_file() or executed["timed_out"]:
        raise BuildError("SCM final cleanup timed out or returned no evidence")
    actual = read_json(worker_report)
    report["cleanup"] = actual
    if executed["exit_code"] != 0 or actual["status"] != "pass":
        raise BuildError(actual["reason"], actual["status"] if actual["status"] != "pass" else "fail")


def check_ci(args, report):
    workflow = read_json(args.workflow)
    from ci_tiers import check_scoped_job
    check_scoped_job(workflow, "build", "svc_scm")
    jobs = workflow["jobs"]
    if set(jobs) != {"build", "windows-scm"} or not jobs["build"]["runs-on"].startswith("ubuntu-"):
        raise BuildError("SCM workflow must isolate an Ubuntu producer and Windows consumer")
    consumer = jobs["windows-scm"]
    if consumer["needs"] != "build" or not consumer["runs-on"].startswith("windows-"):
        raise BuildError("SCM Windows consumer must depend on the Ubuntu producer")
    producer_text = "\n".join(step.get("run", "") for step in jobs["build"]["steps"])
    for required in ("test_svc_scm.py", "scripts/fetch_locked_toolchain.py", "scripts/nightly_toolchain.py fetch",
                     "scripts/ape_helpers.py fetch", "scripts/svc_scm.py build"):
        if required not in producer_text:
            raise BuildError("SCM producer is missing a shared locked-input/build step: " + required)
    steps = consumer["steps"]
    for step in steps:
        if re.search(r"cosmocc|make\s+build-ape|svc_scm\.py\s+build|build_ape\.py\s+build", step.get("run", "")):
            raise BuildError("SCM Windows consumer must not compile")
    transfers = [step for step in steps if "download-artifact@" in step.get("uses", "")]
    if len(transfers) != 1 or transfers[0]["with"]["name"] != "svc-scm-build-${{ github.sha }}":
        raise BuildError("SCM consumer must download the same-commit independent producer")
    if not any("svc_scm.py run --input-report" in step.get("run", "") and "--artifact" in step["run"] for step in steps):
        raise BuildError("SCM consumer must verify the producer and distributed artifact")
    if not any("svc_scm.py cleanup --plan" in step.get("run", "") and step.get("if") == "${{ always() }}" for step in steps):
        raise BuildError("SCM Windows consumer needs unconditional independent cleanup")
    for job in jobs.values():
        if not any("upload-artifact@" in step.get("uses", "") and step.get("if") == "${{ always() }}" for step in job["steps"]):
            raise BuildError("SCM CI must retain evidence on failure")
    report["workflow"] = identity(args.workflow)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("operation", choices=("build", "run", "cleanup", "check-ci"))
    parser.add_argument("--toolchain-cache", type=Path, default=ROOT / ".cache/toolchains/cosmocc")
    parser.add_argument("--helper-source-cache", type=Path, default=ROOT / ".cache/sources/ape-loader")
    parser.add_argument("--build-dir", type=Path)
    parser.add_argument("--output-dir", type=Path, default=ROOT / "out/svc-scm/artifacts")
    parser.add_argument("--report", type=Path, required=True)
    parser.add_argument("--input-report", type=Path)
    parser.add_argument("--artifact", type=Path)
    parser.add_argument("--loader", type=Path)
    parser.add_argument("--ordinary-only", action="store_true")
    parser.add_argument("--plan", type=Path)
    parser.add_argument("--workflow", type=Path, default=ROOT / ".github/workflows/svc-scm.yml")
    parser.add_argument("--timeout", type=int, default=30)
    parser.add_argument("--compile-timeout", type=int, default=300)
    args = parser.parse_args()
    if args.operation == "run" and (not args.input_report or not args.artifact):
        parser.error("run requires --input-report and --artifact")
    if args.operation == "cleanup" and not args.plan:
        parser.error("cleanup requires --plan")
    if not 1 <= args.timeout <= 300 or not 1 <= args.compile_timeout <= 900:
        parser.error("timeout must be 1..300 and compile-timeout 1..900")
    directory = args.report.resolve().parent / "evidence" / str(uuid.uuid4())
    directory.mkdir(parents=True)
    report = {"schema_version": 1, "report_kind": "svc-scm-" + args.operation,
              "report_id": directory.name, "started_at": now(), "status": "not_run", "capability": "not_run",
              "commands": [], "cases": [], "artifacts": [], "host": host_facts(), "ci": ci_facts(),
              "input_reports": {}, "source": None, "build": None, "toolchain": None}
    try:
        if args.operation == "build":
            build(args, report, directory)
        elif args.operation == "run":
            consume(args, report, directory)
        elif args.operation == "cleanup":
            cleanup(args, report, directory)
        else:
            check_ci(args, report)
        report.update(status="pass", reason="SCM probe " + args.operation + " completed")
    except BuildError as error:
        report.update(status=error.status, reason=str(error))
    except (OSError, ValueError, KeyError, TypeError) as error:
        report.update(status="fail", reason=str(error))
    report["finished_at"] = now()
    write_json(args.report, report)
    print(json.dumps({"status": report["status"], "capability": report["capability"],
                      "reason": report["reason"], "report": str(args.report)}))
    return {"pass": 0, "fail": 1, "blocked": 2}[report["status"]]


if __name__ == "__main__":
    sys.exit(main())
