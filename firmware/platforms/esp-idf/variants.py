"""Native CMake variant profiles and evidence; physical identity remains separate."""
from pathlib import Path
import re
import shlex

from io_utils import read_json, require, safe_path


def choices(target):
    return sorted(target.get('variants', {})) or [None]


def case_name(target, example, variant):
    return target['id']+'/'+example+('/'+variant if variant is not None else '')


def definitions(root, target, variant):
    item = target['variants'][variant]
    configuration = item['configuration']
    expected = target['path']+'/variants/'+variant+'.cmake'
    require(configuration == {'profile': expected}, f'{target["id"]}/{variant}: invalid native variant configuration')
    require(item['inputs'] == [expected] and item['dependencies'] == [], f'{variant}: native profile must be the declared variant input')
    text = re.sub(r'#[^\n]*', '', safe_path(root, expected).read_text()).strip()
    match = re.fullmatch(r'set\(CODECRAFT_VARIANT_DEFINES\s+([^)]*)\)', text)
    require(match is not None, f'{variant}: invalid native variant profile')
    values = shlex.split(match[1])
    require(values and all(re.fullmatch(r'CODECRAFT_[A-Z0-9_]+=[01]', value) for value in values), f'{variant}: invalid native variant definition')
    require(len({value.split("=")[0] for value in values}) == len(values), f'{variant}: duplicate native variant definition')
    return values


def validate(root, target):
    if not target.get('variants'):
        return
    router = target['path']+'/variants.cmake'
    require(router in target['inputs'], f'{target["id"]}: native variant router must be a common input')
    safe_path(root, router)
    for variant in choices(target):
        definitions(root, target, variant)


def verify_build(root, target, example, variant, output, sources):
    if variant is None:
        return None
    expected = definitions(root, target, variant)
    cache = (output/'CMakeCache.txt').read_text()
    values = re.findall(r'^CODECRAFT_HARDWARE_VARIANT(?::[^=\n]+)?=([^\n]*)$', cache, re.M)
    require(values == [variant], f'{example}: actual native variant differs from selection')
    commands = read_json(output/'compile_commands.json')
    checked = set()
    for command in commands:
        source = Path(command['file'])
        source = (source if source.is_absolute() else Path(command['directory'])/source).resolve()
        if source not in sources:
            continue
        tokens = command.get('arguments') or shlex.split(command['command'])
        actual = [token[2:] for token in tokens if token.startswith('-D')]
        for definition in expected:
            name = definition.split('=')[0]
            require([value for value in actual if value.split('=')[0] == name] == [definition], f'{example}: compiled variant definition differs from profile')
        checked.add(source)
    require(checked == sources, f'{example}: missing variant compile commands for BSP source')
    return {'id': variant, 'device_variant': target['variants'][variant]['device_variant'],
            'profile': target['variants'][variant]['configuration']['profile'],
            'definitions': expected, 'physical_compatibility': 'unknown'}
