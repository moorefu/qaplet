"""Actual sealed M2 package and package-only C Host, no fabricated product runs."""
import hashlib
import json
import os
from pathlib import Path
import selectors
import subprocess
import sys
import unittest
import zipfile

ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'scripts'))
import sdk
import sdk_m2_example


class M2SDKPackageContract(unittest.TestCase):
    @unittest.skipUnless(sys.platform == 'linux' and os.environ.get('QAPLET_SDK_ARTIFACT_DIR') and
                         os.environ.get('QAPLET_SDK_LOADER_DIR'), 'Actual supplied Linux SDK and loader required')
    def test_aggregate_child_dispatch_stays_alive_until_stdin_eof(self):
        host = Path(os.environ['QAPLET_SDK_ARTIFACT_DIR']) / 'qaplet-host.com'
        loader = Path(os.environ['QAPLET_SDK_LOADER_DIR']) / 'ape-x86_64.elf'
        with subprocess.Popen([loader, host, '--m2-aggregate-child', '--hang'],
                              stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE) as child:
            try:
                with selectors.DefaultSelector() as ready:
                    ready.register(child.stdout, selectors.EVENT_READ)
                    self.assertTrue(ready.select(10), 'Aggregate child did not become ready')
                self.assertEqual(child.stdout.read(1), b'R')
                self.assertIsNone(child.poll(), 'Child exited while its input remained open')
                output, errors = child.communicate(timeout=10)
                self.assertEqual((child.returncode, output, errors), (37, b'', b''))
            finally:
                if child.poll() is None:
                    child.kill()
                    child.communicate(timeout=10)

    @unittest.skipUnless(os.environ.get('QAPLET_SDK_NATIVE_REPORT'), 'Actual supplied M2 SDK native report required')
    def test_native_m2_example_replays_raw_copies_and_rejects_false_reap_or_cleanup(self):
        report=json.loads(Path(os.environ['QAPLET_SDK_NATIVE_REPORT']).read_text(encoding='utf-8'))
        cases={row['case_id']:row for row in report['cases']}
        for suffix in sdk_m2_example.CASES:
            row=cases['qaplet-m2-example.'+suffix]
            command=row['commands'][0]
            stdout=Path(command['stdout_log']).read_bytes()
            stderr=Path(command['stderr_log']).read_bytes()
            self.assertEqual(row['actual']['stdout'].encode(),stdout)
            self.assertEqual(row['actual']['stderr'].encode(),stderr)
            self.assertEqual(row['status'],'pass')
            sdk_m2_example.inspect(suffix,stdout,stderr,command['exit_code'],sdk.M2_RUNTIME_METADATA)
            if suffix in ('normal','cleanup-pending'):
                records=[json.loads(line) for line in stdout.decode().splitlines()]
                if suffix=='normal':
                    next(item for item in records if item['stage']=='process')['reaped']=0
                else:
                    next(item for item in records if item['stage']=='cleanup')['destroyed']=True
                corrupted=('\n'.join(json.dumps(item) for item in records)+'\n').encode()
                with self.assertRaises(ValueError):
                    sdk_m2_example.inspect(suffix,corrupted,stderr,command['exit_code'],sdk.M2_RUNTIME_METADATA)

    @unittest.skipUnless(os.environ.get('QAPLET_SDK_BUILD_REPORT') and os.environ.get('QAPLET_SDK_ARTIFACT_DIR'),
                         'Actual Ubuntu SDK producer and its supplied directory required')
    def test_sealed_sdk_contains_both_creation_examples_and_complete_m2_metadata(self):
        producer=json.loads(Path(os.environ['QAPLET_SDK_BUILD_REPORT']).read_text(encoding='utf-8'))
        supplied=Path(os.environ['QAPLET_SDK_ARTIFACT_DIR'])
        self.assertEqual(producer['status'],'pass')
        artifacts={row['name']:row for row in producer['artifacts']}
        self.assertEqual(set(artifacts),{'qaplet-host','qaplet','qaplet-example','qaplet-m2-example'})
        with zipfile.ZipFile(supplied/producer['package']['file']) as archive:
            for name in ('sdk/application.c','sdk/application-m2.c','sdk/m1-qaplet.h',
                         'sdk/include/qaplet.h','sdk/lib/libqaplet.a','sdk/lib/.aarch64/libqaplet.a'):
                data=archive.read(name)
                member=next(row for row in producer['package']['members'] if row['file']==name)
                self.assertEqual(hashlib.sha256(data).hexdigest(),member['sha256'])
            manifest=json.loads(archive.read('sdk/manifest.json'))
        self.assertEqual(manifest['instance_capabilities'],manifest['capabilities'])
        expected_fields={'worker_threads','helper_threads','worker_stack_bytes','helper_stack_bytes',
                         'work_handlers','handler_concurrency','timers','reactor_registrations',
                         'native_handles','processes','keepalive_tokens','operations','operation_data_bytes',
                         'work_input_bytes','work_output_bytes','io_block_bytes','process_arguments',
                         'process_data_bytes','pump_io_bytes'}
        self.assertEqual(set(manifest['m2_defaults']),expected_fields)
        self.assertEqual({key:manifest['m2_defaults'][key] for key in
                          ('worker_threads','helper_threads','operations','operation_data_bytes')},
                         {'worker_threads':2,'helper_threads':4,'operations':128,'operation_data_bytes':8388608})


if __name__=='__main__':unittest.main()
