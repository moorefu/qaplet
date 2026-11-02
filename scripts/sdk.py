#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Build the Embed SDK on Ubuntu; run supplied Host bytes without rebuilding."""
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
from build_ape import (BuildError, SOURCES, audit, headers, identity, link_portable,
                       native_toolchain, prepare_portable_helpers, publish_ape, run, verify_inputs)
from evidence import read_json
from foundation import ROOT, host_facts, now, sha256
from input_lock import component, consume_identity
from native_toolchain import recheck
from nightly_toolchain import write_json
from startup import check_ci_commit, ci_facts, measured_host, select_loader
from workflow_support import workspace

RECIPE = ("include/qaplet.h", "src/runtime_internal.h", "src/runtime.c", "src/values.c",
          "examples/embed.c", "tests/native/sdk_host.c", "tests/native/sdk_values.c", "scripts/sdk.py", "scripts/build_ape.py", "scripts/ape_helpers.py",
          "scripts/native_toolchain.py", "scripts/foundation.py", "scripts/startup.py",
          "scripts/evidence.py", "scripts/input_lock.py", "scripts/nightly_toolchain.py",
          "scripts/quickjs_source.py", "scripts/workflow_support.py")
HOST_CASES = (
    ("scalar", [], "scalar: 42; closed: true\n"),
    ("state", ["--state"], "state: 40 -> 42 -> exception -> 43; fresh: true\n"),
    ("lifetime", ["--lifetime"], "lifetime: closed twice; destroy ordering: pass; cycles: 1000\n"),
    ("owner", ["--owner"], "owner: foreign operations rejected; snapshot allowed; JS unchanged\n"),
    ("creation", ["--creation"], "creation: 128 bounded heap configurations; null failures; repeated recovery: pass\n"),
    ("resources", ["--resources"], "resources: source precheck; heap failure permanent; stack exception reusable\n"),
    ("abi", ["--abi"], "ABI: version/flags/tails/short structures; bounded diagnostics: pass\n"),
    ("integers", ["--integers"], "integers: exact signed/unsigned 64-bit boundaries; oversized BigInt rejected\n"),
    ("strings", ["--strings"], "strings: strict UTF-8 and NUL; copied retry; accepted/rejected result lifetime; byte limit\n"),
    ("bytes", ["--bytes"], "bytes: copied Uint8Array views; resize/detach/brand/shared boundaries; no user coercion\n"),
    ("diagnostics", ["--diagnostics"], "diagnostics: safe text; getter/Proxy/toString untouched; bounded independent caller buffers\n"),
    ("value-limits", ["--value-limits"], "value limits: exact 1 MiB strings/bytes; independent copies; metered permanent allocation failure\n"),
    ("facts", ["--facts"], None),
)


def recipe():
    return [{"path": name, "sha256": sha256(ROOT / name), "size_bytes": (ROOT / name).stat().st_size}
            for name in RECIPE]


def digest(value):
    return hashlib.sha256(json.dumps(value, sort_keys=True, separators=(",", ":")).encode()).hexdigest()


def build(args, report, directory):
    if report["host"]["os"] != "Linux" or platform.freedesktop_os_release().get("ID") != "ubuntu":
        raise BuildError("SDK distributions are built on Ubuntu only", "blocked")
    original = verify_inputs(args, report, directory)
    tree = native_toolchain(args, report, directory, original)
    helpers = prepare_portable_helpers(args, report, directory, tree)
    source = Path(report["source"]["path"])
    output = args.output_dir.resolve()
    if output.exists() and any(output.iterdir()):
        raise BuildError("Output directory must be new or empty")
    work = args.build_dir.resolve() / report["report_id"]
    work.mkdir(parents=True)
    (work / "tmp").mkdir()
    output.mkdir(parents=True, exist_ok=True)
    distribution = output / "sdk"
    (distribution / "include").mkdir(parents=True)
    (distribution / "lib/.aarch64").mkdir(parents=True)
    shutil.copyfile(ROOT / "include/qaplet.h", distribution / "include/qaplet.h")
    shutil.copyfile(ROOT / "examples/embed.c", distribution / "embed.c")
    shutil.copyfile(ROOT / "tests/native/sdk_host.c", distribution / "host-contract.c")
    shutil.copyfile(ROOT / "tests/native/sdk_values.c", distribution / "host-values.c")
    shutil.copyfile(ROOT / "third_party/quickjs/LICENSE", distribution / "QUICKJS-LICENSE")
    shutil.copyfile(ROOT / "LICENSE", distribution / "LICENSE")
    files = recipe()
    flags = ["-Os", "-fwrapv", "-D_GNU_SOURCE", '-DCONFIG_VERSION="' + (source / "VERSION").read_text().strip() + '"',
             "-I" + str(source), "-I" + str(ROOT / "include"), "-I" + str(ROOT / "src"),
             "-ffile-prefix-map=" + str(ROOT) + "=qaplet", "-ffile-prefix-map=" + str(source) + "=quickjs",
             "-ffile-prefix-map=" + str(work) + "=build", *args.cflag]
    report["build_directory"] = str(work)
    _, revision, _ = run(report, directory, "git-revision", ["git", "rev-parse", "HEAD"], 10)
    _, dirty, _ = run(report, directory, "git-status", ["git", "status", "--porcelain"], 10)
    report["sdk_recipe"] = {"version": 1, "files": files, "input_id": report["input_lock"]["input_id"],
                            "compile_flags": ["-Os", "-fwrapv", "-D_GNU_SOURCE", "CONFIG_VERSION=locked", *args.cflag],
                            "link_flags": ["-Os", "-Wl,-z,max-page-size=65536", "-lm", "-lpthread"],
                            "archive_flags": "rcsD", "git_commit": revision.strip(),
                            "working_tree_dirty": bool(dirty.strip())}
    # The SDK recipe is additive. It does not mutate or extend the frozen M0 lock.
    report["sdk_recipe"]["sha256"] = digest({key: value for key, value in report["sdk_recipe"].items()
                                              if key not in ("git_commit", "working_tree_dirty")})
    report["build"] = {"flags": flags, "recipe_files": files, "sdk_recipe_id": report["sdk_recipe"]["sha256"],
                       "quickjs_sources": list(SOURCES), "portable_helpers_sha256": helpers["sha256"],
                       "native_helpers_sha256": report["native_toolchain"]["helper_set_sha256"],
                       "libc_source": report["toolchain"]["libc_source"], "source_date_epoch": 0}
    report["build"]["sha256"] = digest(report["build"])
    environment = dict(os.environ, TMPDIR=str(work / "tmp"), SOURCE_DATE_EPOCH="0", LC_ALL="C", TZ="UTC",
                       BUILDLOG=str(output / "qaplet-host.build-commands.log"))
    objects = []
    for index, path in enumerate([ROOT / "src/runtime.c", ROOT / "src/values.c", *(source / name for name in SOURCES)]):
        obj = work / (path.stem + ".o")
        run(report, directory, "sdk-compile-" + str(index), [tree / "bin/cosmocc", *flags, "-c", path, "-o", obj],
            args.compile_timeout, work, environment)
        objects.append(obj)
    archive = distribution / "lib/libqaplet.a"
    run(report, directory, "sdk-archive", [tree / "bin/cosmoar", "rcsD", archive, *objects], args.compile_timeout, work, environment)
    # Compile only against the distributed public header and link the supplied archive.
    run(report, directory, "host-compile", [tree / "bin/cosmocc", "-Os", "-D_GNU_SOURCE", "-I" + str(distribution / "include"),
        "-c", distribution / "host-contract.c", "-o", work / "embed.o"], args.compile_timeout, work, environment)
    link_portable(args, report, directory, tree, helpers, "qaplet-host", [work / "embed.o", archive],
                  work / "qaplet-host.com", work, environment)
    publish_ape(work, output, "qaplet-host", helpers, ape_helpers.source_cache(args.helper_source_cache.resolve()))
    artifact = audit(args, report, directory, tree, "qaplet-host", output)
    artifact["build_commands"] = identity(output / "qaplet-host.build-commands.log")
    report["artifacts"].append(artifact)
    report["sdk"] = {"abi_version": 1, "recipe_id": report["sdk_recipe"]["sha256"],
        "components": [{"file": path.relative_to(output).as_posix(), **identity(path)}
                       for path in sorted(distribution.rglob("*")) if path.is_file()]}
    report["sdk"]["sha256"] = digest([{key: value for key, value in item.items() if key != "path"}
                                       for item in report["sdk"]["components"]])
    verify_inputs(args, report, directory, stage="after")
    recheck(report["native_toolchain"], original, component("cosmocc"))
    if recipe() != files:
        raise BuildError("SDK recipe/source changed during build")
    report.update(status="pass", reason="Static SDK and audited C Host fat APE built on Ubuntu; runtime evidence is separate")


def consume(args, report, directory):
    if not args.build_report or not args.artifact:
        raise BuildError("Supplied Ubuntu producer report and Host artifact are required", "blocked")
    producer = read_json(args.build_report)
    consume_identity(producer, report)
    check_ci_commit(producer, report)
    if producer.get("report_kind") != "qaplet-sdk" or producer.get("scope") != "build" or producer.get("status") != "pass":
        raise BuildError("Successful qaplet-sdk producer required")
    selected = producer["artifacts"]
    if len(selected) != 1 or selected[0]["name"] != "qaplet-host" or selected[0]["audit"]["status"] != "pass":
        raise BuildError("Exactly one audited public C Host artifact required")
    artifact = {**selected[0], "path": str(args.artifact.resolve())}
    if sha256(args.artifact) != artifact["sha256"] or args.artifact.stat().st_size != artifact["size_bytes"]:
        raise BuildError("Supplied Host bytes differ from producer")
    # Validate supplied SDK bytes independently, even on hosts which do not compile.
    for item in producer["sdk"]["components"]:
        name = Path(item["file"])
        if name.is_absolute() or ".." in name.parts or name.parts[0] != "sdk":
            raise BuildError("SDK component must stay within supplied sdk directory")
        path = args.artifact.parent / item["file"]
        if sha256(path) != item["sha256"] or path.stat().st_size != item["size_bytes"]:
            raise BuildError("Supplied SDK component differs from producer: " + item["file"])
    if digest([{key: value for key, value in item.items() if key != "path"}
               for item in producer["sdk"]["components"]]) != producer["sdk"]["sha256"]:
        raise BuildError("SDK aggregate identity differs from its components")
    if producer["sdk"]["recipe_id"] != producer["sdk_recipe"]["sha256"] or digest({
            key: value for key, value in producer["sdk_recipe"].items()
            if key not in ("git_commit", "working_tree_dirty", "sha256")}) != producer["sdk_recipe"]["sha256"]:
        raise BuildError("SDK recipe identity is inconsistent")
    headers(args.artifact, [args.artifact.with_suffix(".com.dbg"), args.artifact.with_suffix(".aarch64.elf")])
    report.update(source=producer["source"], toolchain=producer["toolchain"], build=producer["build"],
                  sdk=producer["sdk"], sdk_recipe=producer["sdk_recipe"], producer_report=identity(args.build_report), artifacts=[artifact])
    measured_host(args, report, directory)
    loader = select_loader(args, report)
    if os.name != "nt":
        args.artifact.chmod(args.artifact.stat().st_mode | 0o111)
    for suffix, arguments, expected in HOST_CASES:
        executed, stdout, stderr = run(report, directory, "qaplet-host." + suffix,
                                      [*loader, args.artifact, *arguments], args.timeout, expected=None)
        if suffix == "facts":
            expected = json.dumps({"probe": "qaplet-host", "arch": report["host"]["execution_arch"],
                                   "page_size": report["host"]["page_size"]}, separators=(",", ":")) + "\n"
        passed = executed["exit_code"] == 0 and not executed["timed_out"] and stdout == expected and not stderr
        report["cases"].append({"case_id": "qaplet-host." + suffix, "status": "pass" if passed else "fail",
            "started_at": executed["started_at"], "finished_at": executed["finished_at"],
            "expected": {"exit_code": 0, "stdout": expected, "stderr": ""},
            "actual": {"exit_code": executed["exit_code"], "stdout": stdout, "stderr": stderr, "timed_out": executed["timed_out"]},
            "commands": [executed], "artifact": identity(args.artifact), "sdk_sha256": producer["sdk"]["sha256"],
            "timeout_seconds": args.timeout, "reason": "Actual public SDK consumer executed supplied APE bytes"})
    report["artifacts"][0]["sha256_after"] = sha256(args.artifact)
    if report["artifacts"][0]["sha256_after"] != artifact["sha256"]:
        raise BuildError("Artifact changed during execution")
    if recipe() != producer["sdk_recipe"]["files"]:
        raise BuildError("Consumer recipe differs from selected SDK producer")
    report.update(status="pass" if all(item["status"] == "pass" for item in report["cases"]) else "fail",
                  reason="Supplied SDK/Host identity and actual public C smoke checked; full M1 matrix not claimed")


def main():
    cache = Path(workspace(ROOT)["cache_root"])
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("operation", choices=("build", "run"))
    parser.add_argument("--source-cache", type=Path, default=cache / "quickjs")
    parser.add_argument("--toolchain-cache", type=Path, default=cache / "toolchains/cosmocc")
    parser.add_argument("--helper-source-cache", type=Path, default=cache / "sources/ape-loader")
    parser.add_argument("--build-dir", type=Path, default=cache / "build/sdk")
    parser.add_argument("--output-dir", type=Path, default=ROOT / "out/sdk/artifacts")
    parser.add_argument("--report", type=Path, default=ROOT / "out/sdk/report.json")
    parser.add_argument("--build-report", type=Path)
    parser.add_argument("--artifact", type=Path)
    parser.add_argument("--loader-dir", type=Path)
    parser.add_argument("--loader-manifest", type=Path)
    parser.add_argument("--expected-os")
    parser.add_argument("--expected-arch")
    parser.add_argument("--runner-label", default="local")
    parser.add_argument("--timeout", type=int, default=30)
    parser.add_argument("--compile-timeout", type=int, default=300)
    parser.add_argument("--cflag", action="append", default=[])
    args = parser.parse_args()
    args.controlled_probe = False
    if not 1 <= args.timeout <= 300 or not 1 <= args.compile_timeout <= 900:
        parser.error("Invalid execution/compile timeout")
    directory = args.report.resolve().parent / "evidence" / str(uuid.uuid4())
    directory.mkdir(parents=True)
    report = {"schema_version": 1, "report_kind": "qaplet-sdk", "scope": args.operation, "report_id": directory.name,
              "status": "not_run", "reason": "", "started_at": now(), "host": host_facts(), "ci": ci_facts(),
              "commands": [], "cases": [], "artifacts": [], "input_reports": {}, "source": None, "toolchain": None,
              "build": None, "native_toolchain": None, "evidence_directory": str(directory)}
    try:
        (build if args.operation == "build" else consume)(args, report, directory)
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
