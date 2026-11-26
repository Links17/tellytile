"""Platform-neutral primitives used by repository tools and stack adapters."""
from __future__ import annotations

import hashlib
import json
import os
from pathlib import Path

STATUSES = frozenset({'passed', 'failed', 'skipped', 'not_run', 'not_supported'})
OPERATIONS = ('inspect', 'configure', 'generate', 'build', 'host-test', 'simulate', 'flash', 'device-test', 'logs', 'package', 'firmware-plan', 'firmware-merge', 'firmware-verify')
IGNORED = {'.git', '.codecraft', '.codegraph', '.venv', 'secrets', '.secrets', '__pycache__', '.pytest_cache', 'node_modules', 'managed_components', 'build', 'built'}


class ContractError(ValueError):
    """An input or operation violated a versioned contract."""


class OperationUnavailable(ContractError):
    """An explicitly unsupported operation or an unavailable prerequisite."""

    def __init__(self, status, message):
        require(status in ('not_run', 'not_supported'), 'Invalid unavailable status')
        self.status = status
        super().__init__(message)


def require(condition, message):
    if not condition:
        raise ContractError(message)


def parse_json(text, label):
    def unique(items):
        value = {}
        for key, item in items:
            require(key not in value, f'{label}: duplicate key {key}')
            value[key] = item
        return value
    try:
        return json.loads(text, object_pairs_hook=unique,
                          parse_constant=lambda value: require(False, f'{label}: non-finite number {value}'))
    except json.JSONDecodeError as exc:
        raise ContractError(f'{label}: {exc}') from exc


def read_json(path):
    try:
        return parse_json(Path(path).read_text(encoding='utf-8'), path)
    except OSError as exc:
        raise ContractError(f'{path}: {exc}') from exc


def write_json(path, data):
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(data, ensure_ascii=False, indent=2, sort_keys=True) + '\n', encoding='utf-8')


def safe_path(root, relative, exists=True):
    require(isinstance(relative, str) and relative and not Path(relative).is_absolute(), f'invalid relative path: {relative!r}')
    root = Path(root).resolve()
    path = root / relative
    require(path.resolve().is_relative_to(root), f'path escapes repository: {relative}')
    require('..' not in Path(relative).parts, f'noncanonical relative path: {relative}')
    if exists:
        require(path.exists(), f'missing reference: {relative}')
    return path


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def object_digest(value):
    return hashlib.sha256(json.dumps(value, ensure_ascii=False, sort_keys=True, separators=(',', ':')).encode()).hexdigest()


def source_files(path, *, excluded_dirs=()):
    """Directory imports exclude local build products; explicit file inputs do not."""
    root = Path(path)
    for directory, dirs, files in os.walk(root):
        excluded = set(excluded_dirs) if Path(directory) == root else set()
        dirs[:] = sorted(x for x in dirs if x not in IGNORED | excluded and not x.startswith('build-'))
        for name in dirs:
            require(not (Path(directory)/name).is_symlink(), f'Directory symlink must be an explicit resource input: {Path(directory)/name}')
        for name in sorted(files):
            file = Path(directory) / name
            if name == '.env' or name.startswith('.env.') and name != '.env.example':
                continue
            if name in IGNORED | {'sdkconfig', 'sdkconfig.old', '.DS_Store'} or file.suffix in {'.pyc', '.uf2', '.hex', '.bin', '.elf'}:
                continue
            require(not file.is_symlink() or file.exists(), f'Broken source symlink: {file}')
            if file.is_file():
                yield file


def localized(value, label):
    require(isinstance(value, dict) and set(value) == {'en', 'zh_CN'}, f'{label}: English and Simplified Chinese required')
    require(all(isinstance(text, str) and text.strip() for text in value.values()), f'{label}: empty localization')


def result(name, status, reason='', **fields):
    require(status in STATUSES, f'invalid result status: {status}')
    require(status == 'passed' or bool(reason), f'{name}: non-passing results need a reason')
    return {'name': name, 'status': status, 'reason': reason, **fields}


def package_inventory(package):
    """Fingerprint a standalone package, not a native build source directory."""
    package=Path(package)
    require(package.is_dir() and not package.is_symlink(), 'Skill package must be a real directory')
    files={}
    for path in sorted(package.rglob('*')):
        relative=path.relative_to(package)
        if '__pycache__' in relative.parts or path.name=='.DS_Store':
            continue
        require(not path.is_symlink(), f'Skill package must not contain symlinks: {relative}')
        if path.is_file():
            require(path.name!='.env' and not (path.name.startswith('.env.') and path.name!='.env.example') and not {'secrets','.secrets'} & set(relative.parts),f'Private file inside Skill package: {relative}')
            files[relative.as_posix()]=digest(path)
    return files
