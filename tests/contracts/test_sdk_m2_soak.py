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
    @unittest.skipUnless(os.environ.get('QAPLET_M2_THREAD_RACE_REPORT'), 'Actual Linux native-thread race report required')
    def test_actual_nonowner_thread_races_keep_core_and_owner_observations(self):
        report = json.loads(Path(os.environ['QAPLET_M2_THREAD_RACE_REPORT']).read_bytes())
        self.assertEqual(len([row for row in report['errors'] if row['elapsed'] < 9]), 0)
        self.assertGreater(report['successful_resource_queries'], 0)
        self.assertEqual(report['owner_resource_queries'], report['successful_resource_queries'])
        self.assertGreater(report['nonowner_queries_unavailable'], 0)
        missing = report['zombie_missing_rss']
        self.assertEqual(missing['field'], 'VmRSS')
        self.assertIsNone(missing['errno'])
        self.assertNotIn('\nVmRSS:', missing['raw_proc']['status'])
        self.assertEqual(report['child_exit_code'], 0)
        self.assertTrue(report['zombie_reaped'])

    @unittest.skipUnless(os.environ.get('QAPLET_M2_INCOMPLETE_SAMPLES_RUN'), 'Actual midlife missing OS rows required')
    def test_actual_midlife_missing_resources_cannot_be_silently_accepted(self):
        path = Path(os.environ['QAPLET_M2_INCOMPLETE_SAMPLES_RUN'])
        report = json.loads(path.read_bytes())
        mappings = ([{'source': '/mnt/d/Workspace/dairn/qaplet', 'root': 'D:/Workspace/dairn/qaplet'}]
                    if os.name == 'nt' else [])
        resolve = sdk_m2_soak.EvidencePaths({'path_mappings': mappings}, path.parent).resolve
        command = copy.deepcopy(report['command'])
        command['samples_log'] = str(resolve(command['samples_log']))
        command['progress_log'] = str(resolve(command['progress_log']))
        with self.assertRaisesRegex(ValueError, 'OS resource observation unavailable before actual final'):
            sdk_m2_soak.sample_summary(command)

    @unittest.skipUnless(os.environ.get('QAPLET_M2_C9_CYCLE_REPORT'), 'Actual current C9 cycle soak required')
    def test_actual_c9_requires_identity_quiescent_counts_and_ref_wait_metadata(self):
        report = json.loads(Path(os.environ['QAPLET_M2_C9_CYCLE_REPORT']).read_text(encoding='utf-8'))
        case = report['cases'][0]
        rows = [json.loads(line) for line in Path(case['progress']['path']).read_bytes().splitlines()]
        sdk_m2_soak.inspect_protocol(rows, case['observations'], required=True)
        for key in ('fixture_protocol_version', 'observer_identity_verified', 'baseline_keepalive_tokens',
                    'quiescent_helpers', 'quiescent_workers', 'quiescent_keepalive_tokens'):
            changed = copy.deepcopy(rows)
            del changed[-1][key]
            with self.subTest(missing=key), self.assertRaises(ValueError):
                sdk_m2_soak.inspect_protocol(changed, changed[-1], required=True)
        raw_waits = Path(case['waits']['path']).read_bytes()
        sdk_m2_soak.inspect_waits(raw_waits, case['observations'], required_protocol=True)
        waits = [json.loads(line) for line in raw_waits.splitlines()]
        for key in ('timer_current_before', 'timer_referenced_before', 'drive_reason'):
            for missing in (True, False):
                changed = copy.deepcopy(waits)
                if missing:
                    del changed[0][key]
                else:
                    changed[0][key] = 0
                with self.subTest(wait_field=key, missing=missing), self.assertRaises(ValueError):
                    sdk_m2_soak.inspect_waits(('\n'.join(json.dumps(row) for row in changed)+'\n').encode(),
                                             case['observations'], required_protocol=True)

    @unittest.skipUnless(os.environ.get('QAPLET_M2_CYCLE_SOAK_REPORT'), 'Actual historical C8 cycle soak required')
    def test_actual_legacy_protocol_cannot_be_used_as_c9_full(self):
        report = json.loads(Path(os.environ['QAPLET_M2_CYCLE_SOAK_REPORT']).read_text(encoding='utf-8'))
        case = report['cases'][0]
        rows = [json.loads(line) for line in Path(case['progress']['path']).read_bytes().splitlines()]
        with self.assertRaises(ValueError):
            sdk_m2_soak.inspect_protocol(rows, case['observations'], required=True)

    @unittest.skipUnless(os.environ.get('QAPLET_M2_FULL_SELECTION'), 'Four actual Full originals required')
    def test_actual_full_join_rejects_missing_duplicate_failed_and_contradictory_originals(self):
        selection_path = Path(os.environ['QAPLET_M2_FULL_SELECTION'])
        selection = json.loads(selection_path.read_text(encoding='utf-8'))
        resolve = sdk_m2_soak.EvidencePaths(selection, selection_path.parent).resolve
        paths = [resolve(path) for path in selection['reports']]
        self.assertEqual(sdk_m2_soak.aggregate_native_reports(paths, resolve)['status'], 'pass')
        for missing in (paths[:-1], [paths[0], paths[0], *paths[2:]]):
            with self.assertRaises(ValueError):
                sdk_m2_soak.aggregate_native_reports(missing, resolve)
        original = json.loads(paths[0].read_text(encoding='utf-8'))
        with tempfile.TemporaryDirectory() as temporary:
            for field, value in [('status', 'fail'), ('status', 'skipped'), ('status', 'not_run'),
                                 ('scope', 'smoke'), ('sdk.sha256', 'f'*64),
                                 ('command.timed_out', True), ('host.kernel', None)]:
                mutated = copy.deepcopy(original)
                parent = mutated
                keys = field.split('.')
                for key in keys[:-1]:
                    parent = parent[key]
                parent[keys[-1]] = value
                target = Path(temporary) / 'changed-run.json'
                target.write_text(json.dumps(mutated), encoding='utf-8')
                with self.subTest(field=field, value=value), self.assertRaises(ValueError):
                    sdk_m2_soak.aggregate_native_reports([target, *paths[1:]], resolve)

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
