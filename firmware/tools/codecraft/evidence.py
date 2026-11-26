"""Durable observations, scope coverage and source-sensitive support derivation."""
from datetime import datetime
import re

from io_utils import STATUSES, digest, read_json, require, safe_path
from runtime import load_adapter


def device_evidence_dir(catalog, target):
    return catalog.root/'tests/devices'/target['device']


def validate_log(catalog, target, path, expected):
    log = safe_path(catalog.root, path)
    require(log.resolve().is_relative_to(device_evidence_dir(catalog, target)/'logs'), 'Evidence logs must be retained in tests/devices/<device>/logs')
    require(digest(log) == expected, 'Evidence log hash mismatch')
    require(not re.search(r'(?i)(access[_-]?token|password|secret)\s*[:=]\s*\S+', log.read_text(errors='replace')), 'Sanitize evidence log before recording')


def validate_project_cases(catalog, target, project, example):
    checks = project['build_results']
    names = [item.get('name') for item in checks]
    adapter = load_adapter(catalog.root, catalog.platforms[target['platform']])
    expected = adapter.cases(catalog.root, target, example)
    require(len(names) == len(set(names)) and set(names) == set(expected),
            'Generated project must cover every selected native case, including overlays and variants')


def validate_evidence(catalog, record, current=False):
    required = {'schema_version','target','kind','repository_commit','repository_dirty','source_digest','revision','toolchain','command','environment','result','limitations','operator','log','log_sha256','executed_at','cases'}
    require(isinstance(record, dict) and required <= record.keys(), 'Incomplete evidence record')
    require(record['schema_version'] == 2 and record['kind'] in ('build','device','simulator'), 'Unsupported evidence schema/kind')
    target = catalog.target(record['target'])
    require(record['result'] in STATUSES, 'Invalid evidence result')
    require(re.fullmatch(r'[0-9a-f]{40}', record['repository_commit']) and type(record['repository_dirty']) is bool, 'Invalid repository provenance')
    require(re.fullmatch(r'[0-9a-f]{64}', record['source_digest']), 'Invalid source digest')
    require(datetime.fromisoformat(record['executed_at']).tzinfo is not None, 'Evidence timestamp needs timezone')
    require(isinstance(record['limitations'], list) and isinstance(record['cases'], list) and record['cases'], 'Evidence needs explicit cases and limitations')
    for key in ('toolchain','command','environment','operator'):
        require(record[key], f'Evidence requires {key}')
    validate_log(catalog, target, record['log'], record['log_sha256'])
    if current:
        require(record['source_digest'] == catalog.graph(target['id']).fingerprint(), 'Evidence source is stale')
    names = [case.get('name') for case in record['cases']]
    require(len(names) == len(set(names)), 'Duplicate evidence case')
    require(all(case.get('status') in STATUSES and (case['status'] == 'passed' or case.get('reason')) for case in record['cases']), 'Invalid evidence case result')
    require((record['result'] == 'passed') == all(case['status'] == 'passed' for case in record['cases']), 'Evidence result/cases mismatch')
    if record['kind'] == 'build':
        expected = load_adapter(catalog.root, catalog.platforms[target['platform']]).cases(catalog.root, target)
        # Source-scoped observations retain their recorded case set when the Target changes.
        if current or record['source_digest'] == catalog.graph(target['id']).fingerprint():
            require(set(names) == set(expected), 'Build evidence must cover every native case, including overlays')
        for case in record['cases']:
            require(case.get('toolchain'), 'Build case needs actual toolchain')
            if case.get('log'):
                validate_log(catalog, target, case['log'], case['log_sha256'])
        project = record.get('generated_project', {})
        if record['result'] == 'passed':
            require(project.get('status') == 'passed' and project.get('provenance') == 'passed' and project.get('origin_source_digest') == record['source_digest'], 'Build verification needs a generated project from the same sources')
            require(project.get('example') in target['examples'] and project.get('source_digest') and project.get('build_results'), 'Incomplete generated project evidence')
            require(all(item['status'] == 'passed' for item in project['build_results']), 'Generated project build failed')
            if current or record['source_digest'] == catalog.graph(target['id']).fingerprint():
                validate_project_cases(catalog, target, project, project['example'])
            if (current or record['source_digest'] == catalog.graph(target['id']).fingerprint()) and target.get('variants'):
                projects = project.get('variant_projects', {})
                require(set(projects) == set(target['variants']), 'Generated projects must cover every variant')
                for variant, child in projects.items():
                    require(child.get('variant') == variant and child.get('status') == child.get('provenance') == 'passed', 'Generated project variant evidence mismatch')
                    require(child.get('origin_source_digest') == record['source_digest'] and child.get('source_digest') and child.get('build_results'), 'Incomplete variant project provenance')
                    require(all(item.get('status') == 'passed' and item.get('variant', {}).get('id') == variant for item in child['build_results']), 'Generated project variant build mismatch')
                    require(child.get('example') == project['example'], 'Generated project variant example mismatch')
                    validate_project_cases(catalog, catalog.selected_target(target['id'], variant=variant), child, project['example'])
                    validate_log(catalog, target, child['log'], child['log_sha256'])
            validate_log(catalog, target, project['log'], project['log_sha256'])
    elif record['kind'] == 'device':
        require(record['revision'] not in ('', None, 'unknown'), 'Device evidence requires observed revision')
        acceptance = catalog.devices[target['device']]['acceptance']
        require(set(names) <= {case['id'] for case in acceptance['cases']}, 'Unknown physical acceptance case')
        for case in record['cases']:
            require(case.get('observation') and case.get('procedure'), 'Physical evidence needs actual observation and procedure')
        if current or record['source_digest'] == catalog.graph(target['id']).fingerprint():
            variants = target.get('variants', {})
            variant = record.get('variant')
            require(variant in variants if variants else variant is None, 'Physical evidence variant mismatch')
            if variants:
                hardware = catalog.devices[target['device']]['variants'][variants[variant]['device_variant']]
                revisions = hardware['facts']['compatible_revisions']
                if revisions['status'] != 'unknown':
                    require(record['revision'] in revisions['value'], 'Physical revision is incompatible with selected variant')
    elif record['kind'] == 'simulator':
        require(record.get('coverage') and record.get('simulator'), 'Simulator evidence needs coverage and version')
    return target


def support(catalog, tid):
    target = catalog.target(tid)
    current_digest = catalog.graph(tid).fingerprint()
    state = {'level':'unknown','build':'not_run','project':'not_run','device':'not_run','records':[],'stale_records':[],
             'interfaces': {entry['id']:'unknown' for entry in target['interfaces']}}
    records = []
    for path in sorted((device_evidence_dir(catalog, target)/'records').glob('*.json')):
        record = read_json(path)
        validate_evidence(catalog, record)
        if record['target'] == tid:
            records.append((datetime.fromisoformat(record['executed_at']), path, record))
    variants = target.get('variants', {})
    physical = {variant: {} for variant in (variants or {None: None})}
    for _, path, record in sorted(records):
        relative = str(path.relative_to(catalog.root))
        if record['source_digest'] != current_digest:
            state['stale_records'].append(relative)
            continue
        state['records'].append(relative)
        if record['kind'] == 'build':
            state['build'] = record['result']
            state['project'] = record.get('generated_project',{}).get('status','not_run')
        elif record['kind'] == 'device':
            variant = record.get('variant')
            if variants:
                hardware = catalog.devices[target['device']]['variants'][variants[variant]['device_variant']]
                revisions = hardware['facts']['compatible_revisions']
                compatible = revisions['status'] != 'unknown' and record['revision'] in revisions['value']
            else:
                compatible = record['revision'] == catalog.devices[target['device']]['revision']
            if compatible:
                physical[variant].update({case['name']:case['status'] for case in record['cases']})
    if state['build'] == state['project'] == 'passed':
        state['level'] = 'build-verified'
    acceptance = catalog.devices[target['device']]['acceptance']['cases']
    statuses = {}
    for variant, observations in physical.items():
        statuses[variant] = 'passed' if all(observations.get(case['id']) == 'passed' for case in acceptance) else ('failed' if 'failed' in observations.values() else 'not_run')
    state['device'] = 'passed' if all(value == 'passed' for value in statuses.values()) else ('failed' if 'failed' in statuses.values() else 'not_run')
    if variants:
        state['variants'] = {name: {'device': status} for name, status in statuses.items()}
    for cid in state['interfaces']:
        cases = [case['id'] for case in acceptance if cid in case['interfaces'] or not case['interfaces']]
        if cases and all(observations.get(case) == 'passed' for observations in physical.values() for case in cases):
            state['interfaces'][cid] = 'device-verified'
    if state['level'] == 'build-verified' and state['device'] == 'passed':
        state['level'] = 'device-verified'
    return state
