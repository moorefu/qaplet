#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Build on Ubuntu; validate the same native I/O APE bytes on each supplied host."""
import argparse
import errno
import hashlib
import json
import os
from pathlib import Path
import platform
import shutil
import sys
import uuid
import ape_helpers
from build_ape import publish_ape
import zipfile

from build_ape import (BuildError, audit, identity, native_toolchain, run, verify_inputs,
                       prepare_portable_helpers, link_portable)
from input_lock import component, consume_identity
from evidence import read_json
from foundation import ROOT, host_facts, now, sha256
from native_toolchain import recheck
from nightly_toolchain import write_json

RESOURCE = b"qaplet M0 I/O ZIP\n"
REQUIRED = {"facts", "socket.readiness", "socket.halfclose", "socket.invalid", "socket.cleanup",
            "pipe.readiness", "pipe.eof", "pipe.broken-writer", "pipe.cleanup",
            "tty.redirected", "tty.controlled", "tty.restore", "tty.cleanup",
            "process.spawn-wait", "process.missing", "process.timeout-cleanup",
            "pthread.tls", "zipos.read-eof", "zipos.readonly", "zipos.cleanup"}
EXPECTED = {
    "socket.readiness": {"empty_poll": 0, "send": 1, "ready_poll": 1, "revents_contains": "POLLIN", "read": 1, "byte": 83},
    "socket.halfclose": {"shutdown": 0, "eof_poll": 1, "eof_read": 0, "reverse_send": 1, "reverse_read": 1},
    "socket.invalid": {"read": -1, "errno": "EBADF"},
    "socket.cleanup": {"peer_closed": 1, "client_closed": 1, "listener_closed": 1},
    "pipe.readiness": {"empty_poll": 0, "write": 1, "ready_poll": 1, "revents_contains": "POLLIN", "read": 1},
    "pipe.eof": {"writer_closed": 1, "ready_poll": 1, "read": 0},
    "pipe.broken-writer": {"write": -1, "errno": "EPIPE", "sigpipe_ignored": True},
    "pipe.cleanup": "All four owned descriptors closed and independently rejected by F_GETFD with EBADF",
    "tty.redirected": {"isatty": 0, "getattr": -1, "getattr_errno": "ENOTTY", "owned_pipe_closed": True},
    "tty.controlled": "Private PTY/ConPTY console mode is read, echo bit toggled, and the changed mode read back",
    "tty.restore": {"restored": True},
    "tty.cleanup": "All owned PTY descriptors or ConPTY/process/pipe handles closed; controlled process reaped",
    "tty.console-handle-closed": {"CloseHandle": 1, "after_close_GetConsoleMode": 0, "winerror": 6},
    "process.spawn-wait": {"exit_status": 37, "argv_verified": True, "shell": False,
                           "argv": ["--child", "参数 空格", 'quote"slash\\', "literal&|;$()"]},
    "process.missing": {"posix_spawn": "ENOENT", "live_child_remaining": False},
    "process.timeout-cleanup": {"observed_running": True, "killed": True, "reaped": True, "deadline_ms": 150},
    "pthread.tls": {"thread_initial": 17, "thread_final": 99, "parent_tls": 7, "joined": True},
    "zipos.read-eof": {"read": 18, "eof_read": 0, "content_matches": True},
    "zipos.readonly": {"write_open": -1, "errno": "EROFS"},
    "zipos.cleanup": {"reader_closed": 1, "unexpected_writer_closed": True},
    "runner.terminal-exit": {"exit_code": 0, "timed_out": False},
    "runner.terminal-fixture": "Controlled fixture setup, execution and cleanup complete within their deadlines",
}


def append_resource(path):
    entry = zipfile.ZipInfo("io-baseline.txt", (1980, 1, 1, 0, 0, 0))
    entry.external_attr = 0o100444 << 16
    with zipfile.ZipFile(path, "a", compression=zipfile.ZIP_STORED) as archive:
        archive.writestr(entry, RESOURCE)


def build(args, report, directory):
    if report["host"]["os"] != "Linux" or platform.freedesktop_os_release().get("ID") != "ubuntu":
        raise BuildError("Native I/O artifacts are built on Ubuntu only", "blocked")
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
    source = ROOT / "tests/native/native_io.c"
    recipe_files = [source, *(ROOT / "scripts" / name for name in
        ("native_io.py", "native_io_terminal.py", "build_ape.py", "ape_helpers.py", "native_toolchain.py", "foundation.py", "evidence.py", "nightly_toolchain.py"))]
    files = [identity(p) for p in recipe_files]
    git = shutil.which("git")
    commit = None
    if git:
        _, revision, _ = run(report, directory, "source-revision", [git, "rev-parse", "--verify", "HEAD"], 10, expected=None)
        if len(revision.strip()) == 40:
            commit = revision.strip()
    report["source"] = {"git_commit": commit, "files": files,
                        "sha256": hashlib.sha256(json.dumps(files, sort_keys=True).encode()).hexdigest(),
                        "note": "Actual recipe/source bytes; commit does not assert a clean working tree"}
    report["build_directory"] = str(work)
    environment = dict(os.environ, TMPDIR=str(work / "tmp"), LC_ALL="C", TZ="UTC", SOURCE_DATE_EPOCH="0")
    environment["BUILDLOG"] = str(output / "native-io.build-commands.log")
    own = work / source.name
    shutil.copyfile(source, own)
    flags = ["-Os", "-D_GNU_SOURCE", "-ffile-prefix-map=" + str(work) + "=build", *args.cflag]
    report["build"] = {"flags": flags, "recipe_files": files,
                       "portable_helpers_sha256": helpers["sha256"],
                       "toolchain_lock_sha256": report["toolchain_lock"]["sha256"],
                       "native_helpers_sha256": report["native_toolchain"]["helper_set_sha256"],
                       "resource_order": ["ape-x86_64.elf", "ape-aarch64.elf", "ape-m1.c", "x86_64 ELF", "aarch64 ELF"],
                       "source_date_epoch": 0, "libc_source": report["toolchain"]["libc_source"],
                       "resource": {"path": "io-baseline.txt", "sha256": hashlib.sha256(RESOURCE).hexdigest(),
                                    "size_bytes": len(RESOURCE), "compression": "stored", "timestamp": "1980-01-01T00:00:00"}}
    report["build"]["sha256"] = hashlib.sha256(json.dumps(report["build"], sort_keys=True).encode()).hexdigest()
    run(report, directory, "native-io-compile", [tree / "bin/cosmocc", *flags, "-c", own, "-o", work / "native-io.o"],
        args.compile_timeout, work, environment)
    link_portable(args, report, directory, tree, helpers, "native-io", [work / "native-io.o"], work / "native-io.com",
                  work, environment, libraries=("-lpthread",))
    publish_ape(work, output, "native-io", helpers, ape_helpers.source_cache(args.helper_source_cache.resolve()))
    report["distributed_helpers"] = [identity(output / "ape-aarch64.elf")]
    append_resource(output / "native-io.com")
    artifact = audit(args, report, directory, tree, "native-io", output)
    artifact["build_commands"] = identity(output / "native-io.build-commands.log")
    report["artifacts"].append(artifact)
    verify_inputs(args, report, directory, source=False, stage="after")
    recheck(report["native_toolchain"], original, component("cosmocc"))
    report.update(status="pass", reason="Independent native I/O fat APE built and audited on Ubuntu; run evidence is separate")


def producer_inputs(args, report):
    if not args.artifact or not args.artifact.is_file() or not args.build_report or not args.build_report.is_file():
        raise BuildError("Supplied artifact and producer build report are required; no compilation on validation hosts", "blocked")
    producer = read_json(args.build_report)
    consume_identity(producer, report)
    if not isinstance(producer, dict) or producer.get("report_kind") != "native-io" or producer.get("status") != "pass":
        raise BuildError("Invalid or nonpassing native I/O producer report")
    artifacts = [a for a in producer["artifacts"] if a["name"] == "native-io"]
    if len(artifacts) != 1:
        raise BuildError("Producer must identify exactly one native I/O artifact")
    artifact = artifacts[0]
    if sha256(args.artifact) != artifact["sha256"] or args.artifact.stat().st_size != artifact["size_bytes"]:
        raise BuildError("Artifact SHA-256/length differs from Ubuntu producer; refused before execution")
    if producer.get("scope") != "build":
        raise BuildError("A host run report cannot substitute for the Ubuntu producer report")
    if producer.get("schema_version") != 1 or not isinstance(producer.get("source"), dict) or not isinstance(producer.get("build"), dict):
        raise BuildError("Producer schema/source/build identity is missing")
    digests = (producer["source"].get("sha256"), producer["build"].get("sha256"))
    if not all(isinstance(value, str) and len(value) == 64 and set(value) <= set("0123456789abcdef") for value in digests):
        raise BuildError("Producer schema/source/build identity is missing")
    if artifact.get("audit", {}).get("status") != "pass" or [s.get("machine") for s in artifact.get("headers", {}).get("slices", [])] != [62, 183]:
        raise BuildError("Producer does not prove audited fat x86_64/AArch64 content")
    report.update(source=producer["source"], toolchain=producer["toolchain"], build=producer["build"],
                  producer_report=identity(args.build_report), artifacts=[{**artifact, "path": str(args.artifact.resolve())}])
    return producer, artifact


def consume(args, report, directory):
    producer, artifact = producer_inputs(args, report)
    from startup import measured_host, select_loader
    measured_host(args, report, directory)
    loader = args.loader
    if not loader:
        selected = select_loader(args, report)
        loader = Path(selected[0]) if selected else None
    if report["host"]["os"] != "Windows" and not loader:
        raise BuildError("Linux/macOS validation requires an explicit supplied native APE loader", "blocked")
    if loader:
        if args.loader:
            suffix = "elf" if report["host"]["os"] == "Linux" else "macho"
            expected_path = "bin/ape-" + report["host"]["execution_arch"] + "." + suffix
            locked = [f for f in component("cosmocc")["files"] if f["path"] == expected_path]
            if len(locked) != 1 or sha256(loader) != locked[0]["sha256"]:
                raise BuildError("Explicit loader differs from the official Nightly identity; use a supplied helpers.json bundle for derived loaders")
            loader.chmod(loader.stat().st_mode | 0o111)
            report["loader"] = {**identity(loader), "origin": "official Nightly lock"}
    fixture = directory / "进程 空格 fixture"
    fixture.mkdir()
    executable = fixture / ("native io.exe" if os.name == "nt" else "native io.com")
    shutil.copyfile(args.artifact, executable)
    if sha256(executable) != artifact["sha256"]:
        raise BuildError("Unicode/space executable fixture differs from supplied artifact")
    mode_before = executable.stat().st_mode & 0o777
    executable.chmod(0o755)
    report["process_fixture"] = identity(executable)
    report["process_fixture"]["permissions"] = {"mode_before": mode_before, "mode_after": executable.stat().st_mode & 0o777,
                                                "executable_after": os.access(executable, os.X_OK)}
    if os.name != "nt" and not os.access(executable, os.X_OK):
        raise BuildError("Verified native I/O fixture cannot execute on this filesystem", "blocked")
    report["process_fixture"]["child_argv"] = ["--child", "参数 空格", 'quote"slash\\', "literal&|;$()"]
    argv = ([loader] if loader else []) + [executable, "--" + args.probe_mode] + (["--launcher", loader] if loader else [])
    executed, stdout, stderr = run(report, directory, "suite", argv, args.timeout, expected=None)
    if executed["timed_out"]:
        report["cases"].append({"case_id": "runner.timeout", "status": "fail", "artifact": identity(args.artifact),
                                "expected": "Probe returns within the configured deadline", "actual": {
                                    "timed_out": True, "exit_code": executed["exit_code"],
                                    "process_reaped": executed["exit_code"] is not None},
                                "commands": [executed], "started_at": executed["started_at"],
                                "finished_at": executed["finished_at"], "timeout_seconds": args.timeout,
                                "exit_code": executed["exit_code"], "reason": "Deadline exceeded; controlled process group/tree terminated and waited"})
        raise BuildError("Native probe exceeded deadline; raw logs and actual reaped exit retained")
    records = [json.loads(line) for line in stdout.splitlines() if line.strip()]
    if report["host"]["os"] == "Windows":
        from native_io_terminal import conpty
        terminal_file = directory / "console-results.ndjson"
        try:
            terminal_command, terminal_output = conpty([str(executable), "--tty", "--result", str(terminal_file)], directory, args.timeout)
        except AttributeError as error:
            raise BuildError("Required ConPTY API unavailable: " + str(error), "blocked") from error
        report["commands"].append(terminal_command)
        terminal_command["results_log"] = str(terminal_file)
        if terminal_file.is_file():
            terminal_output = terminal_file.read_text(encoding="utf-8")
        for line in terminal_output.splitlines():
            if line.strip().startswith('{"case_id":'):
                item = json.loads(line)
                item["terminal_command"] = terminal_command
                records.append(item)
        cleaned = terminal_command["cleanup"]
        records.append({"case_id": "tty.cleanup", "status": "pass" if all(cleaned.values()) else "fail",
                        "actual": cleaned, "terminal_command": terminal_command})
        if terminal_command["error"]:
            records.append({"case_id": "runner.terminal-fixture",
                            "status": "fail" if terminal_command["exit_code"] is not None or not all(cleaned.values()) else "blocked",
                            "actual": {"reason": terminal_command["error"], "cleanup": cleaned},
                            "terminal_command": terminal_command})
            for case_id in ("tty.controlled", "tty.restore", "tty.console-handle-closed"):
                if not any(r["case_id"] == case_id for r in records):
                    records.append({"case_id": case_id, "status": "blocked", "actual": {"reason": terminal_command["error"]},
                                    "terminal_command": terminal_command})
        elif terminal_command["timed_out"] or terminal_command["exit_code"] != 0:
            records.append({"case_id": "runner.terminal-exit", "status": "fail",
                            "actual": {"exit_code": terminal_command["exit_code"], "timed_out": terminal_command["timed_out"]},
                            "terminal_command": terminal_command})
    collect_observations(report, records, executed, identity(args.artifact), report["host"], args.timeout)
    if sha256(args.artifact) != artifact["sha256"] or sha256(executable) != artifact["sha256"] or (loader and sha256(loader) != report["loader"]["sha256"]):
        raise BuildError("Artifact/helper changed during execution")
    report.update(status="pass", reason="Supplied native I/O bytes executed on measured host; complete M0 matrix remains separate")


def matches_contract(record, execution):
    actual = record.get("actual")
    if not isinstance(actual, dict):
        return False
    case = record["case_id"]
    if case == "facts":
        os_matches = str(actual.get("os", "")).startswith("Windows") if execution["os"] == "Windows" else actual.get("os") == execution["os"]
        return os_matches and actual.get("probe") == "native-io" and actual.get("arch") == execution["execution_arch"] and actual.get("page_size") == execution["page_size"]
    expected = EXPECTED.get(case)
    if expected is None:
        return False
    if isinstance(expected, dict):
        for key, value in expected.items():
            if key in ("argv", "owned_pipe_closed", "live_child_remaining"):
                continue  # Declared fixture conditions, checked at their owning boundary.
            if key == "revents_contains":
                # WinSock has separate normal/band input bits; POSIX hosts use 1.
                input_mask = 0x0300 if execution["os"] == "Windows" else 1
                if not actual.get("revents", 0) & input_mask:
                    return False
                continue
            if isinstance(value, str) and value in ("EBADF", "EPIPE", "ENOTTY", "ENOENT", "EROFS"):
                value = getattr(errno, value)
            if actual.get(key) != value:
                return False
    if case in ("pipe.cleanup", "tty.cleanup"):
        return bool(actual) and all(value in (1, True) for value in actual.values())
    if case == "tty.redirected":
        return actual.get("reader_closed") == 1 and actual.get("writer_closed") == 1
    if case == "tty.controlled":
        if execution["os"] == "Windows":
            return actual.get("GetConsoleMode") == 1 and actual.get("SetConsoleMode") == 1 and actual.get("owned_CONIN_handle") is True and actual.get("modified_mode") == actual.get("before_mode", 0) ^ 4
        return actual.get("isatty") == 1 and actual.get("getattr") == 0 and actual.get("setattr") == 0 and actual.get("modified") is True
    return True


def collect_observations(report, records, executed, artifact, execution, timeout):
    report["execution"] = {key: execution[key] for key in ("os", "execution_arch", "kernel", "page_size", "execution_mode")}
    ids = [r["case_id"] for r in records]
    required = REQUIRED | ({"tty.console-handle-closed"} if execution["os"] == "Windows" else set())
    report["required_cases"] = sorted(required)
    missing = required - set(ids)
    duplicate = len(ids) != len(set(ids))
    optional = {"runner.terminal-fixture", "runner.terminal-exit"} if execution["os"] == "Windows" else set()
    unexpected = set(ids) - required - optional
    for record in records:
        if record["status"] not in ("pass", "fail", "limitation", "blocked"):
            raise BuildError("Unknown native result status")
        if record["status"] == "limitation" and (not record.get("actual") or not record.get("reason")):
            raise BuildError("An OS limitation requires measured actual values and an explicit reason")
        if record["status"] == "pass" and not matches_contract(record, execution):
            record = {**record, "status": "fail", "reason": "Actual native values differ from the shared I/O contract"}
        event = record.pop("terminal_command", executed)
        report["cases"].append({**record, "artifact": artifact, "commands": [event],
                                "expected": EXPECTED.get(record["case_id"], {"arch": execution["execution_arch"], "page_size": execution["page_size"]}),
                                "reason": record.get("reason", "Measured native ABI result; raw OS return values retained"),
                                "started_at": event["started_at"], "finished_at": event["finished_at"],
                                "timeout_seconds": timeout, "exit_code": event["exit_code"]})
    if missing or duplicate or unexpected:
        for case_id in sorted(missing):
            report["cases"].append({"case_id": case_id, "status": "not_run", "actual": None,
                                    "expected": "Required native I/O observation", "commands": [],
                                    "started_at": executed["started_at"], "finished_at": executed["finished_at"],
                                    "reason": "Probe/fixture did not return this required result"})
        raise BuildError("Native suite returned duplicate/missing/unexpected cases; observed results and raw output retained")
    facts = next(r["actual"] for r in records if r["case_id"] == "facts")
    if facts.get("probe") != "native-io" or facts["arch"] != execution["execution_arch"] or facts["page_size"] != execution["page_size"]:
        raise BuildError("Probe architecture/page size differs from actual host OS measurements")
    if any(c["status"] == "fail" for c in report["cases"]):
        raise BuildError("Native I/O suite failed; see measured cases and raw logs")
    if any(c["status"] == "blocked" for c in report["cases"]):
        raise BuildError("Required controlled fixture unavailable; observed API error retained", "blocked")
    if executed["exit_code"] != 0:
        raise BuildError("Native I/O suite returned unexpected nonzero exit; raw OS status retained")


def import_guest(args, report):
    if not args.guest_report or not args.guest_report.is_file():
        raise BuildError("Full-system guest evidence report is required", "blocked")
    producer, artifact = producer_inputs(args, report)
    guest_report = read_json(args.guest_report)
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
    if " " not in target or not any(ord(char) > 127 for char in target):
        raise BuildError("Guest executable Unicode/space path was not measured", "blocked")
    suites = [case for case in guest_report["cases"] if target in case["argv"] and "--suite" in case["argv"]]
    if len(suites) != 1:
        raise BuildError("Exactly one actual native I/O suite command is required")
    suite = suites[0]
    loader = suite["argv"][0]
    if suite["argv"] != [loader, target, "--suite", "--launcher", loader] or loader not in before:
        raise BuildError("Guest I/O must use explicit loader/program argv with no shell")
    expected_loader = producer.get("portable_helpers", {}).get("loader", {}).get("sha256")
    if expected_loader and before[loader] != expected_loader:
        raise BuildError("Guest loader differs from producer's derived portable helper")
    if suite["status"] != "pass" or suite["exit_code"] != 0 or suite["actual"]["exit_code"] != 0 or suite["actual"]["stderr"]:
        raise BuildError("Guest native I/O command did not return a clean success")
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
               "commands": [{"case_id": "native-io.suite", "argv": [guest_loader, guest_program, "--suite", "--launcher", guest_loader],
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
    parser.add_argument("--output-dir", type=Path, default=ROOT / "out/native-io/artifacts")
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
    directory = args.report.parent / "evidence" / str(uuid.uuid4())
    directory.mkdir(parents=True)
    report = {"schema_version": 1, "report_kind": "native-io", "report_id": directory.name,
              "scope": {"build": "build", "run": "host", "import-guest": "guest", "guest-manifest": "manifest"}[args.operation],
              "started_at": now(), "finished_at": None, "status": "not_run", "m0_core_status": "not_run",
              "reason": "", "host": host_facts(), "ci": None, "qemu": None, "source": None,
              "toolchain": None, "build": None, "artifacts": [], "cases": [], "commands": [],
              "input_reports": {}, "evidence_directory": str(directory)}
    report["required_cases"] = sorted(REQUIRED) if args.operation == "run" else []
    report["runner"] = [identity(ROOT / "scripts" / name) for name in ("native_io.py", "native_io_terminal.py", "startup.py")]
    report["requested_host"] = {"os": args.expected_os, "arch": args.expected_arch, "runner_label": args.runner_label}
    if os.environ.get("GITHUB_ACTIONS") == "true":
        report["ci"] = {key: os.environ.get(value) for key, value in {
            "run_id": "GITHUB_RUN_ID", "run_attempt": "GITHUB_RUN_ATTEMPT", "workflow": "GITHUB_WORKFLOW",
            "commit": "GITHUB_SHA", "runner_name": "RUNNER_NAME", "runner_os": "RUNNER_OS",
            "runner_arch": "RUNNER_ARCH", "image_os": "ImageOS", "image_version": "ImageVersion"}.items()}
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
        report["cases"].append({"case_id": "native-io." + args.operation, "status": report["status"],
                                "reason": report["reason"], "commands": report["commands"],
                                "started_at": report["started_at"], "finished_at": report["finished_at"]})
    write_json(args.report, report)
    print(json.dumps({"status": report["status"], "reason": report["reason"], "report": str(args.report)}))
    return {"pass": 0, "fail": 1, "blocked": 2}[report["status"]]


if __name__ == "__main__":
    sys.exit(main())
