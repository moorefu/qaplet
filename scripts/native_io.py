#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Build on Ubuntu; validate the same native I/O APE bytes on each supplied host."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import shutil
import sys
import uuid
import zipfile

from build_ape import (BuildError, audit, identity, native_toolchain, run, verify_inputs,
                       prepare_portable_helpers, link_portable)
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
    for suffix in (".com", ".com.dbg", ".aarch64.elf"):
        shutil.copyfile(work / ("native-io" + suffix), output / ("native-io" + suffix))
    (output / "native-io.com").chmod(0o755)
    shutil.copyfile(helpers["loader"]["path"], output / "ape-aarch64.elf")
    report["distributed_helpers"] = [identity(output / "ape-aarch64.elf")]
    append_resource(output / "native-io.com")
    for arch, pattern in (("x86_64", "*.com.dbg.map"), ("aarch64", "*.aarch64.elf.map")):
        maps = list((work / "tmp").glob(pattern))
        if len(maps) != 1:
            raise BuildError("Expected exactly one " + arch + " link map")
        shutil.move(str(maps[0]), output / ("native-io." + arch + ".map"))
    artifact = audit(args, report, directory, tree, "native-io", output)
    artifact["build_commands"] = identity(output / "native-io.build-commands.log")
    report["artifacts"].append(artifact)
    verify_inputs(args, report, directory, source=False, stage="after")
    recheck(report["native_toolchain"], original, read_json(ROOT / "toolchains/cosmocc.lock.json"))
    report.update(status="pass", reason="Independent native I/O fat APE built and audited on Ubuntu; run evidence is separate")


def consume(args, report, directory):
    if not args.artifact or not args.artifact.is_file() or not args.build_report or not args.build_report.is_file():
        raise BuildError("Supplied artifact and producer build report are required; no compilation on validation hosts", "blocked")
    producer = read_json(args.build_report)
    if producer.get("report_kind") != "native-io" or producer.get("status") != "pass":
        raise BuildError("Invalid or nonpassing native I/O producer report")
    artifacts = [a for a in producer["artifacts"] if a["name"] == "native-io"]
    if len(artifacts) != 1:
        raise BuildError("Producer must identify exactly one native I/O artifact")
    artifact = artifacts[0]
    if sha256(args.artifact) != artifact["sha256"] or args.artifact.stat().st_size != artifact["size_bytes"]:
        raise BuildError("Artifact SHA-256/length differs from Ubuntu producer; refused before execution")
    if producer.get("scope") != "build":
        raise BuildError("A host run report cannot substitute for the Ubuntu producer report")
    if producer.get("schema_version") != 1 or not producer.get("source", {}).get("sha256") or not producer.get("build", {}).get("sha256"):
        raise BuildError("Producer schema/source/build identity is missing")
    if artifact.get("audit", {}).get("status") != "pass" or [s.get("machine") for s in artifact.get("headers", {}).get("slices", [])] != [62, 183]:
        raise BuildError("Producer does not prove audited fat x86_64/AArch64 content")
    report.update(source=producer["source"], toolchain=producer["toolchain"], build=producer["build"],
                  producer_report=identity(args.build_report), artifacts=[{**artifact, "path": str(args.artifact.resolve())}])
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
            locked = [f for f in read_json(ROOT / "toolchains/cosmocc.lock.json")["files"] if f["path"] == expected_path]
            if len(locked) != 1 or sha256(loader) != locked[0]["sha256"]:
                raise BuildError("Explicit loader differs from the official Nightly identity; use a supplied helpers.json bundle for derived loaders")
            loader.chmod(loader.stat().st_mode | 0o111)
            report["loader"] = {**identity(loader), "origin": "official Nightly lock"}
    fixture = directory / "进程 空格 fixture"
    fixture.mkdir()
    executable = fixture / ("native io.exe" if os.name == "nt" else "native io.com")
    shutil.copyfile(args.artifact, executable)
    executable.chmod(0o755)
    if sha256(executable) != artifact["sha256"]:
        raise BuildError("Unicode/space executable fixture differs from supplied artifact")
    report["process_fixture"] = identity(executable)
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
    ids = [r["case_id"] for r in records]
    required = REQUIRED | ({"tty.console-handle-closed"} if report["host"]["os"] == "Windows" else set())
    report["required_cases"] = sorted(required)
    missing = required - set(ids)
    duplicate = len(ids) != len(set(ids))
    for record in records:
        if record["status"] not in ("pass", "fail", "limitation", "blocked"):
            raise BuildError("Unknown native result status")
        if record["status"] == "limitation" and (not record.get("actual") or not record.get("reason")):
            raise BuildError("An OS limitation requires measured actual values and an explicit reason")
        event = record.pop("terminal_command", executed)
        report["cases"].append({**record, "artifact": identity(args.artifact), "commands": [event],
                                "expected": EXPECTED.get(record["case_id"], {"arch": report["host"]["execution_arch"], "page_size": report["host"]["page_size"]}),
                                "reason": record.get("reason", "Measured native ABI result; raw OS return values retained"),
                                "started_at": event["started_at"], "finished_at": event["finished_at"],
                                "timeout_seconds": args.timeout, "exit_code": event["exit_code"]})
    if missing or duplicate:
        for case_id in sorted(missing):
            report["cases"].append({"case_id": case_id, "status": "not_run", "actual": None,
                                    "expected": "Required native I/O observation", "commands": [],
                                    "started_at": executed["started_at"], "finished_at": executed["finished_at"],
                                    "reason": "Probe/fixture did not return this required result"})
        raise BuildError("Native suite returned duplicate/missing cases; observed results and raw output retained")
    facts = next(r["actual"] for r in records if r["case_id"] == "facts")
    if facts["arch"] != report["host"]["execution_arch"] or facts["page_size"] != report["host"]["page_size"]:
        raise BuildError("Probe architecture/page size differs from actual host OS measurements")
    if any(c["status"] == "fail" for c in report["cases"]):
        raise BuildError("Native I/O suite failed; see measured cases and raw logs")
    if any(c["status"] == "blocked" for c in report["cases"]):
        raise BuildError("Required controlled fixture unavailable; observed API error retained", "blocked")
    if executed["exit_code"] != 0:
        raise BuildError("Native I/O suite returned unexpected nonzero exit; raw OS status retained")
    if sha256(args.artifact) != artifact["sha256"] or sha256(executable) != artifact["sha256"] or (loader and sha256(loader) != report["loader"]["sha256"]):
        raise BuildError("Artifact/helper changed during execution")
    report.update(status="pass", reason="Supplied native I/O bytes executed on measured host; complete M0 matrix remains separate")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("operation", choices=("build", "run"))
    parser.add_argument("--artifact", type=Path)
    parser.add_argument("--build-report", type=Path)
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
              "scope": "build" if args.operation == "build" else "host",
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
        if args.expected_os and args.expected_os != report["host"]["os"] or args.expected_arch and args.expected_arch != report["host"]["execution_arch"]:
            raise BuildError("Actual host OS/execution architecture differs from scheduled target")
        if args.operation == "run":
            consume(args, report, directory)
        else:
            build(args, report, directory)
    except BuildError as error:
        report.update(status=error.status, reason=str(error))
    except (OSError, ValueError, KeyError, TypeError) as error:
        report.update(status="fail", reason=str(error))
    report["finished_at"] = now()
    if not report["cases"] and report["status"] != "pass":
        report["cases"].append({"case_id": "native-io." + args.operation, "status": report["status"],
                                "reason": report["reason"], "commands": report["commands"],
                                "started_at": report["started_at"], "finished_at": report["finished_at"]})
    write_json(args.report, report)
    print(json.dumps({"status": report["status"], "reason": report["reason"], "report": str(args.report)}))
    return {"pass": 0, "fail": 1, "blocked": 2}[report["status"]]


if __name__ == "__main__":
    sys.exit(main())
