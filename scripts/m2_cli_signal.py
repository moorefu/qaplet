#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Real OS interrupt contracts at the supplied CLI boundary."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import selectors
import signal
import subprocess
import time


def identity(path):
    return {"path": str(path), "sha256": hashlib.sha256(path.read_bytes()).hexdigest(), "size_bytes": path.stat().st_size}


def linux_case(cli, loader, directory, signum, mode):
    directory.mkdir(parents=True)
    source = "Promise.all([import('qaplet:stdio'),import('qaplet:timers')]).then(async ([s,t])=>{await s.stdout.write(new Uint8Array([82,69,65,68,89,10])).promise;"
    source += "for(;;){}" if mode == "execute" else "await new Promise(r=>t.setTimeout(r,600000));await s.stdout.write(new Uint8Array([65,70,84,69,82])).promise;"
    source += "})"
    if mode == "output": source = "'X'.repeat(200000)"
    (directory / "source.js").write_text(source)
    argv = [*loader, str(cli), "--timeout-ms", "600000", "--max-dispatches", "100000000", "eval", "--source", source]
    if mode == "queue": argv = [*loader, str(cli), "--queue-stop"]
    if mode == "help": argv = [*loader, str(cli), "--help"]
    pair = os.pipe()
    if mode == "help":
        os.set_blocking(pair[1],False)
        try:
            while True:os.write(pair[1],b'F'*4096)
        except BlockingIOError:pass
        os.set_blocking(pair[1],True)
    p = subprocess.Popen(argv, cwd=directory, stdin=subprocess.DEVNULL, stdout=pair[1], stderr=subprocess.PIPE, start_new_session=True)
    os.close(pair[1])
    selector = selectors.DefaultSelector(); selector.register(pair[0], selectors.EVENT_READ)
    out = bytearray(); ready = False; expired = False
    deadline = time.monotonic() + 20
    while time.monotonic() < deadline and p.poll() is None:
        if mode == "help":
            state=Path(f"/proc/{p.pid}/wchan").read_text()
            if "poll" in state or "pipe_write" in state:ready=True;break
            time.sleep(.001);continue
        if selector.select(max(0, deadline-time.monotonic())):
            chunk = os.read(pair[0], 65536)
            if not chunk: break
            out.extend(chunk)
            if b"READY\n" in out or (mode == "output" and out): ready = True; break
    before = time.monotonic()
    if ready: os.kill(p.pid, signum)
    try: _, errors = p.communicate(timeout=12)
    except subprocess.TimeoutExpired:
        expired = True; p.kill(); _, errors = p.communicate()
    while True:
        chunk = os.read(pair[0], 65536)
        if not chunk: break
        out.extend(chunk)
    os.close(pair[0]); selector.close()
    (directory / "stdout.bin").write_bytes(out); (directory / "stderr.log").write_bytes(errors)
    passed = ready and not expired and p.returncode == 1 and b"EXECUTION_STOPPED" in errors and b"AFTER" not in out and b"cleanup incomplete" not in errors
    queue_path = directory / "queue-result.json"
    queue_result = json.loads(queue_path.read_text()) if queue_path.is_file() else None
    if mode == "queue": passed = passed and bool(queue_result and queue_result["queue_full"] and queue_result["owner_stopped"] and queue_result["sdk_destroyed"] and queue_result["user_marks"] == 0 and queue_result["events_dropped"] == 2 and queue_result["sdk_unix_dispositions_preserved"])
    scope = "Separate public C Host OS-relay/SDK queue-saturation boundary; not CLI end-to-end" if mode == "queue" else "Actual supplied CLI; real process-directed OS signal after completed public stdout READY write"
    if mode == "output": scope = "Actual supplied CLI; copied eval result final Host output accepted into paused real pipe, then real OS signal"
    if mode == "help": scope = "Actual supplied CLI help, no Runtime; real full stdout pipe and observed owner OS poll/pipe_write wait before OS signal"
    return {"case": f"linux-{signal.Signals(signum).name}-{mode}", "status": "pass" if passed else "fail", "argv": argv,
        "pid": p.pid, "real_os_signal": signum if ready else None, "ready_accepted": ready, "exit_code": p.returncode,
        "supervisor_timeout": expired, "elapsed_after_send_seconds": time.monotonic()-before,
        "stdout": identity(directory / "stdout.bin"), "stderr": identity(directory / "stderr.log"),
        "native_cleanup_complete": passed, "queue_result": queue_result, "scope": scope}


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--cli", type=Path, required=True); p.add_argument("--loader", type=Path)
    p.add_argument("--report", type=Path, required=True)
    p.add_argument("--mode", choices=("wait", "execute", "output", "help", "queue", "all"), default="all")
    p.add_argument("--signal", choices=("SIGINT", "SIGTERM", "all"), default="all")
    args = p.parse_args(); args.report.parent.mkdir(parents=True, exist_ok=True)
    modes = ("wait", "execute", "output", "help") if args.mode == "all" else (args.mode,)
    signals = (signal.SIGINT, signal.SIGTERM) if args.signal == "all" else (getattr(signal, args.signal),)
    cases = [linux_case(args.cli.resolve(), [str(args.loader.resolve())] if args.loader else [], args.report.parent / (args.report.stem+"-raw") / f"{signum}-{mode}", signum, mode) for signum in signals for mode in modes]
    report = {"status": "pass" if all(c["status"] == "pass" for c in cases) else "fail", "cli": identity(args.cli), "script": identity(Path(__file__)),
              "host": {"uname": list(os.uname()), "page_size": os.sysconf("SC_PAGESIZE")}, "cases": cases, "ci": None}
    args.report.write_text(json.dumps(report, indent=2)+"\n"); print(json.dumps({"status": report["status"], "report": str(args.report)}))
    return report["status"] != "pass"


if __name__ == "__main__": raise SystemExit(main())
