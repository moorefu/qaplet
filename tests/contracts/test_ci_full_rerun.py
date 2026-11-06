"""The real collector selects current provider originals before local verify."""
import hashlib
import io
import json
import os
from pathlib import Path
import sys
import tempfile
import unittest
from unittest import mock
import zipfile

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'scripts'))
import ci_full


class FullRerunCollection(unittest.TestCase):
    def test_latest_same_name_original_is_selected_and_old_failure_is_not_fallback(self):
        (ROOT / 'out').mkdir(exist_ok=True)
        with tempfile.TemporaryDirectory(prefix='qaplet-rerun-', dir=ROOT / 'out') as temporary:
            root = Path(temporary)
            archives = root / 'archives'
            archives.mkdir()
            commit = 'a' * 40
            records = []
            for number in (2, 1):
                path = archives / (str(number) + '.zip')
                with zipfile.ZipFile(path, 'w') as archive:
                    archive.writestr('observation.json', json.dumps({'marker': 'new-failure' if number == 2 else 'old-success'}))
                records.append({'id': number, 'name': 'm0-build-' + commit, 'expired': False,
                                'workflow_run': {'id': 17, 'head_sha': commit},
                                'created_at': '2026-11-06T00:00:0' + str(number) + 'Z',
                                'digest': 'sha256:' + hashlib.sha256(path.read_bytes()).hexdigest()})
            listing = {'total_count': 2, 'artifacts': records}
            opener = mock.Mock()
            opener.open.return_value = io.BytesIO(json.dumps(listing).encode())
            environment = {'GITHUB_REPOSITORY': 'owner/repo', 'GITHUB_RUN_ID': '17',
                           'GITHUB_SHA': commit, 'GITHUB_TOKEN': 'test-only-placeholder', 'GITHUB_EVENT_NAME': 'push'}
            with mock.patch.dict(os.environ, environment), mock.patch.object(ci_full, 'git_read', return_value=(commit + '\n').encode()), mock.patch.object(ci_full.urllib.request, 'build_opener', return_value=opener):
                result = ci_full.collect_actions(archives.resolve(), commit)
            self.assertEqual(result['artifact_count'], 1)
            selected = json.loads((archives / 'archives.json').read_text(encoding='utf-8'))
            self.assertEqual([a['id'] for a in selected['artifacts']], [2])
            report = json.loads(Path(result['collection_report']).read_text(encoding='utf-8'))
            evidence = Path(report['artifacts'][0]['root']) / 'observation.json'
            self.assertEqual(json.loads(evidence.read_text(encoding='utf-8'))['marker'], 'new-failure')
            self.assertTrue((archives / '1.zip').is_file())
