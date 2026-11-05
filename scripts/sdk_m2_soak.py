#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Observe supplied M2 Full workloads and replay their actual raw evidence."""
import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import time
import uuid

from foundation import host_facts
from workflow_support import EvidencePaths, guard_evidence_outputs, identity, now, write_json

ROOT = Path(__file__).resolve().parents[1]


def linux_resources(pid):
    """Read the target process, including actual kernel wait-channel samples."""
    root = Path('/proc') / str(pid)
    status = dict(line.split(':', 1) for line in (root / 'status').read_text(encoding='ascii').splitlines())
    stat = (root / 'stat').read_text(encoding='ascii').rsplit(')', 1)[1].split()
    waits = []
    for task in sorted((root / 'task').iterdir()):
        try:
            waits.append({'tid': int(task.name), 'wchan': (task / 'wchan').read_text(encoding='ascii').strip(),
                          'state': (task / 'stat').read_text(encoding='ascii').rsplit(')', 1)[1].split()[0]})
        except FileNotFoundError:
            continue  # A sampled native thread really retired during inspection.
    return {'source': '/proc/PID/stat,status,fd,task/TID/wchan',
            'cpu_seconds': (int(stat[11]) + int(stat[12])) / os.sysconf('SC_CLK_TCK'),
            'rss_bytes': int(status.get('VmRSS', '0 kB').split()[0]) * 1024,
            'handles': len(list((root / 'fd').iterdir())),
            'threads': int(status['Threads']), 'thread_waits': waits}


def progress(path):
    """Latest flushed record is advisory during execution; final replay is strict."""
    for line in reversed(path.read_bytes().splitlines()):
        try:
            row = json.loads(line)
            if isinstance(row, dict):
                return row
        except (ValueError, UnicodeError):
            continue
    return {}


def complete_lines(path):
    return Path(path).read_bytes().split(b'\n')[:-1] if Path(path).exists() else []


def observe_command(argv, cwd, directory, timeout, interval=1.0, *, target_progress=None):
    """Execute once, retain raw bytes incrementally, and sample real OS resources.

    This timeout is only a supervisor safety bound. Killing a timed-out command
    always leaves it failed; it cannot supply native quiescence or destroy facts.
    """
    directory = Path(directory).resolve()
    directory.mkdir(parents=True, exist_ok=False)
    cwd = Path(cwd).resolve()
    output, errors = directory / 'stdout.ndjson', directory / 'stderr.log'
    samples = directory / 'os-samples.ndjson'
    started, start = now(), time.monotonic()
    epoch = int(time.time() * 1000)
    timed_out, sampler = False, None
    argv = [str(arg) for arg in argv]
    native_samples = directory / 'windows-native-samples.ndjson'
    progress_path = Path(target_progress) if target_progress else output
    sampler_argv, target_pid = None, None
    with output.open('wb') as stdout, errors.open('wb') as stderr, samples.open('w', encoding='utf-8') as observations:
        with subprocess.Popen(argv, cwd=cwd, stdout=stdout, stderr=stderr) as child:
            consumed = 0
            while child.poll() is None:
                elapsed = time.monotonic() - start
                if elapsed >= timeout:
                    timed_out = True
                    child.kill()
                    break
                if os.name == 'nt':
                    if target_pid is None:
                        if target_progress:
                            ready = [json.loads(line) for line in complete_lines(progress_path)]
                            target_pid = next((row['process_id'] for row in ready if row.get('type') == 'ready'), None)
                        else:
                            target_pid = child.pid
                    if target_pid is not None and sampler is None:
                        script = Path(__file__).with_name('m2_soak_windows_sampler.ps1')
                        shell = Path(os.environ['SystemRoot']) / 'System32/WindowsPowerShell/v1.0/powershell.exe'
                        sampler_argv = [str(shell), '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', str(script),
                            '-ProcessId', str(target_pid), '-OutputPath', str(native_samples),
                            '-StartUnixMilliseconds', str(epoch), '-IntervalMilliseconds', str(max(10, int(interval * 1000)))]
                        sampler = subprocess.Popen(sampler_argv, stdout=subprocess.DEVNULL, stderr=stderr,
                                                   creationflags=subprocess.CREATE_NO_WINDOW)
                    rows = complete_lines(native_samples)
                    for raw in rows[consumed:]:
                        row = json.loads(raw)
                        row['progress'] = progress(progress_path) if progress_path.exists() else {}
                        observations.write(json.dumps(row, sort_keys=True) + '\n')
                    consumed = len(rows)
                else:
                    row = {'elapsed_seconds': elapsed, 'process_id': child.pid,
                           'progress': progress(progress_path) if progress_path.exists() else {}}
                    try:
                        row['resources'] = linux_resources(child.pid)
                    except (FileNotFoundError, ProcessLookupError):
                        row['process_exited_during_sample'] = True
                    observations.write(json.dumps(row, sort_keys=True) + '\n')
                observations.flush()
                try:
                    child.wait(timeout=min(interval, max(.001, timeout - elapsed)))
                except subprocess.TimeoutExpired:
                    pass
            exit_code = child.wait()
            process_elapsed = time.monotonic() - start
            if sampler is not None:
                try:
                    sampler.wait(timeout=max(5, interval * 2))
                except subprocess.TimeoutExpired:
                    sampler.kill()
                    sampler.wait()
                rows = complete_lines(native_samples)
                for raw in rows[consumed:]:
                    row = json.loads(raw)
                    row['progress'] = progress(progress_path) if progress_path.exists() else {}
                    observations.write(json.dumps(row, sort_keys=True) + '\n')
    return {'argv': argv, 'cwd': str(cwd), 'pid': child.pid, 'started_at': started, 'finished_at': now(),
            'elapsed_seconds': time.monotonic() - start, 'process_elapsed_seconds': process_elapsed,
            'exit_code': exit_code, 'timed_out': timed_out, 'supervisor_terminated': timed_out,
            'timeout_seconds': timeout, 'sample_interval_seconds': interval,
            'stdout_log': str(output), 'stderr_log': str(errors), 'samples_log': str(samples),
            'native_samples_log': str(native_samples) if sampler else None,
            'progress_log': str(progress_path), 'target_pid': target_pid or child.pid,
            'sampler_argv': sampler_argv, 'sampler_exit_code': sampler.returncode if sampler else 0}


def console_child(args):
    from m2_tty_terminal import conpty
    record, output = conpty(args.command, args.directory, args.timeout)
    write_json(args.native_report, record)
    sys.stdout.buffer.write(output)
    return 0 if record['exit_code'] == 0 and not record['timed_out'] and not record['error'] else 1


def snapshot_inputs(directory, files):
    rows = []
    for index, path in enumerate(files):
        path = Path(path).resolve()
        target = directory / ('%02d-' % index + path.name)
        shutil.copyfile(path, target)
        original, retained = identity(path), identity(target)
        if original['sha256'] != retained['sha256']:
            raise ValueError('Input changed while freezing: ' + str(path))
        rows.append({'original': original, 'retained': retained})
    return rows


def sample_summary(command):
    """Summarize actual samples, without inventing a cross-platform performance bar."""
    rows = [json.loads(line) for line in complete_lines(command['samples_log'])]
    if any('sampler_error' in row for row in rows) or command['sampler_exit_code'] != 0:
        raise ValueError('OS sampler failed; raw failure remains in evidence')
    active = [row for row in rows if row.get('resources')]
    if len(active) < 2:
        raise ValueError('At least two real OS samples required')
    cpu = [row['resources']['cpu_seconds'] for row in active]
    elapsed = [row['elapsed_seconds'] for row in active]
    if cpu != sorted(cpu) or elapsed != sorted(elapsed):
        raise ValueError('OS CPU or elapsed sample counters regressed')
    progress_rows = [json.loads(line) for line in complete_lines(command['progress_log'])]
    ready = next((row for row in progress_rows if row.get('type') == 'ready'), {})
    owner = ready.get('owner_thread_id')
    owner_waits, wait_kinds = 0, {}
    phases = {}
    for index, row in enumerate(active):
        phase = row['progress'].get('phase', 'unassigned')
        phase_summary = phases.setdefault(phase, {'samples': 0, 'observed_elapsed_seconds': 0, 'cpu_seconds': 0})
        phase_summary['samples'] += 1
        for thread in row['resources']['thread_waits']:
            if thread.get('tid') != owner:
                continue
            reason = thread.get('wait_reason') or thread.get('wchan')
            if thread.get('state') in ('Wait', 'S', 'D') and reason and reason != '0':
                owner_waits += 1
                kind = phase + ':' + reason
                wait_kinds[kind] = wait_kinds.get(kind, 0) + 1
        if index and active[index - 1]['progress'].get('phase', 'unassigned') == phase:
            phase_summary['observed_elapsed_seconds'] += row['elapsed_seconds'] - active[index - 1]['elapsed_seconds']
            phase_summary['cpu_seconds'] += row['resources']['cpu_seconds'] - active[index - 1]['resources']['cpu_seconds']
    ranges = {}
    for key in ('rss_bytes', 'handles', 'threads'):
        values = [row['resources'][key] for row in active]
        ranges[key] = {'first': values[0], 'last': values[-1], 'minimum': min(values), 'maximum': max(values)}
    return {'sample_count': len(active), 'source': sorted({row['resources']['source'] for row in active}),
            'owner_thread_id': owner, 'owner_wait_samples': owner_waits, 'owner_wait_kinds': wait_kinds,
            'first_elapsed_seconds': elapsed[0], 'last_elapsed_seconds': elapsed[-1],
            'cpu_seconds_delta': cpu[-1] - cpu[0], 'ranges': ranges, 'phases': phases,
            'performance_threshold': None,
            'scope': 'OS samples describe live process behavior; fixture post-destroy readback proves baseline return'}


def validate_duration(command, final, minimum_seconds):
    if command['timed_out'] or command['exit_code'] != 0:
        raise ValueError('Incomplete process cannot prove elapsed workload')
    if command['process_elapsed_seconds'] < minimum_seconds:
        raise ValueError('Full requires actual process elapsed at least the requested duration')
    if final['elapsed_ms'] < minimum_seconds * 1000:
        raise ValueError('Full requires fixture monotonic elapsed at least the requested duration')


def check_identity(selected, resolve=Path):
    measured = identity(resolve(selected['path']))
    if any(measured[key] != selected[key] for key in ('sha256', 'size_bytes')):
        raise ValueError('Selected evidence changed: ' + selected['path'])


def inspect_run(report, resolve=Path):
    """Replay the selected raw fixture and OS samples, never cached pass fields."""
    import sdk_m2_cycles
    command = report['command']
    required = {command[key] for key in ('stdout_log', 'stderr_log', 'samples_log', 'progress_log')}
    if command['native_samples_log']:
        required.add(command['native_samples_log'])
    if not required.issubset({row['path'] for row in report['raw_evidence']}):
        raise ValueError('A required raw log is absent from the evidence inventory')
    for selected in report['raw_evidence']:
        check_identity(selected, resolve)
    if command['timed_out'] or command['supervisor_terminated'] or command['exit_code'] != 0:
        raise ValueError('Supervisor interruption or nonzero exit cannot pass Full')
    final = sdk_m2_cycles.inspect(resolve(command['progress_log']).read_bytes(),
        resolve(command['stderr_log']).read_bytes(), command['exit_code'], report['mode'], report['requested'])
    local_command = dict(command)
    for key in ('stdout_log', 'stderr_log', 'samples_log', 'progress_log'):
        local_command[key] = str(resolve(command[key]))
    measured = sample_summary(local_command)
    if final['failed'] != 0 or not final['native_cleanup_complete']:
        raise ValueError('Fixture failure or native cleanup incomplete')
    if report['mode'] == 'soak':
        validate_duration(command, final, report['requested'])
        if final['runtime_instances'] != 1 or final['created'] != 1:
            raise ValueError('Soak requires one persistent Runtime')
        if set(final['phase_elapsed_ms']) != {'idle', 'load', 'cancel'} or any(
                value <= 0 for value in final['phase_elapsed_ms'].values()):
            raise ValueError('Actual idle/load/cancel phases are required')
        if final['idle_wait_calls'] <= 0 or final['idle_wait_elapsed_ms'] <= 0:
            raise ValueError('Actual idle wait observations required')
        if report['purpose'] == 'full' and (report['requested'] < 3600 or not all(
                phase in measured['phases'] and measured['phases'][phase]['samples'] >= 2
                for phase in ('idle', 'load', 'cancel'))):
            raise ValueError('Full requires one hour and observed idle/load/cancel OS samples')
        if report['purpose'] == 'full' and not any(key.startswith('idle:') for key in measured['owner_wait_kinds']):
            raise ValueError('Full requires actual idle owner system-wait observations')
    elif report['purpose'] == 'full' and final['completed'] < 10000:
        raise ValueError('Full requires actual 10000 mixed lifecycles')
    if 'final' in report and report['final'] != final:
        raise ValueError('Cached completion facts contradict original fixture output')
    if 'os_observations' in report and report['os_observations'] != measured:
        raise ValueError('Cached OS observations contradict original samples')
    if report['host']['os'] == 'Windows':
        selected = report['native_console_report']
        check_identity(selected, resolve)
        console = json.loads(resolve(selected['path']).read_text(encoding='utf-8'))
        if console != report['native_console'] or console['timed_out'] or console['error'] or not all(console['cleanup'].values()):
            raise ValueError('Original ConPTY cleanup is incomplete or contradictory')
    return final, measured


def aggregate_native_reports(paths, resolve=Path):
    """Select exactly two native hosts and both actual Full workloads."""
    if len(paths) != 4:
        raise ValueError('Full requires four explicit original runs: Linux/Windows mixed/soak')
    import sdk
    reports = [(Path(path), json.loads(Path(path).read_text(encoding='utf-8'))) for path in paths]
    targets = [(report['host']['os'], report['mode']) for _, report in reports]
    if len(set(targets)) != 4 or set(targets) != {(host, mode) for host in ('Linux', 'Windows') for mode in ('mixed', 'soak')}:
        raise ValueError('Duplicate or missing Full host/workload selection')
    if len({report['command']['progress_log'] for _, report in reports}) != 4:
        raise ValueError('Duplicated raw workload evidence cannot satisfy Full')
    product_keys, cases = set(), []
    for path, report in reports:
        if report['report_kind'] != 'qaplet-m2-full-run' or report['purpose'] != 'full' or report['status'] != 'pass':
            raise ValueError('Required Full workload is failed, missing, or only smoke')
        host = report['host']
        if host['execution_mode'] != 'native' or host['cpu'] != host['execution_arch'] or host['execution_arch'] != 'x86_64' or not host['kernel'] or not host['page_size']:
            raise ValueError('Actual native x86-64 host identity required')
        for key in ('producer_report', 'fixture_producer', 'artifact', 'input', 'command_catalog'):
            check_identity(report[key], resolve)
        for row in report['evaluator_inputs']:
            check_identity(row['retained'], resolve)
        producer = json.loads(resolve(report['producer_report']['path']).read_text(encoding='utf-8'))
        fixture = json.loads(resolve(report['fixture_producer']['path']).read_text(encoding='utf-8'))
        if producer['status'] != 'pass' or fixture['status'] != 'pass' or any(
                report[key] != producer[key] for key in ('sdk', 'sdk_recipe')):
            raise ValueError('Original successful SDK identity does not join the native run')
        if fixture['sdk']['sha256'] != report['sdk']['sha256'] or fixture['sdk_recipe']['sha256'] != report['sdk_recipe']['sha256']:
            raise ValueError('Original cycle supplier identity does not join the native run')
        if not any(row['sha256'] == report['artifact']['sha256'] and row['size_bytes'] == report['artifact']['size_bytes'] for row in fixture['artifacts']):
            raise ValueError('Native cycle bytes differ from supplied artifact')
        sdk.check_package(producer, resolve(report['distribution_root']))
        final, measured = inspect_run(report, resolve)
        product_keys.add((report['sdk']['sha256'], report['sdk_recipe']['sha256'], report['artifact']['sha256']))
        cases.append({'case_id': host['os'] + '.' + report['mode'], 'status': 'pass', 'required': True,
                      'report': identity(path), 'final': final, 'os_observations': measured})
    if len(product_keys) != 1:
        raise ValueError('Full workloads must consume the same fixed SDK and APE bytes')
    return {'status': 'pass', 'cases': cases, 'sdk_sha256': next(iter(product_keys))[0],
            'sdk_recipe_sha256': next(iter(product_keys))[1], 'fixture_sha256': next(iter(product_keys))[2],
            'reason': 'Both native hosts completed actual 10000 mixed lifecycles and one-hour persistent-runtime workloads',
            'm2_formal_complete': False, 'release_ready': False}


def selected_files(paths, resolve):
    """Protect the complete explicit run inventory before creating join outputs."""
    files = []
    for path in paths:
        try:
            report = json.loads(Path(path).read_text(encoding='utf-8'))
        except (OSError, ValueError):
            continue  # The semantic reader reports missing or malformed inputs.
        if not isinstance(report, dict):
            continue
        rows = list(report.get('raw_evidence', []))
        rows += [report.get(key) for key in ('producer_report', 'fixture_producer', 'artifact',
                 'input', 'command_catalog', 'native_console_report')]
        for row in report.get('evaluator_inputs', []):
            rows.extend((row.get('original'), row.get('retained')))
        files.extend(resolve(row['path']) for row in rows if isinstance(row, dict) and isinstance(row.get('path'), str))
    return files


def run_supplied(args):
    import sdk
    from startup import measured_host, select_loader
    report_path = args.report.resolve()
    directory = report_path.parent / 'evidence' / str(uuid.uuid4())
    guard_evidence_outputs([report_path, directory], [args.build_report, args.fixture_producer],
                           [args.artifact_dir, args.supplied_host.parent])
    directory.mkdir(parents=True)
    report = {'schema_version': 1, 'report_kind': 'qaplet-m2-full-run', 'scope': args.purpose,
              'purpose': args.purpose, 'mode': args.mode, 'requested': args.requested,
              'status': 'not_run', 'started_at': now(), 'host': host_facts(), 'ci': None,
              'commands': [], 'raw_evidence': [], 'release_ready': False}
    try:
        producer = json.loads(args.build_report.read_text(encoding='utf-8'))
        fixture = json.loads(args.fixture_producer.read_text(encoding='utf-8'))
        if producer['status'] != 'pass' or fixture['status'] != 'pass':
            raise ValueError('Successful fixed SDK and independently validated cycle supplier required')
        if fixture['sdk']['sha256'] != producer['sdk']['sha256'] or fixture['sdk_recipe']['sha256'] != producer['sdk_recipe']['sha256']:
            raise ValueError('Cycle supplier SDK identity differs from selected fixed SKU')
        sdk.check_package(producer, args.supplied_host.parent.resolve())
        if sdk.recipe() != producer['sdk_recipe']['files']:
            raise ValueError('Current product recipe differs from frozen SDK')
        artifact = args.artifact_dir.resolve() / 'qaplet-m2-cycles.com'
        supplied = identity(artifact)
        matches = [row for row in fixture['artifacts'] if row['sha256'] == supplied['sha256'] and row['size_bytes'] == supplied['size_bytes']]
        if len(matches) != 1:
            raise ValueError('Exact independently supplied cycle APE required')
        report.update(sdk=producer['sdk'], sdk_recipe=producer['sdk_recipe'],
                      product_source={'git_commit': producer['sdk_recipe']['git_commit'],
                                      'working_tree_dirty': producer['sdk_recipe']['working_tree_dirty']},
                      engine_source=producer['source'], distribution_root=str(args.supplied_host.parent.resolve()),
                      producer_report=identity(args.build_report), fixture_producer=identity(args.fixture_producer), artifact=supplied)
        from foundation import command as context_command
        revision, commit, _ = context_command(['git', '-c', 'core.autocrlf=false', 'rev-parse', 'HEAD'],
                                               directory, 'evaluator-revision', cwd=ROOT)
        state, changed, _ = context_command(['git', '-c', 'core.autocrlf=false', 'status', '--porcelain'],
                                             directory, 'evaluator-status', cwd=ROOT)
        report['commands'].extend((revision, state))
        if revision['exit_code'] != 0 or state['exit_code'] != 0:
            raise ValueError('Evaluator checkout identity could not be measured')
        report['evaluator_source'] = {'git_commit': commit.strip(), 'working_tree_dirty': bool(changed.strip()),
                                      'note': 'Exact evaluator input bytes are retained separately from SDK product source'}
        measured_host(args, report, directory)
        loader = select_loader(args, report)
        inputs = directory / 'inputs'
        inputs.mkdir()
        files = [ROOT / name for name in ('scripts/sdk_m2_soak.py', 'scripts/m2_soak_windows_sampler.ps1',
                 'scripts/sdk_m2_cycles.py', 'tests/native/sdk_m2_cycles.c', 'tests/contracts/test_sdk_m2_soak.py',
                 'scripts/m2_tty_terminal.py', 'scripts/workflow_support.py')]
        report['evaluator_inputs'] = snapshot_inputs(inputs, files + [args.build_report, args.fixture_producer])
        work = directory / 'work'
        work.mkdir()
        data = work / 'input.bin'
        data.write_bytes(b'I\0\xffN\n')
        report['input'] = identity(data)
        launcher = loader[0] if loader else '-'
        argv = [*loader, artifact, '--' + args.mode, str(args.requested), artifact, launcher, data]
        if os.name != 'nt':
            artifact.chmod(artifact.stat().st_mode | 0o111)
            executed = argv
        else:
            executed = [sys.executable, Path(__file__).resolve(), 'console-child', '--directory', work,
                        '--native-report', work / 'console.json', '--timeout', str(args.timeout), '--', *argv]
        write_json(inputs / 'command-catalog.json', {'argv': [str(item) for item in executed], 'cwd': str(work),
            'timeout_seconds': args.timeout + (15 if os.name == 'nt' else 0), 'requested': args.requested,
            'mode': args.mode, 'purpose': args.purpose, 'sample_interval_seconds': args.sample_interval})
        report['command_catalog'] = identity(inputs / 'command-catalog.json')
        command = observe_command(executed, work, directory / 'execution',
            args.timeout + (15 if os.name == 'nt' else 0), args.sample_interval,
            target_progress=work / 'cycles-progress.ndjson')
        report['command'] = command
        report['commands'].append(command)
        if os.name == 'nt':
            console = json.loads((work / 'console.json').read_text(encoding='utf-8'))
            report['native_console'] = console
            report['native_console_report'] = identity(work / 'console.json')
            if console['timed_out'] or console['error'] or not all(console['cleanup'].values()):
                raise ValueError('ConPTY Host fixture did not complete controlled cleanup')
        paths = {command[key] for key in ('stdout_log', 'stderr_log', 'samples_log', 'progress_log')}
        if command['native_samples_log']:
            paths.add(command['native_samples_log'])
        report['raw_evidence'] = [identity(path) for path in sorted(paths)]
        final, measured = inspect_run(report)
        if identity(artifact) != supplied or sdk.recipe() != producer['sdk_recipe']['files']:
            raise ValueError('Consumed product or cycle APE changed during execution')
        for row in report['evaluator_inputs']:
            if identity(row['original']['path']) != row['original']:
                raise ValueError('Evaluator source changed during execution')
        report.update(status='pass', final=final, os_observations=measured,
                      reason='Actual supplied mixed/soak fixture and independent OS observations replayed')
    except Exception as error:
        report.update(status='fail', reason=str(error))
    report['finished_at'] = now()
    write_json(report_path, report)
    print(json.dumps({'status': report['status'], 'reason': report['reason'], 'report': str(report_path)}))
    return 0 if report['status'] == 'pass' else 1


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest='action', required=True)
    run = sub.add_parser('run', help='Execute one real supplied mixed or persistent-runtime soak workload')
    run.add_argument('--build-report', type=Path, required=True)
    run.add_argument('--fixture-producer', type=Path, required=True)
    run.add_argument('--artifact-dir', type=Path, required=True)
    run.add_argument('--supplied-host', type=Path, required=True)
    run.add_argument('--loader-dir', type=Path)
    run.add_argument('--loader-manifest', type=Path)
    run.add_argument('--expected-os', choices=('Linux', 'Windows'), required=True)
    run.add_argument('--expected-arch', default='x86_64')
    run.add_argument('--runner-label', default='local-full')
    run.add_argument('--controlled-probe', action='store_true')
    run.add_argument('--mode', choices=('mixed', 'soak'), required=True)
    run.add_argument('--purpose', choices=('smoke', 'full'), required=True)
    run.add_argument('--requested', type=int, required=True)
    run.add_argument('--timeout', type=int, required=True)
    run.add_argument('--sample-interval', type=float, default=1)
    run.add_argument('--report', type=Path, required=True)
    console = sub.add_parser('console-child', help=argparse.SUPPRESS)
    console.add_argument('--directory', type=Path, required=True)
    console.add_argument('--native-report', type=Path, required=True)
    console.add_argument('--timeout', type=int, required=True)
    console.add_argument('command', nargs=argparse.REMAINDER)
    verify = sub.add_parser('verify', help='Read-only Full join over four explicitly selected original native runs')
    verify.add_argument('--run-report', type=Path, action='append', required=True)
    verify.add_argument('--path-map', action='append', default=[], metavar='SOURCE=LOCAL_ROOT')
    verify.add_argument('--report', type=Path, required=True)
    args = parser.parse_args()
    if args.action == 'console-child':
        if args.command[:1] == ['--']:
            args.command = args.command[1:]
        return console_child(args)
    if args.action == 'verify':
        mappings = []
        for value in args.path_map:
            source, root = value.split('=', 1)
            mappings.append({'source': source, 'root': root})
        resolver = EvidencePaths({'path_mappings': mappings}, Path.cwd()).resolve
        guard_evidence_outputs([args.report], args.run_report, files=selected_files(args.run_report, resolver))
        report = {'schema_version': 1, 'report_kind': 'qaplet-m2-full', 'scope': 'full',
                  'started_at': now(), 'evaluator': identity(Path(__file__)), 'release_ready': False}
        try:
            report.update(aggregate_native_reports(args.run_report, resolver))
        except Exception as error:
            report.update(status='fail', reason=str(error), m2_formal_complete=False)
        report['finished_at'] = now()
        write_json(args.report, report)
        print(json.dumps({'status': report['status'], 'reason': report['reason'], 'report': str(args.report)}))
        return 0 if report['status'] == 'pass' else 1
    if args.requested <= 0 or args.timeout <= 0 or args.sample_interval <= 0:
        parser.error('Positive count/duration, timeout and sample interval required')
    if args.purpose == 'full' and args.requested < (10000 if args.mode == 'mixed' else 3600):
        parser.error('Full requires mixed>=10000 or soak>=3600 seconds')
    return run_supplied(args)


if __name__ == '__main__':
    sys.exit(main())
