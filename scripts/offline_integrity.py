#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Bounded disconnected rebuilds, exact input inventory and copy-only refusal evidence."""
import argparse
import errno
import hashlib
import json
import os
from pathlib import Path
import platform
import re
import shutil
import stat
import socket
import sys
import uuid

from build_ape import BuildError, identity
from input_lock import component, attach
from evidence import read_json
from foundation import ROOT, command, host_facts, now
from nightly_toolchain import write_json


def disconnected(args, report):
    if platform.system() != "Linux" or not hasattr(os, "getuid"):
        raise BuildError("An isolated Ubuntu network namespace is required", "blocked")
    current = os.readlink("/proc/self/ns/net")
    facts = {"parent_namespace": args.parent_netns, "child_namespace": current,
             "uid": os.getuid(), "gid": os.getgid(),
             "interfaces": sorted(name for _, name in socket.if_nameindex()),
             "ipv4_routes": Path("/proc/net/route").read_text(),
             "ipv6_routes": Path("/proc/net/ipv6_route").read_text()}
    report["network"] = facts
    # Refuse before issuing even the intentional blocked-connect probe unless the
    # namespace/identity/interface observations prove this is a dedicated child.
    if (not args.parent_netns.startswith("net:[") or current == args.parent_netns
            or facts["uid"] == 0 or facts["interfaces"] != ["lo"]):
        raise BuildError("A fresh isolated network namespace and non-root build UID are required", "blocked")
    with socket.socket() as connection:
        connection.settimeout(2)
        facts["blocked_connect"] = {"address": "203.0.113.1", "port": 443,
                                   "errno": connection.connect_ex(("203.0.113.1", 443))}
    if facts["blocked_connect"]["errno"] != errno.ENETUNREACH:
        raise BuildError("Isolated namespace did not refuse the literal external connect", "blocked")
    if platform.freedesktop_os_release().get("ID") != "ubuntu":
        raise BuildError("Product rebuilds require Ubuntu", "blocked")
    if facts["ipv4_routes"].strip().splitlines()[1:]:
        raise BuildError("Isolated namespace has an IPv4 route", "blocked")
    facts["constraint"] = "Dedicated unshare --net namespace, only lo, no IPv4 route, kernel ENETUNREACH; no host network configuration changed"


def execute(report, directory, label, argv, timeout):
    record, _, _ = command(list(map(str, argv)), directory, label, timeout, cwd=ROOT)
    report["commands"].append(record)
    return record


def traced(report, directory, label, argv, timeout):
    if not shutil.which("strace"):
        raise BuildError("strace is required to independently record actual build network/process calls", "blocked")
    trace = directory / (label + ".strace.log")
    record = execute(report, directory, label,
                     ["strace", "-f", "-qq", "-s", "512", "-e", "trace=execve,%network", "-o", trace, *argv], timeout)
    evidence = network_trace(trace)
    report.setdefault("trace_audits", []).append({"label": label, **evidence})
    if evidence["internet_attempts"]:
        raise BuildError("Implicit internet network attempt observed; see strace: " + label)
    return record, evidence


def network_trace(trace):
    lines = trace.read_text(errors="replace").splitlines()
    if not any("execve(" in line for line in lines):
        raise BuildError("Missing actual process invocation in strace evidence")
    return {"trace": identity(trace), "execve_count": sum("execve(" in line for line in lines),
                "internet_attempts": [line for line in lines if re.search(r"\b(?:connect|sendto|sendmsg|sendmmsg)\(", line)
                                      and re.search(r"AF_INET6?\b", line)],
                "network_syscalls": [line for line in lines if re.search(r"\b(?:socket|connect|sendto|sendmsg|sendmmsg|recvfrom|recvmsg|bind|listen|accept|getsockname|getpeername)\(", line)]}


def refusal_cases(args, report, directory):
    attach(report)
    root = args.cache_root.resolve()
    if args.cache_root.absolute() != root:
        raise BuildError("Refusal fixtures require a physical repository cache root")
    source_lock = component("quickjs")
    nightly = component("cosmocc")
    helpers = component("ape_helpers")
    source_name = source_lock["commit"] + "-" + source_lock["patch_manifest_sha256"][:16]
    source = root / "quickjs" / source_name / "source"
    archive = root / "toolchains/cosmocc" / nightly["archive"]["sha256"] / "archive.zip"
    helper_source = root / "sources/ape-loader" / helpers["commit"]
    if not source.is_dir() or not archive.is_file() or not helper_source.is_dir():
        raise BuildError("Complete locked source, helper source and original archive are required before copying refusal fixtures", "blocked")
    report["refusal_input_locks"] = [identity(ROOT / name) for name in (
        "third_party/quickjs/source.lock.json", "third_party/quickjs/patches.json", "toolchains/cosmocc.lock.json",
        "third_party/ape-helpers/source.lock.json", "third_party/ape-helpers/patches.json")]
    fixtures = root / "validation/offline" / report["report_id"] / "refusals"
    fixtures.mkdir(parents=True)
    report["refusal_fixture_root"] = str(fixtures)
    for name in ("missing-source", "changed-source", "changed-patch", "unlocked-head",
                 "missing-archive", "corrupt-archive", "missing-toolchain-files", "placeholder-digest",
                 "conflicting-provenance", "missing-zig", "missing-helper-source", "missing-helper-loader",
                 "changed-helper-loader", "changed-helper-packer"):
        case_dir = fixtures / name
        case_dir.mkdir()
        target = directory / (name + ".json")
        expectation = (1, "")
        if name in ("missing-source", "changed-source", "changed-patch", "unlocked-head"):
            metadata = case_dir / "metadata"
            shutil.copytree(ROOT / "third_party/quickjs", metadata)
            cache = case_dir / "cache"
            argv = [sys.executable, str(ROOT / "scripts/quickjs_source.py"), "verify", "--fixture-lock", "--lock", str(metadata / "source.lock.json"),
                    "--cache", str(cache), "--report", str(target)]
            if name != "missing-source":
                copied = cache / source_name / "source"
                shutil.copytree(source, copied, symlinks=True)
            if name == "missing-source":
                expectation = (2, "cache is missing")
            elif name == "changed-source":
                path = copied / "quickjs.c"
                path.chmod(0o600)
                with path.open("ab") as stream:
                    stream.write(b"\n/* refusal fixture */\n")
                expectation = (1, "content differs")
            elif name == "changed-patch":
                (metadata / "patches.json").write_bytes(b'{"schema_version":1,"patches":[]}\n')
                expectation = (1, "manifest is missing or modified")
            else:
                (copied / ".git/HEAD").write_text("refs/heads/master\n")
                expectation = (1, "commit differs")
        elif name == "missing-zig":
            argv = [sys.executable, str(ROOT / "scripts/startup_loaders.py"), "verify", "--zig-cache", str(case_dir / "absent"), "--report", str(target)]
            expectation = (2, "missing")
        elif "helper" in name:
            cache = case_dir / "cache"
            argv = [sys.executable, str(ROOT / "scripts/ape_helpers.py"), "verify", "--cache", str(cache), "--report", str(target)]
            if name == "missing-helper-source":
                expectation = (2, "source cache is missing")
            else:
                copied = cache / helpers["commit"]
                shutil.copytree(helper_source, copied, symlinks=True)
                path = copied / ("tool/build/apelink.c" if name == "changed-helper-packer" else "ape/loader.c")
                path.chmod(0o600)
                if name == "missing-helper-loader":
                    path.unlink()
                    expectation = (2, "Missing locked APE helper source")
                else:
                    with path.open("ab") as stream:
                        stream.write(b"\n/* refusal fixture */\n")
                    expectation = (1, "overlay identity differs" if name == "changed-helper-packer" else "source identity mismatch")
        else:
            lock = json.loads(json.dumps(nightly))
            cache = case_dir / "cache"
            content = cache / nightly["archive"]["sha256"]
            content.mkdir(parents=True)
            (content / "tree").mkdir()
            if name in ("corrupt-archive", "missing-toolchain-files"):
                copied_archive = content / "archive.zip"
                shutil.copyfile(archive, copied_archive)
                if name == "corrupt-archive":
                    with copied_archive.open("r+b") as stream:
                        stream.write(b"corrupt!")
            if name == "placeholder-digest":
                lock["tools"][0]["sha256"] = "0" * 64
                expectation = (1, "placeholder")
            elif name == "conflicting-provenance":
                lock["source"]["github"]["artifact_sha256"] = "b" * 64
                expectation = (1, "digest/length differs")
            elif name == "missing-archive":
                expectation = (2, "cache is missing")
            elif name == "corrupt-archive":
                expectation = (1, "SHA-256 mismatch")
            else:
                expectation = (1, "manifest mismatch")
            lock_path = case_dir / "cosmocc.lock.json"
            write_json(lock_path, lock)
            argv = [sys.executable, str(ROOT / "scripts/nightly_toolchain.py"), "verify", "--fixture-lock", "--lock", str(lock_path),
                    "--cache-dir", str(cache), "--report", str(target)]
        started = now()
        fixture_inventory = directory / (name + "-fixture-inventory.json")
        write_json(fixture_inventory, inventory(root, [case_dir]))
        record, trace = traced(report, directory, name, argv, 300)
        actual = read_json(target)
        passed = (record["exit_code"] == expectation[0] and not record["timed_out"]
                  and actual["status"] != "pass" and expectation[1].lower() in actual["reason"].lower()
                  and actual["commands"] == [] and trace["execve_count"] == 1)
        report["cases"].append({"case_id": "offline.refuse." + name, "status": "pass" if passed else "fail",
                                 "started_at": started, "finished_at": now(), "fixture_directory": str(case_dir),
                                 "expected": {"exit_code": expectation[0], "reason_contains": expectation[1],
                                              "supplier_executable_commands": 0, "implicit_network_attempts": 0},
                                 "actual": {"exit_code": record["exit_code"], "status": actual["status"], "reason": actual["reason"],
                                            "supplier_executable_commands": len(actual["commands"])},
                                 "report": identity(target), "trace": trace, "commands": [record], "timeout_seconds": 300,
                                 "fixture_inventory": identity(fixture_inventory),
                                 "reason": "Copied inputs refused before supplier executable use" if passed else "Refusal result did not match the expected corruption"})
        if not passed:
            raise BuildError("Refusal case failed its actual before-use assertions: " + name)


def launch(args, report, directory):
    if platform.system() != "Linux" or not hasattr(os, "geteuid") or os.geteuid() != 0:
        raise BuildError("Namespace setup requires Ubuntu root; child builds drop to --uid/--gid", "blocked")
    if args.uid <= 0 or args.gid <= 0:
        raise BuildError("Child build UID and GID must be non-root")
    for tool in ("unshare", "setpriv"):
        if not shutil.which(tool):
            raise BuildError("Required namespace tool is missing: " + tool, "blocked")
    # This invocation just created the UUID evidence directory. Give only that
    # controlled directory to the worker; never change repository/cache owners.
    os.chown(directory, args.uid, args.gid)
    directory.chmod(0o750)
    child_path = directory / "isolated.json"
    argv = ["unshare", "--net", "setpriv", "--reuid=" + str(args.uid), "--regid=" + str(args.gid),
            "--init-groups", sys.executable, str(Path(__file__).resolve()), "worker", "--parent-netns",
            os.readlink("/proc/self/ns/net"), "--cache-root", args.cache_root, "--phase", args.operation,
            "--report", child_path]
    argv.extend(("--build-timeout", str(args.build_timeout)))
    for path in args.input:
        argv.extend(("--input", str(path)))
    record = execute(report, directory, "namespace-child", argv, 2 * args.build_timeout + 900)
    if not child_path.is_file():
        raise BuildError("Isolated child did not produce evidence; see namespace logs", "blocked")
    child = read_json(child_path)
    parent_commands = report["commands"]
    for key, value in child.items():
        if key not in ("report_id", "started_at", "finished_at", "commands"):
            report[key] = value
    report["commands"] = parent_commands + child["commands"]
    report["child_report"] = identity(child_path)
    report["namespace_command"] = record
    if record["timed_out"] or record["exit_code"] != {"pass": 0, "fail": 1, "blocked": 2}.get(child["status"]):
        raise BuildError("Isolated child process outcome conflicts with its report")


def compare(first, second, report):
    producers = [read_json(path) for path in (first, second)]
    for producer in producers:
        if (producer.get("report_kind") != "ape-build" or producer.get("status") != "pass"
                or [a["name"] for a in producer["artifacts"]] != ["hello", "quickjs-eval"]):
            raise BuildError("Two successful complete APE producer reports are required")
    if producers[0]["build_directory"] == producers[1]["build_directory"]:
        raise BuildError("Independent build directories are required")
    for field in ("source", "toolchain_lock"):
        if producers[0][field]["sha256"] != producers[1][field]["sha256"]:
            raise BuildError("Rebuild input identity conflict: " + field)
    changed = []
    for left, right in zip(producers[0]["artifacts"], producers[1]["artifacts"]):
        for artifact in (left, right):
            observed = identity(Path(artifact["path"]))
            if any(observed[key] != artifact[key] for key in ("sha256", "size_bytes")):
                raise BuildError("Product bytes differ from their producer report: " + artifact["name"])
        for suffix in (".com", ".com.dbg", ".aarch64.elf"):
            paths = [Path(a["path"]).with_name(a["name"] + suffix) for a in (left, right)]
            contents = [path.read_bytes() for path in paths]
            difference = next((index for index, (a, b) in enumerate(zip(*contents)) if a != b), None)
            if difference is None and len(contents[0]) != len(contents[1]):
                difference = min(map(len, contents))
            item = {"name": left["name"] + suffix, "first": identity(paths[0]), "second": identity(paths[1]),
                    "status": "pass" if difference is None else "fail", "first_difference_offset": difference}
            report["comparisons"].append(item)
            if difference is not None:
                changed.append(item["name"])
    report["producer_reports"] = [identity(path) for path in (first, second)]
    report["diagnostic_files"] = "Link maps and command logs retain distinct absolute build paths; they are evidence, not delivered binary artifacts"
    if changed:
        raise BuildError("Deterministic product bytes differ: " + ", ".join(changed))


def inventory(root, selected):
    root = root.absolute()
    if root.resolve() != root:
        raise BuildError("Cache root must be physical, without outside redirection")
    entries = {}
    for base in selected:
        base = base.absolute()
        if not base.is_relative_to(root) or not base.resolve().is_relative_to(root):
            raise BuildError("Input is outside physical cache root: " + str(base))
        if not base.exists():
            raise BuildError("Required cached input is missing: " + str(base), "blocked")
        for path in [base, *sorted(base.rglob("*"))] if base.is_dir() else [base]:
            name = path.relative_to(root).as_posix()
            mode = path.lstat().st_mode
            item = {"path": name, "mode": stat.S_IMODE(mode)}
            if path.is_symlink():
                if not path.resolve(strict=True).is_relative_to(root):
                    raise BuildError("Input link points outside physical cache root: " + name)
                item.update(kind="symlink", target=os.readlink(path))
            elif path.is_dir():
                item.update(kind="directory")
            elif path.is_file():
                measured = identity(path)
                item.update(kind="file", sha256=measured["sha256"], size_bytes=measured["size_bytes"])
            else:
                raise BuildError("Unsupported cached input type: " + name)
            entries[name] = item
    files = [entries[name] for name in sorted(entries)]
    return {"root": str(root), "selected": [str(p.absolute()) for p in selected], "entries": files,
            "sha256": hashlib.sha256(json.dumps(files, sort_keys=True, separators=(",", ":")).encode()).hexdigest(),
            "file_count": sum(e["kind"] == "file" for e in files),
            "size_bytes": sum(e.get("size_bytes", 0) for e in files)}


def rebuild(args, report, directory):
    attach(report)
    root = args.cache_root.absolute()
    source_lock = component("quickjs")
    nightly = component("cosmocc")
    helpers = component("ape_helpers")
    startup = component("startup_loaders")
    selected = [root / "quickjs" / (source_lock["commit"] + "-" + source_lock["patch_manifest_sha256"][:16]),
                root / "toolchains/cosmocc" / nightly["archive"]["sha256"],
                root / "toolchains/cosmocc/official-assimilate-linux-x86_64-v1" / nightly["archive"]["sha256"],
                root / "sources/ape-loader" / helpers["commit"],
                root / "tools/zig/0.15.2" / startup["zig"]["archive"],
                root / "tools/zig/0.15.2" / startup["zig"]["tree"], *args.input]
    recipe_paths = [ROOT / path for path in (
        "toolchains/qaplet.lock.json", "scripts/input_lock.py",
        "third_party/quickjs/source.lock.json", "third_party/quickjs/patches.json",
        "toolchains/cosmocc.lock.json", "toolchains/cosmocc.acquisition.json",
        "third_party/ape-helpers/source.lock.json", "third_party/ape-helpers/patches.json",
        "third_party/ape-helpers/LICENSE", "toolchains/startup-loaders.lock.json",
        "Makefile", "scripts/offline_integrity.py", "scripts/build_ape.py", "scripts/ape_helpers.py",
        "scripts/native_toolchain.py", "scripts/quickjs_source.py", "scripts/nightly_toolchain.py",
        "scripts/startup_loaders.py", "scripts/foundation.py", "scripts/evidence.py",
        "tests/native/ape_hello.c", "tests/native/quickjs_eval.c")]
    for parent in ("third_party/quickjs", "third_party/ape-helpers"):
        recipe_paths.extend(ROOT / parent / entry["path"] for entry in read_json(ROOT / parent / "patches.json")["patches"])
    report["recipe_inputs"] = [identity(path) for path in recipe_paths]
    report["startup_helper_scope"] = "Pinned Zig archive/tree inventoried and missing Zig refused; standalone macOS ARM64 helper is not rebuilt or claimed reproducible by this Core APE comparison"
    before = inventory(root, selected)
    before_path = directory / "cache-before.json"
    write_json(before_path, before)
    report["cache_inventory"] = {"before": identity(before_path), "selected": before["selected"],
                                 "sha256": before["sha256"], "file_count": before["file_count"], "size_bytes": before["size_bytes"]}
    paths = []
    for name in ("first", "second"):
        output = directory / name
        output.mkdir()
        target = output / "build-info.json"
        base = root / "build/offline" / report["report_id"] / name
        if base.exists():
            raise BuildError("Independent build directory already exists")
        argv = [sys.executable, str(ROOT / "scripts/build_ape.py"), "build", "--source-cache", str(root / "quickjs"),
                "--toolchain-cache", str(root / "toolchains/cosmocc"), "--helper-source-cache", str(root / "sources/ape-loader"),
                "--build-dir", str(base), "--output-dir", str(output / "artifacts"), "--report", str(target)]
        started = now()
        record, trace = traced(report, directory, "rebuild-" + name, argv, args.build_timeout)
        actual = read_json(target) if target.is_file() else None
        entry = {"name": name, "report": identity(target) if target.is_file() else None, "trace": trace,
                 "status": actual["status"] if actual else "fail", "build_directory": actual.get("build_directory") if actual else None,
                 "artifacts": actual["artifacts"] if actual else [], "command": record}
        report["builds"].append(entry)
        passed = bool(actual and record["exit_code"] == 0 and not record["timed_out"] and actual["status"] == "pass")
        report["cases"].append({"case_id": "offline.rebuild." + name, "status": "pass" if passed else "fail",
                                 "started_at": started, "finished_at": now(), "expected": "Actual disconnected Ubuntu fat APE producer passes",
                                 "actual": {"exit_code": record["exit_code"], "status": entry["status"], "report": entry["report"]},
                                 "commands": [record], "timeout_seconds": args.build_timeout,
                                 "reason": actual["reason"] if actual else "No producer report"})
        if not passed:
            raise BuildError("Disconnected producer failed: " + name + "; see original producer report and logs")
        paths.append(target)
    compare(*paths, report)
    first = read_json(paths[0])
    second = read_json(paths[1])
    report["helper_comparisons"] = []
    for name in ("loader", "packer"):
        inputs = [Path(producer["portable_helpers"][name]["path"]) for producer in (first, second)]
        equal = inputs[0].read_bytes() == inputs[1].read_bytes()
        report["helper_comparisons"].append({"name": name, "first": identity(inputs[0]), "second": identity(inputs[1]),
                                             "status": "pass" if equal else "fail"})
        if not equal:
            raise BuildError("Source-built helper bytes differ: " + name)
    report.update(source=first["source"], toolchain=first["toolchain"], artifacts=first["artifacts"],
                  build={"producer_reports": [identity(path) for path in paths], "recipe_inputs": report["recipe_inputs"]})
    refusal_cases(args, report, directory)
    after = inventory(root, selected)
    after_path = directory / "cache-after.json"
    write_json(after_path, after)
    report["cache_inventory"]["after"] = identity(after_path)
    report["cache_inventory"]["unchanged"] = before["entries"] == after["entries"]
    if not report["cache_inventory"]["unchanged"]:
        raise BuildError("Accepted cached inputs changed during disconnected validation")
    report["recipe_inputs_after"] = [identity(path) for path in recipe_paths]
    if report["recipe_inputs"] != report["recipe_inputs_after"]:
        raise BuildError("Producer recipe or locked metadata changed during disconnected validation")
    report.update(status="pass", reason="Two independent disconnected real builds have identical six binary files; all copied input corruptions refused before executable use; accepted caches unchanged")


def cache_environment(args, report):
    root = args.cache_root.absolute()
    if root.resolve() != root:
        raise BuildError("Build cache must be physical, without outside redirection")
    home = root / "validation/offline" / report["report_id"] / "home"
    home.mkdir(parents=True)
    temporary = home.parent / "tmp"
    temporary.mkdir()
    overrides = {"HOME": str(home), "XDG_CACHE_HOME": str(home / ".cache"), "TMPDIR": str(temporary),
                 "PYTHONDONTWRITEBYTECODE": "1"}
    os.environ.update(overrides)
    report["cache_environment"] = overrides


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("operation", choices=("run", "preflight", "worker", "compare", "inventory", "refusals", "audit-network"))
    parser.add_argument("--parent-netns", default="")
    parser.add_argument("--cache-root", type=Path, default=ROOT / ".cache")
    parser.add_argument("--report", type=Path, default=ROOT / "out/offline-integrity.json")
    parser.add_argument("--first-build", type=Path)
    parser.add_argument("--second-build", type=Path)
    parser.add_argument("--input", type=Path, action="append", default=[])
    parser.add_argument("--trace", type=Path)
    parser.add_argument("--uid", type=int, default=1000)
    parser.add_argument("--gid", type=int, default=1000)
    parser.add_argument("--build-timeout", type=int, default=1200)
    parser.add_argument("--phase", choices=("run", "preflight", "refusals"), default="run", help=argparse.SUPPRESS)
    args = parser.parse_args()
    if not 1 <= args.build_timeout <= 3600:
        parser.error("build-timeout must be 1–3600 seconds")
    args.report = args.report.resolve()
    report = {"schema_version": 1, "report_kind": "offline-integrity", "report_id": str(uuid.uuid4()),
              "started_at": now(), "finished_at": None, "status": "not_run", "reason": "",
              "m0_core_status": "not_run", "host": host_facts(), "network": None,
              "commands": [], "builds": [], "cases": [], "comparisons": [], "implementation": identity(Path(__file__))}
    directory = args.report.parent / "evidence" / report["report_id"]
    directory.mkdir(parents=True)
    try:
        if args.operation == "compare":
            if not args.first_build or not args.second_build:
                raise BuildError("compare requires both producer reports")
            compare(args.first_build, args.second_build, report)
            report.update(status="pass", reason="All six delivered binary files are byte identical; no disconnected execution claim")
        elif args.operation == "audit-network":
            if not args.trace:
                raise BuildError("audit-network requires --trace")
            report["network_audit"] = network_trace(args.trace)
            if report["network_audit"]["internet_attempts"]:
                raise BuildError("Implicit internet network attempt observed in actual trace")
            report.update(status="pass", reason="Trace contains no implicit internet call; runtime isolation must be verified separately")
        elif args.operation == "inventory":
            if not args.input:
                raise BuildError("inventory requires explicit --input paths")
            report["inventory"] = inventory(args.cache_root, args.input)
            report.update(status="pass", reason="Exact physical cached file/type/link inventory recorded; no build claim")
        else:
            if args.operation != "worker":
                launch(args, report, directory)
            else:
                disconnected(args, report)
                if args.phase == "preflight":
                    report.update(status="pass", reason="Actual isolated unprivileged network preflight passed; no product rebuild claim")
                else:
                    cache_environment(args, report)
                    if args.phase == "refusals":
                        refusal_cases(args, report, directory)
                        report.update(status="pass", reason="Copy-only missing/corrupt input cases refused before any supplier executable; no product rebuild claim")
                    else:
                        rebuild(args, report, directory)
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
