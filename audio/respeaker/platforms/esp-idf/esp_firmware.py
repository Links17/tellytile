"""ESP-IDF factory image layout, native merge and independent content checks.

Format authorities are the activated SDK's components/partition_table/
gen_esp32part.py and esptool's bin_image.py/cmds.py. Flash addresses are
programming offsets from flasher_args.json, never ELF virtual addresses.
"""
from contextlib import redirect_stdout
import hashlib
import importlib.util
import io
import os
from pathlib import Path
import re
import sys

from io_utils import (ContractError, OperationUnavailable, digest, object_digest,
                      read_json, require, safe_path)
from device_entry import read_contract


def native_tools():
    try:
        import esptool
        import esptool.bin_image
        import esptool.cmds
    except ImportError as exc:
        raise OperationUnavailable('not_run', 'Activate ESP-IDF Python with esptool installed') from exc
    sdk = os.environ.get('IDF_PATH')
    path = Path(sdk)/'components/partition_table/gen_esp32part.py' if sdk else None
    if path is None or not path.is_file():
        raise OperationUnavailable('not_run', 'Activate ESP-IDF with its native partition-table tool')
    if esptool.__version__.split('.')[0] != '5':
        raise OperationUnavailable('not_supported', 'Factory merge currently supports esptool 5.x')
    spec = importlib.util.spec_from_file_location('firmware_partition_table', path)
    partitions = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(partitions)
    package = Path(esptool.__file__).parent
    identity = {'esptool_version': esptool.__version__,
                'esptool_sha256': object_digest({str(p.relative_to(package)): digest(p) for p in sorted(package.rglob('*.py'))}),
                'partition_tool_sha256': digest(path), 'python': sys.version.split()[0]}
    return esptool, partitions, identity


def file_record(root, path):
    path = Path(path)
    require(path.is_relative_to(root) and path.resolve().is_relative_to(root), 'Firmware input escapes build root')
    require(not any(p.is_symlink() for p in [path, *path.parents] if p.is_relative_to(root)), 'Firmware inputs must not use symlinks')
    require(path.is_file() and path.stat().st_size > 0, f'Missing or empty firmware input: {path}')
    require(path.stat().st_size <= 256*1024*1024, 'Firmware input exceeds supported image size')
    return {'path': path.relative_to(root).as_posix(), 'size': path.stat().st_size, 'sha256': digest(path)}


def image_check(tool, chip, data):
    try:
        with redirect_stdout(io.StringIO()):
            image = tool.bin_image.LoadFirmwareImage(chip, data)
        require(image.chip_id == tool.CHIP_DEFS[chip].IMAGE_CHIP_ID, 'Firmware image chip mismatch')
        require(image.checksum == image.calculate_checksum(), 'Firmware image checksum mismatch')
        if not image.append_digest:
            raise OperationUnavailable('not_supported', 'Factory image verification requires native SHA-256 image digests')
        require(image.stored_digest == image.calc_digest, 'Firmware image SHA-256 mismatch')
        require(len(data) == image.data_length + 32, 'Unexpected signed, padded or trailing image data')
        return image
    except (OperationUnavailable, ContractError):
        raise
    except Exception as exc:
        raise ContractError(f'Invalid native firmware image: {exc}') from exc


def partition_entries(module, data, offset, capacity):
    # The SDK parser validates magic, MD5, native alignment and table semantics.
    module.offset_part_table = offset
    try:
        with redirect_stdout(io.StringIO()):
            table = module.PartitionTable.from_binary(data)
            table.verify()
    except Exception as exc:
        raise ContractError(f'Invalid native partition table: {exc}') from exc
    require(table, 'Empty partition table')
    entries = []
    for item in table:
        require(item.size > 0 and item.offset + item.size <= capacity, 'Partition exceeds configured Flash capacity')
        if item.encrypted:
            raise OperationUnavailable('not_supported', 'Encrypted partitions need a separate image policy')
        entries.append({'name': item.name, 'type': item.type, 'subtype': item.subtype,
                        'offset': item.offset, 'size': item.size})
    return entries


def physical_capacity(root, target, capacity):
    # Device.facts.flash owns the sourced physical limit. A compiled Flash size
    # is configuration, not a measurement; unresolved Device facts remain unknown.
    fact = read_contract(root/'devices'/target['device']/'AGENTS.md')['facts'].get('flash', {})
    if fact.get('status') == 'unknown' or fact.get('value') is None:
        return {'status': 'unknown', 'reason': 'Physical Flash capacity is unresolved in Device facts.'}
    require(fact.get('unit') == 'MB' and type(fact['value']) is int, 'Unsupported Device Flash capacity unit')
    limit = fact['value']*1024*1024
    require(capacity <= limit, 'Configured Flash capacity exceeds the sourced Device limit')
    return {'status': fact['status'], 'bytes': limit, 'source': fact['source']}


def validate_layout(layout):
    require(isinstance(layout, dict) and layout.get('schema_version') == 1 and layout.get('address_space') == 'flash', 'Invalid native firmware layout')
    require(layout.get('format') == 'bin' and layout.get('purpose') == 'factory' and layout.get('fill') == 255,
            'Unsupported native image format/purpose/fill')
    segments = layout['segments']
    require(isinstance(segments, list) and len(segments) >= 3, 'Incomplete native flash inputs')
    require(all(isinstance(s, dict) and s.get('role') in ('bootloader', 'partition-table', 'app', 'data') for s in segments), 'Invalid firmware segment role')
    require([s['offset'] for s in segments] == sorted(s['offset'] for s in segments), 'Unsorted firmware segments')
    capacity = layout['flash_capacity']
    require(type(capacity) is int and 0 < capacity <= 256*1024*1024, 'Invalid configured Flash capacity')
    end = layout['base_address']
    require(type(end) is int and end >= 0, 'Invalid firmware base address')
    for segment in segments:
        offset, size = segment['offset'], segment['size']
        require(type(offset) is int and type(size) is int and size > 0, 'Invalid firmware segment range')
        # esptool ESPLoader.FLASH_SECTOR_SIZE and SDK partition-table sector: 4 KiB.
        require(offset >= end and offset % 4096 == 0, 'Overlapping or unaligned firmware segment')
        end = offset + size
        require(end <= capacity, 'Firmware segment exceeds configured Flash capacity')
        if segment['role'] == 'bootloader':
            require(offset == layout['base_address'] and end <= layout['partition_table_offset'], 'Bootloader exceeds reserved region')
        elif segment['role'] == 'partition-table':
            require(offset == layout['partition_table_offset'] and size <= 4096, 'Invalid partition table placement')
        else:
            matches = [p for p in layout['partitions'] if p['offset'] == offset and end <= p['offset']+p['size']]
            require(len(matches) == 1, 'Firmware segment is outside its partition')
            require((segment['role'] == 'app') == (matches[0]['type'] == 0), 'Application/data partition type mismatch')
    for role in ('bootloader', 'partition-table'):
        require(sum(s['role'] == role for s in segments) == 1, 'Duplicate or missing '+role)
    require(any(s['role'] == 'app' for s in segments), 'Missing application image')
    require(layout['write_range'] == {'start': layout['base_address'], 'end': end}, 'Invalid factory write range')


def layout(root, target, example, variant=None):
    root = Path(root).resolve()
    build = root/'.codecraft/builds'/target['device']/example
    if variant is not None:
        build /= variant
    meta = read_json(build/'flasher_args.json')
    config = read_json(build/'config/sdkconfig.json')
    description = read_json(build/'project_description.json')
    chip = meta['extra_esptool_args']['chip']
    require(chip == target['toolchain']['target'] == config['IDF_TARGET'] == description['target'], 'Native build chip differs from Target')
    for flag in ('SECURE_BOOT', 'SECURE_FLASH_ENC_ENABLED', 'SECURE_SIGNED_APPS_NO_SECURE_BOOT',
                 'SECURE_SIGNED_ON_BOOT_NO_SECURE_BOOT', 'SECURE_SIGNED_ON_UPDATE_NO_SECURE_BOOT'):
        if config.get(flag):
            raise OperationUnavailable('not_supported', 'Signed/encrypted firmware needs a separate image policy')
    tool, partitions, tools = native_tools()
    require(chip in tool.CHIP_DEFS, 'Unknown native chip')
    settings = meta['flash_settings']
    require(set(settings) == {'flash_mode', 'flash_freq', 'flash_size'}, 'Incomplete native flash settings')
    for name, key in [('flash_mode', 'ESPTOOLPY_FLASHMODE'), ('flash_freq', 'ESPTOOLPY_FLASHFREQ'), ('flash_size', 'ESPTOOLPY_FLASHSIZE')]:
        require(settings[name] == config[key], 'Native Flash settings differ from compiled configuration')
    match = re.fullmatch(r'([1-9][0-9]*)MB', settings['flash_size'])
    require(match is not None, 'Explicit Flash capacity is required')
    capacity = int(match[1])*1024*1024
    require(0 < capacity <= 256*1024*1024, 'Invalid configured Flash capacity')
    physical = physical_capacity(root, target, capacity)
    files = meta['flash_files']
    require(isinstance(files, dict) and files, 'Missing native flash file map')
    for entry in meta.values():
        if isinstance(entry, dict) and 'encrypted' in entry and entry['encrypted'] not in (False, 'false'):
            raise OperationUnavailable('not_supported', 'Encrypted/unknown input policy is unsupported')
    roles = {}
    for role in ('bootloader', 'partition-table', 'app'):
        entry = meta[role]
        if entry.get('encrypted') not in (False, 'false'):
            raise OperationUnavailable('not_supported', 'Encrypted/unknown input policy is unsupported')
        require(files.get(entry['offset']) == entry['file'], 'Native role does not match flash file map')
        roles[entry['offset']] = role
    segments = []
    for address, relative in files.items():
        path = safe_path(build, relative)
        segments.append({**file_record(root, path), 'offset': int(address, 0), 'role': roles.get(address, 'data')})
    segments.sort(key=lambda segment: segment['offset'])
    table = next(s for s in segments if s['role'] == 'partition-table')
    offset = config['PARTITION_TABLE_OFFSET']
    require(table['offset'] == offset, 'Partition table offset differs from configuration')
    entries = partition_entries(partitions, (root/table['path']).read_bytes(), offset, capacity)
    # Every native flash input must have a corresponding partition or reserved region.
    for segment in segments:
        if segment['role'] == 'data' and any(p['type'] == 0 and p['offset'] == segment['offset'] for p in entries):
            segment['role'] = 'app'
        if segment['role'] in ('bootloader', 'app'):
            image_check(tool, chip, (root/segment['path']).read_bytes())
    native_config = Path(description['config_file'])
    if not native_config.is_absolute():
        native_config = root/native_config
    metadata = [file_record(root, p) for p in [build/'flasher_args.json', build/'config/sdkconfig.json',
                build/'project_description.json', build/'CMakeCache.txt', native_config]]
    base = tool.CHIP_DEFS[chip].BOOTLOADER_FLASH_OFFSET
    document = {'schema_version': 1, 'chip': chip, 'address_space': 'flash', 'format': 'bin',
                'purpose': 'factory', 'base_address': base, 'flash_capacity': capacity,
                'partition_table_offset': offset, 'flash_settings': settings,
                'physical_flash': physical,
                'fill': 255, 'segments': segments, 'partitions': entries, 'metadata': metadata, 'tools': tools,
                'write_range': {'start': base, 'end': max(s['offset']+s['size'] for s in segments)}}
    validate_layout(document)
    return document


def checked_bytes(root, record):
    path = safe_path(root, record['path'])
    require(file_record(root, path) == {k: record[k] for k in ('path', 'size', 'sha256')}, 'Firmware input changed')
    data = path.read_bytes()
    require(len(data) == record['size'] and hashlib.sha256(data).hexdigest() == record['sha256'], 'Firmware input changed while reading')
    return data


def normalized_bootloader(tool, chip, original, settings):
    """Allow only native boot-header bytes 2/3 and their appended SHA to change.

    Source: esptool 5 cmds._update_image_flash_params and bin_image image format.
    This is an independent content check, not a second invocation of merge_bin.
    """
    image = image_check(tool, chip, original)
    chip_class = tool.CHIP_DEFS[chip]
    expected = bytearray(original)
    expected[2] = tool.cmds.FLASH_MODES[settings['flash_mode']]
    expected[3] = chip_class.parse_flash_size_arg(settings['flash_size']) | chip_class.parse_flash_freq_arg(settings['flash_freq'])
    expected[image.data_length:image.data_length+32] = hashlib.sha256(expected[:image.data_length]).digest()
    return bytes(expected)


def merge(root, document, stage):
    validate_layout(document)
    tool, _, identity = native_tools()
    require(identity == document['tools'], 'Native merge tool changed')
    for record in document['metadata']:
        checked_bytes(root, record)
    inputs = [(s, checked_bytes(root, s)) for s in document['segments']]
    # Bytes are immutable snapshots, so the native tool cannot reopen drifting files.
    try:
        with redirect_stdout(io.StringIO()):
            output = tool.cmds.merge_bin([(s['offset'], data) for s, data in inputs], document['chip'],
                                        flash_mode=document['flash_settings']['flash_mode'],
                                        flash_freq=document['flash_settings']['flash_freq'],
                                        flash_size=document['flash_settings']['flash_size'],
                                        target_offset=document['base_address'])
    except Exception as exc:
        raise ContractError(f'Native firmware merge failed: {exc}') from exc
    require(isinstance(output, bytes) and len(output) == document['write_range']['end']-document['base_address'], 'Merged image length mismatch')
    regions = []
    cursor = 0
    for segment, original in inputs:
        start = segment['offset']-document['base_address']
        require(output[cursor:start] == b'\xff'*(start-cursor), 'Merged padding differs from Flash erase value')
        expected = normalized_bootloader(tool, document['chip'], original, document['flash_settings']) if segment['role'] == 'bootloader' else original
        actual = output[start:start+len(expected)]
        require(actual == expected, 'Merged segment content mismatch')
        regions.append({'offset': segment['offset'], 'size': len(actual), 'sha256': hashlib.sha256(actual).hexdigest(), 'role': segment['role']})
        cursor = start + len(expected)
    (stage/'firmware.bin').write_bytes(output)
    return {'image': {'file': 'firmware.bin', 'size': len(output), 'sha256': hashlib.sha256(output).hexdigest()},
            'layout': document, 'regions': regions}


def verify(bundle, manifest):
    document = manifest['layout']
    validate_layout(document)
    require(manifest['image']['file'] == 'firmware.bin', 'Unexpected release image path')
    require(manifest.get('format') == 'bin' and manifest.get('purpose') == 'factory', 'Unexpected release format/purpose')
    require(set(p.name for p in bundle.iterdir()) == {'firmware.bin', 'manifest.json'}, 'Incomplete or unexpected release files')
    require(not (bundle/'firmware.bin').is_symlink() and not (bundle/'manifest.json').is_symlink(), 'Release files must not be symlinks')
    require((bundle/'firmware.bin').stat().st_size == document['write_range']['end']-document['base_address'], 'Release image size mismatch')
    image = (bundle/'firmware.bin').read_bytes()
    require(len(image) == manifest['image']['size'] == document['write_range']['end']-document['base_address'], 'Release image size mismatch')
    require(hashlib.sha256(image).hexdigest() == manifest['image']['sha256'], 'Release image SHA-256 mismatch')
    tool, partitions, _ = native_tools()
    require(document['chip'] in tool.CHIP_DEFS, 'Unknown release chip')
    require(document['base_address'] == tool.CHIP_DEFS[document['chip']].BOOTLOADER_FLASH_OFFSET, 'Wrong native image base address')
    require(len(manifest['regions']) == len(document['segments']), 'Release region coverage mismatch')
    cursor = 0
    for region, segment in zip(manifest['regions'], document['segments']):
        require(all(region[key] == segment[key] for key in ('offset', 'size', 'role')), 'Release segment metadata mismatch')
        start = region['offset']-document['base_address']
        data = image[start:start+region['size']]
        require(image[cursor:start] == b'\xff'*(start-cursor), 'Release padding mismatch')
        require(hashlib.sha256(data).hexdigest() == region['sha256'], 'Release segment hash mismatch')
        if region['role'] in ('bootloader', 'app'):
            image_check(tool, document['chip'], data)
            if region['role'] == 'bootloader':
                require(normalized_bootloader(tool, document['chip'], data, document['flash_settings']) == data, 'Bootloader Flash settings mismatch')
        if region['role'] != 'bootloader':
            require(region['sha256'] == segment['sha256'], 'Release input hash mismatch')
        if region['role'] == 'partition-table':
            require(partition_entries(partitions, data, document['partition_table_offset'], document['flash_capacity']) == document['partitions'], 'Release partition metadata mismatch')
        cursor = start + len(data)
