#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Frozen source and process-local temporary roots for development execution."""
import hashlib
import os
from pathlib import Path
import shutil
import stat
import subprocess
import sys
import tempfile

from foundation import command
from workflow_support import git_read, write_json


def temporary_root(path, minimum):
    if type(minimum) is not int or minimum < 0:
        raise ValueError('Temporary free-space minimum must be nonnegative')
    root = Path(path).resolve()
    root.mkdir(parents=True, exist_ok=True)
    free = shutil.disk_usage(root).free
    if free < minimum:
        raise ValueError(f'Temporary root lacks free space: {root}; available={free}, required={minimum}')
    with tempfile.NamedTemporaryFile(dir=root, prefix='qaplet-preflight-', delete=True) as probe:
        probe.write(b'qaplet temporary root\n')
        probe.flush()
    return {'path': str(root), 'free_bytes': free, 'minimum_free_bytes': minimum, 'writable': True,
            'measurement': 'Actual selected filesystem; backing-volume capacity is not inferred'}


def child_environment(root):
    return dict(os.environ, TMPDIR=str(root), TEMP=str(root), TMP=str(root))


def record_command(report, output, label, argv, timeout, cwd, environment):
    record, _, _ = command(list(map(str, argv)), output, label, timeout, cwd=cwd, env=environment)
    report['commands'].append(record)
    if record['exit_code'] != 0 or record['timed_out']:
        raise ValueError('Development execution failed; see captured ' + label + ' logs')


def inventory(repo):
    names = git_read(repo, 'ls-files', '--cached', '--others', '--exclude-standard', '-z').decode().split('\0')
    rows = []
    for name in sorted(set(filter(None, names))):
        path = repo / name
        if path.is_symlink():
            raise ValueError('Source snapshots require regular source files: ' + name)
        if not path.is_file() or not path.resolve().is_relative_to(repo.resolve()):
            raise ValueError('Source snapshot input missing or outside checkout: ' + name)
        rows.append({'file': name, 'sha256': hashlib.sha256(path.read_bytes()).hexdigest(),
                     'size_bytes': path.stat().st_size})
    return rows


def verify_inventory(repo, rows):
    if inventory(repo) != rows:
        raise ValueError('Frozen source snapshot changed during execution')


def sdk_command(source, cache, output):
    return [sys.executable, '-B', str(source / 'scripts/sdk.py'), 'build',
            '--source-cache', str(cache / 'quickjs'), '--toolchain-cache', str(cache / 'toolchains/cosmocc'),
            '--helper-source-cache', str(cache / 'sources/ape-loader'), '--build-dir', str(cache / 'build/sdk'),
            '--output-dir', str(output / 'artifacts'), '--report', str(output / 'sdk-build.json')]


def frozen_sdk(args, context, output, report, environment):
    if not args.source_ref:
        raise ValueError('build-sdk requires --source-ref; commit or explicitly select the intended immutable source')
    repo, cache = Path(context['checkout']), Path(context['cache_root'])
    commit = git_read(repo, 'rev-parse', '--verify', '--end-of-options', args.source_ref + '^{commit}').decode().strip()
    source = output / 'source'
    report['source_snapshot'] = {'commit': commit, 'source_checkout': str(repo), 'path': str(source),
                                 'scope': 'Explicit committed source; uncommitted work is outside this build'}
    report['planned_commands'] = [sdk_command(source, cache, output)]
    if args.plan:
        return
    source.mkdir()
    for label, argv in (
        ('snapshot-init', ['git', '-c', 'core.autocrlf=false', 'init', '-q', str(source)]),
        ('snapshot-fetch', ['git', '-C', str(source), 'fetch', '--no-tags', '--depth=1', str(repo), commit]),
        ('snapshot-checkout', ['git', '-c', 'core.autocrlf=false', '-C', str(source), 'checkout', '--detach', commit]),
    ):
        record_command(report, output, label, argv, args.timeout, repo, environment)
    rows = inventory(source)
    report['source_snapshot']['files'] = rows
    write_json(output / 'source-snapshot.json', report['source_snapshot'])
    record_command(report, output, 'sdk-build', sdk_command(source, cache, output), args.timeout, source, environment)
    verify_inventory(source, rows)
    # Successful compiler exit alone is not a successful SDK producer.
    import json
    producer = json.loads((output / 'sdk-build.json').read_text(encoding='utf-8'))
    if producer.get('status') != 'pass' or producer.get('sdk_recipe', {}).get('git_commit') != commit:
        raise ValueError('Frozen SDK producer did not prove the selected source')


def native_check(args, context, output, report, environment):
    repo, cache = Path(context['checkout']), Path(context['cache_root'])
    report['planned_commands'] = [['make', 'check', 'OUT=<native snapshot>/out/check']]
    if args.plan:
        return
    with tempfile.TemporaryDirectory(prefix='qaplet-native-check-', dir=environment['TMPDIR']) as temporary:
        source = Path(temporary)
        probe = source / 'permission-probe'
        probe.write_bytes(b'x')
        probe.chmod(stat.S_IRUSR)
        if probe.stat().st_mode & stat.S_IWUSR:
            raise ValueError('Native filesystem check requires POSIX permission semantics')
        probe.unlink()
        rows = inventory(repo)
        for row in rows:
            target = source / row['file']
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(repo / row['file'], target)
            if hashlib.sha256(target.read_bytes()).hexdigest() != row['sha256']:
                raise ValueError('Working source changed while capturing snapshot: ' + row['file'])
        report['source_snapshot'] = {'scope': 'Exact working bytes for one-use checks; not an SDK producer',
                                     'files': rows, 'original_checkout': str(repo), 'temporary_filesystem': str(source)}
        write_json(output / 'source-snapshot.json', report['source_snapshot'])
        for label, argv in (
            ('native-init', ['git', 'init', '-q']),
            ('native-add', ['git', '-c', 'core.autocrlf=false', 'add', '.']),
            ('native-commit', ['git', '-c', 'core.autocrlf=false', '-c', 'user.name=Qaplet checks',
                               '-c', 'user.email=checks@localhost', 'commit', '-qm', 'One-use source byte snapshot']),
        ):
            record_command(report, output, label, argv, args.timeout, source, environment)
        argv = ['make', 'check', 'OUT=' + str(source / 'out/check'), 'QUICKJS_CACHE=' + str(cache / 'quickjs'),
                'TOOLCHAIN_CACHE=' + str(cache / 'toolchains/cosmocc'), 'APE_BUILD_DIR=' + str(cache / 'build'),
                'APE_HELPER_SOURCE_CACHE=' + str(cache / 'sources/ape-loader'), 'OFFLINE_CACHE_ROOT=' + str(cache)]
        try:
            record_command(report, output, 'native-check', argv, args.timeout, source, environment)
        finally:
            if (source / 'out/check').is_dir():
                shutil.copytree(source / 'out/check', output / 'check', dirs_exist_ok=True)
