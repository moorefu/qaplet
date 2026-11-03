"""M2 native backend probe CLI contracts at the agreed producer/consumer seam."""
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
CLI = ROOT / "scripts/m2_waker.py"


class M2WakerContract(unittest.TestCase):
    def test_report_cannot_replace_supplied_artifact_even_when_validation_is_blocked(self):
        with tempfile.TemporaryDirectory(prefix="qaplet-m2-waker-collision-") as temporary:
            work = Path(temporary)
            artifact = work / "supplied.com"
            artifact.write_bytes(b"immutable supplied fixture")
            result = subprocess.run([sys.executable, CLI, "run", "--artifact", artifact,
                                     "--build-report", work / "missing.json", "--report", artifact],
                                    capture_output=True, text=True, timeout=20)
            self.assertNotEqual(result.returncode, 0)
            self.assertEqual(artifact.read_bytes(), b"immutable supplied fixture")

    @unittest.skipUnless(os.environ.get("QAPLET_TEST_M2_WAKER_PRODUCER"), "Explicit Ubuntu probe producer required")
    def test_same_bytes_prove_blocking_wake_socket_states_and_cleanup(self):
        producer = Path(os.environ["QAPLET_TEST_M2_WAKER_PRODUCER"])
        with tempfile.TemporaryDirectory(prefix="qaplet-m2-waker-real-") as temporary:
            work = Path(temporary)
            options = ["--loader", os.environ["QAPLET_TEST_M2_WAKER_LOADER"]] if os.environ.get("QAPLET_TEST_M2_WAKER_LOADER") else []
            result = subprocess.run([sys.executable, CLI, "run", "--build-report", producer,
                                     "--artifact", producer.parent / "artifacts/m2-waker.com", *options,
                                     "--report", work / "report.json"], capture_output=True, text=True, timeout=60)
            report = json.loads((work / "report.json").read_text(encoding="utf-8"))
            self.assertEqual(result.returncode, 0, report["reason"])
            cases = {case["case_id"]: case for case in report["cases"]}
            self.assertTrue(all(case["status"] == "pass" for case in cases.values()))
            self.assertEqual(cases["waker.blocked"]["actual"]["timeout_ms"], -1)
            self.assertEqual(cases["waker.blocked"]["actual"]["poll_calls"], 1)
            self.assertEqual(cases["waker.control-failure"]["actual"]["accepted"], 1)
            self.assertEqual(cases["socket.reuse"]["actual"]["same_descriptor"], True)
            self.assertFalse(report["product_support_verified"])

    def test_missing_supplied_bytes_are_blocked_without_compilation(self):
        with tempfile.TemporaryDirectory(prefix="qaplet-m2-waker-") as temporary:
            work = Path(temporary)
            result = subprocess.run([sys.executable, CLI, "run", "--build-report", work / "missing.json",
                                     "--artifact", work / "missing.com", "--report", work / "report.json"],
                                    capture_output=True, text=True, timeout=20)
            self.assertEqual(result.returncode, 2, result.stdout + result.stderr)
            report = json.loads((work / "report.json").read_text(encoding="utf-8"))
            self.assertEqual(report["status"], "blocked")
            self.assertFalse(report["product_support_verified"])
            self.assertEqual(report["commands"], [])


if __name__ == "__main__":
    unittest.main()
