"""Public supplied-SDK native evidence boundary; no synthetic passing reports."""
import json
import hashlib
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


class SDKNativeContract(unittest.TestCase):
    @unittest.skipUnless(os.environ.get("QAPLET_SDK_NATIVE_REPORT"),
                         "Actual supplied SDK native report required")
    def test_mixed_smokes_preserve_separate_working_directories_and_fixed_name_evidence(self):
        report = json.loads(Path(os.environ["QAPLET_SDK_NATIVE_REPORT"]).read_text(encoding="utf-8"))
        cases = {row["case_id"]: row for row in report["cases"]}
        roots = []
        for suffix in ("stop", "races", "nonreturning"):
            case_id = "qaplet-host.m2-mixed-" + suffix
            case = cases[case_id]
            command = case["commands"][0]
            root = Path(command["cwd"])
            roots.append(root)
            self.assertEqual(root, Path(report["evidence_directory"]) / case_id)
            self.assertTrue(Path(command["argv"][0]).is_absolute())
            artifact_index = 0 if report["host"]["os"] == "Windows" else 1
            self.assertTrue(Path(command["argv"][artifact_index]).is_absolute())
            self.assertEqual(Path(case["marker"]["path"]), root / "mixed-result.txt")
            self.assertEqual(Path(case["wait_trace"]["path"]), root / "mixed-waits.ndjson")
            self.assertEqual(Path(case["marker"]["path"]).read_bytes(), case["expected"]["stdout"].encode())
            self.assertEqual([row["requested_active_wait_ms"] for row in case["wait_observations"]], [0, 5, 5])
            self.assertEqual(case["status"], "pass")
            self.assertIs(case["native_cleanup_complete"], suffix != "nonreturning")
        self.assertEqual(len(set(roots)), 3)

    @unittest.skipUnless(os.name == "posix" and os.environ.get("QAPLET_SDK_BUILD_REPORT") and
                         os.environ.get("QAPLET_SDK_NATIVE_REPORT") and os.environ.get("QAPLET_SDK_LOADER_DIR"),
                         "Actual SDK/native closure and POSIX loader modes required")
    def test_read_only_matrix_preserves_loader_mode_and_bytes_on_success_and_failure(self):
        producer = Path(os.environ["QAPLET_SDK_BUILD_REPORT"])
        native = Path(os.environ["QAPLET_SDK_NATIVE_REPORT"])
        with tempfile.TemporaryDirectory(prefix="qaplet-sdk-readonly-") as temp:
            work = Path(temp)
            loaders = work / "loaders"
            shutil.copytree(os.environ["QAPLET_SDK_LOADER_DIR"], loaders)
            for path in loaders.rglob("*"):
                if path.is_file(): path.chmod(0o640)
            before = {str(path): (path.stat().st_mode, hashlib.sha256(path.read_bytes()).hexdigest())
                      for path in loaders.rglob("*") if path.is_file()}
            original = json.loads(native.read_text(encoding="utf-8"))
            shutil.copytree(native.parent / "evidence", work / "evidence")
            for fail in (False, True):
                incoming = json.loads(json.dumps(original))
                if fail: incoming["cases"][0]["status"] = "skip"
                selected = work / "run.json"
                selected.write_text(json.dumps(incoming), encoding="utf-8")
                report = work / ("matrix-fail.json" if fail else "matrix-pass.json")
                result = subprocess.run([sys.executable, str(ROOT / "scripts/sdk.py"), "matrix", "--profile", "fast",
                    "--build-report", str(producer), "--artifact", str(producer.parent / "artifacts/qaplet-host.com"),
                    "--loader-dir", str(loaders), "--input-report", str(selected), "--report", str(report)],
                    capture_output=True, text=True, timeout=60)
                self.assertEqual(result.returncode, 1 if fail else 0, report.read_text(encoding="utf-8")[-1500:])
                self.assertEqual(before, {str(path): (path.stat().st_mode, hashlib.sha256(path.read_bytes()).hexdigest())
                    for path in loaders.rglob("*") if path.is_file()}, "Evidence consumption changed supplied loader")

    def test_fast_matrix_requires_an_actual_native_report(self):
        with tempfile.TemporaryDirectory(prefix="qaplet-sdk-matrix-") as temp:
            report = Path(temp) / "matrix.json"
            result = subprocess.run([sys.executable, str(ROOT / "scripts/sdk.py"), "matrix",
                "--profile", "fast", "--report", str(report)], capture_output=True, text=True, timeout=30)
            self.assertTrue(report.is_file(), result.stdout + result.stderr)
            value = json.loads(report.read_text(encoding="utf-8"))
            self.assertNotEqual(result.returncode, 0)
            self.assertNotEqual(value["status"], "pass")
            self.assertFalse(value["release_ready"])

    @unittest.skipUnless(os.environ.get("QAPLET_SDK_BUILD_REPORT") and os.environ.get("QAPLET_SDK_NATIVE_REPORT"),
                         "Explicit actual supplied SDK producer and native report required")
    def test_actual_native_contracts_replay_and_missing_case_is_rejected(self):
        native = Path(os.environ["QAPLET_SDK_NATIVE_REPORT"])
        producer = Path(os.environ["QAPLET_SDK_BUILD_REPORT"])
        original = json.loads(native.read_text(encoding="utf-8"))
        with tempfile.TemporaryDirectory(prefix="qaplet-sdk-replay-") as temp:
            work = Path(temp)
            def check(selected, name):
                report = work / (name + ".json")
                result = subprocess.run([sys.executable, str(ROOT / "scripts/sdk.py"), "matrix", "--profile", "fast",
                    "--build-report", str(producer), "--artifact", str(producer.parent / "artifacts/qaplet-host.com"),
                    *(["--loader-dir", os.environ["QAPLET_SDK_LOADER_DIR"]] if os.environ.get("QAPLET_SDK_LOADER_DIR") else []),
                    "--input-report", str(selected), "--report", str(report)], capture_output=True, text=True, timeout=60)
                return result.returncode, json.loads(report.read_text(encoding="utf-8"))
            code, value = check(native, "actual")
            self.assertEqual(code, 0, value["reason"])
            self.assertEqual(value["coverage"][0]["lifecycle"]["completed_cycles"], 1000)
            shutil.copytree(native.parent / "evidence" / original["report_id"], work / "evidence" / original["report_id"])
            selected = work / "run.json"
            for name in ("missing_case", "short_lifecycle", "guest_replacement", "required_skip", "mixed_cwd", "mixed_marker", "mixed_waits", "consumer_identity"):
                mutation = json.loads(json.dumps(original))
                if name == "missing_case":
                    mutation["cases"] = [case for case in mutation["cases"] if case["case_id"] != "qaplet-host.stop-ordering"]
                elif name == "short_lifecycle":
                    mutation["lifecycle"]["completed_cycles"] = 99
                elif name == "guest_replacement":
                    mutation["host"]["execution_mode"] = "emulated-system"
                elif name == "required_skip":
                    mutation["cases"][0]["status"] = "skip"
                elif name == "consumer_identity":
                    mutation.pop("consumer_identity")
                else:
                    mixed = next(row for row in mutation["cases"] if row["case_id"] == "qaplet-host.m2-mixed-stop")
                    if name == "mixed_cwd": mixed["commands"][0]["cwd"] = original["evidence_directory"]
                    elif name == "mixed_marker": mixed["marker"]["path"] = original["evidence_directory"] + "/mixed-result.txt"
                    else: mixed["wait_observations"] = []
                selected.write_text(json.dumps(mutation), encoding="utf-8")
                code, value = check(selected, name)
                self.assertEqual(code, 1, name + ": " + str(value))
                self.assertTrue(any(word in value["reason"].lower() for word in ("catalog", "lifecycle", "native", "contract", "mixed", "invocation")), value)


if __name__ == "__main__":
    unittest.main()
