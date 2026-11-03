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
import tempfile
import uuid
import zipfile

import ape_helpers
from build_ape import (BuildError, SOURCES, audit, headers, identity, link_portable,
                       native_toolchain, prepare_portable_helpers, publish_ape, run, verify_inputs)
from evidence import read_json
from foundation import ROOT, host_facts, now, sha256
from input_lock import component, consume_identity, consumption_purpose
from native_toolchain import recheck
from nightly_toolchain import write_json
from startup import check_ci_commit, ci_facts, measured_host, select_loader
from workflow_support import guard_evidence_outputs, workspace, write_evidence_report

# Source, public distribution name, object, recorded compile step. host-values.c
# is included by host-contract.c, so it is shipped/hashed but not compiled twice.
HOST_UNITS = (
    ("tests/native/sdk_wait.c", "host-wait.c", "wait.o", "host-wait-compile"),
    ("tests/native/sdk_host.c", "host-contract.c", "embed.o", "host-compile"),
    ("tests/native/sdk_promises.c", "host-promises.c", "promises.o", "host-promises-compile"),
    ("tests/native/sdk_modules.c", "host-modules.c", "modules-host.o", "host-modules-compile"),
    ("tests/native/sdk_budgets.c", "host-budgets.c", "budgets.o", "host-budgets-compile"),
    ("tests/native/sdk_host_modules.c", "host-host-modules.c", "host-modules.o", "host-host-modules-compile"),
    ("tests/native/sdk_events.c", "host-events.c", "events.o", "host-events-compile"),
    ("tests/native/sdk_module_policy.c", "host-module-policy.c", "module-policy.o", "host-module-policy-compile"),
    ("tests/native/sdk_runtime.c", "host-runtime.c", "runtime-host.o", "host-runtime-compile"),
    ("tests/native/sdk_resources.c", "host-resources.c", "resources.o", "host-resources-compile"),
    ("tests/native/sdk_stop.c", "host-stop.c", "stop.o", "host-stop-compile"),
    ("tests/native/sdk_close.c", "host-close.c", "close.o", "host-close-compile"),
    ("tests/native/sdk_m1_compat.c", "host-m1-compat.c", "m1-compat.o", "host-m1-compat-compile"),
    ("tests/native/sdk_m2_faults.c", "m2-faults.c", None, None),
    ("tests/fixtures/sdk/m1-qaplet.h", "m1-qaplet.h", None, None),
    ("tests/native/sdk_m2_runtime.c", "host-m2-runtime.c", "m2-runtime.o", "host-m2-runtime-compile"),
    ("tests/native/sdk_values.c", "host-values.c", None, None),
)

RECIPE = ("include/qaplet.h", "src/runtime_internal.h", "src/runtime.c", "src/loop.c", "src/execution.c", "src/values.c", "src/modules.c", "src/host_module.c", "src/events.c",
          "examples/embed.c", *(source for source, _, _, _ in HOST_UNITS),
          "scripts/sdk.py", "scripts/build_ape.py", "scripts/ape_helpers.py",
          "scripts/native_toolchain.py", "scripts/foundation.py", "scripts/startup.py",
          "scripts/evidence.py", "scripts/input_lock.py", "scripts/nightly_toolchain.py",
          "scripts/quickjs_source.py", "scripts/workflow_support.py",
          "tests/fixtures/modules/entry.mjs", "tests/fixtures/modules/dependency.js")
RECIPE += tuple(path.relative_to(ROOT).as_posix() for path in sorted((ROOT / "tests/fixtures/modules").rglob("*"))
                if path.is_file() and path.relative_to(ROOT).as_posix() not in RECIPE)
RECIPE += ("src/cli.c",)
RECIPE += ("examples/application.c", "examples/SDK-README.md")

# Same public-process tuple as CLI_CASES; package consumers use this catalog too.
EXAMPLE_CASES = (
    ("abi", ["--abi"], 0, "package ABI: prefixes/short structures/zero tails/flags/reserved; independent worker diagnostics: pass\n", ""),
    ("application", ["{modules}/application.mjs"], 0, "application: result=42; worker copied; stopped; closed; joined; resource released; destroyed\n", ""),
    ("rejection", ["--reject", "{modules}/application.mjs"], 1, "", "application: code=7; closed; joined; resource released; destroyed\n"),
    ("queue-full", ["--queue-full", "{modules}/application.mjs"], 1, "", "application: code=21; closed; joined; resource released; destroyed\n"),
    ("deadline", ["--deadline", "{modules}/application-loop.mjs"], 1, "", "application: code=17; closed; joined; resource released; destroyed\n"),
    ("missing", ["{modules}/application-absent.mjs"], 1, "", "application: code=14; closed; joined; resource released; destroyed\n"),
    ("arguments", [], 2, "", "Usage: qaplet-example"),
)
RUNTIME_METADATA = {"abi_version": 1, "capabilities": 491519, "defaults": {
    "engine_heap_bytes": 67108864, "engine_stack_bytes": 1048576, "native_storage_bytes": 16777216,
    "source_bytes": 4194304, "module_count": 256, "source_cache_bytes": 33554432,
    "execution_timeout_ms": 30000, "execution_dispatches": 100000,
    "event_queue_slots": 256, "event_payload_bytes": 1024, "bridge_value_bytes": 1048576,
    "host_arguments": 32, "pump_jobs": 256, "pump_events": 32}}
EXAMPLE_CASES += (("metadata", ["--metadata"], 0, json.dumps(RUNTIME_METADATA, separators=(",", ":")) + "\n", ""),)
HOST_CASES = (
    ("m2-wait", ["--m2-wait"], "M2 wait: finite timeout; explicit keepalive; idle cleanup: pass\n"),
    ("m2-wait-concurrent", ["--m2-wait-concurrent"], "M2 concurrent wait: copied events; last token; owner stop; real blocked interval; joined cleanup: pass\n"),
    ("m2-wait-tokens", ["--m2-wait-tokens"], "M2 keepalive: finite capacity; Runtime/generation identity; stale/duplicate release; native handles; close invalidation: pass\n"),
    ("m2-wait-deadline", ["--m2-wait-deadline"], "M2 wait deadline: pending entry keeps absolute execution ledger across waits; terminal cleanup starts before close: pass\n"),
    ("m2-wait-budget", ["--m2-wait-budget"], "M2 wait budget: redundant cross-thread wakes preserve cumulative timeout; hints are not work; pump never waits: pass\n"),
    ("m2-wait-events", ["--m2-wait-events"], "M2 waiting events: ready-first bounded drive; copied FIFO/rejections; last-token accepted work; complete jobs; idle result; full-queue stop: pass\n"),
    ("m2-close", ["--m2-close"], "M2 close: STOP_REQUESTED/STOPPED/FAILED; first cause retained; CLOSED never revived; quiesce retry: pass\n"),
    ("m1-frozen-header", ["--m1-compat", "{modules}"], "M1 frozen header: new SDK link; values/modules/events; pending close; old ABI/errors/reserved: pass\n"),
    ("m2-drive", ["--m2-drive"], "M2 drive: ready jobs; independent progress/reason; idle result retained; unsettled entry; no implicit close: pass\n"),
    ("m2-configuration", ["--m2-configuration"], "M2 configuration: explicit capabilities; finite copied defaults; sizes/version/tails; invalid combinations: pass\n"),
    ("m2-owner", ["--m2-owner"], "M2 owner: foreign calls refused; pump/run_once/run guard; safe POD/control/setter; CLOSED query: pass\n"),
    ("m2-creation", ["--m2-creation"], "M2 creation: 256 public heap/native budgets; unpublished null failures; fresh recovery and cleanup: pass\n"),
    ("m2-runtime", ["--m2-runtime"], "M2 runtime: explicit empty instance; idle drive; CLOSED/native quiescent; destroy: pass\n"),
    ("close-states", ["--close-states"], "close states: normal/error/PENDING/STOP_REQUESTED/STOPPED/FAILED; reentry; raw cleanup; retained handle: pass\n"),
    ("close-concurrent", ["--close-concurrent"], "close concurrent: producer overlap observed; post/snapshot/stop stable; CLOSED calls drained before destroy: pass\n"),
    ("close-competition", ["--close-competition"], "close competition: actual allocation refusal and worker stop in both recorded orders; first cause survives close: pass\n"),
    ("runtime", ["--runtime", "{modules}"], "runtime snapshot: fixed read-only argv/cwd; public factory: pass\n"),
    ("cli-contract", ["--cli-contract", "{modules}"], "CLI contrast: shared Script/ESM/cache/rejection/results/budgets; SDK pending preserved: pass\n"),
    ("resource-storage", ["--resource-storage"], "resource storage: actual SDK source-copy refusal permanent; bounded accounting; fresh recovery: pass\n"),
    ("resource-configuration", ["--resource-configuration"], "resource configuration: all finite defaults; copied fixed capacities; overflow rejection: pass\n"),
    ("resource-bridge", ["--resource-bridge"], "resource bridge: configured byte/argument limits; preflight reuse; copied exact boundary; larger finite arguments: pass\n"),
    ("resource-slices", ["--resource-slices"], "resource slices: configured jobs/events quotas; fixed queue; public event accounting: pass\n"),
    ("resource-refusals", ["--resource-refusals"], "resource refusals: metered rejections/registration; caught bridge/engine refusal; 32 factory limits and recovery: pass\n"),
    ("stop", ["--stop"], "stop: queue-independent acceptance; immediate refusal; owner confirmation; raw jobs/events close: pass\n"),
    ("stop-running", ["--stop-running"], "stop running: measured JS CPU loops; async interruption; C callback barrier; idle PENDING: pass\n"),
    ("stop-ordering", ["--stop-ordering"], "stop ordering: claimed event in C; saturated acceptance barrier; unclaimed work dropped; no retry: pass\n"),
    ("stop-causes", ["--stop-causes"], "stop causes: forced stop/deadline record orders; dispatch/allocation preserved; repeat FAILED/CLOSED: pass\n"),
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
    ("args-empty-file", ["run", ""], 2, "", "qaplet: INVALID_ARGUMENT"),
    ("args-missing-separator", ["run", "{modules}/entry.mjs", "extra"], 2, "", "qaplet: INVALID_ARGUMENT"),
    ("args-invalid-budget", ["--timeout-ms", "-1", "eval", "--source", "42"], 2, "", "qaplet: INVALID_ARGUMENT"),
    ("args-overflow-budget", ["--max-dispatches", "18446744073709551616", "eval", "--source", "42"], 2, "", "qaplet: INVALID_ARGUMENT"),
)


def recipe():
    return [{"path": name, "sha256": sha256(ROOT / name), "size_bytes": (ROOT / name).stat().st_size}
            for name in RECIPE]


def digest(value):
    return hashlib.sha256(json.dumps(value, sort_keys=True, separators=(",", ":")).encode()).hexdigest()


def package_consume(args, report, directory):
    if report["host"]["os"] != "Linux" or platform.freedesktop_os_release().get("ID") != "ubuntu":
        raise BuildError("SDK package compilation runs on Ubuntu only", "blocked")
    producer = read_json(args.build_report)
    if not producer.get("package"):
        raise BuildError("A sealed SDK package containing the complete public C application is required")
    consume_identity(producer, report, consumption_purpose("build", args.controlled_probe))
    check_ci_commit(producer, report)
    if producer.get("status") != "pass" or producer.get("report_kind") != "qaplet-sdk" or producer.get("scope") != "build":
        raise BuildError("Successful SDK producer required")
    supplied = args.artifact.resolve().parent
    check_package(producer, supplied)
    output = args.output_dir.resolve()
    if output.exists() and any(output.iterdir()):
        raise BuildError("External consumer output directory must be new or empty")
    output.mkdir(parents=True, exist_ok=True)
    # This is a one-use real application fixture, not a reusable input/cache.
    external = Path(tempfile.mkdtemp(prefix="qaplet-sdk-consumer-"))
    if external.is_relative_to(ROOT) or external.is_relative_to(Path(workspace(ROOT)["main_checkout"])):
        raise BuildError("Package consumer must be outside the source tree")
    report["external_consumer"] = {"directory": str(external), "package": identity(supplied / producer["package"]["file"]),
        "include_directories": [str(external / "sdk/include")], "private_headers": False,
        "archive": str(external / "sdk/lib/libqaplet.a"), "source": str(external / "sdk/application.c")}
    with zipfile.ZipFile(supplied / producer["package"]["file"]) as archive:
        archive.extractall(external)
    check_package(producer, external, check_archive=False)
    original = verify_inputs(args, report, directory)
    tree = native_toolchain(args, report, directory, original)
    helpers = prepare_portable_helpers(args, report, directory, tree)
    report.update(sdk=producer["sdk"], sdk_recipe=producer["sdk_recipe"], producer_report=identity(args.build_report),
                  package=producer["package"])
    if args.operation == "fault-consume":
        compile_m2_faults(args, report, directory, tree, helpers, external / "sdk", external / "build", output)
    else:
        compile_example(args, report, directory, tree, helpers, external / "sdk", external / "build", output)
    measured_host(args, report, directory)
    loader = select_loader(args, report)
    if args.operation == "fault-consume":
        artifact = output / "qaplet-m2-faults.com"
        if os.name != "nt": artifact.chmod(artifact.stat().st_mode | 0o111)
        executed, stdout, stderr = run(report, directory, "qaplet-m2-faults.system-boundary",
            [*loader, artifact], args.timeout, expected=None)
        expected = "controlled M2: four system initialization refusals; resources balanced; PENDING/CLOSED whole-drive guard: pass\ncontrolled M2 wait: pre-sleep handoff; accepted send failure; poll/recv/clock faults; first cause; socket rollback; uncertain close retained; reused fd untouched: pass\n"
        passed = executed["exit_code"] == 0 and not executed["timed_out"] and stdout == expected and not stderr
        report["cases"].append({"case_id": "qaplet-m2-faults.system-boundary", "status": "pass" if passed else "fail",
            "required": True, "controlled_system_fixture": True, "command": executed,
            "expected": {"stdout": expected, "stderr": "", "exit_code": 0}})
    else:
        example_cases(args, report, directory, output / "qaplet-example.com", external / "sdk/modules", loader)
    check_package(producer, external, check_archive=False)
    verify_inputs(args, report, directory, stage="after")
    recheck(report["native_toolchain"], original, component("cosmocc", purpose=consumption_purpose("build", args.controlled_probe)))
    report.update(status="pass" if all(case["status"] == "pass" for case in report["cases"]) else "fail",
                  reason="Actual sealed package extracted outside source tree, compiled using supplied public header/archive, audited and executed")


def relative_identity(path, root):
    return {"file": path.relative_to(root).as_posix(), "sha256": sha256(path), "size_bytes": path.stat().st_size}


def seal_package(report, output):
    distribution = output / "sdk"
    manifest = {"schema_version": 1, "name": "qaplet-embed-sdk", "version": "0.1.0", **RUNTIME_METADATA,
        "capability_names": ["lifecycle", "sync-script", "scalar-result", "owner-thread", "pod-snapshot", "resource-base",
            "copied-values", "promise-jobs", "file-modules", "execution-budgets", "module-policy", "host-modules",
            "pod-events", "resource-limits", "cooperative-stop", "runtime-snapshot", "m2-runtime", "m2-wait"],
        "targets": [{"architecture": arch, **relative_identity(distribution / path, output)}
            for arch, path in (("x86_64", "lib/libqaplet.a"), ("aarch64", "lib/.aarch64/libqaplet.a"))],
        "input_id": report["input_lock"]["input_id"], "sdk_recipe_id": report["sdk_recipe"]["sha256"],
        "source_commit": report["sdk_recipe"]["git_commit"], "engine": report["source"],
        "toolchain_archive": report["toolchain"]["archive"],
        "artifacts": [{"name": item["name"], "file": Path(item["path"]).name,
                       "sha256": item["sha256"], "size_bytes": item["size_bytes"]} for item in report["artifacts"]],
        "components": [relative_identity(path, output) for path in sorted(distribution.rglob("*")) if path.is_file()]}
    write_json(distribution / "manifest.json", manifest)
    members = [relative_identity(path, output) for path in sorted(output.rglob("*")) if path.is_file()]
    path = output / "qaplet-sdk.zip"
    with zipfile.ZipFile(path, "w", compression=zipfile.ZIP_DEFLATED) as archive:
        for item in members:
            entry = zipfile.ZipInfo(item["file"], date_time=(1980, 1, 1, 0, 0, 0))
            entry.compress_type = zipfile.ZIP_DEFLATED
            entry.external_attr = 0o100644 << 16
            archive.writestr(entry, (output / item["file"]).read_bytes())
    report["package"] = {**relative_identity(path, output), "format": "zip", "members": members,
                         "manifest": relative_identity(distribution / "manifest.json", output)}


def check_package(producer, supplied, check_archive=True):
    package = producer["package"]
    members = package["members"]
    expected = {item["file"]: item for item in members}
    if len(expected) != len(members) or not members:
        raise BuildError("Unique complete package members required")
    if producer.get("distribution") and {item["file"]: {key: item[key] for key in ("file", "sha256", "size_bytes")}
            for item in producer["distribution"] if item["file"] != "qaplet-sdk.zip"} != expected:
        raise BuildError("Package inventory differs from delivered distribution")
    if digest({key: value for key, value in producer["sdk_recipe"].items()
               if key not in ("git_commit", "working_tree_dirty", "sha256")}) != producer["sdk_recipe"]["sha256"]:
        raise BuildError("SDK package recipe identity is inconsistent")
    sdk_components = [{key: item[key] for key in ("file", "sha256", "size_bytes")} for item in producer["sdk"]["components"]]
    if sdk_components != [item for name, item in expected.items() if name.startswith("sdk/")] or digest(sdk_components) != producer["sdk"]["sha256"]:
        raise BuildError("SDK package aggregate differs from supplied public components")
    for name in expected:
        path = Path(name)
        if path.is_absolute() or ".." in path.parts or "\\" in name or ":" in name:
            raise BuildError("Unsafe SDK package member")
    if check_archive:
        path = supplied / package["file"]
        if package["file"] != "qaplet-sdk.zip" or relative_identity(path, supplied) != {key: package[key] for key in ("file", "sha256", "size_bytes")}:
            raise BuildError("SDK package archive differs from producer")
        with zipfile.ZipFile(path) as archive:
            if archive.namelist() != list(expected) or archive.testzip() is not None:
                raise BuildError("SDK package members differ from complete distribution")
            for name, item in expected.items():
                data = archive.read(name)
                if len(data) != item["size_bytes"] or hashlib.sha256(data).hexdigest() != item["sha256"]:
                    raise BuildError("SDK package member differs from producer: " + name)
    for name, item in expected.items():
        if relative_identity(supplied / name, supplied) != item:
            raise BuildError("Extracted SDK package member differs from producer: " + name)
    manifest = read_json(supplied / "sdk/manifest.json")
    if {key: manifest[key] for key in RUNTIME_METADATA} != RUNTIME_METADATA or manifest["sdk_recipe_id"] != producer["sdk_recipe"]["sha256"]:
        raise BuildError("SDK manifest ABI/capabilities/defaults/recipe mismatch")
    if manifest["input_id"] != producer["input_lock"]["input_id"] or manifest["source_commit"] != producer["sdk_recipe"]["git_commit"] or manifest["engine"] != producer["source"] or manifest["toolchain_archive"] != producer["toolchain"]["archive"]:
        raise BuildError("SDK manifest source/toolchain identity mismatch")
    if manifest["components"] != [item for name, item in expected.items() if name.startswith("sdk/") and name != "sdk/manifest.json"]:
        raise BuildError("SDK manifest public component inventory mismatch")
    if manifest["artifacts"] != [{"name": item["name"], "file": item["name"] + ".com", "sha256": item["sha256"],
                                  "size_bytes": item["size_bytes"]} for item in producer["artifacts"]]:
        raise BuildError("SDK manifest executable identities mismatch")
    if manifest["targets"] != [{"architecture": arch, **expected[name]} for arch, name in
            (("x86_64", "sdk/lib/libqaplet.a"), ("aarch64", "sdk/lib/.aarch64/libqaplet.a"))]:
        raise BuildError("Both target static archives must match manifest")


def compile_m2_faults(args, report, directory, tree, helpers, sdk, work, output):
    """Separate supplied-archive system fixture; never a normal product case."""
    (work / "tmp").mkdir(parents=True)
    source = sdk / "m2-faults.c"
    symbols = ("pthread_mutex_init", "pthread_mutex_destroy", "pthread_condattr_init", "pthread_condattr_destroy",
               "pthread_condattr_setclock", "pthread_cond_init", "pthread_cond_destroy", "clock_gettime",
               "socket", "accept", "close", "poll", "send", "recv")
    environment = dict(os.environ, TMPDIR=str(work / "tmp"), SOURCE_DATE_EPOCH="0", LC_ALL="C", TZ="UTC",
                       BUILDLOG=str(output / "qaplet-m2-faults.build-commands.log"))
    report["external_consumer"]["source"] = str(source)
    report["controlled_system_fixture"] = {"source": identity(source), "wrapped_symbols": list(symbols),
        "scope": "system initialization, deterministic wait handoff, accepted wake failure, clock/wait faults and whole-call guard; not native OS coverage"}
    run(report, directory, "m2-faults-compile", [tree / "bin/cosmocc", "-Os", "-D_GNU_SOURCE",
        "-I" + str(sdk / "include"), "-ffile-prefix-map=" + str(sdk) + "=sdk",
        "-c", source, "-o", work / "faults.o"], args.compile_timeout, work, environment)
    link_portable(args, report, directory, tree, helpers, "qaplet-m2-faults", [work / "faults.o", sdk / "lib/libqaplet.a"],
        work / "qaplet-m2-faults.com", work, environment,
        libraries=(*("-Wl,--wrap=" + name for name in symbols), "-lm", "-lpthread"))
    publish_ape(work, output, "qaplet-m2-faults", helpers, ape_helpers.source_cache(args.helper_source_cache.resolve(),
                consumption_purpose("build", args.controlled_probe)))
    artifact = audit(args, report, directory, tree, "qaplet-m2-faults", output)
    artifact["build_commands"] = identity(output / "qaplet-m2-faults.build-commands.log")
    report["artifacts"].append(artifact)


def compile_example(args, report, directory, tree, helpers, sdk, work, output):
    (work / "tmp").mkdir(parents=True)
    environment = dict(os.environ, TMPDIR=str(work / "tmp"), SOURCE_DATE_EPOCH="0", LC_ALL="C", TZ="UTC",
                       BUILDLOG=str(output / "qaplet-example.build-commands.log"))
    run(report, directory, "example-compile", [tree / "bin/cosmocc", "-Os", "-D_GNU_SOURCE",
        "-I" + str(sdk / "include"), "-ffile-prefix-map=" + str(sdk) + "=sdk",
        "-c", sdk / "application.c", "-o", work / "application.o"], args.compile_timeout, work, environment)
    link_portable(args, report, directory, tree, helpers, "qaplet-example", [work / "application.o", sdk / "lib/libqaplet.a"],
                  work / "qaplet-example.com", work, environment)
    shutil.copyfile(work / "qaplet-example.original-packer.com", directory / "qaplet-example.original-packer.com")
    publish_ape(work, output, "qaplet-example", helpers, ape_helpers.source_cache(args.helper_source_cache.resolve(),
                consumption_purpose("build", args.controlled_probe)))
    artifact = audit(args, report, directory, tree, "qaplet-example", output)
    artifact["build_commands"] = identity(output / "qaplet-example.build-commands.log")
    report["artifacts"].append(artifact)


def example_cases(args, report, directory, artifact, modules, loader):
    if os.name != "nt": artifact.chmod(artifact.stat().st_mode | 0o111)
    for suffix, arguments, expected_exit, expected_stdout, stderr_contains in EXAMPLE_CASES:
        arguments = [value.replace("{modules}", str(modules)) for value in arguments]
        executed, _, _ = run(report, directory, "qaplet-example." + suffix,
                             [*loader, artifact, *arguments], args.timeout, expected=None)
        stdout = Path(executed["stdout_log"]).read_bytes()
        stderr = Path(executed["stderr_log"]).read_bytes()
        passed = executed["exit_code"] == expected_exit and not executed["timed_out"] and stdout == expected_stdout.encode() and (
            stderr_contains.encode() in stderr if stderr_contains else not stderr)
        report["cases"].append({"case_id": "qaplet-example." + suffix, "required": True,
            "status": "pass" if passed else "fail", "started_at": executed["started_at"], "finished_at": executed["finished_at"],
            "expected": {"exit_code": expected_exit, "stdout": expected_stdout, "stderr_contains": stderr_contains},
            "actual": {"exit_code": executed["exit_code"], "stdout": stdout.decode(), "stderr": stderr.decode(), "timed_out": executed["timed_out"]},
            "commands": [executed], "artifact": identity(artifact), "sdk_sha256": report["sdk"]["sha256"],
            "timeout_seconds": args.timeout, "reason": "Complete public-only C application consumed supplied SDK"})


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
    shutil.copyfile(ROOT / "examples/application.c", distribution / "application.c")
    shutil.copyfile(ROOT / "examples/SDK-README.md", distribution / "README.md")
    for source_name, delivered, _, _ in HOST_UNITS:
        shutil.copyfile(ROOT / source_name, distribution / delivered)
    shutil.copytree(ROOT / "tests/fixtures/modules", distribution / "modules")
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
    for index, path in enumerate([ROOT / "src/runtime.c", ROOT / "src/loop.c", ROOT / "src/execution.c", ROOT / "src/values.c", ROOT / "src/modules.c", ROOT / "src/host_module.c", ROOT / "src/events.c", *(source / name for name in SOURCES)]):
        obj = work / (path.stem + ".o")
        run(report, directory, "sdk-compile-" + str(index), [tree / "bin/cosmocc", *flags, "-c", path, "-o", obj],
            args.compile_timeout, work, environment)
        objects.append(obj)
    archive = distribution / "lib/libqaplet.a"
    run(report, directory, "sdk-archive", [tree / "bin/cosmoar", "rcsD", archive, *objects], args.compile_timeout, work, environment)
    # Compile only against the distributed public header and link the supplied archive.
    host_objects = []
    for _, delivered, object_name, step in HOST_UNITS:
        if object_name is None:
            continue
        obj = work / object_name
        run(report, directory, step, [tree / "bin/cosmocc", "-Os", "-D_GNU_SOURCE", "-I" + str(distribution / "include"),
            "-c", distribution / delivered, "-o", obj], args.compile_timeout, work, environment)
        host_objects.append(obj)
    link_portable(args, report, directory, tree, helpers, "qaplet-host", [*host_objects, archive],
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
    compile_example(args, report, directory, tree, helpers, distribution, work / "example", output)
    seal_package(report, output)
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
    report.update(status="pass", reason="Static SDK ZIP and audited CLI/C Host/application fat APEs built on Ubuntu; runtime evidence is separate")


def consume(args, report, directory):
    if not args.build_report or not args.artifact:
        raise BuildError("Supplied Ubuntu producer report and Host artifact are required", "blocked")
    producer = read_json(args.build_report)
    consume_identity(producer, report, consumption_purpose("build", args.controlled_probe))
    check_ci_commit(producer, report)
    if producer.get("report_kind") != "qaplet-sdk" or producer.get("scope") != "build" or producer.get("status") != "pass":
        raise BuildError("Successful qaplet-sdk producer required")
    selected = producer["artifacts"]
    if len(selected) != 3 or {item["name"] for item in selected} != {"qaplet-host", "qaplet", "qaplet-example"} or any(item["audit"]["status"] != "pass" for item in selected):
        raise BuildError("Exactly one audited public C Host, CLI and complete example artifact required")
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
    check_package(producer, args.artifact.resolve().parent)
    example_path = args.artifact.resolve().parent / "qaplet-example.com"
    example_artifact = {**next(item for item in selected if item["name"] == "qaplet-example"), "path": str(example_path)}
    if sha256(example_path) != example_artifact["sha256"] or example_path.stat().st_size != example_artifact["size_bytes"]:
        raise BuildError("Supplied example bytes differ from producer")
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
    headers(example_path, [example_path.with_suffix(".com.dbg"), example_path.with_suffix(".aarch64.elf")])
    report.update(source=producer["source"], toolchain=producer["toolchain"], build=producer["build"],
                  sdk=producer["sdk"], sdk_recipe=producer["sdk_recipe"], distribution=producer["distribution"], package=producer["package"], producer_report=identity(args.build_report), artifacts=[artifact, cli_artifact, example_artifact])
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
    example_cases(args, report, directory, example_path, args.artifact.parent.resolve() / "sdk/modules", loader)
    report["artifacts"][2]["sha256_after"] = sha256(example_path)
    if report["artifacts"][2]["sha256_after"] != example_artifact["sha256"]:
        raise BuildError("Example artifact changed during execution")
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
    check_package(producer, args.artifact.resolve().parent)
    report["required_cases"] = [item["case_id"] for item in report["cases"] if item["required"]]
    report["unavailable_cases"] = [item["case_id"] for item in report["cases"] if item["status"] == "blocked"]
    lifetime = next(item for item in report["cases"] if item["case_id"] == "qaplet-host.lifetime")
    minimum = 1000 if (report["host"]["os"], report["host"]["execution_arch"]) == ("Linux", "x86_64") else 100
    report["lifecycle"] = {"case_id": lifetime["case_id"], "minimum_cycles": minimum,
        "completed_cycles": 1000 if lifetime["status"] == "pass" else 0,
        "status": lifetime["status"], "measurement": "Actual public Host create/close/destroy loop; exact raw output checked"}
    report.update(status="pass" if all(item["status"] == "pass" or not item["required"] and item["status"] == "blocked" for item in report["cases"]) else "fail",
                  reason="Supplied SDK/CLI/Host application contracts and lifecycle loop checked; native matrix and Full remain separate")


def main():
    cache = Path(workspace(ROOT)["cache_root"])
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("operation", choices=("build", "run", "package-consume", "fault-consume", "matrix"))
    parser.add_argument("--profile", choices=("fast", "cross"), default="cross")
    parser.add_argument("--input-report", type=Path, action="append", default=[])
    parser.add_argument("--input-dir", type=Path, action="append", default=[])
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
    if args.operation != "build":
        try:
            guard_evidence_outputs([args.report, directory, *([args.output_dir] if args.operation in ("package-consume", "fault-consume") else [])],
                [args.build_report, args.loader_manifest, *args.input_report],
                [*([args.artifact.resolve().parent] if args.artifact else []),
                 *([args.loader_dir] if args.loader_dir else []), *args.input_dir],
                [args.artifact, args.cli_artifact])
        except ValueError as error:
            parser.error(str(error))
    directory.mkdir(parents=True)
    report = {"schema_version": 1, "report_kind": "qaplet-sdk", "scope": args.operation, "report_id": directory.name,
              "status": "not_run", "reason": "", "started_at": now(), "host": host_facts(), "ci": ci_facts(),
              "commands": [], "cases": [], "artifacts": [], "input_reports": {}, "source": None, "toolchain": None,
              "build": None, "native_toolchain": None, "evidence_directory": str(directory),
              "controlled_probe": args.controlled_probe}
    try:
        if args.operation == "matrix":
            from sdk_matrix import matrix
            matrix(args, report, directory)
        else:
            {"build": build, "run": consume, "package-consume": package_consume, "fault-consume": package_consume}[args.operation](args, report, directory)
    except BuildError as error:
        report.update(status=error.status, reason=str(error))
    except (OSError, ValueError, KeyError, TypeError, zipfile.BadZipFile) as error:
        report.update(status="fail", reason=str(error))
    report["finished_at"] = now()
    try:
        if args.operation == "build":
            write_json(args.report, report)
        else:
            write_evidence_report(args.report, report)
    except ValueError as error:
        print(json.dumps({"status": "fail", "reason": str(error), "validation_reason": report["reason"],
                          "report": str(args.report), "report_written": False}))
        return 1
    print(json.dumps({"status": report["status"], "reason": report["reason"], "report": str(args.report), "report_written": True}))
    return {"pass": 0, "fail": 1, "blocked": 2}[report["status"]]


if __name__ == "__main__":
    sys.exit(main())
