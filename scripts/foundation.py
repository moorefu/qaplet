#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Local developer entry points. Requires Python 3.10+ and no Python packages."""

import argparse
import ctypes
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import platform
import shutil
import signal
import subprocess
import sys
import tempfile
import uuid

from evidence import check_workflow, read_json, validate_report


ROOT = Path(__file__).resolve().parents[1]


def now():
    return datetime.now(timezone.utc).isoformat()


def sha256(path):
    with Path(path).open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest() if sys.version_info >= (3, 11) else hashlib.sha256(stream.read()).hexdigest()


def decode(data):
    if not data:
        return ""
    return data.decode("utf-16" if b"\x00" in data[:100] else "utf-8", errors="replace")


def command(argv, directory, label, timeout=30):
    started = now()
    stdout_log = directory / (label + ".stdout.log")
    stderr_log = directory / (label + ".stderr.log")
    stdout, stderr, exit_code, timed_out = b"", b"", None, False
    try:
        options = {"creationflags": subprocess.CREATE_NEW_PROCESS_GROUP} if os.name == "nt" else {"start_new_session": True}
        with subprocess.Popen(argv, cwd=ROOT, stdout=subprocess.PIPE, stderr=subprocess.PIPE, **options) as proc:
            try:
                stdout, stderr = proc.communicate(timeout=timeout)
            except subprocess.TimeoutExpired:
                timed_out = True
                if os.name == "nt":
                    taskkill = str(Path(os.environ.get("SystemRoot", r"C:\Windows")) / "System32" / "taskkill.exe")
                    try:
                        subprocess.run([taskkill, "/PID", str(proc.pid), "/T", "/F"], capture_output=True, timeout=5)
                    finally:
                        if proc.poll() is None:
                            proc.kill()
                else:
                    os.killpg(proc.pid, signal.SIGKILL)
                stdout, stderr = proc.communicate(timeout=5)
            exit_code = proc.returncode
    except (OSError, subprocess.SubprocessError) as error:
        stderr = str(error).encode("utf-8")
    stdout_log.write_bytes(stdout)
    stderr_log.write_bytes(stderr)
    return {
        "argv": [str(arg) for arg in argv], "cwd": str(ROOT),
        "started_at": started, "finished_at": now(),
        "exit_code": exit_code, "timeout_seconds": timeout, "timed_out": timed_out,
        "stdout_log": str(stdout_log), "stderr_log": str(stderr_log),
    }, decode(stdout), decode(stderr)


def host_facts():
    machine = platform.machine().lower()
    arch = {"amd64": "x86_64", "arm64": "aarch64"}.get(machine, machine)
    page_size = None
    execution_arch = arch
    if os.name == "nt":
        class SystemInfo(ctypes.Structure):
            _fields_ = [("arch", ctypes.c_ushort), ("reserved", ctypes.c_ushort),
                        ("page_size", ctypes.c_ulong), ("min_addr", ctypes.c_void_p),
                        ("max_addr", ctypes.c_void_p), ("mask", ctypes.c_size_t),
                        ("processors", ctypes.c_ulong), ("type", ctypes.c_ulong),
                        ("granularity", ctypes.c_ulong), ("level", ctypes.c_ushort),
                        ("revision", ctypes.c_ushort)]
        info = SystemInfo()
        ctypes.windll.kernel32.GetSystemInfo(ctypes.byref(info))
        page_size = info.page_size
        process_machine, native_machine = ctypes.c_ushort(), ctypes.c_ushort()
        if ctypes.windll.kernel32.IsWow64Process2(ctypes.windll.kernel32.GetCurrentProcess(), ctypes.byref(process_machine), ctypes.byref(native_machine)):
            execution_arch = {0x8664: "x86_64", 0xaa64: "aarch64", 0x14c: "x86"}.get(process_machine.value or native_machine.value, "unknown")
    elif hasattr(os, "sysconf"):
        page_size = os.sysconf("SC_PAGE_SIZE")
        if ctypes.sizeof(ctypes.c_void_p) == 4 and arch == "x86_64":
            execution_arch = "x86"
    return {
        "os": platform.system(), "cpu": arch, "execution_arch": execution_arch,
        "kernel": platform.version() if os.name == "nt" else platform.release(), "page_size": page_size,
        "execution_mode": "native" if execution_arch == arch else "unknown",
        "measurement": "Python platform + OS page-size API; C case records its own execution architecture",
    }


def source_identity():
    paths = ["Makefile", "scripts/foundation.py", "scripts/evidence.py", "tests/native/foundation_smoke.c",
             "tests/contracts/bootstrap-cases.json", "compat/foundation-report.schema.json",
             ".github/workflows/foundation.yml"]
    files = [{"path": path, "sha256": sha256(ROOT / path)} for path in paths if (ROOT / path).is_file()]
    return {"git_commit": None, "files": files,
            "sha256": hashlib.sha256(json.dumps(files, sort_keys=True).encode()).hexdigest(),
            "note": "Content identity of bootstrap inputs; git revision is unknown until queried."}


def new_report(scope, directory):
    return {"schema_version": 1, "report_id": directory.name, "scope": scope,
            "started_at": now(), "finished_at": None, "status": "not_run",
            "m0_core_status": "not_run", "source": source_identity(),
            "host": host_facts(), "ci": None, "qemu": None, "cases": [],
            "reason": "", "evidence_directory": str(directory)}


def collect_context(report, directory):
    git = shutil.which("git")
    if git:
        _, revision, _ = command([git, "rev-parse", "--verify", "HEAD"], directory, "source-revision")
        revision = revision.strip()
        if len(revision) in {40, 64} and all(c in "0123456789abcdef" for c in revision):
            report["source"]["git_commit"] = revision
            report["source"]["note"] = "Git revision plus current bootstrap input hashes (working tree may differ)"
    if os.environ.get("GITHUB_ACTIONS") == "true":
        # Explicit allowlist: never serialize the environment, auth, or remote URLs.
        report["ci"] = {key: os.environ.get(value) for key, value in {
            "run_id": "GITHUB_RUN_ID", "run_attempt": "GITHUB_RUN_ATTEMPT",
            "workflow": "GITHUB_WORKFLOW", "commit": "GITHUB_SHA",
            "runner_name": "RUNNER_NAME", "runner_os": "RUNNER_OS",
            "runner_arch": "RUNNER_ARCH", "image_os": "ImageOS", "image_version": "ImageVersion",
        }.items()}


def diagnose_tool(name, candidates, directory, version_args=None):
    path = next((found for item in candidates if (found := shutil.which(item))), None)
    if path is None:
        return {"name": name, "path": None, "version": None, "status": "blocked", "reason": "Executable not on PATH", "command": None}
    executed, stdout, stderr = command([path, *(version_args or ["--version"])], directory, "tool-" + name, 10)
    passed = executed["exit_code"] == 0 and not executed["timed_out"]
    return {"name": name, "path": path, "version": (stdout or stderr).strip(),
            "status": "pass" if passed else "blocked",
            "reason": "Queried executable version" if passed else "Version query failed or timed out",
            "command": executed}


def wsl_executable(args):
    if args.wsl:
        return shutil.which(args.wsl)
    installed = Path(os.environ.get("ProgramFiles", r"C:\Program Files")) / "WSL/wsl.exe"
    if os.name == "nt" and installed.is_file():
        return str(installed)
    return shutil.which("wsl.exe")


def linux_build_environment(args, report, directory):
    host = report["host"]
    if host["os"] == "Linux" and host["execution_arch"] == "x86_64":
        return case_record("environment.linux-x64", "pass", "Actual Linux x64 host measured; input/toolchain readiness is separate", actual=host)
    if os.name != "nt":
        return case_record("environment.linux-x64", "blocked", "Primary Linux x64 build environment unavailable on this host")
    wsl = wsl_executable(args)
    if not wsl:
        return case_record("environment.wsl2-linux-x64", "blocked", "WSL executable unavailable; Linux kernel/distribution/page size unknown")
    commands = []
    inventory = {"path": wsl, "linux_host": None}
    for label, arguments in [("status", ["--status"]), ("distributions", ["--list", "--verbose"])]:
        executed, stdout, stderr = command([wsl, *arguments], directory, "wsl-" + label, 15)
        commands.append(executed)
        inventory[label] = (stdout or stderr).strip()
    executed, stdout, _ = command([wsl, "--exec", "sh", "-c", "uname -s; uname -m; uname -r; getconf PAGESIZE"], directory, "wsl-linux-host", 15)
    commands.append(executed)
    lines = stdout.strip().splitlines()
    measured = None
    if executed["exit_code"] == 0 and not executed["timed_out"] and len(lines) == 4:
        if lines[0] == "Linux" and lines[1] == "x86_64" and "wsl2" in lines[2].lower() and lines[3].isdigit() and int(lines[3]) > 0:
            measured = {"os": lines[0], "execution_arch": lines[1], "kernel": lines[2], "page_size": int(lines[3]), "execution_mode": "native"}
    return case_record("environment.wsl2-linux-x64", "pass" if measured else "blocked",
                       "Measured WSL2 Linux x64 kernel and page size" if measured else "No usable WSL2 Linux x64 visible to this process: distribution/permissions/measurement unavailable; see WSL logs",
                       commands=commands, actual={**inventory, "linux_host": measured}, exit_code=executed["exit_code"], timeout_seconds=15)


def doctor(args, report, directory):
    report["tools"] = [diagnose_tool(name, candidates, directory) for name, candidates in [
        ("python", [sys.executable]), ("cc", [args.cc]), ("make", ["make", "gmake", "mingw32-make"]),
        ("git", ["git"]), ("cmake", ["cmake"]), ("ninja", ["ninja"]),
        ("cosmocc", ["cosmocc"]), ("qemu-system-aarch64", ["qemu-system-aarch64"]),
    ]]
    if os.name == "nt":
        report["tools"].append(diagnose_tool("wsl", [wsl_executable(args)] if wsl_executable(args) else [], directory))
    for tool in report["tools"][:4]:
        report["cases"].append(case_record("environment.tool." + tool["name"], tool["status"], tool["reason"],
                                           commands=[tool["command"]] if tool["command"] else [],
                                           actual={"path": tool["path"], "version": tool["version"]}))
    report["cases"].append(linux_build_environment(args, report, directory))
    report["cases"].extend(pending_m0_cases())
    report["cases"].append(case_record("environment.remote-ci", "not_run" if report["ci"] is None else "pass",
                                       "No remote execution performed by doctor" if report["ci"] is None else "Doctor is executing inside this CI run; product probes remain separate", actual=report["ci"]))
    report["status"] = "blocked"
    report["reason"] = "Environment inventory recorded; M0 prerequisites/product evidence remain unavailable"


def case_record(case_id, status, reason, **fields):
    record = {"case_id": case_id, "status": status, "reason": reason,
              "started_at": now(), "finished_at": now(), "commands": [],
              "expected": None, "actual": None, "exit_code": None,
              "timeout_seconds": 30, "toolchain": None, "build": None, "artifact": None}
    record.update(fields)
    if record["commands"]:
        record["started_at"] = record["commands"][0]["started_at"]
        record["finished_at"] = record["commands"][-1]["finished_at"]
    return record


def pending_m0_cases():
    # BOOT-01 deliberately cannot verify product inputs or close the BOOT-09 gate.
    pending = {
        "m0.inputs.quickjs": "QuickJS source lock/provenance verifier not implemented (task 03)",
        "m0.inputs.cosmocc": "Pinned Nightly archive/cache verifier not implemented (task 04)",
        "m0.artifacts": "Hello/eval fat APE build and identities not implemented (task 05)",
        "m0.host.linux-x64": "Same-artifact Linux x64 product probe not executed (task 06)",
        "m0.host.windows-x64": "Same-artifact Windows x64 product probe not executed (task 06)",
        "m0.host.macos-x64": "Same-artifact macOS x64 product probe not executed (task 06)",
        "m0.host.macos-arm64": "Same-artifact macOS ARM64 product probe not executed (task 06)",
        "m0.host.linux-arm64-4k": "Native/complete-system ARM64 4K product probe not executed (task 06)",
        "m0.host.linux-arm64-64k": "Native/complete-system ARM64 64K product probe not executed (tasks 06/07)",
        "m0.native-io": "Native I/O baseline not executed (task 08)",
        "m0.offline": "Offline rebuild/integrity rejection not executed (task 09)",
        "m0.core-gate": "Full evidence/link-boundary gate not implemented (task 10)",
    }
    return [case_record(name, "blocked" if name in {"m0.inputs.quickjs", "m0.inputs.cosmocc", "m0.artifacts", "m0.core-gate"} else "not_run", reason)
            for name, reason in pending.items()]


def smoke(args, report, directory):
    compiler = shutil.which(args.cc)
    if not compiler:
        return case_record("bootstrap.c-smoke", "blocked", "C compiler not found: " + args.cc)
    version, version_out, version_err = command([compiler, "--version"], directory, "compiler-version")
    toolchain = {"path": compiler, "sha256": sha256(compiler), "version": (version_out or version_err).strip()}
    artifact = directory / ("foundation-smoke.exe" if os.name == "nt" else "foundation-smoke")
    source = ROOT / "tests/native/foundation_smoke.c"
    if not source.is_file():
        return case_record("bootstrap.c-smoke", "blocked", "Required C smoke source is missing")
    argv = [compiler, "-std=c11", "-Wall", "-Wextra", "-Werror", str(source), "-o", str(artifact)]
    compiled, _, _ = command(argv, directory, "compile", args.timeout)
    commands = [version, compiled]
    fields = {"commands": commands, "toolchain": toolchain,
              "build": {"id": directory.name, "argv": argv, "source_sha256": sha256(source)},
              "timeout_seconds": args.timeout, "exit_code": compiled["exit_code"]}
    if compiled["exit_code"] != 0 or compiled["timed_out"] or not artifact.is_file():
        return case_record("bootstrap.c-smoke", "fail", "C compilation failed, timed out, or produced no artifact", **fields)
    fields["artifact"] = {"path": str(artifact), "sha256": sha256(artifact), "size_bytes": artifact.stat().st_size, "kind": "system-c-bootstrap"}
    executed, stdout, _ = command([str(artifact)], directory, "run", args.timeout)
    commands.append(executed)
    fields["exit_code"] = executed["exit_code"]
    fields["expected"] = {"answer": 42, "page_size": "positive OS measurement", "execution_arch": report["host"]["execution_arch"]}
    try:
        actual = json.loads(stdout)
        valid = actual["answer"] == 42 and type(actual["page_size"]) is int and actual["page_size"] > 0 and actual["execution_arch"] == report["host"]["execution_arch"]
    except (ValueError, KeyError, TypeError):
        actual, valid = stdout, False
    fields["actual"] = actual
    passed = valid and executed["exit_code"] == 0 and not executed["timed_out"]
    return case_record("bootstrap.c-smoke", "pass" if passed else "fail",
                       "Executed system C artifact and checked output/exit status" if passed else "C artifact output or exit status violated the bootstrap contract", **fields)


def write_report(path, report):
    report["finished_at"] = now()
    validate_report(report, ROOT / "compat/foundation-report.schema.json")
    with tempfile.NamedTemporaryFile(mode="w", encoding="utf-8", dir=path.parent, delete=False, suffix=".tmp") as stream:
        temporary = Path(stream.name)
        json.dump(report, stream, ensure_ascii=False, indent=2)
        stream.write("\n")
    try:
        os.replace(temporary, path)
    finally:
        temporary.unlink(missing_ok=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("operation", choices=["doctor", "test-foundation", "check-ci", "validate-report"])
    parser.add_argument("--profile", choices=["bootstrap", "m0"], default="m0")
    parser.add_argument("--cc", default=os.environ.get("CC", "cc"), help="single C compiler executable (no shell/flags)")
    parser.add_argument("--wsl", help="WSL executable override; otherwise prefer the installed MSI entry over the legacy PATH wrapper")
    parser.add_argument("--timeout", type=int, default=30)
    parser.add_argument("--report", type=Path)
    parser.add_argument("--input-report", type=Path, help="report for structural validation, without treating it as product execution")
    args = parser.parse_args()
    if args.timeout < 1 or args.timeout > 300:
        parser.error("--timeout must be between 1 and 300 seconds")
    scope = args.profile if args.operation == "test-foundation" else args.operation
    path = (args.report or ROOT / "out" / (scope + ".json")).resolve()
    try:
        path.parent.mkdir(parents=True, exist_ok=True)
        directory = path.parent / "evidence" / str(uuid.uuid4())
        directory.mkdir(parents=True)
        report = new_report(scope, directory)
        collect_context(report, directory)
        if args.operation == "test-foundation":
            try:
                required_cases = read_json(ROOT / "tests/contracts/bootstrap-cases.json")
            except (OSError, ValueError):
                required_cases = None
            if required_cases != ["bootstrap.c-smoke"]:
                report["cases"].append(case_record("bootstrap.required-cases", "fail", "Required case manifest missing, empty, duplicated, or contains unsupported cases"))
            else:
                report["cases"].append(smoke(args, report, directory))
            report["status"] = report["cases"][0]["status"]
            report["reason"] = "Bootstrap only; no M0 product probes executed"
            if report["cases"][0]["status"] != "pass":
                report["reason"] = report["cases"][0]["reason"]
            if args.profile == "m0":
                report["cases"].extend(pending_m0_cases())
                report["status"] = "fail" if report["status"] == "fail" else "blocked"
                report["m0_core_status"] = "blocked"
                report["reason"] = "Required M0 inputs/artifacts/cases unavailable; BOOT-01 cannot close the full Core gate"
        elif args.operation == "doctor":
            doctor(args, report, directory)
        else:
            try:
                if args.operation == "check-ci":
                    actual = check_workflow(ROOT / ".github/workflows/foundation.yml")
                    reason = "Local CI syntax/orchestration check passed; remote jobs not run by this check"
                else:
                    if args.input_report is None:
                        raise ValueError("validate-report requires --input-report")
                    validate_report(read_json(args.input_report), ROOT / "compat/foundation-report.schema.json")
                    actual = {"path": str(args.input_report.resolve()), "sha256": sha256(args.input_report)}
                    reason = "Report structure validated; this does not verify execution or certify M0"
                report["cases"].append(case_record(args.operation, "pass", reason, actual=actual))
                report["status"], report["reason"] = "pass", reason
            except (OSError, ValueError, TypeError, KeyError) as error:
                report["cases"].append(case_record(args.operation, "fail", str(error)))
                report["status"], report["reason"] = "fail", str(error)
        write_report(path, report)
        print(f"{scope}: {report['status']}; M0 Core: {report['m0_core_status']}; report: {path}")
        return {"pass": 0, "fail": 1}.get(report["status"], 2)
    except (OSError, ValueError) as error:
        print(f"foundation: evidence/report error: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
