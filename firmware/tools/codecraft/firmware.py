"""Stack-neutral firmware provenance and delivery. Native layouts belong to adapters."""
from contextlib import contextmanager
import json
import os
from pathlib import Path
import tempfile
import uuid

from io_utils import (ContractError, OperationUnavailable, digest, object_digest,
                      read_json, require, write_json)
from runtime import load_adapter


def receipt_path(root, case):
    return root/'.codecraft/firmware/receipts'/(object_digest(case)+'.json')


@contextmanager
def operation_guard(root, tid):
    """Do not run native builds and merges for the same Target concurrently."""
    path = root/'.codecraft/firmware/locks'/object_digest(tid)
    path.parent.mkdir(parents=True, exist_ok=True)
    try:
        path.mkdir()
    except FileExistsError as exc:
        raise ContractError(f'Another firmware operation owns {path}; if interrupted, confirm it stopped before removing this lock') from exc
    try:
        yield
    finally:
        path.rmdir()


def atomic_json(path, value, exclusive=False):
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.NamedTemporaryFile(dir=path.parent, delete=False) as stream:
        temporary = Path(stream.name)
        stream.write((json.dumps(value, indent=2, sort_keys=True)+'\n').encode())
    try:
        if exclusive:
            os.link(temporary, path)
        else:
            os.replace(temporary, path)
    finally:
        temporary.unlink(missing_ok=True)


def invalidate(catalog, target, example=None):
    """Invalidate all requested cases before preflight or subprocess execution."""
    platform = catalog.platforms[target['platform']]
    if not platform['operations']['firmware-plan']['supported']:
        return None
    adapter = load_adapter(catalog.root, platform)
    attempt = uuid.uuid4().hex
    for case in adapter.cases(catalog.root, target, example):
        atomic_json(receipt_path(catalog.root, case), {
            'schema_version': 1, 'kind': 'firmware-build', 'attempt': attempt,
            'target': target['id'], 'case': case, 'status': 'not_run',
            'reason': 'A new build was requested; no completed firmware receipt exists.'})
    return attempt


def source_identity(catalog, target):
    lock = catalog.root/'codecraft.lock'
    if lock.exists() or (catalog.root/'project.yaml').exists():
        from project import verify_project
        require(verify_project(catalog.root)['status'] == 'passed', 'Project source differs from its lock')
    return {'source_digest': catalog.graph(target['id']).fingerprint(),
            'lock_sha256': digest(lock) if lock.is_file() else None}


def finish_build(catalog, target, checks, attempt, identity, toolchain):
    """Only successful, unchanged builds can publish per-case firmware receipts."""
    if attempt is None:
        return checks
    adapter = load_adapter(catalog.root, catalog.platforms[target['platform']])
    try:
        require(source_identity(catalog, target) == identity, 'Source/lock changed during build')
        require(adapter.probe(catalog.root, target) == toolchain, 'Toolchain changed during build')
        for row in checks:
            path = receipt_path(catalog.root, row['name'])
            require(read_json(path)['attempt'] == attempt, 'Another build replaced this attempt')
            receipt = {**read_json(path), **identity, 'toolchain': toolchain,
                       'status': row['status'], 'reason': row.get('reason', '')}
            if row['status'] == 'passed':
                selection = row['firmware_selection']
                receipt.update(selection)
                try:
                    receipt['layout'] = adapter.firmware_layout(catalog.root, target, **selection)
                except OperationUnavailable as exc:
                    receipt.update(status=exc.status, reason=str(exc))
            atomic_json(path, receipt)
    except (ContractError, OSError, ValueError, KeyError, TypeError) as exc:
        for row in checks:
            row.update(status='failed', reason=f'Firmware build provenance failed: {exc}')
        # Invalidate even receipts completed before an error in a later case.
        invalidate(catalog, target)
    return checks


def supported(catalog, target, operation, image_format='bin'):
    platform = catalog.platforms[target['platform']]
    declaration = platform['operations']['firmware-'+operation]
    if not declaration['supported']:
        raise OperationUnavailable('not_supported', declaration['reason'])
    profile = target.get('firmware', {})
    if image_format not in profile.get('formats', []):
        raise OperationUnavailable('not_supported', f'{target["id"]}: firmware format {image_format} is not supported')
    return load_adapter(catalog.root, platform)


def choose(catalog, tid=None, example=None, variant=None):
    if (catalog.root/'project.yaml').exists():
        from project import profile
        data = profile(catalog.root)
        require(tid in (None, data['target']) and example in (None, data['example'])
                and variant in (None, data.get('variant')), 'Selection differs from project.yaml')
        tid, example, variant = data['target'], data['example'], data.get('variant')
    require(tid and example, 'Firmware planning requires --target and --example, or --project')
    target = catalog.selected_target(tid, example, variant)
    return target, example, target.get('selected_variant')


def check_receipt(catalog, target, example, variant, adapter):
    case_names = adapter.cases(catalog.root, target, example)
    require(len(case_names) == 1, 'Firmware planning requires exactly one build case')
    path = receipt_path(catalog.root, case_names[0])
    if not path.exists():
        raise OperationUnavailable('not_run', 'No recorded build; run validate --target/--example or build-project first')
    receipt = read_json(path)
    require(isinstance(receipt, dict) and receipt.get('schema_version') == 1 and receipt.get('kind') == 'firmware-build', 'Invalid firmware build receipt')
    require(receipt.get('target') == target['id'] and receipt.get('case') == case_names[0], 'Firmware receipt identity mismatch')
    if receipt['status'] != 'passed':
        if receipt['status'] in ('not_run', 'not_supported'):
            raise OperationUnavailable(receipt['status'], receipt['reason'])
        raise ContractError('Latest build failed: '+receipt['reason'])
    require(receipt.get('example') == example and receipt.get('variant') == variant, 'Firmware receipt selection mismatch')
    identity = source_identity(catalog, target)
    require(all(receipt.get(key) == value for key, value in identity.items()), 'Firmware build source/lock is stale; rebuild')
    actual = adapter.probe(catalog.root, target)
    if actual.get('status') != 'passed':
        raise OperationUnavailable('not_run', actual.get('reason', 'Build toolchain unavailable'))
    require(actual == receipt['toolchain'], 'Build toolchain differs from the firmware receipt')
    layout = adapter.firmware_layout(catalog.root, target, example, variant)
    require(layout == receipt['layout'], 'Firmware inputs, configuration or merge tools changed; rebuild')
    return receipt, digest(path)


def plan(catalog, tid=None, example=None, variant=None, image_format='bin', output=None):
    target, example, variant = choose(catalog, tid, example, variant)
    adapter = supported(catalog, target, 'plan', image_format)
    receipt, receipt_hash = check_receipt(catalog, target, example, variant, adapter)
    document = {'schema_version': 1, 'kind': 'firmware-plan', 'root': str(catalog.root),
                'target': target['id'], 'example': example, 'variant': variant,
                'format': image_format, 'purpose': 'factory', 'receipt_sha256': receipt_hash,
                'build': receipt}
    document['plan_sha256'] = object_digest(document)
    if output is not None:
        atomic_json(output, document, exclusive=True)
    return {'status': 'passed', 'plan': document, 'device_verification': 'not_run'}


def read_plan(path):
    document = read_json(path)
    require(isinstance(document, dict) and document.get('schema_version') == 1 and document.get('kind') == 'firmware-plan', 'Invalid firmware plan schema')
    content = {key: value for key, value in document.items() if key != 'plan_sha256'}
    require(object_digest(content) == document.get('plan_sha256'), 'Firmware plan digest mismatch')
    return document


def merge(plan_path, output):
    from model import Catalog
    document = read_plan(plan_path)
    catalog = Catalog(document['root'])
    with operation_guard(catalog.root, document['target']):
        return merge_checked(catalog, document, output)


def merge_checked(catalog, document, output):
    target, example, variant = choose(catalog, document['target'], document['example'], document['variant'])
    adapter = supported(catalog, target, 'merge', document['format'])
    receipt, receipt_hash = check_receipt(catalog, target, example, variant, adapter)
    require(receipt_hash == document['receipt_sha256'] and receipt == document['build'], 'Build receipt changed after planning')
    require(document['purpose'] == 'factory', 'Unsupported firmware purpose')
    requested = Path(output).expanduser().absolute()
    require(not requested.exists() and not requested.is_symlink(), 'Firmware output already exists')
    output = requested.resolve()
    # Build products must not become locked source inputs in an application.
    if output.is_relative_to(catalog.root):
        require(output.is_relative_to(catalog.root/'.codecraft'), 'In-tree firmware outputs must be under .codecraft/')
    output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='.codecraft-firmware-', dir=output.parent) as folder:
        stage = Path(folder)
        manifest = adapter.firmware_merge(catalog.root, receipt['layout'], stage)
        manifest.update(schema_version=1, kind='firmware-release', target=target['id'],
                        example=example, variant=variant, purpose='factory', format=document['format'],
                        plan_sha256=document['plan_sha256'], build_receipt_sha256=receipt_hash,
                        source_digest=receipt['source_digest'], lock_sha256=receipt['lock_sha256'],
                        toolchain=receipt['toolchain'], device_verification='not_run')
        write_json(stage/'manifest.json', manifest)
        adapter.firmware_verify(stage, manifest)
        # Recheck after native work to reject concurrent builds/source/tool/input drift.
        current, current_hash = check_receipt(catalog, target, example, variant, adapter)
        require(current_hash == receipt_hash and current == receipt, 'Build changed during merge')
        publish(stage, output)
    return {'status': 'passed', 'path': str(output), 'manifest': manifest, 'device_verification': 'not_run'}


def publish(stage, output):
    """Reserve a new directory exclusively; manifest is the final commit marker."""
    output.mkdir()  # Never replace even an empty destination created meanwhile.
    owned = []
    try:
        for name in ('firmware.bin', 'manifest.json'):
            os.link(stage/name, output/name)
            owned.append(output/name)
    except BaseException:
        for path in owned:
            path.unlink()
        if not any(output.iterdir()):
            output.rmdir()
        raise


def verify(catalog, bundle):
    bundle = Path(bundle).resolve()
    manifest = read_json(bundle/'manifest.json')
    require(isinstance(manifest, dict) and manifest.get('schema_version') == 1 and manifest.get('kind') == 'firmware-release', 'Invalid firmware manifest schema')
    target = catalog.target(manifest['target'])
    adapter = supported(catalog, target, 'verify', manifest['format'])
    require(manifest.get('purpose') == 'factory', 'Unsupported firmware purpose')
    adapter.firmware_verify(bundle, manifest)
    return {'status': 'passed', 'path': str(bundle), 'sha256': manifest['image']['sha256'],
            'scope': 'offline-integrity-and-layout', 'device_verification': 'not_run'}
