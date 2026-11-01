#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Consume selected immutable execution evidence; never build or run probes."""
import argparse
import ast
import base64
from datetime import datetime
import hashlib
import json
from pathlib import Path, PurePosixPath
import re
import sys
import uuid
import zipfile

from build_ape import BuildError, FORBIDDEN, headers
from input_lock import component, attach
from evidence import read_json
from foundation import ROOT, decode, now
from native_io import REQUIRED as IO_REQUIRED, matches_contract
from nightly_toolchain import write_json
from pages64k import REQUIRED as DEEP_REQUIRED
from startup import CASES as STARTUP_CASES, evaluate_case, target_key


def require(condition, reason):
    if not condition:
        raise ValueError(reason)


def digest(data):
    return hashlib.sha256(data).hexdigest()


def normalized(value):
    require(isinstance(value, str) and value and "\x00" not in value, "Invalid evidence path")
    text = value.replace("\\", "/").rstrip("/")
    require(".." not in text.split("/"), "Evidence path traversal rejected: " + text)
    return text


def same(left, right):
    return all(left.get(key) == right.get(key) for key in ("sha256", "size_bytes"))


def validate_recipe_comparison(recipe, repository=ROOT, previous=None):
    """Accept exact current inputs or the narrowly proven historical CI-only edit."""
    inventories = []
    for field in ("tested", "current"):
        entries = recipe[field + "_recipe_inputs"]
        require(digest(json.dumps(entries, sort_keys=True, separators=(",", ":")).encode()) ==
                recipe[field + "_recipe_inventory_sha256"], "Recipe comparison fingerprint mismatch")
        builders = [normalized(entry["path"]) for entry in entries
                    if normalized(entry["path"]).endswith("/scripts/build_ape.py")]
        require(len(builders) == 1, "Recipe inventory requires exactly one builder")
        source_root = builders[0].removesuffix("/scripts/build_ape.py")
        inventory = {}
        for entry in entries:
            original = normalized(entry["path"])
            require(original.startswith(source_root + "/"), "Recipe input escaped measured source root")
            relative = original[len(source_root) + 1:]
            require(relative not in inventory, "Duplicate recipe input")
            inventory[relative] = entry
            if field == "current":
                path = (repository / relative).resolve()
                require(path.is_relative_to(repository.resolve()), "Current recipe input escaped repository")
                data = path.read_bytes()
                require(same(entry, {"sha256": digest(data), "size_bytes": len(data)}),
                        "Current recipe input differs from actual repository: " + original)
        inventories.append(inventory)
    tested, current = inventories
    require(tested.keys() == current.keys(), "Recipe comparison input closure differs")
    changed = [{"tested": tested[name], "current": entry} for name, entry in current.items()
               if not same(tested[name], entry)]
    require(recipe["changed_inputs"] == changed, "Recipe comparison changed inputs do not match inventories")
    if not changed:
        return "identical"
    require(len(changed) == 1 and changed[0] == {"tested": tested["scripts/build_ape.py"], "current": current["scripts/build_ape.py"]},
            "Recipe comparison changes producer semantics")
    require(previous is not None and same(changed[0]["tested"], {"sha256": digest(previous), "size_bytes": len(previous)}),
            "Tested builder snapshot differs from executed recipe")
    trees = [ast.parse(raw) for raw in (previous, (repository / "scripts/build_ape.py").read_bytes())]
    for tree in trees:
        tree.body = [node for node in tree.body if not isinstance(node, (ast.FunctionDef, ast.AsyncFunctionDef)) or node.name != "check_ci"]
    require(ast.dump(trees[0], include_attributes=False) == ast.dump(trees[1], include_attributes=False),
            "Offline/current builder executable semantics changed")
    return "check-ci-only"


def stamp(value):
    result = datetime.fromisoformat(value.replace("Z", "+00:00"))
    require(result.tzinfo is not None, "Evidence timestamps require a timezone")
    return result


def interval(record):
    start, end = stamp(record["started_at"]), stamp(record["finished_at"])
    require(end >= start, "Evidence interval is reversed")
    return start, end


def unique(rows, field="case_id"):
    result = {item[field]: item for item in rows}
    require(len(result) == len(rows), "Duplicate " + field)
    return result


def without_paths(value):
    if isinstance(value, dict):
        return {key: without_paths(item) for key, item in value.items() if key != "path"}
    if isinstance(value, list):
        return [without_paths(item) for item in value]
    return value


def guest_requirements(page_size):
    """The page-gap payload contract; this does not attest an OS execution."""
    require(page_size in (4096, 65536), "Guests only cover required ARM64 4096/65536 configurations")
    deep = ("pages64k.facts", "pages64k.suite", "pages64k.shell-header") if page_size == 65536 else ()
    return {"families": ("startup", "io", "pages64k") if deep else ("startup", "io"),
            "case_ids": tuple(entry[0] for entry in STARTUP_CASES) + ("native-io.suite",) + deep}


def arm64_page_gaps(native_pages, guest_pages):
    """Apply QEMU-only-for-missing-pages policy to already measured page sets."""
    native, guests = set(native_pages), set(guest_pages)
    require(native, "Actual native Linux ARM64 measurement required before QEMU supplementation")
    require(all(type(page) is int and page > 0 for page in native) and guests <= {4096, 65536}, "Invalid measured ARM64 page configuration")
    require(not native & guests, "QEMU cannot replace an available native page configuration")
    return tuple(sorted({4096, 65536} - native - guests))


class Evidence:
    """One explicit resolver for original POSIX/Windows paths and moved archives."""

    def __init__(self, manifest, base):
        self.base = base
        self.mappings = [(normalized(item["source"]), self.local(item["root"]))
                         for item in manifest.get("path_mappings", [])]
        require(len({source for source, _ in self.mappings}) == len(self.mappings), "Ambiguous source-root mappings")
        self.files, self.index, self.ids, self.transports = {}, {}, {}, []
        self.roots = []
        for item in manifest.get("transports", []):
            archive = self.local(item["archive"]["path"])
            raw = archive.read_bytes()
            require(digest(raw) == item["archive"]["sha256"] and len(raw) == item["archive"]["size_bytes"],
                    "Transport archive SHA-256/size mismatch: " + str(archive))
            root = self.local(item["root"])
            self.roots.append(root)
            count = 0
            with zipfile.ZipFile(archive) as bundle:
                names = set()
                for member in bundle.infolist():
                    name = normalized(member.filename)
                    require(not name.startswith("/") and not re.match(r"^[A-Za-z]:", name), "Absolute ZIP member")
                    require(name not in names, "Duplicate ZIP member")
                    names.add(name)
                    require((member.external_attr >> 16) & 0o170000 != 0o120000, "Transport symlink rejected")
                    if member.is_dir():
                        continue
                    path = (root / name).resolve()
                    require(path.is_relative_to(root), "ZIP member escaped artifact root")
                    data = bundle.read(member)  # Also verifies CRC.
                    require(path.is_file() and path.read_bytes() == data, "Extracted byte differs from verified ZIP: " + str(path))
                    require(path not in self.files, "Overlapping transport roots")
                    self.files[path] = {"archive": str(archive), "archive_sha256": item["archive"]["sha256"],
                                        "artifact_id": item.get("id"), "artifact_name": item.get("name"),
                                        "member": name, "sha256": digest(data), "size_bytes": len(data)}
                    count += 1
            self.transports.append({**item, "verified_files": count})

    def local(self, value):
        path = Path(normalized(str(value)))
        return (path if path.is_absolute() else self.base / path).resolve()

    def resolve(self, value):
        original = normalized(str(value))
        matches = [(source, root) for source, root in self.mappings
                   if original == source or original.startswith(source + "/")]
        if matches:
            length = max(len(source) for source, _ in matches)
            selected = [(source, root) for source, root in matches if len(source) == length]
            require(len(selected) == 1, "Ambiguous transported path: " + original)
            source, root = selected[0]
            path = (root / original[len(source):].lstrip("/")).resolve()
            require(path.is_relative_to(root), "Resolved evidence escaped selected root")
            return path
        return self.local(original)

    def read(self, reference):
        info = reference if isinstance(reference, dict) else {"path": str(reference)}
        path = self.resolve(info["path"])
        data = path.read_bytes()
        measured = {"sha256": digest(data), "size_bytes": len(data)}
        if "sha256" in info:
            require(same(info, measured), "Evidence SHA-256/size mismatch: " + info["path"])
        transport = self.files.get(path)
        if any(path.is_relative_to(root) for root in self.roots):
            require(transport is not None and same(transport, measured), "Unbound/changed transported file: " + str(path))
        self.index[(info["path"], str(path))] = {"original_path": info["path"], "resolved_path": str(path),
                                                   **measured, "transport": transport}
        return data

    def json(self, reference):
        self.read(reference)
        path = self.resolve(reference["path"] if isinstance(reference, dict) else reference)
        return read_json(path)

    def report(self, reference, kind, scope=None):
        item = self.json(reference)
        require(item.get("schema_version") == 1 and item.get("report_kind") == kind, "Wrong report kind/version: " + kind)
        if scope is not None:
            require(item.get("scope") in scope, "Execution scope required: " + kind)
        require(item.get("status") == "pass", "Selected Core report is nonpassing: " + str(reference))
        interval(item)
        ident = item["report_id"]
        sha = digest(self.read(reference))
        require(ident not in self.ids, "Duplicate/conflicting report identity: " + ident)
        self.ids[ident] = sha
        return item

    def command(self, record, expected=None):
        interval(record)
        require(isinstance(record.get("argv"), list) and bool(record["argv"]) and
                all(isinstance(value, str) and value for value in record["argv"]) and record.get("cwd"), "Missing actual argv/cwd")
        require(type(record.get("timeout_seconds")) in (int, float) and 0 < record["timeout_seconds"] <= 3600,
                "Missing/bad command deadline")
        require(record.get("timed_out") is False and type(record.get("exit_code")) is int, "Timed out/unexecuted command")
        if expected is not None:
            require(record["exit_code"] == expected, "Actual process exit contradicts passing evidence")
        return tuple(decode(self.read(record[key])) for key in ("stdout_log", "stderr_log"))


class Gate:
    def __init__(self, manifest, base, report):
        require(isinstance(manifest, dict), "Evidence manifest must be an object")
        required = {"schema_version", "profile", "producers", "startup_reports", "io_reports", "pages64k_reports", "offline_report", "offline_recipe_comparison"}
        require(required <= set(manifest) <= required | {"transports", "path_mappings", "svc_reports"}, "Manifest has missing/unknown evidence selections")
        require(manifest.get("schema_version") == 1 and manifest.get("profile") == "full", "Full versioned evidence profile required")
        self.manifest, self.output = manifest, report
        self.evidence = Evidence(manifest, base)
        self.producers, self.artifacts, self.guests = {}, {}, {}
        self.coverage = {key: {"target": key, "startup": [], "io": []} for key in
                         ("linux-x86_64", "windows-x86_64", "macos-x86_64", "macos-aarch64", "linux-aarch64-4k", "linux-aarch64-64k")}
        self.native_arm = {"startup": set(), "io": set()}
        self.guest_arm = {"startup": set(), "io": set()}
        self.notice_symbols = set()
        self.input_lock = attach(report)
        self.nightly = component("cosmocc")

    def ci(self, item, host=None, inherited=None):
        ci = item.get("ci") or inherited
        if ci is None:
            return None
        require(all(ci.get(key) for key in ("run_id", "run_attempt", "workflow", "commit", "runner_name", "runner_os", "runner_arch", "image_os", "image_version")),
                "CI execution requires run, commit, runner and image identities")
        require(re.fullmatch(r"[a-f0-9]{40}", ci["commit"]), "Invalid CI commit")
        report_sha = self.evidence.ids.get(item.get("report_id"))
        transports = [info for info in self.evidence.files.values() if info["sha256"] == report_sha]
        require(transports and all(type(info["artifact_id"]) is int and info["artifact_id"] > 0 and
                                  info["artifact_name"].endswith("-" + ci["commit"]) for info in transports),
                "CI evidence needs a verified archive with provider artifact/commit identity")
        if host:
            require(ci["runner_os"] == {"Darwin": "macOS"}.get(host["os"], host["os"]), "CI OS contradicts measured host")
            require(ci["runner_arch"].lower() == {"x86_64": "x64", "aarch64": "arm64"}[host["execution_arch"]], "CI architecture contradicts measured host")
        return ci

    def context(self, item, producer):
        for field in ("source", "toolchain", "build"):
            require(item[field] == producer[field], "Host/producer context mismatch: " + field)
        self.evidence.read(item["producer_report"])
        require(same(item["producer_report"], self.identity(self.producer_paths[producer["report_id"]])), "Wrong selected producer report")

    def identity(self, reference):
        raw = self.evidence.read(reference)
        return {"sha256": digest(raw), "size_bytes": len(raw)}

    def toolchain(self, item):
        if "input_lock" in item:
            require(item["input_lock"] == self.input_lock.identity(), "Producer unified input lock mismatch")
        toolchain = item["toolchain"]
        require(toolchain["archive"] == self.nightly["archive"], "Nightly archive differs from current immutable lock")
        require(item["toolchain_lock"]["sha256"] == digest((ROOT / "toolchains/cosmocc.lock.json").read_bytes()), "Toolchain lock identity mismatch")
        require(toolchain["libc_source"] == self.nightly["libc_source"], "Unproven libc source identity was changed")
        expected_tools = {x["path"]: x for x in self.nightly["tools"]}
        for entry in toolchain["tools"]:
            require(entry["path"] in expected_tools and same(entry, expected_tools[entry["path"]]), "Compiler identity mismatch")
        require(set(expected_tools) == {entry["path"] for entry in toolchain["tools"]}, "Compiler inventory incomplete")
        require(toolchain["source"] == self.nightly["source"], "Nightly provenance mismatch")

    def audit(self, artifact):
        data = self.evidence.read(artifact)
        path = self.evidence.resolve(artifact["path"])
        sidecars = artifact["headers"]["sidecars"]
        require(len(sidecars) == 2, "Both CPU sidecars required")
        for entry in sidecars:
            self.evidence.read(entry)
        measured = headers(path, [self.evidence.resolve(x["path"]) for x in sidecars])
        require(without_paths(measured) == without_paths(artifact["headers"]), "APE slices/sidecars disagree with actual bytes")
        audit = artifact["audit"]
        require(audit["status"] == "pass" and audit["forbidden_service_symbols"] == [], "Core link audit is nonpassing")
        require(len(audit["maps"]) == 2 and len(audit["commands"]) == 3, "Both CPU maps, symbol inventories and PE imports required")
        texts = []
        for entry in audit["maps"]:
            text = decode(self.evidence.read(entry))
            require("Linker script and memory map" in text and "LOAD " in text, "Missing actual linker map")
            texts.append(text)
        for index, command in enumerate(audit["commands"]):
            stdout, _ = self.evidence.command(command, 0)
            require(stdout.strip() and (index < 2 or "DLL Name:" in stdout), "Missing decoded symbols/imports")
            require(("--defined-only" in command["argv"]) if index < 2 else ("-p" in command["argv"]), "Wrong link inspection command")
            texts.append(stdout)
        forbidden = sorted({match.group(0) for text in texts for match in FORBIDDEN.finditer(text)})
        require(not forbidden, "Core contains service-only symbols: " + ", ".join(forbidden))
        for symbol in ("musl_libc_notice", "openbsd_strings_notice", "openbsd_sorting_notice", "gdtoa_notice", "nsync_notice"):
            if any(re.search(r"\b" + symbol + r"\b", text) for text in texts):
                self.notice_symbols.add(symbol)
        self.output.setdefault("link_audits", []).append({"artifact": artifact, "status": "pass", "maps": audit["maps"],
                                                         "commands": audit["commands"], "method": "Rehashed APE/sidecars/maps, reparsed headers and actual symbol/import logs"})
        return data

    def producer(self, family, reference):
        kinds = {"startup": ("ape-build", None), "io": ("native-io", {"build"}), "pages64k": ("pages64k-build", None)}
        item = self.evidence.report(reference, *kinds[family])
        self.toolchain(item)
        require(item["host"]["os"] == "Linux" and item["host"]["execution_mode"] == "native", "Ubuntu native producer required")
        self.ci(item, item["host"])
        require(item.get("commands"), "Producer has no execution commands")
        require(all(case.get("status") == "pass" for case in item.get("cases", [])) and
                all(case.get("status") == "pass" for case in item.get("negative_cases", [])), "Producer contains a nonpassing Core case")
        accepted_nonzero = []
        if family == "startup":
            cases = unique(item["cases"])
            require(set(cases) == {entry[0] for entry in STARTUP_CASES}, "Producer startup catalog incomplete")
            for case_id, name, _, kind in STARTUP_CASES:
                case = cases[case_id]
                require(len(case["commands"]) == 1, "Producer case lacks actual execution")
                record = case["commands"][0]
                stdout, stderr = self.evidence.command(record)
                require(stdout == case["actual"]["stdout"] and stderr == case["actual"]["stderr"] and
                        record["exit_code"] == case["actual"]["exit_code"] and
                        evaluate_case(case_id, name, kind, item["host"], record, stdout, stderr, case["artifact"])["status"] == "pass", "Producer native case contradicts raw semantics")
                if record["exit_code"]:
                    accepted_nonzero.append(record)
        if family == "pages64k":
            for case in item.get("negative_cases", []):
                require(case["case_id"] == "wrong-native-environment" and case["actual"]["case_id"] == "pages64k.environment" and
                        case["actual"]["status"] == "fail" and case["actual"]["actual"] == item["host"]["page_size"] != 65536 and
                        case["actual"]["expected"] == 65536 and case["command"]["exit_code"] == 1, "Producer negative environment contract mismatch")
                accepted_nonzero.append(case["command"])
        for command in item["commands"]:
            self.evidence.command(command)
            require(command["exit_code"] == 0 or command in accepted_nonzero, "Producer passing label hides a failed build command")
        inputs = unique(item["input_reports"], "stage") if isinstance(item["input_reports"], list) else item["input_reports"]
        for value in inputs.values():
            incoming = self.evidence.json(value)
            require(incoming["status"] == "pass", "Producer fixed-input verification failed")
        require(set(inputs) == ({"before-source", "after-source", "before-toolchain", "after-toolchain"} if family == "startup" else {"before-toolchain", "after-toolchain"}), "Producer before/after fixed-input reports incomplete")
        if family == "startup":
            require(all(self.evidence.json(inputs[key])["source"] == item["source"] for key in ("before-source", "after-source")), "Producer source identity differs from actual input verification")
        if family == "startup":
            lock = component("quickjs")
            require(item["source"]["commit"] == lock["commit"] and item["source"]["lock_sha256"] == digest((ROOT / "third_party/quickjs/source.lock.json").read_bytes()), "QuickJS source lock mismatch")
            require(item["source"]["patches"] == read_json(ROOT / "third_party/quickjs/patches.json")["patches"], "Ordered QuickJS overlays differ")
            require(item["source"]["sha256"] == lock["source"]["sha256"] and item["source"]["clean"] is True, "QuickJS source content differs from locked patched tree")
        portable = item["portable_helpers"]
        require(portable["source"]["lock"]["sha256"] == digest((ROOT / "third_party/ape-helpers/source.lock.json").read_bytes()) and
                portable["source"]["patch_manifest"]["sha256"] == digest((ROOT / "third_party/ape-helpers/patches.json").read_bytes()), "Helper source/patch identity mismatch")
        require(portable["recipe"]["sha256"] == digest((ROOT / "scripts/ape_helpers.py").read_bytes()), "Helper recipe identity mismatch")
        source_lock = component("ape_helpers")
        require(portable["source"]["commit"] == source_lock["commit"] and portable["source"]["git_tree"] == source_lock["git_tree"] and
                portable["source"]["patches"] == read_json(ROOT / "third_party/ape-helpers/patches.json")["patches"], "Helper source provenance/overlay mismatch")
        locked_files = source_lock["files"]
        require(len(portable["source"]["files"]) == len(locked_files) and all(
            len([entry for entry in portable["source"]["files"] if normalized(entry["path"]).endswith("/" + expected["path"]) and same(entry, expected)]) == 1
            for expected in locked_files), "Helper fixed source file inventory mismatch")
        require(portable["patched_source"]["sha256"] == portable["source"]["patches"][0]["result_sha256"], "Patched packer source identity mismatch")
        self.artifacts[family] = unique(item["artifacts"], "name")
        require(set(self.artifacts[family]) == {"startup": {"hello", "quickjs-eval"}, "io": {"native-io"}, "pages64k": {"pages64k"}}[family], "Wrong producer artifact family")
        for artifact in item["artifacts"]:
            self.audit(artifact)
        self.producers[family] = item
        self.producer_paths[item["report_id"]] = reference
        return item

    def host(self, item, family):
        host = item.get("execution") if family == "io" else item["host"]
        mode = host["execution_mode"]
        require(host.get("kernel") and type(host.get("page_size")) is int and host["page_size"] > 0, "Missing measured kernel/page configuration")
        require(mode in ("native", "emulated-system"), "Native or full-system execution required")
        if mode == "native":
            physical = item["host"]
            require(item.get("qemu") == ({"status": "not_applicable"} if family == "startup" else None), "Native claim carries contradictory QEMU identity")
            if family == "io":
                require(item["scope"] == "host", "Native I/O requires actual host execution scope")
            require(physical["execution_mode"] == "native" and physical["cpu"] == physical["execution_arch"] == host["execution_arch"], "Translated/physical/execution architecture mismatch")
            for key in ("os", "kernel", "page_size"):
                require(host[key] == physical[key], "Execution/host fact mismatch: " + key)
            translation = physical.get("translation", {})
            require(translation.get("sysctl.proc_translated", {}).get("stdout", "").strip() != "1", "macOS translation cannot count as native")
            self.ci(item, physical)
        else:
            require(host["os"] == "Linux" and host["execution_arch"] == "aarch64", "Guests only fill Linux ARM64 page gaps")
            guest = self.guest(item["guest_report"])
            actual = guest["guest"]
            require(host["page_size"] == actual["page_size"] and host["kernel"] == actual["kernel"] and item["qemu"] == guest["qemu"], "Guest execution identity contradicts transport")
        key = target_key(host)
        if host["os"] == "Linux" and host["execution_arch"] == "aarch64":
            (self.native_arm if mode == "native" else self.guest_arm)[family].add(host["page_size"])
            if key is None and mode == "native":
                self.output.setdefault("additional_native_observations", []).append({"family": family, "host": host, "report_id": item["report_id"]})
                return None, host
        require(key in self.coverage, "Unknown measured host target")
        if key not in ("linux-aarch64-4k", "linux-aarch64-64k"):
            require(mode == "native", "Non-ARM targets require native execution")
        return key, host

    def startup(self, reference):
        item = self.evidence.report(reference, "ape-startup")
        self.context(item, self.producers["startup"])
        key, host = self.host(item, "startup")
        artifacts = unique(item["artifacts"], "name")
        require(set(artifacts) == set(self.artifacts["startup"]), "Startup artifacts incomplete")
        for name, artifact in artifacts.items():
            require(same(artifact, self.artifacts["startup"][name]) and artifact.get("sha256_after") == artifact["sha256"], "Startup artifact stale/changed")
        cases = unique(item["cases"])
        require(set(cases) == {x[0] for x in STARTUP_CASES}, "Startup required cases missing/unknown")
        for case_id, name, arguments, kind in STARTUP_CASES:
            case = cases[case_id]
            require(case["status"] == "pass" and same(case["artifact"], artifacts[name]) and len(case["commands"]) == 1, "Nonpassing/cross-artifact startup case")
            command = case["commands"][0]
            out, err = self.evidence.command(command)
            require(case["actual"]["stdout"] == out and case["actual"]["stderr"] == err and
                    case["actual"]["exit_code"] == command["exit_code"] and case["actual"]["timed_out"] is False, "Startup actual differs from raw process output")
            require(evaluate_case(case_id, name, kind, host, command, out, err, artifacts[name])["status"] == "pass", "Startup semantic contract failed")
            if host["execution_mode"] == "native":
                prefix = [] if host["os"] == "Windows" else [item["loader"]["path"]]
                require(command["argv"] == prefix + [artifacts[name]["path"], *arguments], "Startup invocation differs from required probe")
                self.evidence.read(artifacts[name])
            else:
                guest_case = unique(self.guest(item["guest_report"])["cases"])[case_id]
                require(command["argv"] == guest_case["argv"] and out == guest_case["actual"]["stdout"] and err == guest_case["actual"]["stderr"], "Startup importer differs from actual guest case")
            self.trace(item, case, host, "startup")
        self.loader(item, host)
        if key:
            self.coverage[key]["startup"].append({"report_id": item["report_id"], "host": host, "ci": item.get("ci"), "status": "pass"})

    def loader(self, item, host):
        if host["execution_mode"] != "native" or host["os"] == "Windows":
            return
        loader = item["loader"]
        self.evidence.read(loader)
        helpers = self.helpers["helpers"]
        expected = [entry for entry in helpers if entry["os"] == host["os"] and entry["arch"] == host["execution_arch"]]
        require(len(expected) == 1 and same(loader, expected[0]), "Host loader differs from distributed bundle")
        require(loader.get("sha256_after", loader["sha256"]) == loader["sha256"], "Loader changed during execution")

    def trace(self, item, case, host, family):
        producer = self.producers[family]
        inherited_ci = self.guest(item["guest_report"]).get("ci") if item.get("guest_report") else None
        self.output.setdefault("case_index", []).append({"family": family, "report_id": item["report_id"],
            "case_id": case["case_id"], "status": case["status"], "source": item.get("source", producer.get("source")),
            "toolchain": item.get("toolchain"), "build": item.get("build"), "artifact": case.get("artifact", item.get("artifact")),
            "host": host, "ci": item.get("ci") or inherited_ci,
            "physical_host": item.get("execution_host", item.get("host")), "build_ci": producer.get("ci"),
            "ci_inheritance": "Verified guest-report edge; build CI remains separate" if family == "pages64k" and inherited_ci and not item.get("ci") else None,
            "started_at": case["started_at"], "finished_at": case["finished_at"],
            "commands": case.get("commands") or [{key: case.get(key) for key in ("argv", "stdout_log", "stderr_log", "timeout_seconds", "exit_code")}],
            "qemu": item.get("qemu"), "guest": item.get("guest")})

    def io(self, reference):
        item = self.evidence.report(reference, "native-io", {"host", "guest"})
        self.context(item, self.producers["io"])
        key, host = self.host(item, "io")
        artifact = self.artifacts["io"]["native-io"]
        require(len(item["artifacts"]) == 1 and same(item["artifacts"][0], artifact), "I/O artifact identity mismatch")
        cases = unique(item["cases"])
        required = IO_REQUIRED | ({"tty.console-handle-closed"} if host["os"] == "Windows" else set())
        require(set(cases) == required, "I/O required cases missing/unknown")
        if host["execution_mode"] == "emulated-system":
            executed = unique(self.guest(item["guest_report"])["cases"])["native-io.suite"]
            raw = executed["actual"]["stdout"]
            require(executed["actual"]["exit_code"] == 0 and not executed["actual"]["stderr"], "Guest I/O suite failed")
            rows = [json.loads(line) for line in raw.splitlines() if line.strip()]
        else:
            suite = [command for command in item["commands"] if "--suite" in command["argv"]]
            require(len(suite) == 1, "Exactly one actual I/O suite execution required")
            raw, err = self.evidence.command(suite[0], 0)
            require(not err, "I/O suite produced unexpected stderr")
            self.evidence.read(item["process_fixture"])
            require(same(item["process_fixture"], artifact), "I/O Unicode fixture bytes differ")
            require(item["process_fixture"]["child_argv"] == ["--child", "参数 空格", 'quote"slash\\', "literal&|;$()"], "I/O child argv fixture contract mismatch")
            prefix = [] if host["os"] == "Windows" else [item["loader"]["path"]]
            require(suite[0]["argv"] == prefix + [item["process_fixture"]["path"], "--suite"] + (["--launcher", item["loader"]["path"]] if prefix else []), "I/O suite invocation mismatch")
            rows = [json.loads(line) for line in raw.splitlines() if line.strip()]
            if host["os"] == "Windows":
                terminals = [command for command in item["commands"] if command.get("results_log")]
                require(len(terminals) == 1, "Actual controlled Windows console command required")
                terminal = terminals[0]
                self.evidence.command(terminal, 0)
                require(not terminal.get("error") and terminal["cleanup"] and all(terminal["cleanup"].values()), "ConPTY cleanup failed")
                content = decode(self.evidence.read(terminal["results_log"]))
                rows.extend(json.loads(line) for line in content.splitlines() if line.strip())
                rows.append({"case_id": "tty.cleanup", "status": "pass", "actual": terminal["cleanup"]})
            self.loader(item, host)
        observations = unique(rows)
        require(set(observations) == required, "Raw I/O suite case coverage mismatch")
        for case_id, case in cases.items():
            require(case["status"] == "pass" and same(case["artifact"], artifact), "Failed/stale I/O case")
            require(case["actual"] == observations[case_id]["actual"] and observations[case_id]["status"] == "pass", "I/O case actual differs from raw OS result")
            require(matches_contract(case, host), "I/O native semantics failed: " + case_id)
            require(len(case["commands"]) == 1, "I/O case lacks actual command")
            self.evidence.command(case["commands"][0], 0)
            self.trace(item, case, host, "io")
        if key:
            self.coverage[key]["io"].append({"report_id": item["report_id"], "host": host, "ci": item.get("ci"), "status": "pass"})

    def guest(self, reference):
        key = str(self.evidence.resolve(reference["path"]))
        if key in self.guests:
            self.evidence.read(reference)
            return self.guests[key]
        item = self.evidence.report(reference, "arm64-guest")
        self.ci(item, item["host"])
        lock = read_json(ROOT / "guests/arm64.lock.json")
        require(item["guest_lock"]["sha256"] == digest((ROOT / "guests/arm64.lock.json").read_bytes()), "Guest lock identity mismatch")
        facts, qemu = item["guest"], item["qemu"]
        page = facts["page_size"]
        require(page in (4096, 65536), "Wrong guest page configuration")
        profile = lock["profiles"][str(page)]
        require(facts["os"] == "Linux" and facts["architecture"] == "aarch64" and facts["execution_mode"] == "emulated-system" and
                facts["page_size_alias"] == page and facts["smaps_kib"] == page // 1024 and facts["kernel"] == profile["kernel_release"], "Actual guest facts disagree with locked target")
        require(qemu["acceleration"] == "tcg,thread=single" and qemu["machine"] == lock["qemu"]["machine"] and
                qemu["cpu"] == lock["qemu"]["cpu"] and ("QEMU emulator version " + lock["qemu"]["version"]) in qemu["version"], "Full-system locked QEMU/TCG required")
        require(qemu["guest_arch"] == "aarch64" and qemu["host_arch"] == item["host"]["execution_arch"], "QEMU architecture mismatch")
        require(qemu["image_page_code"] == {4096: 1, 65536: 3}[page], "Guest Image page flag contradicts actual page configuration")
        expected_profile = profile
        if not {"image", "config"} <= set(profile):
            supplemental_path = ROOT / "guests/arm64-derived.lock.json"
            supplemental = read_json(supplemental_path)
            require(supplemental["schema_version"] == 1 and supplemental["kind"] == "arm64-package-derived-identities" and
                    supplemental["guest_lock_sha256"] == digest((ROOT / "guests/arm64.lock.json").read_bytes()), "Derived guest identities are not bound to canonical package lock")
            expected_profile = supplemental["profiles"][str(page)]
            expected_packages = {name: {key: lock["packages"][name][key] for key in ("sha256", "size_bytes")} for name in profile["packages"]}
            require(expected_profile["packages"] == expected_packages and expected_profile["kernel_release"] == profile["kernel_release"], "Derived guest identities use different pinned packages")
            require(same(qemu["compressed_kernel"], expected_profile["compressed_kernel"]), "Guest compressed kernel identity mismatch")
            self.output.setdefault("guest_derivations", {})[str(page)] = {"supplement_sha256": digest(supplemental_path.read_bytes()),
                "source_lock_sha256": supplemental["guest_lock_sha256"], "derivation": supplemental["derivation"], "identities": expected_profile}
        for field, locked in (("kernel", "image"), ("config", "config")):
            require(same(qemu[field], expected_profile[locked]), "Guest kernel/config identity mismatch")
        for field in ("rootfs", "executable", "compressed_kernel"):
            require(re.fullmatch(r"[0-9a-f]{64}", qemu[field]["sha256"]) and qemu[field]["size_bytes"] > 0, "Missing QEMU/guest provenance: " + field)
        self.evidence.read(qemu["init"])
        serial = decode(self.evidence.read(qemu["serial_log"]))
        self.evidence.read(qemu["stderr_log"])
        require(item["cleanup"]["qemu_exited"] is True and item["cleanup"]["timed_out"] is False and item["cleanup"]["ram_only"] is True and
                item["cleanup"]["disks"] == [] and item["cleanup"]["network"] == "none" and item["cleanup"]["host_global_changes"] is False, "Guest transport cleanup/invocation failed")
        commands = [command for command in item["commands"] if "-accel" in command["argv"]]
        require(len(commands) == 1, "Actual full-system QEMU command required")
        command = commands[0]
        self.evidence.command(command, 0)
        argv = command["argv"]
        for option, value in (("-accel", "tcg,thread=single"), ("-machine", qemu["machine"]), ("-cpu", qemu["cpu"]),
                              ("-nic", "none"), ("-kernel", qemu["kernel"]["path"]), ("-initrd", qemu["rootfs"]["path"])):
            require(argv.count(option) == 1 and argv[argv.index(option) + 1] == value, "QEMU command identity mismatch: " + option)
        inputs = {PurePosixPath(normalized(entry["path"])).name: entry for entry in item["inputs"]}
        for name in [*lock["metadata"], *lock["common_packages"], *profile["packages"]]:
            require(name in inputs and same(inputs[name], lock["metadata"].get(name) or lock["packages"][name]), "Guest package/signature input mismatch")
        require(item["signature_chain"]["status"] == "pass" and item["signature_chain"]["signer_fingerprint"] == lock["signer_fingerprint"], "Guest signature provenance mismatch")
        token = "QAPLET_" + item["report_id"].replace("-", "")
        observed_facts, hashes, after, records, done = {}, {}, {}, {}, 0
        for line in serial.splitlines():
            if not line.startswith(token + " "):
                continue
            kind, value = line.rstrip("\r")[len(token) + 1:].split(" ", 1)
            if kind == "FACT":
                name, content = value.split(" ", 1)
                require(name not in observed_facts, "Duplicate serial fact")
                observed_facts[name] = int(content) if name in ("page_size", "page_size_alias", "smaps_kib") else content
            elif kind in ("HASH", "HASH_AFTER"):
                name, content = value.rsplit(" ", 1)
                destination = hashes if kind == "HASH" else after
                require(name not in destination, "Duplicate serial payload hash")
                destination[name] = content
            elif kind == "CASE":
                name, field, content = value.split(" ", 2)
                require(field not in records.setdefault(name, {}), "Duplicate serial case field")
                records[name][field] = content
            elif kind == "DONE":
                done += 1
            else:
                raise ValueError("Unknown guest serial token")
        require(done == 1 and observed_facts == {key: facts[key] for key in observed_facts} and
                set(observed_facts) == {"os", "architecture", "kernel", "page_size", "page_size_alias", "smaps_kib"}, "Guest serial facts/DONE mismatch")
        files = unique(item["files"], "guest_path")
        expected_hashes = {path: info["sha256"] for path, info in files.items()}
        require(hashes == after == expected_hashes == facts["file_sha256"] == facts["file_sha256_after"], "Guest payload before/after transport mismatch")
        requirements = guest_requirements(page)
        for family in requirements["families"]:
            for artifact in self.artifacts[family].values():
                found = [entry for entry in files.values() if same(entry, artifact)]
                require(len(found) == 1, "Guest lacks selected Core payload")
                self.evidence.read(found[0])
        require("/payload/ape-aarch64.elf" in files and same(files["/payload/ape-aarch64.elf"], self.producers["startup"]["portable_helpers"]["loader"]), "Guest loader differs from selected producer")
        self.evidence.read(files["/payload/ape-aarch64.elf"])
        cases = unique(item["cases"])
        require(set(cases) == set(records), "Guest serial/raw case coverage mismatch")
        for name, case in cases.items():
            raw = records[name]
            require(set(raw) == {"CWD", "START", "END", "EXIT", "STDOUT", "STDERR"}, "Incomplete actual guest command")
            require(case["status"] == "pass" and int(raw["EXIT"]) == case["exit_code"] == case["actual"]["exit_code"] and
                    case["cwd"] == raw["CWD"] and case["started_at"] == raw["START"] and case["finished_at"] == raw["END"], "Guest case contradicts serial command")
            interval(case)
            require(0 < case["timeout_seconds"] <= 300, "Guest case deadline missing")
            for field in ("stdout", "stderr"):
                actual = base64.b64decode(raw[field.upper()], validate=True)
                require(actual == self.evidence.read(case[field + "_log"]) and actual.decode("utf-8", "replace") == case["actual"][field], "Guest raw payload differs from serial")
        expected_commands = {case_id: ["/payload/ape-aarch64.elf", "/payload/" + name + ".com", *arguments]
                             for case_id, name, arguments, _ in STARTUP_CASES}
        io_path = next(path for path, info in files.items() if same(info, self.artifacts["io"]["native-io"]))
        expected_commands["native-io.suite"] = ["/payload/ape-aarch64.elf", io_path, "--suite", "--launcher", "/payload/ape-aarch64.elf"]
        if page == 65536:
            expected_commands.update({"pages64k." + name: ["/payload/ape-aarch64.elf", "/payload/pages64k.com", "--" + name] for name in ("facts", "suite")})
            expected_commands["pages64k.shell-header"] = ["/payload/busybox", "sh", "/payload/pages64k.com", "--facts"]
        require(set(expected_commands) == set(requirements["case_ids"]), "Guest required catalog is inconsistent")
        require(set(cases) == set(expected_commands) and all(case["argv"] == expected_commands[name] for name, case in cases.items()), "Guest invocation catalog mismatch")
        execution = {"os": facts["os"], "execution_arch": facts["architecture"], "page_size": page}
        for case_id, name, _, kind in STARTUP_CASES:
            case = cases[case_id]
            require(case["exit_code"] != 137 and evaluate_case(case_id, name, kind, execution, {**case, "timed_out": False},
                    case["actual"]["stdout"], case["actual"]["stderr"], self.artifacts["startup"][name])["status"] == "pass", "Guest startup semantic contract failed")
        suite = cases["native-io.suite"]
        require(suite["exit_code"] == 0 and not suite["actual"]["stderr"], "Guest I/O suite did not exit cleanly")
        observations = unique([json.loads(line) for line in suite["actual"]["stdout"].splitlines() if line.strip()])
        require(set(observations) == IO_REQUIRED and all(row["status"] == "pass" and matches_contract(row, execution) for row in observations.values()), "Guest raw I/O semantics/coverage failed")
        self.guests[key] = item
        return item

    def deep(self, reference):
        item = self.evidence.report(reference, "pages64k-validation")
        producer = self.producers["pages64k"]
        self.evidence.read(item["producer"])
        require(same(item["producer"], self.identity(self.producer_paths[producer["report_id"]])), "64K selected producer report identity mismatch")
        require(same(item["artifact"], producer["artifacts"][0]) and item["build"] == producer["build"] and item["toolchain"] == producer["toolchain"], "64K producer identity mismatch")
        if item.get("guest_report"):
            guest = self.guest(item["guest_report"])
            host = {"os": "Linux", "execution_arch": "aarch64", "page_size": guest["guest"]["page_size"], "execution_mode": "emulated-system", "kernel": guest["guest"]["kernel"]}
            require(host["page_size"] == 65536 and item["guest"] == guest["guest"] and item["qemu"] == guest["qemu"], "64K deep probe target mismatch")
            executions = unique(guest["cases"])
            for name in ("pages64k.facts", "pages64k.shell-header"):
                case = executions[name]
                require(case["actual"]["exit_code"] == 0 and not case["actual"]["stderr"] and json.loads(case["actual"]["stdout"]) == {"probe": "pages64k", "arch": "aarch64", "page_size": 65536}, "64K facts/shell-header execution failed")
            suite = executions["pages64k.suite"]
            require(suite["actual"]["exit_code"] == 0 and not suite["actual"]["stderr"], "Deep guest suite did not complete")
            stdout = suite["actual"]["stdout"]
        else:
            host = item["host"]
            require(not item.get("guest") and not item.get("qemu"), "64K native execution cannot carry an emulated guest identity")
            require(host["os"] == "Linux" and host["cpu"] == host["execution_arch"] == "aarch64" and
                    host["execution_mode"] == "native" and host["page_size"] == 65536 and host["kernel"], "64K native execution requires actual Linux/ARM64/65536")
            executions = unique(item["executions"])
            require(set(executions) == {"pages64k.facts", "pages64k.suite", "pages64k.shell-header"}, "64K native execution catalog incomplete")
            self.evidence.read(item["loader"])
            require(same(item["loader"], producer["loader"]), "64K native loader differs from producer")
            for name, execution in executions.items():
                out, err = self.evidence.command(execution["command"], 0)
                require(out == execution["stdout"] and not err, "64K native raw output mismatch")
                expected = (["/bin/sh"] if name == "pages64k.shell-header" else [item["loader"]["path"]]) + [item["artifact"]["path"], "--suite" if name == "pages64k.suite" else "--facts"]
                require(execution["command"]["argv"] == expected, "64K native invocation mismatch")
                if name != "pages64k.suite":
                    require(json.loads(out) == {"probe": "pages64k", "arch": "aarch64", "page_size": 65536}, "64K native architecture/page measurement mismatch")
            stdout = executions["pages64k.suite"]["stdout"]
        observations = unique([json.loads(line) for line in stdout.splitlines()])
        cases = unique(item["cases"])
        required = {"pages64k." + name for name in DEEP_REQUIRED}
        require(set(cases) == set(observations) == required, "64K required cases missing/unknown")
        contracts = {name: 0 for name in required}
        contracts.update({"pages64k.environment": 65536, "pages64k.unaligned-munmap-rejected": 22,
                          "pages64k.zero-map-rejected": 22, "pages64k.unaligned-map-rejected": -1,
                          "pages64k.unaligned-file-map-rejected": 22, "pages64k.zip-missing-rejected": 2})
        for name, case in cases.items():
            require(case["status"] == observations[name]["status"] == "pass" and case["actual"] == observations[name]["actual"] == contracts[name] and case["expected"] == contracts[name], "64K native semantic contract failed: " + name)
            require(same(case["artifact"], item["artifact"]), "64K case artifact mismatch")
            if name == "pages64k.unaligned-map-rejected":
                require(case["observed_errno"] in (22, 1) and observations[name]["observed_errno"] == case["observed_errno"], "64K unaligned map errno mismatch")
            if item.get("guest_report"):
                self.evidence.read(case["stdout_log"])
                self.evidence.read(case["stderr_log"])
                require(case["argv"] == executions["pages64k.suite"]["argv"], "64K imported case invocation mismatch")
            else:
                require(case["commands"] == [executions["pages64k.suite"]["command"]], "64K native case detached from actual suite")
            self.trace(item, case, host, "pages64k")
        self.output["deep_probe"] = {"status": "pass", "report_id": item["report_id"], "host": host, "cases": len(cases)}

    def run(self):
        self.producer_paths = {}
        for family in ("startup", "io", "pages64k"):
            self.producer(family, self.manifest["producers"][family])
        producer_path = self.evidence.resolve(self.manifest["producers"]["startup"])
        self.helpers = self.evidence.json(str(producer_path.parent / "loaders/helpers.json"))
        require(self.helpers["report_kind"] == "ape-loaders" and self.helpers["status"] == "pass", "Distributed loader bundle required")
        for helper in self.helpers["helpers"]:
            self.evidence.read(str(producer_path.parent / "loaders" / helper["file"]))
            require(same(helper, self.identity(str(producer_path.parent / "loaders" / helper["file"]))), "Distributed helper bytes differ from loader manifest")
        for field, method in (("startup_reports", self.startup), ("io_reports", self.io), ("pages64k_reports", self.deep)):
            require(self.manifest.get(field), "Required execution report set missing: " + field)
            for reference in self.manifest[field]:
                method(reference)
        for family in ("startup", "io"):
            require(not arm64_page_gaps(self.native_arm[family], self.guest_arm[family]), "Missing required ARM64 page target execution: " + family)
        for slot in self.coverage.values():
            require(slot["startup"] and slot["io"], "Missing required target execution: " + slot["target"])
        self.offline(self.manifest["offline_report"])
        self.dependencies(producer_path.parent)
        self.output["coverage"] = list(self.coverage.values())
        self.output["transports"] = self.evidence.transports
        self.output["producers"] = {family: {"report": self.manifest["producers"][family], "report_id": item["report_id"],
                                             **{key: item.get(key) for key in ("source", "toolchain", "build", "host", "ci", "artifacts", "portable_helpers")}}
                                    for family, item in self.producers.items()}

    def dependencies(self, producer_root):
        quickjs = component("quickjs")
        quickjs_license = (ROOT / "third_party/quickjs/LICENSE").read_bytes()
        require(digest(quickjs_license) == quickjs["license"]["sha256"], "QuickJS original license identity mismatch")
        helper_lock = component("ape_helpers")
        notices = producer_root / "artifacts/ape-helper-notices"
        for name in ("LICENSE", "source.lock.json", "HACL-LICENSE.txt"):
            self.evidence.read(str(notices / name))
        require(self.evidence.read(str(notices / "LICENSE")) == (ROOT / "third_party/ape-helpers/LICENSE").read_bytes() and
                self.evidence.json(str(notices / "source.lock.json")) == helper_lock, "Delivered helper notices do not match actual source closure")
        files = {entry["path"]: entry for entry in self.nightly["files"]}
        archives = [files[arch + "-linux-cosmo/lib/libcosmo.a"] for arch in ("x86_64", "aarch64")]
        validation = read_json(ROOT / "guests/arm64.lock.json")
        self.output["dependencies"] = {
            "project": {"license": "MIT", "license_sha256": digest((ROOT / "LICENSE").read_bytes()), "scope": "Project-owned sources only"},
            "quickjs": {"repository": quickjs["repository"], "commit": quickjs["commit"], "source_sha256": quickjs["source"]["sha256"],
                        "patches": quickjs["patches"], "license": {"spdx": "MIT", **quickjs["license"]}},
            "runtime_libc": {"archive": self.nightly["archive"], "archives": archives, "source": self.nightly["libc_source"],
                             "licensing": "Embedded notice licenses apply per component; no blanket ISC/MIT or complete source mapping asserted",
                             "observed_notice_symbols": sorted(self.notice_symbols),
                             "notice_evidence": "Both CPU link maps and linked-symbol inventories in link_audits"},
            "nightly_tools": {"role": "Build tools; tool licenses do not relicense Core", "provenance": self.nightly["source"], "tools": self.nightly["tools"],
                              "license_files": [entry for path, entry in files.items() if path.startswith("LICENSE.")]},
            "portable_helpers": {"role": "Independent source-built loader and build-only packer closure", "commit": helper_lock["commit"],
                                 "files": [{key: entry[key] for key in ("path", "sha256", "license", "purpose")} for entry in helper_lock["files"]],
                                 "delivered_notices": [str(notices / name) for name in ("LICENSE", "source.lock.json", "HACL-LICENSE.txt")],
                                 "per_build_identities": {family: {key: producer["portable_helpers"][key] for key in ("loader", "packer", "recipe", "patched_source")}
                                                          for family, producer in self.producers.items()}},
            "startup_helper_build_tools": {"role": "Build-only Zig and separate Apple Silicon native loader", "lock": component("startup_loaders")},
            "guest_validation": {"role": "Validation inputs/tools, separate from shipped Core", "lock_sha256": digest((ROOT / "guests/arm64.lock.json").read_bytes()),
                                 "packages": validation["packages"], "qemu": [guest["qemu"] for guest in self.guests.values()]},
            "network_tls": {"backend": "unknown", "version": "unknown", "cve_applicability": "unverified",
                            "historical_observation": "The plan's Mbed TLS 2.26.0 observation has not been mapped to this Nightly archive",
                            "evidence_scope": "Thread-local storage probes and tls-named compiler/header files do not validate network TLS",
                            "decision": "No network TLS backend selected or implemented in M0", "follow_up": "M4: establish exact shipped TLS source/backend/version, license and CVE applicability before network TLS acceptance"}}

    def offline(self, reference):
        item = self.evidence.report(reference, "offline-integrity")
        child = self.evidence.report(item["child_report"], "offline-integrity")
        for field in ("network", "builds", "cases", "comparisons", "helper_comparisons", "cache_inventory", "recipe_inputs", "recipe_inputs_after", "trace_audits"):
            require(item[field] == child[field], "Offline parent/isolated child conflict: " + field)
        net = item["network"]
        require(net["parent_namespace"] != net["child_namespace"] and all(re.fullmatch(r"net:\[\d+\]", net[key]) for key in ("parent_namespace", "child_namespace")) and
                net["uid"] > 0 and net["gid"] > 0 and net["interfaces"] == ["lo"] and not net["ipv4_routes"].strip().splitlines()[1:] and
                net["blocked_connect"] == {"address": "203.0.113.1", "port": 443, "errno": 101}, "Offline actual namespace/nonroot/network evidence incomplete")
        namespace = item["namespace_command"]
        self.evidence.command(namespace, 0)
        require("unshare" in namespace["argv"] and "--net" in namespace["argv"] and "setpriv" in namespace["argv"], "Offline report did not execute isolated child")
        refusal_names = {"missing-source", "changed-source", "changed-patch", "unlocked-head", "missing-archive", "corrupt-archive", "missing-toolchain-files",
                         "placeholder-digest", "conflicting-provenance", "missing-zig", "missing-helper-source", "missing-helper-loader", "changed-helper-loader", "changed-helper-packer"}
        cases = unique(item["cases"])
        require(set(cases) == {"offline.rebuild.first", "offline.rebuild.second"} | {"offline.refuse." + name for name in refusal_names}, "Offline rebuild/refusal catalog missing/unknown")
        for case in cases.values():
            require(case["status"] == "pass" and len(case["commands"]) == 1, "Offline required case did not execute/pass")
            self.evidence.command(case["commands"][0])
            if case["case_id"].startswith("offline.refuse."):
                require(case["actual"]["exit_code"] in (1, 2) and case["commands"][0]["exit_code"] == case["actual"]["exit_code"] and
                        case["actual"]["supplier_executable_commands"] == 0 and case["actual"]["status"] in ("fail", "blocked"), "Input refusal did not precede supplier execution")
                argv = case["commands"][0]["argv"]
                incoming = self.evidence.json(argv[argv.index("--report") + 1])
                require(incoming["status"] == case["actual"]["status"], "Supplier refusal report conflicts with case")
                require(not any(c.get("argv", [""])[0].endswith(("cosmocc", "-gcc", "zig")) for c in incoming.get("commands", [])), "Supplier executable was used before input refusal")
        builds = unique(item["builds"], "name")
        require(set(builds) == {"first", "second"} and builds["first"]["build_directory"] != builds["second"]["build_directory"], "Two independent fresh rebuilds required")
        producers = []
        for name, build in builds.items():
            require(build["status"] == "pass", "Disconnected build failed")
            self.evidence.command(build["command"], 0)
            require(build["command"] == cases["offline.rebuild." + name]["commands"][0], "Rebuild command identity conflict")
            producer = self.evidence.report(build["report"], "ape-build")
            self.toolchain(producer)
            require(producer["build_directory"] == build["build_directory"] and producer["source"] == item["source"] and
                    producer["source"]["sha256"] == self.producers["startup"]["source"]["sha256"] and
                    producer["source"]["commit"] == self.producers["startup"]["source"]["commit"] and
                    producer["source"]["patches"] == self.producers["startup"]["source"]["patches"], "Offline/current fixed source identities differ")
            require(producer["toolchain"] == self.producers["startup"]["toolchain"], "Offline/current toolchain identities differ")
            for artifact in producer["artifacts"]:
                require(same(artifact, self.artifacts["startup"][artifact["name"]]), "Offline product differs from delivered selected artifact")
                self.audit(artifact)
            producers.append(producer)
        comparisons = unique(item["comparisons"], "name")
        require(set(comparisons) == {name + suffix for name in ("hello", "quickjs-eval") for suffix in (".com", ".com.dbg", ".aarch64.elf")}, "Six disconnected binary comparisons required")
        for comparison in comparisons.values():
            for index, field in enumerate(("first", "second")):
                name = comparison["name"]
                expected_artifact = next(artifact for artifact in producers[index]["artifacts"] if name.startswith(artifact["name"] + "."))
                artifact_path = normalized(expected_artifact["path"])
                expected_path = artifact_path.rsplit("/", 1)[0] + "/" + name
                require(normalized(comparison[field]["path"]) == expected_path, "Offline binary comparison detached from actual producer")
            require(comparison["status"] == "pass" and comparison["first_difference_offset"] is None and
                    self.evidence.read(comparison["first"]) == self.evidence.read(comparison["second"]), "Disconnected binary bytes differ")
            require(normalized(comparison["first"]["path"]) != normalized(comparison["second"]["path"]), "Comparison reuses same binary")
        helpers = unique(item["helper_comparisons"], "name")
        require(set(helpers) == {"loader", "packer"}, "Both disconnected helper comparisons required")
        for name, comparison in helpers.items():
            require(comparison["status"] == "pass" and self.evidence.read(comparison["first"]) == self.evidence.read(comparison["second"]), "Disconnected helper bytes differ")
            require(same(comparison["first"], producers[0]["portable_helpers"][name]) and same(comparison["second"], producers[1]["portable_helpers"][name]), "Helper comparison detached from actual producer")
        traces = unique(item["trace_audits"], "label")
        require(set(traces) == {"rebuild-first", "rebuild-second"} | refusal_names, "Missing actual rebuild/refusal trace")
        for label, trace in traces.items():
            text = decode(self.evidence.read(trace["trace"]))
            lines = text.splitlines()
            count = sum("execve(" in line for line in lines)
            require(count == trace["execve_count"] and count > 0, "Missing actual process trace")
            require(not trace["internet_attempts"] and not any(re.search(r"\b(?:connect|sendto|sendmsg|sendmmsg)\(", line) and re.search(r"AF_INET6?\b", line) for line in lines), "Disconnected build attempted Internet traffic")
            if label in refusal_names:
                require(count == 1, "Supplier executable observed in refusal trace")
        inventory = item["cache_inventory"]
        before, after = [self.evidence.json(inventory[field]) for field in ("before", "after")]
        require(inventory["unchanged"] is True and before == after and before["selected"] == inventory["selected"], "Accepted input caches changed during offline execution")
        entries = before["entries"]
        require(digest(json.dumps(entries, sort_keys=True, separators=(",", ":")).encode()) == inventory["sha256"] == before["sha256"] and
                sum(entry["kind"] == "file" for entry in entries) == inventory["file_count"] and
                sum(entry.get("size_bytes", 0) for entry in entries) == inventory["size_bytes"], "Offline cache inventory fingerprint/count mismatch")
        require(len(unique(entries, "path")) == len(entries) and len(before["selected"]) >= 6, "Offline inventory omitted required input closure")
        require(item["recipe_inputs"] == item["recipe_inputs_after"], "Recipe changed during disconnected execution")
        recipe = self.evidence.json(self.manifest["offline_recipe_comparison"])
        require(recipe["status"] == "pass" and same(recipe["disconnected_report"], self.identity(reference)) and recipe["tested_recipe_inputs"] == item["recipe_inputs"], "Offline recipe receipt not bound to actual run")
        self.evidence.read(recipe["source_diff"])
        previous = None
        if recipe["changed_inputs"]:
            tested = self.evidence.resolve(self.manifest["offline_recipe_comparison"]).parent / "builder-tested.py"
            previous = self.evidence.read(str(tested))
        recipe_scope = validate_recipe_comparison(recipe, previous=previous)
        self.output["offline"] = {"status": "pass", "report_id": item["report_id"], "report": reference,
            "source": item["source"], "toolchain": item["toolchain"], "host": item["host"], "ci": None,
            "cases": item["cases"], "builds": item["builds"], "producer_reports": item["producer_reports"], "network": net, "binary_comparisons": item["comparisons"],
            "helper_comparisons": item["helper_comparisons"], "cache_inventory": inventory, "recipe_comparison": recipe,
            "scope": ("Actual pair rebuilt the current recipe and fixed inputs. " if recipe_scope == "identical" else
                      "Actual pair rebuilt the selected Hello/eval and fixed inputs. Current raw recipe fingerprint was not rebuilt; only check_ci AST differs. ") +
                     "Independent CI helper identities remain distinct."}


def evaluate(input_manifest, guest_report=None):
    report = {"schema_version": 1, "report_kind": "m0-core-gate", "report_id": str(uuid.uuid4()), "scope": "full",
              "started_at": now(), "finished_at": None,
              "core": {"status": "fail", "errors": []}, "svc": {"status": "not_run", "reports": []}}
    gate = None
    manifest = {}
    verdict = report["core"]
    if guest_report is not None:
        report.update(report_kind="m0-guest-validation", scope="guest-transport")
        report["core"]["status"] = "not_run"
        verdict = report["guest_validation"] = {"status": "fail", "errors": []}
    try:
        manifest = read_json(Path(input_manifest))
        report["selected_inputs"] = manifest
        gate = Gate(manifest, Path(input_manifest).resolve().parent, verdict)
        if guest_report is None:
            gate.run()
        else:
            gate.producer_paths = {}
            selected_guest = gate.evidence.json(str(guest_report))
            for family in guest_requirements(selected_guest["guest"]["page_size"])["families"]:
                gate.producer(family, manifest["producers"][family])
            incoming = gate.guest({"path": str(guest_report)})
            verdict.update(report_id=incoming["report_id"], host=incoming["host"], guest=incoming["guest"],
                           qemu=incoming["qemu"], cases=incoming["cases"], cleanup=incoming["cleanup"])
        verdict["status"] = "pass"
    except (OSError, ValueError, KeyError, TypeError, AttributeError, IndexError, StopIteration, BuildError, zipfile.BadZipFile) as error:
        verdict["errors"].append(type(error).__name__ + ": " + str(error))
    finally:
        if gate:
            report["evidence_index"] = list(gate.evidence.index.values())
        services = []
        for reference in manifest.get("svc_reports", []) if isinstance(manifest, dict) else []:
            try:
                path = gate.evidence.resolve(reference) if gate else Path(reference)
                service = read_json(path)
                artifacts = service.get("artifacts", [])
                if service.get("artifact"):
                    artifacts = [*artifacts, service["artifact"]]
                require(service.get("schema_version") == 1 and service.get("report_kind") in ("svc-scm", "svc-job", "svc-scm-run", "svc-job-run"), "Unsupported independent service execution evidence")
                core_ids = {artifact["sha256"] for family in gate.artifacts.values() for artifact in family.values()} if gate else set()
                require(not core_ids & {artifact.get("sha256") for artifact in artifacts}, "Service artifact is not independent of Core")
                raw = path.read_bytes()
                services.append({"path": str(path), "sha256": digest(raw), "size_bytes": len(raw), "report_id": service.get("report_id"),
                                 "status": service["status"], "reason": service.get("reason"), "artifacts": artifacts,
                                 "capability": "scm" if "scm" in service["report_kind"] else "job",
                                 "ci": service.get("ci"), "scope": service.get("scope"), "verification": "Independent status pointer; never executes service probes"})
            except (OSError, ValueError, TypeError, KeyError) as error:
                services.append({"path": str(reference), "status": "fail", "reason": str(error)})
        def service_status(items):
            states = [item["status"] for item in items]
            return "not_run" if not states else "fail" if "fail" in states else "blocked" if "blocked" in states else "not_run" if "not_run" in states else "pass" if all(state == "pass" for state in states) else "blocked"
        capabilities = {name: {"status": service_status([item for item in services if item.get("capability") == name]),
                               "reports": [item["path"] for item in services if item.get("capability") == name]} for name in ("scm", "job")}
        report["svc"] = {"status": service_status([*services, *capabilities.values()]), "reports": services,
                         "capabilities": capabilities, "core_blocking": False}
        report["finished_at"] = now()
        report["status"], report["m0_core_status"] = verdict["status"], report["core"]["status"]
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("operation", choices=("evaluate", "validate-guest"))
    parser.add_argument("--input-manifest", type=Path, required=True)
    parser.add_argument("--report", type=Path, required=True)
    parser.add_argument("--guest-report", type=Path)
    args = parser.parse_args()
    if (args.operation == "validate-guest") != bool(args.guest_report):
        parser.error("validate-guest requires --guest-report; evaluate does not accept it")
    report = evaluate(args.input_manifest, args.guest_report.resolve() if args.guest_report else None)
    write_json(args.report, report)
    print(json.dumps({"status": report["status"], "core": report["core"]["status"], "report": str(args.report)}))
    return 0 if report["status"] == "pass" else 1


if __name__ == "__main__":
    sys.exit(main())
