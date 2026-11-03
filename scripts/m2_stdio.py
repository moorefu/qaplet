#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Ubuntu producer and strict same-byte consumer for M2 stdio OS probes."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import shutil
import sys
import uuid

import ape_helpers
from build_ape import (BuildError, audit, identity, link_portable, native_toolchain,
                       prepare_portable_helpers, publish_ape, run, verify_inputs)
from evidence import read_json
from foundation import ROOT, host_facts, now, sha256
from input_lock import component, consume_identity
from native_toolchain import recheck
from nightly_toolchain import write_json
from startup import check_ci_commit, ci_facts, measured_host, select_loader

NAME = "m2-stdio"
# A probe pass records a measured backend choice, never a product capability.
COMMON = {"facts", "pipe.bytes", "pipe.eof-broken", "pipe.alias", "pipe.backpressure",
          "pipe.cancel-wait", "pipe.close-wait", "pipe.late-completion", "pipe.cleanup",
          "stdin.pipe", "stdin.file", "stdin.invalid", "stdin.restore", "stdout.pipe", "stderr.pipe"}
TTY = {"stdin.terminal", "terminal.alias", "terminal.restore", "terminal.cleanup"}
EXPECTED = {
    "pipe.bytes": {"hex": "00017f80ff0a", "read": 6, "write": 6, "short_read": True, "poll": 1},
    "pipe.eof-broken": {"eof": True, "broken": True},
    "pipe.alias": {"alias_observed": True, "restored": True},
    "pipe.backpressure": {"would_block": True, "raw_bytes_match": True, "all_closed": True},
    "pipe.cancel-wait": {"accepted": True, "native_exited": True, "joined": True, "borrowed_open": True, "all_closed": True, "delivered": False},
    "pipe.close-wait": {"native_exited": True, "joined": True, "eof": True, "all_closed": True},
    "pipe.late-completion": {"cancel_first": True, "delivered": False, "bytes_preserved": True, "joined": True, "all_closed": True},
    "pipe.cleanup": {"all_closed": True},
    "stdin.pipe": {"kind": "pipe", "hex": "00017f80ff0a", "borrowed_open": True, "alias_open": True},
    "stdin.file": {"kind": "file", "hex": "00017f80ff0a", "shared_offset": True, "borrowed_open": True},
    "stdin.invalid": {"rejected": True},
    "stdin.restore": {"restored": True},
    "stdout.pipe": {"hex": "00017f80ff0a", "read": 6, "write": 6, "borrowed_open": True, "restored": True, "all_closed": True},
    "stderr.pipe": {"hex": "00017f80ff0a", "read": 6, "write": 6, "borrowed_open": True, "restored": True, "all_closed": True},
    "stdin.terminal": {"isatty": True, "borrowed_open": True},
    "terminal.alias": {"shared_mode": True},
    "terminal.restore": {"restored": True},
    "terminal.cleanup": {"all_closed": True},
}


def build(args, report, directory):
    if report["host"]["os"] != "Linux" or platform.freedesktop_os_release().get("ID") != "ubuntu":
        raise BuildError("Artifacts are built on Ubuntu only", "blocked")
    original = verify_inputs(args, report, directory, source=False)
    tree = native_toolchain(args, report, directory, original)
    helpers = prepare_portable_helpers(args, report, directory, tree)
    output = args.output_dir.resolve()
    if output.exists() and any(output.iterdir()):
        raise BuildError("Output directory must be empty; choose a new run")
    work = args.build_dir.resolve() / report["report_id"]
    work.mkdir(parents=True)
    (work / "tmp").mkdir()
    output.mkdir(parents=True, exist_ok=True)
    source = ROOT / "tests/native/m2_stdio.c"
    files = [identity(p) for p in [source, *(ROOT / "scripts" / n for n in
             ("m2_stdio.py", "native_io_terminal.py", "startup.py", "build_ape.py", "ape_helpers.py",
              "native_toolchain.py", "foundation.py", "evidence.py", "nightly_toolchain.py", "input_lock.py"))]]
    _, revision, _ = run(report, directory, "source-revision", ["git", "rev-parse", "HEAD"], 10)
    report["source"] = {"git_commit": revision.strip(), "files": files,
                        "sha256": hashlib.sha256(json.dumps(files, sort_keys=True).encode()).hexdigest(),
                        "note": "Actual source/recipe bytes; commit does not imply a clean tree"}
    environment = dict(os.environ, TMPDIR=str(work / "tmp"), LC_ALL="C", TZ="UTC", SOURCE_DATE_EPOCH="0",
                       BUILDLOG=str(output / (NAME + ".build-commands.log")))
    flags = ["-Os", "-D_GNU_SOURCE", "-ffile-prefix-map=" + str(work) + "=build", *args.cflag]
    report["build"] = {"flags": flags, "recipe_files": files, "source_date_epoch": 0,
                       "portable_helpers_sha256": helpers["sha256"],
                       "toolchain_lock_sha256": report["toolchain_lock"]["sha256"],
                       "native_helpers_sha256": report["native_toolchain"]["helper_set_sha256"]}
    report["build"]["sha256"] = hashlib.sha256(json.dumps(report["build"], sort_keys=True).encode()).hexdigest()
    shutil.copyfile(source, work / source.name)
    run(report, directory, "compile", [tree / "bin/cosmocc", *flags, "-c", work / source.name,
                                       "-o", work / (NAME + ".o")], args.compile_timeout, work, environment)
    link_portable(args, report, directory, tree, helpers, NAME, [work / (NAME + ".o")],
                  work / (NAME + ".com"), work, environment, libraries=("-lpthread",))
    publish_ape(work, output, NAME, helpers, ape_helpers.source_cache(args.helper_source_cache.resolve()))
    artifact = audit(args, report, directory, tree, NAME, output)
    artifact["build_commands"] = identity(output / (NAME + ".build-commands.log"))
    report["artifacts"].append(artifact)
    verify_inputs(args, report, directory, source=False, stage="after")
    recheck(report["native_toolchain"], original, component("cosmocc"))
    if [identity(Path(f["path"])) for f in files] != files:
        raise BuildError("Source/recipe changed during build; choose a stable checkout and rebuild")
    report.update(status="pass", reason="Ubuntu built/audited independent M2 stdio probe; OS execution remains separate")


def producer_inputs(args, report):
    if not args.artifact or not args.artifact.is_file() or not args.build_report or not args.build_report.is_file():
        raise BuildError("Supplied artifact and Ubuntu producer report required; validation never compiles", "blocked")
    producer = read_json(args.build_report)
    consume_identity(producer, report)
    if producer.get("schema_version") != 1 or producer.get("report_kind") != NAME or producer.get("scope") != "build" or producer.get("status") != "pass":
        raise BuildError("Successful M2 stdio Ubuntu producer required")
    artifacts = [a for a in producer["artifacts"] if a["name"] == NAME]
    if len(artifacts) != 1:
        raise BuildError("Exactly one M2 stdio artifact required")
    artifact = artifacts[0]
    if identity(args.artifact)["sha256"] != artifact["sha256"] or args.artifact.stat().st_size != artifact["size_bytes"]:
        raise BuildError("Artifact SHA-256/length differs from producer; refused before execution")
    for key in ("source", "build"):
        digest = producer.get(key, {}).get("sha256", "")
        if len(digest) != 64 or set(digest) - set("0123456789abcdef"):
            raise BuildError("Missing producer " + key + " identity")
    if artifact.get("audit", {}).get("status") != "pass" or [s["machine"] for s in artifact["headers"]["slices"]] != [62, 183]:
        raise BuildError("Audited fat x86_64/AArch64 APE required")
    report.update(source=producer["source"], build=producer["build"], toolchain=producer["toolchain"],
                  producer_report=identity(args.build_report), artifacts=[{**artifact, "path": str(args.artifact.resolve())}])
    if args.operation == "run":
        check_ci_commit(producer, report)
    return producer, artifact


def collect(report, records, event, required, execution):
    ids = [r["case_id"] for r in records]
    for item in records:
        actual = item.get("actual", {})
        valid = isinstance(actual, dict)
        if item["case_id"] == "facts":
            valid = valid and actual.get("probe") == NAME and actual.get("arch") == execution["execution_arch"] and actual.get("page_size") == execution["page_size"] and actual.get("os") == execution["os"]
        else:
            valid = valid and item["case_id"] in EXPECTED and all(actual.get(k) == v for k, v in EXPECTED.get(item["case_id"], {}).items())
            if valid and item["case_id"] == "pipe.bytes":
                valid = bool(actual.get("revents", 0) & (0x300 if execution["os"] == "Windows" else 1))
            if valid and item["case_id"] == "pipe.backpressure":
                partial = actual.get("partial_write") is True and 0 < actual.get("write", 0) < actual.get("requested", 0)
                # A measured Windows outcome: TRUE with zero bytes after a
                # partial drain. This proves retention/cleanup, not short-write
                # capability. Future product writers must advance in chunks.
                no_progress = execution["os"] == "Windows" and all(actual.get(k) == v for k, v in {
                    "route": "Win32 NOWAIT byte pipe", "partial_write": False, "write": 0,
                    "write_success": True, "os_error": 0, "full_error": 0, "eof_error": 109,
                    "drained": 4096, "requested": 16384,
                    "capability_limit": "nowait-large-write-zero-progress; retain bytes and retry smaller chunks"}.items())
                valid = partial or no_progress
            if valid and item["case_id"] == "pipe.cancel-wait" and execution["os"] == "Windows":
                valid = actual.get("pending_observed") == 1 and actual.get("os_error") == 995 and actual.get("syscall_result") == -1
        status = item.get("status")
        if status != "pass":
            valid = False
        report["cases"].append({**item, "status": status if valid else "fail", "commands": [event],
                                "expected": EXPECTED.get(item["case_id"], execution),
                                "started_at": event["started_at"], "finished_at": event["finished_at"]})
    for missing in sorted(required - set(ids)):
        report["cases"].append({"case_id": missing, "status": "not_run", "reason": "Required OS observation missing", "commands": [event]})
    if set(ids) != required or len(ids) != len(set(ids)):
        raise BuildError("Missing/duplicate/unexpected probe cases; raw observations retained")
    if event["timed_out"] or event["exit_code"] != 0 or any(c["status"] != "pass" for c in report["cases"]):
        raise BuildError("OS probe failed or exceeded supervisor deadline; cancellation/cleanup is not proven")


def consume(args, report, directory):
    _, artifact = producer_inputs(args, report)
    measured_host(args, report, directory)
    if args.loader:
        suffix = "elf" if report["host"]["os"] == "Linux" else "macho"
        name = "bin/ape-" + report["host"]["execution_arch"] + "." + suffix
        entries = [f for f in component("cosmocc")["files"] if f["path"] == name]
        if len(entries) != 1 or sha256(args.loader) != entries[0]["sha256"]:
            raise BuildError("Explicit native loader differs from locked official helper")
        args.loader.chmod(args.loader.stat().st_mode | 0o111)
        report["loader"] = identity(args.loader)
        loader = [str(args.loader)]
    else:
        loader = select_loader(args, report)
    executable = directory / (NAME + (".exe" if os.name == "nt" else ".com"))
    shutil.copyfile(args.artifact, executable)
    executable.chmod(executable.stat().st_mode | 0o111)
    event, output, errors = run(report, directory, "suite", [*loader, executable, "--" + args.probe_mode], args.timeout, expected=None)
    records = [json.loads(line) for line in output.splitlines() if line.strip()]
    required = COMMON | (set() if os.name == "nt" else TTY)
    report["required_cases"] = sorted(COMMON | TTY)
    collect(report, records, event, required, report["host"])
    if errors:
        raise BuildError("Unexpected native stderr; raw log retained")
    if os.name == "nt":
        from native_io_terminal import conpty
        results = directory / "terminal.ndjson"
        event, output = conpty([str(executable), "--tty", "--result", str(results)], directory, args.timeout)
        report["commands"].append(event)
        if results.is_file():
            output = results.read_text(encoding="utf-8")
        records = [json.loads(line) for line in output.splitlines() if line.strip().startswith('{"case_id":')]
        collect(report, records, event, TTY, report["host"])
        if event["error"] or not all(event["cleanup"].values()):
            raise BuildError("Private ConPTY cleanup failed; raw fixture facts retained")
    if sha256(executable) != artifact["sha256"] or sha256(args.artifact) != artifact["sha256"]:
        raise BuildError("Supplied probe changed during execution")
    if loader and sha256(Path(loader[0])) != report["loader"]["sha256"]:
        raise BuildError("Supplied loader changed during execution")
    report["execution"] = report["host"]
    report["capability_limits"] = [{"case_id": c["case_id"], "observation": c["actual"]["capability_limit"]}
                                  for c in report["cases"] if c.get("actual", {}).get("capability_limit")]
    report.update(status="pass", reason="Same-byte OS stdio probes consumed; limitations retained; M2 product acceptance remains separate")


def guest_manifest(args, report):
    producer, artifact = producer_inputs(args, report)
    if not args.manifest or not args.loader or not args.loader.is_file():
        raise BuildError("Guest manifest and supplied ARM64 loader required", "blocked")
    loader = identity(args.loader)
    expected = producer.get("portable_helpers", {}).get("loader", {}).get("sha256")
    if loader["sha256"] != expected:
        raise BuildError("Guest loader differs from supplied portable helper")
    arm = next(s for s in artifact["headers"]["slices"] if s["machine"] == 183)
    if not all(s["alignment"] >= args.page_size and (s["virtual_address"] - s["offset"]) % args.page_size == 0 for s in arm["load_segments"]):
        raise BuildError("ARM64 LOAD layout does not prove requested guest page size")
    payload = {"schema_version": 1, "page_size": args.page_size,
               "files": [{**loader, "guest_path": "/payload/m2-stdio-loader.elf"},
                         {**identity(args.artifact), "guest_path": "/payload/m2-stdio.com"}],
               "commands": [{"case_id": NAME + ".suite", "argv": ["/payload/m2-stdio-loader.elf", "/payload/m2-stdio.com", "--suite"],
                             "expected_exit_code": 0, "expected_stderr": ""}]}
    write_json(args.manifest, payload)
    report.update(status="pass", reason="Guest payload exported; execution not performed", manifest=identity(args.manifest))


def import_guest(args, report):
    producer, artifact = producer_inputs(args, report)
    if not args.guest_report or not args.guest_report.is_file():
        raise BuildError("Full-system guest transport report required", "blocked")
    guest = read_json(args.guest_report)
    report["guest_report"] = identity(args.guest_report)
    check_ci_commit(producer, guest)
    if guest.get("schema_version") != 1 or guest.get("report_kind") != "arm64-guest" or guest.get("status") != "pass":
        raise BuildError("Passing arm64-guest transport required")
    facts, qemu, cleanup = guest["guest"], guest["qemu"], guest["cleanup"]
    if facts.get("os") != "Linux" or facts.get("architecture") != "aarch64" or facts.get("execution_mode") != "emulated-system" or facts.get("page_size") not in (4096, 65536):
        raise BuildError("Actual full-system ARM64 kernel/page facts required")
    if facts["page_size"] != facts.get("page_size_alias") or facts.get("smaps_kib") != facts["page_size"] // 1024 or not facts.get("kernel"):
        raise BuildError("Guest page/kernel measurements conflict")
    if args.expected_os and args.expected_os != "Linux" or args.expected_arch and args.expected_arch != "aarch64":
        raise BuildError("Guest identity differs from requested target")
    transports = [c for c in guest["commands"] if "-kernel" in c["argv"] and "-initrd" in c["argv"]]
    if len(transports) != 1 or "qemu-system-aarch64" not in Path(transports[0]["argv"][0]).name or transports[0]["exit_code"] != 0 or transports[0]["timed_out"] or cleanup.get("timed_out") or not cleanup.get("qemu_exited"):
        raise BuildError("Guest transport/cleanup failed")
    if qemu.get("guest_arch") != "aarch64" or qemu.get("acceleration") != "tcg,thread=single" or not all(qemu.get(k) for k in ("machine", "cpu", "version")):
        raise BuildError("Fixed QEMU/TCG identity required")
    for key in ("kernel", "config", "rootfs", "executable", "serial_log"):
        digest = qemu[key]["sha256"]
        if len(digest) != 64 or set(digest) - set("0123456789abcdef"):
            raise BuildError("Missing QEMU input identity: " + key)
    before = {f["guest_path"]: f["sha256"] for f in guest["files"]}
    if len(before) != len(guest["files"]) or before != facts["file_sha256"] or before != facts["file_sha256_after"]:
        raise BuildError("Guest payload before/after identities conflict")
    if before.get("/payload/m2-stdio.com") != artifact["sha256"] or before.get("/payload/m2-stdio-loader.elf") != producer["portable_helpers"]["loader"]["sha256"]:
        raise BuildError("Guest probe/loader differs from Ubuntu supply")
    suites = [c for c in guest["cases"] if c["argv"] == ["/payload/m2-stdio-loader.elf", "/payload/m2-stdio.com", "--suite"]]
    if len(suites) != 1 or suites[0]["status"] != "pass":
        raise BuildError("Exactly one successful actual stdio suite required")
    suite = suites[0]
    for key in ("stdout", "stderr"):
        entry = suite[key + "_log"]
        path = args.guest_report.parent / "evidence" / guest["report_id"] / Path(entry["path"]).name
        if not path.is_file() or sha256(path) != entry["sha256"] or path.stat().st_size != entry["size_bytes"] or path.read_text(encoding="utf-8") != suite["actual"][key]:
            raise BuildError("Missing or changed guest raw " + key)
    event = {"argv": suite["argv"], "exit_code": suite["exit_code"], "timed_out": suite["actual"].get("timed_out", False),
             "started_at": suite["started_at"], "finished_at": suite["finished_at"]}
    execution = {"os": "Linux", "execution_arch": "aarch64", "page_size": facts["page_size"],
                 "kernel": facts["kernel"], "execution_mode": "emulated-system"}
    collect(report, [json.loads(line) for line in suite["actual"]["stdout"].splitlines() if line.strip()], event, COMMON | TTY, execution)
    if suite["actual"]["exit_code"] != 0 or suite["actual"]["stderr"]:
        raise BuildError("Guest suite returned an error")
    report.update(status="pass", reason="Same-byte full-system ARM64 OS probes consumed; no native hardware claim",
                  guest_report=identity(args.guest_report), guest=facts, qemu=qemu, transport_cleanup=cleanup,
                  execution=execution, required_cases=sorted(COMMON | TTY))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("operation", choices=("build", "run", "guest-manifest", "import-guest"))
    for option in ("artifact", "build-report", "guest-report", "manifest", "loader", "loader-dir", "loader-manifest"):
        parser.add_argument("--" + option, type=Path)
    parser.add_argument("--page-size", type=int, choices=(4096, 65536), default=65536)
    parser.add_argument("--toolchain-cache", type=Path, default=ROOT / ".cache/toolchains/cosmocc")
    parser.add_argument("--helper-source-cache", type=Path, default=ROOT / ".cache/sources/ape-loader")
    parser.add_argument("--build-dir", type=Path, default=ROOT / ".cache/build")
    parser.add_argument("--output-dir", type=Path, default=ROOT / "out/m2-stdio/artifacts")
    parser.add_argument("--timeout", type=int, default=30)
    parser.add_argument("--compile-timeout", type=int, default=300)
    parser.add_argument("--cflag", action="append", default=[])
    parser.add_argument("--expected-os", choices=("Linux", "Windows", "Darwin"))
    parser.add_argument("--expected-arch", choices=("x86_64", "aarch64"))
    parser.add_argument("--runner-label")
    parser.add_argument("--probe-mode", choices=("suite", "hang"), default="suite",
                        help="hang is a controlled supervisor-timeout negative fixture")
    parser.add_argument("--report", type=Path, required=True)
    args = parser.parse_args()
    if not 1 <= args.timeout <= 300 or not 1 <= args.compile_timeout <= 900:
        parser.error("Timeout outside allowed bounds")
    directory = args.report.resolve().parent / "evidence" / str(uuid.uuid4())
    directory.mkdir(parents=True)
    report = {"schema_version": 1, "report_kind": NAME, "report_id": directory.name,
              "scope": {"build": "build", "run": "host", "guest-manifest": "manifest", "import-guest": "guest"}[args.operation],
              "started_at": now(), "status": "not_run", "m2_product_status": "not_run", "reason": "",
              "host": host_facts(), "ci": ci_facts(), "source": None, "build": None, "toolchain": None,
              "commands": [], "cases": [], "artifacts": [], "input_reports": {}, "evidence_directory": str(directory),
              "runner": [identity(ROOT / "scripts" / f) for f in ("m2_stdio.py", "native_io_terminal.py", "startup.py")]}
    try:
        {"build": build, "run": consume, "guest-manifest": lambda a, r, d: guest_manifest(a, r),
         "import-guest": lambda a, r, d: import_guest(a, r)}[args.operation](args, report, directory)
    except BuildError as error:
        report.update(status=error.status, reason=str(error))
    except (OSError, ValueError, KeyError, TypeError, AttributeError) as error:
        report.update(status="fail", reason=str(error))
    report["finished_at"] = now()
    write_json(args.report, report)
    print(json.dumps({"status": report["status"], "reason": report["reason"], "report": str(args.report)}))
    return {"pass": 0, "fail": 1, "blocked": 2}[report["status"]]


if __name__ == "__main__":
    sys.exit(main())
