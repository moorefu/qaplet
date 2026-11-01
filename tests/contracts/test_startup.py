"""Startup CLI contracts at distributed bytes, OS execution and report seams."""
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
CLI = ROOT / "scripts/startup.py"


class StartupContract(unittest.TestCase):
    def invoke(self, *arguments):
        return subprocess.run([sys.executable, str(CLI), *map(str, arguments)],
                              capture_output=True, text=True, timeout=60)

    def test_changed_distributed_bytes_fail_before_any_execution(self):
        with tempfile.TemporaryDirectory(prefix="qaplet-startup-") as temp:
            work = Path(temp)
            (work / "hello.com").write_bytes(b"changed")
            (work / "producer.json").write_text(json.dumps({
                "report_kind": "ape-build", "schema_version": 1, "status": "pass",
                "source": {}, "toolchain": {}, "build": {}, "artifacts": [
                    {"name": "hello", "sha256": "1" * 64, "size_bytes": 7},
                    {"name": "quickjs-eval", "sha256": "2" * 64, "size_bytes": 8}]}))
            result = self.invoke("run", "--build-report", work / "producer.json",
                                 "--artifact-dir", work, "--report", work / "report.json")
            self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
            report = json.loads((work / "report.json").read_text())
            self.assertEqual(report["status"], "fail")
            self.assertIn("SHA-256", report["reason"])
            self.assertEqual(report["commands"], [])
            self.assertEqual(report["m0_core_status"], "not_run")

    def test_helper_build_is_offline_and_missing_zig_is_blocked(self):
        with tempfile.TemporaryDirectory(prefix="qaplet-loaders-") as temp:
            work = Path(temp)
            result = subprocess.run([sys.executable, str(ROOT / "scripts/startup_loaders.py"), "build",
                                     "--zig-cache", str(work / "missing"), "--output-dir", str(work / "loaders"),
                                     "--report", str(work / "helpers.json")], capture_output=True, text=True, timeout=30)
            self.assertEqual(result.returncode, 2, result.stdout + result.stderr)
            report = json.loads((work / "helpers.json").read_text())
            self.assertEqual(report["status"], "blocked")
            self.assertIn("archive", report["reason"])
            self.assertFalse((work / "missing").exists())
            self.assertFalse((work / "loaders").exists())

    @unittest.skipUnless(os.environ.get("QAPLET_STARTUP_BUILD"), "Explicit distributed producer required")
    def test_real_unchanged_products_report_facts_value_and_nonzero_errors(self):
        producer = Path(os.environ["QAPLET_STARTUP_BUILD"])
        (ROOT / "out").mkdir(exist_ok=True)
        with tempfile.TemporaryDirectory(prefix="qaplet-native-startup-", dir=ROOT / "out") as temp:
            work = Path(temp)
            arguments = ["run", "--build-report", producer, "--artifact-dir", producer.parent / "artifacts",
                         "--report", work / "report.json"]
            if os.environ.get("QAPLET_STARTUP_LOADERS"):
                arguments += ["--loader-dir", os.environ["QAPLET_STARTUP_LOADERS"]]
            result = self.invoke(*arguments)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            report = json.loads((work / "report.json").read_text())
            self.assertEqual(report["status"], "pass")
            self.assertEqual(len(report["cases"]), 5)
            cases = {case["case_id"]: case for case in report["cases"]}
            self.assertEqual(cases["quickjs-eval.value"]["actual"]["stdout"], "42\n")
            self.assertNotEqual(cases["quickjs-eval.exception"]["actual"]["exit_code"], 0)
            self.assertIn("Error: m0-probe", cases["quickjs-eval.exception"]["actual"]["stderr"])
            self.assertIn("SyntaxError:", cases["quickjs-eval.syntax-error"]["actual"]["stderr"])
            self.assertEqual(report["host"]["execution_mode"], "native")
            self.assertTrue(all(case["status"] == "pass" for case in report["cases"]))
            self.assertTrue(all(Path(cmd["stdout_log"]).is_file() for cmd in report["commands"]))
            self.assertTrue(all(artifact["sha256"] == artifact["sha256_after"] for artifact in report["artifacts"]))
            matrix = self.invoke("matrix", "--build-report", producer, "--artifact-dir", producer.parent / "artifacts",
                                 "--input-report", work / "report.json", "--report", work / "matrix.json")
            self.assertEqual(matrix.returncode, 2, matrix.stdout + matrix.stderr)
            coverage = json.loads((work / "matrix.json").read_text())
            self.assertEqual(len(coverage["coverage"]), 6)
            self.assertEqual(sum(item["status"] == "pass" for item in coverage["coverage"]), 1)
            self.assertEqual(coverage["status"], "blocked")


if __name__ == "__main__":
    unittest.main()
