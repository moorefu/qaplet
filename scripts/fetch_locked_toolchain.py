#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Explicitly acquire the exact GitHub Nightly ZIP named by the existing lock.

This bootstrap is separate from the offline build. An expired/unavailable
upstream artifact fails without choosing a rolling channel or rewriting a lock.
GH_TOKEN is sent to curl on stdin, never in argv, JSON, or command logs.
"""
import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import uuid

from evidence import read_json
from foundation import ROOT, host_facts, now, sha256
from nightly_toolchain import validate_lock, write_json


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--lock", type=Path, default=ROOT / "toolchains/cosmocc.lock.json")
    parser.add_argument("--archive", type=Path, required=True)
    parser.add_argument("--report", type=Path, required=True)
    parser.add_argument("--timeout", type=int, default=900)
    args = parser.parse_args()
    if not 1 <= args.timeout <= 1800:
        parser.error("--timeout must be between 1 and 1800 seconds")
    args.archive, args.report = args.archive.resolve(), args.report.resolve()
    directory = args.report.parent / "evidence" / str(uuid.uuid4())
    directory.mkdir(parents=True)
    report = {"schema_version": 1, "report_kind": "locked-nightly-download", "report_id": directory.name,
              "started_at": now(), "status": "fail", "m0_core_status": "not_run", "commands": [],
              "artifact": None, "reason": "", "lock_sha256": None}
    report.update(host=host_facts(), ci=None, qemu=None, build=None, toolchain=None)
    if os.environ.get("GITHUB_ACTIONS") == "true":
        report["ci"] = {key: os.environ.get(value) for key, value in {
            "run_id": "GITHUB_RUN_ID", "run_attempt": "GITHUB_RUN_ATTEMPT", "workflow": "GITHUB_WORKFLOW",
            "commit": "GITHUB_SHA", "runner_os": "RUNNER_OS", "runner_arch": "RUNNER_ARCH",
            "runner_name": "RUNNER_NAME", "image_os": "ImageOS", "image_version": "ImageVersion"}.items()}
    partial = args.archive.with_name(args.archive.name + "." + directory.name + ".partial")
    try:
        lock = read_json(args.lock)
        validate_lock(lock)
        report["lock_sha256"] = sha256(args.lock)
        if lock["source"].get("provider") != "github-actions":
            raise ValueError("This explicit bootstrap requires a locked official GitHub Nightly artifact")
        report["source"] = lock["source"]
        if not args.archive.exists():
            token = os.environ.get("GH_TOKEN")
            if not token:
                report["status"] = "blocked"
                raise ValueError("Explicit GitHub artifact acquisition requires GH_TOKEN in the environment")
            if "\r" in token or "\n" in token:
                raise ValueError("Invalid authentication token format")
            curl = shutil.which("curl")
            if not curl:
                report["status"] = "blocked"
                raise ValueError("curl is required for explicit archive acquisition")
            args.archive.parent.mkdir(parents=True, exist_ok=True)
            argv = [curl, "--disable", "--proto", "=https", "--proto-redir", "=https",
                    "--location", "--fail", "--silent", "--show-error", "--max-time", str(args.timeout),
                    "--header", "@-", "--output", str(partial), lock["source"]["url"]]
            record = {"argv": argv, "cwd": str(ROOT), "started_at": now(), "exit_code": None,
                      "timeout_seconds": args.timeout + 10, "timed_out": False,
                      "stdout_log": str(directory / "download.stdout.log"),
                      "stderr_log": str(directory / "download.stderr.log")}
            report["commands"].append(record)
            try:
                result = subprocess.run(argv, input=("Authorization: Bearer " + token + "\nAccept: application/vnd.github+json\n").encode(),
                                        capture_output=True, timeout=args.timeout + 10, cwd=ROOT)
                record["exit_code"] = result.returncode
                # Sanitize even exceptional curl diagnostics before persisting bytes.
                output, errors = result.stdout.replace(token.encode(), b"[redacted]"), result.stderr.replace(token.encode(), b"[redacted]")
            except subprocess.TimeoutExpired:
                record["timed_out"] = True
                output, errors = b"", b"Bounded archive acquisition timed out\n"
            record["finished_at"] = now()
            Path(record["stdout_log"]).write_bytes(output)
            Path(record["stderr_log"]).write_bytes(errors)
            if record["exit_code"] != 0 or record["timed_out"]:
                raise ValueError("Exact locked upstream artifact acquisition failed; no rolling-channel fallback")
            measured = {"sha256": sha256(partial), "size_bytes": partial.stat().st_size}
            if measured != lock["archive"]:
                raise ValueError("Downloaded original ZIP SHA-256/length differs from lock")
            os.replace(partial, args.archive)
        if {"sha256": sha256(args.archive), "size_bytes": args.archive.stat().st_size} != lock["archive"]:
            raise ValueError("Existing archive SHA-256/length differs from lock; refusing overwrite")
        report.update(status="pass", reason="Exact locked original ZIP verified; cache extraction/compiler checks remain separate",
                      artifact={"path": str(args.archive), **lock["archive"]})
    except (OSError, ValueError, TypeError, KeyError, subprocess.SubprocessError) as error:
        report["reason"] = str(error)
    finally:
        partial.unlink(missing_ok=True)
    report["finished_at"] = now()
    write_json(args.report, report)
    print(json.dumps({"status": report["status"], "reason": report["reason"], "report": str(args.report)}))
    return {"pass": 0, "fail": 1, "blocked": 2}[report["status"]]


if __name__ == "__main__":
    sys.exit(main())
