"""Public supplied-SDK native evidence boundary; no synthetic passing reports."""
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


class SDKNativeContract(unittest.TestCase):
    def test_fast_matrix_requires_an_actual_native_report(self):
        with tempfile.TemporaryDirectory(prefix="qaplet-sdk-matrix-") as temp:
            report = Path(temp) / "matrix.json"
            result = subprocess.run([sys.executable, str(ROOT / "scripts/sdk.py"), "matrix",
                "--profile", "fast", "--report", str(report)], capture_output=True, text=True, timeout=30)
            self.assertTrue(report.is_file(), result.stdout + result.stderr)
            value = json.loads(report.read_text())
            self.assertNotEqual(result.returncode, 0)
            self.assertNotEqual(value["status"], "pass")
            self.assertFalse(value["release_ready"])

    @unittest.skipUnless(os.environ.get("QAPLET_SDK_BUILD_REPORT") and os.environ.get("QAPLET_SDK_NATIVE_REPORT"),
                         "Explicit actual supplied SDK producer and native report required")
    def test_actual_native_contracts_replay_and_missing_case_is_rejected(self):
        native = Path(os.environ["QAPLET_SDK_NATIVE_REPORT"])
        producer = Path(os.environ["QAPLET_SDK_BUILD_REPORT"])
        original = json.loads(native.read_text())
        with tempfile.TemporaryDirectory(prefix="qaplet-sdk-replay-") as temp:
            work = Path(temp)
            def check(selected):
                report = work / "matrix.json"
                result = subprocess.run([sys.executable, str(ROOT / "scripts/sdk.py"), "matrix", "--profile", "fast",
                    "--build-report", str(producer), "--artifact", str(producer.parent / "artifacts/qaplet-host.com"),
                    *(["--loader-dir", os.environ["QAPLET_SDK_LOADER_DIR"]] if os.environ.get("QAPLET_SDK_LOADER_DIR") else []),
                    "--input-report", str(selected), "--report", str(report)], capture_output=True, text=True, timeout=60)
                return result.returncode, json.loads(report.read_text())
            code, value = check(native)
            self.assertEqual(code, 0, value["reason"])
            self.assertEqual(value["coverage"][0]["lifecycle"]["completed_cycles"], 1000)
            shutil.copytree(native.parent / "evidence" / original["report_id"], work / "evidence" / original["report_id"])
            selected = work / "run.json"
            for name in ("missing_case", "short_lifecycle", "guest_replacement", "required_skip"):
                mutation = json.loads(json.dumps(original))
                if name == "missing_case":
                    mutation["cases"] = [case for case in mutation["cases"] if case["case_id"] != "qaplet-host.stop-ordering"]
                elif name == "short_lifecycle":
                    mutation["lifecycle"]["completed_cycles"] = 99
                elif name == "guest_replacement":
                    mutation["host"]["execution_mode"] = "emulated-system"
                else:
                    mutation["cases"][0]["status"] = "skip"
                selected.write_text(json.dumps(mutation))
                code, value = check(selected)
                self.assertEqual(code, 1, name + ": " + str(value))
                self.assertTrue(any(word in value["reason"].lower() for word in ("catalog", "lifecycle", "native", "contract")), value)


if __name__ == "__main__":
    unittest.main()
