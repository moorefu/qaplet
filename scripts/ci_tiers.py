#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Shared Fast/Cross/Full selection; plans are never execution evidence."""
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


def changed_paths(args, event):
    if args.changed_paths:
        paths = read_json(args.changed_paths)
    elif args.event_name == "pull_request":
        pull = event["pull_request"]
        paths = git_read(ROOT, "diff", "--name-only", "-z", pull["base"]["sha"], pull["head"]["sha"]).decode().split("\0")
    else:
        paths = []
    require(isinstance(paths, list) and all(isinstance(path, str) for path in paths), "Changed paths must be a JSON string array")
    paths = sorted(set(path for path in paths if path))
    require(all(not path.startswith(("/", "\\")) and ".." not in path.replace("\\", "/").split("/") for path in paths), "Invalid changed path")
    return paths


def plan(args):
    event = read_json(args.event_json)
    paths = changed_paths(args, event)
    # Unknown/new source locations are sensitive too. Only known prose is exempt.
    sensitive = [path for path in paths if not path.startswith(("docs/", ".scratch/")) and path not in ("README.md", "GLOSSARY.md", "LICENSE", "AGENTS.md")]
    name, ref = args.event_name, args.ref
    if name == "pull_request":
        tier, reason = ("full", "sensitive pull request paths") if sensitive else ("cross", "pull request native smoke")
    elif name == "push":
        tier, reason = ("full", "release candidate tag") if ref.startswith("refs/tags/") else ("fast", "every pushed commit")
    elif name == "schedule":
        tier, reason = "full", "daily implemented Full contracts"
    elif name == "workflow_dispatch":
        tier, reason = event.get("inputs", {}).get("tier", "cross"), "explicit manual tier replay"
        require(tier in ("fast", "cross", "full"), "Manual tier must be fast, cross or full")
    else:
        raise ValueError("Unsupported CI event: " + name)
    default = event.get("repository", {}).get("default_branch")
    trusted = name in ("push", "schedule", "workflow_dispatch") and bool(default) and ref == "refs/heads/" + default
    contracts = [{"id": "linux-engine", "state": "implemented", "stage": "M0-A", "required": True},
                 {"id": "native-startup-io", "state": "implemented", "stage": "M0-B", "required": tier != "fast"},
                 {"id": "arm64-page-gaps-deep-abi", "state": "implemented", "stage": "M0-B/C", "required": tier == "full"},
                 {"id": "offline-rebuild-refusals", "state": "implemented", "stage": "M0", "required": tier == "full"},
                 {"id": "node-compatibility", "state": "not_implemented", "stage": "M3", "required": False},
                 {"id": "runtime-soak", "state": "not_implemented", "stage": "M2", "required": False},
                 {"id": "network-tls-security", "state": "not_implemented", "stage": "M4", "required": False},
                 {"id": "original-archive-retention", "state": "missing_resource", "stage": "release/06", "required": False, "required_for_release": True}]
    return {"schema_version": 1, "report_kind": "ci-tier-plan", "status": "pass", "planned_at": now(),
            "evidence_verified": False, "tier": tier, "reason": reason, "event": name, "ref": ref,
            "commit": os.environ.get("GITHUB_SHA"), "changed_paths": paths, "sensitive_paths": sensitive,
            "run_cross": tier in ("cross", "full"), "run_full": tier == "full", "write_cache": trusted,
            "native_matrix": {"include": NATIVE}, "contracts": contracts,
            "release_ready": False, "execution_status": "not_run",
            "cache_key": "locked-inputs-v1-" + identity(ROOT / "toolchains/qaplet.lock.json")["sha256"]}


def check_workflow(workflow):
    runner = workflow["jobs"]["build"]["runs-on"]
    if not isinstance(runner, str) or not runner.startswith("ubuntu-"):
        raise ValueError("M0 product builds require an Ubuntu runner")
    if workflow.get("permissions") != {"contents": "read", "actions": "read"}:
        raise ValueError("M0 workflow must use read-only contents/actions permissions")
    steps = workflow["jobs"]["build"]["steps"]
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
        if name in ("build", "tier-plan"):
            continue
        for step in job.get("steps", []):
            if re.search(r"build-ape|(?:build_ape|native_io|pages64k|startup_loaders)\.py\s+build|cosmocc", step.get("run", "")):
                raise ValueError("Test job must consume distributed bytes without recompilation: " + name)
    native = workflow["jobs"].get("native-startup", {})
    rows = native.get("strategy", {}).get("matrix", {}).get("include", [])
    expected_rows = {("ubuntu-24.04", "Linux", "x86_64"), ("windows-2022", "Windows", "x86_64"),
                     ("macos-15-intel", "Darwin", "x86_64"), ("macos-15", "Darwin", "aarch64"),
                     ("ubuntu-24.04-arm", "Linux", "aarch64")}
    if len(rows) != 5 or {(row.get("runner"), row.get("os"), row.get("arch")) for row in rows} != expected_rows:
        raise ValueError("Native startup must schedule all five explicit OS/architecture runners")
    if native.get("needs") != ["tier-plan", "build"] or native.get("strategy", {}).get("fail-fast") is not False:
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
    check_tier_transport(workflow)


def check_tier_transport(workflow):
    triggers = workflow.get("on", {})
    require(set(triggers) == {"push", "pull_request", "schedule", "workflow_dispatch"}, "Tier triggers must include push, PR, daily and manual replay")
    require(triggers["schedule"] == [{"cron": "17 3 * * *"}], "Full must run daily")
    require(triggers["push"] == {"branches": ["**"], "tags": ["v*rc*", "v*RC*"]}, "Fast every push and Full RC tags must remain scheduled")
    require(triggers["workflow_dispatch"]["inputs"]["tier"] == {
        "description": "Replay a tier on this exact ref", "type": "choice", "required": True,
        "default": "cross", "options": ["fast", "cross", "full"]}, "Manual replay must select an explicit tier")
    jobs = workflow["jobs"]
    required = {"tier-plan", "build", "native-startup", "page-plan", "arm64-page-gaps", "startup-matrix", "full-core"}
    require(required <= jobs.keys(), "Missing required tier job")
    require(jobs["build"].get("needs") == "tier-plan" and "if" not in jobs["build"], "Every selected tier must build Fast on Ubuntu")
    selected = {
        "native-startup": "${{ needs.tier-plan.outputs.run_cross == 'true' }}",
        "page-plan": "${{ always() && needs.tier-plan.outputs.run_full == 'true' && needs.build.result == 'success' }}",
        "arm64-page-gaps": "${{ always() && needs.tier-plan.outputs.run_full == 'true' && needs.build.result == 'success' && needs.page-plan.outputs.needs_guest == 'true' }}",
        "startup-matrix": "${{ always() && needs.tier-plan.outputs.run_cross == 'true' && needs.build.result == 'success' }}",
        "full-core": "${{ always() && needs.tier-plan.outputs.run_full == 'true' && needs.build.result == 'success' }}",
    }
    for name, condition in selected.items():
        require(jobs[name].get("if") == condition and "tier-plan" in jobs[name].get("needs", []), "Required tier condition/dependency differs: " + name)
    planning = jobs["tier-plan"]
    require(planning.get("runs-on") == "ubuntu-24.04" and "if" not in planning, "Tier planner must always run on Ubuntu")
    calls = [step for step in planning["steps"] if "run" in step]
    require(len(calls) == 1 and calls[0].get("id") == "plan" and calls[0]["run"] ==
        'python scripts/ci_tiers.py plan --event-name "$CI_EVENT_NAME" --event-json "$GITHUB_EVENT_PATH" --ref "$CI_REF" --github-output "$GITHUB_OUTPUT" --report out/tier/plan.json', "Workflow must call the shared tier planner")
    require("if" not in calls[0], "The required tier planner cannot be skipped")
    for key in ("tier", "run_cross", "run_full", "write_cache", "native_matrix", "cache_key"):
        require(planning.get("outputs", {}).get(key) == "${{ steps.plan.outputs." + key + " }}", "Required shared planner output missing: " + key)
    cache_paths = ".cache/downloads/cosmocc.zip\n.cache/quickjs\n.cache/toolchains/cosmocc\n.cache/sources/ape-loader\n.cache/tools/zig/0.15.2"
    for name, job in jobs.items():
        require(not job.get("continue-on-error"), "Required jobs cannot tolerate failures: " + name)
        for step in job.get("steps", []):
            run, uses = step.get("run", ""), step.get("uses", "")
            require(not step.get("continue-on-error") or uses.startswith("actions/download-artifact@"), "Required probe cannot tolerate failures: " + name)
            require("secrets." not in json.dumps(step), "Tiered CI must not require third-party secrets, including fork PRs")
            if uses.startswith("actions/checkout@"):
                require(step.get("with", {}).get("persist-credentials") is False, "Checkout cannot retain credentials")
            if uses.startswith("actions/cache/"):
                require(name == "build" and step.get("with", {}) == {"path": cache_paths, "key": "${{ needs.tier-plan.outputs.cache_key }}"}, "Cache must bind exact locked input bytes")
                if uses.startswith("actions/cache/save@"):
                    require(step.get("if") == "${{ success() && (github.event_name == 'push' || github.event_name == 'schedule' || github.event_name == 'workflow_dispatch') && github.ref == format('refs/heads/{0}', github.event.repository.default_branch) && needs.tier-plan.outputs.write_cache == 'true' }}", "Only verified trusted default-branch events may save caches")
                else:
                    require(uses.startswith("actions/cache/restore@") and "if" not in step, "Only split cache restore/save is permitted")
            elif "actions/cache@" in uses:
                raise ValueError("Trusted cache policy requires separate restore/save")
            if any(command in run for command in ("fetch_locked_toolchain.py", "make quickjs-fetch", "scripts/ape_helpers.py fetch", "scripts/startup_loaders.py fetch")):
                require("if" not in step, "Restored cache inputs must always be verified; cache-hit bypass refused")
    build = jobs["build"]["steps"]
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
    offline = [step for step in build if "scripts/offline_integrity.py run" in step.get("run", "")]
    require(len(offline) == 1 and offline[0].get("if") == "${{ needs.tier-plan.outputs.run_full == 'true' }}" and
            '--cache-root "$PWD/.cache"' in offline[0]["run"] and '--uid "$(id -u)"' in offline[0]["run"], "Full requires actual disconnected nonroot rebuild/refusals")
    sealing = [step for step in build if "scripts/ci_tiers.py seal-offline" in step.get("run", "")]
    require(len(sealing) == 1 and sealing[0].get("if") == "${{ needs.tier-plan.outputs.run_full == 'true' }}" and
            sealing[0]["run"] == "python scripts/ci_tiers.py seal-offline --offline-report out/m0/offline/offline-integrity.json --report out/m0/offline/transport-receipt.json", "Full must retain actual offline helper bytes and recipe receipt")
    core = jobs["full-core"]
    require(core.get("needs") == ["tier-plan", "build", "native-startup", "arm64-page-gaps", "startup-matrix"] and core.get("runs-on") == "ubuntu-24.04", "Full Core must await all selected same-commit jobs")
    require([step["run"] for step in core["steps"] if "run" in step] == [
        'python scripts/ci_tiers.py collect-actions --commit "$CI_COMMIT" --output-dir out/full/archives --report out/full/acquisition.json',
        'python scripts/ci_tiers.py manifest --collection-report out/full/collection.json --commit "$CI_COMMIT" --report out/full/manifest.json',
        'python scripts/core_gate.py evaluate --input-manifest out/full/manifest.json --report out/full/core.json'], "Full requires original ZIP verification, explicit same-commit selection and existing Core consumer")
    matrix = [step for step in jobs["startup-matrix"]["steps"] if "scripts/startup.py matrix" in step.get("run", "")]
    require(len(matrix) == 1 and '--profile "$CI_TIER"' in matrix[0]["run"] and matrix[0].get("env") == {"CI_TIER": "${{ needs.tier-plan.outputs.tier }}"}, "Cross and Full must use matching aggregate profiles")
    for name in required:
        require(any(step.get("uses", "").startswith("actions/upload-artifact@") and step.get("if") == "${{ always() }}" and
                    step.get("with", {}).get("if-no-files-found") == "error" for step in jobs[name]["steps"]), "Failure evidence retention missing: " + name)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("operation", choices=("plan", "seal-offline", "manifest", "collect-actions"))
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
                for key in ("tier", "run_cross", "run_full", "write_cache", "native_matrix", "cache_key"):
                    value = result[key]
                    output.write(key + "=" + (json.dumps(value, separators=(",", ":")) if not isinstance(value, str) else value) + "\n")
    except (OSError, ValueError, KeyError, TypeError, AttributeError) as error:
        result = {"status": "fail", "reason": str(error), "evidence_verified": False}
    write_json(args.report, result)
    print(json.dumps({key: result.get(key) for key in ("status", "tier", "reason")}))
    return 0 if result.get("status") == "pass" or result.get("profile") == "full" else 1


if __name__ == "__main__":
    sys.exit(main())
