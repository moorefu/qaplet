#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Offline, locked, RAM-only ARM64 full-system TCG guest transport."""
import argparse
import base64
import gzip
import hashlib
import json
import lzma
import os
from pathlib import Path, PurePosixPath
import platform
import re
import shlex
import shutil
import stat
import struct
import sys
import time
import uuid

from build_ape import BuildError, identity, run
from evidence import read_json
from foundation import ROOT, host_facts, now, sha256
from nightly_toolchain import write_json
from startup import ci_facts

LOCK = ROOT / "guests/arm64.lock.json"


def checked(path, expected):
    if not path.is_file():
        raise BuildError("Missing locked guest input: " + str(path), "blocked")
    if path.stat().st_size != expected["size_bytes"] or sha256(path) != expected["sha256"]:
        raise BuildError("Guest input SHA-256/length mismatch: " + str(path))
    return identity(path)


def verify(args, report, directory):
    lock = read_json(LOCK)
    profile = lock["profiles"][str(args.page_size)]
    report["guest_lock"] = identity(LOCK)
    selected = [*lock["common_packages"], *profile["packages"]]
    report["inputs"] = []
    # Check bytes before invoking any tool, writing a cache or doing network I/O.
    for name in [*lock["metadata"], *selected]:
        expected = lock["metadata"].get(name) or lock["packages"][name]
        report["inputs"].append(checked(args.cache / "inputs" / name, expected))
    if platform.system() != "Linux":
        raise BuildError("Guest input signature verification requires Ubuntu", "blocked")
    for name in ("noble-updates-InRelease",):
        run(report, directory, "signature", ["gpgv", "--keyring", args.keyring, args.cache / "inputs" / name], 30)
    release = (args.cache / "inputs/noble-updates-InRelease").read_text()
    index_name = "noble-updates-main-arm64-Packages.xz"
    meta = lock["metadata"][index_name]
    if not re.search(r"^ " + meta["sha256"] + r"\s+" + str(meta["size_bytes"]) + r" main/binary-arm64/Packages.xz$", release, re.M):
        raise BuildError("Locked Packages index is absent from verified Ubuntu InRelease")
    index = lzma.decompress((args.cache / "inputs" / index_name).read_bytes()).decode()
    paragraphs = [{line.split(": ", 1)[0]: line.split(": ", 1)[1] for line in paragraph.splitlines() if ": " in line and not line.startswith(" ")}
                  for paragraph in index.split("\n\n")]
    for name in selected:
        expected = lock["packages"][name]
        if not any(item.get("Filename") == expected["archive_path"] and item.get("SHA256") == expected["sha256"] and
                   item.get("Size") == str(expected["size_bytes"]) and item.get("Architecture") == "arm64" for item in paragraphs):
            raise BuildError("Package is absent from signed locked ARM64 index: " + name)
    report["signature_chain"] = {"signer_fingerprint": lock["signer_fingerprint"], "keyring": identity(args.keyring),
                                 "index": index_name, "status": "pass"}
    return lock, profile, selected


def fetch(args, report, directory):
    lock = read_json(LOCK)
    profile = lock["profiles"][str(args.page_size)]
    (args.cache / "inputs").mkdir(parents=True, exist_ok=True)
    for name in [*lock["metadata"], *lock["common_packages"], *profile["packages"]]:
        item = lock["metadata"].get(name) or lock["packages"][name]
        destination = args.cache / "inputs" / name
        if destination.exists():
            checked(destination, item)
            continue
        url = item.get("url") or lock["archive_base_url"] + item["archive_path"]
        part = destination.with_suffix(destination.suffix + ".part-" + report["report_id"])
        try:
            deadline = time.monotonic() + 190
            for attempt in range(1, 4):
                remaining = deadline - time.monotonic()
                if remaining <= 1:
                    raise BuildError("Locked guest download deadline exhausted: " + name)
                part.unlink(missing_ok=True)
                record, status, _ = run(report, directory, "fetch-" + name + "-attempt-" + str(attempt),
                    ["curl", "--fail", "--show-error", "--location", "--proto", "=https", "--tlsv1.2",
                     "--connect-timeout", "20", "--max-time", str(max(1, min(180, int(remaining - 1)))),
                     "--write-out", "%{http_code}", "--output", part, url], remaining, expected=None)
                if record["exit_code"] == 0 and not record["timed_out"]:
                    checked(part, item)
                    part.replace(destination)
                    break
                if record["timed_out"] or record["exit_code"] != 22 or status.strip() not in (
                        "408", "429", "500", "502", "503", "504") or attempt == 3:
                    raise BuildError("Locked guest download failed after " + str(attempt) + " attempt(s): " + name + "; see raw logs")
                time.sleep(min(1, max(0, deadline - time.monotonic())))
        finally:
            part.unlink(missing_ok=True)
    verify(args, report, directory)


def prepare(args, report, directory, lock, profile, selected):
    # Assembling an initramfs is a build: honour the Ubuntu-only build contract.
    if platform.system() != "Linux" or platform.freedesktop_os_release().get("ID") != "ubuntu":
        raise BuildError("Guest image assembly requires Ubuntu", "blocked")
    cache = args.cache / "unpacked" / profile["id"]
    for name in selected:
        target = cache / name.removesuffix(".deb")
        if not target.exists():
            temp = cache / (name + ".partial-" + report["report_id"])
            temp.mkdir(parents=True)
            run(report, directory, "unpack-" + name, ["dpkg-deb", "-x", args.cache / "inputs" / name, temp], 60)
            temp.rename(target)
    def source(package, relative):
        return cache / package.removesuffix(".deb") / relative
    image_source = source(profile["packages"][0], "boot/vmlinuz-" + profile["kernel_release"])
    config = source(profile["packages"][1], "usr/lib/linux/" + profile["kernel_release"] + "/config")
    configuration = config.read_text()
    if "CONFIG_ARM64_" + ("64K" if args.page_size == 65536 else "4K") + "_PAGES=y\n" not in configuration:
        raise BuildError("Kernel config does not select requested ARM64 page size")
    if "config" in profile:
        checked(config, profile["config"])
    compressed = image_source.read_bytes()
    content = gzip.decompress(compressed) if compressed[:2] == b"\x1f\x8b" else compressed
    code = (struct.unpack_from("<Q", content, 24)[0] >> 1) & 3
    if content[56:60] != b"ARM\x64" or code != {4096: 1, 65536: 3}[args.page_size]:
        raise BuildError("Kernel Image header architecture/page flag mismatch")
    image = cache / "Image"
    if "image" in profile and (len(content) != profile["image"]["size_bytes"] or hashlib.sha256(content).hexdigest() != profile["image"]["sha256"]):
        raise BuildError("Derived kernel Image identity differs from lock")
    if not image.exists():
        image.write_bytes(content)
    if image.read_bytes() != content:
        raise BuildError("Cached Image differs from verified Debian kernel")
    files = {}
    for guest_path, item in lock["rootfs_files"].items():
        path = source(item["package"], item["path"])
        checked(path, item)
        files[guest_path] = (stat.S_IFREG | 0o755, path.read_bytes())
    report["qemu"] = {"host_arch": report["host"]["execution_arch"], "guest_arch": "aarch64", "acceleration": "tcg,thread=single",
        "machine": lock["qemu"]["machine"], "cpu": lock["qemu"]["cpu"], "smp": 1, "memory_mib": 512,
        "kernel": identity(image), "compressed_kernel": identity(image_source), "config": identity(config), "image_page_code": code}
    return image, files


def manifest(args, path=None):
    data = read_json(path or args.manifest)
    if data.get("schema_version") != 1 or data.get("page_size") != args.page_size:
        raise BuildError("Manifest version/page size mismatch")
    if not isinstance(data.get("files"), list) or not data["files"] or not isinstance(data.get("commands"), list) or not data["commands"]:
        raise BuildError("Manifest requires nonempty files and commands")
    paths = set()
    for item in data["files"]:
        target = PurePosixPath(item["guest_path"])
        if not target.is_absolute() or target.parts[:2] != ("/", "payload") or ".." in target.parts or str(target) in paths or any(char in item["guest_path"] for char in "\\\r\n\0"):
            raise BuildError("Payload paths must be unique safe absolute /payload paths")
        paths.add(str(target))
        path = Path(item["path"]).resolve()
        if not path.is_file():
            raise BuildError("Missing payload: " + str(path), "blocked")
        if not re.fullmatch(r"[0-9a-f]{64}", item["sha256"]) or sha256(path) != item["sha256"]:
            raise BuildError("Payload SHA-256 mismatch: " + str(path))
        item.update(path=str(path), size_bytes=path.stat().st_size)
    cases = set()
    for item in data["commands"]:
        case = item["case_id"]
        if not re.fullmatch(r"[A-Za-z0-9_.-]+", case) or case in cases:
            raise BuildError("Command case IDs must be unique safe identifiers")
        cases.add(case)
        argv = item["argv"]
        if not isinstance(argv, list) or not argv or any(not isinstance(arg, str) or "\x00" in arg for arg in argv) or argv[0] not in paths:
            raise BuildError("Command requires string argv and a supplied executable")
        if type(item.get("expected_exit_code")) is not int or not 0 <= item["expected_exit_code"] <= 255:
            raise BuildError("Command expected_exit_code must be 0..255")
        for key in ("expected_stdout", "expected_stderr", "stderr_contains"):
            if key in item and not isinstance(item[key], str):
                raise BuildError("Optional output assertions must be strings")
    return data


def merge_manifests(args, report):
    output = args.manifest.resolve()
    files, commands, sources = {}, {}, []
    for path in args.input_manifest:
        if path.resolve() == output:
            raise BuildError("Combined manifest may not overwrite an input manifest")
        data = manifest(args, path)
        sources.append(identity(path))
        for item in data["files"]:
            key = item["guest_path"]
            if key in files and (files[key]["sha256"], files[key]["size_bytes"]) != (item["sha256"], item["size_bytes"]):
                raise BuildError("Conflicting guest payload bytes: " + key)
            files.setdefault(key, item)
        for item in data["commands"]:
            if item["case_id"] in commands:
                raise BuildError("Duplicate guest command case ID: " + item["case_id"])
            commands[item["case_id"]] = item
    combined = {"schema_version": 1, "page_size": args.page_size, "files": list(files.values()),
                "commands": list(commands.values()), "input_manifests": sources}
    write_json(output, combined)
    report.update(manifest=identity(output), input_manifests=sources,
                  payload_count=len(files), command_count=len(commands))


def cpio(files):
    archive = bytearray()
    inode = 0
    def add(name, mode, payload=b"", major=0, minor=0):
        nonlocal inode
        inode += 1
        name = name.encode() + b"\0"
        values = [inode, mode, 0, 0, 1, 0, len(payload), 0, 0, major, minor, len(name), 0]
        archive.extend(b"070701" + "".join(f"{v:08x}" for v in values).encode() + name)
        archive.extend(b"\0" * (-len(archive) % 4))
        archive.extend(payload)
        archive.extend(b"\0" * (-len(archive) % 4))
    directories = {"bin", "sbin", "usr/bin", "lib/aarch64-linux-gnu", "proc", "sys", "dev", "dev/pts", "tmp", "etc", "payload"}
    for name in files:
        directories.update(str(parent) for parent in PurePosixPath(name).parents if str(parent) != ".")
    for name in sorted(directories):
        add(name, stat.S_IFDIR | 0o755)
    for name, (mode, content) in sorted(files.items()):
        add(name, mode, content)
    add("bin/sh", stat.S_IFLNK | 0o777, b"busybox")
    for applet in ("dd", "gzip", "uname", "mkdir", "chmod", "rm", "cat", "tail", "od", "cp", "mv", "sed"):
        add("bin/" + applet, stat.S_IFLNK | 0o777, b"busybox")
    add("dev/ptmx", stat.S_IFLNK | 0o777, b"pts/ptmx")
    add("dev/console", stat.S_IFCHR | 0o600, major=5, minor=1)
    add("dev/null", stat.S_IFCHR | 0o666, major=1, minor=3)
    add("TRAILER!!!", 0)
    archive.extend(b"\0" * (-len(archive) % 512))
    return gzip.compress(bytes(archive), mtime=0)


def init_script(data, token, timeout):
    lines = ["#!/bin/sh", "export PATH=/bin:/usr/bin:/sbin", "export TMPDIR=/tmp", "export HOME=/tmp", "export LC_ALL=C", "export TZ=UTC",
             "/bin/busybox mount -t proc proc /proc", "/bin/busybox mount -t sysfs sysfs /sys", "/bin/busybox mount -t devtmpfs devtmpfs /dev",
             "/bin/busybox mkdir -p /dev/pts", "/bin/busybox mount -t devpts -o newinstance,ptmxmode=0666,mode=0620 devpts /dev/pts",
             "/bin/busybox ln -sf pts/ptmx /dev/ptmx", "/bin/busybox ip link set lo up",
             "/bin/busybox date -u -s " + shlex.quote(now()[:19].replace("T", " ")) + " >/dev/null"]
    def emit(kind, expression):
        lines.append("printf '%s\\n' " + shlex.quote(token + " " + kind + " ") + expression)
    for key, command in (("os", "/bin/busybox uname -s"), ("architecture", "/bin/busybox uname -m"),
                         ("kernel", "/bin/busybox uname -r"), ("page_size", "/usr/bin/getconf PAGESIZE"),
                         ("page_size_alias", "/usr/bin/getconf PAGE_SIZE"),
                         ("smaps_kib", "/bin/busybox awk '/KernelPageSize/ {print $2; exit}' /proc/self/smaps")):
        emit("FACT " + key, '"$(' + command + ')"')
    for item in data["files"]:
        emit("HASH " + item["guest_path"], '"$(/bin/busybox sha256sum ' + shlex.quote(item["guest_path"]) + " | /bin/busybox cut -d ' ' -f 1)\"")
    lines += ['test "$(/bin/busybox uname -m)" = aarch64 || /bin/busybox poweroff -f',
              'test "$(/usr/bin/getconf PAGESIZE)" = ' + str(data["page_size"]) + ' || /bin/busybox poweroff -f']
    for index, item in enumerate(data["commands"]):
        prefix = "CASE " + item["case_id"]
        # Every shell/header control must extract its own embedded loader;
        # a previous successful payload may not mask another payload's bytes.
        temporary = "/tmp/command-" + item["case_id"]
        lines += ["/bin/busybox mkdir -p " + shlex.quote(temporary), "export TMPDIR=" + shlex.quote(temporary),
                  "export HOME=" + shlex.quote(temporary)]
        emit(prefix + " CWD", '"$(/bin/busybox pwd)"')
        emit(prefix + " START", '"$(/bin/busybox date -u +%Y-%m-%dT%H:%M:%SZ)"')
        lines.append("/bin/busybox timeout -s KILL " + str(timeout) + " " + shlex.join(item["argv"]) + " >/tmp/stdout 2>/tmp/stderr")
        lines.append("rc=$?")
        emit(prefix + " EXIT", '"$rc"')
        emit(prefix + " STDOUT", '"$(/bin/busybox base64 /tmp/stdout | /bin/busybox tr -d \'\\n\')"')
        emit(prefix + " STDERR", '"$(/bin/busybox base64 /tmp/stderr | /bin/busybox tr -d \'\\n\')"')
        emit(prefix + " END", '"$(/bin/busybox date -u +%Y-%m-%dT%H:%M:%SZ)"')
    for item in data["files"]:
        emit("HASH_AFTER " + item["guest_path"], '"$(/bin/busybox sha256sum ' + shlex.quote(item["guest_path"]) + " | /bin/busybox cut -d ' ' -f 1)\"")
    emit("DONE", "")
    lines.append("/bin/busybox poweroff -f")
    return ("\n".join(lines) + "\n").encode()


def execute(args, report, directory):
    data = manifest(args)  # Reject changed payloads before touching guest inputs.
    report["manifest"] = identity(args.manifest)
    report["files"] = data["files"]
    lock, profile, selected = verify(args, report, directory)
    image, files = prepare(args, report, directory, lock, profile, selected)
    token = "QAPLET_" + report["report_id"].replace("-", "")
    for item in data["files"]:
        files[item["guest_path"].lstrip("/")] = (stat.S_IFREG | 0o755, Path(item["path"]).read_bytes())
    files["init"] = (stat.S_IFREG | 0o755, init_script(data, token, args.command_timeout))
    work = args.cache / "build" / report["report_id"]
    work.mkdir(parents=True)
    initrd = work / "initramfs.cpio.gz"
    initrd.write_bytes(cpio(files))
    (directory / "init").write_bytes(files["init"][1])
    report["qemu"]["rootfs"] = identity(initrd)
    report["qemu"]["init"] = identity(directory / "init")
    if not shutil.which(args.qemu):
        raise BuildError("Full-system qemu-system-aarch64 executable is missing", "blocked")
    _, version, _ = run(report, directory, "qemu-version", [args.qemu, "--version"], 10)
    found = re.search(r"QEMU emulator version (\d+\.\d+\.\d+)", version)
    if not found or found[1] != lock["qemu"]["version"]:
        raise BuildError("QEMU version differs from locked " + lock["qemu"]["version"], "blocked")
    report["qemu"]["version"] = version.strip()
    executable = shutil.which(args.qemu)
    report["qemu"]["executable"] = identity(Path(executable))
    argv = [args.qemu, "-machine", lock["qemu"]["machine"], "-cpu", lock["qemu"]["cpu"],
            "-accel", "tcg,thread=single", "-smp", "1", "-m", "512M", "-display", "none", "-serial", "stdio",
            "-monitor", "none", "-nic", "none", "-no-reboot", "-kernel", image, "-initrd", initrd,
            "-append", "console=ttyAMA0 rdinit=/init panic=-1"]
    executed, serial, errors = run(report, directory, "qemu-serial", argv, args.timeout, expected=None)
    report["cleanup"] = {"qemu_exited": executed["exit_code"] is not None, "timed_out": executed["timed_out"],
                         "process_group_cleanup": "foundation.command terminates the QEMU process group on timeout",
                         "ram_only": True, "disks": [], "network": "none", "host_global_changes": False}
    report["qemu"]["serial_log"] = identity(Path(executed["stdout_log"]))
    report["qemu"]["stderr_log"] = identity(Path(executed["stderr_log"]))
    if executed["timed_out"]:
        raise BuildError("Full-system guest deadline exceeded; QEMU process group terminated and raw serial retained")
    rows = [line.rstrip("\r").split(" ", 2)[1:] for line in serial.splitlines() if line.startswith(token + " ")]
    facts, hashes, hashes_after, records = {}, {}, {}, {}
    done = 0
    for kind, value in rows:
        if kind == "FACT":
            key, content = value.split(" ", 1)
            if key in facts:
                raise BuildError("Duplicate guest fact")
            facts[key] = int(content) if key in ("page_size", "page_size_alias", "smaps_kib") else content
        elif kind in ("HASH", "HASH_AFTER"):
            key, content = value.rsplit(" ", 1)
            destination = hashes if kind == "HASH" else hashes_after
            if key in destination:
                raise BuildError("Duplicate guest payload hash")
            destination[key] = content
        elif kind == "CASE":
            case, key, content = value.split(" ", 2)
            record = records.setdefault(case, {})
            if key in record:
                raise BuildError("Duplicate guest command field")
            record[key] = content
        elif kind == "DONE":
            done += 1
    report["guest"] = {**facts, "execution_mode": "emulated-system", "file_sha256": hashes, "file_sha256_after": hashes_after,
                       "clock": "Isolated guest UTC seeded from host immediately before boot; host command brackets preserved"}
    if facts.get("os") != "Linux" or facts.get("architecture") != "aarch64" or facts.get("page_size") != args.page_size or facts.get("page_size_alias") != args.page_size or facts.get("smaps_kib") != args.page_size // 1024 or facts.get("kernel") != profile["kernel_release"]:
        raise BuildError("Guest actual OS/architecture/kernel/page size does not match locked target")
    if hashes != {item["guest_path"]: item["sha256"] for item in data["files"]}:
        raise BuildError("Guest transferred payload SHA-256 differs from original files")
    if hashes_after != hashes:
        raise BuildError("Guest payload bytes changed during execution")
    for item in data["commands"]:
        raw = records.get(item["case_id"], {})
        if set(raw) != {"CWD", "START", "END", "EXIT", "STDOUT", "STDERR"}:
            raise BuildError("Missing complete guest execution: " + item["case_id"])
        stdout = base64.b64decode(raw["STDOUT"], validate=True)
        stderr = base64.b64decode(raw["STDERR"], validate=True)
        out = directory / (item["case_id"] + ".stdout.log")
        err = directory / (item["case_id"] + ".stderr.log")
        out.write_bytes(stdout)
        err.write_bytes(stderr)
        code = int(raw["EXIT"])
        passed = code == item["expected_exit_code"] and code != 137
        text_out, text_err = stdout.decode("utf-8", "replace"), stderr.decode("utf-8", "replace")
        passed = passed and ("expected_stdout" not in item or text_out == item["expected_stdout"])
        passed = passed and ("expected_stderr" not in item or text_err == item["expected_stderr"])
        passed = passed and ("stderr_contains" not in item or item["stderr_contains"] in text_err)
        report["cases"].append({"case_id": item["case_id"], "status": "pass" if passed else "fail", "argv": item["argv"],
            "cwd": raw["CWD"],
            "started_at": raw["START"], "finished_at": raw["END"], "timeout_seconds": args.command_timeout,
            "guest_temporary_directory": "/tmp/command-" + item["case_id"],
            "expected": {key: value for key, value in item.items() if key.startswith("expected_") or key == "stderr_contains"}, "exit_code": code,
            "actual": {"stdout": text_out, "stderr": text_err, "exit_code": code},
            "stdout_log": identity(out), "stderr_log": identity(err), "serial_log": report["qemu"]["serial_log"],
            "reason": "Actual full-system guest command and returned raw bytes"})
    # A successful shell fragment cannot turn a kernel timeout/panic into green.
    if executed["exit_code"] != 0 or executed["timed_out"] or done != 1 or set(records) != {item["case_id"] for item in data["commands"]}:
        raise BuildError("Full-system guest did not complete cleanly; see serial log")
    if any(case["status"] != "pass" for case in report["cases"]):
        raise BuildError("Guest command failed expected exit contract")
    # Recheck every original byte after execution; the transport never patches APEs.
    manifest(args)
    verify(args, report, directory)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("operation", choices=("verify", "fetch", "prepare", "merge-manifests", "run"))
    parser.add_argument("--cache", type=Path, default=ROOT / ".cache/guests")
    parser.add_argument("--report", type=Path, required=True)
    parser.add_argument("--manifest", type=Path)
    parser.add_argument("--input-manifest", type=Path, action="append", default=[])
    parser.add_argument("--page-size", type=int, choices=(4096, 65536), default=65536)
    parser.add_argument("--qemu", default="qemu-system-aarch64")
    parser.add_argument("--keyring", type=Path, default=Path("/usr/share/keyrings/ubuntu-archive-keyring.gpg"))
    parser.add_argument("--timeout", type=int, default=180)
    parser.add_argument("--command-timeout", type=int, default=30)
    args = parser.parse_args()
    if args.operation == "run" and not args.manifest:
        parser.error("run requires --manifest")
    if args.operation == "merge-manifests" and (not args.manifest or not args.input_manifest):
        parser.error("merge-manifests requires --input-manifest and output --manifest")
    if args.manifest and args.manifest.resolve() == args.report.resolve():
        parser.error("manifest and report paths must be distinct")
    if not 1 <= args.timeout <= 900 or not 1 <= args.command_timeout <= 60:
        parser.error("timeout must be 1..900 and command-timeout 1..60")
    args.cache, args.report = args.cache.resolve(), args.report.resolve()
    directory = args.report.parent / "evidence" / str(uuid.uuid4())
    directory.mkdir(parents=True)
    report = {"schema_version": 1, "report_kind": "arm64-guest" if args.operation == "run" else "arm64-guest-inputs", "report_id": directory.name, "started_at": now(),
              "finished_at": None, "status": "not_run", "reason": "", "host": host_facts(), "guest": None, "qemu": None,
              "cases": [], "commands": [], "files": [], "inputs": [], "cleanup": None,
              "ci": ci_facts(),
              "source": {"runner": identity(Path(__file__)), "lock": identity(LOCK)}}
    try:
        if args.operation == "run":
            execute(args, report, directory)
        elif args.operation == "merge-manifests":
            merge_manifests(args, report)
        elif args.operation == "prepare":
            lock, profile, selected = verify(args, report, directory)
            prepare(args, report, directory, lock, profile, selected)
        elif args.operation == "fetch":
            fetch(args, report, directory)
        else:
            verify(args, report, directory)
        report.update(status="pass", reason="Locked guest " + args.operation + " completed; no physical hardware/performance claim")
    except BuildError as error:
        report.update(status=error.status, reason=str(error))
    except (OSError, ValueError, KeyError, TypeError, struct.error, lzma.LZMAError) as error:
        report.update(status="fail", reason=str(error))
    report["finished_at"] = now()
    write_json(args.report, report)
    print(json.dumps({"status": report["status"], "reason": report["reason"], "report": str(args.report)}))
    return {"pass": 0, "fail": 1, "blocked": 2}[report["status"]]


if __name__ == "__main__":
    sys.exit(main())
