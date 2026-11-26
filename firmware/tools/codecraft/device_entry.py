"""Read and edit ordinary Device Markdown without generated regions or sidecars."""
from pathlib import Path
import html
import json
import os
import re

from io_utils import ContractError, parse_json, require
from markdown_docs import (REFERENCE, local_reference, reference_definitions,
                           reference_url, sections)

SECTIONS = ('Controller', 'Connectivity', 'Inputs', 'Outputs', 'Power', 'Accessories')
OWNED = ('Profile', 'Overview', 'Usage', 'Physical layout', 'Specifications',
         'Hardware variants', 'Limitations', 'Source conflicts', 'Physical acceptance', 'Assets', 'Sources')
OPTIONAL = {'Hardware variants', 'Source conflicts', 'Assets'}
FACT_COLUMNS = ('Fact', 'Type', 'Value', 'Unit', 'Status', 'Source', 'Assertion / reason')
PROFILE = {'ID':'id', 'Format':'schema_version', 'Revision':'revision',
           'Maintenance':'maintenance', 'Maturity':'maturity'}


def read_entry(path):
    return Path(path).read_bytes().decode('utf-8')


def cell(value):
    return html.escape(str(value), quote=False).replace('|', '&#124;').replace('\n', '&#10;')


def table(columns, rows):
    return '\n'.join(['| '+' | '.join(columns)+' |', '| '+' | '.join('---' for _ in columns)+' |',
                       *['| '+' | '.join(row)+' |' for row in rows]])


def rows(body, columns, label):
    lines = [line.strip() for line in body.splitlines() if line.strip()]
    require(len(lines) >= 2 and all(line.startswith('|') and line.endswith('|') for line in lines),
            f'{label}: Markdown table required')
    parsed = [[value.strip() for value in line[1:-1].split('|')] for line in lines]
    require(tuple(parsed[0]) == columns and len(parsed[1]) == len(columns)
            and all(re.fullmatch(r':?-{3,}:?', value) for value in parsed[1]), f'{label}: invalid table columns')
    require(all(len(row) == len(columns) for row in parsed[2:]), f'{label}: invalid table row')
    return parsed[2:]


def subsections(body, level, label):
    marker = '#' * level + ' '
    matches = list(re.finditer(r'^'+re.escape(marker)+r'(.+)$', body, re.M))
    require(not body[:matches[0].start()].strip() if matches else not body.strip(), f'{label}: unexpected section text')
    result = {}
    for index, match in enumerate(matches):
        name = match[1].strip()
        require(name not in result, f'{label}: duplicate subsection: {name}')
        result[name] = body[match.end():matches[index+1].start() if index+1 < len(matches) else len(body)].strip()
    return result


def scalar(kind, value, label):
    value = html.unescape(value)
    if kind == 'string':
        return value
    if kind == 'integer':
        require(re.fullmatch(r'\d+', value), f'{label}: invalid integer')
        return int(value)
    if kind == 'number':
        result = parse_json(value, label)
        require(type(result) in (int, float), f'{label}: invalid number')
        return result
    if kind == 'boolean':
        require(value in ('true', 'false'), f'{label}: invalid boolean')
        return value == 'true'
    if kind == 'strings':
        result = parse_json(value, label)
        require(isinstance(result, list) and all(isinstance(item, str) for item in result), f'{label}: string list required')
        return result
    raise ContractError(f'{label}: unsupported fact type: {kind}')


def facts(body, level, definitions, relative):
    result = {}
    for section, content in subsections(body, level, relative).items():
        require(section in SECTIONS, f'{relative}: invalid fact section: {section}')
        for title, kind, value, unit, status, source, assertion in rows(content, FACT_COLUMNS, relative):
            match = re.fullmatch(r'(.+) \(`([a-z][a-z0-9_]*)`\)', title)
            require(match is not None, f'{relative}: fact needs a label and stable ID')
            name, identity = html.unescape(match[1]), match[2]
            require(identity not in result, f'{relative}: duplicate fact: {identity}')
            fact = {'label':name, 'section':section, 'kind':kind, 'status':status}
            if unit != '—':
                fact['unit'] = html.unescape(unit)
            if status == 'unknown':
                require(value == source == '—', f'{relative}: unknown facts cannot assert a value or source')
                fact.update(value=None, reason=html.unescape(assertion))
            else:
                fact.update(value=scalar(kind,value,relative), source={
                    'path':local_reference(relative,reference_url(source,definitions,relative)),
                    'template':html.unescape(assertion)})
            result[identity] = fact
    return result


def read_contract(path):
    text = read_entry(path).replace('\r\n', '\n')
    # Device entry paths have exactly one authoritative location in both repos and exports.
    relative = f'devices/{Path(path).parent.name}/AGENTS.md'
    spans = sections(text, str(path))
    required = set(OWNED) - OPTIONAL
    require(required <= spans.keys(), f'{path}: missing Device Markdown sections: {sorted(required-spans.keys())}')
    definitions = reference_definitions(text, str(path))
    bodies = {name: REFERENCE.sub('',text[start:end]).strip() for name,(_,start,end) in spans.items()}
    for name in OPTIONAL:
        bodies.setdefault(name, '')
    headings = re.findall(r'^# (.+)$', text, re.M)
    require(len(headings) == 1, f'{path}: exactly one device title required')
    data = {'name':headings[0]}
    for key, value in rows(bodies['Profile'], ('Field','Value'), str(path)):
        require(key in PROFILE and PROFILE[key] not in data, f'{path}: invalid or duplicate profile field: {key}')
        data[PROFILE[key]] = scalar('integer', value, str(path)) if key == 'Format' else html.unescape(value)
    require(set(PROFILE.values()) <= data.keys(), f'{path}: incomplete device profile')
    for field, heading in [('overview','Overview'),('scenarios','Usage'),('physical_description','Physical layout')]:
        data[field] = bodies[heading]
    data['facts'] = facts(bodies['Specifications'],3,definitions,relative)
    data['variants'] = {}
    if bodies['Hardware variants']:
        for heading, content in subsections(bodies['Hardware variants'],3,relative).items():
            match = re.fullmatch(r'([a-z][a-z0-9_-]*): (.+)',heading)
            require(match is not None, f'{path}: invalid hardware variant heading')
            source, separator, rest = content.partition('\n\n')
            require(source.startswith('Source: ') and separator, f'{path}: variant source required')
            data['variants'][match[1]] = {'label':match[2],
                'source':local_reference(relative,reference_url(source[len('Source: '):],definitions,relative)),
                'facts':facts(rest,4,definitions,relative)}
    unknowns = bodies['Limitations']
    require(unknowns == 'None recorded.' or all(line.startswith('- ') for line in unknowns.splitlines()), f'{path}: limitations must be a list')
    data['unknowns'] = [] if unknowns == 'None recorded.' else [line[2:] for line in unknowns.splitlines()]
    data['conflicts'] = [{'source':local_reference(relative,reference_url(source,definitions,relative)),
                          'resolution':html.unescape(resolution)}
                         for source,resolution in (rows(bodies['Source conflicts'],('Source','Resolution'),relative) if bodies['Source conflicts'] else [])]
    acceptance = subsections(bodies['Physical acceptance'],3,relative)
    require('Preconditions' in acceptance and len(acceptance) > 1, f'{path}: acceptance requires preconditions and cases')
    cases = []
    for identity, content in acceptance.items():
        if identity == 'Preconditions':
            continue
        metadata, separator, procedure = content.partition('\n\n')
        lines = metadata.splitlines()
        require(separator and len(lines) == 2 and lines[0].startswith('- Interfaces: ')
                and lines[1].startswith('- Source: '), f'{path}: invalid acceptance case scope: {identity}')
        procedure, separator, expected = procedure.rpartition('\n\nExpected: ')
        require(separator and procedure.strip() and expected.strip(), f'{path}: acceptance procedure and expected result required')
        interfaces = lines[0][len('- Interfaces: '):]
        cases.append({'id':identity, 'interfaces':[] if interfaces == 'none' else interfaces.split(', '),
            'procedure':procedure, 'expected':expected,
            'source':local_reference(relative,reference_url(lines[1][len('- Source: '):],definitions,relative))})
    data['acceptance'] = {'preconditions':'' if acceptance['Preconditions'] == 'None.' else acceptance['Preconditions'], 'cases':cases}
    data['assets'] = []
    for file, origin, license, sha256, revision, width, height, alpha in (rows(bodies['Assets'],('File','Origin','License','SHA256','Revision','Width','Height','Alpha'),relative) if bodies['Assets'] else []):
        data['assets'].append({'path':local_reference(relative,reference_url(file,definitions,relative)),
            'source':html.unescape(origin),'license':html.unescape(license),'sha256':sha256,'revision':html.unescape(revision),
            'width':scalar('integer',width,relative),'height':scalar('integer',height,relative),'alpha':scalar('boolean',alpha,relative)})
    data['references'],data['vendor_sources'] = [],[]
    for line in bodies['Sources'].splitlines():
        require(line.startswith('- '), f'{path}: Sources must be named references')
        url = reference_url(line[2:],definitions,relative)
        group = data['vendor_sources'] if url.startswith('https://') else data['references']
        value = url if url.startswith('https://') else local_reference(relative,url)
        require(value not in group, f'{path}: duplicate source: {value}')
        group.append(value)
    return data


def source_notes(text, label):
    """Facts must be substantiated outside the specification/acceptance tables."""
    spans = sections(text, label)
    for name,(start,_,end) in reversed(list(spans.items())):
        if name in OWNED:
            text = text[:start]+text[end:]
    return text


def document_sections(data, relative, definitions):
    """Render only explicit fields; preserve separately authored guidance."""
    allowed = {'name', *PROFILE.values(), 'overview','scenarios','physical_description', 'facts','variants',
               'unknowns','conflicts','acceptance','assets','references','vendor_sources'}
    require(set(data) == allowed, f'{relative}: missing or unsupported Device fields: {sorted(set(data)^allowed)}')
    def ref(value):
        url = os.path.relpath(value,Path(relative).parent) if not value.startswith('https://') else value
        if value == relative:
            url += '#hardware-notes'
        for key,current in definitions.items():
            if current == url:
                return f'[{key}][{key}]'
        stem = 'vendor' if value.startswith('https://') else Path(value).stem.replace('_','-').lower()
        if value == relative:stem='device-notes'
        key=stem;number=2
        while key in definitions:
            key=f'{stem}-{number}';number+=1
        definitions[key]=url
        return f'[{key}][{key}]'
    def fact_table(entries, level):
        blocks=[]
        for section in SECTIONS:
            values=[]
            for identity,fact in entries.items():
                if fact['section'] != section:continue
                unknown=fact['status']=='unknown'
                value=fact['value']
                rendered = json.dumps(value,ensure_ascii=False) if isinstance(value,(list,bool)) else str(value)
                values.append([f'{cell(fact["label"])} (`{identity}`)',fact['kind'],'—' if unknown else cell(rendered),
                    cell(fact.get('unit','—')),fact['status'],'—' if unknown else ref(fact['source']['path']),
                    cell(fact['reason'] if unknown else fact['source']['template'])])
            if values:blocks.append('#'*level+' '+section+'\n\n'+table(FACT_COLUMNS,values))
        return '\n\n'.join(blocks)
    bodies={'Profile':table(('Field','Value'),[[name,cell(data[key])] for name,key in PROFILE.items()]),
            'Overview':data['overview'],'Usage':data['scenarios'],'Physical layout':data['physical_description'],
            'Specifications':fact_table(data['facts'],3),'Hardware variants':'',
            'Limitations':'\n'.join('- '+item for item in data['unknowns']) or 'None recorded.',
            'Source conflicts':table(('Source','Resolution'),[[ref(item['source']),cell(item['resolution'])] for item in data['conflicts']]) if data['conflicts'] else ''}
    if data['variants']:
        bodies['Hardware variants']='\n\n'.join('### '+name+': '+item['label']+'\n\nSource: '+ref(item['source'])+'\n\n'+fact_table(item['facts'],4)
                                                    for name,item in data['variants'].items())
    acceptance=data['acceptance']
    bodies['Physical acceptance']='### Preconditions\n\n'+(acceptance['preconditions'] or 'None.')
    for item in acceptance['cases']:
        bodies['Physical acceptance']+='\n\n### '+item['id']+'\n\n- Interfaces: '+(', '.join(item['interfaces']) or 'none')+'\n- Source: '+ref(item['source'])+'\n\n'+item['procedure']+'\n\nExpected: '+item['expected']
    require(isinstance(data['assets'],list),f'{relative}: assets must be a list')
    bodies['Assets']=table(('File','Origin','License','SHA256','Revision','Width','Height','Alpha'),[
        [ref(item['path']),*[cell(item[key]) for key in ('source','license','sha256','revision','width','height')],str(item['alpha']).lower()] for item in data['assets']]) if data['assets'] else ''
    bodies['Sources']='\n'.join('- '+ref(value) for value in [*data['references'],*data['vendor_sources']])
    return bodies


def write_contract(path, data):
    """Update managed Markdown sections without altering the surrounding guidance."""
    path=Path(path);text=read_entry(path);relative=f'devices/{path.parent.name}/AGENTS.md'
    spans=sections(text,str(path));definitions=reference_definitions(text,str(path))
    bodies=document_sections(data,relative,definitions)
    newline='\r\n' if '\r\n' in text else '\n'
    for name,(start,_,end) in reversed(list(spans.items())):
        if name in bodies:
            block = ('## '+name+'\n\n'+bodies[name]+'\n\n').replace('\n',newline) if bodies[name] else ''
            text=text[:start]+block+text[end:]
    for name in OWNED:
        if name not in spans and bodies[name]:
            location=text.index('## Sources')
            text=text[:location]+('## '+name+'\n\n'+bodies[name]+'\n\n').replace('\n',newline)+text[location:]
    text=re.sub(r'^# [^\r\n]+',lambda _:'# '+data['name'],text,count=1,flags=re.M)
    # Definitions are scoped to the document, referenced from any section.
    text=REFERENCE.sub('',text).rstrip()+newline+newline
    text+=newline.join(f'[{key}]: {value}' for key,value in definitions.items())+newline
    path.write_bytes(text.encode('utf-8'))
