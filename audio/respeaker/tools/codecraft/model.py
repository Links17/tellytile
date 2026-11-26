"""Catalog orchestration across Device and Platform contracts."""
from __future__ import annotations

import copy
from pathlib import Path

from io_utils import read_json, require, safe_path
from graph import SourceGraph
from registry import domain, load_group
from device_entry import read_contract
from device_validation import validate as validate_device


def native_roots(target, platform):
    """Declared native inputs and pipeline; knowledge/evidence are added by Catalog."""
    roots = [f'{target["path"]}/target.json', f'platforms/{platform["id"]}/platform.json',
             platform['adapter'], *platform['runtime'], *target['inputs'],
             *target['dependencies'], target['toolchain']['source']]
    for name in target['examples']:
        roots += target['example_inputs'][name] + target['example_dependencies'][name]
    for choice in target.get('variants', {}).values():
        roots += choice['inputs'] + choice['dependencies']
    for binding in target['interfaces']:
        roots += binding['implementation']
        roots += [ref for refs in binding.get('example_implementation', {}).values() for ref in refs]
    return roots


class Catalog:
    def __init__(self, root):
        self.root = Path(root).resolve()
        self.devices = load_group(self.root, 'devices/*/AGENTS.md', reader=read_contract)
        self.platforms = load_group(self.root, 'platforms/*/platform.json')
        self.targets = load_group(self.root, 'devices/*/targets/target.json')
        self.platform_schema = domain(self.root, 'platforms')
        require(self.devices and self.platforms and self.targets, 'empty catalog')
        self.check()

    def check(self):
        for area, manifest in [('devices', 'AGENTS.md'), ('platforms', 'platform.json')]:
            for path in (self.root/area).iterdir():
                if path.is_dir() and not path.name.startswith(('.', '_')):
                    require((path/manifest).is_file(), f'unregistered {area} directory: {path.name}')
        for path in self.root.glob('devices/*/targets'):
            if path.is_dir() and not path.name.startswith('.'):
                require((path/'target.json').is_file(), f'unregistered Target: {path}')
                entry = read_json(path/'target.json')
                require(entry.get('path') == path.relative_to(self.root).as_posix(),
                        f'{path}: Target manifest location mismatch')
                require(not (path/'tests').exists(),
                        'Target tests belong in tests/devices/<device>/targets')
                require(not any((path/stack).exists() for stack in self.platforms),
                        f'{path}: native files belong directly in targets/, without a stack directory')
        for path in self.root.glob('platforms/*/targets'):
            require(not path.exists(), 'Targets belong in devices/<device>/targets')
        for identity, item in self.devices.items():
            require((self.root/'devices'/identity/'AGENTS.md').is_file(), 'Device identity/path mismatch')
            require(not (self.root/'devices'/identity/'tests').exists(), 'Device evidence belongs in tests/devices/<device>')
            validate_device(self.root, item)
        for path in self.root.glob('tests/devices/*'):
            if path.is_dir() and not path.name.startswith('.'):
                require(path.name in self.devices, f'evidence for unregistered Device: {path.name}')
        for identity, item in self.platforms.items():
            require((self.root/'platforms'/identity/'platform.json').is_file(), 'Platform identity/path mismatch')
            self.platform_schema.validate(self.root, item)
        for tid, target in self.targets.items():
            self.platform_schema.target(self.root, target, self.devices, self.platforms)
            self.graph(tid)
        require(all(self.device_targets(did) for did in self.devices), 'Device requires at least one Target')

    def device_targets(self, did):
        return sorted(tid for tid, target in self.targets.items() if target['device'] == did)

    def target(self, tid):
        require(tid in self.targets, f'unknown Target: {tid}')
        return self.targets[tid]

    def selected_target(self, tid, example=None, variant=None):
        target = copy.deepcopy(self.target(tid))
        variants = target.get('variants', {})
        if variant is not None or (example and variants):
            variant = variant if variant is not None else target['default_variant']
            require(variant in variants, f'{tid}: unknown variant {variant}')
            target['variants'] = {variant: variants[variant]}
            target['selected_variant'] = variant
            target['default_variant'] = variant
        if not example:
            return target
        require(example in target['examples'], f'{tid}: unknown example {example}')
        target['examples'] = [example]
        for key in ('example_inputs', 'example_dependencies'):
            target[key] = {example: target[key][example]}
        bindings = []
        for binding in target['interfaces']:
            scoped = binding.get('example_implementation', {})
            binding['example_implementation'] = {example: scoped[example]} if example in scoped else {}
            if not binding['implementation'] and not binding['example_implementation']:
                continue
            for operation in binding['operations'].values():
                if 'example_sources' in operation:
                    operation['example_sources'] = {example: operation['example_sources'][example]}
            bindings.append(binding)
        target['interfaces'] = bindings
        return target

    def graph(self, tid, example=None, variant=None):
        target = self.selected_target(tid, example, variant)
        device = self.devices[target['device']]
        platform = self.platforms[target['platform']]
        roots = ['AGENTS.md', f'platforms/{platform["id"]}/AGENTS.md',
                 f'devices/{device["id"]}/AGENTS.md', *device['references'],
                 *native_roots(target, platform)]
        roots.extend(asset['path'] for asset in device['assets'])
        for case in device['acceptance']['cases']:
            roots.append(case['source'])
        graph = SourceGraph(self.root)
        graph.resolve(roots)
        profile = self.root/'project.yaml'
        if profile.is_file():
            from project import profile as read_profile
            data = read_profile(self.root)
            items = data.get('skills', [])
            require(isinstance(items, list), 'Skill selections must be a list')
            if items:
                from skill_contracts import selected_graph
                graph.resolve(selected_graph(self.root, items).files)
        return graph

    def support(self, tid):
        from evidence import support
        return support(self, tid)

    def context(self, tid, example=None, task='develop', selected_skills=None, variant=None):
        skills = {'ideate': ['codecraft-development-ideate', 'codecraft-development-discover'], 'develop': ['codecraft-development-develop'], 'setup': ['codecraft-development-setup'], 'build': ['codecraft-development-build'],
                  'debug': ['codecraft-development-debug'], 'device-test': ['codecraft-development-device-test'],
                  'discover': ['codecraft-development-discover'], 'validate': ['codecraft-development-validate']}
        require(task in skills, f'unknown context task: {task}')
        target = self.selected_target(tid, example, variant)
        graph = self.graph(tid, example, variant)
        if selected_skills is not None:
            require(isinstance(selected_skills,list),'Skill selections must be a list')
            if selected_skills:
                from skill_contracts import selected_graph
                graph.resolve(selected_graph(self.root,selected_skills).files)
        guidance = ['AGENTS.md', f'devices/{target["device"]}/AGENTS.md', f'platforms/{target["platform"]}/AGENTS.md']
        guidance += [f'skills/{name}/SKILL.md' for name in skills[task]]
        for path in guidance:
            safe_path(self.root, path)
        require(set(guidance)<=graph.files, 'Workflow guidance is missing from the source graph')
        return {'schema_version': 3, 'device': self.devices[target['device']], 'target': target, 'task': task,
                'guidance': guidance, 'files': sorted(graph.files), 'dependencies': graph.snapshot()['edges'],
                'source_digest': graph.fingerprint(), 'support': self.support(tid), 'unknowns': self.devices[target['device']]['unknowns']}

    def resolve(self, interface_ids=(), platform=None, constraints=None):
        constraints = constraints or {}
        available = {entry['id'] for target in self.targets.values() for entry in target['interfaces']}
        require(set(interface_ids) | set(constraints) <= available, 'unknown requested native interface')
        require(platform is None or platform in self.platforms, 'unknown requested platform')
        requested = set(interface_ids) | set(constraints)
        candidates, rejected = [], []
        for tid, target in sorted(self.targets.items()):
            if platform and target['platform'] != platform:
                continue
            bindings = {binding['id']: binding for binding in target['interfaces']}
            if not requested <= bindings.keys():
                rejected.append({'target': tid, 'reason': 'Missing implementations', 'missing': sorted(requested - bindings.keys())})
                continue
            matches = {cid: self.platform_schema.match_interface(bindings[cid], constraints.get(cid, {})) for cid in requested}
            if any(item['status'] == 'unsupported' for item in matches.values()):
                rejected.append({'target': tid, 'constraints': matches})
                continue
            candidates.append({'target': tid, 'compatibility': 'unknown' if any(item['status'] == 'unknown' for item in matches.values()) else 'match',
                               'constraints': matches, 'verification': self.support(tid)['level'], 'unknowns': self.devices[target['device']]['unknowns']})
        return {'candidates': candidates, 'rejected': rejected, 'status': 'candidates' if candidates else 'unsupported',
                'needs_user_input': len(candidates) != 1, 'physical_compatibility': 'unknown'}
