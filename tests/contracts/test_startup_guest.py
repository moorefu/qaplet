"""Guest import and measured ARM64 page-gap planning through the public CLI."""
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
CLI = ROOT / "scripts/startup.py"


class StartupGuestContract(unittest.TestCase):
    def invoke(self, *arguments):
        return subprocess.run([sys.executable, str(CLI), *map(str, arguments)],
                              capture_output=True, text=True, timeout=30)

    @unittest.skipUnless(os.environ.get("QAPLET_CANDIDATE_BUILD"), "Actual controlled candidate producer required")
    def test_candidate_export_verifies_source_built_arm64_loader_bytes(self):
        producer = Path(os.environ["QAPLET_CANDIDATE_BUILD"])
        reports = ROOT / "out/startup-guest-contracts"
        reports.mkdir(parents=True, exist_ok=True)
        work = Path(tempfile.mkdtemp(prefix="candidate-export-", dir=reports))
        with tempfile.TemporaryDirectory(prefix="qaplet-candidate-loader-") as temporary:
            loaders = Path(temporary)
            loader = loaders / "ape-aarch64.elf"
            shutil.copyfile(producer.parent / "artifacts/ape-aarch64.elf", loader)
            arguments = ["guest-manifest", "--controlled-probe", "--build-report", producer,
                         "--artifact-dir", producer.parent / "artifacts", "--loader-dir", loaders,
                         "--page-size", "65536", "--manifest", work / "manifest.json"]
            result = self.invoke(*arguments, "--report", work / "intact.json")
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            manifest = json.loads((work / "manifest.json").read_text(encoding="utf-8"))
            self.assertEqual(len(manifest["commands"]), 17)
            self.assertIn("quickjs-eval.pointer-roundtrip", [item["case_id"] for item in manifest["commands"]])
            changed = bytearray(loader.read_bytes())
            changed[-1] ^= 1
            loader.write_bytes(changed)
            result = self.invoke(*arguments, "--report", work / "corrupt.json")
            self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
            refused = json.loads((work / "corrupt.json").read_text(encoding="utf-8"))
            self.assertIn("SHA-256/size", refused["reason"])
            self.assertEqual(refused["commands"], [])

    def test_absent_native_evidence_plans_both_arm64_page_gaps_without_claiming_coverage(self):
        with tempfile.TemporaryDirectory(prefix="qaplet-page-plan-") as temp:
            work = Path(temp)
            result = self.invoke("page-gaps", "--report", work / "plan.json", "--github-output", work / "outputs")
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            report = json.loads((work / "plan.json").read_text(encoding="utf-8"))
            self.assertEqual(report["report_kind"], "ape-page-plan")
            self.assertEqual(report["covered_native_pages"], [])
            self.assertEqual(report["missing_pages"], [4096, 65536])
            self.assertEqual(report["m0_core_status"], "not_run")
            outputs = (work / "outputs").read_text(encoding="utf-8")
            self.assertIn('matrix={"include":[{"page_size":4096},{"page_size":65536}]}', outputs)
            self.assertIn("needs_guest=true", outputs)

    def test_page_planning_retains_native_failure_and_handles_16k_drift(self):
        # Report fixtures exercise the scheduling interface; plans never attest
        # that these fixture measurements are actual hardware acceptance.
        for page, missing in ((4096, [65536]), (65536, [4096]), (16384, [4096, 65536])):
            with self.subTest(page=page), tempfile.TemporaryDirectory(prefix="qaplet-page-fixture-") as temp:
                work = Path(temp)
                (work / "native.json").write_text(json.dumps({"schema_version": 1, "report_kind": "ape-startup", "status": "fail",
                    "host": {"os": "Linux", "cpu": "aarch64", "execution_arch": "aarch64", "execution_mode": "native",
                             "page_size": page, "kernel": "fixture-kernel"}, "qemu": {"status": "not_applicable"}}), encoding="utf-8")
                result = self.invoke("page-gaps", "--input-report", work / "native.json", "--report", work / "plan.json")
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                report = json.loads((work / "plan.json").read_text(encoding="utf-8"))
                self.assertEqual(report["missing_pages"], missing)
                self.assertEqual(report["observations"][0]["status"], "fail")
                self.assertEqual(report["m0_core_status"], "not_run")

    @unittest.skipUnless(os.environ.get("QAPLET_STARTUP_NATIVE_REPORT"), "Actual native Linux ARM64 startup report required")
    def test_actual_native_page_measurement_schedules_only_the_missing_page(self):
        native = Path(os.environ["QAPLET_STARTUP_NATIVE_REPORT"])
        original = json.loads(native.read_text(encoding="utf-8"))
        self.assertEqual(original["host"]["os"], "Linux")
        self.assertEqual(original["host"]["execution_arch"], "aarch64")
        measured = original["host"]["page_size"]
        with tempfile.TemporaryDirectory(prefix="qaplet-native-page-") as temp:
            work = Path(temp)
            result = self.invoke("page-gaps", "--input-report", native, "--report", work / "plan.json")
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            report = json.loads((work / "plan.json").read_text(encoding="utf-8"))
            self.assertEqual(report["covered_native_pages"], [measured] if measured in (4096, 65536) else [])
            self.assertEqual(report["missing_pages"], [page for page in (4096, 65536) if page != measured])
            self.assertEqual(report["observations"][0]["status"], original["status"])

    @unittest.skipUnless(os.environ.get("QAPLET_STARTUP_GUEST_BUILD") and os.environ.get("QAPLET_STARTUP_GUEST_REPORT"),
                         "Actual producer and full-system guest reports required")
    def test_actual_guest_import_retains_semantics_host_identity_and_unchanged_bytes(self):
        producer = Path(os.environ["QAPLET_STARTUP_GUEST_BUILD"])
        guest = Path(os.environ["QAPLET_STARTUP_GUEST_REPORT"])
        reports = ROOT / "out/startup-guest-contracts"
        reports.mkdir(parents=True, exist_ok=True)
        work = Path(tempfile.mkdtemp(prefix="actual-import-", dir=reports))
        result = self.invoke("import-guest", "--build-report", producer, "--artifact-dir", producer.parent / "artifacts",
                             "--guest-report", guest, "--report", work / "startup.json")
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        report = json.loads((work / "startup.json").read_text(encoding="utf-8"))
        original = json.loads(guest.read_text(encoding="utf-8"))
        self.assertEqual(report["status"], "pass")
        self.assertEqual(report["host"]["os"], "Linux")
        self.assertEqual(report["host"]["execution_arch"], "aarch64")
        self.assertEqual(report["host"]["execution_mode"], "emulated-system")
        self.assertEqual(report["host"]["page_size"], original["guest"]["page_size"])
        self.assertEqual(report["qemu"], original["qemu"])
        self.assertEqual(report["validation_host"]["execution_arch"], original["host"]["execution_arch"])
        cases = {case["case_id"]: case for case in report["cases"]}
        self.assertEqual(len(cases), 17)
        self.assertEqual(cases["quickjs-eval.pointer-roundtrip"]["actual"]["stdout"], "JSValue pointer round-trip: pass\n")
        self.assertEqual(cases["quickjs-eval.objects-lifecycle"]["actual"]["stdout"], "9990000\n")
        self.assertEqual(cases["quickjs-eval.value"]["actual"]["stdout"], "42\n")
        self.assertTrue(all(case["status"] == "pass" for case in report["cases"]))
        self.assertTrue(all(item["sha256"] == item["sha256_after"] for item in report["artifacts"]))
        self.assertTrue(all(Path(case["commands"][0]["stdout_log"]).is_file() for case in report["cases"]))
        matrix = self.invoke("matrix", "--build-report", producer, "--artifact-dir", producer.parent / "artifacts",
                             "--input-dir", work, "--report", work / "matrix.json")
        self.assertEqual(matrix.returncode, 2, matrix.stdout + matrix.stderr)
        coverage = json.loads((work / "matrix.json").read_text(encoding="utf-8"))
        self.assertEqual(coverage["status"], "blocked")
        self.assertEqual(sum(item["status"] == "pass" for item in coverage["coverage"]), 1)

    @unittest.skipUnless(os.environ.get("QAPLET_STARTUP_GUEST_BUILD") and os.environ.get("QAPLET_STARTUP_GUEST_REPORT"),
                         "Actual producer and full-system guest reports required")
    def test_guest_raw_log_identity_mismatch_cannot_be_imported_as_pass(self):
        producer = Path(os.environ["QAPLET_STARTUP_GUEST_BUILD"])
        incoming = json.loads(Path(os.environ["QAPLET_STARTUP_GUEST_REPORT"]).read_text(encoding="utf-8"))
        incoming["cases"][0]["stdout_log"]["sha256"] = "f" * 64
        with tempfile.TemporaryDirectory(prefix="qaplet-bad-guest-log-") as temp:
            work = Path(temp)
            (work / "guest.json").write_text(json.dumps(incoming), encoding="utf-8")
            result = self.invoke("import-guest", "--build-report", producer, "--artifact-dir", producer.parent / "artifacts",
                                 "--guest-report", work / "guest.json", "--report", work / "startup.json")
            self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
            report = json.loads((work / "startup.json").read_text(encoding="utf-8"))
            self.assertEqual(report["status"], "fail")
            self.assertIn("raw", report["reason"].lower())

    @unittest.skipUnless(os.environ.get("QAPLET_STARTUP_GUEST_BUILD") and os.environ.get("QAPLET_STARTUP_GUEST_REPORT"),
                         "Actual producer and full-system guest reports required")
    def test_guest_report_can_be_imported_after_transport_with_its_raw_logs(self):
        producer = Path(os.environ["QAPLET_STARTUP_GUEST_BUILD"])
        incoming = json.loads(Path(os.environ["QAPLET_STARTUP_GUEST_REPORT"]).read_text(encoding="utf-8"))
        with tempfile.TemporaryDirectory(prefix="qaplet-transported-guest-") as temp:
            work = Path(temp)
            logs = work / "evidence" / incoming["report_id"]
            logs.mkdir(parents=True)
            entries = [incoming["qemu"][key] for key in ("serial_log", "stderr_log")]
            for case in incoming["cases"]:
                entries.extend(case[key] for key in ("stdout_log", "stderr_log", "serial_log"))
            original_serial = incoming["qemu"]["serial_log"]["path"]
            for entry in entries:
                source = Path(entry["path"])
                shutil.copyfile(source, logs / source.name)
                entry["path"] = "/unavailable-original-runner/evidence/" + source.name
            for event in incoming["commands"]:
                if event["stdout_log"] == original_serial:
                    event["stdout_log"] = incoming["qemu"]["serial_log"]["path"]
            (work / "guest.json").write_text(json.dumps(incoming), encoding="utf-8")
            result = self.invoke("import-guest", "--build-report", producer, "--artifact-dir", producer.parent / "artifacts",
                                 "--guest-report", work / "guest.json", "--report", work / "startup.json")
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            report = json.loads((work / "startup.json").read_text(encoding="utf-8"))
            self.assertEqual(report["status"], "pass")
            self.assertTrue(all(Path(case["commands"][0]["stdout_log"]).is_file() for case in report["cases"]))

    @unittest.skipUnless(os.environ.get("QAPLET_STARTUP_GUEST_BUILD") and os.environ.get("QAPLET_STARTUP_GUEST_REPORT"),
                         "Actual producer and full-system guest reports required")
    def test_later_blocked_report_does_not_erase_a_target_failure(self):
        producer = Path(os.environ["QAPLET_STARTUP_GUEST_BUILD"])
        with tempfile.TemporaryDirectory(prefix="qaplet-conflicting-startup-") as temp:
            work = Path(temp)
            result = self.invoke("import-guest", "--build-report", producer, "--artifact-dir", producer.parent / "artifacts",
                                 "--guest-report", os.environ["QAPLET_STARTUP_GUEST_REPORT"], "--report", work / "actual.json")
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            report = json.loads((work / "actual.json").read_text(encoding="utf-8"))
            for status in ("fail", "blocked"):
                report.update(status=status, reason="Conservative report-status fixture")
                (work / (status + ".json")).write_text(json.dumps(report), encoding="utf-8")
            result = self.invoke("matrix", "--build-report", producer, "--artifact-dir", producer.parent / "artifacts",
                                 "--input-report", work / "fail.json", "--input-report", work / "blocked.json", "--report", work / "matrix.json")
            self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
            matrix = json.loads((work / "matrix.json").read_text(encoding="utf-8"))
            self.assertEqual(matrix["status"], "fail")
            self.assertEqual(next(slot for slot in matrix["coverage"] if slot["reports"])["status"], "fail")


if __name__ == "__main__":
    unittest.main()
