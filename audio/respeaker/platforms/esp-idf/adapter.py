"""Native esp-idf adapter. No other platform owns these operations."""
from pathlib import Path
import ast
import json
import os
import re
import shutil
import subprocess
import sys
sys.path.insert(0, str(Path(__file__).resolve().parents[2]/'tools/codecraft'))
sys.path.insert(0, str(Path(__file__).resolve().parent))
from io_utils import ContractError, digest, object_digest, read_json, require, result, safe_path
from runtime import command, main, selected
import composition
import variants
from esp_firmware import layout as firmware_layout, merge as firmware_merge, verify as firmware_verify


def cases(root, target, example=None):
    return [variants.case_name(target,name,variant) for name in selected(target, example) for variant in variants.choices(target)]


def build(root, target, example=None):
    path = safe_path(root, target['path'])
    examples = selected(target, example)
    checks = []
    if not shutil.which('idf.py'):
        return [result(name, 'not_run', 'ESP-IDF is not activated.') for name in cases(root, target, example)]
    version = subprocess.run(['idf.py', '--version'], capture_output=True, text=True)
    actual = version.stdout.strip()
    required = target['toolchain']['version']
    if version.returncode or not re.search(r'\bv' + re.escape(required) + r'(?:\b|-)', actual):
        return [result(name, 'not_run', f'Expected ESP-IDF {required}; got {actual!r}') for name in cases(root, target, example)]
    for ex in examples:
        for variant in variants.choices(target):
            output = root / '.codecraft/builds' / target['device'] / ex
            options = []
            if variant is not None:
                output = output/variant
                options = ['-D', 'CODECRAFT_HARDWARE_VARIANT='+variant]
            check=command(root, variants.case_name(target,ex,variant), ['idf.py', '-C', str(path/'examples'/ex), '-B', str(output), *options, 'build'], actual)
            if check['status']=='passed':
                try:
                    check['composition']=composition.verify_build(root,target,ex,output)
                    if variant is not None:
                        sources=composition.describe(root,target,ex)['sources']
                        check['variant']=variants.verify_build(root,target,ex,variant,output,sources)
                except (ContractError, OSError, KeyError, TypeError, ValueError) as exc:
                    check.update(status='failed',reason=f'Native composition verification failed: {exc}')
            check['firmware_selection'] = {'example': ex, 'variant': variant}
            checks.append(check)
    return checks


def probe(root, target):
    if not shutil.which('idf.py'):
        return {'status': 'not_run', 'reason': 'ESP-IDF is not activated.'}
    run = subprocess.run(['idf.py', '--version'], capture_output=True, text=True)
    actual = run.stdout.strip()
    if run.returncode or not re.search(r'\bv' + re.escape(target['toolchain']['version']) + r'(?:\b|-)', actual):
        return {'status': 'not_run', 'reason': f'Expected ESP-IDF {target["toolchain"]["version"]}; got {actual}'}
    sdk = os.environ.get('IDF_PATH')
    if not sdk:
        return {'status': 'not_run', 'reason': 'IDF_PATH is required for toolchain provenance.'}
    def git(*args):
        run = subprocess.run(['git', '-C', sdk, *args], capture_output=True, text=True)
        require(run.returncode == 0, 'Cannot fingerprint ESP-IDF checkout')
        return run.stdout.strip()
    compiler = subprocess.run(['xtensa-esp-elf-gcc', '--version'], capture_output=True, text=True)
    require(compiler.returncode == 0, 'ESP-IDF compiler is not activated')
    return {'status': 'passed', 'version': actual, 'sdk_commit': git('rev-parse', 'HEAD'),
            'sdk_submodules': git('submodule', 'status', '--recursive'),
            'sdk_changes_sha256': object_digest(git('diff', 'HEAD', '--submodule=diff')),
            'compiler': compiler.stdout.splitlines()[0], 'python': sys.version.split()[0]}


def validate_target(root, target):
    composition.validate(root, target)
    variants.validate(root, target)
    require(re.fullmatch(r'\d+\.\d+\.\d+', target['toolchain']['version']), 'ESP-IDF version must be exact')
    path = root/target['path']
    text = (path/'bsp/include/bsp_pins.h').read_text()
    require('SPDX-License-Identifier: Apache-2.0' in text, 'Pins header missing license')
    names = re.findall(r'^#define\s+(BSP_\w+)\b', text, re.M)
    require(len(names) == len(set(names)), 'Duplicate pin definitions')
    for name in target['examples']:
        folder = path/'examples'/name
        require((folder/'sdkconfig.defaults').is_file() and (folder/'dependencies.lock').is_file(), f'{name}: configuration/lock required')
        require(str((folder/'dependencies.lock').relative_to(root)) in target['example_dependencies'][name], f'{name}: lock not declared')
        text = (folder/'CMakeLists.txt').read_text()
        for relative in re.findall(r'\$\{CMAKE_CURRENT_SOURCE_DIR\}/([^"\s]+)', text):
            component = (folder/relative).resolve()
            require(component.is_relative_to(root) and component.exists(), f'{name}: broken native component path {relative}')


if __name__ == '__main__':
    raise SystemExit(main('esp-idf'))
