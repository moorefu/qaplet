#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Ubuntu producer and same-byte consumer for real M2 waker/socket OS probes."""
import argparse
import hashlib
import json
import os
from pathlib import Path, PureWindowsPath
import platform
import shutil
import sys
import uuid
import ape_helpers
from build_ape import publish_ape

from build_ape import (BuildError, audit, identity, native_toolchain, run, verify_inputs,
                       prepare_portable_helpers, link_portable)
from input_lock import component, consume_identity
from evidence import read_json
from foundation import ROOT, host_facts, now, sha256
from native_toolchain import recheck
from nightly_toolchain import write_json
from startup import ci_facts, check_ci_commit
from workflow_support import guard_evidence_outputs


def build(args, report, directory):
    distribution = platform.freedesktop_os_release() if report["host"]["os"] == "Linux" else {}
    if report["host"]["os"] != "Linux" or distribution.get("ID") != "ubuntu":
        raise BuildError("M2 waker/socket artifacts are built on Ubuntu only", "blocked")
    report["host"]["distribution"] = {key: distribution.get(key) for key in ("ID", "VERSION_ID", "PRETTY_NAME")}
    original = verify_inputs(args, report, directory, source=False)
    tree = native_toolchain(args, report, directory, original)
    helpers = prepare_portable_helpers(args, report, directory, tree)
    output = args.output_dir.resolve()
    if output.exists() and any(output.iterdir()):
        raise BuildError("Output directory must be empty; choose a new run")
    work = args.build_dir.resolve() / report["report_id"]
    if any(" " in str(p) for p in (tree, work)):
        raise BuildError("Locked cosmocc does not support spaces in compilation paths")
    work.mkdir(parents=True)
    (work / "tmp").mkdir()
    output.mkdir(parents=True, exist_ok=True)
    source = ROOT / "tests/native/m2_waker.c"
    recipe_files = [source, *(ROOT / "scripts" / name for name in
        ("m2_waker.py", "build_ape.py", "ape_helpers.py", "native_toolchain.py", "foundation.py", "evidence.py", "nightly_toolchain.py", "workflow_support.py"))]
    files = [identity(p) for p in recipe_files]
    git = shutil.which("git")
    commit = None
    if git:
        _, revision, _ = run(report, directory, "source-revision", [git, "rev-parse", "--verify", "HEAD"], 10, expected=None)
        marker = ROOT / ".git"
        if len(revision.strip()) != 40 and marker.is_file() and platform.system() == "Linux":
            value = marker.read_text().strip().removeprefix("gitdir: ")
            windows = PureWindowsPath(value)
            candidate = Path("/mnt") / windows.drive[0].lower() / Path(*windows.parts[1:]) if windows.drive else Path(value)
            if candidate.is_dir():
                _, revision, _ = run(report, directory, "source-revision-worktree", [git, "--git-dir", candidate, "rev-parse", "HEAD"], 10, expected=None)
        if len(revision.strip()) == 40:
            commit = revision.strip()
    report["source"] = {"git_commit": commit, "files": files,
                        "sha256": hashlib.sha256(json.dumps(files, sort_keys=True).encode()).hexdigest(),
                        "note": "Actual recipe/source bytes; commit does not assert a clean working tree"}
    report["build_directory"] = str(work)
    environment = dict(os.environ, TMPDIR=str(work / "tmp"), LC_ALL="C", TZ="UTC", SOURCE_DATE_EPOCH="0")
    environment["BUILDLOG"] = str(output / "m2-waker.build-commands.log")
    own = work / source.name
    shutil.copyfile(source, own)
    flags = ["-Os", "-D_GNU_SOURCE", "-ffile-prefix-map=" + str(work) + "=build", *args.cflag]
    report["build"] = {"flags": flags, "recipe_files": files,
                       "portable_helpers_sha256": helpers["sha256"],
                       "toolchain_lock_sha256": report["toolchain_lock"]["sha256"],
                       "native_helpers_sha256": report["native_toolchain"]["helper_set_sha256"],
                       "resource_order": ["ape-x86_64.elf", "ape-aarch64.elf", "ape-m1.c", "x86_64 ELF", "aarch64 ELF"],
                       "source_date_epoch": 0, "libc_source": report["toolchain"]["libc_source"]}
    report["build"]["sha256"] = hashlib.sha256(json.dumps(report["build"], sort_keys=True).encode()).hexdigest()
    run(report, directory, "m2-waker-compile", [tree / "bin/cosmocc", *flags, "-c", own, "-o", work / "m2-waker.o"],
        args.compile_timeout, work, environment)
    link_portable(args, report, directory, tree, helpers, "m2-waker", [work / "m2-waker.o"], work / "m2-waker.com",
                  work, environment, libraries=("-lpthread",))
    publish_ape(work, output, "m2-waker", helpers, ape_helpers.source_cache(args.helper_source_cache.resolve()))
    report["distributed_helpers"] = [identity(output / "ape-aarch64.elf")]
    artifact = audit(args, report, directory, tree, "m2-waker", output)
    artifact["build_commands"] = identity(output / "m2-waker.build-commands.log")
    report["artifacts"].append(artifact)
    if files != [identity(path) for path in recipe_files]:
        raise BuildError("Probe source/recipe changed during build; artifact cannot be signed off")
    verify_inputs(args, report, directory, source=False, stage="after")
    recheck(report["native_toolchain"], original, component("cosmocc"))
    report.update(status="pass", reason="Independent M2 waker/socket fat APE built and audited on Ubuntu; run evidence is separate")


def producer_inputs(args, report):
    if not args.artifact or not args.artifact.is_file() or not args.build_report or not args.build_report.is_file():
        raise BuildError("Supplied artifact and producer build report are required; no compilation on validation hosts", "blocked")
    producer = read_json(args.build_report)
    consume_identity(producer, report)
    if not isinstance(producer, dict) or producer.get("report_kind") != "m2-waker" or producer.get("status") != "pass":
        raise BuildError("Invalid or nonpassing M2 waker/socket producer report")
    artifacts = [a for a in producer["artifacts"] if a["name"] == "m2-waker"]
    if len(artifacts) != 1:
        raise BuildError("Producer must identify exactly one M2 waker/socket artifact")
    artifact = artifacts[0]
    if sha256(args.artifact) != artifact["sha256"] or args.artifact.stat().st_size != artifact["size_bytes"]:
        raise BuildError("Artifact SHA-256/length differs from Ubuntu producer; refused before execution")
    if producer.get("scope") != "build":
        raise BuildError("A host run report cannot substitute for the Ubuntu producer report")
    if producer.get("schema_version") != 1 or not isinstance(producer.get("source"), dict) or not isinstance(producer.get("build"), dict):
        raise BuildError("Producer schema/source/build identity is missing")
    if producer.get("host", {}).get("distribution", {}).get("ID") != "ubuntu":
        raise BuildError("Probe producer must record its actual Ubuntu distribution")
    digests = (producer["source"].get("sha256"), producer["build"].get("sha256"))
    if not all(isinstance(value, str) and len(value) == 64 and set(value) <= set("0123456789abcdef") for value in digests):
        raise BuildError("Producer schema/source/build identity is missing")
    for name, payload in (("source", producer["source"].get("files")),
                          ("build", {key: value for key, value in producer["build"].items() if key != "sha256"})):
        if hashlib.sha256(json.dumps(payload, sort_keys=True).encode()).hexdigest() != producer[name]["sha256"]:
            raise BuildError("Producer " + name + " recipe digest conflicts with recorded fields")
    if artifact.get("audit", {}).get("status") != "pass" or [s.get("machine") for s in artifact.get("headers", {}).get("slices", [])] != [62, 183]:
        raise BuildError("Producer does not prove audited fat x86_64/AArch64 content")
    report.update(source=producer["source"], toolchain=producer["toolchain"], build=producer["build"],
                  producer_report=identity(args.build_report), artifacts=[{**artifact, "path": str(args.artifact.resolve())}])
    if args.operation == "run":
        check_ci_commit(producer, report)
    return producer, artifact



REQUIRED = {"facts", "waker.before-check", "waker.prepared", "waker.blocked",
            "waker.coalesced", "waker.cancel", "waker.close-before-notify",
            "waker.notify-before-close", "waker.control-failure", "waker.cleanup",
            "socket.connect", "socket.readiness", "socket.halfclose", "socket.eof",
            "socket.connect-error", "socket.invalid", "socket.reuse", "socket.cleanup"}


def matches_contract(record, execution):
    actual = record.get("actual")
    if not isinstance(actual, dict) or record.get("status") != "pass":
        return False
    case = record["case_id"]
    if case == "facts":
        os_match = str(actual.get("os", "")).startswith("Windows") if execution["os"] == "Windows" else actual.get("os") == execution["os"]
        return (os_match and actual.get("probe") == "m2-waker" and actual.get("arch") == execution["execution_arch"]
                and actual.get("page_size") == execution["page_size"] and actual.get("helper_limit") == 1)
    expected = {
        "waker.before-check": {"accepted": 1, "observed": 1, "joined": True},
        "waker.prepared": {"empty_check": 0, "accepted": 1, "observed": 1, "poll": 1, "joined": True},
        "waker.blocked": {"accepted": 1, "observed": 1, "poll": 1, "poll_calls": 1, "timeout_ms": -1, "joined": True},
        "waker.coalesced": {"accepted": 1000, "observed": 1000, "signal_bytes": 1, "extra_poll": 0, "joined": True},
        "waker.cancel": {"accepted": 1, "observed": 1, "poll": 1, "cancel_seen": True, "joined": True},
        "waker.close-before-notify": {"accepted": 0, "closed": True, "joined": True},
        "waker.notify-before-close": {"accepted": 1, "closed": True, "joined": True},
        "waker.control-failure": {"accepted": 1, "observed": 1, "send": -1, "control_failed": True, "fault_fixture": "closed-notify-descriptor"},
        "waker.cleanup": {"all_descriptors_closed": True, "threads_joined": 7, "live_helpers": 0},
        "socket.connect": {"nonblocking": True, "connected": True, "socket_error": 0},
        "socket.readiness": {"empty_poll": 0, "send": 1, "poll": 1, "readable": True, "read": 1, "byte": 83},
        "socket.halfclose": {"shutdown": 0, "poll": 1, "eof": 0, "reverse_send": 1, "reverse_read": 1, "byte": 82},
        "socket.eof": {"peer_closed": True, "poll": 1, "eof": 0},
        "socket.connect-error": {"connection_failed": True},
        "socket.invalid": {"invalid_observed": True},
        "socket.reuse": {"same_descriptor": True, "read": 1, "byte": 78},
        "socket.cleanup": {"all_descriptors_closed": True},
    }.get(case)
    if expected is None or any(actual.get(key) != value for key, value in expected.items()):
        return False
    if case == "waker.blocked":
        return (actual.get("elapsed_ms", -1) >= 80 and actual.get("gate_deadline_ms") == 100
                and actual.get("usage_measured") is True and actual.get("process_cpu_us", -1) >= 0
                and actual.get("maxrss_raw", -1) >= 0)
    if case == "waker.control-failure":
        return actual.get("errno", 0) != 0
    if case == "socket.connect-error":
        return (actual.get("connect") == -1 and actual.get("socket_error", 0) != 0
                and (actual.get("poll") == 1 or actual.get("select", 0) > 0))
    return True


def collect_observations(report, records, executed, artifact, execution, timeout):
    report["execution"] = {key: execution[key] for key in ("os", "execution_arch", "kernel", "page_size", "execution_mode")}
    ids = [record["case_id"] for record in records]
    for record in records:
        if record["status"] not in ("pass", "fail", "blocked"):
            raise BuildError("Unknown probe status; limitations cannot replace required execution")
        if not matches_contract(record, execution):
            record = {**record, "status": "fail", "reason": "Observed native result does not satisfy the probe contract"}
        report["cases"].append({**record, "artifact": artifact, "commands": [executed],
                                "started_at": executed["started_at"], "finished_at": executed["finished_at"],
                                "timeout_seconds": timeout, "exit_code": executed["exit_code"]})
    missing = REQUIRED - set(ids)
    for case in sorted(missing):
        report["cases"].append({"case_id": case, "status": "not_run", "actual": None, "commands": [],
                                "reason": "Required native observation missing"})
    if missing or len(ids) != len(set(ids)) or set(ids) - REQUIRED:
        raise BuildError("Probe returned missing/duplicate/unexpected cases; raw evidence retained")
    if executed["exit_code"] != 0 or executed["timed_out"] or any(case["status"] != "pass" for case in report["cases"]):
        raise BuildError("Real OS probe did not complete every required case")
    report["backend_evidence"] = {
        "waker": "nonblocking loopback TCP pair; owner poll; state published under mutex before signal",
        "socket_wait": "owner poll for connected readiness; failed-connect poll and explicit select observation retained separately",
        "helper_limit": 1, "cancellation": "cooperative state publication plus socket wake; join before close",
        "already_waiting_observation": "condition handoff then timed notification gate; one infinite poll measured >=80ms",
        "scope": "OS feasibility evidence only; does not prove Runtime generation filtering or product APIs"}
    failed_connect = next(record["actual"] for record in records if record["case_id"] == "socket.connect-error")
    report["backend_evidence"]["failed_connect_wait"] = "poll" if failed_connect["poll"] == 1 else "select; poll did not report completion within its recorded deadline"


def consume(args, report, directory):
    producer, artifact = producer_inputs(args, report)
    from startup import measured_host, select_loader
    measured_host(args, report, directory)
    loader = args.loader
    if loader:
        suffix = "elf" if report["host"]["os"] == "Linux" else "macho"
        expected = "bin/ape-" + report["host"]["execution_arch"] + "." + suffix
        entries = [item for item in component("cosmocc")["files"] if item["path"] == expected]
        if len(entries) != 1 or sha256(loader) != entries[0]["sha256"]:
            raise BuildError("Explicit loader differs from locked native helper; use a supplied helpers.json bundle for derived loaders")
        loader.chmod(loader.stat().st_mode | 0o111)
        report["loader"] = {**identity(loader), "origin": "official Nightly lock"}
    else:
        selected = select_loader(args, report)
        loader = Path(selected[0]) if selected else None
    if report["host"]["os"] != "Windows" and not loader:
        raise BuildError("Linux/macOS validation requires supplied native loader", "blocked")
    args.artifact.chmod(args.artifact.stat().st_mode | 0o111)
    argv = ([loader] if loader else []) + [args.artifact, "--" + args.probe_mode]
    executed, stdout, stderr = run(report, directory, "suite", argv, args.timeout, expected=None)
    if executed["timed_out"]:
        report["cases"].append({"case_id": "runner.timeout", "status": "fail", "actual": {
            "timed_out": True, "exit_code": executed["exit_code"], "process_reaped": executed["exit_code"] is not None},
            "commands": [executed], "reason": "External deadline is failure, not successful native cancellation"})
        raise BuildError("Probe deadline exceeded; raw logs and supervisor cleanup retained")
    records = [json.loads(line) for line in stdout.splitlines() if line.strip()]
    collect_observations(report, records, executed, identity(args.artifact), report["host"], args.timeout)
    if stderr:
        raise BuildError("Unexpected native stderr; raw output retained")
    if sha256(args.artifact) != artifact["sha256"] or (loader and sha256(loader) != report["loader"]["sha256"]):
        raise BuildError("Supplied artifact/helper changed during execution")
    report.update(status="pass", reason="Same supplied probe bytes passed on measured host; product support and full M2 matrix remain separate")

def import_guest(args, report):
    if not args.guest_report or not args.guest_report.is_file():
        raise BuildError("Full-system guest evidence report is required", "blocked")
    producer, artifact = producer_inputs(args, report)
    guest_report = read_json(args.guest_report)
    check_ci_commit(producer, guest_report)
    report["guest_report"] = identity(args.guest_report)
    if guest_report.get("schema_version") != 1 or guest_report.get("report_kind") != "arm64-guest" or guest_report.get("status") != "pass":
        raise BuildError("Complete successful arm64-guest v1 transport evidence is required")
    guest, qemu = guest_report["guest"], guest_report["qemu"]
    if guest.get("os") != "Linux" or guest.get("architecture") != "aarch64" or guest.get("execution_mode") != "emulated-system" or guest.get("page_size") not in (4096, 65536):
        raise BuildError("Actual full-system Linux ARM64 4K/64K guest facts are required")
    if guest.get("page_size_alias") != guest["page_size"] or guest.get("smaps_kib") != guest["page_size"] // 1024 or not guest.get("kernel"):
        raise BuildError("Independent guest page-size/kernel observations conflict")
    execution = {"os": "Linux", "execution_arch": "aarch64", "kernel": guest["kernel"],
                 "page_size": guest["page_size"], "execution_mode": "emulated-system"}
    if args.expected_os and args.expected_os != "Linux" or args.expected_arch and args.expected_arch != "aarch64":
        raise BuildError("Guest execution identity differs from requested target")
    cleanup = guest_report["cleanup"]
    transports = [c for c in guest_report["commands"] if "-kernel" in c["argv"] and "-initrd" in c["argv"]]
    if qemu.get("guest_arch") != "aarch64" or qemu.get("acceleration") != "tcg,thread=single" or not qemu.get("machine") or not qemu.get("cpu") or not qemu.get("version"):
        raise BuildError("Full-system QEMU/TCG machine, CPU, and version identities are required")
    if len(transports) != 1 or "qemu-system-aarch64" not in Path(transports[0]["argv"][0]).name or transports[0]["exit_code"] != 0 or transports[0]["timed_out"] or not cleanup["qemu_exited"] or cleanup["timed_out"]:
        raise BuildError("Full-system guest transport/cleanup did not finish successfully")
    for name in ("kernel", "config", "rootfs", "executable", "serial_log"):
        value = qemu[name]["sha256"]
        if not isinstance(value, str) or len(value) != 64 or set(value) - set("0123456789abcdef"):
            raise BuildError("Missing QEMU/guest input SHA-256 identity: " + name)
    files = guest_report["files"]
    before = {item["guest_path"]: item["sha256"] for item in files}
    if len(before) != len(files) or guest["file_sha256"] != before or guest["file_sha256_after"] != before:
        raise BuildError("Guest payload before/after SHA-256 identities conflict")
    targets = [item for item in files if item["sha256"] == artifact["sha256"] and item["size_bytes"] == artifact["size_bytes"]]
    if len(targets) != 1:
        raise BuildError("Guest did not execute the same I/O artifact as the Ubuntu producer")
    target = targets[0]["guest_path"]
    suites = [case for case in guest_report["cases"] if target in case["argv"] and "--suite" in case["argv"]]
    if len(suites) != 1:
        raise BuildError("Exactly one actual M2 waker/socket suite command is required")
    suite = suites[0]
    loader = suite["argv"][0]
    if suite["argv"] != [loader, target, "--suite"] or loader not in before:
        raise BuildError("Guest I/O must use explicit loader/program argv with no shell")
    expected_loader = producer.get("portable_helpers", {}).get("loader", {}).get("sha256")
    if expected_loader and before[loader] != expected_loader:
        raise BuildError("Guest loader differs from producer's derived portable helper")
    if suite["status"] != "pass" or suite["exit_code"] != 0 or suite["actual"]["exit_code"] != 0 or suite["actual"]["stderr"]:
        raise BuildError("Guest M2 waker/socket command did not return a clean success")
    paths = {}
    for kind in ("stdout", "stderr"):
        entry = suite[kind + "_log"]
        path = args.guest_report.parent / "evidence" / guest_report["report_id"] / Path(entry["path"]).name
        if not path.is_file():
            raise BuildError("Guest raw " + kind + " evidence is missing", "blocked")
        if sha256(path) != entry["sha256"] or path.stat().st_size != entry["size_bytes"] or path.read_bytes().decode("utf-8") != suite["actual"][kind]:
            raise BuildError("Guest raw " + kind + " log differs from reported bytes")
        paths[kind] = str(path.resolve())
    records = [json.loads(line) for line in suite["actual"]["stdout"].splitlines() if line.strip()]
    report.update(validation_host=report["host"], validation_ci=report["ci"], host=guest_report["host"],
                  ci=guest_report.get("ci"), guest=guest, qemu=qemu, transport_commands=guest_report["commands"],
                  transport_cleanup=cleanup, loader={"guest_path": loader, "sha256": before[loader]},
                  process_fixture={"guest_path": target, "sha256": artifact["sha256"]},
                  validation_started_at=report["started_at"],
                  guest_execution_window={"started_at": guest_report["started_at"], "finished_at": guest_report["finished_at"]})
    event = {"argv": suite["argv"], "cwd": "guest:/", "started_at": suite["started_at"], "finished_at": suite["finished_at"],
             "timeout_seconds": suite["timeout_seconds"], "exit_code": 0, "timed_out": False,
             "stdout_log": paths["stdout"], "stderr_log": paths["stderr"], "guest_report": report["guest_report"]}
    collect_observations(report, records, event, identity(args.artifact), execution, suite["timeout_seconds"])
    report.update(status="pass", reason="Same-artifact full-system guest I/O observations validated; no native hardware claim")


def guest_manifest(args, report):
    producer, artifact = producer_inputs(args, report)
    if not args.manifest or not args.loader or not args.loader.is_file():
        raise BuildError("Guest manifest output and supplied ARM64 loader are required", "blocked")
    loader = identity(args.loader)
    expected_loader = producer.get("portable_helpers", {}).get("loader", {}).get("sha256")
    if not expected_loader:
        entries = [item for item in component("cosmocc")["files"] if item["path"] == "bin/ape-aarch64.elf"]
        expected_loader = entries[0]["sha256"] if len(entries) == 1 else None
    if loader["sha256"] != expected_loader:
        raise BuildError("Guest loader SHA-256 differs from producer's supplied/locked ARM64 helper")
    arm = next(item for item in artifact["headers"]["slices"] if item["machine"] == 183)
    if not all(load["alignment"] >= args.page_size and (load["virtual_address"] - load["offset"]) % args.page_size == 0 for load in arm["load_segments"]):
        raise BuildError("Producer ARM64 LOAD layout is not proven for the requested guest page size", "blocked")
    guest_loader, guest_program = "/payload/ape-aarch64.elf", "/payload/进程 空格/native io.com"
    payload = {"schema_version": 1, "page_size": args.page_size,
               "files": [{**loader, "guest_path": guest_loader}, {**identity(args.artifact), "guest_path": guest_program}],
               "commands": [{"case_id": "m2-waker.suite", "argv": [guest_loader, guest_program, "--suite"],
                             "expected_exit_code": 0, "expected_stderr": ""}]}
    write_json(args.manifest, payload)
    report.update(status="pass", reason="Same-byte Unicode/space I/O guest payload exported; guest execution not performed",
                  manifest=identity(args.manifest), loader=loader)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("operation", choices=("build", "run", "import-guest", "guest-manifest"))
    parser.add_argument("--artifact", type=Path)
    parser.add_argument("--build-report", type=Path)
    parser.add_argument("--guest-report", type=Path)
    parser.add_argument("--manifest", type=Path)
    parser.add_argument("--page-size", type=int, choices=(4096, 65536), default=65536)
    parser.add_argument("--toolchain-cache", type=Path, default=ROOT / ".cache/toolchains/cosmocc")
    parser.add_argument("--helper-source-cache", type=Path, default=ROOT / ".cache/sources/ape-loader")
    parser.add_argument("--build-dir", type=Path, default=ROOT / ".cache/build")
    parser.add_argument("--output-dir", type=Path, default=ROOT / "out/m2/waker/artifacts")
    parser.add_argument("--loader", type=Path)
    parser.add_argument("--loader-dir", type=Path)
    parser.add_argument("--loader-manifest", type=Path)
    parser.add_argument("--probe-mode", choices=("suite", "hang"), default="suite", help="hang is the deliberate deadline negative fixture")
    parser.add_argument("--expected-os", choices=("Linux", "Windows", "Darwin"))
    parser.add_argument("--expected-arch", choices=("x86_64", "aarch64"))
    parser.add_argument("--runner-label")
    parser.add_argument("--timeout", type=int, default=30)
    parser.add_argument("--compile-timeout", type=int, default=300)
    parser.add_argument("--cflag", action="append", default=[])
    parser.add_argument("--report", type=Path, required=True)
    args = parser.parse_args()
    if not 1 <= args.timeout <= 300 or not 1 <= args.compile_timeout <= 900:
        parser.error("timeout outside allowed bounds")
    args.report = args.report.resolve()
    try:
        if args.manifest and args.manifest.resolve() == args.report:
            raise ValueError("Manifest and report must have separate output paths")
        guard_evidence_outputs([args.report, args.manifest],
                               [path for path in (args.build_report, args.guest_report, args.loader_manifest) if path],
                               files=[args.artifact, args.loader])
    except (OSError, ValueError) as error:
        print(json.dumps({"status": "fail", "reason": str(error)}))
        return 1
    directory = args.report.parent / "evidence" / str(uuid.uuid4())
    directory.mkdir(parents=True)
    report = {"schema_version": 1, "report_kind": "m2-waker", "report_id": directory.name,
              "scope": {"build": "build", "run": "host", "import-guest": "guest", "guest-manifest": "manifest"}[args.operation],
              "started_at": now(), "finished_at": None, "status": "not_run", "product_support_verified": False,
              "reason": "", "host": host_facts(), "ci": ci_facts(), "qemu": None, "source": None,
              "toolchain": None, "build": None, "artifacts": [], "cases": [], "commands": [],
              "input_reports": {}, "evidence_directory": str(directory)}
    report["required_cases"] = sorted(REQUIRED) if args.operation in ("run", "import-guest") else []
    report["runner"] = [identity(ROOT / "scripts" / name) for name in ("m2_waker.py", "startup.py")]
    report["requested_host"] = {"os": args.expected_os, "arch": args.expected_arch, "runner_label": args.runner_label}
    try:
        if args.operation in ("run", "build") and (args.expected_os and args.expected_os != report["host"]["os"] or args.expected_arch and args.expected_arch != report["host"]["execution_arch"]):
            raise BuildError("Actual host OS/execution architecture differs from scheduled target")
        if args.operation == "run":
            consume(args, report, directory)
        elif args.operation == "import-guest":
            import_guest(args, report)
        elif args.operation == "guest-manifest":
            guest_manifest(args, report)
        else:
            build(args, report, directory)
    except BuildError as error:
        report.update(status=error.status, reason=str(error))
    except (OSError, ValueError, KeyError, TypeError, AttributeError) as error:
        report.update(status="fail", reason=str(error))
    report["finished_at"] = now()
    if "guest_execution_window" in report:
        report["validation_finished_at"] = report["finished_at"]
        report.update(report["guest_execution_window"])
    if not report["cases"] and report["status"] != "pass":
        report["cases"].append({"case_id": "m2-waker." + args.operation, "status": report["status"],
                                "reason": report["reason"], "commands": report["commands"],
                                "started_at": report["started_at"], "finished_at": report["finished_at"]})
    write_json(args.report, report)
    print(json.dumps({"status": report["status"], "reason": report["reason"], "report": str(args.report)}))
    return {"pass": 0, "fail": 1, "blocked": 2}[report["status"]]


if __name__ == "__main__":
    sys.exit(main())
