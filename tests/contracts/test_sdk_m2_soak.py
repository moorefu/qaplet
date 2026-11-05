"""Full evidence accepts actual completion and retains native observations."""
import json
import copy
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "scripts"))
import sdk_m2_soak


class FullObservationContract(unittest.TestCase):
    @unittest.skipUnless(os.environ.get('QAPLET_M2_FULL_RUN'), 'Actual Full runner smoke required')
    def test_actual_persistent_runtime_rejects_rolling_or_nonzero_resource_baselines(self):
        report = json.loads(Path(os.environ['QAPLET_M2_FULL_RUN']).read_text(encoding='utf-8'))
        sdk_m2_soak.inspect_run(report)
        original = report['command']['progress_log']
        rows = [json.loads(line) for line in Path(original).read_bytes().splitlines()]
        mutations = []
        nonzero = copy.deepcopy(rows)
        for row in nonzero:
            row['baseline_operations'] = 1
        mutations.append(nonzero)
        rolling = copy.deepcopy(rows)
        rolling[-1]['os_handles_before'] += 1
        mutations.append(rolling)
        generations = copy.deepcopy(rows)
        generations[-1]['generation_checks'] = 0
        mutations.append(generations)
        outcomes = copy.deepcopy(rows)
        outcomes[-1]['normal'] = outcomes[-1]['completed']
        mutations.append(outcomes)
        with tempfile.TemporaryDirectory() as temporary:
            for index, mutated in enumerate(mutations):
                target = Path(temporary) / (str(index) + '.ndjson')
                target.write_bytes(('\n'.join(json.dumps(row) for row in mutated)+'\n').encode())
                selected = copy.deepcopy(report)
                selected.pop('final')
                selected.pop('os_observations')
                selected['command']['progress_log'] = str(target)
                selected['raw_evidence'] = [sdk_m2_soak.identity(target) if item['path'] == original else item
                                            for item in selected['raw_evidence']]
                with self.subTest(mutation=index), self.assertRaises(ValueError):
                    sdk_m2_soak.inspect_run(selected)

    @unittest.skipUnless(os.environ.get('QAPLET_M2_FULL_RUN'), 'Actual Full runner smoke required')
    def test_actual_command_catalog_rejects_a_relabelled_run_scope(self):
        report = json.loads(Path(os.environ['QAPLET_M2_FULL_RUN']).read_text(encoding='utf-8'))
        report['scope'] = 'full' if report['purpose'] == 'smoke' else 'smoke'
        with self.assertRaises(ValueError):
            sdk_m2_soak.inspect_run(report)

    @unittest.skipUnless(os.environ.get('QAPLET_M2_CYCLE_SOAK_REPORT'), 'Actual shared cycle soak required')
    def test_actual_wait_calls_replay_and_reject_missing_or_changed_raw_measurements(self):
        report = json.loads(Path(os.environ['QAPLET_M2_CYCLE_SOAK_REPORT']).read_text(encoding='utf-8'))
        case = report['cases'][0]
        raw = Path(case['waits']['path']).read_bytes()
        actual = sdk_m2_soak.inspect_waits(raw, case['observations'])
        self.assertEqual(actual['calls'], case['observations']['idle_wait_calls'])
        self.assertGreater(actual['elapsed_ms_total'], 0)
        rows = [json.loads(line) for line in raw.splitlines()]
        for mutated in (rows[:-1], [dict(rows[0], index=2), *rows[1:]],
                        [dict(rows[0], actual_monotonic_elapsed_ms=0), *rows[1:]]):
            with self.assertRaises(ValueError):
                sdk_m2_soak.inspect_waits(('\n'.join(json.dumps(row) for row in mutated)+'\n').encode(),
                                         case['observations'])

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
            self.assertTrue(any(row["resources"]["rss_bytes"] > 0 for row in active))
            self.assertTrue(all(row["resources"]["rss_bytes"] >= 0 for row in active))
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

    def test_read_only_join_cannot_overwrite_selected_raw_evidence(self):
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            original = directory / "raw.ndjson"
            original.write_bytes(b"original raw evidence\n")
            selected = directory / "selected.json"
            selected.write_text(json.dumps({"raw_evidence": [{"path": str(original)}]}), encoding="utf-8")
            result = subprocess.run([sys.executable, str(ROOT / "scripts/sdk_m2_soak.py"), "verify",
                "--run-report", str(selected), "--report", str(original)], capture_output=True)
            self.assertNotEqual(result.returncode, 0)
            self.assertEqual(original.read_bytes(), b"original raw evidence\n")


if __name__ == "__main__":
    unittest.main()
