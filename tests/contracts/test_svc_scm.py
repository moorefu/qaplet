"""Public SCM probe command contracts; no synthetic SCM success."""
import json
import hashlib
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

from input_fixtures import current_input_identity

ROOT = Path(__file__).resolve().parents[2]
CLI = ROOT / "scripts/svc_scm.py"


class SCMContract(unittest.TestCase):
    def test_ci_contract_keeps_build_on_ubuntu_and_always_cleans_windows(self):
        with tempfile.TemporaryDirectory() as temp:
            work = Path(temp)
            executed = subprocess.run([sys.executable, str(CLI), "check-ci", "--report", str(work / "report.json")],
                                      capture_output=True, text=True)
            self.assertEqual(executed.returncode, 0, executed.stdout + executed.stderr)
            report = json.loads((work / "report.json").read_text(encoding="utf-8"))
            self.assertEqual(report["status"], "pass")
            self.assertEqual(report["capability"], "not_run")

    def test_ci_rejects_core_make_compilation_on_windows_consumer(self):
        with tempfile.TemporaryDirectory() as temp:
            work = Path(temp)
            workflow = json.loads((ROOT / ".github/workflows/svc-scm.yml").read_text(encoding="utf-8"))
            workflow["jobs"]["windows-scm"]["steps"].insert(3, {"run": "make build-ape OUT=out/mutated"})
            candidate = work / "workflow.json"
            candidate.write_text(json.dumps(workflow), encoding="utf-8")
            executed = subprocess.run([sys.executable, str(CLI), "check-ci", "--workflow", str(candidate),
                "--report", str(work / "report.json")], capture_output=True, text=True)
            self.assertEqual(executed.returncode, 1, executed.stdout + executed.stderr)
            report = json.loads((work / "report.json").read_text(encoding="utf-8"))
            self.assertIn("must not compile", report["reason"])

    def test_missing_locked_inputs_block_build_without_creating_cache(self):
        with tempfile.TemporaryDirectory() as temp:
            work = Path(temp)
            executed = subprocess.run([sys.executable, str(CLI), "build", "--toolchain-cache", str(work / "absent"),
                "--output-dir", str(work / "artifacts"), "--report", str(work / "report.json")], capture_output=True, text=True)
            self.assertEqual(executed.returncode, 2, executed.stdout + executed.stderr)
            report = json.loads((work / "report.json").read_text(encoding="utf-8"))
            self.assertEqual(report["status"], "blocked")
            self.assertEqual(report["capability"], "not_run")

    @unittest.skipIf(sys.platform == "win32", "Windows uses native PE CreateProcess")
    def test_consumer_without_distributed_loader_cannot_claim_execution(self):
        with tempfile.TemporaryDirectory() as temp:
            work = Path(temp)
            artifact = work / "svc-scm.com"
            artifact.write_bytes(b"unchanged")
            producer = work / "producer.json"
            producer.write_text(json.dumps({"schema_version": 1, "report_kind": "svc-scm-build", "status": "pass", "input_lock": current_input_identity(),
                "build": {}, "toolchain": {}, "loaders": [],
                "artifacts": [{"name": "svc-scm", "sha256": hashlib.sha256(b"unchanged").hexdigest(), "size_bytes": 9}]}), encoding="utf-8")
            executed = subprocess.run([sys.executable, str(CLI), "run", "--artifact", str(artifact),
                "--input-report", str(producer), "--report", str(work / "report.json")], capture_output=True, text=True)
            self.assertEqual(executed.returncode, 2, executed.stdout + executed.stderr)
            report = json.loads((work / "report.json").read_text(encoding="utf-8"))
            self.assertEqual(report["capability"], "not_run")
            self.assertIn("loader", report["reason"])
            self.assertEqual(report["commands"], [])
            self.assertEqual(report["artifacts"], [])
            self.assertFalse((work / "absent").exists())

    def test_changed_distributed_artifact_is_rejected_before_execution(self):
        with tempfile.TemporaryDirectory() as temp:
            work = Path(temp)
            artifact = work / "svc-scm.com"
            artifact.write_bytes(b"original")
            producer = work / "producer.json"
            producer.write_text(json.dumps({"schema_version": 1, "report_kind": "svc-scm-build", "status": "pass", "input_lock": current_input_identity(),
                "artifacts": [{"name": "svc-scm", "sha256": hashlib.sha256(b"original").hexdigest(), "size_bytes": 8}]}), encoding="utf-8")
            artifact.write_bytes(b"tampered")
            executed = subprocess.run([sys.executable, str(CLI), "run", "--artifact", str(artifact),
                "--input-report", str(producer), "--report", str(work / "report.json")], capture_output=True, text=True)
            self.assertEqual(executed.returncode, 1, executed.stdout + executed.stderr)
            report = json.loads((work / "report.json").read_text(encoding="utf-8"))
            self.assertIn("SHA-256", report["reason"])
            self.assertEqual(report["commands"], [])
            self.assertEqual(report["cases"], [])
            self.assertEqual(report["capability"], "not_run")


if __name__ == "__main__":
    unittest.main()
