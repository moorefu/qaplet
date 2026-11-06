"""Public observations and fixed-input execution must preserve evidence boundaries."""
import copy
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'scripts'))
import ci_observation as observation
import dev_execution
import guest_observation
import ci_tiers


def git(repo, *args):
    return subprocess.check_output(['git', '-c', 'core.autocrlf=false', '-C', str(repo), *args], text=True, stderr=subprocess.PIPE).strip()


def repository(repo):
    git(repo, 'init', '-q')
    git(repo, 'config', 'user.name', 'Fixture')
    git(repo, 'config', 'user.email', 'fixture@example.invalid')


class ObservationCLI(unittest.TestCase):
    def invoke(self, repo, operation, *arguments, stdin=None, success=True):
        target = repo / 'out' / ('result-' + str(len(list((repo / 'out').glob('*.json')))) + '.json')
        process = subprocess.run([sys.executable, '-B', str(ROOT / 'scripts/ci_evidence.py'), operation,
                                  '--repo', str(repo), '--report', str(target), *map(str, arguments)],
                                 input=stdin, capture_output=True, text=True, timeout=20)
        self.assertEqual(process.returncode, 0 if success else 1, process.stdout + process.stderr)
        return json.loads(target.read_text(encoding='utf-8'))

    def save(self, repo, name, value):
        path = repo / (name + '.json')
        path.write_text(json.dumps(value), encoding='utf-8')
        return path

    def test_connector_stdin_and_zero_delta_keep_pending_failure_and_missing_visible(self):
        with tempfile.TemporaryDirectory() as temp:
            repo = Path(temp)
            (repo / 'out').mkdir()
            rows = {'total_count': 1, 'workflow_runs': [{'id': 1, 'head_sha': 'a' * 40, 'name': 'Product',
                'head_branch': 'main', 'event': 'push', 'status': 'in_progress', 'conclusion': None}]}
            envelope = json.dumps({'structuredContent': {'content': json.dumps(rows)}})
            common = ['--commit', 'a' * 40, '--input-response', '-', '--workflow', 'Product', '--workflow', 'Tools']
            value = self.invoke(repo, 'status', *common, stdin=envelope)
            self.assertEqual(value['pending_run_ids'], [1])
            self.assertEqual(value['missing_workflows'], ['Tools'])
            self.assertFalse(value['all_success'])
            self.assertEqual(value['network_requests'], 0)
            retained = Path(value['sources'][0]['path'])
            self.assertEqual(retained.read_text(encoding='utf-8'), envelope)
            previous = self.save(repo, 'previous', value)
            current = self.invoke(repo, 'status', *common, '--previous-report', previous, stdin=envelope)
            self.assertEqual(current['changed_count'], 0)
            rows['workflow_runs'][0].update(status='completed', conclusion='failure')
            failed = self.invoke(repo, 'status', *common, '--previous-report', previous, stdin=json.dumps(rows))
            self.assertEqual(failed['changed_count'], 1)
            self.assertFalse(failed['all_success'])
            rows['total_count'] = 2
            self.invoke(repo, 'status', *common, stdin=json.dumps(rows), success=False)

    def test_artifact_exact_names_ids_and_commit_never_use_architecture_prefix(self):
        with tempfile.TemporaryDirectory() as temp:
            repo = Path(temp)
            (repo / 'out').mkdir()
            def artifact(number, name):
                return {'id': number, 'name': name, 'expired': False, 'digest': 'sha256:' + 'b' * 64,
                        'size_in_bytes': 16, 'workflow_run': {'id': 17, 'head_sha': 'a' * 40}}
            x64 = artifact(1, 'native-ubuntu-x64')
            arm = artifact(2, 'native-ubuntu-arm64')
            catalog = self.save(repo, 'catalog', {'artifacts': [x64, arm]})
            original = catalog.read_bytes()
            common = ['--commit', 'a' * 40, '--run-id', 17, '--manifest', catalog]
            value = self.invoke(repo, 'select-artifacts', *common, '--artifact-name', x64['name'])
            self.assertEqual([row['id'] for row in value['artifacts']], [1])
            self.assertEqual(catalog.read_bytes(), original)
            self.invoke(repo, 'select-artifacts', *common, '--artifact-name', 'native-ubuntu', success=False)
            for mutation in ('duplicate', 'commit', 'run', 'expired', 'digest'):
                rows = copy.deepcopy([x64, arm])
                if mutation == 'duplicate': rows.append(artifact(3, x64['name']))
                elif mutation == 'commit': rows[0]['workflow_run']['head_sha'] = 'c' * 40
                elif mutation == 'run': rows[0]['workflow_run']['id'] = 99
                elif mutation == 'expired': rows[0]['expired'] = True
                else: rows[0]['digest'] = None
                self.save(repo, 'catalog', {'artifacts': rows})
                with self.subTest(mutation=mutation):
                    self.invoke(repo, 'select-artifacts', *common, '--artifact-name', x64['name'], success=False)
                if mutation == 'duplicate':
                    selected = self.invoke(repo, 'select-artifacts', *common, '--artifact-name', x64['name'], '--artifact-id', 3)
                    self.assertEqual(selected['artifacts'][0]['id'], 3)

    def test_status_requires_complete_job_coverage_before_all_success(self):
        with tempfile.TemporaryDirectory() as temp:
            repo = Path(temp)
            (repo / 'out').mkdir()
            runs = self.save(repo, 'runs', {'workflow_runs': [{'id': 1, 'head_sha': 'a' * 40, 'name': 'Product',
                'status': 'completed', 'conclusion': 'success'}]})
            common = ['--commit', 'a' * 40, '--input-response', runs, '--workflow', 'Product']
            value = self.invoke(repo, 'status', *common)
            self.assertFalse(value['all_success'])
            self.assertEqual(value['missing_job_run_ids'], [1])
            job = {'id': 11, 'run_id': 1, 'name': 'build', 'status': 'completed', 'conclusion': 'success'}
            jobs = self.save(repo, 'jobs', {'total_count': 2, 'jobs': [job]})
            self.invoke(repo, 'status', *common, '--jobs-response', jobs, success=False)
            self.save(repo, 'jobs', {'total_count': 1, 'jobs': [job]})
            self.assertTrue(self.invoke(repo, 'status', *common, '--jobs-response', jobs)['all_success'])
            job['conclusion'] = 'failure'
            self.save(repo, 'jobs', {'jobs': [job]})
            self.assertFalse(self.invoke(repo, 'status', *common, '--jobs-response', jobs)['all_success'])

    def test_final_receipt_requires_real_merge_tree_main_and_every_workflow_job(self):
        with tempfile.TemporaryDirectory() as temp:
            repo = Path(temp)
            (repo / 'out').mkdir()
            repository(repo)
            (repo / '.github/workflows').mkdir(parents=True)
            for name in ('Product', 'Tools'):
                filename = 'm0.yml' if name == 'Product' else 'tools.yml'
                (repo / '.github/workflows' / filename).write_text(json.dumps({'name': name, 'on': {'push': {}}}), encoding='utf-8')
            git(repo, 'add', '.github')
            git(repo, 'commit', '-qm', 'base')
            git(repo, 'branch', '-M', 'main')
            git(repo, 'checkout', '-qb', 'codex/feature')
            (repo / 'source').write_bytes(b'new')
            git(repo, 'add', 'source')
            git(repo, 'commit', '-qm', 'reviewed')
            head = git(repo, 'rev-parse', 'HEAD')
            git(repo, 'checkout', '-q', 'main')
            git(repo, 'merge', '--no-ff', '-qm', 'merge', 'codex/feature')
            commit = git(repo, 'rev-parse', 'HEAD')
            pull = self.save(repo, 'pull', {'merged': True, 'state': 'closed', 'head': {'sha': head}, 'merge_commit_sha': commit})
            main = self.save(repo, 'main', {'ref': 'refs/heads/main', 'object': {'sha': commit}})
            rows = [{'id': i, 'name': name, 'head_sha': commit, 'head_branch': 'main', 'event': 'push',
                     'status': 'completed', 'conclusion': 'success'} for i, name in enumerate(('Product', 'Tools'), 1)]
            runs = self.save(repo, 'runs', {'total_count': 2, 'workflow_runs': rows})
            jobs = [{'id': i + 10, 'run_id': i, 'name': 'build' if i == 1 else 'tools',
                     'status': 'completed', 'conclusion': 'success'} for i in (1, 2)]
            job_path = self.save(repo, 'jobs', {'total_count': 2, 'jobs': jobs})
            common = ['--commit', commit, '--reviewed-head', head, '--pull-response', pull,
                      '--main-response', main, '--input-response', runs, '--jobs-response', job_path]
            value = self.invoke(repo, 'finalize', *common)
            self.assertTrue(value['main_fixed'])
            self.assertFalse(value['formal_m2_complete'])
            self.assertFalse(value['release_ready'])
            for conclusion in ('failure', 'cancelled', None):
                with self.subTest(conclusion=conclusion):
                    jobs[1]['conclusion'] = conclusion
                    self.save(repo, 'jobs', {'jobs': jobs})
                    self.invoke(repo, 'finalize', *common, success=False)
            jobs[1]['conclusion'] = 'success'
            self.save(repo, 'jobs', {'jobs': jobs[:1]})
            self.invoke(repo, 'finalize', *common, success=False)
            self.save(repo, 'jobs', {'jobs': jobs})
            jobs[0]['conclusion'] = 'skipped'
            jobs[1]['name'] = 'build'
            self.save(repo, 'jobs', {'jobs': jobs})
            self.invoke(repo, 'finalize', *common, success=False)
            jobs[0]['conclusion'] = 'success'
            self.save(repo, 'jobs', {'jobs': jobs})
            self.save(repo, 'main', {'ref': 'refs/heads/main', 'object': {'sha': head}})
            self.invoke(repo, 'finalize', *common, success=False)

    def test_reports_never_overwrite_sources_or_previous_results(self):
        with tempfile.TemporaryDirectory() as temp:
            repo = Path(temp)
            (repo / 'out').mkdir()
            source = self.save(repo / 'out', 'original', {'keep': True})
            before = source.read_bytes()
            process = subprocess.run([sys.executable, str(ROOT / 'scripts/ci_evidence.py'), 'status', '--repo', str(repo),
                '--report', str(source), '--input-response', str(source), '--commit', 'a' * 40], capture_output=True, timeout=10)
            self.assertEqual(process.returncode, 1)
            self.assertEqual(source.read_bytes(), before)

    def test_failed_stdin_is_retained_in_a_new_nested_report_directory(self):
        with tempfile.TemporaryDirectory() as temp:
            repo = Path(temp)
            target = repo / 'out/nested/result.json'
            process = subprocess.run([sys.executable, str(ROOT / 'scripts/ci_evidence.py'), 'status', '--repo', str(repo),
                '--report', str(target), '--input-response', '-', '--commit', 'a' * 40], input='{"workflow_runs":[]}',
                capture_output=True, text=True, timeout=10)
            self.assertEqual(process.returncode, 1)
            self.assertEqual(target.with_name('result-stdin.json').read_bytes(), b'{"workflow_runs":[]}')


class ExecutionContract(unittest.TestCase):
    def test_new_execution_entry_refuses_existing_outputs_without_overwriting(self):
        with tempfile.TemporaryDirectory() as temp:
            repo = Path(temp)
            repository(repo)
            output = repo / 'out/build'
            output.mkdir(parents=True)
            target = output / 'workflow.json'
            target.write_bytes(b'original evidence')
            result = subprocess.run([sys.executable, str(ROOT / 'scripts/dev.py'), 'build-sdk', '--repo', str(repo),
                '--source-ref', 'HEAD', '--plan', '--output-dir', str(output)], capture_output=True, timeout=10)
            self.assertEqual(result.returncode, 1)
            self.assertEqual(target.read_bytes(), b'original evidence')

    def test_temporary_preflight_and_child_environment_do_not_change_parent(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            before = dict(os.environ)
            value = dev_execution.temporary_root(root, 0)
            self.assertTrue(value['writable'])
            self.assertEqual(list(root.iterdir()), [])
            child = dev_execution.child_environment(root)
            self.assertEqual(child['TEMP'], str(root))
            self.assertEqual(dict(os.environ), before)
            with self.assertRaisesRegex(ValueError, 'free space'):
                dev_execution.temporary_root(root, 2**100)

    def test_frozen_source_uses_committed_bytes_and_refuses_mutated_supplier(self):
        from types import SimpleNamespace
        with tempfile.TemporaryDirectory() as temp:
            repo = Path(temp)
            repository(repo)
            (repo / 'scripts').mkdir()
            (repo / 'marker').write_bytes(b'committed')
            producer = "import argparse,json,subprocess\nfrom pathlib import Path\np=argparse.ArgumentParser();p.add_argument('--report');a,_=p.parse_known_args()\nvalue={'status':'pass','sdk_recipe':{'git_commit':subprocess.check_output(['git','rev-parse','HEAD'],text=True).strip()},'marker':Path('marker').read_text()}\nPath(a.report).write_text(json.dumps(value))\n"
            (repo / 'scripts/sdk.py').write_text(producer, encoding='utf-8')
            git(repo, 'add', '.')
            git(repo, 'commit', '-qm', 'frozen')
            commit = git(repo, 'rev-parse', 'HEAD')
            (repo / 'marker').write_bytes(b'working edits')
            context = {'checkout': str(repo), 'cache_root': str(repo / '.cache')}
            environment = dev_execution.child_environment(repo)
            args = SimpleNamespace(source_ref=commit, plan=False, timeout=20)
            output = repo / 'out/build'
            output.mkdir(parents=True)
            report = {'commands': []}
            dev_execution.frozen_sdk(args, context, output, report, environment)
            self.assertEqual(json.loads((output / 'sdk-build.json').read_text(encoding='utf-8'))['marker'], 'committed')
            self.assertEqual((repo / 'marker').read_bytes(), b'working edits')
            self.assertIn(str(repo / '.cache/quickjs'), report['planned_commands'][0])
            (repo / 'scripts/sdk.py').write_text(producer + "Path('marker').write_text('changed')\n", encoding='utf-8')
            git(repo, 'add', 'scripts/sdk.py')
            git(repo, 'commit', '-qm', 'mutating supplier')
            args.source_ref = git(repo, 'rev-parse', 'HEAD')
            output = repo / 'out/mutating'
            output.mkdir()
            with self.assertRaisesRegex(ValueError, 'snapshot changed'):
                dev_execution.frozen_sdk(args, context, output, {'commands': []}, environment)


class GuestObservationContract(unittest.TestCase):
    def test_proc_sample_is_direct_child_only_bounded_and_advisory(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            pid = str(os.getpid())
            proc = root / 'proc'
            children = proc / pid / 'task' / pid / 'children'
            children.parent.mkdir(parents=True)
            children.write_text('123 124', encoding='utf-8')
            for number, parent in ((123, pid), (124, '1')):
                base = proc / str(number)
                base.mkdir()
                (base / 'cmdline').write_bytes(b'/usr/bin/qemu-system-aarch64\0-kernel\0')
                fields = ['S', parent] + ['0'] * 22
                fields[11], fields[12], fields[17], fields[19], fields[21] = '3', '4', '1', '99', '42'
                (base / 'stat').write_text(str(number) + ' (qemu test) ' + ' '.join(fields), encoding='utf-8')
            sampler = guest_observation.ResourceSampler(root, 'qemu-system-aarch64', 1, proc=proc, cgroup=root / 'missing')
            sample = sampler.sample()
            self.assertEqual([row['pid'] for row in sample['qemu_processes']], [123])
            self.assertEqual(sample['qemu_processes'][0]['rss_pages'], 42)
            with patch.object(sampler.stop, 'wait', return_value=False):
                sampler.observe()
            result = sampler.summary()
            self.assertEqual(result['samples'], 3)
            self.assertEqual(len(sampler.path.read_text(encoding='utf-8').splitlines()), 3)
            self.assertFalse(result['evidence_verified'])
            self.assertTrue(result['limitations'])
            with patch.object(guest_observation, 'identity', side_effect=OSError('unreadable diagnostic')):
                self.assertIsNone(sampler.summary()['log'])

    def test_missing_observer_output_does_not_hide_kernel_unknown(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            with guest_observation.ResourceSampler(root / 'missing', 'qemu', 1) as sampler:
                pass
            self.assertTrue(sampler.summary()['observer_joined'])
            log = root / 'serial'
            log.write_text('QAPLET_ab123 CASE m2-process-js-terminal START 1\nwatchdog: BUG: soft lockup\n', encoding='utf-8')
            value = guest_observation.kernel_observation([log])
            self.assertEqual(value['last_case_started'], 'm2-process-js-terminal')
            self.assertEqual(value['attribution'], 'Unknown')
            self.assertEqual(len(value['soft_lockup_observations']), 1)


class CLIWiringContract(unittest.TestCase):
    def test_required_gate_cannot_drop_cli_from_full(self):
        from types import SimpleNamespace
        with tempfile.TemporaryDirectory() as temp:
            names = ('tier-plan', 'fast', 'portable-build', 'native-startup', 'page-plan', 'arm64-page-gaps',
                     'startup-matrix', 'full-core', 'offline-rebuild')
            results = {name: {'result': 'success'} for name in names}
            results['tier-plan']['outputs'] = {'run_fast': 'false', 'run_cross': 'true', 'run_full': 'true', 'run_cli': 'false'}
            results['page-plan']['outputs'] = {'needs_guest': 'true'}
            path = Path(temp) / 'needs.json'
            path.write_text(json.dumps(results), encoding='utf-8')
            with self.assertRaisesRegex(ValueError, 'Invalid selected tier'):
                ci_tiers.required_gate(SimpleNamespace(needs_json=path))

    def test_cli_producer_consumer_and_failure_transport_cannot_be_missing_or_tolerated(self):
        original = json.loads((ROOT / '.github/workflows/m0.yml').read_text(encoding='utf-8'))
        ci_tiers.check_cli_transport(original)
        for job, pattern in (('portable-build', 'scripts/sdk.py cli-consume'), ('native-startup', 'scripts/sdk_cli.py '),
                             ('native-startup', 'm2-cli-native-')):
            for mutation in ('missing', 'skip', 'tolerate'):
                value = copy.deepcopy(original)
                steps = value['jobs'][job]['steps']
                selected = next(step for step in steps if pattern in step.get('run', '') or pattern in step.get('with', {}).get('name', ''))
                if mutation == 'missing': steps.remove(selected)
                elif mutation == 'skip': selected['if'] = '${{ false }}'
                else: selected['continue-on-error'] = True
                with self.subTest(job=job, pattern=pattern, mutation=mutation), self.assertRaises(ValueError):
                    ci_tiers.check_cli_transport(value)


if __name__ == '__main__':
    unittest.main()
