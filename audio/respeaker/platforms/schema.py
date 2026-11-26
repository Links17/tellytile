"""Platform metadata and native Target registration, independent of stack type."""
import re

from io_utils import OPERATIONS, require, safe_path


def validate(root, platform):
    pid = platform['id']
    require(platform.get('schema_version') == 2, f'{pid}: unsupported platform schema')
    require(platform.get('adapter') == f'platforms/{pid}/adapter.py', f'{pid}: adapter must be owned by platform')
    safe_path(root, platform['adapter'])
    require(platform.get('runtime'), f'{pid}: runtime dependency declaration required')
    for ref in platform['runtime']:
        safe_path(root, ref)
    require(set(platform.get('operations', {})) == set(OPERATIONS), f'{pid}: incomplete operations')
    for name, declaration in platform['operations'].items():
        require(type(declaration.get('supported')) is bool and declaration.get('reason'), f'{pid}/{name}: explicit support and reason required')
    require(platform.get('ci', {}).get('runner'), f'{pid}: CI runner declaration required')


def target(root, entry, devices, platforms):
    tid = entry['id']
    allowed = {'schema_version','id','platform','device','path','examples','inputs','dependencies',
               'example_inputs','example_dependencies','toolchain','interfaces','firmware',
               'variants','default_variant','selected_variant'}
    require(set(entry) <= allowed, f'{tid}: unknown Target fields; cloud selections belong in project.yaml')
    require(entry.get('schema_version') == 3, f'{tid}: unsupported Target schema')
    require(isinstance(entry.get('variants'), dict), f'{tid}: Target schema_version 3 requires variants object')
    require(entry.get('platform') in platforms and entry.get('device') in devices, f'{tid}: unknown platform/device')
    require(tid == f'{entry["platform"]}/{entry["device"]}' and entry.get('path') == f'devices/{entry["device"]}/targets', f'{tid}: identity/path mismatch')
    examples = entry.get('examples')
    require(isinstance(examples, list) and examples and len(set(examples)) == len(examples), f'{tid}: examples required and unique')
    require(all(re.fullmatch(r'[a-zA-Z0-9_-]+', name) for name in examples), f'{tid}: invalid example id')
    for key in ('example_inputs', 'example_dependencies'):
        require(set(entry.get(key, {})) == set(examples), f'{tid}: {key} must cover every example')
        require(all(isinstance(refs, list) for refs in entry[key].values()), f'{tid}: invalid {key}')
    require(all(entry['example_inputs'].values()), f'{tid}: every example needs native inputs')
    for key in ('inputs', 'dependencies'):
        require(isinstance(entry.get(key), list), f'{tid}: {key} must be declared')
    require(isinstance(entry.get('toolchain'), dict) and {'name', 'version', 'source'} <= entry['toolchain'].keys(), f'{tid}: toolchain provenance required')
    safe_path(root, entry['toolchain']['source'])
    firmware = entry.get('firmware')
    if firmware is not None:
        require(isinstance(firmware, dict) and set(firmware) == {'formats', 'purpose', 'security'}, f'{tid}: invalid firmware profile')
        require(firmware['formats'] == ['bin'] and firmware['purpose'] == 'factory'
                and firmware['security'] == 'unsigned-unencrypted', f'{tid}: unsupported firmware profile')
        require(all(platforms[entry['platform']]['operations']['firmware-'+op]['supported'] for op in ('plan', 'merge', 'verify')),
                f'{tid}: firmware profile requires supported Platform operations')
    variants = entry.get('variants', {})
    require(isinstance(variants, dict), f'{tid}: variants must be an object')
    if variants:
        require(entry.get('default_variant') in variants, f'{tid}: declared default variant required')
        for name, variant in variants.items():
            require(re.fullmatch(r'[a-z][a-z0-9_-]*', name) and isinstance(variant, dict), f'{tid}: invalid build variant')
            require(variant.get('device_variant') in devices[entry['device']].get('variants', {}), f'{tid}/{name}: unknown Device variant')
            require(isinstance(variant.get('inputs'), list) and variant['inputs'] and isinstance(variant.get('dependencies'), list), f'{tid}/{name}: variant source declarations required')
            require(isinstance(variant.get('configuration'), dict) and variant['configuration'], f'{tid}/{name}: native variant configuration required')
        if 'selected_variant' in entry:
            require(set(variants) == {entry['selected_variant']} and entry['default_variant'] == entry['selected_variant'], f'{tid}: selected variant projection mismatch')
    else:
        require('default_variant' not in entry and 'selected_variant' not in entry, f'{tid}: variant selection without declarations')
    require(isinstance(entry.get('interfaces'), list) and entry['interfaces'], f'{tid}: native interfaces required')
    require(len({item['id'] for item in entry['interfaces']}) == len(entry['interfaces']), f'{tid}: duplicate native interface')
    for item in entry['interfaces']:
        native_interface(root, item, examples)


def native_interface(root, entry, examples):
    identity = entry['id']
    require('verification' not in entry, f'{identity}: verification is derived from evidence')
    refs = entry.get('implementation', [])
    scoped = entry.get('example_implementation', {})
    require(isinstance(refs, list) and isinstance(scoped, dict) and (refs or scoped), f'{identity}: implementation references required')
    require(set(scoped) <= set(examples) and all(isinstance(paths, list) and paths for paths in scoped.values()), f'{identity}: invalid example scope')
    all_refs = set(refs + [path for paths in scoped.values() for path in paths])
    for path in all_refs:
        safe_path(root, path)
    require(isinstance(entry.get('operations'), dict) and entry['operations'], f'{identity}: native operation references required')
    for name, operation in entry['operations'].items():
        require(type(operation.get('supported')) is bool and operation.get('reason'), f'{identity}/{name}: support and reason required')
        if operation['supported']:
            sources = [operation['source']] if 'source' in operation else list(operation.get('example_sources', {}).values())
            require(sources and set(sources) <= all_refs, f'{identity}/{name}: operation source outside interface')
            require('example_sources' not in operation or set(operation['example_sources']) == set(scoped), f'{identity}/{name}: example scope mismatch')
            require(operation.get('symbol') and all(operation['symbol'] in safe_path(root, path).read_text() for path in sources), f'{identity}/{name}: native symbol missing')
    for group in ('configuration', 'resources'):
        require(isinstance(entry.get(group), dict), f'{identity}: {group} must be declared')
        for name, item in entry[group].items():
            require(item.get('type') in ('integer','number','string','boolean'), f'{identity}/{name}: typed native parameter required')
            require(item.get('status') in ('known','unknown'), f'{identity}/{name}: explicit parameter status required')
            if item['status'] == 'unknown':
                require(item.get('value') is None and item.get('reason'), f'{identity}/{name}: unknown needs null value and reason')
            else:
                parameter_value(item, item.get('value'))
                source = item.get('source', {})
                require(source.get('path') in all_refs and source.get('assertion'), f'{identity}/{name}: native source assertion required')
                require(' '.join(source['assertion'].split()) in ' '.join(safe_path(root, source['path']).read_text().split()) and str(item['value']) in source['assertion'], f'{identity}/{name}: source/value mismatch')


def parameter_value(spec, actual):
    kinds = {'integer':int,'number':(int,float),'string':str,'boolean':bool}
    require(isinstance(actual,kinds[spec['type']]) and not (spec['type'] in ('integer','number') and type(actual) is bool), 'Native parameter type mismatch')
    require('enum' not in spec or actual in spec['enum'], 'Native parameter outside enum')
    require('minimum' not in spec or actual >= spec['minimum'], 'Native parameter below minimum')
    require('maximum' not in spec or actual <= spec['maximum'], 'Native parameter above maximum')


def match_interface(entry, request):
    require(set(request) <= {'operations','configuration','resources'}, 'Unknown native interface constraint')
    rejected, unknown = [], []
    for name in request.get('operations', []):
        require(name in entry['operations'], 'Unknown native operation')
        if not entry['operations'][name]['supported']:
            rejected.append(name+': '+entry['operations'][name]['reason'])
    for group in ('configuration','resources'):
        for name, expected in request.get(group, {}).items():
            require(name in entry[group], 'Unknown native parameter')
            item = entry[group][name]
            parameter_value(item,expected)
            if item['status'] == 'unknown':
                unknown.append(group+'/'+name+': '+item['reason'])
            elif (item['value'] > expected if group == 'resources' else item['value'] != expected):
                rejected.append(group+'/'+name+': constraint conflicts with native configuration')
    return {'status':'unsupported' if rejected else ('unknown' if unknown else 'match'),'conflicts':rejected,'unknowns':unknown,'physical_verification':'unknown'}
