"""Cross-host acceptance through existing CLIs and explicitly selected evidence."""
import copy
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


class PageProducerCI(unittest.TestCase):
    def test_failed_page_build_still_records_its_actual_ci_execution_identity(self):
        with tempfile.TemporaryDirectory() as temporary:
            work = Path(temporary)
            environment = dict(os.environ, GITHUB_ACTIONS="true", GITHUB_SHA="a" * 40,
                               GITHUB_RUN_ID="123", GITHUB_RUN_ATTEMPT="2", GITHUB_WORKFLOW="fixture",
                               RUNNER_NAME="fixture", RUNNER_OS="Linux", RUNNER_ARCH="X64",
                               ImageOS="fixture", ImageVersion="1")
            output = work / "build.json"
            result = subprocess.run([sys.executable, ROOT / "scripts/pages64k.py", "build",
                "--toolchain-cache", work / "missing", "--report", output], env=environment,
                capture_output=True, text=True, timeout=30)
            self.assertNotEqual(result.returncode, 0)
            report = json.loads(output.read_text(encoding="utf-8"))
            self.assertEqual(report.get("ci", {}).get("commit"), "a" * 40)
            self.assertEqual(report["ci"]["run_id"], "123")


@unittest.skipUnless(os.environ.get("QAPLET_M0A_BUILD") and os.environ.get("QAPLET_M0A_STARTUP"),
                     "Explicit current APE producer and actual native startup required")
class CrossHostIdentity(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.work = Path(self.temporary.name)
        self.producer_path = Path(os.environ["QAPLET_M0A_BUILD"])
        self.producer = json.loads(self.producer_path.read_text(encoding="utf-8"))
        self.native = json.loads(Path(os.environ["QAPLET_M0A_STARTUP"]).read_text(encoding="utf-8"))

    def matrix(self, producer=None, incoming=None, profile="full", duplicate=False):
        build = self.producer_path
        if producer is not None:
            build = self.work / "build.json"
            build.write_text(json.dumps(producer), encoding="utf-8")
            incoming = copy.deepcopy(incoming or self.native)
            incoming["producer_report"] = {"path": str(build), "sha256": hashlib.sha256(build.read_bytes()).hexdigest(),
                                           "size_bytes": build.stat().st_size}
        native = self.work / "native.json"
        native.write_text(json.dumps(incoming or self.native), encoding="utf-8")
        output = self.work / "matrix.json"
        arguments = [sys.executable, ROOT / "scripts/startup.py", "matrix",
            "--build-report", build, "--artifact-dir", self.producer_path.parent / "artifacts",
            "--input-report", native, "--profile", profile, "--report", output]
        if duplicate:
            arguments += ["--input-report", native]
        result = subprocess.run(arguments,
            capture_output=True, text=True, timeout=30)
        self.assertTrue(output.is_file(), result.stderr)
        return result.returncode, json.loads(output.read_text(encoding="utf-8"))

    def test_cross_retains_missing_native_hosts_without_claiming_full_core(self):
        code, report = self.matrix(profile="cross")
        self.assertEqual(code, 2, report["reason"])
        self.assertEqual(report["profile"], "cross")
        self.assertEqual(len(report["coverage"]), 5)
        self.assertEqual(sum(slot["status"] == "pass" for slot in report["coverage"]), 1)
        self.assertEqual(report["m0_core_status"], "not_run")

    def test_matrix_refuses_ci_commit_drift_or_missing_counterpart_identity(self):
        # CI fields here are mutations of real reports, never remote-run evidence.
        ci = dict(run_id="1", run_attempt="1", workflow="contract-fixture", commit="a" * 40,
                  runner_name="fixture", runner_os="Linux", runner_arch="X64", image_os="fixture", image_version="1")
        producer = copy.deepcopy(self.producer)
        producer["ci"] = ci
        native = copy.deepcopy(self.native)
        native["ci"] = dict(ci)
        code, report = self.matrix(producer, native)
        self.assertEqual(code, 2, report["reason"])
        invalid_identities = [dict(ci, commit="b" * 40), None]
        invalid_identities += [dict(ci, **{field: None}) for field in ci]
        invalid_identities += [dict(ci, commit=value) for value in ("A" * 40, "a" * 39, 123)]
        for changed in invalid_identities:
            with self.subTest(ci=changed):
                native["ci"] = changed
                code, report = self.matrix(producer, native)
                self.assertEqual(code, 1, report["reason"])
                self.assertIn("CI", report["reason"])

    def test_matrix_refuses_another_producer_even_when_artifact_bytes_match(self):
        incoming = copy.deepcopy(self.native)
        incoming["producer_report"]["sha256"] = "b" * 64
        code, report = self.matrix(incoming=incoming)
        self.assertEqual(code, 1, report["reason"])
        self.assertIn("selected producer", report["reason"])

    def test_duplicate_selected_report_cannot_count_twice(self):
        code, report = self.matrix(duplicate=True)
        self.assertEqual(code, 1, report["reason"])
        self.assertIn("Duplicate", report["reason"])

    def test_new_input_cannot_relabel_a_global_eval_only_producer(self):
        producer = copy.deepcopy(self.producer)
        producer["cases"] = producer["cases"][:7]
        code, report = self.matrix(producer=producer)
        self.assertEqual(code, 1, report["reason"])
        self.assertIn("catalog", report["reason"])

    def test_current_matrix_refuses_old_missing_failed_and_conflicting_records(self):
        mutations = {
            "old-input": lambda report: report["input_lock"].update(input_id="b" * 64),
            "old-catalog": lambda report: report.update(cases=report["cases"][:7]),
            "missing-case": lambda report: report["cases"].pop(),
            "failed-selection": lambda report: report.update(status="fail", reason="Selected failure retained"),
            "skipped-case": lambda report: report["cases"][0].update(status="skip"),
            "wrong-artifact": lambda report: report["artifacts"][0].update(sha256="b" * 64),
            "wrong-page": lambda report: report["host"].update(page_size=16384),
            "qemu-user": lambda report: report["host"].update(execution_mode="qemu-user"),
        }
        for name, change in mutations.items():
            with self.subTest(mutation=name):
                incoming = copy.deepcopy(self.native)
                change(incoming)
                code, report = self.matrix(incoming=incoming)
                self.assertEqual(code, 1, report["reason"])


if __name__ == "__main__":
    unittest.main()
