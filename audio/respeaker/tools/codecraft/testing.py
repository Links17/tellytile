"""Discover host tests under tests/ and run plain functions or unittest suites."""
from contextlib import redirect_stderr, redirect_stdout
import hashlib
import importlib.util
import inspect
import io
import itertools
from pathlib import Path
import sys
import unittest

from io_utils import read_json, require, result

_load_sequence = itertools.count()


def test_files(root, platform=None, selection=None):
    root = Path(root).resolve()
    stack = platform or '*'
    patterns = ('tests/tools/*', 'tests/skills/*', f'tests/platforms/{stack}')
    directories = {directory for pattern in patterns for directory in root.glob(pattern)}
    for directory in root.glob('tests/devices/*/targets'):
        device = directory.parent.name
        native = f'devices/{device}/targets'
        target = read_json(root/native/'target.json')
        require(target.get('device') == device and target.get('path') == native
                and target.get('id') == f'{target.get("platform")}/{device}',
                f'{directory}: test owner differs from Target manifest')
        if platform is None or target.get('platform') == platform:
            directories.add(directory)
    paths = set()
    for directory in sorted(directories):
        for path in directory.rglob('test_*.py'):
            relative = path.relative_to(root)
            if any(part.startswith('.') or part in ('__pycache__', 'fixtures', 'stubs', 'mocks', 'build')
                   or part.startswith('build-')
                   for part in relative.parts):
                continue
            require(not any(p.is_symlink() for p in (path, *path.parents) if p != root),
                    f'Symlink test path is not allowed: {relative}')
            paths.add(path)
    if selection is not None:
        relative = Path(selection)
        require(not relative.is_absolute() and '..' not in relative.parts,
                'Test selection must be a repository-relative file or directory')
        selected = root / relative
        paths = {p for p in paths if p == selected or selected in p.parents}
    require(paths, 'No host tests discovered for the requested scope')
    return sorted(paths)


def _cases(suite):
    for item in suite:
        if isinstance(item, unittest.TestSuite):
            yield from _cases(item)
        else:
            yield item


def _collect(root, platform=None, selection=None):
    groups = []
    for path in test_files(root, platform, selection):
        relative = path.relative_to(Path(root).resolve()).as_posix()
        name = 'codecraft_host_' + hashlib.sha256(str(path).encode()).hexdigest()+f'_{next(_load_sequence)}'
        group = {'path': relative, 'name': name, 'tests': []}
        try:
            spec = importlib.util.spec_from_file_location(name, path)
            module = importlib.util.module_from_spec(spec)
            sys.modules[name] = module
            with redirect_stdout(io.StringIO()), redirect_stderr(io.StringIO()):
                spec.loader.exec_module(module)
            functions = [(key, value) for key, value in sorted(vars(module).items())
                         if key.startswith('test_') and inspect.isfunction(value)
                         and value.__module__ == name]
            require(len({id(fn) for _, fn in functions}) == len(functions),
                    f'{relative}: duplicate function aliases')
            require(not any(inspect.iscoroutinefunction(fn) for _, fn in functions),
                    f'{relative}: asynchronous tests require an explicit synchronous runner')
            loader = unittest.TestLoader()
            suites = [loader.loadTestsFromTestCase(value) for _, value in sorted(vars(module).items())
                      if inspect.isclass(value) and value.__module__ == name
                      and issubclass(value, unittest.TestCase)]
            require(not loader.errors, f'{relative}: unittest collection errors: {loader.errors}')
            suite = unittest.TestSuite(suites)
            group.update(functions=functions, suite=suite)
            group['tests'] = [{'name': relative+'::'+key, 'kind': 'function'} for key, _ in functions]
            group['tests'] += [{'name': relative+'::'+case.id().removeprefix(name+'.'), 'kind': 'unittest'}
                               for case in _cases(suite)]
            ids = [entry['name'] for entry in group['tests']]
            require(ids and len(ids) == len(set(ids)), f'{relative}: missing or duplicate tests')
        except (Exception, SystemExit) as exc:
            group['error'] = result(relative, 'failed', f'Collection: {type(exc).__name__}: {exc}')
            group['tests'] = []
        groups.append(group)
    return groups


def inventory(root, platform=None, selection=None):
    groups = _collect(root, platform, selection)
    return {'tests': [entry for group in groups for entry in group['tests']],
            'collection_errors': [group['error'] for group in groups if 'error' in group]}


class _Results(unittest.TestResult):
    def __init__(self, group):
        super().__init__()
        self.group = group
        self.rows = {}

    def record(self, test, status, reason=''):
        identity = self.group['path']+'::'+test.id().removeprefix(self.group['name']+'.')
        if identity in self.rows and self.rows[identity]['status'] == 'failed':
            if reason:
                self.rows[identity]['reason'] += '\n'+reason
            return
        self.rows[identity] = result(identity, status, reason)

    def addSuccess(self, test):
        super().addSuccess(test)
        self.record(test, 'passed')

    def addError(self, test, error):
        super().addError(test, error)
        self.record(test, 'failed', self._exc_info_to_string(error, test))

    def addFailure(self, test, error):
        super().addFailure(test, error)
        self.record(test, 'failed', self._exc_info_to_string(error, test))

    def addSubTest(self, test, subtest, error):
        super().addSubTest(test, subtest, error)
        if error is not None:
            self.record(test, 'failed', str(subtest)+': '+self._exc_info_to_string(error, test))

    def addSkip(self, test, reason):
        super().addSkip(test, reason)
        self.record(test, 'skipped', reason)

    def addExpectedFailure(self, test, error):
        super().addExpectedFailure(test, error)
        self.record(test, 'skipped', 'Expected failure: '+self._exc_info_to_string(error, test))

    def addUnexpectedSuccess(self, test):
        super().addUnexpectedSuccess(test)
        self.record(test, 'failed', 'Unexpected success of an expected-failure test')


def run_tests(root, platform=None, selection=None):
    checks = []
    for group in _collect(root, platform, selection):
        if 'error' in group:
            checks.append(group['error'])
            continue
        for name, function in group['functions']:
            identity = group['path']+'::'+name
            transcript = io.StringIO()
            try:
                with redirect_stdout(transcript), redirect_stderr(transcript):
                    function()
                checks.append(result(identity, 'passed'))
            except unittest.SkipTest as exc:
                checks.append(result(identity, 'skipped', str(exc)))
            except (Exception, SystemExit) as exc:
                checks.append(result(identity, 'failed', f'{type(exc).__name__}: {exc}\n'+transcript.getvalue()))
        result_set = _Results(group)
        with redirect_stdout(io.StringIO()), redirect_stderr(io.StringIO()):
            group['suite'].run(result_set)
        checks.extend(result_set.rows.values())
        for entry in group['tests']:
            if entry['kind'] == 'unittest' and entry['name'] not in result_set.rows:
                checks.append(result(entry['name'], 'not_run', 'A suite fixture prevented this test from running'))
    return checks
