"""Application-private Job public CLI contracts."""
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
CLI = ROOT / "scripts/svc_job.py"


class JobContract(unittest.TestCase):
    def invoke(self, *arguments):
        if os.environ.get("QAPLET_TEST_JOB_PRODUCER"):
            parent = ROOT / "out/svc-job/contracts"
            parent.mkdir(parents=True, exist_ok=True)
            work = Path(tempfile.mkdtemp(prefix="job-", dir=parent))
        else:
            self.temp = tempfile.TemporaryDirectory(prefix="qaplet-job-contract-")
            self.addCleanup(self.temp.cleanup)
            work = Path(self.temp.name)
        result = subprocess.run([sys.executable, CLI, *arguments, "--report", work / "result.json"],
                                capture_output=True, text=True, timeout=30)
        self.assertTrue((work / "result.json").exists(), result.stdout + result.stderr)
        return result, json.loads((work / "result.json").read_text())

    def test_missing_producer_is_blocked_without_execution(self):
        result, report = self.invoke("run", "--input-report", ROOT / "out/absent-job.json",
                                     "--artifact", ROOT / "out/absent-job.com")
        self.assertEqual(result.returncode, 2, result.stdout + result.stderr)
        self.assertEqual(report["status"], "blocked")
        self.assertEqual(report["capability"], "not_run")
        self.assertEqual(report["m0_core_status"], "not_run")
        self.assertEqual(report["commands"], [])

    def test_changed_application_bytes_are_refused_before_execution(self):
        with tempfile.TemporaryDirectory(prefix="qaplet-job-corrupt-") as temp:
            work = Path(temp)
            artifact = work / "job.com"
            artifact.write_bytes(b"a changed application fixture")
            producer = {"schema_version": 1, "report_kind": "svc-job-build", "status": "pass",
                        "artifacts": [{"name": "svc-job", "sha256": "1" * 64, "size_bytes": 29}]}
            path = work / "producer.json"
            path.write_text(json.dumps(producer))
            result, report = self.invoke("run", "--input-report", path, "--artifact", artifact)
            self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
            self.assertIn("SHA-256", report["reason"])
            self.assertEqual(report["commands"], [])

    def test_malformed_producer_still_writes_nonpassing_evidence(self):
        with tempfile.TemporaryDirectory(prefix="qaplet-job-malformed-") as temp:
            work = Path(temp)
            artifact, producer = work / "job.com", work / "producer.json"
            artifact.write_bytes(b"not an executable")
            producer.write_text("[]")
            result, report = self.invoke("run", "--input-report", producer, "--artifact", artifact)
            self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
            self.assertEqual(report["status"], "fail")
            self.assertEqual(report["commands"], [])

    def test_ci_has_only_an_ubuntu_producer_and_same_byte_windows_consumer(self):
        result, report = self.invoke("check-ci")
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertEqual(report["status"], "pass")

    def test_ci_rejects_compilation_in_windows_consumer(self):
        with tempfile.TemporaryDirectory(prefix="qaplet-job-ci-") as temp:
            workflow = json.loads((ROOT / ".github/workflows/svc-job.yml").read_text())
            workflow["jobs"]["windows-job"]["steps"].append({"run": "python scripts/svc_job.py build --report out/forbidden.json"})
            path = Path(temp) / "workflow.json"
            path.write_text(json.dumps(workflow))
            result, report = self.invoke("check-ci", "--workflow", path)
            self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
            self.assertIn("must not compile", report["reason"])

    def test_ci_rejects_core_make_compilation_in_windows_consumer(self):
        with tempfile.TemporaryDirectory(prefix="qaplet-job-ci-make-") as temp:
            workflow = json.loads((ROOT / ".github/workflows/svc-job.yml").read_text())
            workflow["jobs"]["windows-job"]["steps"].append({"run": "make check"})
            path = Path(temp) / "workflow.json"
            path.write_text(json.dumps(workflow))
            result, report = self.invoke("check-ci", "--workflow", path)
            self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
            self.assertIn("must not compile", report["reason"])

    @unittest.skipUnless(os.environ.get("QAPLET_TEST_JOB_PRODUCER"), "explicit Ubuntu producer required")
    def test_supplied_artifact_has_portable_entry_and_bounded_owned_job_cases(self):
        producer = Path(os.environ["QAPLET_TEST_JOB_PRODUCER"])
        options = ["--loader", os.environ["QAPLET_TEST_JOB_LOADER"]] if os.environ.get("QAPLET_TEST_JOB_LOADER") else []
        result, report = self.invoke("run", "--input-report", producer,
                                     "--artifact", producer.parent / "artifacts/svc-job.com", *options)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        cases = {case["case_id"]: case for case in report["cases"]}
        self.assertEqual(cases["job.ordinary"]["status"], "pass")
        if os.name == "nt":
            self.assertEqual(report["capability"], "verified")
            for name in ("job.context", "job.resolve", "job.missing-export", "job.assignment-denied",
                         "job.tree-terminate", "job.nested-close", "job.early-exit", "job.timeout-cleanup",
                         "job.handles", "job.supervisor-cleanup"):
                self.assertEqual(cases[name]["status"], "pass", cases[name])
            self.assertEqual(report["cleanup"]["active_processes"], 0)
        else:
            self.assertEqual(report["capability"], "not_run")
            self.assertEqual(cases["job.nonwindows-rejected"]["status"], "pass")

    @unittest.skipUnless(os.name == "nt" and os.environ.get("QAPLET_TEST_JOB_PRODUCER"), "explicit Windows artifact validation required")
    def test_real_probe_deadline_is_failure_with_zero_owned_survivors(self):
        producer = Path(os.environ["QAPLET_TEST_JOB_PRODUCER"])
        result, report = self.invoke("run", "--input-report", producer,
                                     "--artifact", producer.parent / "artifacts/svc-job.com", "--probe-mode", "hang", "--timeout", "1")
        self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
        self.assertEqual(report["capability"], "failed")
        self.assertTrue(report["commands"][-1]["timed_out"])
        self.assertEqual(report["cleanup"]["active_processes"], 0)
        self.assertTrue(report["cleanup"]["probe_reaped"])


if __name__ == "__main__":
    unittest.main()
