#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Run selected current SDK contracts locally; never fetch inputs or sign a release."""
import argparse
from contextlib import contextmanager
from datetime import datetime
import json
import os
from pathlib import Path
import platform
import shlex
import shutil
import struct
from types import SimpleNamespace
import sys
import uuid
import zipfile

import sdk
from input_lock import InputLock
from workflow_support import (guard_evidence_outputs, identity, now, read_json,
                              git_read, workspace, write_json)

ROOT = Path(__file__).resolve().parents[1]
PROFILES = ('engine', 'loop', 'work', 'process', 'tty', 'all', 'ci')
CORE = {'facts', 'abi', 'lifetime', 'promises', 'modules', 'module-async',
        'resources', 'close-states'}
ENGINE = {'scalar', 'state', 'owner', 'creation', 'integers', 'strings', 'bytes',
          'diagnostics', 'value-limits', 'promise-values', 'module-limits',
          'module-pending', 'module-budgets', 'module-nul', 'module-cache',
          'module-policy', 'budgets', 'resource-refusals'}
CLI_CORE = {'help', 'eval-number', 'eval-promise', 'eval-rejection',
            'eval-unsettled', 'eval-unknown-builtin', 'run-argv',
            'run-cache-namespace', 'run-rejection', 'eval-dispatch-budget',
            'eval-deadline'}
EXAMPLES = {'abi', 'application', 'rejection'}
CI_PATTERNS = ('test_ci*.py', 'test_workflow_tools.py', 'test_local_check.py')


def select_cases(profile):
    if profile not in PROFILES:
        raise ValueError('Unknown local profile')
    if profile == 'ci':
        return (), (), ()
    groups = ('engine', 'loop', 'work', 'process') if profile == 'all' else (profile,)
    if profile == 'all' and any(row[0].startswith(('m2-tty', 'm2-stdio')) for row in sdk.HOST_CASES):
        groups += ('tty',)
    names = set(CORE)
    for group in groups:
        if group == 'engine':
            names.update(ENGINE)
        prefixes = {'loop': ('m2-timers', 'm2-wait', 'm2-notify', 'events'),
                    'work': ('m2-work',), 'process': ('m2-process',),
                    'tty': ('m2-tty', 'm2-stdio')}.get(group, ())
        matches = {row[0] for row in sdk.HOST_CASES if row[0].startswith(prefixes)} if prefixes else set()
        if group == 'tty' and not matches:
            raise sdk.BuildError('Product TTY/stdio is not implemented; OS probes are not a Runtime test', 'blocked')
        names.update(matches)
        if group == 'loop':
            names.update({'m2-runtime', 'm2-drive', 'm2-configuration', 'm2-owner', 'm2-close'})
    available = {row[0] for row in sdk.HOST_CASES}
    if not names <= available:
        raise ValueError('Required SDK catalog cases missing: ' + ', '.join(sorted(names - available)))
    cli_names = CLI_CORE | ({'run-timer-tla', 'eval-interval-entry', 'eval-immediate-result'}
                            if profile in ('loop', 'all') else set())
    if not cli_names <= {row[0] for row in sdk.CLI_CASES}:
        raise ValueError('Required CLI catalog cases missing')
    return (tuple(row for row in sdk.HOST_CASES if row[0] in names),
            tuple(row for row in sdk.CLI_CASES if row[0] in cli_names),
            tuple(row for row in sdk.EXAMPLE_CASES if row[0] in EXAMPLES))


def current_recipe():
    return sdk.recipe_identity(sdk.recipe(), InputLock().identity()['input_id'])


def contract_identity(profile):
    catalog = select_cases(profile)
    paths = ('scripts/local_check.py', 'scripts/sdk.py', 'scripts/sdk_matrix.py',
             'scripts/startup.py', 'scripts/build_ape.py', 'scripts/foundation.py',
             'scripts/workflow_support.py', 'scripts/input_lock.py')
    if profile == 'ci':
        paths += ('scripts/ci_tiers.py', 'scripts/dev.py', 'tests/contracts/test_local_check.py',
                  'tests/contracts/test_ci_tiers.py', 'tests/contracts/test_ci_efficiency.py')
    value = {'profile': profile, 'host_cases': catalog[0], 'cli_cases': catalog[1], 'example_cases': catalog[2],
             'helpers': [{'file': name, **{key: identity(ROOT / name)[key]
                         for key in ('sha256', 'size_bytes')}} for name in paths]}
    return {'sha256': sdk.digest(value), **value}


def validate_supplier(path, artifacts, expected):
    producer = read_json(path)
    if (producer.get('report_kind'), producer.get('scope'), producer.get('status')) != ('qaplet-sdk', 'build', 'pass'):
        raise ValueError('Successful existing SDK producer required')
    recipe = {k: v for k, v in producer['sdk_recipe'].items()
              if k not in ('git_commit', 'working_tree_dirty')}
    if recipe != expected:
        raise ValueError('Supplier recipe/source differs from current checkout; no old SDK reuse')
    if producer.get('input_lock') != InputLock().identity():
        raise ValueError('Supplier unified input identity differs from current pinned lock')
    delivered = producer.get('distribution', [])
    names = [item['file'] for item in delivered]
    if not names or len(names) != len(set(names)):
        raise ValueError('Complete unique SDK distribution required')
    for item in delivered:
        name = Path(item['file'])
        if name.is_absolute() or '..' in name.parts or '\\' in item['file'] or ':' in item['file']:
            raise ValueError('Unsafe supplied distribution path')
        target = artifacts / name
        if target.is_symlink() or not target.resolve().is_relative_to(artifacts.resolve()):
            raise ValueError('Supplied distribution escapes physical root')
        observed = identity(target)
        if any(observed[key] != item[key] for key in ('sha256', 'size_bytes')):
            raise ValueError('Supplied distribution bytes differ: ' + item['file'])
    actual_names = {p.relative_to(artifacts).as_posix() for p in artifacts.rglob('*') if p.is_file()}
    if actual_names != set(names):
        raise ValueError('SDK distribution contains missing or unrecorded files')
    sdk.check_package(producer, artifacts)
    return producer


@contextmanager
def selected_catalog(catalog):
    previous = sdk.HOST_CASES, sdk.CLI_CASES, sdk.EXAMPLE_CASES
    sdk.HOST_CASES, sdk.CLI_CASES, sdk.EXAMPLE_CASES = catalog
    try:
        yield
    finally:
        sdk.HOST_CASES, sdk.CLI_CASES, sdk.EXAMPLE_CASES = previous


def sdk_report(scope):
    return {'schema_version': 1, 'report_kind': 'qaplet-local-sdk', 'scope': scope,
            'report_id': uuid.uuid4().hex, 'status': 'not_run', 'reason': '',
            'started_at': now(), 'host': sdk.host_facts(), 'ci': sdk.ci_facts(),
            'commands': [], 'cases': [], 'artifacts': [], 'input_reports': {},
            'controlled_probe': False}


def sdk_arguments(cache, output, directory, args):
    return SimpleNamespace(source_cache=cache / 'quickjs', toolchain_cache=cache / 'toolchains/cosmocc',
        helper_source_cache=cache / 'sources/ape-loader', build_dir=directory / 'objects',
        output_dir=output, timeout=args.timeout, compile_timeout=args.compile_timeout,
        cflag=[], controlled_probe=False, expected_os=None, expected_arch=None,
        runner_label='local-check', loader_dir=args.loader_dir, loader_manifest=None,
        build_report=None, artifact=None, cli_artifact=None)


def dependency_paths(path):
    text = path.read_text(encoding='utf-8').replace('\\\n', '')
    target, separator, dependencies = text.partition(':')
    if not separator or target.strip() != 'qaplet-public-header':
        raise ValueError('Unexpected C dependency format')
    result = sorted({Path(name).resolve() for name in shlex.split(dependencies)})
    if not result:
        raise ValueError('C compiler did not identify actual input headers')
    return result


def input_snapshot(paths):
    return [identity(path) for path in sorted(set(paths))]


def verify_object(path, architecture):
    data = path.read_bytes()
    machine = {'x86_64': 62, 'aarch64': 183}.get(architecture)
    if (len(data) < 64 or data[:6] != b'\x7fELF\x02\x01' or
            struct.unpack_from('<HH', data, 16) != (1, machine)):
        raise ValueError('Public C compile must produce a native ELF64 REL object')
    return {**identity(path), 'elf_class': 64, 'elf_type': 'REL', 'machine': machine}


def public_header_compile(args, artifacts, directory):
    """Ubuntu ordinary C caller only; no Runtime compile, SDK link or APE build."""
    report = sdk_report('public-header-compile')
    report['report_kind'] = 'qaplet-local-c-compile'
    report['scope_notes'] = 'Ordinary Ubuntu public C header smoke; not a portable SDK/package/link contract'
    directory.mkdir()
    compiler = shutil.which('cc')
    if compiler is None:
        raise sdk.BuildError('Ubuntu PATH cc is required for the public C header smoke', 'blocked')
    compiler = Path(compiler).resolve()
    # Query actual compiler subprocesses, so a checked driver cannot conceal a
    # changed cc1/assembler. Unsupported host compilers fail explicitly.
    tools = [compiler]
    for name in ('cc1', 'as'):
        _, output, _ = sdk.run(report, directory, 'compiler-' + name,
            [compiler, '-print-prog-name=' + name], args.timeout)
        resolved = shutil.which(output.strip())
        if not resolved:
            raise sdk.BuildError('Cannot identify actual ordinary C compiler backend: ' + name, 'blocked')
        tools.append(Path(resolved).resolve())
    source = artifacts / 'sdk/embed.c'
    header = artifacts / 'sdk/include/qaplet.h'
    before_tools = input_snapshot(tools)
    sdk.run(report, directory, 'compiler-version', [compiler, '--version'], args.timeout)
    flags = ['-std=c11', '-Wall', '-Werror', '-I' + str(header.parent)]
    before_deps, after_deps = directory / 'before.d', directory / 'object.d'
    sdk.run(report, directory, 'public-c-dependencies',
        [compiler, *flags, '-M', '-MT', 'qaplet-public-header', '-MF', before_deps, source], args.compile_timeout)
    closure = dependency_paths(before_deps)
    if source.resolve() not in closure or header.resolve() not in closure:
        raise ValueError('Public C dependency closure omits supplied source/header')
    before = input_snapshot([*closure, *tools])
    if input_snapshot(tools) != before_tools:
        raise ValueError('Ordinary C compiler changed while discovering input closure')
    output = directory / 'public-header.o'
    sdk.run(report, directory, 'public-c-compile', [compiler, *flags, '-MD', '-MT',
        'qaplet-public-header', '-MF', after_deps, '-c', source, '-o', output], args.compile_timeout)
    after = input_snapshot([*closure, *tools])
    if dependency_paths(after_deps) != closure or after != before:
        raise ValueError('Public C source/header/compiler input bytes changed during compilation')
    report['inputs_before'], report['inputs_after'] = before, after
    report['compiler'] = before_tools
    report['dependency_files'] = [identity(before_deps), identity(after_deps)]
    report['object'] = verify_object(output, report['host']['execution_arch'])
    report.update(status='pass', reason='Actual compile-only public C caller; all actual input bytes stable', finished_at=now())
    return report


def supplier(args, report, cache, expected):
    if bool(args.supplier_report) != bool(args.supplier_dir):
        raise ValueError('--supplier-report and --supplier-dir must be supplied together')
    if args.supplier_report:
        path, artifacts, reused = args.supplier_report.resolve(), args.supplier_dir.resolve(), True
    else:
        if platform.system() != 'Linux' or platform.freedesktop_os_release().get('ID') != 'ubuntu':
            raise sdk.BuildError('Build current SDK on Ubuntu, or explicitly supply matching SDK bytes', 'blocked')
        target = cache / 'build/local-check' / expected['sha256']
        path, artifacts = target / 'build.json', target / 'artifacts'
        reused = target.exists()
        if not reused:
            target.mkdir(parents=True)
            build = sdk_report('build')
            build['report_kind'] = 'qaplet-sdk'
            directory = target / 'evidence' / build['report_id']
            directory.mkdir(parents=True)
            try:
                sdk.build(sdk_arguments(cache, artifacts, target, args), build, directory)
            except Exception as error:
                build.update(status='fail', reason=str(error))
                raise
            finally:
                build['finished_at'] = now()
                write_json(path, build)
        # A partial/bad existing cache is rejected, never silently overwritten or green.
    producer = validate_supplier(path, artifacts, expected)
    report['supplier'] = {'mode': 'verified-reuse' if reused else 'built-current',
        'producer_report': identity(path), 'producer_report_id': producer['report_id'],
        'artifacts_directory': str(artifacts),
        'producer_commit': producer['sdk_recipe']['git_commit'],
        'sdk_sha256': producer['sdk']['sha256'], 'recipe_sha256': expected['sha256'],
        'package': {key: producer['package'][key] for key in ('sha256', 'size_bytes')},
        'previous_host_results_reused_as_new_execution': False}
    return path, artifacts


def run_product(args, report, cache, catalog, expected, directory):
    path, artifacts = supplier(args, report, cache, expected)
    options = sdk_arguments(cache, args.output_dir.resolve() / 'public-c', directory / 'public-c', args)
    options.build_report, options.artifact = path, artifacts / 'qaplet-host.com'
    if options.loader_dir is None and os.name != 'nt':
        options.loader_dir = cache / 'toolchains/cosmocc' / InputLock().component('cosmocc')['archive']['sha256'] / 'tree/bin'
    guard_evidence_outputs([args.report, args.output_dir, directory], [path], [artifacts], [options.artifact])
    selected = sdk_report('selected-run')
    selected_directory = directory / 'selected'
    selected_directory.mkdir()
    try:
        with selected_catalog(catalog):
            sdk.consume(options, selected, selected_directory)
    except Exception as error:
        selected.update(status='fail', reason=str(error))
        report['execution_status'] = 'partial' if selected['commands'] else 'not_run'
        raise
    finally:
        selected['finished_at'] = now()
        write_json(args.output_dir / 'selected-sdk.json', selected)
    report['runtime'] = {'report': identity(args.output_dir / 'selected-sdk.json'),
        'status': selected['status'], 'cases': len(selected['cases']),
        'failures': [item['case_id'] for item in selected['cases'] if item['status'] != 'pass'],
        'lifecycle': selected['lifecycle'], 'host': selected['host'],
        'elapsed_seconds': (datetime.fromisoformat(selected['finished_at']) - datetime.fromisoformat(selected['started_at'])).total_seconds()}
    report['public_c'] = {'status': 'not_applicable', 'reason': 'Compilation is Ubuntu-only; supplied Runtime execution remains separate'}
    if platform.system() == 'Linux' and platform.freedesktop_os_release().get('ID') == 'ubuntu':
        compiled = public_header_compile(args, artifacts, directory / 'public-c')
        write_json(args.output_dir / 'public-c.json', compiled)
        report['public_c'] = {'status': compiled['status'], 'scope': 'ordinary-C-compile-only',
            'report': identity(args.output_dir / 'public-c.json'),
            'elapsed_seconds': (datetime.fromisoformat(compiled['finished_at']) - datetime.fromisoformat(compiled['started_at'])).total_seconds()}
    validate_supplier(path, artifacts, expected)
    if current_recipe() != expected:
        raise ValueError('Current source/input recipe changed while local checks ran')
    report['status'] = 'pass' if selected['status'] == 'pass' and report['public_c']['status'] in ('pass', 'not_applicable') else 'fail'
    report['execution_status'] = 'executed'


def run_ci(args, report, directory):
    from dev import preflight
    report['preflight'] = preflight(ROOT)
    report['commands'] = []
    for index, pattern in enumerate(CI_PATTERNS):
        result, _, _ = sdk.run(report, directory, 'ci-' + str(index),
            [sys.executable, '-B', '-m', 'unittest', 'discover', '-s', 'tests/contracts', '-p', pattern, '-v'],
            300, expected=None)
        if result['exit_code'] != 0 or result['timed_out']:
            raise ValueError('CI tooling contracts failed: ' + pattern)
    report.update(status='pass', execution_status='executed', product_execution=False)


def result_pointer(cache, report):
    key = sdk.digest({key: report[key] for key in ('commit', 'profile', 'current_recipe_sha256', 'contract_sha256')}
                     | {key: report['host'][key] for key in ('os', 'execution_arch')})
    return cache / 'build/local-check-results' / (key + '.json')


def try_reuse(args, report, cache):
    from ci_evidence import reuse, actions_reuse_candidates
    attempts = []
    options = SimpleNamespace(commit=report['commit'], profile=args.profile,
        expected_os=args.expected_os or report['host']['os'],
        expected_arch=args.expected_arch or report['host']['execution_arch'],
        max_age_hours=args.max_age_hours, supplier_dir=None)
    if args.supplier_report:
        try:
            validate_supplier(args.supplier_report, args.supplier_dir.resolve(), current_recipe())
        except (sdk.BuildError, OSError, ValueError, KeyError, TypeError, zipfile.BadZipFile) as error:
            return {'status': 'miss', 'attempts': [{'supplier': str(args.supplier_report), 'status': 'fail', 'reason': str(error)[:500]}],
                    'new_product_executions': 0, 'reason': 'Explicit supplied bytes must be valid before result reuse'}

    def verify(path, mappings=(), provider=None):
        options.input_report, options.path_mappings = path, mappings
        try:
            result = reuse(options)
            result['provider'] = provider
            if provider:
                result['network_requests'] = None  # Discovery/acquisition happened; not a zero-network claim.
            attempts.append({'input_report': str(path), 'status': 'pass'})
            result['attempts'] = attempts
            return result
        except (sdk.BuildError, OSError, ValueError, KeyError, TypeError, zipfile.BadZipFile) as error:
            attempts.append({'input_report': str(path), 'status': 'fail', 'reason': str(error)[:500]})
            return None

    candidates = list(args.reuse_report)
    pointer = result_pointer(cache, report)
    if pointer.exists():
        try:
            reference = read_json(pointer)['original_execution']
            if identity(reference['path']) != reference:
                raise ValueError('Indexed original report bytes changed')
            candidates.append(Path(reference['path']))
        except (OSError, ValueError, KeyError, TypeError) as error:
            attempts.append({'index': str(pointer), 'status': 'fail', 'reason': str(error)[:500]})
    for path in dict.fromkeys(candidates):
        result = verify(path)
        if result:
            return result
    if args.reuse_actions:
        args.commit = report['commit']
        args.reuse_attempts = attempts
        try:
            for path, mappings, provider in actions_reuse_candidates(args, args.output_dir / 'reuse-actions'):
                result = verify(path, mappings, provider)
                if result:
                    return result
        except (OSError, ValueError, KeyError, TypeError) as error:
            attempts.append({'provider': 'Actions', 'status': 'fail', 'reason': str(error)[:500]})
    return {'status': 'miss', 'attempts': attempts, 'new_product_executions': 0,
            'reason': 'No strictly verified matching successful original; execute the selected contract'}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--profile', choices=PROFILES, default='all')
    parser.add_argument('--output-dir', type=Path, default=ROOT / 'out/local-check')
    parser.add_argument('--report', type=Path, default=ROOT / 'out/local-check/check.json')
    parser.add_argument('--supplier-report', '--build-report', type=Path)
    parser.add_argument('--supplier-dir', type=Path)
    parser.add_argument('--loader-dir', type=Path)
    parser.add_argument('--plan', action='store_true')
    parser.add_argument('--reuse-report', type=Path, action='append', default=[])
    parser.add_argument('--reuse-only', action='store_true', help='Discover and verify only; a miss permits actual execution by the caller')
    parser.add_argument('--reuse-actions', action='store_true', help='Read same-SHA successful M0 Fast artifacts before execution')
    parser.add_argument('--reuse-repository')
    parser.add_argument('--reuse-api-base', default='https://api.github.com')
    parser.add_argument('--reuse-timeout-seconds', type=int, choices=range(5, 121), default=30)
    parser.add_argument('--max-age-hours', type=int, choices=range(1, 169), default=24)
    parser.add_argument('--expected-commit', help='Explicit original identity for read-only reuse-only verification')
    parser.add_argument('--expected-os', choices=('Linux', 'Windows', 'Darwin'))
    parser.add_argument('--expected-arch', choices=('x86_64', 'aarch64'))
    parser.add_argument('--github-output', type=Path)
    parser.add_argument('--timeout', type=int, default=30)
    parser.add_argument('--compile-timeout', type=int, default=300)
    args = parser.parse_args()
    if (args.expected_commit or args.expected_os or args.expected_arch) and not args.reuse_only:
        parser.error('Explicit original identities are only valid for read-only --reuse-only')
    if not 1 <= args.timeout <= 300 or not 1 <= args.compile_timeout <= 900:
        parser.error('Invalid bounded timeout')
    if bool(args.supplier_report) != bool(args.supplier_dir):
        parser.error('--supplier-report and --supplier-dir must be supplied together')
    args.output_dir, args.report = args.output_dir.resolve(), args.report.resolve()
    cache = Path(workspace(ROOT)['cache_root'])
    try:
        protected = [ROOT / name for name in ('src', 'include', 'scripts', 'tests', 'toolchains', 'third_party', '.git')]
        if args.output_dir == ROOT:
            raise ValueError('Output directory must not be the checkout root')
        cache_inputs = [cache / name for name in ('downloads', 'quickjs', 'toolchains', 'sources', 'build', 'guests', 'tools')]
        guard_evidence_outputs([args.output_dir, args.report, args.github_output], [args.supplier_report, *args.reuse_report],
            [*cache_inputs, *protected, *([args.supplier_dir] if args.supplier_dir else [])],
            [ROOT / 'Makefile', ROOT / 'AGENTS.md'])
        if args.report.exists() or (args.output_dir / 'selected-sdk.json').exists() or (args.output_dir / 'public-c.json').exists():
            raise ValueError('Use new report/output paths; existing local evidence is retained')
    except (OSError, ValueError) as error:
        parser.error(str(error))
    report = {'schema_version': 1, 'report_kind': 'qaplet-local-check', 'profile': args.profile,
        'scope': 'development-only', 'status': 'not_run', 'execution_status': 'not_run',
        'evidence_verified': False, 'cross_verified': False, 'full_verified': False,
        'release_ready': False, 'started_at': now(), 'supplier': None,
        'commit': git_read(ROOT, 'rev-parse', 'HEAD').decode().strip(), 'host': sdk.host_facts(), 'ci': sdk.ci_facts(),
        'contract_limits': 'Selected current SDK contracts only; no M0 probe, QEMU, network acquisition, native matrix or release gate.'}
    if args.expected_commit:
        report['commit'] = args.expected_commit
    args.output_dir.mkdir(parents=True, exist_ok=True)
    directory = args.output_dir / 'evidence' / uuid.uuid4().hex
    directory.mkdir(parents=True)
    try:
        catalog = select_cases(args.profile)
        report['test_contract'] = contract_identity(args.profile)
        report['contract_sha256'] = report['test_contract']['sha256']
        report['selection'] = {'host_cases': [row[0] for row in catalog[0]],
            'cli_cases': [row[0] for row in catalog[1]], 'example_cases': [row[0] for row in catalog[2]],
            'ci_patterns': list(CI_PATTERNS) if args.profile == 'ci' else []}
        expected = current_recipe()
        report['current_recipe_sha256'] = expected['sha256']
        if args.plan:
            report.update(status='planned', reason='Plan only; no product compilation or execution')
        elif args.profile == 'ci':
            run_ci(args, report, directory)
        else:
            report['reuse'] = try_reuse(args, report, cache)
            if report['reuse']['status'] == 'pass':
                report.update(status='pass', execution_status='reused_previous')
            elif args.reuse_only:
                report.update(status='pass', reason='Reuse miss; selected contract still requires actual execution')
            else:
                run_product(args, report, cache, catalog, expected, directory)
        if contract_identity(args.profile) != report['test_contract']:
            raise ValueError('Selected test contract/helpers changed during local checks')
        if current_recipe() != expected:
            raise ValueError('Current product recipe changed during local checks')
    except (sdk.BuildError, OSError, ValueError, KeyError, TypeError, zipfile.BadZipFile) as error:
        report.update(status=error.status if isinstance(error, sdk.BuildError) else 'fail', reason=str(error))
    report['finished_at'] = now()
    write_json(args.report, report)
    if report['status'] == 'pass' and report['execution_status'] == 'executed' and args.profile not in ('ci', 'tty'):
        pointer = result_pointer(cache, report)
        pointer.parent.mkdir(parents=True, exist_ok=True)
        temporary = pointer.with_name('.' + uuid.uuid4().hex + '.tmp')
        try:
            write_json(temporary, {'original_execution': identity(args.report)})
            temporary.replace(pointer)
        finally:
            temporary.unlink(missing_ok=True)
    if args.github_output:
        with args.github_output.open('a', encoding='utf-8', newline='\n') as output:
            output.write('reused=' + str(report['execution_status'] == 'reused_previous').lower() + '\n')
    print(json.dumps({'status': report['status'], 'profile': args.profile,
                      'execution_status': report['execution_status'], 'report': str(args.report)}))
    return 0 if report['status'] in ('pass', 'planned') else 1


if __name__ == '__main__':
    sys.exit(main())
