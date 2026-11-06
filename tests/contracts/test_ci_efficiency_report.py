"""Retained Actions API timing is observation, not a product pass."""
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


class EfficiencyReportContract(unittest.TestCase):
    def test_reuse_refuses_plans_failed_execution_and_changed_commit(self):
        for status, execution in (("planned", "not_run"), ("fail", "executed"), ("pass", "not_run"), ("pass", "executed")):
            with self.subTest(status=status, execution=execution), tempfile.TemporaryDirectory() as temp:
                work = Path(temp)
                original = work / "original.json"
                original.write_text(json.dumps({"report_kind": "qaplet-local-check", "status": status,
                    "execution_status": execution, "scope": "development-only", "commit": "b" * 40}), encoding="utf-8")
                before = original.read_bytes()
                result = subprocess.run([sys.executable, str(ROOT / "scripts/ci_evidence.py"), "reuse",
                    "--input-report", str(original), "--commit", "a" * 40, "--profile", "engine",
                    "--expected-os", "Linux", "--expected-arch", "x86_64", "--report", str(work / "reuse.json")],
                    capture_output=True, text=True, timeout=20)
                self.assertEqual(result.returncode, 1)
                self.assertEqual(original.read_bytes(), before)

    def report(self, runner, steps, *, inherited=False, conclusion="failure", recreated=False):
        with tempfile.TemporaryDirectory() as temp:
            work = Path(temp)
            run = {"id": 17, "head_sha": "a" * 40, "run_attempt": 2,
                   "name": "M0", "status": "completed", "conclusion": conclusion,
                   "created_at": "2026-11-01T00:00:00Z", "run_started_at": "2026-11-01T00:02:00Z",
                   "updated_at": "2026-11-01T00:10:00Z"}
            job = {"id": 18, "run_id": 17, "name": "Build", "runner_id": runner,
                   "created_at": "2026-11-01T00:02:00Z" if recreated else "2026-11-01T00:00:00Z", "started_at": "2026-11-01T00:02:00Z" if inherited and recreated and not runner else "2026-11-01T00:00:05Z" if inherited else "2026-11-01T00:02:05Z",
                   "completed_at": "2026-11-01T00:01:05Z" if inherited else "2026-11-01T00:03:05Z",
                   "conclusion": conclusion, "steps": steps}
            for name, value in (("run", run), ("jobs", {"jobs": [job]})):
                (work / (name + ".json")).write_text(json.dumps(value), encoding="utf-8")
            result = subprocess.run([sys.executable, str(ROOT / "scripts/ci_evidence.py"), "efficiency",
                "--input-response", str(work / "run.json"), "--jobs-response", str(work / "jobs.json"),
                "--tier", "full", "--report", str(work / "report.json")], capture_output=True, text=True, timeout=20)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            return json.loads((work / "report.json").read_text(encoding="utf-8"))

    def test_runnerless_interval_is_not_product_execution_or_queue(self):
        value = self.report(0, [])
        job = value["jobs"][0]
        self.assertIsNone(job["execution_seconds"])
        self.assertIsNone(job["queue_seconds"])
        self.assertEqual(value["runner_seconds"], 0)
        self.assertEqual(value["failure_class"], "Unknown")
        self.assertIsNone(value["cache_hit"])
        self.assertFalse(value["evidence_verified"])

    def test_real_job_and_steps_are_measured_separately_from_api_updated_at(self):
        value = self.report(12, [{"name": "Build SDK", "started_at": "2026-11-01T00:02:05Z",
                                 "completed_at": "2026-11-01T00:02:35Z", "conclusion": "success"}], conclusion="success")
        self.assertEqual(value["jobs"][0]["execution_seconds"], 60)
        self.assertEqual(value["jobs"][0]["queue_seconds"], 125)
        self.assertEqual(value["workflow_span_seconds"], 65)
        self.assertEqual(value["api_state_span_seconds"], 600)
        self.assertEqual(value["jobs"][0]["build_and_verify_seconds"], 30)

    def test_attempt_two_inherited_execution_is_not_new_runner_work(self):
        value = self.report(12, [{"name": "old successful step"}], inherited=True)
        self.assertTrue(value["jobs"][0]["inherited_execution"])
        self.assertEqual(value["runner_seconds"], 0)
        self.assertIsNone(value["workflow_span_seconds"])

    def test_rerun_recreated_job_keeps_original_execution_without_negative_queue(self):
        value = self.report(12, [{"name": "old successful step"}], inherited=True, recreated=True)
        self.assertTrue(value['jobs'][0]['inherited_execution'])
        self.assertEqual(value['jobs'][0]['execution_seconds'], 60)
        self.assertIsNone(value['jobs'][0]['queue_seconds'])
        self.assertEqual(value['runner_seconds'], 0)
        self.assertIsNone(value['queue_seconds'])

    def test_inherited_skip_has_no_new_observation_interval(self):
        value = self.report(0, [], inherited=True, recreated=True)
        self.assertTrue(value['jobs'][0]['inherited_execution'])
        self.assertIsNone(value['jobs'][0]['unexecuted_observation_seconds'])


if __name__ == "__main__":
    unittest.main()
