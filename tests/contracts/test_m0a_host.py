"""M0-A behavior at the actual supplied fat APE process boundary."""
import copy
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest


@unittest.skipUnless(os.environ.get("QAPLET_M0A_BUILD") or os.environ.get("QAPLET_M0A_NATIVE"),
                     "Explicit actual M0-A producer or native C host required")
class M0AHostContract(unittest.TestCase):
    def invoke(self, mode):
        if os.environ.get("QAPLET_M0A_NATIVE"):
            return subprocess.run([os.environ["QAPLET_M0A_NATIVE"], mode], capture_output=True, text=True, timeout=5)
        producer = json.loads(Path(os.environ["QAPLET_M0A_BUILD"]).read_text())
        artifact = next(item for item in producer["artifacts"] if item["name"] == "quickjs-eval")
        launcher = producer["build"]["launcher"]["path"]
        return subprocess.run([launcher, artifact["path"], mode], capture_output=True, text=True, timeout=5)

    def assert_host_failure(self, mode, error):
        actual = self.invoke(mode)
        self.assertNotEqual(actual.returncode, 0)
        self.assertEqual(actual.stdout, "")
        self.assertIn(error, actual.stderr)

    def test_esm_export_is_observed_from_the_module_namespace(self):
        actual = self.invoke("--esm-export")
        self.assertEqual((actual.returncode, actual.stdout, actual.stderr), (0, "42\n", ""))

    def test_esm_dependency_is_loaded_from_the_explicit_fixture(self):
        actual = self.invoke("--esm-dependency")
        self.assertEqual((actual.returncode, actual.stdout, actual.stderr), (0, "42\n", ""))

    def test_module_load_compile_and_evaluation_failures_are_observable(self):
        for mode, error in (("--esm-missing", "m0 module unavailable: m0:missing"),
                            ("--esm-syntax", "SyntaxError:"),
                            ("--esm-exception", "Error: m0-module-eval")):
            with self.subTest(mode=mode):
                self.assert_host_failure(mode, error)

    def test_pending_promise_jobs_complete_before_the_export_is_read(self):
        actual = self.invoke("--jobs-complete")
        self.assertEqual((actual.returncode, actual.stdout, actual.stderr), (0, "42\n", ""))

    def test_job_exception_and_unhandled_rejection_fail_the_host(self):
        for mode, error in (("--jobs-exception", "InternalError: m0-job-exception"),
                            ("--jobs-rejection", "Error: m0-job-rejection")):
            with self.subTest(mode=mode):
                self.assert_host_failure(mode, error)

    def test_unfinished_and_endlessly_queued_jobs_are_bounded_failures(self):
        for mode, error in (("--jobs-unfinished", "m0 Promise unfinished"),
                            ("--jobs-budget", "m0 job budget exceeded")):
            with self.subTest(mode=mode):
                self.assert_host_failure(mode, error)

    @unittest.skipUnless(os.environ.get("QAPLET_M0A_BUILD"), "Actual APE producer required")
    def test_producer_records_esm_and_job_execution_evidence(self):
        producer = json.loads(Path(os.environ["QAPLET_M0A_BUILD"]).read_text())
        required = {"esm-export", "esm-dependency", "esm-missing", "esm-syntax", "esm-exception",
                    "jobs-complete", "jobs-exception", "jobs-rejection", "jobs-unfinished", "jobs-budget"}
        cases = {item["case_id"]: item for item in producer["cases"]}
        self.assertTrue({"quickjs-eval." + name for name in required}.issubset(cases))
        for name in required:
            case = cases["quickjs-eval." + name]
            self.assertEqual(case["status"], "pass")
            self.assertEqual(len(case["commands"]), 1)
            command = case["commands"][0]
            self.assertFalse(command["timed_out"])
            self.assertTrue(Path(command["stdout_log"]).is_file())
            self.assertTrue(Path(command["stderr_log"]).is_file())

    @unittest.skipUnless(os.environ.get("QAPLET_M0A_BUILD") and os.environ.get("QAPLET_M0A_STARTUP"),
                         "Actual current producer and native startup evidence required")
    def test_consumer_rejects_global_only_wrong_result_and_successful_error_exit(self):
        producer = Path(os.environ["QAPLET_M0A_BUILD"])
        actual = json.loads(Path(os.environ["QAPLET_M0A_STARTUP"]).read_text())
        root = Path(__file__).resolve().parents[2]
        parent = Path(os.environ.get("QAPLET_M0A_REPORT_DIR", root / "out/m0a-contracts"))
        parent.mkdir(parents=True, exist_ok=True)
        work = Path(tempfile.mkdtemp(prefix="consumer-refusals-", dir=parent))
        for name in ("global-only", "wrong-export", "successful-job-error", "error-stdout"):
            incoming = copy.deepcopy(actual)
            cases = {item["case_id"]: item for item in incoming["cases"]}
            if name == "global-only":
                incoming["cases"] = incoming["cases"][:7]
            elif name == "wrong-export":
                cases["quickjs-eval.esm-export"]["actual"]["stdout"] = "41\n"
            elif name == "successful-job-error":
                case = cases["quickjs-eval.jobs-exception"]
                case["actual"]["exit_code"] = case["commands"][0]["exit_code"] = 0
            else:
                cases["quickjs-eval.jobs-exception"]["actual"]["stdout"] = "42\n"
            path = work / (name + ".json")
            path.write_text(json.dumps(incoming))
            result = subprocess.run([sys.executable, str(root / "scripts/startup.py"), "matrix",
                "--build-report", str(producer), "--artifact-dir", str(producer.parent / "artifacts"),
                "--input-report", str(path), "--report", str(work / (name + "-refused.json"))],
                capture_output=True, text=True, timeout=30)
            self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
            self.assertEqual(json.loads((work / (name + "-refused.json")).read_text())["status"], "fail")


if __name__ == "__main__":
    unittest.main()
