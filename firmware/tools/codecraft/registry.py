"""Neutral metadata loading; domain validation and orchestration stay with owners."""
import importlib.util
import re

from io_utils import read_json, require


def domain(root, name):
    spec = importlib.util.spec_from_file_location('codecraft_' + name + '_schema', root/name/'schema.py')
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def load_group(root, pattern, reader=read_json):
    values = {}
    for path in sorted(root.glob(pattern)):
        data = reader(path)
        require(isinstance(data, dict), f'{path}: expected object')
        identity = data.get('id')
        require(isinstance(identity, str) and re.fullmatch(r'[a-z0-9]+(?:[-/][a-z0-9]+)*', identity), f'{path}: invalid id')
        require(identity not in values, f'duplicate id: {identity}')
        values[identity] = data
    return values
