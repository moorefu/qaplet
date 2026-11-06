# SPDX-License-Identifier: MIT
"""Public producer/consumer CLI contracts; native behavior uses supplied APEs."""
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

from input_fixtures import current_input_identity

ROOT = Path(__file__).resolve().parents[2]
CLI = ROOT / "scripts/m2_stdio.py"


class M2StdioContracts(unittest.TestCase):
    def test_changed_supply_is_refused_before_execution(self):
        with tempfile.TemporaryDirectory() as temp:
            directory = Path(temp)
            artifact = directory / "probe.com"
            artifact.write_bytes(b"changed supplied bytes")
            producer = {"schema_version": 1, "report_kind": "m2-stdio", "scope": "build", "status": "pass",
                        "input_lock": current_input_identity(), "artifacts": [
                            {"name": "m2-stdio", "sha256": "a" * 64, "size_bytes": artifact.stat().st_size}]}
            supplied = directory / "build.json"
            supplied.write_text(json.dumps(producer), encoding="utf-8")
            report = directory / "result.json"
            result = subprocess.run([sys.executable, CLI, "run", "--artifact", artifact,
                                     "--build-report", supplied, "--report", report],
                                    capture_output=True, text=True, timeout=20)
            self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
            observed = json.loads(report.read_text(encoding="utf-8"))
            self.assertIn("SHA-256/length", observed["reason"])
            self.assertEqual(observed["commands"], [])

    @unittest.skipUnless(os.environ.get("QAPLET_TEST_M2_STDIO_PRODUCER"), "requires supplied Ubuntu APE")
    def test_real_pipe_cancellation_and_borrowed_stdin_matrix(self):
        producer = Path(os.environ["QAPLET_TEST_M2_STDIO_PRODUCER"])
        with tempfile.TemporaryDirectory() as temp:
            report = Path(temp) / "result.json"
            result = subprocess.run([sys.executable, CLI, "run", "--artifact", producer.parent / "artifacts/m2-stdio.com",
                                     "--build-report", producer, *(["--loader-dir", os.environ["QAPLET_TEST_M2_STDIO_LOADERS"]]
                                      if os.environ.get("QAPLET_TEST_M2_STDIO_LOADERS") else []), "--report", report],
                                    capture_output=True, text=True, timeout=90)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            observed = json.loads(report.read_text(encoding="utf-8"))
            cases = {c["case_id"]: c for c in observed["cases"]}
            self.assertEqual(cases["stdin.pipe"]["actual"]["hex"], "00017f80ff0a")
            self.assertEqual(cases["stdout.pipe"]["actual"]["hex"], "00017f80ff0a")
            self.assertEqual(cases["stderr.pipe"]["actual"]["hex"], "00017f80ff0a")
            self.assertEqual(cases["pipe.cancel-wait"]["status"], "pass")
            self.assertEqual(cases["pipe.late-completion"]["status"], "pass")
            self.assertEqual(set(cases), set(observed["required_cases"]))

    @unittest.skipUnless(os.environ.get("QAPLET_TEST_M2_STDIO_PRODUCER"), "requires supplied Ubuntu APE")
    def test_supervisor_timeout_does_not_prove_native_cancellation_or_cleanup(self):
        producer = Path(os.environ["QAPLET_TEST_M2_STDIO_PRODUCER"])
        with tempfile.TemporaryDirectory() as temp:
            report = Path(temp) / "result.json"
            result = subprocess.run([sys.executable, CLI, "run", "--artifact", producer.parent / "artifacts/m2-stdio.com",
                                     "--build-report", producer, *(["--loader-dir", os.environ["QAPLET_TEST_M2_STDIO_LOADERS"]]
                                      if os.environ.get("QAPLET_TEST_M2_STDIO_LOADERS") else []),
                                     "--probe-mode", "hang", "--timeout", "1", "--report", report],
                                    capture_output=True, text=True, timeout=20)
            self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
            observed = json.loads(report.read_text(encoding="utf-8"))
            self.assertEqual(observed["status"], "fail")
            self.assertEqual(observed["m2_product_status"], "not_run")
            self.assertTrue(observed["commands"][-1]["timed_out"])
            self.assertFalse(any(c["status"] == "pass" for c in observed["cases"]))

    def test_missing_supply_is_blocked_without_running_or_building(self):
        with tempfile.TemporaryDirectory() as temp:
            report = Path(temp) / "result.json"
            result = subprocess.run([sys.executable, CLI, "run", "--artifact", Path(temp) / "missing.com",
                                     "--build-report", Path(temp) / "missing.json", "--report", report],
                                    capture_output=True, text=True, timeout=20)
            self.assertEqual(result.returncode, 2, result.stdout + result.stderr)
            observed = json.loads(report.read_text(encoding="utf-8"))
            self.assertEqual(observed["status"], "blocked")
            self.assertEqual(observed["commands"], [])
            self.assertEqual(observed["m2_product_status"], "not_run")


if __name__ == "__main__":
    unittest.main()
