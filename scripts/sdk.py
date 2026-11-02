#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Build the Embed SDK on Ubuntu; run supplied Host bytes without rebuilding."""
import argparse
from datetime import datetime
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
from input_lock import component, consume_identity, consumption_purpose
from native_toolchain import recheck
from nightly_toolchain import write_json
from startup import check_ci_commit, ci_facts, measured_host, select_loader
from workflow_support import workspace

RECIPE = ("include/qaplet.h", "src/runtime_internal.h", "src/runtime.c", "src/execution.c", "src/values.c", "src/modules.c", "src/host_module.c", "src/events.c",
          "examples/embed.c", "tests/native/sdk_host.c", "tests/native/sdk_values.c", "tests/native/sdk_promises.c",
          "tests/native/sdk_budgets.c", "tests/native/sdk_module_policy.c", "tests/native/sdk_host_modules.c", "tests/native/sdk_events.c",
          "scripts/sdk.py", "scripts/build_ape.py", "scripts/ape_helpers.py",
          "scripts/native_toolchain.py", "scripts/foundation.py", "scripts/startup.py",
          "scripts/evidence.py", "scripts/input_lock.py", "scripts/nightly_toolchain.py",
          "scripts/quickjs_source.py", "scripts/workflow_support.py", "tests/native/sdk_modules.c",
          "tests/fixtures/modules/entry.mjs", "tests/fixtures/modules/dependency.js")
RECIPE += tuple(path.relative_to(ROOT).as_posix() for path in sorted((ROOT / "tests/fixtures/modules").rglob("*"))
                if path.is_file() and path.relative_to(ROOT).as_posix() not in RECIPE)
RECIPE += ("tests/native/sdk_runtime.c", "src/cli.c")
HOST_CASES = (
    ("runtime", ["--runtime", "{modules}"], "runtime snapshot: fixed read-only argv/cwd; public factory: pass\n"),
    ("cli-contract", ["--cli-contract", "{modules}"], "CLI contrast: shared Script/ESM/cache/rejection/results/budgets; SDK pending preserved: pass\n"),
    ("events", ["--events"], "events: once-only registration; copied POD and independent exact JS values: pass\n"),
    ("events-queue", ["--events-queue"], "event queue: finite defaults; full/oversize atomic refusal; FIFO wrap; raw close drop: pass\n"),
    ("events-scheduling", ["--events-scheduling"], "event scheduling: jobs-first slices; ignored Promise return; checkpoint rejection; ordinary-error reuse: pass\n"),
    ("events-threads", ["--events-threads"], "event threads: controlled producer FIFO; owner/callback barriers; post and snapshot reentry: pass\n"),
    ("events-budgets", ["--events-budgets"], "event budgets: shared/idle cumulative ledgers; owner-wait deadline; allocation first cause; factory refusal: pass\n"),
    ("events-entry", ["--events-entry", "{modules}"], "event entry: static builtin; no-progress TLA; worker copy; owner pump completion: pass\n"),
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
    ("promise-values", ["--promise-values"], "Promise values: exact/copied results; safe rejection diagnostics; first-error and handled checkpoints\n"),
    ("promises", ["--promises"], "promises: explicit pump; bounded jobs; checkpoints; failure reuse; pending close: pass\n"),
    ("modules", ["--modules", "{modules}"], "modules: real static graph; completed result: pass\n"),
    ("module-async", ["--module-async", "{modules}"], "module async: static/dynamic imports; TLA; cycle; namespace then untouched: pass\n"),
    ("module-identity", ["--module-identity", "{modules}"], "module identity: fixed cwd; first metadata; canonical reuse; explicit Script base: pass\n"),
    ("module-limits", ["--module-limits", "{modules}"], "module limits: source precheck preserves result; count/cache bounded; close releases: pass\n"),
    ("module-pending", ["--module-pending", "{modules}"], "module pending: unresolved TLA; no progress; BUSY; pending close: pass\n"),
    ("module-budgets", ["--module-budgets", "{modules}"], "module budgets: cumulative TLA jobs; host-wait deadline; module loop interrupted; permanent failure: pass\n"),
    ("module-nul", ["--module-nul", "{modules}"], "module NUL: computed/static names rejected; existing files intact; ordinary NUL strings preserved: pass\n"),
    ("module-cache", ["--module-cache", "{modules}"], "module cache: source/load/parse/link/eval/TLA failures frozen; fresh repair; partial effects retained; accounting: pass\n"),
    ("module-policy", ["--module-policy", "{modules}"], "module policy: URL/path/NUL/format refusals; real ordinary files only; no failed identities for invalid requests: pass\n"),
    ("module-aliases", ["--module-aliases", "{modules}"], "module aliases: actual hardlink identities; native Windows drive and OS path case: pass\n"),
    ("module-symlinks", ["--module-symlinks", "{modules}"], "module symlinks: canonical reuse; frozen retarget; fresh Runtime sees target; target format checked: pass\n"),
    ("budgets", ["--budgets"], "budgets: defaults; deadlines/host wait; cumulative jobs; tightening; first cause; close: pass\n"),
    ("host-modules", ["--host-modules", "{modules}"], "host modules: copied registration; synchronous typed callback; catchable errors; guarded lifetime: pass\n"),
    ("facts", ["--facts"], None),
)


CLI_CASES = (
    ("help", ["--help"], 0, "Usage: qaplet [--timeout-ms N] [--max-dispatches N] run <file> [-- args...]\n       qaplet [--timeout-ms N] [--max-dispatches N] eval --source <text>\n       qaplet --help | --version\nrun uses ESM; argv contains only args after --. eval uses Script.\neval prints scalar values, raw UTF-8 strings, or 0x-prefixed hex bytes, then a newline.\nZero budgets use finite SDK defaults. Exit: 0 success, 1 execution, 2 arguments.\n", ""),
    ("version", ["--version"], 0, "qaplet 0.1.0 (Embed SDK ABI 1)\n", ""),
    ("eval-number", ["eval", "--source", "6 * 7"], 0, "42\n", ""),
    ("eval-undefined", ["eval", "--source", "undefined"], 0, "undefined\n", ""),
    ("eval-null", ["eval", "--source", "null"], 0, "null\n", ""),
    ("eval-bool", ["eval", "--source", "true"], 0, "true\n", ""),
    ("eval-signed", ["eval", "--source", "-9223372036854775808n"], 0, "-9223372036854775808\n", ""),
    ("eval-unsigned", ["eval", "--source", "18446744073709551615n"], 0, "18446744073709551615\n", ""),
    ("eval-string-nul", ["eval", "--source", "'a\\0😀'"], 0, "a\0😀\n", ""),
    ("eval-bytes", ["eval", "--source", "new Uint8Array([0,255,17])"], 0, "0x00ff11\n", ""),
    ("eval-object", ["eval", "--source", "({toString(){throw new Error('conversion ran')}})"], 1, "", "qaplet: code=8 VALUE_TYPE_UNSUPPORTED"),
    ("eval-thenable", ["eval", "--source", "({get then(){throw new Error('getter ran')}})"], 1, "", "qaplet: code=8 VALUE_TYPE_UNSUPPORTED"),
    ("eval-script", ["eval", "--source", "var answer=40; answer+2"], 0, "42\n", ""),
    ("eval-esm-refused", ["eval", "--source", "export const answer=42"], 1, "", "qaplet: code=7 "),
    ("eval-promise", ["eval", "--source", "Promise.resolve(42)"], 0, "42\n", ""),
    ("eval-rejection", ["eval", "--source", "Promise.reject(new Error('entry rejection'))"], 1, "", "qaplet: code=7 "),
    ("eval-checkpoint", ["eval", "--source", "let bad=Promise.reject('handled');let p=Promise.resolve();for(let i=0;i<600;++i)p=p.then(()=>{});p.then(()=>{bad.catch(()=>{});return 42})"], 0, "42\n", ""),
    ("eval-diagnostic-getter", ["eval", "--source", "throw Object.defineProperty(new Error(),'message',{get(){throw new Error('diagnostic getter ran')}})"], 1, "", "qaplet: code=7 JS_EXCEPTION"),
    ("eval-unsettled", ["eval", "--source", "new Promise(()=>{})"], 1, "", "qaplet: UNSETTLED_ENTRY"),
    ("eval-no-base", ["eval", "--source", "import('./entry.mjs')"], 1, "", "qaplet: code=14 MODULE_RESOLVE"),
    ("eval-runtime", ["eval", "--source", "import('qaplet:runtime').then(m=>Object.isFrozen(m.argv)&&m.argv.length===0&&typeof m.cwd==='string'&&m.cwd[0]==='/')"], 0, "true\n", ""),
    ("eval-unknown-builtin", ["eval", "--source", "import('qaplet:future')"], 1, "", "qaplet: code=6 UNSUPPORTED"),
    ("run-argv", ["run", "{modules}/cli-runtime.mjs", "--", "first", "参数 空格", "--literal"], 0, "", ""),
    ("run-cache-namespace", ["run", "{modules}/cli-cache.mjs"], 0, "", ""),
    ("run-rejection", ["run", "{modules}/cli-reject.mjs"], 1, "", "qaplet: code=7 "),
    ("run-unsettled", ["run", "{modules}/cli-unsettled.mjs"], 1, "", "qaplet: UNSETTLED_ENTRY"),
    ("run-query", ["run", "{modules}/entry.mjs?refused"], 1, "", "qaplet: code=14 MODULE_RESOLVE"),
    ("run-directory", ["run", "{modules}"], 1, "", "qaplet: code=15 MODULE_LOAD"),
    ("run-missing", ["run", "{modules}/cli-absent.mjs"], 1, "", "qaplet: code=14 MODULE_RESOLVE"),
    ("eval-dispatch-budget", ["--max-dispatches", "600", "eval", "--source", "let p=Promise.resolve();for(let i=0;i<1000;++i)p=p.then(()=>{});p"], 1, "", "qaplet: code=18 DISPATCH_LIMIT"),
    ("run-dispatch-budget", ["--max-dispatches", "600", "run", "{modules}/cli-jobs.mjs"], 1, "", "qaplet: code=18 DISPATCH_LIMIT"),
    ("eval-deadline", ["--timeout-ms", "20", "eval", "--source", "for(;;){}"], 1, "", "qaplet: code=17 DEADLINE_EXCEEDED"),
    ("run-deadline", ["--timeout-ms", "20", "run", "{modules}/cli-loop.mjs"], 1, "", "qaplet: code=17 DEADLINE_EXCEEDED"),
    ("args-empty", [], 2, "", "qaplet: INVALID_ARGUMENT"),
    ("args-implicit-source", ["eval", "42"], 2, "", "qaplet: INVALID_ARGUMENT"),
    ("args-missing-source", ["eval", "--source"], 2, "", "qaplet: INVALID_ARGUMENT"),
    ("args-source-tail", ["eval", "--source", "42", "extra"], 2, "", "qaplet: INVALID_ARGUMENT"),
    ("args-missing-file", ["run"], 2, "", "qaplet: INVALID_ARGUMENT"),
    ("args-missing-separator", ["run", "{modules}/entry.mjs", "extra"], 2, "", "qaplet: INVALID_ARGUMENT"),
    ("args-invalid-budget", ["--timeout-ms", "-1", "eval", "--source", "42"], 2, "", "qaplet: INVALID_ARGUMENT"),
    ("args-overflow-budget", ["--max-dispatches", "18446744073709551616", "eval", "--source", "42"], 2, "", "qaplet: INVALID_ARGUMENT"),
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
    shutil.copyfile(ROOT / "tests/native/sdk_promises.c", distribution / "host-promises.c")
    shutil.copyfile(ROOT / "tests/native/sdk_modules.c", distribution / "host-modules.c")
    shutil.copyfile(ROOT / "tests/native/sdk_module_policy.c", distribution / "host-module-policy.c")
    shutil.copytree(ROOT / "tests/fixtures/modules", distribution / "modules")
    shutil.copyfile(ROOT / "tests/native/sdk_budgets.c", distribution / "host-budgets.c")
    shutil.copyfile(ROOT / "tests/native/sdk_host_modules.c", distribution / "host-host-modules.c")
    shutil.copyfile(ROOT / "tests/native/sdk_events.c", distribution / "host-events.c")
    shutil.copyfile(ROOT / "tests/native/sdk_runtime.c", distribution / "host-runtime.c")
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
    for index, path in enumerate([ROOT / "src/runtime.c", ROOT / "src/execution.c", ROOT / "src/values.c", ROOT / "src/modules.c", ROOT / "src/host_module.c", ROOT / "src/events.c", *(source / name for name in SOURCES)]):
        obj = work / (path.stem + ".o")
        run(report, directory, "sdk-compile-" + str(index), [tree / "bin/cosmocc", *flags, "-c", path, "-o", obj],
            args.compile_timeout, work, environment)
        objects.append(obj)
    archive = distribution / "lib/libqaplet.a"
    run(report, directory, "sdk-archive", [tree / "bin/cosmoar", "rcsD", archive, *objects], args.compile_timeout, work, environment)
    # Compile only against the distributed public header and link the supplied archive.
    run(report, directory, "host-compile", [tree / "bin/cosmocc", "-Os", "-D_GNU_SOURCE", "-I" + str(distribution / "include"),
        "-c", distribution / "host-contract.c", "-o", work / "embed.o"], args.compile_timeout, work, environment)
    run(report, directory, "host-promises-compile", [tree / "bin/cosmocc", "-Os", "-D_GNU_SOURCE", "-I" + str(distribution / "include"),
        "-c", distribution / "host-promises.c", "-o", work / "promises.o"], args.compile_timeout, work, environment)
    run(report, directory, "host-modules-compile", [tree / "bin/cosmocc", "-Os", "-D_GNU_SOURCE", "-I" + str(distribution / "include"),
        "-c", distribution / "host-modules.c", "-o", work / "modules-host.o"], args.compile_timeout, work, environment)
    run(report, directory, "host-budgets-compile", [tree / "bin/cosmocc", "-Os", "-D_GNU_SOURCE", "-I" + str(distribution / "include"),
        "-c", distribution / "host-budgets.c", "-o", work / "budgets.o"], args.compile_timeout, work, environment)
    run(report, directory, "host-module-policy-compile", [tree / "bin/cosmocc", "-Os", "-D_GNU_SOURCE", "-I" + str(distribution / "include"),
        "-c", distribution / "host-module-policy.c", "-o", work / "module-policy.o"], args.compile_timeout, work, environment)
    run(report, directory, "host-host-modules-compile", [tree / "bin/cosmocc", "-Os", "-D_GNU_SOURCE", "-I" + str(distribution / "include"),
        "-c", distribution / "host-host-modules.c", "-o", work / "host-modules.o"], args.compile_timeout, work, environment)
    run(report, directory, "host-events-compile", [tree / "bin/cosmocc", "-Os", "-D_GNU_SOURCE", "-I" + str(distribution / "include"),
        "-c", distribution / "host-events.c", "-o", work / "events.o"], args.compile_timeout, work, environment)
    run(report, directory, "host-runtime-compile", [tree / "bin/cosmocc", "-Os", "-D_GNU_SOURCE", "-I" + str(distribution / "include"),
        "-c", distribution / "host-runtime.c", "-o", work / "runtime-host.o"], args.compile_timeout, work, environment)
    link_portable(args, report, directory, tree, helpers, "qaplet-host", [work / "embed.o", work / "promises.o", work / "modules-host.o", work / "budgets.o", work / "host-modules.o", work / "events.o", work / "module-policy.o", work / "runtime-host.o", archive],
                  work / "qaplet-host.com", work, environment)
    publish_ape(work, output, "qaplet-host", helpers, ape_helpers.source_cache(args.helper_source_cache.resolve(),
                consumption_purpose("build", args.controlled_probe)))
    artifact = audit(args, report, directory, tree, "qaplet-host", output)
    artifact["build_commands"] = identity(output / "qaplet-host.build-commands.log")
    report["artifacts"].append(artifact)
    cli_work = work / "cli"
    (cli_work / "tmp").mkdir(parents=True)
    cli_environment = dict(environment, TMPDIR=str(cli_work / "tmp"), BUILDLOG=str(output / "qaplet.build-commands.log"))
    run(report, directory, "cli-compile", [tree / "bin/cosmocc", "-Os", "-I" + str(distribution / "include"),
        "-c", ROOT / "src/cli.c", "-o", cli_work / "cli.o"], args.compile_timeout, cli_work, cli_environment)
    link_portable(args, report, directory, tree, helpers, "qaplet", [cli_work / "cli.o", archive],
                  cli_work / "qaplet.com", cli_work, cli_environment)
    publish_ape(cli_work, output, "qaplet", helpers, ape_helpers.source_cache(args.helper_source_cache.resolve(),
                consumption_purpose("build", args.controlled_probe)))
    cli_artifact = audit(args, report, directory, tree, "qaplet", output)
    cli_artifact["build_commands"] = identity(output / "qaplet.build-commands.log")
    report["artifacts"].append(cli_artifact)
    report["sdk"] = {"abi_version": 1, "recipe_id": report["sdk_recipe"]["sha256"],
        "components": [{"file": path.relative_to(output).as_posix(), **identity(path)}
                       for path in sorted(distribution.rglob("*")) if path.is_file()]}
    report["sdk"]["sha256"] = digest([{key: value for key, value in item.items() if key != "path"}
                                       for item in report["sdk"]["components"]])
    # Bind all delivered APE sidecars, CPU maps, notices, loader and SDK bytes.
    report["distribution"] = [{"file": path.relative_to(output).as_posix(), **identity(path)}
                              for path in sorted(output.rglob("*")) if path.is_file()]
    verify_inputs(args, report, directory, stage="after")
    recheck(report["native_toolchain"], original, component("cosmocc", purpose=consumption_purpose("build", args.controlled_probe)))
    if recipe() != files:
        raise BuildError("SDK recipe/source changed during build")
    report.update(status="pass", reason="Static SDK and audited CLI/C Host fat APEs built on Ubuntu; runtime evidence is separate")


def consume(args, report, directory):
    if not args.build_report or not args.artifact:
        raise BuildError("Supplied Ubuntu producer report and Host artifact are required", "blocked")
    producer = read_json(args.build_report)
    consume_identity(producer, report, consumption_purpose("build", args.controlled_probe))
    check_ci_commit(producer, report)
    if producer.get("report_kind") != "qaplet-sdk" or producer.get("scope") != "build" or producer.get("status") != "pass":
        raise BuildError("Successful qaplet-sdk producer required")
    selected = producer["artifacts"]
    if len(selected) != 2 or {item["name"] for item in selected} != {"qaplet-host", "qaplet"} or any(item["audit"]["status"] != "pass" for item in selected):
        raise BuildError("Exactly one audited public C Host and CLI artifact required")
    artifact = {**next(item for item in selected if item["name"] == "qaplet-host"), "path": str(args.artifact.resolve())}
    cli_path = args.cli_artifact.resolve() if args.cli_artifact else args.artifact.resolve().parent / "qaplet.com"
    cli_artifact = {**next(item for item in selected if item["name"] == "qaplet"), "path": str(cli_path)}
    if sha256(args.artifact) != artifact["sha256"] or args.artifact.stat().st_size != artifact["size_bytes"]:
        raise BuildError("Supplied Host bytes differ from producer")
    if cli_path.parent != args.artifact.resolve().parent:
        raise BuildError("Supplied CLI and Host must share the delivered distribution")
    if sha256(cli_path) != cli_artifact["sha256"] or cli_path.stat().st_size != cli_artifact["size_bytes"]:
        raise BuildError("Supplied CLI bytes differ from producer")
    def check_distribution():
        delivered = producer.get("distribution")
        if not delivered or len({item["file"] for item in delivered}) != len(delivered):
            raise BuildError("Unique complete delivered distribution identities required")
        for item in delivered:
            name = Path(item["file"])
            if name.is_absolute() or ".." in name.parts:
                raise BuildError("Distribution component must stay within its supplied directory")
            path = args.artifact.resolve().parent / name
            if sha256(path) != item["sha256"] or path.stat().st_size != item["size_bytes"]:
                raise BuildError("Supplied distribution component differs from producer: " + item["file"])
    check_distribution()
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
    headers(cli_path, [cli_path.with_suffix(".com.dbg"), cli_path.with_suffix(".aarch64.elf")])
    report.update(source=producer["source"], toolchain=producer["toolchain"], build=producer["build"],
                  sdk=producer["sdk"], sdk_recipe=producer["sdk_recipe"], distribution=producer["distribution"], producer_report=identity(args.build_report), artifacts=[artifact, cli_artifact])
    measured_host(args, report, directory)
    loader = select_loader(args, report)
    if os.name != "nt":
        args.artifact.chmod(args.artifact.stat().st_mode | 0o111)
        cli_path.chmod(cli_path.stat().st_mode | 0o111)
    for suffix, arguments, expected in HOST_CASES:
        arguments = [str(args.artifact.parent.resolve() / "sdk/modules") if value == "{modules}" else value for value in arguments]
        executed, stdout, stderr = run(report, directory, "qaplet-host." + suffix,
                                      [*loader, args.artifact, *arguments], args.timeout, expected=None)
        if suffix == "facts":
            expected = json.dumps({"probe": "qaplet-host", "arch": report["host"]["execution_arch"],
                                   "page_size": report["host"]["page_size"]}, separators=(",", ":")) + "\n"
        passed = executed["exit_code"] == 0 and not executed["timed_out"] and stdout == expected and not stderr
        required = not (suffix == "module-symlinks" and report["host"]["os"] == "Windows")
        # This supplied Cosmopolitan PE reports exit(77) as 19712 on the measured
        # Windows host; retain its actual exit and explicit fixture limitation.
        unavailable = not required and executed["exit_code"] in (77, 77 << 8) and not executed["timed_out"] and not stdout and stderr.startswith("file-symlink fixture unavailable: errno=")
        status = "pass" if passed else "blocked" if unavailable else "fail"
        report["cases"].append({"case_id": "qaplet-host." + suffix, "status": status, "required": required,
            "started_at": executed["started_at"], "finished_at": executed["finished_at"],
            "expected": {"exit_code": 0, "stdout": expected, "stderr": ""},
            "actual": {"exit_code": executed["exit_code"], "stdout": stdout, "stderr": stderr, "timed_out": executed["timed_out"]},
            "commands": [executed], "artifact": identity(args.artifact), "sdk_sha256": producer["sdk"]["sha256"],
            "timeout_seconds": args.timeout, "reason": "Actual Windows file-symlink fixture unavailable; no symlink behavior pass claimed" if unavailable else "Actual public SDK consumer executed supplied APE bytes",
            "scope_notes": "POSIX FIFO fixture executes on POSIX; Windows file and directory loading are covered without a FIFO" if suffix == "module-policy" and report["host"]["os"] == "Windows" else ""})
    for suffix, arguments, expected_exit, expected_stdout, stderr_contains in CLI_CASES:
        arguments = [value.replace("{modules}", str(args.artifact.parent.resolve() / "sdk/modules")) for value in arguments]
        executed, _, _ = run(report, directory, "qaplet." + suffix, [*loader, cli_path, *arguments], args.timeout, expected=None)
        # CLI strings are UTF-8 with explicit NUL bytes. The frozen M0 helper's
        # decoding heuristic is unsuitable here; compare the preserved raw logs.
        stdout = Path(executed["stdout_log"]).read_bytes()
        stderr = Path(executed["stderr_log"]).read_bytes()
        elapsed = (datetime.fromisoformat(executed["finished_at"]) - datetime.fromisoformat(executed["started_at"])).total_seconds()
        immediate = suffix not in ("eval-unsettled", "run-unsettled") or elapsed < 2
        passed = executed["exit_code"] == expected_exit and not executed["timed_out"] and immediate and stdout == expected_stdout.encode("utf-8") and (stderr_contains.encode("utf-8") in stderr if stderr_contains else not stderr)
        report["cases"].append({"case_id": "qaplet." + suffix, "status": "pass" if passed else "fail", "required": True,
            "started_at": executed["started_at"], "finished_at": executed["finished_at"],
            "expected": {"exit_code": expected_exit, "stdout": expected_stdout, "stderr_contains": stderr_contains,
                         "maximum_elapsed_seconds": 2 if suffix in ("eval-unsettled", "run-unsettled") else None},
            "actual": {"exit_code": executed["exit_code"], "stdout": stdout.decode("utf-8"), "stderr": stderr.decode("utf-8"),
                       "timed_out": executed["timed_out"], "elapsed_seconds": elapsed},
            "commands": [executed], "artifact": identity(cli_path), "sdk_sha256": producer["sdk"]["sha256"],
            "timeout_seconds": args.timeout, "reason": "Actual CLI process uses supplied public SDK, raw OS exit and raw UTF-8 logs"})
    report["artifacts"][1]["sha256_after"] = sha256(cli_path)
    if report["artifacts"][1]["sha256_after"] != cli_artifact["sha256"]:
        raise BuildError("CLI artifact changed during execution")
    report["artifacts"][0]["sha256_after"] = sha256(args.artifact)
    if report["artifacts"][0]["sha256_after"] != artifact["sha256"]:
        raise BuildError("Artifact changed during execution")
    for item in producer["sdk"]["components"]:
        path = args.artifact.parent / item["file"]
        if sha256(path) != item["sha256"] or path.stat().st_size != item["size_bytes"]:
            raise BuildError("Supplied SDK component changed during execution: " + item["file"])
    if recipe() != producer["sdk_recipe"]["files"]:
        raise BuildError("Consumer recipe differs from selected SDK producer")
    check_distribution()
    report["required_cases"] = [item["case_id"] for item in report["cases"] if item["required"]]
    report["unavailable_cases"] = [item["case_id"] for item in report["cases"] if item["status"] == "blocked"]
    report.update(status="pass" if all(item["status"] == "pass" or not item["required"] and item["status"] == "blocked" for item in report["cases"]) else "fail",
                  reason="Supplied SDK/CLI/Host identities and actual public process contracts checked; full M1 matrix not claimed")


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
    parser.add_argument("--cli-artifact", type=Path, help="Supplied qaplet.com beside Host (default: sibling qaplet.com)")
    parser.add_argument("--loader-dir", type=Path)
    parser.add_argument("--loader-manifest", type=Path)
    parser.add_argument("--expected-os")
    parser.add_argument("--expected-arch")
    parser.add_argument("--runner-label", default="local")
    parser.add_argument("--timeout", type=int, default=30)
    parser.add_argument("--compile-timeout", type=int, default=300)
    parser.add_argument("--cflag", action="append", default=[])
    parser.add_argument("--controlled-probe", action="store_true",
                        help="explicit candidate producer/consumer; ordinary SDK use requires pinned inputs")
    args = parser.parse_args()
    if not 1 <= args.timeout <= 300 or not 1 <= args.compile_timeout <= 900:
        parser.error("Invalid execution/compile timeout")
    directory = args.report.resolve().parent / "evidence" / str(uuid.uuid4())
    directory.mkdir(parents=True)
    report = {"schema_version": 1, "report_kind": "qaplet-sdk", "scope": args.operation, "report_id": directory.name,
              "status": "not_run", "reason": "", "started_at": now(), "host": host_facts(), "ci": ci_facts(),
              "commands": [], "cases": [], "artifacts": [], "input_reports": {}, "source": None, "toolchain": None,
              "build": None, "native_toolchain": None, "evidence_directory": str(directory),
              "controlled_probe": args.controlled_probe}
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
