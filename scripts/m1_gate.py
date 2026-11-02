#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Join actual M1 execution evidence using the established read-only consumers."""
from datetime import datetime
import json
from pathlib import Path
import re
from types import SimpleNamespace
import zipfile

from build_ape import BuildError, identity
from ci_evidence import unpack
from ci_identity import validate_ci_identity
import core_gate
from core_gate import Evidence, Gate, interval, normalized, require, same, unique
from foundation import ROOT, now
from input_lock import component, consume_identity, check_loader_bundle
from nightly_toolchain import write_json
import sdk
import sdk_guest
from sdk_matrix import TARGETS, original_path, validate_loader, validate_native
from startup import check_ci_commit

SOURCES = ("m1_gate.py", "check_evidence.py", "sdk_matrix.py", "sdk_guest.py", "core_gate.py",
           "ci_evidence.py", "ci_identity.py", "arm64_guest.py")
EXCLUSIONS = ["M2 OS waker", "I/O cancellation", "late completions", "full scheduler",
              "physical ARM64 64K hardware/performance", "deferred original ZIP release mirror"]


def provider_catalog(path, commit):
    data = json.loads(path.read_text(encoding="utf-8"))
    records = unique(data["artifacts"], "id")
    require(records, "Explicit original provider artifact catalog required")
    return records


def providers(transports, catalog, commit, base):
    require(transports and len(unique(transports, "id")) == len(unique(transports, "name")),
            "Unique provider ID/name transports required")
    for row in transports:
        provider = catalog[row["id"]]
        require(type(row["id"]) is int and row["id"] > 0 and row["name"] == provider["name"] and
                row["name"].endswith("-" + commit), "Provider artifact ID/name/commit mismatch")
        require(provider["workflow_run"]["head_sha"] == commit and provider["expired"] is False,
                "Provider artifact belongs to another source commit/expired closure")
        require(row["archive"]["sha256"] == provider["digest"].removeprefix("sha256:") and
                row["archive"]["size_bytes"] == provider["size_in_bytes"], "Provider original archive identity mismatch")
        # Existing read-only branch checks the complete extracted inventory, CRC,
        # case-fold duplicates, special entries and expansion bounds.
        root, archive = (Path(row[key]) if key == "root" else Path(row["archive"]["path"]) for key in ("root", "archive"))
        root = (root if root.is_absolute() else base / root).resolve()
        archive = (archive if archive.is_absolute() else base / archive).resolve()
        require(root.is_dir(), "Verified extracted provider root must already exist")
        unpack(archive, root, 8 * 1024**3)


def ci(evidence, item, reference, catalog, commit, prefix=None, measured=True):
    value = item.get("ci")
    validate_ci_identity(value)
    require(value["commit"] == commit, "Selected CI report belongs to another product commit")
    transport = evidence.files.get(evidence.resolve(reference["path"] if isinstance(reference, dict) else reference))
    require(transport is not None, "Selected CI report requires original provider transport membership")
    provider = catalog[transport["artifact_id"]]
    require(transport["artifact_name"] == provider["name"] and
            str(provider["workflow_run"]["id"]) == str(value["run_id"]), "CI report/provider run identity mismatch")
    if prefix: require(provider["name"].startswith(prefix), "Wrong provider family for selected report")
    if measured:
        host = item["host"]
        require(value["runner_os"] == {"Darwin": "macOS"}.get(host["os"], host["os"]) and
                value["runner_arch"].lower() == {"x86_64": "x64", "aarch64": "arm64"}[host["execution_arch"]],
                "CI runner contradicts measured native host")


def audit_context(evidence, output):
    # Reuse the Core audit/toolchain contract, without invoking its M0 evaluator.
    return SimpleNamespace(evidence=evidence, output=output, notice_symbols=set(), nightly=component("cosmocc"))


def audit(evidence, artifact, output):
    sidecars = artifact["headers"]["sidecars"]
    require(len(sidecars) == 2 and len(artifact["audit"]["commands"]) == 3, "Complete CPU inspection targets required")
    targets = [sidecars[0]["path"], sidecars[1]["path"], artifact["path"]]
    tools = ("x86_64-linux-cosmo-nm", "aarch64-linux-cosmo-nm", "x86_64-linux-cosmo-objdump")
    for index, command in enumerate(artifact["audit"]["commands"]):
        arguments = command["argv"]
        tail = 4 if index < 2 else 3
        require(len(arguments) in (tail, tail + 1) and normalized(arguments[-1]) == normalized(targets[index]) and
                arguments[-tail].endswith("/" + tools[index]) and
                arguments[-tail + 1:-1] == (["-g", "--defined-only"] if index < 2 else ["-p"]),
                "Link audit inspection target/command differs from selected CPU artifact")
    return Gate.audit(audit_context(evidence, output), artifact)


def package_inventory(item):
    """Completeness derives from the current public distribution recipe, not claims."""
    copied = {"sdk/include/qaplet.h": "include/qaplet.h", "sdk/embed.c": "examples/embed.c",
              "sdk/application.c": "examples/application.c", "sdk/README.md": "examples/SDK-README.md",
              "sdk/LICENSE": "LICENSE", "sdk/QUICKJS-LICENSE": "third_party/quickjs/LICENSE"}
    hosts = {"contract": "host", "values": "values", "promises": "promises", "modules": "modules",
             "module-policy": "module_policy", "budgets": "budgets", "host-modules": "host_modules",
             "events": "events", "runtime": "runtime", "resources": "resources", "stop": "stop", "close": "close"}
    copied.update({"sdk/host-" + name + ".c": "tests/native/sdk_" + source + ".c" for name, source in hosts.items()})
    copied.update({"sdk/modules/" + path.relative_to(ROOT / "tests/fixtures/modules").as_posix(): path.relative_to(ROOT).as_posix()
                   for path in (ROOT / "tests/fixtures/modules").rglob("*") if path.is_file()})
    expected = set(copied) | {"sdk/manifest.json", "sdk/lib/libqaplet.a", "sdk/lib/.aarch64/libqaplet.a", "ape-aarch64.elf"}
    expected.update("ape-helper-notices/" + name for name in ("HACL-LICENSE.txt", "LICENSE", "source.lock.json"))
    expected.update(name + suffix for name in ("qaplet", "qaplet-host", "qaplet-example")
                    for suffix in (".com", ".com.dbg", ".aarch64.elf", ".x86_64.map", ".aarch64.map", ".build-commands.log"))
    members = unique(item["package"]["members"], "file")
    require(set(members) == expected, "Complete authoritative public SDK package inventory required")
    for name, source in copied.items():
        data = (ROOT / source).read_bytes()
        # Git's Windows text checkout may translate the two license files. The
        # Ubuntu package carries their canonical LF form; product recipe inputs
        # themselves were already compared byte-for-byte by producer().
        if name in ("sdk/LICENSE", "sdk/QUICKJS-LICENSE"): data = data.replace(b"\r\n", b"\n")
        require(same(members[name], {"sha256": core_gate.digest(data), "size_bytes": len(data)}),
                "Packaged public source differs from product recipe: " + name)


def external_consumer(evidence, reference, producer, producer_reference, loaders, catalog, commit, output):
    item = evidence.report(reference, "qaplet-sdk", ("package-consume",))
    ci(evidence, item, reference, catalog, commit, "m1-sdk-build-")
    consume_identity(item, {})
    check_ci_commit(producer, item)
    evidence.read(item["producer_report"])
    require(evidence.resolve(item["producer_report"]["path"]) == evidence.resolve(producer_reference) and
            same(item["producer_report"], identity(evidence.resolve(producer_reference))), "External consumer selected another SDK producer")
    for field in ("sdk", "sdk_recipe", "package", "source", "toolchain"):
        require(item[field] == producer[field], "External package consumer context differs: " + field)
    require(item["host"]["os"] == "Linux" and item["host"]["execution_mode"] == "native" and
            item["host"]["cpu"] == item["host"]["execution_arch"] and item["host"]["kernel"] and
            type(item["host"]["page_size"]) is int and item["host"]["page_size"] > 0,
            "Actual Ubuntu native package consumer required")
    Gate.toolchain(audit_context(evidence, output), item)
    validate_loader(loaders, item, evidence)
    external = item["external_consumer"]
    directory = normalized(external["directory"])
    require(re.fullmatch(r"/tmp/qaplet-sdk-consumer-[^/]+", directory) is not None,
            "External application must compile outside the product checkout")
    public, work = directory + "/sdk", directory + "/build"
    require(external["private_headers"] is False and external["include_directories"] == [public + "/include"] and
            external["archive"] == public + "/lib/libqaplet.a" and external["source"] == public + "/application.c" and
            same(external["package"], producer["package"]), "External consumer must use only sealed public package inputs")
    evidence.read(external["package"])
    # Temporary extracted files were intentionally ephemeral. Their exact sources
    # are retained inside the verified sealed package, not today's repository.
    archive = unique(producer["package"]["members"], "file")
    require(all(name in archive for name in ("sdk/include/qaplet.h", "sdk/application.c", "sdk/lib/libqaplet.a")),
            "External consumer public input closure missing")
    commands = item["commands"]
    for command in commands:
        evidence.command(command, 0 if not any(command == case["commands"][0] for case in item["cases"]) else None)
        require(interval(item)[0] <= interval(command)[0] <= interval(command)[1] <= interval(item)[1], "External command outside execution interval")
    compiles = [command for command in commands if "-c" in command["argv"] and external["source"] in command["argv"]]
    require(len(compiles) == 1, "Exactly one actual sealed public application compile required")
    compile = compiles[0]
    compiler = item["native_toolchain"]["tree"] + "/bin/cosmocc"
    require(compile["argv"] == [compiler, "-Os", "-D_GNU_SOURCE", "-I" + public + "/include",
        "-ffile-prefix-map=" + public + "=sdk", "-c", public + "/application.c", "-o", work + "/application.o"] and
        compile["cwd"] == work, "External compile used private/current source headers or wrong archive source")
    links = [command for command in commands if external["archive"] in command["argv"]]
    require(len(links) == 1 and links[0]["argv"] == [compiler, "-Os", "-Wl,-z,max-page-size=65536", "-Wl,-Map=%.map",
        work + "/application.o", public + "/lib/libqaplet.a", "-lm", "-lpthread", "-o", work + "/qaplet-example.com"] and
        links[0]["cwd"] == work, "External application must link the supplied public static archive")
    artifacts = unique(item["artifacts"], "name")
    require(set(artifacts) == {"qaplet-example"}, "New external C application artifact required")
    artifact = artifacts["qaplet-example"]
    audit(evidence, artifact, output)
    require(normalized(artifact["path"]) != normalized(next(row for row in producer["artifacts"] if row["name"] == "qaplet-example")["path"]),
            "Producer prebuilt path cannot substitute for external package compile output")
    build_log = evidence.read(artifact["build_commands"])
    require(build_log.strip() and public.encode() in build_log and b"application.o" in build_log,
            "External application lacks raw compiler/link command log")
    cases = unique(item["cases"])
    require(set(cases) == {"qaplet-example." + row[0] for row in sdk.EXAMPLE_CASES}, "Complete eight external application contracts required")
    for suffix, arguments, code, stdout, stderr in sdk.EXAMPLE_CASES:
        case = cases["qaplet-example." + suffix]
        require(case["status"] == "pass" and case["required"] is True and len(case["commands"]) == 1,
                "External public application contract skipped/failed/unexecuted")
        command = case["commands"][0]
        require(command in commands and same(case["artifact"], artifact) and case["sdk_sha256"] == producer["sdk"]["sha256"],
                "External per-case command/SDK/artifact identity mismatch")
        arguments = [argument.replace("{modules}", public + "/modules") for argument in arguments]
        wanted = [item["loader"]["path"], artifact["path"], *arguments]
        require([original_path(value, command["cwd"]) if index < 2 else value for index, value in enumerate(command["argv"])] == wanted,
                "External application invocation differs from public package catalog")
        require(case["expected"] == {"exit_code": code, "stdout": stdout, "stderr_contains": stderr},
                "External expected output contradicts authoritative catalog")
        raw = [evidence.read(command[key + "_log"]) for key in ("stdout", "stderr")]
        require(command["exit_code"] == code and case["actual"]["exit_code"] == code and case["actual"]["timed_out"] is False and
                raw == [case["actual"][key].encode() for key in ("stdout", "stderr")] and raw[0] == stdout.encode() and
                (stderr.encode() in raw[1] if stderr else raw[1] == b""), "External raw output failed public contract")
        require(interval(case) == interval(command) and case["timeout_seconds"] == command["timeout_seconds"] <= 300 and
                (interval(command)[1] - interval(command)[0]).total_seconds() <= command["timeout_seconds"],
                "External case deadline/interval mismatch")
    return {"status": "pass", "report": reference, "cases": len(cases), "artifact": artifact,
            "public_inputs": [archive[name] for name in ("sdk/include/qaplet.h", "sdk/application.c", "sdk/lib/libqaplet.a")]}


def evaluate(path):
    output = {"schema_version": 1, "report_kind": "qaplet-m1-evidence", "scope": "m1-full-evidence",
        "status": "fail", "started_at": now(), "reason": "", "m1": {"status": "fail", "coverage": []},
        "m0": {"status": "not_run"}, "m0_core_status": "not_run", "release_ready": False,
        "required_gaps": [], "optional_gaps": [], "exclusions": EXCLUSIONS,
        "evaluator_sources": [identity(ROOT / "scripts" / name) for name in SOURCES], "evidence_index": []}
    evidence = None
    try:
        manifest = json.loads(path.read_text(encoding="utf-8"))
        required = {"schema_version", "profile", "product_commit", "provider_catalog", "producer_report", "distribution_root",
                    "package_consumer", "native_reports", "loader_manifest", "sdk_guest", "m0_manifest", "m0_original_manifest",
                    "path_mappings", "transports"}
        require(set(manifest) == required and manifest["schema_version"] == 1 and manifest["profile"] == "m1-full",
                "Explicit versioned complete M1 Full selections required")
        commit = manifest["product_commit"]
        require(re.fullmatch(r"[a-f0-9]{40}", commit or ""), "Exact selected product source commit required")
        output["selected_product_commit"] = commit
        base = path.resolve().parent
        catalog_path = Path(manifest["provider_catalog"])
        catalog_path = catalog_path if catalog_path.is_absolute() else base / catalog_path
        catalog = provider_catalog(catalog_path, commit)
        output["selected_inputs"] = {"manifest": identity(path), "provider_catalog": identity(catalog_path), "selection": manifest}
        providers(manifest["transports"], catalog, commit, base)
        evidence = Evidence(manifest, base, require_transport=True)
        output["transports"] = evidence.transports
        supplied = evidence.resolve(manifest["distribution_root"])
        item = sdk_guest.producer(evidence, manifest["producer_report"], supplied, output, commit)
        ci(evidence, item, manifest["producer_report"], catalog, commit, "m1-sdk-build-")
        require(item["sdk_recipe"]["working_tree_dirty"] is False, "Selected product producer must have clean recipe source")
        package_inventory(item)
        require(item.get("commands"), "Actual SDK producer execution commands required")
        require(item["build"]["recipe_files"] == item["sdk_recipe"]["files"] and
                item["build"]["sdk_recipe_id"] == item["sdk_recipe"]["sha256"] and
                sdk.digest({key: value for key, value in item["build"].items() if key != "sha256"}) == item["build"]["sha256"],
                "SDK producer build/recipe aggregate differs")
        for command in item["commands"]:
            evidence.command(command, 0)
            require(interval(item)[0] <= interval(command)[0] <= interval(command)[1] <= interval(item)[1] and
                    (interval(command)[1] - interval(command)[0]).total_seconds() <= command["timeout_seconds"],
                    "SDK producer execution interval/deadline mismatch")
        Gate.toolchain(audit_context(evidence, output), item)
        for artifact in item["artifacts"]: audit(evidence, artifact, output)
        output["selected_product"] = {"commit": commit, "producer_report": identity(evidence.resolve(manifest["producer_report"])),
            "sdk_sha256": item["sdk"]["sha256"], "sdk_recipe_sha256": item["sdk_recipe"]["sha256"], "package": item["package"]}
        loaders = evidence.report(manifest["loader_manifest"], "ape-loaders")
        locked = consume_identity(loaders, {})
        check_loader_bundle(loaders, locked)
        ci(evidence, loaders, manifest["loader_manifest"], catalog, commit, "m0-full-core-")
        args = SimpleNamespace(loader_helpers=loaders, loader_dir=None, loader_manifest=None)
        output["m1"]["package_consumer"] = external_consumer(evidence, manifest["package_consumer"], item,
            manifest["producer_report"], args, catalog, commit, output)
        require(len(manifest["native_reports"]) == len(TARGETS), "Exactly five actual native target reports required")
        seen = set()
        for reference in manifest["native_reports"]:
            incoming = evidence.report(reference, "qaplet-sdk", ("run",))
            ci(evidence, incoming, reference, catalog, commit, "m1-sdk-native-")
            host = incoming["host"]
            target = next((name for name, system, arch in TARGETS if (host["os"], host["execution_arch"]) == (system, arch)), None)
            require(target is not None and target not in seen, "Duplicate/conflicting/unknown native target selection")
            seen.add(target)
            lifecycle = validate_native(args, incoming, item, output["selected_product"]["producer_report"], evidence, None)
            output["m1"]["coverage"].append({"target": target, "status": "pass", "report": reference, "host": host,
                "required_cases": len(incoming["required_cases"]), "optional_cases": [case["case_id"] for case in incoming["cases"] if not case["required"]],
                "unavailable_cases": incoming["unavailable_cases"], "lifecycle": lifecycle})
            output["optional_gaps"].extend(incoming["unavailable_cases"])
        arm = next(row for row in output["m1"]["coverage"] if row["target"] == "linux-aarch64")
        if arm["host"]["page_size"] == 65536:
            require(manifest["sdk_guest"] is None, "Native 64K must not be replaced by redundant guest evidence")
            output["m1"]["arm64_64k"] = {"status": "pass", "execution_mode": "native", "page_size": 65536}
        else:
            guest_selection = manifest["sdk_guest"]
            guest = evidence.json(guest_selection["guest_report"])
            ci(evidence, guest, guest_selection["guest_report"], catalog, commit, "m0-page-gap-65536-", measured=False)
            guest_output = {}
            sdk_guest.consume(guest_selection, base, guest_output, commit, evidence,
                              (item, manifest["producer_report"], supplied))
            require(guest_output["page_size"] == 65536, "Actual SDK 64K kernel coverage required")
            guest_output.pop("evidence_index", None)
            output["m1"]["arm64_64k"] = guest_output
        # The current Full manifest remains a separate M0 product contract.
        m0_path = Path(manifest["m0_manifest"])
        m0_path = m0_path if m0_path.is_absolute() else base / m0_path
        m0 = json.loads(m0_path.read_text())
        original = evidence.json(manifest["m0_original_manifest"])
        for field in ("schema_version", "profile", "producers", "startup_reports", "io_reports", "pages64k_reports", "offline_report", "offline_recipe_comparison"):
            require(m0[field] == original[field], "Current M0 Full selection differs from original provider manifest: " + field)
        require([(row["id"], row["name"], row["archive"]["sha256"], row["archive"]["size_bytes"]) for row in m0["transports"]] ==
                [(row["id"], row["name"], row["archive"]["sha256"], row["archive"]["size_bytes"]) for row in original["transports"]],
                "Current M0 Full provider identity closure differs")
        providers(m0["transports"], catalog, commit, m0_path.parent)
        m0_evidence = Evidence(m0, m0_path.parent, require_transport=True)
        for family, reference in m0["producers"].items():
            producer = m0_evidence.json(reference)
            ci(m0_evidence, producer, reference, catalog, commit)
            check_ci_commit(item, producer)
            require(producer["input_lock"] == item["input_lock"], "M0/M1 immutable input closure differs")
        result = core_gate.evaluate(m0_path)
        output["m0"] = result
        output["m0_core_status"] = result["core"]["status"]
        require(result["core"]["status"] == "pass", "Current M0 Full failed: " + "; ".join(result["core"]["errors"]))
        for entry in result["evidence_index"]:
            require(entry["transport"] is not None, "M0 Full contains untransported raw evidence")
        output["m1"]["status"] = "pass"
        output.update(status="pass", reason="Complete current M1 native/public package/64K evidence and independent current M0 Full verified")
    except (OSError, ValueError, KeyError, TypeError, BuildError, zipfile.BadZipFile) as error:
        output["reason"] = str(error)
        output["required_gaps"].append(str(error))
    finally:
        if evidence is not None: output["evidence_index"] = list(evidence.index.values())
        output["finished_at"] = now()
    return output
