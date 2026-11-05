"""Synthetic archived process evidence exercises the real startup/reuse CLI only."""
from datetime import datetime, timezone, timedelta
import json
import hashlib
import http.server
import threading
import zipfile
import io
import shutil
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'scripts'))
import local_check as local
from sdk_matrix import catalog


def fixture(root, os_name='Windows', ci=None, commit='a' * 40):
    """Data-only fixture; these bytes must never be executed as product binaries."""
    artifacts = root / '.cache/build/local-check/fixture/artifacts'
    result_root = root / 'out/fast'
    result_root.mkdir(parents=True)
    for name in ('sdk/lib/libqaplet.a', 'sdk/lib/.aarch64/libqaplet.a', 'sdk/include/qaplet.h',
                 'qaplet-host.com', 'qaplet.com', 'qaplet-example.com'):
        path = artifacts / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(b'synthetic contract fixture: ' + name.encode())
    sdk = local.sdk
    recipe = {**local.current_recipe(), 'git_commit': commit, 'working_tree_dirty': False}
    rows = [sdk.relative_identity(p, artifacts) for p in sorted((artifacts / 'sdk').rglob('*')) if p.is_file()]
    producer = {'schema_version': 1, 'report_kind': 'qaplet-sdk', 'scope': 'build', 'status': 'pass',
        'report_id': 'fixture-producer', 'sdk_recipe': recipe, 'input_lock': local.InputLock().identity(),
        'sdk': {'components': rows, 'sha256': sdk.digest(rows)}, 'source': {'fixture': True},
        'toolchain': {'archive': {'fixture': True}}, 'artifacts': [{'name': name, **local.identity(artifacts / (name + '.com'))}
            for name in ('qaplet-host', 'qaplet', 'qaplet-example')]}
    sdk.seal_package(producer, artifacts)
    rows = [sdk.relative_identity(p, artifacts) for p in sorted(artifacts.rglob('*')) if p.is_file()]
    producer['distribution'] = rows
    # seal_package includes the manifest in the aggregate SDK identity.
    producer['sdk']['components'] = [row for row in rows if row['file'].startswith('sdk/')]
    producer['sdk']['sha256'] = sdk.digest(producer['sdk']['components'])
    path = artifacts.parent / 'build.json'
    local.write_json(path, producer)
    host = {'os': os_name, 'cpu': 'x86_64', 'execution_arch': 'x86_64', 'execution_mode': 'native', 'page_size': 4096}
    cases = []
    with local.selected_catalog(local.select_cases('engine')):
        for index, (case, name, argv, code, out, err, contains) in enumerate(catalog(host)):
            stdout, stderr = result_root / ('stdout-' + str(index)), result_root / ('stderr-' + str(index))
            stdout.write_bytes(out.encode('utf-8'))
            stderr.write_bytes(err.encode('utf-8'))
            cases.append({'case_id': case, 'status': 'pass', 'artifact': local.identity(artifacts / (name + '.com')),
                'commands': [{'exit_code': code, 'timed_out': False, 'stdout_log': str(stdout), 'stderr_log': str(stderr)}]})
    runtime = result_root / 'selected-sdk.json'
    local.write_json(runtime, {'status': 'pass', 'host': host, 'ci': ci, 'cases': cases})
    report = result_root / 'check.json'
    value = {'report_kind': 'qaplet-local-check', 'scope': 'development-only', 'profile': 'engine',
        'status': 'pass', 'execution_status': 'executed', 'commit': commit, 'ci': ci,
        'contract_sha256': local.contract_identity('engine')['sha256'], 'current_recipe_sha256': local.current_recipe()['sha256'],
        'finished_at': datetime.now(timezone.utc).isoformat(),
        'supplier': {'producer_report': local.identity(path), 'artifacts_directory': str(artifacts)},
        'runtime': {'status': 'pass', 'report': local.identity(runtime), 'host': host}, 'public_c': {'status': 'not_applicable'}}
    if os_name == 'Linux':
        import struct
        compiler, source, deps, obj = [result_root / name for name in ('fixture-cc', 'fixture.c', 'fixture.d', 'fixture.o')]
        compiler.write_bytes(b'synthetic compiler fixture')
        source.write_bytes(b'synthetic source fixture')
        deps.write_bytes(b'synthetic dependencies fixture')
        data = bytearray(64)
        data[:6] = b'\x7fELF\x02\x01'
        struct.pack_into('<HH', data, 16, 1, 62)
        obj.write_bytes(data)
        inputs = [local.identity(source), local.identity(compiler)]
        c_report = result_root / 'public-c.json'
        local.write_json(c_report, {'report_kind': 'qaplet-local-c-compile', 'scope': 'public-header-compile',
            'status': 'pass', 'host': host, 'commands': [cases[0]['commands'][0]],
            'inputs_before': inputs, 'inputs_after': inputs, 'compiler': [local.identity(compiler)],
            'object': local.verify_object(obj, 'x86_64'), 'dependency_files': [local.identity(deps)]})
        value['public_c'] = {'status': 'pass', 'report': local.identity(c_report)}
    local.write_json(report, value)
    return report, value


class StartupReuseContract(unittest.TestCase):
    def test_rejected_reuse_reaches_the_actual_execution_supplier_and_preserves_rejection(self):
        with tempfile.TemporaryDirectory() as temp:
            work = Path(temp)
            original, value = fixture(work / 'origin')
            value['finished_at'] = '2000-01-01T00:00:00+00:00'
            local.write_json(original, value)
            bad_supplier = work / 'bad-supplier.json'
            local.write_json(bad_supplier, {'status': 'fail'})
            artifacts = work / 'bad-artifacts'
            artifacts.mkdir()
            process = subprocess.run([sys.executable, str(ROOT / 'scripts/dev.py'), 'check-fast', '--profile', 'engine',
                '--reuse-report', str(original), '--supplier-report', str(bad_supplier), '--supplier-dir', str(artifacts),
                '--output-dir', str(work / 'output'), '--report', str(work / 'dev.json')],
                cwd=ROOT, capture_output=True, text=True, timeout=30)
            self.assertEqual(process.returncode, 1, process.stdout + process.stderr)
            result = json.loads((work / 'output/local-check.json').read_text(encoding='utf-8'))
            self.assertEqual(result['reuse']['status'], 'miss')
            self.assertIn('Successful existing SDK producer', result['reason'])
            self.assertEqual(result['execution_status'], 'not_run')
            self.assertEqual(result['reuse']['attempts'][0]['status'], 'fail')

    def test_actions_startup_replays_original_zip_and_provider_before_skipping(self):
        for mode in ('valid', 'prior-original', 'previous-attempt', 'stale', 'sha', 'recipe', 'contract', 'missing-report', 'bad-bytes', 'provider-digest', 'bad-existing-id'):
            with self.subTest(mode=mode), tempfile.TemporaryDirectory(dir=ROOT / 'out') as temp:
                work = Path(temp)
                commit = hashlib.sha1(str(work).encode()).hexdigest()
                origin, value = fixture(work / 'origin', 'Linux', {'run_id': '23', 'commit': commit}, commit)
                if mode == 'stale': value['finished_at'] = '2000-01-01T00:00:00+00:00'
                if mode == 'sha': value['commit'] = 'b' * 40
                if mode == 'recipe': value['current_recipe_sha256'] = 'b' * 64
                if mode == 'contract': value['contract_sha256'] = 'b' * 64
                local.write_json(origin, value)
                if mode == 'missing-report': Path(value['runtime']['report']['path']).unlink()
                if mode == 'bad-bytes': (origin.parent / 'stdout-0').write_bytes(b'bad')
                data = io.BytesIO()
                with zipfile.ZipFile(data, 'w', zipfile.ZIP_DEFLATED) as bundle:
                    for path in (work / 'origin').rglob('*'):
                        if path.is_file(): bundle.write(path, path.relative_to(work / 'origin').as_posix())
                payload = data.getvalue()
                latest = io.BytesIO(payload)
                with zipfile.ZipFile(latest, 'a', zipfile.ZIP_DEFLATED) as bundle:
                    bundle.writestr('latest-wrapper.txt', 'distinct later run; original report still has CI 23')
                latest_payload = latest.getvalue()
                self.assertTrue((work / 'origin').resolve().is_relative_to(work.resolve()))
                shutil.rmtree(work / 'origin')  # A path alias to local originals cannot pass.
                artifact_id = int.from_bytes(os.urandom(5), 'big') + 1
                name = 'qaplet-fast-' + commit
                requests = []
                class Handler(http.server.BaseHTTPRequestHandler):
                    def log_message(self, *args): pass
                    def do_GET(self):
                        requests.append((self.path, self.headers.get('Authorization')))
                        run = {'id': 23, 'head_sha': commit, 'status': 'completed', 'conclusion': 'success'}
                        if self.path.endswith('/zip'):
                            self.send_response(302)
                            self.send_header('Location', 'http://127.0.0.1:' + str(self.server.server_port) + '/archive-' + self.path.split('/')[-2])
                            self.end_headers()
                            return
                        if self.path.startswith('/archive-'):
                            body = latest_payload if self.path.endswith(str(artifact_id + 1)) else payload
                        elif '/workflows/' in self.path:
                            body = json.dumps({'workflow_runs': [] if mode == 'previous-attempt' else ([{**run, 'id': 24}] if mode == 'prior-original' else []) + [run]}).encode()
                        elif '/attempts/' in self.path:
                            body = json.dumps(run).encode()
                        else:
                            newer = '/runs/24/' in self.path
                            content = latest_payload if newer else payload
                            body = json.dumps({'total_count': 1, 'artifacts': [{'id': artifact_id + int(newer), 'name': name,
                                'expired': False, 'digest': 'sha256:' + ('b' * 64 if mode == 'provider-digest' else hashlib.sha256(content).hexdigest()),
                                'size_in_bytes': len(content), 'workflow_run': {'id': 24 if newer else 23, 'head_sha': commit}}]}).encode()
                        self.send_response(200)
                        self.end_headers()
                        self.wfile.write(body)
                server = http.server.ThreadingHTTPServer(('127.0.0.1', 0), Handler)
                worker = threading.Thread(target=server.serve_forever, daemon=True)
                worker.start()
                cache = Path(local.workspace(ROOT)['cache_root']) / 'downloads/ci'
                if mode == 'bad-existing-id':
                    cache.mkdir(parents=True, exist_ok=True)
                    (cache / (str(artifact_id) + '-' + name + '.zip')).write_bytes(b'retained corrupt synthetic cache')
                try:
                    env = dict(os.environ, GITHUB_TOKEN='must-not-reach-loopback')
                    if mode == 'previous-attempt':
                        env.update(GITHUB_RUN_ID='23', GITHUB_RUN_ATTEMPT='2')
                    process = subprocess.run([sys.executable, str(ROOT / 'scripts/local_check.py'), '--profile', 'engine',
                        '--reuse-only', '--reuse-actions', '--reuse-repository', 'owner/repo',
                        '--reuse-api-base', 'http://127.0.0.1:' + str(server.server_port), '--expected-commit', commit,
                        '--expected-os', 'Linux', '--expected-arch', 'x86_64',
                        '--output-dir', str(work / 'output'), '--report', str(work / 'result.json')],
                        cwd=ROOT, env=env, capture_output=True, text=True, timeout=30)
                    self.assertEqual(process.returncode, 0, process.stdout + process.stderr)
                    result = json.loads((work / 'result.json').read_text(encoding='utf-8'))
                    self.assertEqual(result['execution_status'], 'reused_previous' if mode in ('valid', 'prior-original', 'previous-attempt') else 'not_run', result['reuse'])
                    self.assertTrue(all(auth is None for _, auth in requests))
                    if mode in ('valid', 'prior-original', 'previous-attempt'):
                        self.assertEqual(result['reuse']['original_ci']['run_id'], '23')
                        self.assertEqual(result['reuse']['provider']['artifact']['id'], artifact_id)
                        self.assertTrue(list((work / 'output').rglob('original.zip')))
                    if mode == 'prior-original':
                        first = cache / (str(artifact_id) + '-' + name + '.zip')
                        second = cache / (str(artifact_id + 1) + '-' + name + '.zip')
                        self.assertEqual(first.read_bytes(), payload)
                        self.assertEqual(second.read_bytes(), latest_payload)
                    if mode == 'bad-existing-id':
                        self.assertEqual((cache / (str(artifact_id) + '-' + name + '.zip')).read_bytes(), b'retained corrupt synthetic cache')
                        self.assertTrue(any('Cached archive identity mismatch' in attempt.get('reason', '') for attempt in result['reuse']['attempts']))
                finally:
                    server.shutdown()
                    server.server_close()

    def test_fast_workflow_checks_originals_before_any_input_acquisition_and_guards_fallback(self):
        workflow = json.loads((ROOT / '.github/workflows/m0.yml').read_text(encoding='utf-8'))
        steps = workflow['jobs']['fast']['steps']
        reuse_step = next((step for step in steps if step.get('id') == 'reuse'), None)
        self.assertIsNotNone(reuse_step)
        self.assertIn('--reuse-only --reuse-actions', reuse_step['run'])
        self.assertNotIn('if', reuse_step)
        first_acquire = next(i for i, step in enumerate(steps) if 'fetch_locked_toolchain' in step.get('run', ''))
        self.assertLess(steps.index(reuse_step), first_acquire)
        for step in steps:
            if 'run' in step and step != reuse_step:
                self.assertEqual(step.get('if'), "${{ steps.reuse.outputs.reused != 'true' }}")
        # The public workflow checker must reject a forged shortcut or missing
        # verifier; cache-hit itself is never authorization to skip execution.
        with tempfile.TemporaryDirectory() as temp:
            work = Path(temp)
            for mode in ('missing-verifier', 'unconditional-skip', 'manual-cancel'):
                value = json.loads(json.dumps(workflow))
                if mode == 'missing-verifier':
                    value['jobs']['fast']['steps'] = [step for step in value['jobs']['fast']['steps'] if step.get('id') != 'reuse']
                if mode == 'unconditional-skip':
                    next(step for step in value['jobs']['fast']['steps'] if 'local_check.py --profile all --output' in step.get('run', ''))['if'] = '${{ false }}'
                if mode == 'manual-cancel':
                    value['jobs']['fast']['concurrency']['cancel-in-progress'] = "${{ needs.tier-plan.outputs.run_full != 'true' }}"
                path = work / (mode + '.json')
                local.write_json(path, value)
                result = subprocess.run([sys.executable, str(ROOT / 'scripts/build_ape.py'), 'check-ci', '--workflow', str(path),
                    '--report', str(work / (mode + '-result.json'))], capture_output=True, text=True, timeout=30)
                self.assertEqual(result.returncode, 1, result.stdout + result.stderr)

    def test_startup_accepts_matching_original_and_falls_back_on_each_refusal(self):
        for mode in ('valid', 'stale', 'sha', 'recipe', 'contract', 'missing-report', 'bad-bytes'):
            with self.subTest(mode=mode), tempfile.TemporaryDirectory() as temp:
                root = Path(temp)
                original, value = fixture(root / 'original')
                if mode == 'stale': value['finished_at'] = (datetime.now(timezone.utc) - timedelta(days=8)).isoformat()
                if mode == 'sha': value['commit'] = 'b' * 40
                if mode == 'recipe': value['current_recipe_sha256'] = 'b' * 64
                if mode == 'contract': value['contract_sha256'] = 'b' * 64
                local.write_json(original, value)
                if mode == 'missing-report': Path(value['runtime']['report']['path']).unlink()
                if mode == 'bad-bytes': (original.parent / 'stdout-0').write_bytes(b'bad')
                before = original.read_bytes()
                process = subprocess.run([sys.executable, str(ROOT / 'scripts/local_check.py'), '--profile', 'engine',
                    '--reuse-only', '--reuse-report', str(original), '--expected-commit', 'a' * 40,
                    '--expected-os', 'Windows', '--expected-arch', 'x86_64',
                    '--output-dir', str(root / 'output'), '--report', str(root / 'result.json')],
                    cwd=ROOT, capture_output=True, text=True, timeout=30)
                self.assertEqual(process.returncode, 0, process.stdout + process.stderr)
                result = json.loads((root / 'result.json').read_text(encoding='utf-8'))
                self.assertEqual(result['execution_status'], 'reused_previous' if mode == 'valid' else 'not_run')
                self.assertEqual(result['reuse']['status'], 'pass' if mode == 'valid' else 'miss')
                self.assertEqual(result['reuse']['new_product_executions'], 0)
                self.assertEqual(original.read_bytes(), before)
                self.assertFalse(result['full_verified'])


if __name__ == '__main__':
    unittest.main()
