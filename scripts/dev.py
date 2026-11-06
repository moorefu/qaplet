#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Repository execution entry point with explicit worktree/cache context."""
import argparse
import ast
import hashlib
import json
import os
from pathlib import Path
import platform
import tempfile
import uuid

from foundation import command
from workflow_support import git_read, identity, now, read_json, workspace, write_json

ROOT = Path(__file__).resolve().parents[1]


def context(repo):
    result = workspace(repo)
    distro = platform.freedesktop_os_release().get("ID") if platform.system() == "Linux" else None
    result["host"] = {"os": platform.system(), "kernel": platform.release(), "arch": platform.machine(),
                      "distribution": distro, "uid": os.getuid() if hasattr(os, "getuid") else None,
                      "gid": os.getgid() if hasattr(os, "getgid") else None,
                      "page_size": os.sysconf("SC_PAGE_SIZE") if hasattr(os, "sysconf") else None,
                      "temporary_directory": tempfile.gettempdir()}
    result["build_allowed"] = platform.system() == "Linux" and distro == "ubuntu"
    return result


def preflight(repo, receipt=None):
    generated = [name for name in git_read(repo, "ls-files", "-z").decode().split("\x00")
                 if name.startswith(("out/", ".cache/", "toolchains/evidence/"))]
    if generated:
        raise ValueError("Generated outputs are tracked: " + ", ".join(generated[:8]))
    scripts = sorted((repo / "scripts").glob("*.py"))
    for source in scripts:
        ast.parse(source.read_bytes(), filename=str(source))
    result = {"tracked_generated_files": 0, "python_files_parsed": len(scripts)}
    from text_encoding import check
    result["text_encoding"] = check(repo)
    if (repo / "toolchains/qaplet.lock.json").is_file():
        from input_lock import InputLock, audit_consumers
        result["input_lock"] = InputLock(repo / "toolchains/qaplet.lock.json").identity()
        result["input_consumers"] = audit_consumers(repo)
    workflow = repo / ".github/workflows/workflow-tools.yml"
    if (repo / "scripts/dev.py").is_file() and not workflow.is_file():
        raise ValueError("Repository workflow tooling must have its CI guard")
    if workflow.is_file():
        value = read_json(workflow)
        if set(value.get("on", {})) != {"push", "pull_request", "workflow_dispatch"}:
            raise ValueError("Tooling CI must run on push, pull_request and workflow_dispatch")
        if value.get("permissions") != {"contents": "read"}:
            raise ValueError("Tooling CI requires read-only repository permission")
        jobs = value.get("jobs", {})
        if set(jobs) != {"workflow"} or not str(jobs["workflow"].get("runs-on", "")).startswith("ubuntu-"):
            raise ValueError("Tooling checks must run on Ubuntu")
        from ci_tiers import check_scopes
        result["scope_ci"] = check_scopes(repo)
        steps = jobs["workflow"].get("steps", [])
        required = ["python scripts/dev.py preflight --report out/workflow/preflight.json",
                    "python -B -m unittest discover -s tests/contracts -p 'test_workflow*.py' -v"]
        from ci_tiers import check_scoped_job
        if check_scoped_job(value, "workflow", "workflow_tools") != required or any(step.get("continue-on-error") for step in steps):
            raise ValueError("Tooling CI must run preflight before required CLI contracts")
        if not any(step.get("if") == "${{ always() }}" and step.get("uses", "").startswith("actions/upload-artifact@")
                   and step.get("with", {}).get("if-no-files-found") == "error" for step in steps):
            raise ValueError("Tooling CI must retain failure evidence")
        result["tooling_ci"] = "pass"
    if receipt:
        from core_gate import validate_recipe_comparison
        data = read_json(receipt)
        previous = (receipt.resolve().parent / "builder-tested.py").read_bytes() if data["changed_inputs"] else None
        result["recipe_scope"] = validate_recipe_comparison(data, repository=repo, previous=previous)
        entries = data["current_recipe_inputs"]
        builder = next(entry["path"].replace("\\", "/") for entry in entries
                       if entry["path"].replace("\\", "/").endswith("/scripts/build_ape.py"))
        original = builder[:-len("/scripts/build_ape.py")]
        for entry in entries:
            name = entry["path"].replace("\\", "/")[len(original) + 1:]
            staged = git_read(repo, "show", ":" + name)
            if hashlib.sha256(staged).hexdigest() != entry["sha256"] or len(staged) != entry["size_bytes"]:
                raise ValueError("Staged recipe bytes differ from the selected inputs: " + name)
        result["staged_recipe_inputs"] = len(entries)
    return result


def planned(args, ctx, output):
    import sys
    repo, cache = Path(ctx["checkout"]), Path(ctx["cache_root"])
    python = sys.executable
    if args.operation == "check-fast":
        return [[python, "-B", str(repo / "scripts/local_check.py"), "--profile", args.profile,
                 "--output-dir", str(output), "--report", str(output / "local-check.json"),
                 *[part for path in args.reuse_report for part in ("--reuse-report", str(path.resolve()))],
                 *(["--supplier-report", str(args.supplier_report.resolve()), "--supplier-dir", str(args.supplier_dir.resolve())]
                   if args.supplier_report and args.supplier_dir else [])]]
    if args.operation == "check":
        return [["make", "check", "OUT=" + str(output), "QUICKJS_CACHE=" + str(cache / "quickjs"),
                 "TOOLCHAIN_CACHE=" + str(cache / "toolchains/cosmocc"), "APE_BUILD_DIR=" + str(cache / "build"),
                 "APE_HELPER_SOURCE_CACHE=" + str(cache / "sources/ape-loader"), "OFFLINE_CACHE_ROOT=" + str(cache)]]
    if args.operation == "build-ape":
        return [[python, "-B", str(repo / "scripts/build_ape.py"), "build",
                 "--source-cache", str(cache / "quickjs"), "--toolchain-cache", str(cache / "toolchains/cosmocc"),
                 "--helper-source-cache", str(cache / "sources/ape-loader"), "--build-dir", str(cache / "build"),
                 "--output-dir", str(output / "artifacts"), "--report", str(output / "build-info.json")]]
    if args.operation == "check-evidence":
        if not args.config:
            raise ValueError("check-evidence requires --config")
        return [[python, "-B", str(repo / "scripts/check_evidence.py"), "--config", str(args.config.resolve()),
                 "--report", str(output / "evidence-regression.json"), *(["--preflight-only"] if args.preflight_only else [])]]
    if args.operation == "offline":
        common = ["--cache-root", str(cache), "--uid", str(args.uid), "--gid", str(args.gid)]
        commands = [[python, "-B", str(repo / "scripts/offline_integrity.py"), "preflight", *common,
                     "--report", str(output / "offline-preflight.json")]]
        if not args.preflight_only:
            commands.append([python, "-B", str(repo / "scripts/offline_integrity.py"), "run", *common,
                             "--report", str(output / "offline-integrity.json")])
        return commands
    return []


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("operation", choices=("context", "preflight", "check", "check-fast", "build-ape", "build-sdk", "offline", "check-evidence"))
    parser.add_argument("--profile", choices=("all", "engine", "loop", "work", "process", "tty", "ci"), default="all")
    parser.add_argument("--supplier-report", type=Path)
    parser.add_argument("--supplier-dir", type=Path)
    parser.add_argument("--reuse-report", type=Path, action="append", default=[])
    parser.add_argument("--repo", type=Path, default=ROOT)
    parser.add_argument("--report", type=Path)
    parser.add_argument("--output-dir", type=Path)
    parser.add_argument("--config", type=Path)
    parser.add_argument("--recipe-receipt", type=Path)
    parser.add_argument("--plan", action="store_true", help="Print an execution plan; execute no build/probe commands")
    parser.add_argument("--preflight-only", action="store_true")
    parser.add_argument("--uid", type=int, default=1000)
    parser.add_argument("--gid", type=int, default=1000)
    parser.add_argument("--timeout", type=int, default=1800)
    parser.add_argument("--temp-root", type=Path, help="Child-process temporary directory; global environment stays unchanged")
    parser.add_argument("--min-temp-free-bytes", type=int, default=64 * 1024**2)
    parser.add_argument("--native-fs", action="store_true", help="Run make check in a one-use POSIX filesystem snapshot")
    parser.add_argument("--source-ref", help="Explicit committed input for frozen build-sdk")
    args = parser.parse_args()
    output = (args.output_dir or args.repo / "out/dev" / uuid.uuid4().hex).resolve()
    target = (args.report or output / "workflow.json").resolve()
    report = {"schema_version": 1, "report_kind": "workflow-command", "scope": "repository-workflow",
              "status": "fail", "started_at": now(), "reason": "", "commands": []}
    target_validated = not (args.operation == 'build-sdk' or args.native_fs)
    try:
        if not 1 <= args.timeout <= 7200:
            raise ValueError("timeout must be 1–7200 seconds")
        if args.uid <= 0 or args.gid <= 0:
            raise ValueError("Offline worker UID/GID must both be nonzero")
        if bool(args.supplier_report) != bool(args.supplier_dir):
            raise ValueError("Supplier report and directory must be supplied together")
        ctx = context(args.repo)
        report["context"] = ctx
        repo = Path(ctx["checkout"])
        special = args.operation == 'build-sdk' or args.native_fs
        if special and (not output.is_relative_to(repo / 'out') or output.exists() or
                        not target.is_relative_to(repo / 'out') or target.exists()):
            raise ValueError('Use NEW execution directory and report under repository out/')
        target_validated = True
        if args.native_fs and args.operation != 'check':
            raise ValueError('--native-fs is only available for check')
        if args.source_ref and args.operation != 'build-sdk':
            raise ValueError('--source-ref is only available for build-sdk')
        if args.operation != "context":
            report["preflight"] = preflight(repo, args.recipe_receipt)
        from dev_execution import child_environment, frozen_sdk, native_check, temporary_root
        selected_temp = (args.temp_root or Path(ctx['host']['temporary_directory'])).resolve()
        if special and selected_temp.is_relative_to(output):
            raise ValueError('Temporary root must be outside the new execution directory')
        environment = child_environment(selected_temp)
        if not args.plan and args.operation != 'context':
            report['temporary_root'] = temporary_root(selected_temp, args.min_temp_free_bytes)
        commands = planned(args, ctx, output)
        report["planned_commands"] = commands
        if special:
            if not args.plan and not ctx['build_allowed']:
                raise ValueError('Frozen builds and native checks require Ubuntu')
            if not args.plan:
                output.mkdir(parents=True, exist_ok=False)
            (frozen_sdk if args.operation == 'build-sdk' else native_check)(args, ctx, output, report, environment)
            report.update(status='planned' if args.plan else 'pass', reason='Fixed-input development execution completed' if not args.plan else 'Plan only; no execution')
        elif args.plan:
            report.update(status="planned", reason="Plan only; no build or probe commands executed")
        else:
            if args.operation in ("check", "build-ape", "offline") and not ctx["build_allowed"]:
                raise ValueError("Build/test execution requires Ubuntu; run this entry point inside the Ubuntu checkout")
            output.mkdir(parents=True, exist_ok=True)
            for index, argv in enumerate(commands):
                write_json(output / "progress.json", {"phase": args.operation, "command": index + 1,
                                                      "total": len(commands), "updated_at": now()})
                record, _, _ = command(argv, output, "command-" + str(index), args.timeout, cwd=repo, env=environment)
                report["commands"].append(record)
                if record["exit_code"] != 0 or record["timed_out"]:
                    raise ValueError("Workflow command failed; see captured command logs")
            report.update(status="pass", reason="Selected repository workflow completed")
    except (OSError, ValueError, KeyError, TypeError, SyntaxError) as error:
        report["reason"] = str(error)
    report["finished_at"] = now()
    if target_validated:
        write_json(target, report)
    print(json.dumps({"status": report["status"], "report": str(target), "reason": report["reason"][:300],
                      "cache_root": report.get("context", {}).get("cache_root")}))
    return 0 if report["status"] in ("pass", "planned") else 1


if __name__ == "__main__":
    raise SystemExit(main())
