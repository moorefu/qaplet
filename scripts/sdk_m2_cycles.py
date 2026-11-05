#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Sealed public SDK cycle fixture: Ubuntu builds, every host consumes bytes."""
import argparse
import json
import os
from pathlib import Path
import platform
import shutil
import sys
import tempfile
import uuid
import zipfile

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))


def inspect(stdout, stderr, exit_code, mode, requested):
    if exit_code != 0 or stderr:
        raise ValueError("Cycle process did not finish successfully")
    rows = [json.loads(line) for line in stdout.decode("utf-8").splitlines()]
    finals = [row for row in rows if row.get("type") == "final"]
    if len(finals) != 1 or rows[-1] != finals[0]:
        raise ValueError("One final cycle record is required")
    final = finals[0]
    if rows[0].get("type") != "ready" or any(row.get("mode") != mode for row in rows):
        raise ValueError("Actual ready record and one unchanged mode are required")
    for previous, current in zip(rows, rows[1:]):
        if current["elapsed_ms"] < previous["elapsed_ms"] or current["completed"] < previous["completed"]:
            raise ValueError("Actual cycle progress is not monotonic")
        if (current["process_id"], current["owner_thread_id"]) != (previous["process_id"], previous["owner_thread_id"]):
            raise ValueError("Cycle owner identity changed")
    if final["mode"] != mode or final["requested"] != requested or final["failed"]:
        raise ValueError("Cycle request or outcome differs")
    if mode != "soak" and final["completed"] != requested:
        raise ValueError("Actual completed cycle count differs")
    if mode == "soak" and (final["elapsed_ms"] < requested * 1000 or final["created"] != 1):
        raise ValueError("Persistent Runtime soak duration is incomplete")
    expected = 1 if mode == "soak" else requested
    if any(final[key] != expected for key in ("created", "closed", "quiescent", "destroyed")):
        raise ValueError("Public lifecycle barriers are incomplete")
    if not final["native_cleanup_complete"] or not final["baseline_checks"]:
        raise ValueError("Actual native cleanup and resource baseline are required")
    if final["helper_limit"] != 4 or final["os_resource_source"] == "not_measured" or (
            final["os_handles_after"] > final["os_handles_before"] or final["os_threads_after"] > final["os_threads_before"]):
        raise ValueError("Default helper limit or frozen OS resource baseline differs")
    frozen = [row for row in rows if row.get("baseline_frozen")]
    baseline_keys = ("baseline_operations", "baseline_data_bytes", "baseline_timers", "baseline_processes",
                     "baseline_io_endpoints", "baseline_handles", "baseline_helpers", "baseline_workers")
    if final.get("fixture_protocol_version") == 9:
        baseline_keys += ("baseline_keepalive_tokens",)
        if (not final["observer_identity_verified"] or final["baseline_keepalive_tokens"] or
                any(final[key] for key in ("quiescent_helpers", "quiescent_workers", "quiescent_keepalive_tokens"))):
            raise ValueError("Actual native identity and token/worker/helper cleanup facts are required")
        if final["observer_resolutions"] != (1 if final["os_resource_source"].startswith("GetProcessHandleCount") else 0):
            raise ValueError("The Windows observer must resolve its borrowed module APIs once")
    if frozen and any(tuple(row[key] for key in baseline_keys) != tuple(frozen[0][key] for key in baseline_keys)
                      for row in frozen):
        raise ValueError("Frozen OPEN resource baseline changed")
    if mode != "lifecycle":
        if any(final[key] != final["completed"] for key in
               ("work", "events", "timer", "stdio", "tty", "child_reaped", "child_eof", "producer_joined")):
            raise ValueError("Mixed resources did not complete every actual cycle")
        if min(final["normal"], final["cancelled"], final["errors"]) < 1:
            raise ValueError("Mixed completion/cancellation/error coverage is incomplete")
    return final


def build(args, report, directory):
    import ape_helpers
    import sdk
    from build_ape import BuildError, audit, identity, native_toolchain, prepare_portable_helpers, publish_ape, run, verify_inputs
    from foundation import sha256
    from input_lock import consumption_purpose
    from sdk_link import link_portable
    if report["host"]["os"] != "Linux" or platform.freedesktop_os_release().get("ID") != "ubuntu":
        raise BuildError("Cycle fixture compilation requires Ubuntu")
    producer = json.loads(args.producer.read_text(encoding="utf-8"))
    sdk.check_package(producer, args.supplied.resolve())
    if producer["status"] != "pass":
        raise BuildError("Successful sealed SDK producer required")
    report.update(sdk=producer["sdk"], sdk_recipe=producer["sdk_recipe"], producer_report=identity(args.producer))
    output = args.output_dir.resolve()
    if output.exists() and any(output.iterdir()):
        raise BuildError("Cycle artifacts require a new output directory")
    output.mkdir(parents=True, exist_ok=True)
    # Exact compiler inputs survive before the first compiler/helper command.
    # The original external directory remains in argv and is never reconstructed.
    external = Path(tempfile.mkdtemp(prefix="qaplet-m2-cycles-"))
    with zipfile.ZipFile(args.supplied / producer["package"]["file"]) as archive:
        archive.extractall(external)
    sdk.check_package(producer, external, check_archive=False)
    shutil.copyfile(ROOT / "tests/native/sdk_m2_cycles.c", external / "sdk/cycles.c")
    retained = directory / "compiler-inputs"
    retained.mkdir()
    inputs = []
    for source in sorted((external / "sdk").rglob("*")):
        if not source.is_file():
            continue
        target = retained / source.relative_to(external)
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(source, target)
        if sha256(source) != sha256(target):
            raise BuildError("Compiler input preservation differs")
        inputs.append({"original": identity(source), "retained": identity(target)})
    report["compiler_inputs"] = inputs
    report["external_directory"] = str(external)
    (directory / "compiler-inputs.json").write_text(json.dumps({"producer": report["producer_report"],
        "sdk": producer["sdk"], "inputs": inputs}, indent=2) + "\n", encoding="utf-8")
    evaluators = directory / "evaluator-inputs"
    evaluators.mkdir()
    report["evaluator_inputs"] = []
    for source in (Path(__file__), ROOT / "tests/contracts/test_sdk_m2_cycles.py"):
        target = evaluators / source.name
        shutil.copyfile(source, target)
        report["evaluator_inputs"].append({"original": identity(source), "retained": identity(target)})
    original = verify_inputs(args, report, directory, source=False)
    tree = native_toolchain(args, report, directory, original)
    helpers = prepare_portable_helpers(args, report, directory, tree)
    work = args.build_dir.resolve() / directory.name
    (work / "tmp").mkdir(parents=True)
    name = "qaplet-m2-cycles"
    environment = dict(os.environ, TMPDIR=str(work / "tmp"), SOURCE_DATE_EPOCH="0", LC_ALL="C", TZ="UTC",
                       BUILDLOG=str(output / (name + ".build-commands.log")))
    run(report, directory, "cycles-compile", [tree / "bin/cosmocc", "-Os", "-D_GNU_SOURCE",
        "-I" + str(external / "sdk/include"), "-I" + str(external / "sdk"),
        "-ffile-prefix-map=" + str(external) + "=external", "-c", external / "sdk/cycles.c", "-o", work / "cycles.o"],
        args.compile_timeout, work, environment)
    link_portable(args, report, directory, tree, helpers, name,
                  [work / "cycles.o", external / "sdk/lib/libqaplet.a"], work / (name + ".com"), work, environment)
    publish_ape(work, output, name, helpers, ape_helpers.source_cache(args.helper_source_cache.resolve(),
                consumption_purpose("build", args.controlled_probe)))
    report["artifacts"].append(audit(args, report, directory, tree, name, output))
    report["portable_helpers"] = helpers
    for row in inputs:
        if sha256(Path(row["original"]["path"])) != row["original"]["sha256"]:
            raise BuildError("Cycle compiler input changed during build")
    report.update(status="pass", reason="Independent public cycle fixture compiled against sealed SDK; product recipe unchanged")


def main():
    from build_ape import BuildError, identity, run
    from foundation import host_facts, now
    from startup import measured_host, select_loader
    from workflow_support import guard_evidence_outputs, workspace, write_evidence_report
    cache = Path(workspace(ROOT)["cache_root"])
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("operation", choices=("build", "run"))
    parser.add_argument("--producer", type=Path, required=True)
    parser.add_argument("--supplied", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path)
    parser.add_argument("--fixture-producer", type=Path)
    parser.add_argument("--artifact", type=Path)
    parser.add_argument("--mode", choices=("lifecycle", "mixed", "soak"), default="mixed")
    parser.add_argument("--requested", type=int, default=100)
    parser.add_argument("--report", type=Path, required=True)
    parser.add_argument("--loader-dir", type=Path)
    parser.add_argument("--loader-manifest", type=Path)
    parser.add_argument("--source-cache", type=Path, default=cache / "quickjs")
    parser.add_argument("--toolchain-cache", type=Path, default=cache / "toolchains/cosmocc")
    parser.add_argument("--helper-source-cache", type=Path, default=cache / "sources/ape-loader")
    parser.add_argument("--build-dir", type=Path, default=cache / "build/m2-24-cycles")
    parser.add_argument("--controlled-probe", action="store_true")
    parser.add_argument("--timeout", type=int, default=300)
    parser.add_argument("--compile-timeout", type=int, default=300)
    parser.add_argument("--expected-os")
    parser.add_argument("--expected-arch")
    parser.add_argument("--runner-label", default="local")
    args = parser.parse_args()
    if args.requested < 1:
        parser.error("requested must be positive")
    directory = args.report.resolve().parent / "evidence" / str(uuid.uuid4())
    guard_evidence_outputs([args.report, directory, *([args.output_dir] if args.output_dir else [])],
                           [args.producer, args.fixture_producer], [args.supplied])
    directory.mkdir(parents=True)
    report = {"schema_version": 1, "report_kind": "qaplet-m2-cycles", "scope": args.operation, "report_id": directory.name,
              "status": "not_run", "started_at": now(), "host": host_facts(), "ci": None,
              "commands": [], "cases": [], "artifacts": [], "input_reports": {},
              "source": None, "native_toolchain": None, "controlled_probe": args.controlled_probe}
    try:
        if args.operation == "build":
            if not args.output_dir:
                raise BuildError("build requires output-dir")
            build(args, report, directory)
        else:
            import sdk
            producer = json.loads(args.producer.read_text(encoding="utf-8"))
            supplied_fixture = json.loads(args.fixture_producer.read_text(encoding="utf-8"))
            sdk.check_package(producer, args.supplied.resolve())
            artifact = args.artifact.resolve()
            supplied = supplied_fixture["artifacts"][0]
            if identity(artifact)["sha256"] != supplied["sha256"] or supplied_fixture["sdk"] != producer["sdk"]:
                raise BuildError("Cycle fixture or SDK differs from actual producer")
            report.update(sdk=producer["sdk"], sdk_recipe=producer["sdk_recipe"],
                          producer_report=identity(args.producer), fixture_producer=identity(args.fixture_producer))
            measured_host(args, report, directory)
            loader = select_loader(args, report)
            work = directory / args.mode
            work.mkdir()
            file = work / "input.bin"
            file.write_bytes(b"I\0\xffN\n")
            argv = [*loader, artifact, "--" + args.mode, str(args.requested)]
            if args.mode != "lifecycle":
                argv += [str(artifact), str(loader[0]) if loader else "-", str(file)]
            if report["host"]["os"] == "Windows" and args.mode != "lifecycle":
                argv_file, native_file = work / "console-argv.json", work / "console-command.json"
                argv_file.write_text(json.dumps(list(map(str, argv))) + "\n", encoding="utf-8")
                executed, _, _ = run(report, directory, "console-supervisor", [sys.executable, Path(__file__).resolve(),
                    "console-child", "--argv-file", argv_file, "--work", work, "--native-report", native_file,
                    "--timeout", str(args.timeout)], args.timeout + 15, work, expected=None)
                native = json.loads(native_file.read_text(encoding="utf-8"))
                report["native_command"] = native
                stdout, stderr = (Path(executed[key + "_log"]).read_bytes() for key in ("stdout", "stderr"))
                if native["error"] or native["timed_out"] or native["exit_code"] != 0 or not all(native["cleanup"].values()):
                    raise BuildError("Console controller cleanup incomplete")
            else:
                executed, _, _ = run(report, directory, args.mode, argv, args.timeout, work, expected=None)
                stdout = Path(executed["stdout_log"]).read_bytes()
                stderr = Path(executed["stderr_log"]).read_bytes()
            progress = work / "cycles-progress.ndjson"
            final = inspect(progress.read_bytes(), stderr, executed["exit_code"], args.mode, args.requested)
            if executed["timed_out"]:
                raise BuildError("Outer timeout is incomplete")
            report["cases"].append({"case_id": "m2-" + args.mode, "status": "pass", "required": True,
                                    "observations": final, "command": executed, "artifact": identity(artifact),
                                    "progress": identity(progress),
                                    "waits": identity(work / "cycles-waits.ndjson") if (work / "cycles-waits.ndjson").exists() else None})
            report.update(status="pass", reason="Actual M2 public lifecycle/resource cycles and cleanup barriers")
    except (OSError, ValueError, KeyError, TypeError, BuildError) as error:
        report.update(status="fail", reason=str(error))
    report["finished_at"] = now()
    write_evidence_report(args.report, report)
    print(json.dumps({"status": report["status"], "reason": report["reason"], "report": str(args.report)}))
    return 0 if report["status"] == "pass" else 1


def console_child():
    from m2_tty_terminal import conpty
    parser = argparse.ArgumentParser()
    parser.add_argument("--argv-file", type=Path, required=True)
    parser.add_argument("--work", type=Path, required=True)
    parser.add_argument("--native-report", type=Path, required=True)
    parser.add_argument("--timeout", type=int, required=True)
    args = parser.parse_args(sys.argv[2:])
    native, _ = conpty(json.loads(args.argv_file.read_text(encoding="utf-8")), args.work, args.timeout)
    args.native_report.write_text(json.dumps(native, indent=2) + "\n", encoding="utf-8")
    return 0 if native["exit_code"] == 0 and not native["timed_out"] and not native["error"] and all(native["cleanup"].values()) else 1


if __name__ == "__main__":
    sys.exit(console_child() if len(sys.argv) > 1 and sys.argv[1] == "console-child" else main())
