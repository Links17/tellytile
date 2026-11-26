"""Stack-neutral adapter protocol, subprocess execution and result integrity."""
from __future__ import annotations

import argparse
from contextlib import nullcontext
import importlib.util
import json
from pathlib import Path
import re
import shutil
import subprocess
import sys
import time

from io_utils import ContractError, OperationUnavailable, OPERATIONS, STATUSES, digest, read_json, require, result, safe_path


def selected(target, example=None):
    names = [example] if example else target['examples']
    require(all(name in target['examples'] for name in names), 'unknown example')
    return names


def command(root, name, argv, toolchain=None):
    if not shutil.which(argv[0]):
        return result(name, 'not_run', f'Missing executable: {argv[0]}', command=argv)
    log = root / '.codecraft/logs' / (re.sub(r'[^a-zA-Z0-9_-]', '_', name) + '.log')
    log.parent.mkdir(parents=True, exist_ok=True)
    start = time.monotonic()
    try:
        with log.open('w') as stream:
            run = subprocess.run(argv, cwd=root, stdout=stream, stderr=subprocess.STDOUT, timeout=1800)
        return result(name, 'passed' if run.returncode == 0 else 'failed', '' if run.returncode == 0 else f'Command exited {run.returncode}', command=argv, exit_code=run.returncode, elapsed_seconds=round(time.monotonic()-start, 2), log=str(log.relative_to(root)), log_sha256=digest(log), toolchain=toolchain)
    except subprocess.TimeoutExpired:
        return result(name, 'failed', 'Command timed out after 1800 seconds', command=argv, log=str(log.relative_to(root)), log_sha256=digest(log))


def load_adapter(root, platform):
    path = safe_path(root, platform['adapter'])
    spec = importlib.util.spec_from_file_location('native_' + platform['id'].replace('-', '_'), path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    for name in ('build', 'cases', 'probe', 'validate_target'):
        require(callable(getattr(module, name, None)), f'{platform["id"]}: missing adapter function {name}')
    for operation, name in [('plan', 'firmware_layout'), ('merge', 'firmware_merge'), ('verify', 'firmware_verify')]:
        if platform['operations']['firmware-'+operation]['supported']:
            require(callable(getattr(module, name, None)), f'{platform["id"]}: missing adapter function {name}')
    return module


def exit_code(results):
    return 1 if any(item['status'] == 'failed' for item in results) else (0 if results and all(item['status'] == 'passed' for item in results) else 2)


def envelope(target, operation, source_digest, results):
    return {'schema_version': 2, 'target': target, 'operation': operation, 'source_digest': source_digest, 'ok': exit_code(results) == 0, 'results': results}


def check_envelope(payload, tid, operation, source_digest, expected, returncode):
    require(payload.get('schema_version') == 2, 'unsupported adapter protocol')
    require(payload.get('target') == tid and payload.get('operation') == operation and payload.get('source_digest') == source_digest, 'adapter identity/operation/source mismatch')
    checks = payload.get('results')
    require(isinstance(checks, list) and checks, 'empty adapter results')
    names = [item.get('name') for item in checks]
    require(len(set(names)) == len(names) and set(names) == set(expected), 'adapter result coverage mismatch')
    for item in checks:
        require(item.get('status') in STATUSES, 'invalid adapter status')
        require(item['status'] == 'passed' or item.get('reason'), 'non-passing result requires reason')
    require(payload.get('ok') == (exit_code(checks) == 0) and returncode == exit_code(checks), 'adapter exit/status mismatch')
    return checks


def run_target(catalog, tid, example=None, variant=None):
    target = catalog.target(tid) if variant is None else catalog.selected_target(tid, variant=variant)
    platform = catalog.platforms[target['platform']]
    adapter = load_adapter(catalog.root, platform)
    fingerprint = catalog.graph(tid).fingerprint()
    expected = adapter.cases(catalog.root, target, example)
    from firmware import invalidate
    invalidate(catalog, target, example)
    argv = [sys.executable, str(catalog.root/platform['adapter']), 'build', '--root', str(catalog.root), '--target', tid]
    if example:
        argv += ['--example', example]
    if variant is not None:
        argv += ['--variant', variant]
    try:
        run = subprocess.run(argv, capture_output=True, text=True, timeout=1900)
        return check_envelope(json.loads(run.stdout), tid, 'build', fingerprint, expected, run.returncode)
    except (ValueError, ContractError, KeyError, TypeError, subprocess.TimeoutExpired) as exc:
        return [result(name, 'failed', f'Adapter protocol failure: {exc}') for name in expected]


def main(stack):
    from model import Catalog
    parser = argparse.ArgumentParser()
    parser.add_argument('operation', choices=OPERATIONS)
    parser.add_argument('--root', type=Path, required=True)
    parser.add_argument('--target')
    parser.add_argument('--example')
    parser.add_argument('--variant')
    parser.add_argument('--output', type=Path)
    parser.add_argument('--project', type=Path)
    parser.add_argument('--plan', type=Path)
    parser.add_argument('--bundle', type=Path)
    parser.add_argument('--format', default='bin')
    args = parser.parse_args()
    c = t = None
    try:
        c = Catalog(args.root)
        p = c.platforms[stack]
        t = c.selected_target(args.target, variant=args.variant) if args.target else None
        require(t is None or t['platform'] == stack, 'Target/platform mismatch')
        declaration = p['operations'][args.operation]
        adapter = load_adapter(c.root, p)
        if not declaration['supported']:
            checks = [result(args.operation, 'not_supported', declaration['reason'])]
        elif args.operation == 'inspect':
            checks = [result(args.operation, 'passed', context=c.context(t['id'], args.example, variant=args.variant) if t else p)]
        elif args.operation == 'host-test':
            from testing import run_tests
            checks = run_tests(c.root, stack)
        elif args.operation == 'generate':
            from project import generate_project
            require(t and args.example and args.output, 'generate needs --target, --example, --output')
            checks = [result('generate', 'passed', project=generate_project(c, t['id'], args.example, args.output, variant=args.variant))]
        elif args.operation == 'package':
            from project import package_project
            require(args.project and args.output, 'package needs --project and --output')
            require(read_json(args.project/'project.yaml')['target'].split('/')[0] == stack, 'project/platform mismatch')
            checks = [result('package', 'passed', package=package_project(args.project, args.output))]
        elif args.operation == 'build':
            require(t, 'build needs --target')
            from firmware import invalidate, source_identity, finish_build, operation_guard
            guard = operation_guard(c.root, t['id']) if p['operations']['firmware-plan']['supported'] else nullcontext()
            with guard:
                attempt = invalidate(c, t, args.example)
                checks = []
                if (c.root/'project.yaml').exists() or (c.root/'codecraft.lock').exists():
                    from project import verify_project_build
                    _, _, checks = verify_project_build(c, t, args.example, args.variant)
                if not checks:
                    identity = source_identity(c, t) if attempt else None
                    toolchain = adapter.probe(c.root, t) if attempt else None
                    checks = adapter.build(c.root, t, args.example)
                    checks = finish_build(c, t, checks, attempt, identity, toolchain)
        elif args.operation.startswith('firmware-'):
            import firmware
            if args.operation == 'firmware-plan':
                value = firmware.plan(c, args.target, args.example, args.variant, args.format, args.output)
            elif args.operation == 'firmware-merge':
                require(args.plan and args.output, 'firmware-merge needs --plan and --output')
                require(firmware.read_plan(args.plan)['target'].split('/')[0] == stack, 'Plan/platform mismatch')
                value = firmware.merge(args.plan, args.output)
            else:
                require(args.bundle, 'firmware-verify needs --bundle')
                require(read_json(args.bundle/'manifest.json')['target'].split('/')[0] == stack, 'Bundle/platform mismatch')
                value = firmware.verify(c, args.bundle)
            checks = [result(args.operation, value['status'], firmware=value)]
        else:
            raise ContractError(f'No handler for declared supported operation: {args.operation}')
        payload = envelope(args.target, args.operation, c.graph(t['id']).fingerprint() if t else None, checks)
        print(json.dumps(payload, ensure_ascii=False))
        return exit_code(checks)
    except OperationUnavailable as exc:
        checks = [result(args.operation, exc.status, str(exc))]
        print(json.dumps(envelope(args.target, args.operation, c.graph(t['id']).fingerprint() if c and t else None, checks)))
        return exit_code(checks)
    except (ContractError, OSError, ValueError, KeyError, TypeError) as exc:
        print(json.dumps({'schema_version': 2, 'ok': False, 'error': {'type': 'contract', 'message': str(exc)}}))
        return 1
