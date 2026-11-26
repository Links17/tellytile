"""Deterministic English engineering views; README files alone are bilingual."""
import json
from pathlib import Path
import re

from io_utils import require
from device_entry import read_entry

def paired(path, en, zh):
    path = Path(path)
    require(path.name == 'README.md', 'Only README.md may have a translated document')
    cn = path.with_name(path.stem + '.zh_CN.md')
    return {str(path): f'<p align="right"><strong>English</strong> · <a href="{cn.name}">简体中文</a></p>\n\n'+en.rstrip()+'\n',
            str(cn): f'<p align="right"><a href="{path.name}">English</a> · <strong>简体中文</strong></p>\n\n'+zh.rstrip()+'\n'}


def matrix(catalog):
    return {'include': [{'id': target['id'], 'device': target['device'], 'platform': target['platform'],
                         'toolchain': target['toolchain']['version'], 'chip': target['toolchain'].get('target', ''),
                         **catalog.platforms[target['platform']]['ci']} for target in catalog.targets.values()]}


def views(c):
    outputs = {}
    rows = ['| Device | Target | Implementations | Build + project | Device |', '| --- | --- | --- | --- | --- |']
    overview = ['# Device directory', '',
                'This directory index is generated from the authored Device Markdown entries.', '',
                'Each device has its own directory and AGENTS.md knowledge entry. Keep hardware knowledge, references and assets there; targets/ owns its native BSP, examples and board configuration. Use reference/ for supporting material and assets/ for actual registered media when needed. Shared stack/runtime adapters, build helpers and reusable components belong in platforms/<stack>/. Board-specific tests live in tests/devices/<device>/targets/; observations, records and logs live alongside that test subtree.', '']
    sources = ['# Vendor source index','','Generated references. Read primary sources before relying on facts.','']
    compatibility = []
    for did, device in c.devices.items():
        tids = c.device_targets(did)
        targets = [c.targets[tid] for tid in tids]
        caps = sorted({b['id'] for t in targets for b in t['interfaces']})
        states = {tid: c.support(tid) for tid in tids}
        levels = ', '.join(state['level'] for state in states.values())
        physical = ', '.join(state['device'] for state in states.values())
        rows.append(f'| [{device["name"]}](devices/{did}/AGENTS.md) | {", ".join(tids)} | {", ".join(caps)} | {levels} | {physical} |')
        text = lambda field: device[field]
        overview += [f'## [{device["name"]}]({did}/AGENTS.md)','',text('overview'),'',text('scenarios'),'']
        sources += [f'## {device["name"]}', '', f'[Authoritative sources and notes](../devices/{did}/AGENTS.md#sources)', '']
        for target in targets:
            for binding in target['interfaces']:
                compatibility.append({'target':target['id'],'interface':binding['id'],
                                      'operations':binding['operations'],'configuration':binding['configuration'],'resources':binding['resources'],
                                      'verification':states[target['id']]['interfaces'][binding['id']]})
    outputs['devices/AGENTS.md'] = '\n'.join(overview).rstrip()+'\n'
    outputs['docs/vendor-sources.md'] = '\n'.join(sources).rstrip()+'\n'
    outputs['docs/inventory.md'] = '# Native interface matrix\n\nGenerated implementations and scoped evidence. See the [JSON bindings](interface-matrix.json) for operation, configuration and resource details.\n\n'+'\n'.join(rows).replace('(devices/','(../devices/')+'\n'
    outputs['docs/interface-matrix.json'] = json.dumps({'schema_version':2,'bindings':compatibility},ensure_ascii=False,indent=2,sort_keys=True)+'\n'
    from skill_contracts import registrations, load_package
    packages = []
    for sid in sorted(registrations(c.root)):
        record, contract = load_package(c.root, sid)
        packages.append({'id':sid,'version':record['version'],'contract_version':contract['version'],
                         'policy_version':contract['policy']['version'],'api_version':contract['api']['version'],
                         'operations':sorted(contract['operations']),'route':f'skills/{sid}/SKILL.md'})
    outputs['docs/skill-inventory.json'] = json.dumps({'schema_version':1,'skills':packages},ensure_ascii=False,indent=2,sort_keys=True)+'\n'
    outputs['.github/target-matrix.json'] = json.dumps(matrix(c),ensure_ascii=False,indent=2)+'\n'
    return outputs


def generate(catalog, check=False):
    changed = []
    for relative, text in views(catalog).items():
        path = catalog.root/relative
        if not path.exists() or read_entry(path) != text:
            changed.append(relative)
            if not check:
                path.parent.mkdir(parents=True,exist_ok=True)
                path.write_bytes(text.encode('utf-8'))
    return changed
