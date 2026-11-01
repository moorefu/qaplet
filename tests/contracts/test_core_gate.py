# SPDX-License-Identifier: MIT
"""Core completion is observed only through the public evidence-consumer CLI."""
import json
import os
import hashlib
import shutil
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
import zipfile

ROOT = Path(__file__).resolve().parents[2]


class CoreGateCLI(unittest.TestCase):
    def test_empty_results_cannot_complete_core(self):
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            manifest = directory / "inputs.json"
            manifest.write_text(json.dumps({"schema_version": 1, "profile": "full"}), encoding="utf-8")
            report = directory / "gate.json"
            result = subprocess.run([sys.executable, str(ROOT / "scripts/core_gate.py"), "evaluate",
                                     "--input-manifest", str(manifest), "--report", str(report)],
                                    capture_output=True, text=True, timeout=30)
            self.assertNotEqual(result.returncode, 0)
            self.assertTrue(report.is_file(), result.stderr)
            self.assertEqual(json.loads(report.read_text())["core"]["status"], "fail")

    @unittest.skipUnless(os.environ.get("QAPLET_GATE_MANIFEST"), "Explicit real full-matrix evidence required")
    def test_actual_complete_evidence_passes(self):
        with tempfile.TemporaryDirectory() as temporary:
            report = Path(temporary) / "gate.json"
            result = subprocess.run([sys.executable, str(ROOT / "scripts/core_gate.py"), "evaluate",
                                     "--input-manifest", os.environ["QAPLET_GATE_MANIFEST"], "--report", str(report)],
                                    capture_output=True, text=True, timeout=120)
            actual = json.loads(report.read_text())
            self.assertEqual(result.returncode, 0, actual["core"].get("errors"))
            self.assertEqual(actual["core"]["status"], "pass")
            self.assertEqual(len(actual["core"]["coverage"]), 6)


@unittest.skipUnless(os.environ.get("QAPLET_GATE_MANIFEST"), "Explicit real full-matrix evidence required")
class ActualCoreEvidence(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.directory = Path(self.temporary.name)
        self.manifest = json.loads(Path(os.environ["QAPLET_GATE_MANIFEST"]).read_text())

    def evaluate(self):
        manifest = self.directory / "inputs.json"
        manifest.write_text(json.dumps(self.manifest), encoding="utf-8")
        output = self.directory / "gate.json"
        process = subprocess.run([sys.executable, str(ROOT / "scripts/core_gate.py"), "evaluate",
                                  "--input-manifest", str(manifest), "--report", str(output)],
                                 capture_output=True, text=True, timeout=120)
        self.assertTrue(output.is_file(), process.stderr)
        return process.returncode, json.loads(output.read_text())

    def inject_report(self, selection, change, index=0, mutate_files=None):
        reference = Path(self.manifest[selection][index])
        transport = next(item for item in self.manifest["transports"] if reference.is_relative_to(Path(item["root"])))
        original = Path(transport["root"])
        moved = self.directory / ("artifact-" + str(len(list(self.directory.glob("artifact-*")))))
        shutil.copytree(original, moved)

        def relocate(value):
            if isinstance(value, str) and value.startswith(str(original)):
                return str(moved) + value[len(str(original)):]
            if isinstance(value, list):
                return [relocate(item) for item in value]
            if isinstance(value, dict):
                return {key: relocate(item) for key, item in value.items()}
            return value

        self.manifest = relocate(self.manifest)
        reference = moved / reference.relative_to(original)
        document = json.loads(reference.read_text(encoding="utf-8"))
        change(document)
        reference.write_text(json.dumps(document, ensure_ascii=False), encoding="utf-8")
        if mutate_files:
            mutate_files(moved)
        transport = next(item for item in self.manifest["transports"] if item["root"] == str(moved))
        archive = self.directory / (moved.name + ".zip")
        with zipfile.ZipFile(archive, "w") as bundle:
            for path in sorted(moved.rglob("*")):
                if path.is_file():
                    bundle.write(path, path.relative_to(moved).as_posix())
        raw = archive.read_bytes()
        transport["archive"] = {"path": str(archive), "sha256": hashlib.sha256(raw).hexdigest(), "size_bytes": len(raw)}

    def inject_offline(self, change):
        original = Path(self.manifest["offline_report"])
        report = json.loads(original.read_text())
        child_path = Path(next(item["root"] + report["child_report"]["path"][len(item["source"]):]
                               for item in self.manifest["path_mappings"] if report["child_report"]["path"].startswith(item["source"] + "/")))
        child = json.loads(child_path.read_text())
        change(report)
        change(child)
        copied_child = self.directory / "isolated.json"
        copied_child.write_text(json.dumps(child), encoding="utf-8")
        report["child_report"] = {"path": str(copied_child), "sha256": hashlib.sha256(copied_child.read_bytes()).hexdigest(), "size_bytes": copied_child.stat().st_size}
        copied = self.directory / "offline.json"
        copied.write_text(json.dumps(report), encoding="utf-8")
        recipe_path = Path(self.manifest["offline_recipe_comparison"])
        recipe = json.loads(recipe_path.read_text())
        recipe["disconnected_report"] = {"path": str(copied), "sha256": hashlib.sha256(copied.read_bytes()).hexdigest(), "size_bytes": copied.stat().st_size}
        copied_recipe = self.directory / "recipe.json"
        copied_recipe.write_text(json.dumps(recipe), encoding="utf-8")
        shutil.copyfile(recipe_path.parent / "builder-tested.py", self.directory / "builder-tested.py")
        self.manifest.update(offline_report=str(copied), offline_recipe_comparison=str(copied_recipe))

    def test_offline_comparisons_must_reference_the_two_actual_producers(self):
        def substitute(report):
            comparison = report["comparisons"][0]
            comparison.update(first=report["cache_inventory"]["before"], second=report["cache_inventory"]["after"])
        self.inject_offline(substitute)
        code, result = self.evaluate()
        self.assertNotEqual(code, 0)
        self.assertIn("comparison detached", " ".join(result["core"]["errors"]))

    def test_native_claim_cannot_carry_guest_identity(self):
        self.inject_report("startup_reports", lambda report: report.update(qemu={"acceleration": "tcg,thread=single"}))
        code, result = self.evaluate()
        self.assertNotEqual(code, 0)
        self.assertIn("QEMU", " ".join(result["core"]["errors"]))

    def test_ci_consumer_commit_must_match_selected_producer_even_with_matching_archive_suffix(self):
        def change(report):
            ci = report.get("ci") or dict(run_id="1", run_attempt="1", workflow="fixture", runner_name="fixture",
                runner_os="Linux", runner_arch="X64", image_os="fixture", image_version="1")
            report["ci"] = dict(ci, commit="b" * 40)
        self.inject_report("startup_reports", change)
        reference = Path(self.manifest["startup_reports"][0])
        transport = next(item for item in self.manifest["transports"] if reference.is_relative_to(Path(item["root"])))
        transport["name"] = "consumer-mutation-" + "b" * 40
        code, result = self.evaluate()
        self.assertNotEqual(code, 0)
        self.assertIn("CI", " ".join(result["core"]["errors"]))

    def test_mutated_real_startup_reports_cannot_hide_core_failures(self):
        mutations = {
            "missing-case": lambda report: report["cases"].pop(),
            "duplicate-case": lambda report: report["cases"].append(report["cases"][0]),
            "false-actual": lambda report: report["cases"][2]["actual"].update(stdout="43\n"),
            "skip-failure": lambda report: report["cases"][2].update(status="skip"),
            "hidden-nonzero": lambda report: report["cases"][2]["commands"][0].update(exit_code=7),
            "hidden-timeout": lambda report: report["cases"][2]["commands"][0].update(timed_out=True),
            "missing-deadline": lambda report: report["cases"][2]["commands"][0].pop("timeout_seconds"),
            "stale-artifact": lambda report: report["artifacts"][0].update(sha256="a" * 64),
            "case-artifact": lambda report: report["cases"][0]["artifact"].update(sha256="a" * 64),
            "fake-arm64": lambda report: report["host"].update(cpu="aarch64", execution_arch="aarch64"),
            "page-drift": lambda report: report["host"].update(page_size=16384),
            "translated": lambda report: report["host"].update(execution_mode="translated"),
            "unknown-host": lambda report: report["host"].update(os="Unknown"),
            "manifest-scope": lambda report: report.update(report_kind="ape-startup-manifest"),
            "no-commands": lambda report: report["cases"][0].update(commands=[]),
            "path-traversal": lambda report: report["cases"][0]["commands"][0].update(stdout_log="../unrelated.stdout.log"),
        }
        original = json.loads(Path(os.environ["QAPLET_GATE_MANIFEST"]).read_text())
        for name, mutate in mutations.items():
            with self.subTest(injection=name):
                self.manifest = json.loads(json.dumps(original))
                self.inject_report("startup_reports", mutate)
                code, report = self.evaluate()
                self.assertNotEqual(code, 0, name)
                self.assertEqual(report["core"]["status"], "fail", name)

    def test_missing_target_and_duplicate_report_cannot_complete_coverage(self):
        self.manifest["startup_reports"].pop(0)
        code, result = self.evaluate()
        self.assertNotEqual(code, 0)
        self.assertIn("Missing required target", " ".join(result["core"]["errors"]))
        self.manifest = json.loads(Path(os.environ["QAPLET_GATE_MANIFEST"]).read_text())
        self.manifest["startup_reports"].append(self.manifest["startup_reports"][0])
        code, result = self.evaluate()
        self.assertNotEqual(code, 0)
        self.assertIn("Duplicate/conflicting", " ".join(result["core"]["errors"]))

    def test_local_fast_profile_never_certifies_full_core(self):
        self.manifest["profile"] = "local-fast"
        code, result = self.evaluate()
        self.assertNotEqual(code, 0)
        self.assertEqual(result["core"]["status"], "fail")

    def test_mutated_real_io_and_deep_semantics_are_rejected(self):
        original = json.loads(Path(os.environ["QAPLET_GATE_MANIFEST"]).read_text())
        mutations = (
            ("io_reports", lambda report: report["cases"][1]["actual"].update(send=0)),
            ("io_reports", lambda report: report["cases"].pop()),
            ("io_reports", lambda report: report.update(scope="manifest")),
            ("io_reports", lambda report: report["artifacts"][0].update(sha256="b" * 64)),
            ("pages64k_reports", lambda report: report["cases"][0].update(actual=4096, expected=4096)),
            ("pages64k_reports", lambda report: report["cases"].pop()),
            ("pages64k_reports", lambda report: report["guest"].update(page_size=4096)),
        )
        for selection, change in mutations:
            with self.subTest(selection=selection, injection=str(change)):
                self.manifest = json.loads(json.dumps(original))
                self.inject_report(selection, change)
                code, report = self.evaluate()
                self.assertNotEqual(code, 0)
                self.assertEqual(report["core"]["status"], "fail")

    def test_missing_raw_log_cannot_be_repaired_by_same_named_file(self):
        def remove(root):
            report = json.loads((root / "startup.json").read_text())
            name = report["cases"][0]["commands"][0]["stdout_log"].replace("\\", "/").rsplit("/", 1)[-1]
            path = next(root.rglob(name))
            unrelated = root / "unrelated" / name
            unrelated.parent.mkdir()
            shutil.move(str(path), str(unrelated))
        self.inject_report("startup_reports", lambda report: None, mutate_files=remove)
        code, report = self.evaluate()
        self.assertNotEqual(code, 0)
        self.assertIn("FileNotFoundError", " ".join(report["core"]["errors"]))

    def test_offline_missing_build_refusal_trace_or_inventory_is_nonpassing(self):
        original = json.loads(Path(os.environ["QAPLET_GATE_MANIFEST"]).read_text())
        mutations = (
            lambda report: report["builds"].pop(),
            lambda report: report["cases"].pop(),
            lambda report: report["comparisons"].pop(),
            lambda report: report["helper_comparisons"].pop(),
            lambda report: report["trace_audits"].pop(),
            lambda report: report["cache_inventory"].update(unchanged=False),
            lambda report: report["network"].update(uid=0),
            lambda report: report["network"].update(interfaces=["lo", "eth0"]),
        )
        for mutation in mutations:
            with self.subTest(injection=str(mutation)):
                self.manifest = json.loads(json.dumps(original))
                self.inject_offline(mutation)
                code, report = self.evaluate()
                self.assertNotEqual(code, 0)
                self.assertEqual(report["core"]["status"], "fail")

    def test_actual_link_inventory_rejects_service_only_symbol(self):
        def poison(root):
            path = root / "build-info.json"
            report = json.loads(path.read_text())
            entry = report["artifacts"][0]["audit"]["maps"][0]
            target = root / "artifacts" / entry["path"].rsplit("/", 1)[-1]
            target.write_bytes(target.read_bytes() + b"\nCreateServiceW\n")
            entry.update(sha256=hashlib.sha256(target.read_bytes()).hexdigest(), size_bytes=target.stat().st_size)
            path.write_text(json.dumps(report), encoding="utf-8")
        self.inject_report("producers", lambda report: None, index="startup", mutate_files=poison)
        code, result = self.evaluate()
        self.assertNotEqual(code, 0)
        self.assertIn("Core contains service-only", " ".join(result["core"]["errors"]))

    def test_serial_execution_is_reparsed_after_all_declared_hashes_are_updated(self):
        def change_serial(root):
            guest_path = root / "guest.json"
            guest = json.loads(guest_path.read_text())
            old_guest_sha = hashlib.sha256(guest_path.read_bytes()).hexdigest()
            old = guest["qemu"]["serial_log"].copy()
            serial = next(root.rglob(old["path"].rsplit("/", 1)[-1]))
            token = "QAPLET_" + guest["report_id"].replace("-", "")
            raw = serial.read_bytes()
            raw = b"\n".join(line for line in raw.split(b"\n") if not line.startswith((token + " DONE ").encode()))
            serial.write_bytes(raw)
            replacement = {**old, "sha256": hashlib.sha256(raw).hexdigest(), "size_bytes": len(raw)}

            def replace(value, guest_identity=None):
                if isinstance(value, dict):
                    if value.get("path") == old["path"] and value.get("sha256") == old["sha256"]:
                        return replacement
                    if guest_identity and value.get("sha256") == old_guest_sha and value.get("path", "").endswith("/guest.json"):
                        return {**value, **guest_identity}
                    return {key: replace(item, guest_identity) for key, item in value.items()}
                if isinstance(value, list):
                    return [replace(item, guest_identity) for item in value]
                return value

            guest_path.write_text(json.dumps(replace(guest)), encoding="utf-8")
            new_identity = {"sha256": hashlib.sha256(guest_path.read_bytes()).hexdigest(), "size_bytes": guest_path.stat().st_size}
            for path in root.glob("*.json"):
                if path != guest_path:
                    path.write_text(json.dumps(replace(json.loads(path.read_text()), new_identity)), encoding="utf-8")
        self.inject_report("pages64k_reports", lambda report: None, mutate_files=change_serial)
        code, result = self.evaluate()
        self.assertNotEqual(code, 0)
        self.assertIn("serial facts/DONE", " ".join(result["core"]["errors"]))

    def test_changed_extracted_bytes_do_not_inherit_verified_archive_identity(self):
        original = next(item["archive"] for item in self.manifest["transports"]
                        if Path(self.manifest["startup_reports"][0]).is_relative_to(Path(item["root"])))
        self.inject_report("startup_reports", lambda report: report["cases"][0].update(reason="Changed extracted byte"))
        next(item for item in self.manifest["transports"] if "artifact-" in item["root"])["archive"] = original
        code, report = self.evaluate()
        self.assertNotEqual(code, 0)
        self.assertIn("Extracted byte differs", " ".join(report["core"]["errors"]))

    def test_core_failure_still_fails_with_actual_passing_service_pointer(self):
        if not self.manifest.get("svc_reports"):
            self.skipTest("Explicit actual passing independent service pointer required")
        self.inject_report("startup_reports", lambda report: report["cases"][0].update(status="fail"))
        code, result = self.evaluate()
        self.assertNotEqual(code, 0)
        self.assertEqual(result["core"]["status"], "fail")
        self.assertEqual(result["svc"]["capabilities"]["scm"]["status"], "pass")

    def test_self_consistent_recipe_receipt_cannot_change_current_inputs(self):
        self.inject_offline(lambda report: None)
        path = Path(self.manifest["offline_recipe_comparison"])
        receipt = json.loads(path.read_text())
        receipt["current_recipe_inputs"][0]["sha256"] = "f" * 64
        receipt["current_recipe_inventory_sha256"] = hashlib.sha256(json.dumps(receipt["current_recipe_inputs"], sort_keys=True, separators=(",", ":")).encode()).hexdigest()
        path.write_text(json.dumps(receipt), encoding="utf-8")
        code, result = self.evaluate()
        self.assertNotEqual(code, 0)
        self.assertIn("Current recipe input", " ".join(result["core"]["errors"]))

    def test_ci_path_only_logs_require_verified_transport_provenance(self):
        self.manifest["transports"] = []
        code, result = self.evaluate()
        self.assertNotEqual(code, 0)
        self.assertIn("verified archive", " ".join(result["core"]["errors"]))

    def test_producer_pass_label_does_not_hide_its_native_case_failure(self):
        self.inject_report("producers", lambda report: report["cases"][0].update(status="fail"), index="startup")
        code, result = self.evaluate()
        self.assertNotEqual(code, 0)
        self.assertIn("Producer contains", " ".join(result["core"]["errors"]))

    def test_service_status_is_independent_of_actual_core(self):
        service = self.directory / "svc.json"
        for status in ("fail", "blocked", "not_run"):
            with self.subTest(status=status):
                service.write_text(json.dumps({"schema_version": 1, "report_kind": "svc-scm", "status": status,
                                               "reason": "Injected service-only failure", "artifacts": []}), encoding="utf-8")
                self.manifest["svc_reports"] = [str(service)]
                code, result = self.evaluate()
                self.assertEqual(code, 0, result["core"].get("errors"))
                self.assertEqual(result["core"]["status"], "pass")
                self.assertEqual(result["svc"]["reports"][0]["status"], status)

    def test_actual_dependency_inventory_preserves_unknown_libc_and_tls(self):
        code, result = self.evaluate()
        self.assertEqual(code, 0, result["core"].get("errors"))
        inventory = result["core"]["dependencies"]
        self.assertEqual(inventory["network_tls"]["backend"], "unknown")
        self.assertEqual(inventory["network_tls"]["version"], "unknown")
        self.assertEqual(inventory["runtime_libc"]["source"]["status"], "unproven")
        self.assertEqual(len(inventory["runtime_libc"]["archives"]), 2)
        self.assertEqual(inventory["quickjs"]["license"]["spdx"], "MIT")


if __name__ == "__main__":
    unittest.main()
