#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Small observations from retained provider JSON; no network or CI mutations."""
import hashlib
import json
from pathlib import Path
import re
import sys

from workflow_support import git_read, identity, read_json


def require(value, reason):
    if not value:
        raise ValueError(reason)


def api_value(value):
    """Normalize the raw REST and connector envelopes used by this repository."""
    for _ in range(6):
        if isinstance(value, dict) and isinstance(value.get('structuredContent'), dict):
            value = value['structuredContent']
        elif isinstance(value, dict) and isinstance(value.get('content'), str):
            value = json.loads(value['content'])
        elif isinstance(value, dict) and isinstance(value.get('content'), list):
            require(all(isinstance(item, dict) for item in value['content']), 'Structured text content required')
            texts = [item['text'] for item in value['content'] if item.get('type') == 'text']
            require(len(texts) == 1, 'One structured API response required')
            value = json.loads(texts[0])
        else:
            return value
    raise ValueError('API envelope nesting exceeds the finite limit')


class Inputs:
    def __init__(self):
        self.sources = []
        self.cache = {}
        self.stdin_data = None

    def read(self, path):
        key = str(path)
        if key not in self.cache:
            maximum = 16 * 1024**2
            if key != '-':
                require(Path(path).stat().st_size <= maximum, 'API metadata exceeds 16 MiB')
            data = sys.stdin.buffer.read(maximum + 1) if key == '-' else Path(path).read_bytes()
            require(len(data) <= maximum, 'API metadata exceeds 16 MiB')
            if key == '-':
                self.stdin_data = data
            self.sources.append({'path': 'stdin' if key == '-' else str(Path(path).resolve()),
                                 'sha256': hashlib.sha256(data).hexdigest(), 'size_bytes': len(data)})
            self.cache[key] = api_value(json.loads(data))
        return self.cache[key]

    def retain_stdin(self, report):
        if self.stdin_data is None:
            return
        target = report.with_name(report.stem + '-stdin.json')
        target.parent.mkdir(parents=True, exist_ok=True)
        with target.open('xb') as stream:
            stream.write(self.stdin_data)
        next(row for row in self.sources if row['path'] == 'stdin').update(path=str(target.resolve()), source_kind='stdin')


def guard(args):
    target = args.report.resolve()
    require(not target.exists() and target.is_relative_to(args.repo.resolve() / 'out'),
            'Use a NEW observation report under repository out/')
    names = ('input_response', 'manifest', 'pull_response', 'main_response', 'previous_report')
    inputs = [getattr(args, key, None) for key in names] + list(getattr(args, 'jobs_response', []))
    require(sum(str(path) == '-' for path in inputs if path is not None) <= 1, 'Only one input may use stdin')
    require(all(str(path) == '-' or path.resolve() != target for path in inputs if path is not None),
            'Observation output collides with original input')
    if any(str(path) == '-' for path in inputs if path is not None):
        retained = target.with_name(target.stem + '-stdin.json')
        require(not retained.exists() and all(str(path) == '-' or path.resolve() != retained for path in inputs if path is not None),
                'Retained stdin would collide with existing evidence')


def selected_runs(value, commit, event=None, branch=None):
    require(re.fullmatch(r'[0-9a-f]{40}', commit), 'Exact full commit SHA required')
    rows = value.get('workflow_runs') if isinstance(value, dict) else value
    require(isinstance(rows, list), 'Retained workflow_runs API response required')
    if isinstance(value, dict) and 'total_count' in value:
        require(value['total_count'] == len(rows), 'Complete filtered run response required; retain all pages first')
    seen, result = set(), []
    for row in rows:
        if row.get('head_sha') != commit or event and row.get('event') != event or branch and row.get('head_branch') != branch:
            continue
        require(type(row.get('id')) is int and row['id'] > 0 and row['id'] not in seen, 'Duplicate/invalid run identity')
        seen.add(row['id'])
        result.append(row)
    require(result, 'No runs match the exact commit/event/branch')
    return result


def expected_workflows(repo, commit):
    paths = git_read(repo, 'ls-tree', '-r', '--name-only', commit, '--', '.github/workflows').decode().splitlines()
    names = []
    for path in paths:
        if not path.endswith(('.yml', '.yaml')):
            continue
        value = json.loads(git_read(repo, 'show', commit + ':' + path))
        if 'push' in value.get('on', {}):
            names.append(value['name'])
    require(names and len(names) == len(set(names)), 'Unique committed push workflow names required')
    return sorted(names)


def status(args, inputs):
    rows = selected_runs(inputs.read(args.input_response), args.commit, args.event, args.branch)
    expected = args.workflow or expected_workflows(args.repo, args.commit)
    missing = sorted(set(expected) - {row['name'] for row in rows})
    known = {row['id'] for row in rows}
    jobs, seen = [], set()
    for path in args.jobs_response:
        for job in inputs.read(path)['jobs']:
            require(job.get('run_id') in known and job['id'] not in seen, 'Job belongs to another run or is duplicated')
            seen.add(job['id'])
            jobs.append({'id': job['id'], 'run_id': job['run_id'], 'name': job['name'], 'status': job['status'],
                         'conclusion': job.get('conclusion'), 'active_steps': [step['name'] for step in job.get('steps', []) if step.get('status') == 'in_progress']})
    projected = [{key: row.get(key) for key in ('id', 'name', 'head_sha', 'head_branch', 'event', 'run_attempt', 'status', 'conclusion', 'html_url')} for row in rows]
    previous = read_json(args.previous_report) if args.previous_report else {}
    require(not previous or previous.get('commit') == args.commit, 'Previous observation must bind the same commit')
    old = {('job', row['id']): row for row in previous.get('jobs', [])}
    old.update({('run', row['id']): row for row in previous.get('runs', [])})
    changes = [{'kind': kind, **row} for kind, values in (('run', projected), ('job', jobs))
               for row in values if old.get((kind, row['id'])) != row]
    return {'status': 'pass', 'scope': 'ci-status-observation', 'commit': args.commit, 'evidence_verified': False,
            'runs': projected, 'jobs': jobs, 'missing_workflows': missing,
            'all_success': not missing and all(row['status'] == 'completed' and row.get('conclusion') == 'success' for row in rows) and
                           all(job['status'] == 'completed' and job['conclusion'] in ('success', 'skipped') for job in jobs),
            'pending_run_ids': [row['id'] for row in rows if row['status'] != 'completed'],
            'changes': changes, 'changed_count': len(changes), 'sources': inputs.sources, 'network_requests': 0}


def select_artifacts(args, inputs):
    require(re.fullmatch(r'[0-9a-f]{40}', args.commit), 'Exact full commit SHA required')
    require(args.run_id > 0 and all(number > 0 for number in args.artifact_id), 'Positive provider run/artifact IDs required')
    value = inputs.read(args.manifest)
    rows = value['artifacts']
    if 'total_count' in value:
        require(value['total_count'] == len(rows), 'Retain the complete artifact catalog before selection')
    names, ids = set(args.artifact_name), set(args.artifact_id)
    require(len(names) == len(args.artifact_name), 'Duplicate exact artifact name')
    require(all(any(row['id'] == number and row['name'] in names for row in rows) for number in ids),
            'Explicit artifact ID/name differs from catalog')
    selected = []
    for name in args.artifact_name:
        candidates = [row for row in rows if row['name'] == name]
        explicit = [row for row in candidates if row['id'] in ids]
        if explicit:
            candidates = explicit
        require(len(candidates) == 1, 'Exact artifact name missing or ambiguous; select a provider ID: ' + name)
        row = candidates[0]
        require(type(row.get('id')) is int and row['id'] > 0, 'Positive integer artifact identity required')
        require(re.fullmatch(r'[A-Za-z0-9][A-Za-z0-9._-]{0,199}', name), 'Unsafe exact artifact name')
        require(row.get('expired') is False and row['workflow_run']['head_sha'] == args.commit and
                str(row['workflow_run']['id']) == str(args.run_id), 'Artifact run/commit/expiry differs')
        require(re.fullmatch(r'sha256:[0-9a-f]{64}', row.get('digest', '')) and
                type(row.get('size_in_bytes')) is int and 0 < row['size_in_bytes'] <= 2 * 1024**3,
                'Provider digest and bounded archive length required')
        selected.append({key: row[key] for key in ('id', 'name', 'expired', 'digest', 'size_in_bytes', 'created_at', 'updated_at', 'workflow_run') if key in row})
    return {'status': 'pass', 'scope': 'ci-artifact-selection', 'commit': args.commit, 'run_id': args.run_id,
            'artifacts': selected, 'sources': inputs.sources, 'evidence_verified': False, 'network_requests': 0}


def finalize(args, inputs):
    pull, branch = inputs.read(args.pull_response), inputs.read(args.main_response)
    head = pull.get('head', {}).get('sha') or pull.get('head_sha')
    require(pull.get('merged') is True and pull.get('state') == 'closed' and head == args.reviewed_head and
            pull.get('merge_commit_sha') == args.commit, 'Merged PR identity differs from reviewed head/main')
    actual = branch.get('object', {}).get('sha') or branch.get('commit', {}).get('sha')
    require(actual == args.commit and (branch.get('ref') == 'refs/heads/' + args.branch or branch.get('name') == args.branch),
            'Fresh main ref response must bind the merge commit')
    require(re.fullmatch(r'[0-9a-f]{40}', args.reviewed_head), 'Exact reviewed head required')
    require(git_read(args.repo, 'rev-parse', args.commit + '^{tree}') == git_read(args.repo, 'rev-parse', args.reviewed_head + '^{tree}'),
            'Merged Git tree differs from reviewed head')
    parents = git_read(args.repo, 'show', '-s', '--format=%P', args.commit).decode().split()
    require(args.reviewed_head in parents and len(parents) == 2, 'Expected a two-parent reviewed PR merge')
    rows = selected_runs(inputs.read(args.input_response), args.commit, 'push', args.branch)
    expected = expected_workflows(args.repo, args.commit)
    require(sorted(row['name'] for row in rows) == expected and all(row['status'] == 'completed' and row.get('conclusion') == 'success' for row in rows),
            'Every committed main push workflow must complete successfully, once')
    known = {row['id'] for row in rows}
    covered, jobs, seen = set(), [], set()
    for path in args.jobs_response:
        value = inputs.read(path)
        require(isinstance(value.get('jobs'), list) and value['jobs'] and value.get('total_count', len(value['jobs'])) == len(value['jobs']),
                'Complete nonempty job response required for each workflow')
        for job in value['jobs']:
            require(job.get('run_id') in known and job['id'] not in seen, 'Duplicate job or wrong workflow run')
            require(job['status'] == 'completed' and job.get('conclusion') in ('success', 'skipped'), 'Failed/pending/cancelled job cannot finalize main')
            seen.add(job['id']); covered.add(job['run_id'])
            jobs.append({key: job.get(key) for key in ('id', 'run_id', 'name', 'status', 'conclusion')})
    require(covered == known, 'Job observations missing for selected main workflow')
    require(any(job['name'] == 'build' and job['conclusion'] == 'success' for job in jobs), 'Current required product/scope gate must succeed')
    return {'status': 'pass', 'scope': 'post-merge-ci-completion', 'main_fixed': True, 'commit': args.commit,
            'reviewed_head': args.reviewed_head, 'main_ci': [{key: row.get(key) for key in ('id', 'name', 'run_attempt', 'status', 'conclusion', 'html_url')} for row in rows],
            'jobs': jobs, 'sources': inputs.sources, 'evidence_verified': False,
            'formal_m2_complete': False, 'release_ready': False, 'network_requests': 0}


def add_parsers(commands, root):
    for name in ('status', 'select-artifacts', 'finalize'):
        child = commands.add_parser(name, help='Read retained API JSON; stdin is allowed; no network/CI mutation')
        child.add_argument('--repo', type=Path, default=root)
        child.add_argument('--commit', required=True)
        child.add_argument('--report', type=Path, required=True)
        if name == 'select-artifacts':
            child.add_argument('--manifest', type=Path, required=True)
            child.add_argument('--run-id', type=int, required=True)
            child.add_argument('--artifact-name', action='append', required=True)
            child.add_argument('--artifact-id', type=int, action='append', default=[])
        else:
            child.add_argument('--input-response', type=Path, required=True)
            child.add_argument('--jobs-response', type=Path, action='append', default=[])
            child.add_argument('--branch', default='main' if name == 'finalize' else None)
            if name == 'status':
                child.add_argument('--event', choices=('push', 'pull_request', 'workflow_dispatch', 'schedule'))
                child.add_argument('--workflow', action='append', default=[])
                child.add_argument('--previous-report', type=Path)
            else:
                child.add_argument('--pull-response', type=Path, required=True)
                child.add_argument('--main-response', type=Path, required=True)
                child.add_argument('--reviewed-head', required=True)


def execute(args):
    inputs = Inputs()
    try:
        return {'status': status, 'select-artifacts': select_artifacts, 'finalize': finalize}[args.operation](args, inputs)
    finally:
        inputs.retain_stdin(args.report)
