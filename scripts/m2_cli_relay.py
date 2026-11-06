"""Actual Windows relay retirement, with release and retained-thread gates."""
import json
import os
import subprocess
import time

from build_ape import identity
from foundation import now


def append_cases(args, report, directory, artifact, help_bytes):
    for mode, name in (('1', 'released'), ('2', 'held')):
        work = directory / ('relay-retirement-' + name)
        work.mkdir()
        started, before = now(), time.monotonic()
        command = [str(artifact), '--help']
        timed_out = False
        process = subprocess.Popen(command, cwd=work, env=dict(os.environ, QAPLET_RELAY_RETIRE_FAULT=mode),
                                   stdin=subprocess.DEVNULL, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        try:
            stdout, stderr = process.communicate(timeout=args.timeout)
        except subprocess.TimeoutExpired:
            timed_out = True
            process.kill()
            stdout, stderr = process.communicate()
        elapsed = time.monotonic() - before
        for filename, data in (('stdout.log', stdout), ('stderr.log', stderr)):
            (work / filename).write_bytes(data)
        trace = work / 'relay-retirement.ndjson'
        rows = [json.loads(line) for line in trace.read_text().splitlines()] if trace.is_file() else []
        operations = [row['operation'] for row in rows]
        waits = [row['value'] for row in rows if row['operation'] == 'relay-native-bounded-wait']
        passed = not timed_out and stdout == help_bytes and bool(waits) and all(0 < value <= 5000 for value in waits)
        passed = passed and 'relay-native-handle' in operations and 'relay-return-held' in operations
        if mode == '1':
            order = ('relay-native-handle', 'relay-native-bounded-wait', 'relay-return-released', 'relay-joined', 'relay-native-handle-closed')
            passed = passed and process.returncode == 0 and not stderr and all(value in operations for value in order)
            passed = passed and [operations.index(value) for value in order] == sorted(operations.index(value) for value in order)
            passed = passed and 'output-return-released' in operations
        else:
            passed = passed and process.returncode == 1 and b'interrupt relay retained' in stderr
            passed = passed and not any(value in operations for value in ('relay-return-released', 'relay-joined', 'relay-native-handle-closed'))
        observation = {'argv': command, 'cwd': str(work), 'started_at': started, 'finished_at': now(),
            'exit_code': process.returncode, 'timeout_seconds': args.timeout, 'timed_out': timed_out,
            'elapsed_seconds': elapsed, 'stdout_log': str(work / 'stdout.log'), 'stderr_log': str(work / 'stderr.log')}
        report['commands'].append(observation)
        report['cases'].append({'case_id': 'qaplet-cli.relay-retirement-' + name, 'required': True,
            'status': 'pass' if passed else 'fail', 'controlled_system_fixture': True, 'commands': [observation],
            'expected': {'exit_code': 0 if mode == '1' else 1, 'native_cleanup_complete': mode == '1'},
            'native_cleanup_complete': passed and mode == '1', 'native_retention_verified': passed and mode == '2',
            'trace': identity(trace) if trace.is_file() else None, 'observations': rows,
            'scope': 'Actual thread retirement and bounded retention; a negative case never claims cleanup success'})
