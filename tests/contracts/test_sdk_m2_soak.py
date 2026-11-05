"""Full evidence accepts actual completion and retains native observations."""
import json
import os
from pathlib import Path
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "scripts"))
import sdk_m2_soak


class FullObservationContract(unittest.TestCase):
    def test_real_process_keeps_raw_progress_and_os_measurements(self):
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            code = ("import json,time,os,threading; print(json.dumps({'type':'ready','process_id':os.getpid(),"
                    "'owner_thread_id':threading.get_native_id()}),flush=True); print(json.dumps({'stage':'progress','phase':'idle',"
                    "'completed':0}),flush=True); time.sleep(" + ('2' if os.name == 'nt' else '.35') + "); "
                    "print(json.dumps({'stage':'final','completed':1}),flush=True)")
            result = sdk_m2_soak.observe_command(
                [sys.executable, "-c", code], directory, directory / "run", 5, .05)
            self.assertEqual(result["exit_code"], 0)
            self.assertFalse(result["timed_out"])
            self.assertGreaterEqual(result["elapsed_seconds"], .35)
            raw = Path(result["stdout_log"]).read_bytes()
            self.assertEqual(json.loads(raw.splitlines()[-1]), {"stage": "final", "completed": 1})
            self.assertEqual(Path(result["stderr_log"]).read_bytes(), b"")
            samples = [json.loads(line) for line in Path(result["samples_log"]).read_bytes().splitlines()]
            active = [row for row in samples if row.get("resources")]
            self.assertGreaterEqual(len(active), 2)
            self.assertTrue(any(row["progress"].get("phase") == "idle" for row in active))
            self.assertTrue(all(row["resources"]["rss_bytes"] > 0 for row in active))
            self.assertTrue(all(row["resources"]["threads"] >= 1 for row in active))
            self.assertTrue(all(row["resources"]["cpu_seconds"] >= 0 for row in active))
            self.assertEqual([row["elapsed_seconds"] for row in samples],
                             sorted(row["elapsed_seconds"] for row in samples))
            observed = sdk_m2_soak.sample_summary(result)
            self.assertGreater(observed["owner_wait_samples"], 0)

    def test_supervisor_timeout_remains_incomplete(self):
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            result = sdk_m2_soak.observe_command(
                [sys.executable, "-c", "import time; time.sleep(10)"],
                directory, directory / "timeout", .15, .05)
            self.assertTrue(result["timed_out"])
            self.assertTrue(result["supervisor_terminated"])
            self.assertNotEqual(result["exit_code"], 0)

    def test_a_claimed_hour_cannot_replace_actual_native_elapsed(self):
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            result = sdk_m2_soak.observe_command(
                [sys.executable, "-c", "import time; time.sleep(.2)"],
                directory, directory / "short", 5, .05)
            with self.assertRaisesRegex(ValueError, "actual process elapsed"):
                sdk_m2_soak.validate_duration(result, {"elapsed_ms": 3600000}, 3600)

    def test_full_aggregate_requires_four_explicit_original_runs(self):
        with self.assertRaisesRegex(ValueError, "four explicit"):
            sdk_m2_soak.aggregate_native_reports([])


if __name__ == "__main__":
    unittest.main()
