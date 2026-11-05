#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Ubuntu producer and same-byte Windows consumer for the private Job probe."""
import argparse
import json
import hashlib
import os
from pathlib import Path, PureWindowsPath
import platform
import re
import shutil
import sys
import uuid
import ape_helpers
from build_ape import publish_ape

from build_ape import BuildError
from build_ape import identity
from build_ape import (headers, link_portable, native_toolchain, prepare_portable_helpers, run, verify_inputs)
from input_lock import component, consume_identity
from evidence import read_json
from foundation import ROOT, host_facts, now, sha256
from native_toolchain import recheck
from nightly_toolchain import write_json
from startup import ci_facts

REQUIRED = {"job.context", "job.resolve", "job.assignment-denied", "job.tree-terminate", "job.nested-close",
            "job.early-exit", "job.timeout-cleanup", "job.handles"}
EXPECTED = {
    "job.handles": {"owned_handle_closes": "all", "repeat_baseline": "three equal post-cleanup counts, unchanged by full suite",
                    "initial_growth": "retained as an unattributed observation, not a zero-leak claim"},
    "job.resolve": {"exports": 20, "abi": "ms-x64", "basic_size": 64, "extended_size": 144, "accounting_size": 48},
    "job.missing-export": {"resolved": 0, "winerror": 127},
    "job.assignment-denied": {"assigned": 0, "winerror": 5, "before_member": 0, "after_member": 0, "resumed": False, "cleanup": True},
    "job.tree-terminate": {"assigned": 1, "verified_before_resume": 1, "resumed": 1, "listed": 2, "child_member": 1,
                           "terminated": 1, "parent_exit": 71, "child_exit": 71, "total_processes": 2, "active_processes": 0, "cleanup": True},
    "job.nested-close": {"outer_assigned": 1, "nested_assigned": 1, "both_memberships": 1, "close": 1,
                         "repeat_close": 0, "repeat_winerror": 6, "stopped": 1, "active_processes": 0, "cleanup": True},
    "job.early-exit": {"assigned": 1, "verified_before_resume": 1, "resumed": 1, "wait_result": 0, "exit_code": 37, "stopped": 1, "cleanup": True},
    "job.timeout-cleanup": {"assigned": 1, "verified_before_resume": 1, "resumed": 1, "wait_result": 258, "deadline_ms": 150, "stopped": 1, "cleanup": True},
}


def build(args, report, directory):
    if platform.system() != "Linux" or platform.freedesktop_os_release().get("ID") != "ubuntu":
        raise BuildError("Job probe builds require Ubuntu", "blocked")
    original = verify_inputs(args, report, directory, source=False)
    tree = native_toolchain(args, report, directory, original)
    helpers = prepare_portable_helpers(args, report, directory, tree)
    output = args.output_dir.resolve()
    if output.exists() and any(output.iterdir()):
        raise BuildError("Output directory must be new or empty")
    work = args.build_dir.resolve() / report["report_id"]
    if any(" " in str(path) for path in (tree, work)):
        raise BuildError("Locked cosmocc requires compilation paths without spaces")
    (work / "tmp").mkdir(parents=True)
    output.mkdir(parents=True, exist_ok=True)
    source = ROOT / "apps/svc/probes/job_probe.c"
    shutil.copyfile(source, work / source.name)
    files = [identity(source), *(identity(ROOT / "scripts" / name) for name in
             ("svc_job.py", "svc_job_windows.py", "build_ape.py", "ape_helpers.py", "native_toolchain.py", "foundation.py", "nightly_toolchain.py"))]
    report["source"] = {"files": files, "sha256": hashlib.sha256(json.dumps(files, sort_keys=True).encode()).hexdigest(),
                        "git_commit": None, "note": "Measured input bytes; a revision does not assert a clean worktree"}
    _, revision, _ = run(report, directory, "source-revision", ["git", "rev-parse", "HEAD"], 10, expected=None)
    # Git worktrees created by Windows retain a Windows-absolute gitdir file;
    # query the same metadata read-only through its actual WSL mount path.
    marker = ROOT / ".git"
    if not re.fullmatch(r"[0-9a-f]{40}", revision.strip()) and marker.is_file() and platform.system() == "Linux":
        value = marker.read_text().strip().removeprefix("gitdir: ")
        windows = PureWindowsPath(value)
        candidate = Path("/mnt") / windows.drive[0].lower() / Path(*windows.parts[1:]) if windows.drive else Path(value)
        if candidate.is_dir():
            _, revision, _ = run(report, directory, "source-revision-worktree", ["git", "--git-dir", candidate, "rev-parse", "HEAD"], 10, expected=None)
    if re.fullmatch(r"[0-9a-f]{40}", revision.strip()): report["source"]["git_commit"] = revision.strip()
    flags = ["-Os", "-fwrapv", "-Werror=incompatible-pointer-types", "-ffile-prefix-map=" + str(work) + "=svc-job-build"]
    report["build"] = {"flags": flags, "source": report["source"], "toolchain_lock": report["toolchain_lock"],
        "private_boundary": "Application-private apps/svc/probes only; no Core or Embed SDK linkage",
        "abi": "Explicit raw MS-ABI pointers on the ordinary Cosmopolitan main thread; no foreign-thread callbacks",
        "portable_helpers_sha256": helpers["sha256"], "libc_source": report["toolchain"]["libc_source"]}
    env = dict(os.environ, TMPDIR=str(work / "tmp"), LC_ALL="C", TZ="UTC", SOURCE_DATE_EPOCH="0",
               BUILDLOG=str(output / "svc-job.build-commands.log"))
    obj = work / "job_probe.o"
    run(report, directory, "job-compile", [tree / "bin/cosmocc", *flags, "-c", work / source.name, "-o", obj], args.compile_timeout, work, env)
    run(report, directory, "job-msabi-assembly", [tree / "bin/x86_64-linux-cosmo-objdump", "-dr", obj], args.timeout, work, env)
    link_portable(args, report, directory, tree, helpers, "svc-job", [obj], work / "svc-job.com", work, env, libraries=())
    maps = publish_ape(work, output, "svc-job", helpers, ape_helpers.source_cache(args.helper_source_cache.resolve()))
    artifact = {"name": "svc-job", **identity(output / "svc-job.com"), "maps": maps,
        "headers": headers(output / "svc-job.com", [output / "svc-job.com.dbg", output / "svc-job.aarch64.elf"]),
        "build_commands": identity(output / "svc-job.build-commands.log")}
    _, imports, _ = run(report, directory, "job-pe-imports", [tree / "bin/x86_64-linux-cosmo-objdump", "-p", output / "svc-job.com"], args.timeout, work, env)
    if re.search(r"\b(?:CreateJobObjectW|AssignProcessToJobObject|TerminateJobObject|SetInformationJobObject|IsProcessInJob)\b", imports):
        raise BuildError("Private Job calls unexpectedly became static PE imports")
    _, symbols, _ = run(report, directory, "job-arm-symbols", [tree / "bin/aarch64-linux-cosmo-nm", "-g", output / "svc-job.aarch64.elf"], args.timeout, work, env)
    if re.search(r"LoadLibrary|GetProcAddress|CreateJobObject|AssignProcessToJobObject", symbols):
        raise BuildError("Windows private bridge leaked into ARM slice")
    artifact["audit"] = {"status": "pass", "dynamic_private_job_bridge": True, "windows_code_in_arm": False,
                         "note": "This application is separate from Core; Core audit remains its own gate"}
    report["artifacts"], report["loaders"] = [artifact], []
    for arch, loader in (("x86_64", tree / "bin/ape-x86_64.elf"), ("aarch64", Path(helpers["loader"]["path"]))):
        target = output / ("ape-" + arch + ".elf")
        shutil.copyfile(loader, target); target.chmod(0o755)
        report["loaders"].append({"os": "Linux", "arch": arch, **identity(target)})
    ordinary(args, report, directory, [output / "ape-x86_64.elf"], output / "svc-job.com")
    verify_inputs(args, report, directory, source=False, stage="after")
    recheck(report["native_toolchain"], original, component("cosmocc"))
    report["build"]["sha256"] = hashlib.sha256(json.dumps(report["build"], sort_keys=True).encode()).hexdigest()


def add_case(report, name, passed, expected, actual, executed, timeout):
    case = {"case_id": name, "status": "pass" if passed else "fail", "expected": expected, "actual": actual,
            "started_at": executed["started_at"], "finished_at": executed["finished_at"],
            "timeout_seconds": timeout, "exit_code": executed["exit_code"], "commands": [executed],
            "reason": "Actual native output independently checked against the public probe contract"}
    report["cases"].append(case)
    return passed


def ordinary(args, report, directory, prefix, artifact):
    executed, output, errors = run(report, directory, "ordinary", [*prefix, artifact, "--ordinary"], args.timeout, expected=None)
    expected = {"probe": "svc-job", "arch": report["host"]["execution_arch"], "page_size": report["host"]["page_size"],
                "windows": report["host"]["os"] == "Windows", "job_calls": 0}
    actual = json.loads(output) if output.strip() else None
    if not add_case(report, "job.ordinary", not executed["timed_out"] and executed["exit_code"] == 0 and not errors and actual == expected,
                    expected, actual, executed, args.timeout): raise BuildError("Portable Job ordinary-entry assertion failed")
    if report["host"]["os"] != "Windows":
        executed, output, errors = run(report, directory, "nonwindows", [*prefix, artifact, "--suite"], args.timeout, expected=None)
        passed = not executed["timed_out"] and executed["exit_code"] not in (None, 0) and "Job is unavailable on this host" in errors
        add_case(report, "job.nonwindows-rejected", passed, "Nonzero exit without private Windows calls",
                 {"stdout": output, "stderr": errors}, executed, args.timeout)
        if not passed: raise BuildError("Non-Windows private entry was not rejected")


def consume(args, report, directory):
    if not args.input_report or not args.input_report.is_file() or not args.artifact or not args.artifact.is_file():
        raise BuildError("Supplied Ubuntu producer and application artifact required; validation never compiles", "blocked")
    producer = read_json(args.input_report)
    consume_identity(producer, report)
    if not isinstance(producer, dict) or producer.get("schema_version") != 1 or producer.get("report_kind") != "svc-job-build" or producer.get("status") != "pass":
        raise BuildError("Passing svc-job-build v1 producer required")
    entries = producer.get("artifacts", [])
    if len(entries) != 1 or entries[0].get("name") != "svc-job":
        raise BuildError("Exactly one independent Job artifact required")
    artifact = identity(args.artifact)
    if any(artifact[key] != entries[0].get(key) for key in ("sha256", "size_bytes")):
        raise BuildError("Distributed Job artifact SHA-256/size differs from producer")
    if not isinstance(producer.get("build"), dict) or not isinstance(producer.get("source"), dict):
        raise BuildError("Producer source/build identity missing")
    if not all(re.fullmatch(r"[0-9a-f]{64}", producer[key].get("sha256", "")) for key in ("source", "build")):
        raise BuildError("Producer source/build digest invalid")
    if entries[0].get("audit", {}).get("status") != "pass" or [s.get("machine") for s in entries[0].get("headers", {}).get("slices", [])] != [62, 183]:
        raise BuildError("Producer does not identify an audited fat Job application")
    report.update(artifact=artifact, producer=identity(args.input_report), source=producer["source"],
                  build=producer["build"], toolchain=producer["toolchain"])
    prefix = []
    if report["host"]["os"] != "Windows":
        if not args.loader or not args.loader.is_file(): raise BuildError("Supplied native loader required", "blocked")
        matches = [item for item in producer["loaders"] if item["os"] == report["host"]["os"] and item["arch"] == report["host"]["execution_arch"]]
        loader = identity(args.loader)
        if len(matches) != 1 or any(loader[k] != matches[0][k] for k in ("sha256", "size_bytes")):
            raise BuildError("Distributed native loader differs from Ubuntu producer")
        args.loader.chmod(args.loader.stat().st_mode | 0o111); args.artifact.chmod(args.artifact.stat().st_mode | 0o111)
        prefix = [args.loader.resolve()]; report["loader"] = loader
    ordinary(args, report, directory, prefix, args.artifact.resolve())
    if report["host"]["os"] == "Windows": windows(args, report, directory)
    if sha256(args.artifact) != artifact["sha256"]: raise BuildError("Artifact changed during validation")


def valid_record(name, actual):
    if not isinstance(actual, dict): return False
    if name == "job.context":
        return actual.get("membership_query") == 1 and actual.get("in_job") in (0, 1) and actual.get("runner_assigned_or_terminated") is False and (not actual["in_job"] or actual.get("immediate_limits_query") == 1)
    if name == "job.handles":
        warm = actual.get("warmup_counts")
        return actual.get("count_query") is True and actual.get("warmup_cleanup") is True and isinstance(warm, list) and len(warm) == 3 and len(set(warm)) == 1 and isinstance(actual.get("opened"), int) and actual["opened"] > 0 and actual["opened"] == actual.get("closed") and actual.get("before") == actual.get("after") == warm[-1]
    return name in EXPECTED and all(actual.get(k) == v for k, v in EXPECTED[name].items())


def collect(report, output, errors, executed, names, timeout):
    records = [json.loads(line) for line in output.splitlines() if line.strip()]
    ids = [record.get("case_id") for record in records]
    if len(ids) != len(set(ids)) or set(ids) - names: raise BuildError("Duplicate or unexpected native Job case")
    for name in sorted(names):
        record = next((record for record in records if record["case_id"] == name), None)
        if not record:
            add_case(report, name, False, EXPECTED.get(name, "Required measured result"), None, executed, timeout)
            report["cases"][-1]["status"] = "not_run"
        else:
            add_case(report, name, record.get("status") == "pass" and valid_record(name, record.get("actual")) and not errors and not executed["timed_out"],
                     EXPECTED.get(name, "Measured membership/handle-count invariant"), record["actual"], executed, timeout)


def record_launch(report, label, case_id, executed, output, errors, facts, names, timeout):
    """Retain and gate each real launch before its result can be superseded."""
    report["commands"].append(executed)
    report.setdefault("supervisors", {})[label] = {"command": executed, "facts": facts, "expected_exit_code": 0}
    report["supervisor"] = facts
    cleaned = facts.get("cleanup", {})
    report["cleanup"] = cleaned
    clean = (all(cleaned.get(key) is True for key in
                 ("terminate_owned_job", "probe_reaped", "accounting_query", "thread_closed", "process_closed", "job_closed"))
             and type(cleaned.get("active_processes")) is int and cleaned["active_processes"] == 0)
    launched = (all(facts.get(key) is True for key in
                    ("job_created", "kill_on_close_set", "created_suspended", "assigned", "verified_before_resume", "resumed"))
                and facts.get("inheritable") is False and facts.get("errors") == [] and not facts.get("error"))
    # Both native modes exit zero only after checking their assertions. The
    # missing export's expected OS failure is null/error 127, not permission to
    # ignore an unexpected nonzero process exit.
    execution_ok = executed.get("timed_out") is False and type(executed.get("exit_code")) is int and executed["exit_code"] == 0
    passed = clean and launched and execution_ok
    add_case(report, case_id, passed,
             "Owned non-inheritable fence verified before resume; exit 0 without timeout/error; zero active processes, waited probe and all handles closed",
             facts, executed, timeout)
    try:
        collect(report, output, errors, executed, names, timeout)
    finally:
        if not passed:
            blocked = clean and not facts.get("resumed") and bool(facts.get("error"))
            report["capability"] = "blocked" if blocked else "failed"
            raise BuildError("Windows supervisor launch or cleanup failed: " + label, "blocked" if blocked else "fail")


def windows(args, report, directory):
    from svc_job_windows import launch
    report["runner"] = [identity(ROOT / "scripts/svc_job.py"), identity(ROOT / "scripts/svc_job_windows.py")]
    report["consumer_source"] = {"files": report["runner"],
                                 "sha256": hashlib.sha256(json.dumps(report["runner"], sort_keys=True).encode()).hexdigest(),
                                 "note": "Current consumer bytes; producer source/build and C artifact identity remain historical and unchanged"}
    report["scope"] = "Current Windows host; local execution is not an isolated VM; CI identity recorded separately"
    report["coverage"] = ["Owned parent creates exactly one inherited child; explicit terminate and two independent waits",
                          "Owned leaf assigned to two new nested Jobs; kill on last inner close",
                          "Query-only Job handle rejects assignment; fixture is never resumed",
                          "Owned leaf exits early; owned hanging leaf deadline kills on close",
                          "Supervisor fence contains only the artifact and its newly created descendants"]
    report["unverified"] = ["Arbitrary process trees, breakaway, unrelated Jobs, cross-session/security policy, reboot, ARM Windows, production API"]
    report["capability_scope"] = "Owned fixtures only: membership, termination, close results and stable process handle counts after measured initialization"
    missing_dir = directory / "missing-export"; missing_dir.mkdir()
    executed, output, errors, facts = launch([str(args.artifact.resolve()), "--resolve-missing"], missing_dir, args.timeout)
    record_launch(report, "missing-export", "job.missing-export-supervisor", executed, output, errors, facts,
                  {"job.missing-export"}, args.timeout)
    suite_dir = directory / "suite"; suite_dir.mkdir()
    executed, output, errors, facts = launch([str(args.artifact.resolve()), "--hang" if args.probe_mode == "hang" else "--suite"], suite_dir, args.timeout)
    record_launch(report, "suite", "job.supervisor-cleanup", executed, output, errors, facts, REQUIRED, args.timeout)
    handles = next((case["actual"] for case in report["cases"] if case["case_id"] == "job.handles"), None)
    if isinstance(handles, dict):
        report["limitations"] = {"initial_process_handle_count": handles.get("initial"),
                                 "repeat_baseline_count": handles.get("before"),
                                 "initial_growth_attribution": "Unproven; no claim that all process handles are leak-free"}
    failed = [case for case in report["cases"] if case["status"] != "pass"]
    if failed or executed["exit_code"] != 0 or executed["timed_out"]:
        if any(case["case_id"] in ("job.tree-terminate", "job.nested-close") and isinstance(case["actual"], dict) and case["actual"].get("winerror") == 5 for case in failed):
            report["capability"] = "blocked"
            raise BuildError("Current host rejected owned Job assignment/nesting with ERROR_ACCESS_DENIED; not evidence of an ABI failure", "blocked")
        report["capability"] = "failed"
        raise BuildError("Job cases incomplete, failed or timed out; native OS values and cleanup retained")
    report["capability"] = "verified"


def check_ci(args, report):
    workflow = read_json(args.workflow)
    from ci_tiers import check_scoped_job
    check_scoped_job(workflow, "build", "svc_job")
    jobs = workflow["jobs"]
    if set(jobs) != {"build", "windows-job"} or not jobs["build"]["runs-on"].startswith("ubuntu-"):
        raise BuildError("Job workflow requires isolated Ubuntu producer and Windows consumer")
    consumer = jobs["windows-job"]
    if consumer["needs"] != "build" or not consumer["runs-on"].startswith("windows-"):
        raise BuildError("Windows Job consumer must depend on Ubuntu producer")
    text = "\n".join(step.get("run", "") for step in jobs["build"]["steps"])
    for required in ("test_svc_job.py", "scripts/fetch_locked_toolchain.py", "scripts/nightly_toolchain.py fetch",
                     "scripts/ape_helpers.py fetch", "scripts/svc_job.py build"):
        if required not in text: raise BuildError("Job producer missing locked-input/build step: " + required)
    for step in consumer["steps"]:
        if re.search(r"\b(?:make|cmake|ninja|cc|gcc|clang|cl|cosmocc)\b|(?:svc_job|build_ape|native_io|startup_loaders)\.py\s+build", step.get("run", "")):
            raise BuildError("Job Windows consumer must not compile")
    downloads = [step for step in consumer["steps"] if "download-artifact@" in step.get("uses", "")]
    if len(downloads) != 1 or downloads[0]["with"]["name"] != "svc-job-build-${{ github.sha }}":
        raise BuildError("Job consumer must download same-commit independent producer")
    if not any("svc_job.py run --input-report" in step.get("run", "") and "--artifact" in step["run"] for step in consumer["steps"]):
        raise BuildError("Job Windows consumer must verify distributed producer/artifact")
    for job in jobs.values():
        if not any("upload-artifact@" in step.get("uses", "") and step.get("if") == "${{ always() }}" for step in job["steps"]):
            raise BuildError("Job workflow must retain evidence on failure")
    report["workflow"] = identity(args.workflow)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("operation", choices=("build", "run", "check-ci"))
    parser.add_argument("--report", type=Path, required=True)
    parser.add_argument("--input-report", type=Path)
    parser.add_argument("--artifact", type=Path)
    parser.add_argument("--loader", type=Path)
    parser.add_argument("--toolchain-cache", type=Path, default=ROOT / ".cache/toolchains/cosmocc")
    parser.add_argument("--helper-source-cache", type=Path, default=ROOT / ".cache/sources/ape-loader")
    parser.add_argument("--build-dir", type=Path, default=ROOT / ".cache/build")
    parser.add_argument("--output-dir", type=Path, default=ROOT / "out/svc-job/artifacts")
    parser.add_argument("--timeout", type=int, default=15)
    parser.add_argument("--compile-timeout", type=int, default=300)
    parser.add_argument("--probe-mode", choices=("suite", "hang"), default="suite")
    parser.add_argument("--workflow", type=Path, default=ROOT / ".github/workflows/svc-job.yml")
    args = parser.parse_args()
    if not 1 <= args.timeout <= 300 or not 1 <= args.compile_timeout <= 900:
        parser.error("timeout must be 1..300 and compile-timeout 1..900")
    directory = args.report.resolve().parent / "evidence" / str(uuid.uuid4())
    directory.mkdir(parents=True)
    report = {"schema_version": 1, "report_kind": "svc-job-" + args.operation,
              "report_id": directory.name, "started_at": now(), "status": "not_run", "capability": "not_run",
              "m0_core_status": "not_run", "commands": [], "cases": [], "artifacts": [],
              "host": host_facts(), "ci": ci_facts(), "input_reports": {}, "source": None,
              "build": None, "toolchain": None}
    try:
        if args.operation == "run":
            consume(args, report, directory)
        elif args.operation == "build":
            build(args, report, directory)
        elif args.operation == "check-ci":
            check_ci(args, report)
        else:
            raise BuildError("Operation not yet implemented", "blocked")
        report.update(status="pass", reason="Private Job probe completed")
    except BuildError as error:
        report.update(status=error.status, reason=str(error))
    except (OSError, ValueError, KeyError, TypeError, AttributeError) as error:
        report.update(status="fail", reason=str(error))
    report["finished_at"] = now()
    write_json(args.report, report)
    print(json.dumps({key: report[key] for key in ("status", "capability", "reason")}))
    return {"pass": 0, "fail": 1, "blocked": 2}[report["status"]]


if __name__ == "__main__":
    sys.exit(main())
