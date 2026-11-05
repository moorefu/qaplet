#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Public package-only M2 C Host composition and its observable contract."""
import json
from pathlib import Path

from build_ape import identity, run

CASES = ('normal', 'file-error', 'cancel', 'cleanup-pending', 'metadata', 'arguments')
FILE_BYTES = b'I\0\xffN\n'
M2_DEFAULTS = {
    'worker_threads': 2, 'helper_threads': 4, 'worker_stack_bytes': 1048576,
    'helper_stack_bytes': 1048576, 'work_handlers': 64, 'handler_concurrency': 1,
    'timers': 1024, 'reactor_registrations': 256, 'native_handles': 256,
    'processes': 32, 'keepalive_tokens': 64, 'operations': 128,
    'operation_data_bytes': 8388608, 'work_input_bytes': 1048576,
    'work_output_bytes': 1048576, 'io_block_bytes': 65536,
    'process_arguments': 256, 'process_data_bytes': 1048576, 'pump_io_bytes': 262144}


def expected(suffix):
    return {'exit_code': 0 if suffix in ('normal', 'metadata') else 2 if suffix == 'arguments' else 1,
            'contract': 'm2-c-host/' + suffix + '/v1'}


def arguments(suffix, artifact, launcher, work):
    if suffix == 'metadata':
        return ['--metadata']
    if suffix == 'arguments':
        return []
    return ['--' + suffix, str(artifact), str(launcher), str(work / ('missing.bin' if suffix == 'file-error' else 'input.bin'))]


def inspect(suffix, stdout, stderr, exit_code, metadata):
    """Check public copied bytes, OS status and separate cleanup facts."""
    def check(condition, message):
        if not condition:
            raise ValueError('M2 example ' + suffix + ': ' + message)
    check(exit_code == expected(suffix)['exit_code'], 'actual native exit differs')
    if suffix == 'arguments':
        check(not stdout and stderr.startswith(b'usage: application-m2 MODE SELF LAUNCHER FILE\n'), 'usage bytes differ')
        return None
    check(not stderr, 'unexpected stderr')
    if suffix == 'metadata':
        value = json.loads(stdout.decode('utf-8'))
        check(value == metadata, 'live library/instance/default metadata differs from manifest')
        return value
    rows = [json.loads(line) for line in stdout.decode('utf-8').splitlines()]
    stages = {row['stage']: row for row in rows}
    wanted = ['created'] + (['cancel'] if suffix in ('cancel', 'cleanup-pending') else [])
    wanted += ['active', 'event', 'work', 'process', 'closed']
    pending = suffix == 'cleanup-pending'
    wanted += ([] if pending else ['quiescent']) + ['cleanup']
    check([row['stage'] for row in rows] == wanted, 'stage order/unique observations differ')
    check(stages['created'] == {'stage': 'created', 'capabilities': metadata['instance_capabilities'],
                               'helper_limit': 4, 'rollback': 0}, 'creation/default helper contract differs')
    check(stages['event'] == {'stage': 'event', 'consumed': 1, 'copied': True}, 'copied producer event differs')
    if suffix in ('cancel', 'cleanup-pending'):
        check(stages['cancel'] == {'stage': 'cancel', 'accepted': True, 'code': 28, 'physical_pending': 1},
              'accepted cancellation must retain the in-flight worker')
    work = stages['work']
    work_contract = (2, 0, 0, 0, 5, FILE_BYTES.hex()) if suffix == 'normal' else (
        (3, 19, 1, 2, 0, '') if suffix == 'file-error' else (4, 28, 0, 0, 0, ''))
    check(tuple(work[key] for key in ('state', 'code', 'error_domain', 'error', 'bytes', 'hex')) == work_contract,
          'file work result/native error/copy differs')
    child = stages['process']
    check(all(child[key] == 1 for key in ('exited', 'reaped', 'delivered')) and
          child['native_exit_code'] == 37 and child['native_signal'] == 0,
          'actual direct-child exit/reap differs')
    check(child['stdout_eof'] is True and child['stderr_eof'] is True and
          child['stdin_written'] == (5 if suffix == 'normal' else 0) and
          child['stdout_hex'] == (FILE_BYTES.hex() if suffix == 'normal' else '') and
          child['stderr_hex'] == b'child stderr\n'.hex(), 'managed stdio bytes/EOF differ')
    for stage in ('active', 'closed', *(() if pending else ('quiescent',))):
        pod = stages[stage]
        check(not pod['termination_reason'] and not pod['cleanup_reason'] and not pod['control_error'] and
              not pod['uncertain_handles'] and 0 < pod['helpers_peak'] <= 4,
              'POD native/control/default helper facts differ')
    check(stages['closed']['lifecycle'] == 4 and stages['closed']['phase'] == 0, 'close must publish CLOSED')
    cleanup = stages['cleanup']
    check(cleanup == {'stage': 'cleanup', 'native_cleanup_complete': not pending, 'destroyed': not pending,
                      'producer_joined': True, 'destroy_code': 4 if pending else 0,
                      'wait_reason': 2 if pending else 6, 'retained_until_process_exit': pending},
          'close/quiesce/destroy and producer handoff differ')
    if pending:
        check(stages['closed']['native_state'] == 1 and stages['closed']['operations'] == 1 and
              stages['closed']['operation_data_bytes'] > 0, 'in-flight native data must remain held after CLOSED')
    else:
        pod = stages['quiescent']
        check(pod['lifecycle'] == 4 and pod['native_state'] == 2 and
              all(pod[key] == 0 for key in ('handles', 'operations', 'operation_data_bytes', 'io_endpoints', 'helpers', 'helper_stack_bytes')),
              'quiescence must retire actual managed native resources')
    return stages


def run_cases(args, report, directory, artifact, loader, metadata, cases=CASES):
    artifact = artifact.resolve()
    loader = [Path(value).resolve() for value in loader]
    launcher = loader[0] if loader else '-'
    for suffix in cases:
        case_id = 'qaplet-m2-example.' + suffix
        work = directory / case_id
        work.mkdir()
        input_identity = None
        if suffix not in ('metadata', 'arguments', 'file-error'):
            (work / 'input.bin').write_bytes(FILE_BYTES)
            input_identity = identity(work / 'input.bin')
        argv = [*loader, artifact, *arguments(suffix, artifact, launcher, work)]
        executed, _, _ = run(report, directory, case_id, argv, args.timeout, cwd=work, expected=None)
        stdout = Path(executed['stdout_log']).read_bytes()
        stderr = Path(executed['stderr_log']).read_bytes()
        passed = not executed['timed_out']
        facts, reason = None, 'Public package-only C Host composed M2 resources in one Runtime'
        try:
            facts = inspect(suffix, stdout, stderr, executed['exit_code'], metadata)
        except (ValueError, KeyError, TypeError) as error:
            passed, reason = False, str(error)
        report['cases'].append({'case_id': case_id, 'required': True, 'status': 'pass' if passed else 'fail',
            'started_at': executed['started_at'], 'finished_at': executed['finished_at'],
            'expected': expected(suffix), 'actual': {'exit_code': executed['exit_code'],
                'stdout': stdout.decode('utf-8', 'replace'), 'stderr': stderr.decode('utf-8', 'replace'),
                'timed_out': executed['timed_out']}, 'commands': [executed], 'artifact': identity(artifact),
            'sdk_sha256': report['sdk']['sha256'], 'timeout_seconds': args.timeout,
            'input': input_identity, 'observations': facts,
            'native_cleanup_complete': None if suffix in ('metadata', 'arguments') else suffix != 'cleanup-pending',
            'reason': reason})
