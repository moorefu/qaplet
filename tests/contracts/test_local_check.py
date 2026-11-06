# SPDX-License-Identifier: MIT
"""Public local-check refusal/selection contracts; no cached product is executed."""
import copy
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import struct
from types import SimpleNamespace
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'scripts'))
import local_check as local


class LocalCheckTests(unittest.TestCase):
    def test_shared_recipe_identity_is_pure_and_binds_inputs_and_flags(self):
        files = [{'path': 'example.c', 'sha256': 'a' * 64, 'size_bytes': 7}]
        original = copy.deepcopy(files)
        value = local.sdk.recipe_identity(files, 'locked-input')
        self.assertEqual(value['compile_flags'], ['-Os', '-fwrapv', '-D_GNU_SOURCE', 'CONFIG_VERSION=locked'])
        self.assertEqual(value['link_flags'], ['-Os', '-Wl,-z,max-page-size=65536', '-lm', '-lpthread'])
        self.assertEqual(value['archive_flags'], 'rcsD')
        self.assertNotIn('git_commit', value)
        self.assertEqual(files, original)
        for changed in (local.sdk.recipe_identity(files, 'different-input'),
                        local.sdk.recipe_identity(files, 'locked-input', ['-DTEST=1']),
                        local.sdk.recipe_identity([{**files[0], 'sha256': 'b' * 64}], 'locked-input')):
            self.assertNotEqual(value['sha256'], changed['sha256'])
        self.assertEqual(local.current_recipe(), local.sdk.recipe_identity(local.sdk.recipe(), local.InputLock().identity()['input_id']))

    def cli(self, *arguments):
        return subprocess.run([sys.executable, '-B', str(ROOT / 'scripts/local_check.py'), *map(str, arguments)],
                              cwd=ROOT, capture_output=True, text=True, timeout=30)

    def test_plans_are_selected_product_contracts_not_execution(self):
        with tempfile.TemporaryDirectory() as temp:
            directory = Path(temp)
            report = directory / 'check.json'
            result = self.cli('--profile', 'work', '--plan', '--output-dir', directory, '--report', report)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            value = json.loads(report.read_text(encoding='utf-8'))
            self.assertEqual(value['status'], 'planned')
            self.assertEqual(value['execution_status'], 'not_run')
            self.assertFalse(value['evidence_verified'])
            self.assertFalse(value['release_ready'])
            self.assertIn('m2-work-js-cancel', value['selection']['host_cases'])
            self.assertIn('lifetime', value['selection']['host_cases'])
            self.assertIn('run-cache-namespace', value['selection']['cli_cases'])
            self.assertNotIn('m2-process-inherit', value['selection']['host_cases'])
            self.assertIsNone(value['supplier'])

    def test_tty_product_absence_cannot_pass_as_os_probe(self):
        if any(row[0].startswith(('m2-tty', 'm2-stdio')) for row in local.sdk.HOST_CASES):
            self.skipTest('Product TTY catalog has since been implemented')
        with tempfile.TemporaryDirectory() as temp:
            report = Path(temp) / 'check.json'
            result = self.cli('--profile', 'tty', '--plan', '--output-dir', temp, '--report', report)
            self.assertEqual(result.returncode, 1)
            self.assertEqual(json.loads(report.read_text(encoding='utf-8'))['status'], 'blocked')

    def test_ci_plan_has_no_runtime_case_or_build(self):
        with tempfile.TemporaryDirectory() as temp:
            report = Path(temp) / 'check.json'
            result = self.cli('--profile', 'ci', '--plan', '--output-dir', temp, '--report', report)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            value = json.loads(report.read_text(encoding='utf-8'))
            self.assertEqual(value['selection']['host_cases'], [])
            self.assertIn('test_ci*.py', value['selection']['ci_patterns'])

    def test_all_and_tty_plans_include_implemented_stdio_without_tty_claim(self):
        with tempfile.TemporaryDirectory() as temp:
            for profile in ('all', 'tty'):
                with self.subTest(profile=profile):
                    report = Path(temp) / (profile + '.json')
                    result = self.cli('--profile', profile, '--plan', '--output-dir', temp, '--report', report)
                    self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                    value = json.loads(report.read_text(encoding='utf-8'))
                    self.assertEqual(value['execution_status'], 'not_run')
                    self.assertIn('m2-stdio-js-bytes', value['selection']['host_cases'])
                    self.assertIn('m2-stdio-backpressure', value['selection']['host_cases'])

    def test_invalid_profile_and_partial_supplier_are_refused(self):
        for args in [('--profile', 'unknown'), ('--supplier-report', 'old.json')]:
            with self.subTest(args=args):
                self.assertEqual(self.cli(*args).returncode, 2)

    def test_outputs_cannot_overwrite_sources_supplier_or_old_report(self):
        with tempfile.TemporaryDirectory() as temp:
            directory = Path(temp)
            report = directory / 'old.json'
            report.write_text('old bytes', encoding='utf-8')
            self.assertEqual(self.cli('--plan', '--output-dir', temp, '--report', report).returncode, 2)
            self.assertEqual(report.read_text(encoding='utf-8'), 'old bytes')
            self.assertEqual(self.cli('--plan', '--output-dir', ROOT / 'src', '--report', directory / 'a.json').returncode, 2)
            supplier = directory / 'supplier'
            supplier.mkdir()
            source = directory / 'producer.json'
            source.write_text('{}', encoding='utf-8')
            self.assertEqual(self.cli('--plan', '--supplier-report', source, '--supplier-dir', supplier,
                '--output-dir', supplier, '--report', directory / 'b.json').returncode, 2)

    def test_old_recipe_and_changed_bytes_are_rejected_before_execution(self):
        expected = {'sha256': 'a' * 64, 'files': [{'path': 'src/runtime.c', 'sha256': 'b' * 64}]}
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            artifact = root / 'host.com'
            artifact.write_bytes(b'actual')
            row = {'file': 'host.com', **{k: local.identity(artifact)[k] for k in ('sha256', 'size_bytes')}}
            producer = {'report_kind': 'qaplet-sdk', 'scope': 'build', 'status': 'pass',
                        'sdk_recipe': copy.deepcopy(expected), 'input_lock': {'pinned': True}, 'distribution': [row]}
            path = root / 'producer.json'
            # Metadata stays outside the distribution, as a real producer does.
            distribution = root / 'artifacts'
            distribution.mkdir()
            artifact.rename(distribution / 'host.com')
            path.write_text(json.dumps(producer), encoding='utf-8')
            with patch.object(local, 'InputLock') as lock, patch.object(local.sdk, 'check_package'):
                lock.return_value.identity.return_value = {'pinned': True}
                local.validate_supplier(path, distribution, expected)
                (distribution / 'host.com').write_bytes(b'corrupt')
                with self.assertRaisesRegex(ValueError, 'bytes differ'):
                    local.validate_supplier(path, distribution, expected)
                producer['sdk_recipe']['files'][0]['sha256'] = 'c' * 64
                path.write_text(json.dumps(producer), encoding='utf-8')
                with self.assertRaisesRegex(ValueError, 'recipe/source'):
                    local.validate_supplier(path, distribution, expected)

    def test_extra_distribution_file_is_not_a_cache_hit(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            distribution = root / 'artifacts'
            distribution.mkdir()
            (distribution / 'good').write_bytes(b'good')
            (distribution / 'unexpected').write_bytes(b'extra')
            path = root / 'build.json'
            row = local.identity(distribution / 'good')
            value = {'report_kind': 'qaplet-sdk', 'scope': 'build', 'status': 'pass', 'sdk_recipe': {},
                'input_lock': {}, 'distribution': [{'file': 'good', **{k: row[k] for k in ('sha256', 'size_bytes')}}]}
            path.write_text(json.dumps(value), encoding='utf-8')
            with patch.object(local, 'InputLock') as lock:
                lock.return_value.identity.return_value = {}
                with self.assertRaisesRegex(ValueError, 'unrecorded'):
                    local.validate_supplier(path, distribution, {})

    def test_partial_cache_is_refused_without_rebuilding(self):
        with tempfile.TemporaryDirectory() as temp:
            cache = Path(temp)
            key = 'b' * 64
            (cache / 'build/local-check' / key).mkdir(parents=True)
            args = SimpleNamespace(supplier_report=None, supplier_dir=None)
            with patch.object(local.platform, 'system', return_value='Linux'), \
                 patch.object(local.platform, 'freedesktop_os_release', return_value={'ID': 'ubuntu'}), \
                 patch.object(local.sdk, 'build') as build:
                with self.assertRaises(OSError):
                    local.supplier(args, {}, cache, {'sha256': key})
                build.assert_not_called()

    def test_contract_identity_binds_expected_values_and_helper_bytes(self):
        original = local.contract_identity('engine')
        cases = tuple((name, argv, expected + ' altered' if name == 'lifetime' else expected)
                      for name, argv, expected in local.sdk.HOST_CASES)
        with patch.object(local.sdk, 'HOST_CASES', cases):
            self.assertNotEqual(local.contract_identity('engine')['sha256'], original['sha256'])
        self.assertTrue(any(row['file'] == 'scripts/local_check.py' for row in original['helpers']))
        self.assertEqual(local.contract_identity('engine')['sha256'], original['sha256'])

    def test_public_c_dependency_and_object_validation(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            deps = root / 'source.d'
            deps.write_text('qaplet-public-header: /tmp/source.c /tmp/path\\ with\\ spaces.h\n', encoding='utf-8')
            self.assertEqual(len(local.dependency_paths(deps)), 2)
            data = bytearray(64)
            data[:6] = b'\x7fELF\x02\x01'
            struct.pack_into('<HH', data, 16, 1, 62)
            obj = root / 'header.o'
            obj.write_bytes(data)
            self.assertEqual(local.verify_object(obj, 'x86_64')['machine'], 62)
            with self.assertRaisesRegex(ValueError, 'ELF64 REL'):
                local.verify_object(obj, 'aarch64')
            (root / 'header.o').write_bytes(b'not a real object')
            with self.assertRaisesRegex(ValueError, 'ELF64 REL'):
                local.verify_object(obj, 'x86_64')


if __name__ == '__main__':
    unittest.main()
