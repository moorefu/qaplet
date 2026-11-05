#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Risk-directed CI selection; plans are never execution evidence."""
import argparse
import json
import os
from pathlib import Path
import re
import sys

from workflow_support import git_read, identity, now, read_json, write_json

ROOT = Path(__file__).resolve().parents[1]
NATIVE = [
    {"runner": "ubuntu-24.04", "os": "Linux", "arch": "x86_64"},
    {"runner": "windows-2022", "os": "Windows", "arch": "x86_64"},
    {"runner": "macos-15-intel", "os": "Darwin", "arch": "x86_64"},
    {"runner": "macos-15", "os": "Darwin", "arch": "aarch64"},
    {"runner": "ubuntu-24.04-arm", "os": "Linux", "arch": "aarch64"},
]


def require(condition, reason):
    if not condition:
        raise ValueError(reason)


def native_targets(path):
    value = read_json(path)
    rows = value["native_matrix"]["include"]
    require(value.get("report_kind") == "ci-tier-plan" and value.get("status") == "pass" and
            value.get("run_cross") is True and rows, "Actual selected Cross plan required")
    require(all(row in NATIVE for row in rows) and len({row["runner"] for row in rows}) == len(rows),
            "Selected native targets must be unique declared OS/architectures")
    require(not value["run_full"] or rows == NATIVE, "Full cannot narrow the declared native matrix")
    return [(row["os"], row["arch"]) for row in rows]


def check_scoped_job(workflow, name, scope):
    job = workflow["jobs"][name]
    steps = job["steps"]
    planner = [step for step in steps if step.get("id") == "scope"]
    directory = {"foundation": "scope", "workflow_tools": "workflow", "svc_scm": "svc-scm", "svc_job": "svc-job"}[scope]
    require(len(planner) == 1 and planner[0].get("uses") == "./.github/actions/ci-scope" and "if" not in planner[0] and
            planner[0].get("with") == {"report": "out/" + directory + "/scope.json"},
            "Scope selector must always execute the shared fail-closed policy")
    require("if" not in job, "Existing required-check producer must remain created for every scope")
    require(any(step.get("uses", "").startswith("actions/checkout@") and
                step.get("with", {}).get("fetch-depth") == 0 for step in steps), "Changed paths require complete Git history")
    runs = [step for step in steps if "run" in step and step.get("id") != "scope" and
            step.get("name") != "Record unrelated scope without claiming execution"]
    require(all(step.get("if") == "${{ steps.scope.outputs." + scope + " == 'true' }}" for step in runs),
            "Only affected contracts may execute after the scope selector")
    require(workflow.get("concurrency") == {"group": "${{ github.workflow }}-${{ github.event_name == 'workflow_dispatch' && github.run_id || github.ref }}",
            "cancel-in-progress": "${{ github.event_name == 'push' || github.event_name == 'pull_request' }}"},
            "Only superseded ordinary push/PR runs may be cancelled")
    return [step["run"] for step in runs]


def check_scopes(root):
    action = read_json(root / ".github/actions/ci-scope/action.yml")
    steps = action.get("runs", {}).get("steps", [])
    require(action.get("runs", {}).get("using") == "composite" and len(steps) == 1 and steps[0].get("id") == "plan" and
            steps[0].get("run") == 'python scripts/ci_tiers.py plan --event-name "$CI_EVENT_NAME" --event-json "$GITHUB_EVENT_PATH" --ref "$CI_REF" --github-output "$GITHUB_OUTPUT" --report "$CI_SCOPE_REPORT"' and
            "if" not in steps[0], "Scope action must execute only the shared selector")
    for name, job, scope in (("foundation", "bootstrap", "foundation"), ("workflow-tools", "workflow", "workflow_tools"),
                             ("svc-scm", "build", "svc_scm"), ("svc-job", "build", "svc_job")):
        check_scoped_job(read_json(root / ".github/workflows" / (name + ".yml")), job, scope)
    return "pass"


def changed_paths(args, event):
    if args.changed_paths:
        paths = read_json(args.changed_paths)
    elif args.event_name == "pull_request":
        pull = event["pull_request"]
        paths = git_read(ROOT, "diff", "--name-only", "-z", pull["base"]["sha"] + "..." + pull["head"]["sha"]).decode().split("\0")
    elif args.event_name == "push" and event.get("before") and event.get("after"):
        before, after = event["before"], event["after"]
        require(all(re.fullmatch(r"[0-9a-f]{40}", value) for value in (before, after)), "Invalid push commit")
        paths = git_read(ROOT, *( ["ls-tree", "-r", "--name-only", "-z", after] if before == "0" * 40 else
                                 ["diff", "--name-only", "-z", before, after])).decode().split("\0")
    elif args.event_name in ("push", "pull_request") and not args.ref.startswith("refs/tags/"):
        raise ValueError("Actual changed paths unavailable; refuse to downgrade unknown risk")
    else:
        paths = []
    require(isinstance(paths, list) and all(isinstance(path, str) for path in paths), "Changed paths must be a JSON string array")
    paths = sorted(set(path for path in paths if path))
    require(all(not path.startswith(("/", "\\")) and ".." not in path.replace("\\", "/").split("/") for path in paths), "Invalid changed path")
    return paths


def classify(paths):
    """Explicit dependency closure. Unknown paths select everything, never Fast."""
    scopes = dict.fromkeys(("m0", "foundation", "svc_scm", "svc_job", "workflow_tools"), False)
    rank, reasons, windows_only = 0, [], bool(paths)
    shared = {"scripts/build_ape.py", "scripts/ape_helpers.py", "scripts/native_toolchain.py",
              "scripts/input_lock.py", "scripts/nightly_toolchain.py", "scripts/fetch_locked_toolchain.py",
              "scripts/workflow_support.py", "scripts/evidence.py", "scripts/foundation.py"}
    for path in paths:
        if path.startswith(("docs/", ".scratch/")) or path in ("README.md", "GLOSSARY.md", "LICENSE", "AGENTS.md"):
            continue
        windows_only &= path in ("tests/native/sdk_process_winchild.c", "scripts/m2_process_pe.py")
        if path in shared or path.startswith(("toolchains/", "patches/", "guests/", "third_party/")):
            scopes = dict.fromkeys(scopes, True)
            rank = max(rank, 3)
            reasons.append("toolchain/build/ABI dependency")
        elif "svc_scm" in path or "svc-scm" in path:
            scopes["svc_scm"] = True
        elif "svc_job" in path or "svc-job" in path:
            scopes["svc_job"] = True
        elif path in (".github/workflows/foundation.yml", "tests/contracts/test_foundation.py"):
            scopes["foundation"] = True
        elif path in ("src/execution.c", "src/values.c"):
            scopes["m0"] = True
            rank = max(rank, 1)
        elif path.startswith(("src/", "include/", "tests/native/")) or path.startswith("scripts/m2_"):
            scopes["m0"] = True
            rank = max(rank, 2)
            reasons.append("public ABI or native OS behavior")
        elif path.startswith(("examples/", "tests/fixtures/modules/")):
            scopes["m0"] = True
            rank = max(rank, 1)
        elif path.startswith(("scripts/", "tests/contracts/", ".github/workflows/")) or path in ("Makefile", ".gitignore"):
            scopes["workflow_tools"] = True
            scopes["m0"] = True
            rank = max(rank, 3)
            reasons.append("verification/tooling contract")
        else:
            scopes = dict.fromkeys(scopes, True)
            rank = 3
            reasons.append("unknown change risk")
    return rank, scopes, sorted(set(reasons)), windows_only


def plan(args):
    event = read_json(args.event_json)
    paths = changed_paths(args, event)
    sensitive = [path for path in paths if not path.startswith(("docs/", ".scratch/")) and path not in ("README.md", "GLOSSARY.md", "LICENSE", "AGENTS.md")]
    name, ref = args.event_name, args.ref
    risk, scopes, risks, windows_only = classify(paths)
    tier = ("fast", "fast", "cross", "full")[risk]
    reason = ", ".join(risks) or "ordinary scoped push"
    if name == "pull_request":
        if scopes["m0"] and tier == "fast":
            tier, reason = "cross", "integration pull request native contracts"
    elif (name == "push" and ref.startswith("refs/tags/")) or name == "schedule":
        tier, reason = "full", "scheduled or release candidate Full"
        scopes["m0"] = True
        windows_only = False
    elif name == "push":
        pass
    elif name == "workflow_dispatch":
        requested = event.get("inputs", {}).get("tier", "cross")
        require(requested in ("fast", "cross", "full"), "Manual tier must be fast, cross or full")
        tier = max((tier, requested), key=("fast", "cross", "full").index)
        reason = "explicit replay (cannot lower known change risk)"
        scopes = dict.fromkeys(scopes, True)
        windows_only = False
    else:
        raise ValueError("Unsupported CI event: " + name)
    default = event.get("repository", {}).get("default_branch")
    trusted = name in ("push", "schedule", "workflow_dispatch") and bool(default) and ref == "refs/heads/" + default
    contracts = [{"id": "linux-engine", "state": "implemented", "stage": "M0-A", "required": scopes["m0"]},
                 {"id": "native-startup-io", "state": "implemented", "stage": "M0-B", "required": scopes["m0"] and tier != "fast"},
                 {"id": "arm64-page-gaps-deep-abi", "state": "implemented", "stage": "M0-B/C", "required": scopes["m0"] and tier == "full"},
                 {"id": "offline-rebuild-refusals", "state": "implemented", "stage": "M0", "required": scopes["m0"] and tier == "full"},
                 {"id": "node-compatibility", "state": "not_implemented", "stage": "M3", "required": False},
                 {"id": "runtime-soak", "state": "not_implemented", "stage": "M2", "required": False},
                 {"id": "network-tls-security", "state": "not_implemented", "stage": "M4", "required": False},
                 {"id": "original-archive-retention", "state": "missing_resource", "stage": "release/06", "required": False, "required_for_release": True}]
    matrix = [row for row in NATIVE if not windows_only or row["os"] == "Windows"]
    return {"schema_version": 1, "report_kind": "ci-tier-plan", "status": "pass", "planned_at": now(),
            "evidence_verified": False, "tier": tier, "reason": reason, "event": name, "ref": ref,
            "commit": os.environ.get("GITHUB_SHA"), "changed_paths": paths, "sensitive_paths": sensitive,
            "run_fast": scopes["m0"] and tier == "fast",
            "run_cross": scopes["m0"] and tier in ("cross", "full"), "run_full": scopes["m0"] and tier == "full", "write_cache": trusted,
            "workflow_scope": scopes, "risk_reasons": risks,
            "native_matrix": {"include": matrix}, "contracts": contracts,
            "release_ready": False, "execution_status": "not_run",
            "cache_key": "locked-inputs-v1-" + identity(ROOT / "toolchains/qaplet.lock.json")["sha256"]}


def check_workflow(workflow):
    runner = workflow["jobs"]["portable-build"]["runs-on"]
    if not isinstance(runner, str) or not runner.startswith("ubuntu-"):
        raise ValueError("M0 product builds require an Ubuntu runner")
    if workflow.get("permissions") != {"contents": "read", "actions": "read"}:
        raise ValueError("M0 workflow must use read-only contents/actions permissions")
    steps = workflow["jobs"]["portable-build"]["steps"]
    runs = [step["run"] for step in steps if "run" in step]
    expected = ["make check OUT=out/m0/check",
                "python scripts/fetch_locked_toolchain.py --archive .cache/downloads/cosmocc.zip --report out/m0/acquisition.json",
                "make quickjs-fetch toolchain-fetch TOOLCHAIN_ARCHIVE=.cache/downloads/cosmocc.zip OUT=out/m0/inputs",
                "python scripts/ape_helpers.py fetch --report out/m0/inputs/ape-helpers.json",
                "make build-ape OUT=out/m0",
                "python scripts/native_io.py build --output-dir out/m0/io/artifacts --report out/m0/io/build-info.json",
                "python scripts/pages64k.py build --output-dir out/m0/pages64k/artifacts --report out/m0/pages64k/build-info.json",
                "python scripts/startup_loaders.py fetch --report out/m0/inputs/startup-loaders.json",
                "python scripts/startup_loaders.py build --producer-report out/m0/build-info.json --output-dir out/m0/loaders --report out/m0/loaders/helpers.json"]
    if runs[:9] != expected:
        raise ValueError("M0 build must check, explicitly populate locked inputs, then invoke the shared offline recipe")
    if not any(step.get("uses", "").startswith("actions/upload-artifact@") and
               step.get("if") == "${{ always() }}" and step.get("with", {}).get("path") == "out/m0/" and
               step["with"].get("if-no-files-found") == "error" for step in steps):
        raise ValueError("M0 must distribute original artifacts and failure evidence")
    for name, job in workflow["jobs"].items():
        if name in ("build", "portable-build", "tier-plan", "fast", "offline-rebuild"):
            continue
        for step in job.get("steps", []):
            if re.search(r"build-ape|(?:build_ape|native_io|pages64k|startup_loaders|m2_waker|m2_stdio|m2_process|m2_tty)\.py\s+build|cosmocc", step.get("run", "")):
                raise ValueError("Test job must consume distributed bytes without recompilation: " + name)
    native = workflow["jobs"].get("native-startup", {})
    if native.get("strategy", {}).get("matrix") != "${{ fromJSON(needs.tier-plan.outputs.native_matrix) }}":
        raise ValueError("Native startup must consume the shared risk-directed matrix; Full declares all five")
    if native.get("needs") != ["tier-plan", "portable-build"] or native.get("strategy", {}).get("fail-fast") is not False:
        raise ValueError("Native startup requires one distributed build and must retain every host result")
    native_steps = native.get("steps", [])
    if not any(step.get("uses", "").startswith("actions/download-artifact@") and step.get("with", {}).get("name") == "m0-build-${{ github.sha }}" for step in native_steps):
        raise ValueError("Native startup must download the same producer artifact")
    if not any("scripts/startup.py run" in step.get("run", "") and "--expected-arch" in step["run"] and "--expected-os" in step["run"] for step in native_steps):
        raise ValueError("Native startup must use the shared runner and verify measured OS/architecture")
    if not any(step.get("uses", "").startswith("actions/upload-artifact@") and step.get("if") == "${{ always() }}" for step in native_steps):
        raise ValueError("Native startup must retain failures and raw logs")
    io = [step for step in native_steps if "scripts/native_io.py run" in step.get("run", "")]
    if len(io) != 1 or io[0].get("if") != "${{ !cancelled() }}" or not all(arg in io[0]["run"] for arg in
        ("--build-report out/m0/io/build-info.json", "--artifact out/m0/io/artifacts/native-io.com",
         "--loader-dir out/m0/loaders", "--expected-os ${{ matrix.os }}", "--expected-arch ${{ matrix.arch }}",
         "--runner-label ${{ matrix.runner }}")):
        raise ValueError("Native I/O must consume the producer artifact and helpers on every measured native host, including startup failures")
    if not any(step.get("uses", "").startswith("actions/upload-artifact@") and step.get("if") == "${{ always() }}" and
               step.get("with", {}).get("path") == "out/io/${{ matrix.runner }}/" for step in native_steps):
        raise ValueError("Native I/O must retain every failure and cleanup log")
    planning = workflow["jobs"].get("page-plan", {})
    plan_steps = planning.get("steps", [])
    plans = [step for step in plan_steps if "scripts/startup.py page-gaps" in step.get("run", "")]
    if len(plans) != 1 or "--input-report out/native-arm64/startup.json" not in plans[0]["run"] or "--github-output" not in plans[0]["run"]:
        raise ValueError("ARM64 page gaps must be computed from the actual native startup report")
    gaps = workflow["jobs"].get("arm64-page-gaps", {})
    if gaps.get("strategy", {}).get("matrix") != "${{ fromJSON(needs.page-plan.outputs.matrix) }}" or "needs.page-plan.outputs.needs_guest == 'true'" not in gaps.get("if", ""):
        raise ValueError("QEMU must schedule only measured missing ARM64 page configurations")
    gap_steps = gaps.get("steps", [])
    for cli in ("scripts/arm64_guest.py fetch", "scripts/arm64_guest.py prepare", "scripts/startup.py guest-manifest",
                "scripts/native_io.py guest-manifest", "scripts/arm64_guest.py merge-manifests", "scripts/arm64_guest.py run"):
        if not any(cli in step.get("run", "") for step in gap_steps):
            raise ValueError("Missing shared full-system page-gap command: " + cli)
    for cli in ("scripts/startup.py import-guest", "scripts/native_io.py import-guest", "scripts/pages64k.py verify-guest"):
        if not any(cli in step.get("run", "") and "!cancelled()" in step.get("if", "") for step in gap_steps):
            raise ValueError("Guest semantic validators must retain failures: " + cli)
    if not any(step.get("uses", "").startswith("actions/upload-artifact@") and step.get("if") == "${{ always() }}" for step in gap_steps):
        raise ValueError("QEMU page gaps must retain failures and raw serial logs")
    aggregation = workflow["jobs"].get("startup-matrix", {})
    if "always()" not in aggregation.get("if", "") or not any("scripts/startup.py matrix" in step.get("run", "") and
            "--input-dir" in step["run"] for step in aggregation.get("steps", [])):
        raise ValueError("Six-target startup aggregation must run even when a consumer fails")
    check_m2_probe_transport(workflow)
    check_guest_input_supply(workflow)
    check_tier_transport(workflow)


def check_guest_input_supply(workflow):
    steps = workflow["jobs"]["arm64-page-gaps"]["steps"]
    expected = [
        {"id": "guest-input-plan", "if": "${{ matrix.page_size == 65536 }}",
         "run": 'python scripts/guest_inputs.py plan --config guests/ci-inputs.json --github-output "$GITHUB_OUTPUT" --report out/page-gaps/${{ matrix.page_size }}/supply-plan.json'},
        {"id": "guest-input-checkout", "if": "${{ matrix.page_size == 65536 }}", "uses": "actions/checkout@v7",
         "with": {"repository": "${{ github.repository }}", "ref": "${{ steps.guest-input-plan.outputs.source_commit }}",
                  "path": ".cache/guests/supplied", "persist-credentials": False}},
        {"if": "${{ !cancelled() && matrix.page_size == 65536 }}",
         "env": {"CI_GUEST_INPUT_COMMIT": "${{ steps.guest-input-plan.outputs.source_commit }}"},
         "run": 'python scripts/guest_inputs.py import --source-dir .cache/guests/supplied --source-commit "$CI_GUEST_INPUT_COMMIT" --cache .cache/guests --page-size 65536 --report out/page-gaps/${{ matrix.page_size }}/supply-import.json'},
    ]
    positions = []
    for required in expected:
        matches = [index for index, step in enumerate(steps) if {key: value for key, value in step.items() if key != "name"} == required]
        require(len(matches) == 1, "Guest input supply requires fixed same-repository read-only checkout and explicit verification")
        positions.append(matches[0])
    fetches = [index for index, step in enumerate(steps) if "scripts/arm64_guest.py fetch" in step.get("run", "")]
    require(len(fetches) == 1 and positions == sorted(positions) and positions[-1] < fetches[0],
            "Complete guest input verification/import must precede the unchanged explicit fetch")
    config = read_json(ROOT / "guests/ci-inputs.json")
    lock = identity(ROOT / "guests/arm64.lock.json")
    require(config.get("schema_version") == 1 and config.get("page_size") == 65536 and
            isinstance(config.get("source_commit"), str) and re.fullmatch(r"[0-9a-f]{40}", config["source_commit"]) and
            config.get("guest_lock") == {key: lock[key] for key in ("sha256", "size_bytes")},
            "Guest input supply configuration must pin a complete commit and the unchanged current guest lock")


def check_m2_probe_transport(workflow):
    """New backend facts keep their own suppliers, host logs and 64K semantics."""
    jobs = workflow["jobs"]
    build = jobs["portable-build"]["steps"]
    native = jobs["native-startup"]["steps"]
    guest = jobs["arm64-page-gaps"]["steps"]
    supplier = "m2-probes-build-${{ github.sha }}"
    require(any(step.get("if") == "${{ always() }}" and
                step.get("uses", "").startswith("actions/upload-artifact@") and
                step.get("with", {}).get("name") == supplier and
                step["with"].get("path") == "out/m2-probes/" and
                step["with"].get("if-no-files-found") == "error" for step in build),
            "M2 probes must distribute original supplier bytes and Fast failure logs")
    require(any(step.get("uses", "").startswith("actions/download-artifact@") and
                step.get("with") == {"name": supplier, "path": "out/m2-probes"} for step in native),
            "Every M2 native host must consume the same Ubuntu supplier")
    require(any(step.get("if") == "${{ always() }}" and
                step.get("uses", "").startswith("actions/upload-artifact@") and
                step.get("with", {}).get("name") == "m2-probes-native-${{ matrix.runner }}-${{ github.sha }}" and
                step["with"].get("path") == "out/m2-probes-native/${{ matrix.runner }}/" and
                step["with"].get("if-no-files-found") == "error" for step in native),
            "M2 native failures and cleanup observations must be retained")
    guest_if = "${{ !cancelled() && matrix.page_size == 65536 }}"
    require(any(step.get("if") == guest_if and step.get("with") ==
                {"name": supplier, "path": "out/m2-probes"} for step in guest),
            "Missing 64K validation must download the same M2 supplier")
    for name in ("waker", "stdio", "process", "tty"):
        prefix = "python scripts/m2_" + name + ".py "
        base = "out/m2-probes/" + name
        inputs = " --build-report " + base + "/build-info.json --artifact " + base + "/artifacts/m2-" + name + ".com"
        expected = {
            "build": prefix + "build --output-dir " + base + "/artifacts --report " + base + "/build-info.json",
            "fast": prefix + "run" + inputs + " --loader-dir out/m0/loaders --expected-os Linux --expected-arch x86_64 --runner-label ubuntu-24.04 --report out/m2-probes/fast/" + name + ".json",
            "native": prefix + "run" + inputs + " --loader-dir out/m0/loaders --expected-os ${{ matrix.os }} --expected-arch ${{ matrix.arch }} --runner-label ${{ matrix.runner }} --report out/m2-probes-native/${{ matrix.runner }}/" + name + ".json",
            "export": prefix + "guest-manifest" + inputs + " --loader " + base + "/artifacts/ape-aarch64.elf --page-size 65536 --manifest out/page-gaps/65536/m2/" + name + "-payload.json --report out/page-gaps/65536/m2/" + name + "-export.json",
            "import": prefix + "import-guest" + inputs + " --guest-report out/page-gaps/65536/m2/guest.json --report out/page-gaps/65536/m2/" + name + ".json",
        }
        for key, steps, condition in (("build", build, None), ("fast", build, None),
                ("native", native, "${{ !cancelled() }}"), ("export", guest, guest_if), ("import", guest, guest_if)):
            matches = [step for step in steps if step.get("run") == expected[key]]
            require(len(matches) == 1 and matches[0].get("if") == condition,
                    "Required M2 " + name + " " + key + " command is missing or may be skipped")
    merged = [step for step in guest if "scripts/arm64_guest.py merge-manifests" in step.get("run", "") and
              "--manifest out/page-gaps/65536/m2/payload.json" in step["run"]]
    require(len(merged) == 1 and merged[0].get("if") == guest_if and all(
                "--input-manifest out/page-gaps/65536/m2/" + name + "-payload.json" in merged[0]["run"]
                for name in ("waker", "stdio", "process", "tty")), "64K must execute every independent M2 probe payload")
    require(any(step.get("if") == guest_if and step.get("run") ==
                "python scripts/arm64_guest.py run --cache .cache/guests --page-size 65536 --manifest out/page-gaps/65536/m2/payload.json --timeout 300 --command-timeout 60 --report out/page-gaps/65536/m2/guest.json"
                for step in guest), "M2 probes need actual full-system 64K execution before semantic import")


def check_tier_transport(workflow):
    triggers = workflow.get("on", {})
    require(set(triggers) == {"push", "pull_request", "schedule", "workflow_dispatch"}, "Tier triggers must include push, PR, daily and manual replay")
    require(triggers["schedule"] == [{"cron": "17 3 * * *"}], "Full must run daily")
    require(triggers["push"] == {"branches": ["**"], "tags": ["v*rc*", "v*RC*"]}, "Fast every push and Full RC tags must remain scheduled")
    require(triggers["workflow_dispatch"]["inputs"]["tier"] == {
        "description": "Replay a tier on this exact ref", "type": "choice", "required": True,
        "default": "cross", "options": ["fast", "cross", "full"]}, "Manual replay must select an explicit tier")
    jobs = workflow["jobs"]
    required = {"tier-plan", "fast", "build", "portable-build", "offline-rebuild", "native-startup", "page-plan", "arm64-page-gaps", "startup-matrix", "full-core"}
    require(required <= jobs.keys(), "Missing required tier job")
    gate = jobs["build"]
    require(gate.get("if") == "${{ always() }}" and gate.get("runs-on") == "ubuntu-24.04" and
            set(gate.get("needs", [])) == required - {"build"} and "name" not in gate and
            [step.get("run") for step in gate["steps"] if "run" in step] ==
            ["python scripts/ci_tiers.py gate --report out/required-gate/result.json"] and
            [step.get("env") for step in gate["steps"] if "run" in step] == [{"CI_JOB_RESULTS": "${{ toJSON(needs) }}"}] and
            all("if" not in step for step in gate["steps"] if "run" in step),
            "Existing build required check must always propagate every selected product failure on Ubuntu")
    require(jobs["portable-build"].get("needs") == "tier-plan" and jobs["portable-build"].get("if") == "${{ needs.tier-plan.outputs.run_cross == 'true' }}", "Only Cross/Full require the complete portable supplier")
    require(jobs["fast"].get("if") == "${{ needs.tier-plan.outputs.run_fast == 'true' }}" and
            jobs["fast"].get("needs") == "tier-plan" and jobs["fast"].get("runs-on") == "ubuntu-24.04", "Fast requires its real Ubuntu product job")
    fast_runs = [step["run"] for step in jobs["fast"]["steps"] if "run" in step]
    require(fast_runs[-1] == "python scripts/local_check.py --profile all --output-dir out/fast --report out/fast/check.json" and
            not any("offline_integrity" in run or "make check " in run or "m2_" in run or "build-ape" in run for run in fast_runs),
            "Fast must compile/run current SDK behavior without historical apps, offline pair or guest")
    require(any(step.get("with", {}).get("path") == "out/fast/\nout/m0/\n.cache/build/local-check/\n!.cache/build/local-check/**/objects/**" and
                step["with"].get("include-hidden-files") is True and step.get("if") == "${{ always() }}"
                for step in jobs["fast"]["steps"]), "Fast must retain original supplier SDK/report/logs, including ARM archive, without build objects")
    selected = {
        "native-startup": "${{ needs.tier-plan.outputs.run_cross == 'true' }}",
        "page-plan": "${{ always() && needs.tier-plan.outputs.run_full == 'true' && needs.portable-build.result == 'success' }}",
        "arm64-page-gaps": "${{ always() && needs.tier-plan.outputs.run_full == 'true' && needs.portable-build.result == 'success' && needs.page-plan.outputs.needs_guest == 'true' }}",
        "startup-matrix": "${{ always() && needs.tier-plan.outputs.run_cross == 'true' && needs.portable-build.result == 'success' }}",
        "full-core": "${{ always() && needs.tier-plan.outputs.run_full == 'true' && needs.portable-build.result == 'success' }}",
    }
    for name, condition in selected.items():
        require(jobs[name].get("if") == condition and "tier-plan" in jobs[name].get("needs", []), "Required tier condition/dependency differs: " + name)
    planning = jobs["tier-plan"]
    require(planning.get("runs-on") == "ubuntu-24.04" and "if" not in planning, "Tier planner must always run on Ubuntu")
    calls = [step for step in planning["steps"] if "run" in step]
    require(len(calls) == 1 and calls[0].get("id") == "plan" and calls[0]["run"] ==
        'python scripts/ci_tiers.py plan --event-name "$CI_EVENT_NAME" --event-json "$GITHUB_EVENT_PATH" --ref "$CI_REF" --github-output "$GITHUB_OUTPUT" --report out/tier/plan.json', "Workflow must call the shared tier planner")
    require("if" not in calls[0], "The required tier planner cannot be skipped")
    for key in ("tier", "run_fast", "run_cross", "run_full", "write_cache", "native_matrix", "cache_key", "workflow_scope"):
        require(planning.get("outputs", {}).get(key) == "${{ steps.plan.outputs." + key + " }}", "Required shared planner output missing: " + key)
    cache_paths = ".cache/downloads/cosmocc.zip\n.cache/quickjs\n.cache/toolchains/cosmocc\n.cache/sources/ape-loader\n.cache/tools/zig/0.15.2"
    for name, job in jobs.items():
        require(name == "ci-efficiency" or not job.get("continue-on-error"), "Required product jobs cannot tolerate failures: " + name)
        for step in job.get("steps", []):
            run, uses = step.get("run", ""), step.get("uses", "")
            require(not step.get("continue-on-error") or uses.startswith("actions/download-artifact@"), "Required probe cannot tolerate failures: " + name)
            require("secrets." not in json.dumps(step), "Tiered CI must not require third-party secrets, including fork PRs")
            if uses.startswith("actions/checkout@"):
                require(step.get("with", {}).get("persist-credentials") is False, "Checkout cannot retain credentials")
            if uses.startswith("actions/cache/"):
                require(name in ("portable-build", "fast", "offline-rebuild") and step.get("with", {}) == {"path": cache_paths, "key": "${{ needs.tier-plan.outputs.cache_key }}"}, "Cache must bind exact locked input bytes")
                if uses.startswith("actions/cache/save@"):
                    require(step.get("if") == "${{ success() && (github.event_name == 'push' || github.event_name == 'schedule' || github.event_name == 'workflow_dispatch') && github.ref == format('refs/heads/{0}', github.event.repository.default_branch) && needs.tier-plan.outputs.write_cache == 'true' }}", "Only verified trusted default-branch events may save caches")
                else:
                    require(uses.startswith("actions/cache/restore@") and "if" not in step, "Only split cache restore/save is permitted")
            elif "actions/cache@" in uses:
                raise ValueError("Trusted cache policy requires separate restore/save")
            if any(command in run for command in ("fetch_locked_toolchain.py", "make quickjs-fetch", "scripts/ape_helpers.py fetch", "scripts/startup_loaders.py fetch")):
                require("if" not in step, "Restored cache inputs must always be verified; cache-hit bypass refused")
    build = jobs["portable-build"]["steps"]
    require(all("if" not in step for step in build if "run" in step and not any(command in step["run"] for command in
                ("sudo apt-get", "scripts/offline_integrity.py run", "scripts/ci_tiers.py seal-offline"))), "Required Fast input/compiler/probe commands cannot be skipped")
    require(sum(step.get("uses", "").startswith("actions/cache/restore@") for step in build) == 1 and
            sum(step.get("uses", "").startswith("actions/cache/save@") for step in build) == 1, "Build requires split trusted cache restore/save")
    restore = next(i for i, step in enumerate(build) if step.get("uses", "").startswith("actions/cache/restore@"))
    verify = next(i for i, step in enumerate(build) if "fetch_locked_toolchain.py" in step.get("run", ""))
    require(restore < verify, "Cache verification must follow restore")
    save = next(i for i, step in enumerate(build) if step.get("uses", "").startswith("actions/cache/save@"))
    last_input = max(i for i, step in enumerate(build) if "scripts/startup_loaders.py build" in step.get("run", ""))
    require(save > last_input, "Trusted cache save must follow complete restored-input verification")
    full_steps = jobs["offline-rebuild"]["steps"]
    require(jobs["offline-rebuild"].get("if") == "${{ needs.tier-plan.outputs.run_full == 'true' }}" and
            jobs["offline-rebuild"].get("needs") == ["tier-plan", "portable-build"] and
            jobs["offline-rebuild"].get("runs-on") == "ubuntu-24.04", "Offline evidence must be a separate Ubuntu Full-only supplier")
    offline = [step for step in full_steps if "scripts/offline_integrity.py run" in step.get("run", "")]
    require(len(offline) == 1 and "if" not in offline[0] and
            offline[0]["run"].startswith("mkdir -p out/full-offline/offline\nsudo ") and
            '--cache-root "$PWD/.cache"' in offline[0]["run"] and '--uid "$(id -u)"' in offline[0]["run"], "Full requires runner-owned report directory before actual disconnected nonroot rebuild/refusals")
    sealing = [step for step in full_steps if "scripts/ci_tiers.py seal-offline" in step.get("run", "")]
    require(len(sealing) == 1 and "if" not in sealing[0] and
            sealing[0]["run"] == "python scripts/ci_tiers.py seal-offline --offline-report out/full-offline/offline/offline-integrity.json --report out/full-offline/offline/transport-receipt.json", "Full must retain actual offline helper bytes and recipe receipt")
    sdk_fast = [
        "python scripts/sdk.py build --output-dir out/sdk/artifacts --report out/sdk/build.json",
        "python scripts/sdk.py run --build-report out/sdk/build.json --artifact out/sdk/artifacts/qaplet-host.com --loader-dir out/m0/loaders --expected-os Linux --expected-arch x86_64 --runner-label ubuntu-24.04 --report out/sdk/fast/run.json",
        "python scripts/sdk.py package-consume --build-report out/sdk/build.json --artifact out/sdk/artifacts/qaplet-host.com --output-dir out/sdk/external --loader-dir out/m0/loaders --report out/sdk/package-consume.json",
        "python scripts/sdk.py fault-consume --build-report out/sdk/build.json --artifact out/sdk/artifacts/qaplet-host.com --output-dir out/sdk/fault-artifacts --loader-dir out/m0/loaders --report out/sdk/fault-consume.json",
        "python scripts/sdk.py matrix --profile fast --build-report out/sdk/build.json --artifact out/sdk/artifacts/qaplet-host.com --loader-dir out/m0/loaders --input-report out/sdk/fast/run.json --report out/sdk/fast-matrix.json",
    ]
    require([step["run"] for step in build if "scripts/sdk.py" in step.get("run", "")] == sdk_fast,
            "Fast must build, execute, externally consume and verify the complete SDK lifecycle contract")
    uploads = [step for step in build if step.get("with", {}).get("name") == "m1-sdk-build-${{ github.sha }}"]
    require(len(uploads) == 1 and uploads[0].get("if") == "${{ always() }}" and
            uploads[0]["with"].get("path") == "out/sdk/" and uploads[0]["with"].get("include-hidden-files") is True and
            uploads[0]["with"].get("if-no-files-found") == "error", "SDK upload must preserve hidden ARM64 archive and all failure evidence")
    native = jobs["native-startup"]["steps"]
    require(any(step.get("with", {}) == {"name": "m1-sdk-build-${{ github.sha }}", "path": "out/sdk"} for step in native),
            "Native jobs must download the same SDK producer bytes")
    sdk_native = [step for step in native if "scripts/sdk.py" in step.get("run", "")]
    require(len(sdk_native) == 1 and sdk_native[0].get("if") == "${{ !cancelled() }}" and sdk_native[0]["run"] ==
            "python scripts/sdk.py run --build-report out/sdk/build.json --artifact out/sdk/artifacts/qaplet-host.com --loader-dir out/m0/loaders --expected-os ${{ matrix.os }} --expected-arch ${{ matrix.arch }} --runner-label ${{ matrix.runner }} --report out/sdk-native/${{ matrix.runner }}/run.json",
            "Five existing native jobs must execute supplied SDK contracts on their measured host")
    require(any(step.get("if") == "${{ always() }}" and step.get("with", {}).get("name") == "m1-sdk-native-${{ matrix.runner }}-${{ github.sha }}"
                for step in native), "Native SDK failures/raw logs must be uploaded")
    sdk_matrix = [step for step in jobs["startup-matrix"]["steps"] if "scripts/sdk.py matrix" in step.get("run", "")]
    require(len(sdk_matrix) == 1 and sdk_matrix[0].get("if") == "${{ !cancelled() }}" and sdk_matrix[0]["run"] ==
            "python scripts/sdk.py matrix --profile cross --build-report out/sdk/build.json --artifact out/sdk/artifacts/qaplet-host.com --loader-dir out/m0/loaders --input-dir out/sdk-native --report out/sdk-matrix/matrix.json --target-plan out/tier/plan.json",
            "Cross must retain missing/failed SDK targets through the shared native matrix CLI")
    sdk_guest = [step for step in jobs["arm64-page-gaps"]["steps"] if "scripts/sdk_guest.py" in step.get("run", "") or
                 "/sdk/payload.json --timeout" in step.get("run", "")]
    require([step["run"] for step in sdk_guest] == [
        "python scripts/sdk_guest.py export --build-report out/sdk/build.json --distribution out/sdk/artifacts --manifest out/page-gaps/${{ matrix.page_size }}/sdk/payload.json --selection out/page-gaps/${{ matrix.page_size }}/sdk/selection.json --report out/page-gaps/${{ matrix.page_size }}/sdk/export.json",
        "python scripts/arm64_guest.py run --cache .cache/guests --page-size 65536 --manifest out/page-gaps/${{ matrix.page_size }}/sdk/payload.json --timeout 900 --command-timeout 60 --report out/page-gaps/${{ matrix.page_size }}/sdk/guest.json",
        "python scripts/sdk_guest.py import --selection out/page-gaps/${{ matrix.page_size }}/sdk/selection.json --guest-report out/page-gaps/${{ matrix.page_size }}/sdk/guest.json --report out/page-gaps/${{ matrix.page_size }}/sdk/import.json"],
        "Full must execute and independently import the actual changed SDK on the missing locked 64K kernel")
    require(all(step.get("if") == "${{ !cancelled() && matrix.page_size == 65536 }}" for step in sdk_guest),
            "SDK guest execution must preserve native-first missing-64K selection and retain failures")
    core = jobs["full-core"]
    require(core.get("needs") == ["tier-plan", "portable-build", "native-startup", "arm64-page-gaps", "startup-matrix", "offline-rebuild"] and core.get("runs-on") == "ubuntu-24.04", "Full Core must await all selected same-commit jobs including offline evidence")
    require([step["run"] for step in core["steps"] if "run" in step] == [
        'python scripts/ci_tiers.py collect-actions --commit "$CI_COMMIT" --output-dir out/full/archives --report out/full/acquisition.json',
        'python scripts/ci_tiers.py manifest --collection-report out/full/collection.json --commit "$CI_COMMIT" --report out/full/manifest.json',
        'python scripts/core_gate.py evaluate --input-manifest out/full/manifest.json --report out/full/core.json'], "Full requires original ZIP verification, explicit same-commit selection and existing Core consumer")
    require(all("if" not in step for step in core["steps"] if "run" in step), "Required Full collection/selection/Core consumer cannot be skipped")
    matrix = [step for step in jobs["startup-matrix"]["steps"] if "scripts/startup.py matrix" in step.get("run", "")]
    require(len(matrix) == 1 and '--profile "$CI_TIER"' in matrix[0]["run"] and matrix[0].get("env") == {"CI_TIER": "${{ needs.tier-plan.outputs.tier }}"}, "Cross and Full must use matching aggregate profiles")
    for name in required:
        require(any(step.get("uses", "").startswith("actions/upload-artifact@") and step.get("if") == "${{ always() }}" and
                    step.get("with", {}).get("if-no-files-found") == "error" for step in jobs[name]["steps"]), "Failure evidence retention missing: " + name)
    for name, job in jobs.items():
        if name in ("tier-plan", "ci-efficiency"):
            continue
        concurrency = job.get("concurrency", {})
        require(concurrency.get("cancel-in-progress") == "${{ needs.tier-plan.outputs.run_full != 'true' }}" and
                "github.run_id" in concurrency.get("group", ""), "Full/RC evidence cannot be auto-cancelled: " + name)
    measured = jobs.get("ci-efficiency", {})
    require(measured.get("if") == "${{ always() }}" and measured.get("continue-on-error") is True and
            measured.get("concurrency", {}).get("cancel-in-progress") is False and
            any("scripts/ci_evidence.py efficiency-actions" in step.get("run", "") for step in measured.get("steps", [])),
            "Every M0 run must retain an efficiency observation without cancelling evidence")


def required_gate(args):
    results = read_json(args.needs_json) if args.needs_json else json.loads(os.environ.get("CI_JOB_RESULTS", "null"))
    expected = {"tier-plan", "fast", "portable-build", "native-startup", "page-plan", "arm64-page-gaps",
                "startup-matrix", "full-core", "offline-rebuild"}
    require(isinstance(results, dict) and set(results) == expected, "Complete actual job results required")
    require(results["tier-plan"].get("result") == "success", "Tier/scope selection failed; product execution cannot be skipped into success")
    outputs = results["tier-plan"].get("outputs", {})
    flags = [outputs.get(key) for key in ("run_fast", "run_cross", "run_full")]
    require(all(value in ("true", "false") for value in flags) and
            not (flags[0] == flags[1] == "true") and not (flags[2] == "true" and flags[1] != "true"), "Invalid selected tier outputs")
    selected = ["fast"] if flags[0] == "true" else []
    if flags[1] == "true": selected += ["portable-build", "native-startup", "startup-matrix"]
    if flags[2] == "true":
        selected += ["page-plan", "offline-rebuild", "full-core"]
        needs_guest = results["page-plan"].get("outputs", {}).get("needs_guest")
        require(needs_guest in ("true", "false"), "Actual Full page-gap selection required")
        if needs_guest == "true":
            selected.append("arm64-page-gaps")
    failed = [name for name in selected if results[name].get("result") != "success"]
    require(not failed, "Required selected jobs failed, cancelled or were skipped: " + ", ".join(failed))
    return {"status": "pass", "scope": "ci-required-gate", "selected_jobs": selected,
            "job_results": results, "product_scope": "selected" if selected else "not_applicable",
            "evidence_verified": False, "release_ready": False,
            "reason": "Selected job results succeeded; product evidence remains in original reports" if selected else
                      "Scope only: no product execution selected or claimed"}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("operation", choices=("plan", "gate", "seal-offline", "manifest", "collect-actions"))
    parser.add_argument("--needs-json", type=Path, help="Retained needs context for local gate replay; Actions uses CI_JOB_RESULTS")
    parser.add_argument("--event-name")
    parser.add_argument("--event-json", type=Path)
    parser.add_argument("--changed-paths", type=Path, help="explicit JSON path list for local replay")
    parser.add_argument("--ref", default="")
    parser.add_argument("--github-output", type=Path)
    parser.add_argument("--offline-report", type=Path)
    parser.add_argument("--collection-report", type=Path)
    parser.add_argument("--output-dir", type=Path)
    parser.add_argument("--commit")
    parser.add_argument("--report", type=Path, required=True)
    args = parser.parse_args()
    try:
        if args.operation == "plan":
            require(args.event_name and args.event_json, "Tier plan requires event name and payload")
            result = plan(args)
        elif args.operation == "gate":
            result = required_gate(args)
        else:
            from ci_full import collect_actions, manifest, seal_offline
            if args.operation == "seal-offline":
                require(args.offline_report, "Offline transport requires actual report")
                result = seal_offline(args.offline_report.resolve())
            elif args.operation == "manifest":
                require(args.collection_report, "Full manifest requires verified collection report")
                result = manifest(args.collection_report, args.commit)
            else:
                require(args.output_dir, "Actions collection needs an output directory")
                result = collect_actions(args.output_dir.resolve(), args.commit)
        if args.operation == "plan" and args.github_output:
            with args.github_output.open("a", encoding="utf-8") as output:
                for key in ("tier", "run_fast", "run_cross", "run_full", "write_cache", "native_matrix", "cache_key", "workflow_scope"):
                    value = result[key]
                    output.write(key + "=" + (json.dumps(value, separators=(",", ":")) if not isinstance(value, str) else value) + "\n")
                for key, value in result["workflow_scope"].items():
                    output.write(key + "=" + json.dumps(value) + "\n")
            if os.environ.get("GITHUB_STEP_SUMMARY"):
                with open(os.environ["GITHUB_STEP_SUMMARY"], "a", encoding="utf-8") as summary:
                    summary.write("Scope plan (no product execution): " + json.dumps(result["workflow_scope"]) + "\n")
    except (OSError, ValueError, KeyError, TypeError, AttributeError) as error:
        result = {"status": "fail", "reason": str(error), "evidence_verified": False}
    write_json(args.report, result)
    print(json.dumps({key: result.get(key) for key in ("status", "tier", "reason")}))
    return 0 if result.get("status") == "pass" or result.get("profile") == "full" else 1


if __name__ == "__main__":
    sys.exit(main())
