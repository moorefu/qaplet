# SPDX-License-Identifier: MIT
"""ENV-01 acceptance using real Linux tools; never installs or updates dependencies."""

from pathlib import Path
import shutil
import tempfile
from urllib.parse import urlsplit

from foundation import ROOT, case_record, command, diagnose_tool, sha256, smoke


def inspect_environment(args, report, directory):
    cases = report["cases"]

    def run(case_id, argv, expected=None):
        executed, stdout, stderr = command(argv, directory, case_id, args.timeout)
        passed = executed["exit_code"] == 0 and not executed["timed_out"]
        if expected is not None:
            passed = passed and stdout.strip() == expected
        unavailable = executed["exit_code"] is None and not executed["timed_out"]
        status = "pass" if passed else "blocked" if unavailable else "fail"
        reason = ("Command and output verified" if passed else
                  "Command could not be launched (permissions or executable unavailable); see raw logs" if unavailable else
                  "Command failed, timed out, or output differed; see raw logs")
        record = case_record(case_id, status, reason,
                             commands=[executed], expected=expected, actual=stdout.strip(),
                             exit_code=executed["exit_code"], timeout_seconds=args.timeout)
        cases.append(record)
        return record

    if report["host"]["os"] != "Linux" or report["host"]["execution_arch"] != "x86_64":
        cases.append(case_record("environment.linux-x64", "blocked", "Run inside the existing Linux x64 distribution; Windows is not Linux evidence"))
    else:
        paths = [ROOT, directory] + ([args.backing_dir.resolve()] if args.backing_dir else [])
        try:
            disks = [{"path": str(path), "free_bytes": shutil.disk_usage(path).free,
                      "total_bytes": shutil.disk_usage(path).total} for path in paths]
            enough = all(disk["free_bytes"] >= args.min_free_bytes for disk in disks)
            cases.append(case_record("environment.disk", "pass" if enough else "blocked",
                "Measured source/build/backing filesystems; virtual capacity alone does not prove backing-drive free space",
                expected={"minimum_free_bytes": args.min_free_bytes}, actual=disks))
        except OSError as error:
            enough = False
            cases.append(case_record("environment.disk", "blocked", str(error)))
        tools = [("make", ["make"], None), ("cc", [args.cc], None), ("git", ["git"], None),
                 ("curl", ["curl"], None), ("tar", ["tar"], None), ("gzip", ["gzip"], None),
                 ("xz", ["xz"], None), ("unzip", ["unzip"], ["-v"]),
                 ("zip", ["zip"], ["-v"]), ("sha256sum", ["sha256sum"], None),
                 ("sh", ["sh"], ["-c", "printf shell-ready"]),
                 ("uname", ["uname"], None), ("getconf", ["getconf"], None)]
        report["tools"] = [diagnose_tool(name, candidates, directory, version) for name, candidates, version in tools]
        for tool in report["tools"]:
            cases.append(case_record("environment.tool." + tool["name"], tool["status"], tool["reason"],
                                     commands=[tool["command"]] if tool["command"] else [],
                                     actual={"path": tool["path"], "version": tool["version"]}))
        if enough and all(tool["status"] == "pass" for tool in report["tools"]):
            check_linux(args, report, directory, run)
    statuses = {case["status"] for case in cases}
    report["status"] = "fail" if "fail" in statuses else "blocked" if statuses - {"pass"} else "pass"
    report["reason"] = "ENV-01 only: Linux tools, disk, filesystem access, HTTPS/archive/SHA-256 and system C; M0 product probes remain unexecuted"


def check_linux(args, report, directory, run):
    cases = report["cases"]
    host = report["host"]
    run("environment.uname", ["uname", "-s"], "Linux")
    run("environment.architecture", ["uname", "-m"], "x86_64")
    run("environment.kernel", ["uname", "-r"], host["kernel"])
    run("environment.pages", ["getconf", "PAGESIZE"], str(host["page_size"]))
    run("environment.distribution", ["cat", "/etc/os-release"])
    run("environment.cpu", ["lscpu"])
    for name, location in (("source", ROOT / "out"), ("build", directory)):
        try:
            location.mkdir(parents=True, exist_ok=True)
            with tempfile.TemporaryDirectory(prefix="env01-access-", dir=location) as scratch:
                probe = Path(scratch) / "execute.sh"
                probe.write_text("#!/bin/sh\nprintf 'env01-access-ok\\n'\n", encoding="utf-8")
                probe.chmod(0o700)
                run("environment.access." + name, [str(probe)], "env01-access-ok")
        except OSError as error:
            cases.append(case_record("environment.access." + name, "blocked", str(error)))
    if any(case["status"] != "pass" for case in cases if case["case_id"].startswith("environment.access.")):
        return
    run("environment.fresh-shell", ["sh", "-lc",
        'set -eu; cd "$1"; test -r tests/native/foundation_smoke.c; cd "$2"; '
        'for tool in make git curl tar gzip xz unzip zip sha256sum; do command -v "$tool" >/dev/null; done; '
        'make --version >/dev/null; "$3" --version >/dev/null; git --version >/dev/null; printf shell-ready',
        "env01", str(ROOT), str(directory), args.cc], "shell-ready")

    source = ROOT / "tests/native/foundation_smoke.c"
    digest = sha256(source)
    run("environment.sha256", ["sha256sum", str(source)], digest + "  " + str(source))
    archive = directory / "source.tar.gz"
    extracted = directory / "tar-extracted"
    extracted.mkdir()
    run("environment.tar-create", ["tar", "-czf", str(archive), "-C", str(source.parent), source.name])
    run("environment.tar-extract", ["tar", "-xzf", str(archive), "-C", str(extracted)])
    run("environment.tar-content", ["sha256sum", str(extracted / source.name)], digest + "  " + str(extracted / source.name))
    zipped = directory / "source.zip"
    extracted_zip = directory / "zip-extracted"
    run("environment.zip-create", ["zip", "-j", str(zipped), str(source)])
    run("environment.zip-extract", ["unzip", "-q", str(zipped), "-d", str(extracted_zip)])
    run("environment.zip-content", ["sha256sum", str(extracted_zip / source.name)], digest + "  " + str(extracted_zip / source.name))

    url = urlsplit(args.download_url or "")
    if not args.download_url:
        cases.append(case_record("environment.https-download", "blocked", "Supply an explicit --download-url to exercise HTTPS; no implicit network fetch"))
    elif url.scheme != "https" or not url.hostname or url.username or url.password:
        cases.append(case_record("environment.https-download", "fail", "Test download requires HTTPS without embedded credentials"))
    else:
        downloaded = directory / "https-download"
        record = run("environment.https-download", ["curl", "--fail", "--location", "--silent", "--show-error",
            "--proto", "=https", "--proto-redir", "=https", "--connect-timeout", "10", "--max-time", str(args.timeout),
            "--max-filesize", str(5 * 1024**2), "--output", str(downloaded), args.download_url])
        if record["status"] == "pass":
            if not downloaded.is_file() or downloaded.stat().st_size == 0:
                record["status"], record["reason"] = "fail", "HTTPS produced no nonempty file"
            else:
                record["artifact"] = {"path": str(downloaded), "sha256": sha256(downloaded),
                                      "size_bytes": downloaded.stat().st_size, "kind": "https-environment-fixture"}
                run("environment.download-sha256", ["sha256sum", str(downloaded)], sha256(downloaded) + "  " + str(downloaded))
    c_case = smoke(args, report, directory)
    if c_case["status"] == "pass" and c_case["actual"]["page_size"] != host["page_size"]:
        c_case["status"], c_case["reason"] = "fail", "C sysconf page size differs from OS measurement"
    cases.append(c_case)
