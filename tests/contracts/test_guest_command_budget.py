"""Explicit bounded budgets for the actual long lifecycle command only."""
import argparse
import hashlib
import json
from pathlib import Path
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'scripts'))
import arm64_guest
import sdk_guest


class GuestCommandBudget(unittest.TestCase):
    def test_actual_init_uses_long_budget_without_changing_ordinary_commands(self):
        commands = [{'case_id': 'long', 'argv': ['/payload/probe'], 'timeout_seconds': 300},
                    {'case_id': 'ordinary', 'argv': ['/payload/probe']}]
        script = arm64_guest.init_script({'commands': commands, 'files': [], 'page_size': 65536}, 'token', 60).decode()
        self.assertIn('timeout -s KILL 300 /payload/probe', script)
        self.assertIn('timeout -s KILL 60 /payload/probe', script)

    def test_authoritative_catalog_only_extends_the_1100_cycle_command(self):
        commands = sdk_guest.catalog(65536)
        explicit = {c['case_id']: c['timeout_seconds'] for c in commands if 'timeout_seconds' in c}
        self.assertEqual(explicit, {'qaplet-host.m2-process-js-cycles': 300})
        command = next(c for c in commands if c['case_id'] in explicit)
        self.assertIn('1000 actual create/spawn/reap/close/quiesce/destroy', command['expected_stdout'])
        self.assertIn('100 mixed binary IO/cancel/strict-error cycles', command['expected_stdout'])

    def test_invalid_explicit_budget_is_refused_before_inputs_or_execution(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            payload = root / 'probe'
            payload.write_bytes(b'payload')
            for invalid in (0, 301, True, '300', 1.5):
                value = {'schema_version': 1, 'page_size': 65536,
                         'files': [{'path': str(payload), 'guest_path': '/payload/probe',
                                    'sha256': hashlib.sha256(payload.read_bytes()).hexdigest()}],
                         'commands': [{'case_id': 'probe', 'argv': ['/payload/probe'],
                                       'expected_exit_code': 0, 'timeout_seconds': invalid}]}
                manifest = root / 'manifest.json'
                manifest.write_text(json.dumps(value), encoding='utf-8')
                with self.subTest(invalid=invalid), self.assertRaisesRegex(Exception, 'timeout'):
                    arm64_guest.manifest(argparse.Namespace(manifest=manifest, page_size=65536))

    def test_consumer_rejects_inflated_budget_and_real_deadline_overrun(self):
        expected = next(c for c in sdk_guest.catalog(65536) if c['case_id'] == 'qaplet-host.m2-process-js-cycles')
        case = {'case_id': expected['case_id'], 'started_at': '2026-11-06T00:00:00Z',
                'finished_at': '2026-11-06T00:02:51Z', 'timeout_seconds': 300}
        sdk_guest.verify_deadline(case, expected)
        for invalid in (301, 299, True):
            with self.subTest(invalid=invalid), self.assertRaisesRegex(Exception, 'timeout'):
                sdk_guest.verify_deadline(dict(case, timeout_seconds=invalid), expected)
        with self.assertRaisesRegex(Exception, 'exceeded deadline'):
            sdk_guest.verify_deadline(dict(case, finished_at='2026-11-06T00:05:01Z'), expected)
        with self.assertRaisesRegex(Exception, 'timeout'):
            sdk_guest.verify_deadline(dict(case, timeout_seconds=300), {'case_id': 'ordinary'})
