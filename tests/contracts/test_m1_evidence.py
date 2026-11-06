"""Strict M1 acceptance consumes actual sealed execution closures through the CLI."""
import json
import hashlib
import os
import re
from pathlib import Path
import subprocess
import shutil
import sys
import tempfile
import unittest
import uuid
import zipfile
from contextlib import contextmanager
from datetime import datetime, timedelta, timezone

ROOT = Path(__file__).resolve().parents[2]


def write(path, value):
    path.write_text(json.dumps(value, indent=2), encoding="utf-8")


def selected_path(manifest, reference):
    value = reference["path"] if isinstance(reference, dict) else reference
    value = value.replace("\\", "/")
    matches = [row for row in manifest["path_mappings"] if value == row["source"] or value.startswith(row["source"] + "/")]
    if matches:
        row = max(matches, key=lambda row: len(row["source"]))
        return Path(row["root"]) / value[len(row["source"]):].lstrip("/")
    return Path(value)


@contextmanager
def fixture():
    retained = os.environ.get("QAPLET_M1_TEST_OUT")
    if retained:
        root = Path(retained) / uuid.uuid4().hex
        root.mkdir(parents=True)
        yield root
    else:
        with tempfile.TemporaryDirectory(prefix="qaplet-m1-mutation-") as temp:
            yield Path(temp)


def cli(root, manifest):
    inputs = root / "inputs.json"
    write(inputs, manifest)
    config = root / "config.json"
    write(config, {"schema_version": 1, "profile": "m1-evidence", "input_manifest": str(inputs)})
    report = root / "result.json"
    process = subprocess.run([sys.executable, str(ROOT / "scripts/check_evidence.py"), "--config", str(config),
        "--report", str(report), "--preflight-only"], capture_output=True, text=True, timeout=300)
    return process, json.loads(report.read_text(encoding="utf-8"))


def clone_transport(root, manifest, reference):
    selected = selected_path(manifest, reference).resolve()
    row = next(row for row in manifest["transports"] if selected.is_relative_to(Path(row["root"]).resolve()))
    old = Path(row["root"]).resolve()
    moved = root / "bundle"
    shutil.copytree(old, moved)
    def replace(value):
        if isinstance(value, dict): return {key: replace(item) for key, item in value.items()}
        if isinstance(value, list): return [replace(item) for item in value]
        if isinstance(value, str):
            text = value.replace("\\", "/")
            prefix = old.as_posix()
            if text == prefix or text.startswith(prefix + "/"): return moved.as_posix() + text[len(prefix):]
        return value
    manifest = replace(manifest)
    row = next(item for item in manifest["transports"] if item["id"] == row["id"])
    return manifest, row, moved / selected.relative_to(old)


def reseal(root, manifest, transport):
    archive = root / "repacked-semantic.zip"
    bundle = Path(transport["root"])
    with zipfile.ZipFile(archive, "w", compression=zipfile.ZIP_DEFLATED, compresslevel=1) as output:
        for path in sorted(bundle.rglob("*")):
            if path.is_file(): output.write(path, path.relative_to(bundle).as_posix())
    transport["archive"] = {"path": str(archive), "sha256": hashlib.sha256(archive.read_bytes()).hexdigest(),
                            "size_bytes": archive.stat().st_size}
    catalog = json.loads(Path(manifest["provider_catalog"]).read_text(encoding="utf-8"))
    provider = next(row for row in catalog["artifacts"] if row["id"] == transport["id"])
    provider.update(digest="sha256:" + transport["archive"]["sha256"], size_in_bytes=transport["archive"]["size_bytes"])
    path = root / "repacked-fixture-provider-catalog.json"
    write(path, catalog)
    manifest["provider_catalog"] = str(path)


def repack_sdk(producer, distribution, removed):
    """Rehash all inner package edges so completeness is tested semantically."""
    (distribution / removed).unlink()
    manifest_path = distribution / "sdk/manifest.json"
    metadata = json.loads(manifest_path.read_text(encoding="utf-8"))
    metadata["components"] = [row for row in metadata["components"] if row["file"] != removed]
    write(manifest_path, metadata)
    def measured(name):
        data = (distribution / name).read_bytes()
        return {"file": name, "sha256": hashlib.sha256(data).hexdigest(), "size_bytes": len(data)}
    members = [measured(row["file"]) for row in producer["package"]["members"] if row["file"] != removed]
    archive = distribution / "qaplet-sdk.zip"
    with zipfile.ZipFile(archive, "w", compression=zipfile.ZIP_DEFLATED, compresslevel=1) as output:
        for row in members: output.write(distribution / row["file"], row["file"])
    producer["package"].update(measured("qaplet-sdk.zip"), members=members, manifest=measured("sdk/manifest.json"))
    producer["sdk"]["components"] = [{**row, "path": next(old["path"] for old in producer["sdk"]["components"] if old["file"] == row["file"])}
                                       for row in members if row["file"].startswith("sdk/")]
    producer["sdk"]["sha256"] = hashlib.sha256(json.dumps([row for row in members if row["file"].startswith("sdk/")],
                                                sort_keys=True, separators=(",", ":")).encode()).hexdigest()
    producer["distribution"] = [{**measured(row["file"]), "path": row["path"]}
                               for row in producer["distribution"] if row["file"] != removed]


NATIVE_MUTATIONS = ("missing_case", "duplicate_case", "empty_cases", "required_skip", "required_blocked", "required_not_run",
    "short_lifecycle", "guest_replaces_native", "translated_cpu", "wrong_page", "missing_kernel", "wrong_ci_commit", "missing_ci",
    "incomplete_ci", "wrong_ci_run", "wrong_sdk", "wrong_per_case_sdk", "wrong_ape", "wrong_loader", "nonzero_exit", "timed_out",
    "missing_timestamp", "reversed_time", "wrong_argv", "wrong_cwd", "wrong_module_path", "changed_raw_log", "forged_actual",
    "forged_expected_actual_and_log", "duplicate_report_id", "conflicting_report_id")
SDK_MUTATIONS = ("wrong_source", "wrong_sdk_aggregate", "wrong_recipe", "duplicate_component", "duplicate_distribution",
                 "inner_zip_tamper", "missing_public_source_repacked", "missing_hidden_archive_repacked", "audit_service_symbol")
GUEST_MUTATIONS = ("wrong_sdk_selection", "different_producer_selection", "wrong_source_selection", "wrong_actual_page",
                   "native_relabel", "required_skip", "missing_sdk_case", "cleanup_failed")
EXTERNAL_MUTATIONS = ("missing_compile", "compile_private_include", "wrong_link_archive", "compile_nonzero", "compile_timeout",
                      "prebuilt_output", "missing_build_log", "missing_case", "required_skip", "wrong_stdout")
SELECTION_MUTATIONS = ("missing_producer", "missing_package_consumer", "missing_64k", "missing_m0", "historic_m0",
                       "wrong_product_commit", "duplicate_target", "wrong_provider_id", "wrong_provider_name", "wrong_provider_commit",
                       "loose_report", "outer_zip_tamper", "extracted_report_tamper")


def mutate_native(name, incoming, manifest, original):
    case = incoming["cases"][0]
    command = case["commands"][0]
    if name == "missing_case": incoming["cases"].pop()
    elif name == "duplicate_case": incoming["cases"].append(incoming["cases"][0])
    elif name == "empty_cases": incoming["cases"] = []
    elif name.startswith("required_"): case["status"] = name.removeprefix("required_")
    elif name == "short_lifecycle": incoming["lifecycle"]["completed_cycles"] = 99
    elif name == "guest_replaces_native": incoming["host"]["execution_mode"] = "emulated-system"
    elif name == "translated_cpu": incoming["host"]["cpu"] = "aarch64" if incoming["host"]["cpu"] == "x86_64" else "x86_64"
    elif name == "wrong_page": incoming["host"]["page_size"] = 4096 if incoming["host"]["page_size"] == 65536 else 65536
    elif name == "missing_kernel": incoming["host"]["kernel"] = ""
    elif name == "wrong_ci_commit": incoming["ci"]["commit"] = "0" * 40
    elif name == "missing_ci": incoming["ci"] = None
    elif name == "incomplete_ci": incoming["ci"].pop("run_attempt")
    elif name == "wrong_ci_run": incoming["ci"]["run_id"] = "41486421618"
    elif name == "wrong_sdk": incoming["sdk"]["sha256"] = "0" * 64
    elif name == "wrong_per_case_sdk": case["sdk_sha256"] = "0" * 64
    elif name == "wrong_ape": case["artifact"]["sha256"] = "0" * 64
    elif name == "wrong_loader": incoming["loader"]["sha256"] = "0" * 64
    elif name == "nonzero_exit": command["exit_code"] = case["actual"]["exit_code"] = 1
    elif name == "timed_out": command["timed_out"] = case["actual"]["timed_out"] = True
    elif name == "missing_timestamp": command.pop("started_at")
    elif name == "reversed_time": command["started_at"], command["finished_at"] = command["finished_at"], command["started_at"]
    elif name == "wrong_argv": command["argv"].append("--fake-pass")
    elif name == "wrong_cwd": command["cwd"] = "/tmp/wrong-original-cwd"
    elif name == "wrong_module_path":
        case = next(row for row in incoming["cases"] if row["case_id"] == "qaplet.run-argv")
        case["commands"][0]["argv"][3] = "/tmp/current-checkout/cli-runtime.mjs"
    elif name == "changed_raw_log": selected_path(manifest, command["stdout_log"]).write_bytes(b"forged pass\n")
    elif name == "forged_actual": case["actual"]["stdout"] = "forged pass\n"
    elif name == "forged_expected_actual_and_log":
        case["expected"]["stdout"] = case["actual"]["stdout"] = "forged pass\n"
        selected_path(manifest, command["stdout_log"]).write_bytes(b"forged pass\n")
    elif name in ("duplicate_report_id", "conflicting_report_id"):
        incoming["report_id"] = json.loads(selected_path(original, original["native_reports"][1]).read_text(encoding="utf-8"))["report_id"]
        if name == "conflicting_report_id": incoming["reason"] = "Conflicting copy of another report identity"
    else: raise AssertionError(name)


def mutation_test(family, name):
    def test(self):
        original = json.loads(Path(os.environ["QAPLET_M1_INPUTS"]).read_text(encoding="utf-8"))
        manifest = json.loads(json.dumps(original))
        with fixture() as root:
            if family == "selection":
                if name in ("missing_producer", "missing_package_consumer", "missing_64k", "missing_m0"):
                    manifest.pop({"missing_producer": "producer_report", "missing_package_consumer": "package_consumer",
                                  "missing_64k": "sdk_guest", "missing_m0": "m0_manifest"}[name])
                elif name == "wrong_product_commit": manifest["product_commit"] = "0" * 40
                elif name == "historic_m0":
                    path = root / "historic-m0.json"
                    old = json.loads(Path(manifest["m0_manifest"]).read_text(encoding="utf-8"))
                    old["producers"]["startup"] = "/historic/M0-only/build-info.json"
                    write(path, old)
                    manifest["m0_manifest"] = str(path)
                elif name == "duplicate_target": manifest["native_reports"][1] = manifest["native_reports"][0]
                elif name.startswith("wrong_provider_"):
                    row = manifest["transports"][0]
                    if name == "wrong_provider_id": row["id"] = 1
                    elif name == "wrong_provider_name": row["name"] = "wrong-family-" + manifest["product_commit"]
                    else: row["name"] = row["name"].replace(manifest["product_commit"], "0" * 40)
                elif name == "loose_report":
                    path = root / "untransported-run.json"
                    shutil.copyfile(selected_path(manifest, manifest["native_reports"][0]), path)
                    manifest["native_reports"][0] = str(path)
                elif name == "outer_zip_tamper":
                    row = manifest["transports"][0]
                    path = root / "damaged-original.zip"
                    path.write_bytes(Path(row["archive"]["path"]).read_bytes() + b"TAMPER")
                    row["archive"]["path"] = str(path)
                elif name == "extracted_report_tamper":
                    manifest, row, path = clone_transport(root, manifest, manifest["native_reports"][0])
                    incoming = json.loads(path.read_text(encoding="utf-8"))
                    incoming["reason"] = "Extracted-only tamper; original ZIP remains unchanged"
                    write(path, incoming)
                else: raise AssertionError(name)
            else:
                reference = manifest["native_reports"][0] if family == "native" else manifest["producer_report"] if family == "sdk" else manifest["sdk_guest"]["guest_report"] if family == "guest" else manifest["package_consumer"]
                manifest, row, path = clone_transport(root, manifest, reference)
                incoming = json.loads(path.read_text(encoding="utf-8"))
                if family == "native": mutate_native(name, incoming, manifest, original)
                elif family == "sdk":
                    if name == "wrong_source": incoming["sdk_recipe"]["git_commit"] = "0" * 40
                    elif name == "wrong_sdk_aggregate": incoming["sdk"]["sha256"] = "0" * 64
                    elif name == "wrong_recipe": incoming["sdk_recipe"]["sha256"] = "0" * 64
                    elif name == "duplicate_component": incoming["sdk"]["components"].append(incoming["sdk"]["components"][0])
                    elif name == "duplicate_distribution": incoming["distribution"].append(incoming["distribution"][0])
                    elif name == "inner_zip_tamper":
                        package = path.parent / "artifacts/qaplet-sdk.zip"
                        package.write_bytes(package.read_bytes() + b"TAMPER")
                    elif name.startswith("missing_"):
                        removed = "sdk/host-stop.c" if name == "missing_public_source_repacked" else "sdk/lib/.aarch64/libqaplet.a"
                        repack_sdk(incoming, path.parent / "artifacts", removed)
                    elif name == "audit_service_symbol":
                        log = selected_path(manifest, incoming["artifacts"][0]["audit"]["commands"][0]["stdout_log"])
                        log.write_bytes(log.read_bytes() + b"\n00000000 T StartServiceCtrlDispatcherW\n")
                    else: raise AssertionError(name)
                elif family == "guest":
                    selection_path = selected_path(manifest, manifest["sdk_guest"]["selection"])
                    selection = json.loads(selection_path.read_text(encoding="utf-8"))
                    if name == "wrong_sdk_selection": selection["sdk_sha256"] = "0" * 64
                    elif name == "different_producer_selection": selection["producer_report"]["sha256"] = "0" * 64
                    elif name == "wrong_source_selection": selection["qaplet_commit"] = "0" * 40
                    elif name == "wrong_actual_page": incoming["guest"]["page_size"] = 4096
                    elif name == "native_relabel": incoming["guest"]["execution_mode"] = "native"
                    elif name == "required_skip": next(case for case in incoming["cases"] if case["case_id"].startswith("qaplet-host."))["status"] = "skip"
                    elif name == "missing_sdk_case": incoming["cases"] = [case for case in incoming["cases"] if case["case_id"] != "qaplet-host.stop-ordering"]
                    elif name == "cleanup_failed": incoming["cleanup"]["timed_out"] = True
                    else: raise AssertionError(name)
                    write(selection_path, selection)
                else:
                    commands = incoming["commands"]
                    compile = next(command for command in commands if "-c" in command["argv"] and incoming["external_consumer"]["source"] in command["argv"])
                    if name == "missing_compile": commands.remove(compile)
                    elif name == "compile_private_include": compile["argv"].insert(2, "-I/home/runner/work/qaplet/qaplet/src")
                    elif name == "wrong_link_archive":
                        link = next(command for command in commands if incoming["external_consumer"]["archive"] in command["argv"])
                        link["argv"][link["argv"].index(incoming["external_consumer"]["archive"])] = "/current/private/libqaplet.a"
                    elif name == "compile_nonzero": compile["exit_code"] = 1
                    elif name == "compile_timeout": compile["timed_out"] = True
                    elif name == "prebuilt_output":
                        incoming["artifacts"][0]["path"] = json.loads(selected_path(manifest, manifest["producer_report"]).read_text(encoding="utf-8"))["artifacts"][2]["path"]
                    elif name == "missing_build_log": incoming["artifacts"][0].pop("build_commands")
                    elif name == "missing_case": incoming["cases"].pop()
                    elif name == "required_skip": incoming["cases"][0]["status"] = "skip"
                    elif name == "wrong_stdout":
                        case = incoming["cases"][0]
                        case["actual"]["stdout"] = case["expected"]["stdout"] = "fake external success\n"
                        selected_path(manifest, case["commands"][0]["stdout_log"]).write_bytes(b"fake external success\n")
                    else: raise AssertionError(name)
                write(path, incoming)
                reseal(root, manifest, row)
            process, value = cli(root, manifest)
            self.assertEqual(process.returncode, 1, name + ": " + value.get("reason", process.stderr))
            self.assertEqual(value["status"], "fail")
            self.assertFalse(value.get("release_ready", False))
            self.assertTrue(value.get("reason"))
            if family != "selection":
                self.assertNotIn("Extracted byte", value["reason"], "Mutation never reached semantic validation")
                self.assertNotIn("Transport archive SHA", value["reason"], "Mutation never reached semantic validation")
    return test


@unittest.skipUnless(os.environ.get("QAPLET_M1_INPUTS"), "Explicit actual complete M1 closure required")
class ActualM1Evidence(unittest.TestCase):
    def test_failed_loose_m0_evidence_cannot_replace_a_consumed_raw_log(self):
        original = json.loads(Path(os.environ["QAPLET_M1_INPUTS"]).read_text(encoding="utf-8"))
        selected = json.loads(selected_path(original, original["m0_manifest"]).read_text(encoding="utf-8"))
        for consumer, case_index in (("core", 0), ("core", 1), ("strict", 1)):
            with self.subTest(consumer=consumer, raw_already_read=case_index == 0), fixture() as root:
                root = root.resolve()
                manifest = json.loads(json.dumps(selected))
                producer = json.loads(selected_path(manifest, manifest["producers"]["startup"]).read_text(encoding="utf-8"))
                command = producer["cases"][case_index]["commands"][0]
                raw = root / "loose-stdout.log"
                shutil.copyfile(selected_path(manifest, command["stdout_log"]), raw)
                before = hashlib.sha256(raw.read_bytes()).hexdigest()
                # A negative, transport-free fixture stops after reading the actual
                # stdout. No provider report or provider raw bytes are modified.
                producer["ci"] = None
                command["stdout_log"] = str(raw)
                producer["cases"][0]["commands"][0]["stderr_log"] = str(root / "missing-stderr.log")
                producer_path = root / "producer.json"
                write(producer_path, producer)
                manifest.pop("transports")
                manifest["producers"]["startup"] = str(producer_path)
                inputs = root / "m0.json"
                write(inputs, manifest)
                if consumer == "core":
                    arguments = ["core_gate.py", "evaluate", "--input-manifest", str(inputs)]
                else:
                    publications = root / "publications.json"
                    write(publications, {"reports": []})
                    config = root / "config.json"
                    write(config, {"schema_version": 1, "profile": "m0-evidence", "input_manifest": str(inputs),
                        "guest_manifest": str(inputs), "guest_report": str(inputs), "publication_inputs": str(publications)})
                    arguments = ["check_evidence.py", "--config", str(config), "--preflight-only"]
                def run(output):
                    return subprocess.run([sys.executable, str(ROOT / "scripts" / arguments[0]), *arguments[1:],
                        "--report", str(output)], capture_output=True, text=True, timeout=60)
                control_path = root / "unrelated-result.json"
                control = run(control_path)
                control_report = json.loads(control_path.read_text(encoding="utf-8"))
                self.assertNotEqual(control.returncode, 0)
                if consumer == "core":
                    self.assertIn("missing-stderr.log", " ".join(control_report["core"]["errors"]))
                    consumed = [entry["resolved_path"] for entry in control_report["evidence_index"]]
                    if case_index == 0:
                        self.assertIn(str(raw), consumed, "Control must actually consume the loose raw evidence")
                    else:
                        self.assertNotIn(str(raw), consumed, "Control must fail before the later selected raw input")
                process = run(raw)
                write(root / "raw-preservation.json", {"consumer": consumer, "before_sha256": before,
                    "after_sha256": hashlib.sha256(raw.read_bytes()).hexdigest(), "exit_code": process.returncode,
                    "stdout": process.stdout, "stderr": process.stderr})
                self.assertNotEqual(process.returncode, 0)
                self.assertEqual(hashlib.sha256(raw.read_bytes()).hexdigest(), before,
                    "Failed evidence consumer replaced selected raw evidence")
                self.assertIn("immutable", process.stdout + process.stderr)
                self.assertFalse(json.loads(process.stdout)["report_written"])

    def test_required_catalog_cannot_skip_error_be_empty_or_interrupt_to_pass(self):
        with fixture() as root:
            clone = root / "checkout"
            names = subprocess.check_output(["git", "-C", str(ROOT), "ls-files", "-z"]).decode().split("\x00")
            names += [str(path.relative_to(ROOT)) for path in (ROOT / "scripts").glob("*.py")]
            names += ["tests/contracts/test_m1_evidence.py"]
            for name in set(filter(None, names)):
                source, destination = ROOT / name, clone / name
                if source.is_file():
                    destination.parent.mkdir(parents=True, exist_ok=True)
                    shutil.copyfile(source, destination)
            runner = clone / "scripts/check_evidence.py"
            original = runner.read_text(encoding="utf-8")
            config = root / "runner-config.json"
            write(config, {"schema_version": 1, "profile": "m1-evidence", "input_manifest": os.environ["QAPLET_M1_INPUTS"]})
            for name, body in (("skip", "self.skipTest('required fixture skip')"),
                               ("error", "raise RuntimeError('required fixture error')"),
                               ("interrupt", "raise KeyboardInterrupt()"), ("empty", "pass")):
                with self.subTest(name=name):
                    (clone / "tests/contracts/test_m1_evidence.py").write_text("import unittest\nclass Required(unittest.TestCase):\n def test_required(self):\n  " + body + "\n", encoding="utf-8")
                    modules = 'M1_MODULES = ()' if name == "empty" else 'M1_MODULES = ("test_m1_evidence",)'
                    runner.write_text(re.sub(r'^M1_MODULES = .*$', modules, original, flags=re.MULTILINE), encoding="utf-8")
                    report = root / ("runner-" + name + ".json")
                    process = subprocess.run([sys.executable, str(runner), "--config", str(config), "--report", str(report)],
                        capture_output=True, text=True, timeout=300)
                    value = json.loads(report.read_text(encoding="utf-8"))
                    self.assertNotEqual(process.returncode, 0, name)
                    self.assertEqual(value["status"], "fail")
                    self.assertFalse(value["release_ready"])
                    if name == "skip": self.assertEqual(len(value["tests"]["skipped"]), 1)
                    if name == "error": self.assertEqual(value["tests"]["errors"], 1)
                    if name == "interrupt": self.assertEqual(value["tests"]["run"], 1)
                    if name == "empty": self.assertIn("empty", value["reason"])

    def test_actual_current_full_closure_is_accepted_separately_from_release(self):
        with fixture() as root:
            config = root / "config.json"
            config.write_text(json.dumps({"schema_version": 1, "profile": "m1-evidence",
                "input_manifest": os.environ["QAPLET_M1_INPUTS"]}), encoding="utf-8")
            report = root / "result.json"
            report.write_text("previous unrelated result", encoding="utf-8")
            result = subprocess.run([sys.executable, str(ROOT / "scripts/check_evidence.py"),
                "--config", str(config), "--report", str(report), "--preflight-only"],
                capture_output=True, text=True, timeout=300)
            value = json.loads(report.read_text(encoding="utf-8"))
            self.assertEqual(result.returncode, 0, value.get("reason", result.stderr))
            self.assertEqual(value["m1"]["status"], "pass")
            self.assertEqual(value["m0_core_status"], "pass")
            self.assertEqual(value["scope"], "evidence-preflight")
            self.assertEqual(len(value["m1"]["coverage"]), 5)
            self.assertFalse(value["release_ready"])

    def test_missing_target_is_rejected(self):
        manifest = json.loads(Path(os.environ["QAPLET_M1_INPUTS"]).read_text(encoding="utf-8"))
        manifest["native_reports"].pop()
        with fixture() as root:
            process, value = cli(root, manifest)
            self.assertEqual(process.returncode, 1, value)
            self.assertIn("five", value["reason"])

    def test_repacked_native_required_command_must_finish_within_its_recorded_deadline(self):
        manifest = json.loads(Path(os.environ["QAPLET_M1_INPUTS"]).read_text(encoding="utf-8"))
        with fixture() as root:
            manifest, transport, path = clone_transport(root, manifest, manifest["native_reports"][0])
            incoming = json.loads(path.read_text(encoding="utf-8"))
            case = next(row for row in incoming["cases"] if row["case_id"] == "qaplet-host.lifetime")
            case["timeout_seconds"] = case["commands"][0]["timeout_seconds"] = 0.000001
            write(path, incoming)
            reseal(root, manifest, transport)
            process, value = cli(root, manifest)
            self.assertEqual(process.returncode, 1, value["reason"])
            self.assertIn("deadline", value["reason"].lower())

    def test_report_output_cannot_replace_a_selected_immutable_input(self):
        manifest = json.loads(Path(os.environ["QAPLET_M1_INPUTS"]).read_text(encoding="utf-8"))
        with fixture() as root:
            manifest, transport, path = clone_transport(root, manifest, manifest["native_reports"][0])
            before = path.read_bytes()
            inputs, config = root / "inputs.json", root / "config.json"
            write(inputs, manifest)
            write(config, {"schema_version": 1, "profile": "m1-evidence", "input_manifest": str(inputs)})
            process = subprocess.run([sys.executable, str(ROOT / "scripts/check_evidence.py"), "--config", str(config),
                "--report", str(path), "--preflight-only"], capture_output=True, text=True, timeout=300)
            self.assertNotEqual(process.returncode, 0)
            self.assertEqual(path.read_bytes(), before, "Strict output replaced original selected report")

    def test_report_output_cannot_replace_selected_original_archives_including_m0(self):
        original = json.loads(Path(os.environ["QAPLET_M1_INPUTS"]).read_text(encoding="utf-8"))
        for nested in (False, True):
            with self.subTest(nested_m0=nested), fixture() as root:
                root = root.resolve()
                manifest = json.loads(json.dumps(original))
                selected = json.loads(selected_path(manifest, manifest["m0_manifest"]).read_text(encoding="utf-8")) if nested else manifest
                provider = Path(selected["transports"][0]["archive"]["path"])
                before = hashlib.sha256(provider.read_bytes()).hexdigest()
                archive = root / "cloned-provider-original.zip"
                shutil.copyfile(provider, archive)
                selected["transports"][0]["archive"]["path"] = str(archive)
                if nested:
                    m0 = root / "m0-inputs.json"
                    write(m0, selected)
                    manifest["m0_manifest"] = str(m0)
                inputs, config = root / "inputs.json", root / "config.json"
                write(inputs, manifest)
                write(config, {"schema_version": 1, "profile": "m1-evidence", "input_manifest": str(inputs)})
                process = subprocess.run([sys.executable, str(ROOT / "scripts/check_evidence.py"), "--config", str(config),
                    "--report", str(archive)], capture_output=True, text=True, timeout=30)
                write(root / "preservation.json", {"before_sha256": before,
                    "clone_after_sha256": hashlib.sha256(archive.read_bytes()).hexdigest(),
                    "provider_after_sha256": hashlib.sha256(provider.read_bytes()).hexdigest(), "exit_code": process.returncode})
                self.assertEqual(hashlib.sha256(archive.read_bytes()).hexdigest(), before)
                self.assertEqual(hashlib.sha256(provider.read_bytes()).hexdigest(), before)
                self.assertNotEqual(process.returncode, 0)
                self.assertIn("immutable", process.stdout)
                self.assertFalse((root / "evidence").exists(), "Collision began evidence execution")

    def test_repacked_sdk_audit_must_inspect_its_selected_cpu_sidecar(self):
        manifest = json.loads(Path(os.environ["QAPLET_M1_INPUTS"]).read_text(encoding="utf-8"))
        with fixture() as root:
            manifest, transport, path = clone_transport(root, manifest, manifest["producer_report"])
            producer = json.loads(path.read_text(encoding="utf-8"))
            producer["artifacts"][0]["audit"]["commands"][0]["argv"][-1] = producer["artifacts"][1]["headers"]["sidecars"][0]["path"]
            write(path, producer)
            reseal(root, manifest, transport)
            process, value = cli(root, manifest)
            self.assertEqual(process.returncode, 1)
            self.assertIn("inspection target", value["reason"].lower())

    def test_repacked_sdk_build_requires_actual_execution_commands(self):
        manifest = json.loads(Path(os.environ["QAPLET_M1_INPUTS"]).read_text(encoding="utf-8"))
        with fixture() as root:
            manifest, transport, path = clone_transport(root, manifest, manifest["producer_report"])
            producer = json.loads(path.read_text(encoding="utf-8"))
            producer["commands"] = []
            write(path, producer)
            reseal(root, manifest, transport)
            process, value = cli(root, manifest)
            self.assertEqual(process.returncode, 1)
            self.assertIn("producer execution", value["reason"].lower())

    def test_repacked_native_timestamps_compare_instants_across_timezones(self):
        original = json.loads(Path(os.environ["QAPLET_M1_INPUTS"]).read_text(encoding="utf-8"))
        for equivalent in (False, True):
            with self.subTest(equivalent_instant=equivalent), fixture() as root:
                manifest, transport, path = clone_transport(root, json.loads(json.dumps(original)), original["native_reports"][0])
                incoming = json.loads(path.read_text(encoding="utf-8"))
                case = incoming["cases"][0]
                for field in ("started_at", "finished_at"):
                    instant = datetime.fromisoformat(case[field])
                    changed = instant.astimezone(timezone(timedelta(hours=12))) if equivalent else instant.replace(tzinfo=timezone(timedelta(hours=12)))
                    case[field] = case["commands"][0][field] = changed.isoformat()
                write(path, incoming)
                reseal(root, manifest, transport)
                process, value = cli(root, manifest)
                self.assertEqual(process.returncode, 0 if equivalent else 1, value["reason"])
                if not equivalent: self.assertIn("outside report interval", value["reason"].lower())


for family, names in (("native", NATIVE_MUTATIONS), ("sdk", SDK_MUTATIONS), ("guest", GUEST_MUTATIONS), ("external", EXTERNAL_MUTATIONS), ("selection", SELECTION_MUTATIONS)):
    if family == "guest" and os.environ.get("QAPLET_M1_INPUTS") and json.loads(Path(os.environ["QAPLET_M1_INPUTS"]).read_text(encoding="utf-8"))["sdk_guest"] is None:
        continue  # No guest branch is selected when the actual native kernel is 64K.
    for name in names:
        setattr(ActualM1Evidence, "test_" + family + "_" + name, mutation_test(family, name))


if __name__ == "__main__":
    unittest.main()
