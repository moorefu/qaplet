#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Build and verify independent ARM64 64 KiB native system probes."""
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

from build_ape import (BuildError, audit, identity, link_portable, native_toolchain,
                       prepare_portable_helpers, run, verify_inputs)
from native_toolchain import recheck
from evidence import read_json
from foundation import ROOT, host_facts, now, sha256
from nightly_toolchain import write_json

REQUIRED = {"environment", "pthread-tls", "allocation", "unaligned-munmap-rejected", "mmap-mprotect-munmap",
            "zero-map-rejected", "unaligned-map-rejected", "unaligned-file-map-rejected", "filesystem-cleanup",
            "zip-resource-pages", "zip-resource-bounds", "zip-missing-rejected"}


def build(args, report, directory):
    if platform.system() != "Linux" or platform.freedesktop_os_release().get("ID") != "ubuntu":
        raise BuildError("Probe builds require Ubuntu", "blocked")
    tree = verify_inputs(args, report, directory, source=False)
    tree = native_toolchain(args, report, directory, tree)
    helpers = prepare_portable_helpers(args, report, directory, tree)
    output = args.output_dir.resolve()
    if output.exists() and any(output.iterdir()):
        raise BuildError("Output directory must be new or empty")
    output.mkdir(parents=True, exist_ok=True)
    base = (args.build_dir or ROOT / ".cache/build").resolve()
    work = base / report["report_id"]
    (work / "tmp").mkdir(parents=True)
    source = ROOT / "tests/native/pages64k.c"
    copied = work / source.name
    shutil.copyfile(source, copied)
    env = dict(os.environ, TMPDIR=str(work / "tmp"), LC_ALL="C", TZ="UTC", SOURCE_DATE_EPOCH="0",
               BUILDLOG=str(output / "pages64k.build-commands.log"))
    flags = ["-Os", "-fwrapv", "-ffile-prefix-map=" + str(work) + "=pages64k-build"]
    report["build"] = {"source": identity(source), "flags": flags, "toolchain_lock": report["toolchain_lock"],
        "helpers": helpers["sha256"], "resource": {"path": "pages64k.bin", "size_bytes": 131089,
          "content": "byte at offset n is (29*n+7) modulo256; deflated ZIP appended after packing"},
        "recipe_files": [identity(ROOT / name) for name in ("scripts/pages64k.py", "scripts/build_ape.py", "scripts/ape_helpers.py", "scripts/arm64_guest.py")]}
    obj = work / "pages64k.o"
    run(report, directory, "pages64k-compile", [tree / "bin/cosmocc", *flags, "-c", copied, "-o", obj], args.compile_timeout, work, env)
    destination = work / "pages64k.com"
    link_portable(args, report, directory, tree, helpers, "pages64k", [obj], destination, work, env)
    resource = bytes((index * 29 + 7) % 256 for index in range(131089))
    with zipfile.ZipFile(destination, "a", compression=zipfile.ZIP_DEFLATED, compresslevel=9) as archive:
        info = zipfile.ZipInfo("pages64k.bin", date_time=(1980, 1, 1, 0, 0, 0))
        info.compress_type = zipfile.ZIP_DEFLATED
        info.external_attr = 0o100444 << 16
        archive.writestr(info, resource)
    report["build"]["resource"]["sha256"] = hashlib.sha256(resource).hexdigest()
    for suffix in (".com", ".com.dbg", ".aarch64.elf"):
        shutil.copyfile(work / ("pages64k" + suffix), output / ("pages64k" + suffix))
    (output / "pages64k.com").chmod(0o755)
    for architecture, pattern in (("x86_64", "*.com.dbg.map"), ("aarch64", "*.aarch64.elf.map")):
        maps = list((work / "tmp").glob(pattern))
        if len(maps) != 1:
            raise BuildError("Expected one CPU map: " + architecture)
        shutil.copyfile(maps[0], output / ("pages64k." + architecture + ".map"))
    shutil.copyfile(helpers["loader"]["path"], output / "ape-aarch64.elf")
    report["loader"] = identity(output / "ape-aarch64.elf")
    artifact = audit(args, report, directory, tree, "pages64k", output)
    artifact["build_commands"] = identity(output / "pages64k.build-commands.log")
    report["artifacts"] = [artifact]
    _, facts, _ = run(report, directory, "pages64k-native-facts", [tree / "bin/ape-x86_64.elf", artifact["path"], "--facts"], args.timeout, work, env)
    if json.loads(facts) != {"probe": "pages64k", "arch": report["host"]["execution_arch"], "page_size": report["host"]["page_size"]}:
        raise BuildError("Probe local facts do not match actual host")
    # This negative is an actual x86_64 OS run, not an invented wrong-arch report.
    command, text, _ = run(report, directory, "pages64k-native-wrong-environment", [tree / "bin/ape-x86_64.elf", artifact["path"], "--suite"], args.timeout, work, env, expected=None)
    expected_negative = json.loads(text)
    if command["exit_code"] != 1 or expected_negative["case_id"] != "pages64k.environment" or expected_negative["status"] != "fail":
        raise BuildError("Probe accepted wrong actual architecture/page size")
    report["negative_cases"] = [{"case_id": "wrong-native-environment", "status": "pass", "actual": expected_negative, "command": command}]
    verify_inputs(args, report, directory, source=False, stage="after")
    recheck(report["native_toolchain"], Path(report["native_toolchain"]["original_tree"]), read_json(ROOT / "toolchains/cosmocc.lock.json"))
    report["build"]["sha256"] = hashlib.sha256(json.dumps(report["build"], sort_keys=True, separators=(",", ":")).encode()).hexdigest()


def guest_manifest(args, report):
    producer = read_json(args.input_report)
    if producer["status"] != "pass" or producer["report_kind"] != "pages64k-build":
        raise BuildError("Manifest requires a passing independent probe producer")
    files = []
    for item in (producer["loader"], producer["artifacts"][0]):
        path = Path(item["path"])
        if sha256(path) != item["sha256"]:
            raise BuildError("Distributed probe/helper SHA-256 mismatch")
        files.append({"path": str(path), "guest_path": "/payload/" + path.name, "sha256": item["sha256"]})
    lock = read_json(ROOT / "guests/arm64.lock.json")
    item = lock["rootfs_files"]["bin/busybox"]
    path = args.guest_cache.resolve() / "unpacked" / lock["profiles"]["65536"]["id"] / item["package"].removesuffix(".deb") / item["path"]
    if not path.is_file() or sha256(path) != item["sha256"]:
        raise BuildError("Verified guest BusyBox cache required for shell startup control", "blocked")
    files.append({"path": str(path), "guest_path": "/payload/busybox", "sha256": item["sha256"]})
    commands = [{"case_id": "pages64k.facts", "argv": ["/payload/ape-aarch64.elf", "/payload/pages64k.com", "--facts"], "expected_exit_code": 0},
                {"case_id": "pages64k.suite", "argv": ["/payload/ape-aarch64.elf", "/payload/pages64k.com", "--suite"], "expected_exit_code": 0},
                {"case_id": "pages64k.shell-header", "argv": ["/payload/busybox", "sh", "/payload/pages64k.com", "--facts"], "expected_exit_code": 0}]
    # Manifest is the shared guest runner input itself; metadata fields retain
    # the independent producer/report identities for later gate verification.
    report.update(schema_version=1, page_size=65536, files=files, commands=commands, producer=identity(args.input_report))


def verify_guest(args, report):
    guest = read_json(args.guest_report)
    producer = read_json(args.input_report)
    if guest["status"] != "pass" or guest["report_kind"] != "arm64-guest" or producer["status"] != "pass":
        raise BuildError("Passing real guest and independent producer reports are required")
    actual = guest["guest"]
    if actual["os"] != "Linux" or actual["architecture"] != "aarch64" or actual["page_size"] != 65536 or actual["execution_mode"] != "emulated-system":
        raise BuildError("Actual Linux/AArch64/65536 full-system guest evidence required")
    if actual["file_sha256"] != actual["file_sha256_after"]:
        raise BuildError("Guest payload changed during execution")
    for item in (producer["loader"], producer["artifacts"][0]):
        if actual["file_sha256"].get("/payload/" + Path(item["path"]).name) != item["sha256"]:
            raise BuildError("Guest artifact differs from independent producer")
    cases = {case["case_id"]: case for case in guest["cases"]}
    for name in ("pages64k.facts", "pages64k.shell-header"):
        result = cases[name]
        if result["status"] != "pass" or result["actual"]["exit_code"] != 0 or result["actual"]["stderr"] or json.loads(result["actual"]["stdout"]) != {"probe": "pages64k", "arch": "aarch64", "page_size": 65536}:
            raise BuildError("Actual facts/shell-header startup assertion failed")
    suite = cases["pages64k.suite"]
    if suite["status"] != "pass" or suite["actual"]["exit_code"] != 0 or suite["actual"]["stderr"]:
        raise BuildError("Actual native deep probe did not exit cleanly")
    rows = [json.loads(line) for line in suite["actual"]["stdout"].splitlines()]
    expected = {"pages64k." + case for case in REQUIRED}
    if len(rows) != len(expected) or {row["case_id"] for row in rows} != expected or any(row["status"] != "pass" or row["actual"] != row["expected"] for row in rows):
        raise BuildError("Missing, duplicate or failing required native deep cases")
    for row in rows:
        row.update(started_at=suite["started_at"], finished_at=suite["finished_at"], timeout_seconds=suite["timeout_seconds"],
                   exit_code=suite["actual"]["exit_code"], artifact={key: producer["artifacts"][0][key] for key in ("path", "sha256", "size_bytes")},
                   reason="Actual independent C assertion within the recorded guest suite interval", argv=suite["argv"],
                   stdout_log=suite["stdout_log"], stderr_log=suite["stderr_log"])
    report.update(cases=rows, guest=guest["guest"], qemu=guest["qemu"], cleanup=guest["cleanup"],
        producer=identity(args.input_report), guest_report=identity(args.guest_report), artifact=producer["artifacts"][0],
        build=producer["build"], toolchain=producer["toolchain"], portable_helpers=producer["portable_helpers"])


def run_native(args, report, directory):
    host = report["host"]
    if host["os"] != "Linux" or host["execution_arch"] != "aarch64" or host["execution_mode"] != "native" or host["page_size"] != 65536:
        raise BuildError("Actual native Linux/AArch64/65536 host required; use full-system guest for a missing page configuration", "blocked")
    producer = read_json(args.input_report)
    if producer.get("status") != "pass" or producer.get("report_kind") != "pages64k-build":
        raise BuildError("Passing independent probe producer required")
    artifact = args.artifact.resolve()
    loader = args.loader.resolve()
    for path, item in ((artifact, producer["artifacts"][0]), (loader, producer["loader"])):
        if sha256(path) != item["sha256"] or path.stat().st_size != item["size_bytes"]:
            raise BuildError("Distributed native probe/helper identity differs from producer")
        path.chmod(path.stat().st_mode | 0o111)
    executions = []
    shell_cache = directory / "shell-cache"
    shell_cache.mkdir()
    environment = dict(os.environ, TMPDIR=str(shell_cache), HOME=str(shell_cache))
    for name, argv in (("facts", [loader, artifact, "--facts"]), ("suite", [loader, artifact, "--suite"]),
                       ("shell-header", ["/bin/sh", artifact, "--facts"])):
        executed, output, errors = run(report, directory, "native-" + name, argv, args.timeout, environment=environment, expected=None)
        if executed["exit_code"] != 0 or executed["timed_out"] or errors:
            raise BuildError("Actual native 64K probe failed: " + name)
        executions.append({"case_id": "pages64k." + name, "command": executed, "stdout": output})
        if name == "suite":
            rows = [json.loads(line) for line in output.splitlines()]
            required = {"pages64k." + case for case in REQUIRED}
            if len(rows) != len(required) or {row["case_id"] for row in rows} != required or any(row["status"] != "pass" or row["actual"] != row["expected"] for row in rows):
                raise BuildError("Missing or failing required native deep cases")
            for row in rows:
                row.update(started_at=executed["started_at"], finished_at=executed["finished_at"], timeout_seconds=args.timeout,
                           exit_code=executed["exit_code"], artifact=identity(artifact), commands=[executed],
                           reason="Actual independent C assertion within the recorded native suite interval")
            report["cases"] = rows
        elif json.loads(output) != {"probe": "pages64k", "arch": "aarch64", "page_size": 65536}:
            raise BuildError("Native C sysconf/architecture differs from measured host")
    if sha256(artifact) != producer["artifacts"][0]["sha256"] or sha256(loader) != producer["loader"]["sha256"]:
        raise BuildError("Native artifact/helper bytes changed")
    report.update(executions=executions, artifact=identity(artifact), loader=identity(loader), producer=identity(args.input_report),
                  build=producer["build"], toolchain=producer["toolchain"], portable_helpers=producer["portable_helpers"],
                  cleanup={"probe_exited": True, "filesystem_fixture": "Probe independently checks removal", "host_global_changes": False})


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("operation", choices=("build", "guest-manifest", "verify-guest", "run-native"))
    parser.add_argument("--toolchain-cache", type=Path, default=ROOT / ".cache/toolchains/cosmocc")
    parser.add_argument("--helper-source-cache", type=Path, default=ROOT / ".cache/sources/ape-loader")
    parser.add_argument("--guest-cache", type=Path, default=ROOT / ".cache/guests")
    parser.add_argument("--build-dir", type=Path)
    parser.add_argument("--output-dir", type=Path, default=ROOT / "out/pages64k/artifacts")
    parser.add_argument("--report", type=Path, required=True)
    parser.add_argument("--input-report", type=Path)
    parser.add_argument("--guest-report", type=Path)
    parser.add_argument("--artifact", type=Path)
    parser.add_argument("--loader", type=Path)
    parser.add_argument("--timeout", type=int, default=30)
    parser.add_argument("--compile-timeout", type=int, default=300)
    args = parser.parse_args()
    if args.operation != "build" and not args.input_report:
        parser.error("non-build operations require --input-report")
    if args.operation == "verify-guest" and not args.guest_report:
        parser.error("verify-guest requires --guest-report")
    if args.operation == "run-native" and (not args.artifact or not args.loader):
        parser.error("run-native requires --artifact and --loader")
    if not 1 <= args.timeout <= 300 or not 1 <= args.compile_timeout <= 900:
        parser.error("timeout must be 1..300 and compile-timeout 1..900")
    directory = args.report.resolve().parent / "evidence" / str(uuid.uuid4())
    directory.mkdir(parents=True)
    report = {"schema_version": 1, "report_kind": "pages64k-" + ("build" if args.operation == "build" else "validation"),
              "report_id": directory.name, "started_at": now(), "status": "not_run", "commands": [], "cases": [],
              "artifacts": [], "host": host_facts(), "input_reports": {}, "build": None, "toolchain": None,
              "source": {"probe": identity(ROOT / "tests/native/pages64k.c")}}
    try:
        if args.operation == "build":
            build(args, report, directory)
        elif args.operation == "guest-manifest":
            guest_manifest(args, report)
        elif args.operation == "run-native":
            run_native(args, report, directory)
        else:
            verify_guest(args, report)
        report.update(status="pass", reason="Independent 64K probe " + args.operation + " completed; hardware/performance not claimed")
    except BuildError as error:
        report.update(status=error.status, reason=str(error))
    except (OSError, ValueError, KeyError, TypeError) as error:
        report.update(status="fail", reason=str(error))
    report["finished_at"] = now()
    write_json(args.report, report)
    print(json.dumps({"status": report["status"], "reason": report["reason"], "report": str(args.report)}))
    return {"pass": 0, "fail": 1, "blocked": 2}[report["status"]]


if __name__ == "__main__":
    sys.exit(main())
