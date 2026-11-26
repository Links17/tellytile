"""Check native BSP compositions against declared source closure and IDF output."""
from pathlib import Path
import re
import shlex

from graph import SourceGraph
from io_utils import read_json, require, safe_path


def settings(path):
    text=re.sub(r'#[^\n]*','',path.read_text())
    pattern=r'set\((CODECRAFT_BSP_(?:SOURCES|REQUIRES))\s+([^)]*)\)'
    matches=list(re.finditer(pattern,text))
    require(not re.sub(pattern,'',text).strip(),f'{path}: unsupported composition syntax')
    data={}
    for match in matches:
        key,body=match.groups();values=shlex.split(body)
        require(key not in data and values and len(set(values))==len(values),f'{path}: empty or duplicate composition setting')
        expression=r'src/bsp_[a-z0-9_]+\.c' if key.endswith('SOURCES') else r'[a-z][a-z0-9_]*'
        require(all(re.fullmatch(expression,value) for value in values),f'{path}: invalid composition value')
        data[key]=values
    require(set(data)=={'CODECRAFT_BSP_SOURCES','CODECRAFT_BSP_REQUIRES'},f'{path}: incomplete BSP composition')
    return data


def describe(root,target,example):
    root=Path(root).resolve();base=root/target['path'];bsp=base/'bsp'
    folder=base/'examples'/example;data=settings(folder/'bsp.cmake')
    sources={safe_path(root,(bsp/relative).relative_to(root).as_posix()) for relative in data['CODECRAFT_BSP_SOURCES']}
    files=set(sources);pending=[*sources,folder/'main/main.c']
    while pending:
        path=pending.pop()
        for header in re.findall(r'^\s*#include\s+"(bsp_[a-z0-9_]+\.h)"',path.read_text(),re.M):
            candidates=[bsp/'include'/header,bsp/'src'/header]
            candidates=[p for p in candidates if p.is_file()]
            require(len(candidates)==1,f'{example}: missing or ambiguous BSP header {header}')
            selected=candidates[0]
            if header!='bsp_pins.h':
                require(bsp/'src'/header.replace('.h','.c') in sources,
                        f'{example}: BSP header {header} has no selected implementation')
            if selected not in files:
                files.add(selected);pending.append(selected)
    requirements=set(data['CODECRAFT_BSP_REQUIRES'])
    require({'freertos','log'}<=requirements,f'{example}: missing common BSP runtime requirements')
    components=root/'platforms/esp-idf/components'
    local={components/name for name in requirements if (components/name/'CMakeLists.txt').is_file()}
    return {'sources':sources,'files':files,'requirements':requirements,'components':local}


def validate(root,target):
    root=Path(root).resolve();base=root/target['path'];bsp=base/'bsp'
    require(not list(bsp.glob('*.c')) and not list(bsp.glob('*.h')),f'{target["id"]}: private BSP implementation belongs in src/')
    common={bsp/'CMakeLists.txt',bsp/'idf_component.yml',bsp/'include/bsp_pins.h'}
    manifest=re.sub(r'#[^\n]*','',(bsp/'idf_component.yml').read_text()).strip()
    require(manifest=='dependencies: {}',f'{target["id"]}: optional managed dependencies belong to the example')
    for example in target['examples']:
        selected=describe(root,target,example);folder=base/'examples'/example
        main=(folder/'CMakeLists.txt').read_text()
        require('idf_build_set_property(MINIMAL_BUILD ON)' in main,f'{example}: minimal native component selection is required')
        extra={(folder/value).resolve() for value in re.findall(r'\$\{CMAKE_CURRENT_SOURCE_DIR\}/([^"\s]+)',main)}
        require(extra=={bsp,*selected['components']},f'{example}: native component directories differ from BSP requirements')
        require((folder/'main/idf_component.yml').is_file(),f'{example}: managed dependency declaration is required')
        refs=[*target['inputs'],*target['dependencies'],*target['example_inputs'][example],*target['example_dependencies'][example]]
        for interface in target['interfaces']:
            refs+=interface['implementation']+interface.get('example_implementation',{}).get(example,[])
        graph=SourceGraph(root)
        graph.resolve(refs)
        actual={root/name for name in graph.files if name.startswith(str(bsp.relative_to(root))+'/')}
        require(actual==common|selected['files'],f'{example}: declared BSP source closure differs from native composition')
        actual_components={root/'/'.join(Path(name).parts[:4]) for name in graph.files
                           if name.startswith('platforms/esp-idf/components/')}
        require(actual_components==selected['components'],f'{example}: declared component dependencies differ from native composition')
        declared=set(target['example_dependencies'][example])
        require((folder/'main/idf_component.yml').relative_to(root).as_posix() in declared,
                f'{example}: example managed manifest must be a declared dependency')


def verify_build(root,target,example,output):
    selected=describe(root,target,example)
    data=read_json(output/'project_description.json')
    bsp=data['build_component_info']['bsp']
    require({Path(path).resolve() for path in bsp['sources']}==selected['sources'],f'{example}: built BSP sources differ from composition')
    # ESP-IDF expands short managed names into their namespace-qualified names.
    normalize=lambda name:name.removeprefix('espressif__')
    require({normalize(name) for name in bsp['reqs']}=={normalize(name) for name in selected['requirements']},
            f'{example}: built BSP requirements differ from composition')
    components=set(data['build_components'])
    require('main' in components and 'bsp' in components,f'{example}: missing native application/BSP component')
    return {'bsp_sources':sorted(str(path.relative_to(root)) for path in selected['sources']),
            'bsp_requirements':sorted(bsp['reqs']),'build_components':sorted(components)}
