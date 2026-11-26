"""CodeCraft hardware catalog, discovery, generation and evidence gate."""
from __future__ import annotations

import argparse
import ast
from datetime import datetime, timezone
import importlib.util
import hashlib
import json
from pathlib import Path
import re
import subprocess
import sys
import tempfile
from urllib.parse import unquote

from io_utils import ContractError, OperationUnavailable, digest, read_json, require, source_files, object_digest, result
from runtime import run_target, load_adapter, check_envelope, exit_code
from model import Catalog
from project import generate_project, package_project, verify_project, lock_project, build_project
from render import generate
from evidence import device_evidence_dir, validate_evidence
from testing import inventory as test_inventory, run_tests
from markdown_docs import document_links

ROOT = Path(__file__).resolve().parents[2]


def check_documentation(root):
    """Enforce README-only translations and resolve every local Markdown link."""
    # Private applications are validated explicitly, not as reference content.
    excluded = {'projects'} if not (root/'project.yaml').is_file() else set()
    for entry in source_files(root, excluded_dirs=excluded):
        if entry.suffix != '.md':
            continue
        relative = entry.relative_to(root).as_posix()
        require(not entry.name.endswith('.zh_CN.md') or entry.name == 'README.zh_CN.md',
                f'{relative}: only README may have a Chinese counterpart')
        if entry.name == 'README.md':
            translated = entry.with_name('README.zh_CN.md')
            require(translated.is_file(), f'{relative}: missing README Chinese counterpart')
            require('README.zh_CN.md' in entry.read_text() and 'README.md' in translated.read_text(),
                    f'{relative}: missing reciprocal README links')
        elif entry.name == 'README.zh_CN.md':
            require(entry.with_name('README.md').is_file(), f'{relative}: orphan Chinese README')
        for url in document_links(entry.read_text(), relative):
            link=url.split('#')[0]
            if not link or '://' in link or link.startswith(('mailto:','data:')) or any(x in link for x in '<> '):
                continue
            require((entry.parent/unquote(link)).exists(),f'{relative}: broken local link {link}')


def static_checks(c):
    checks = []
    ast.parse((c.root/'codecraft.py').read_text(),filename='codecraft.py')
    from skill_contracts import check_packages
    checks.extend(check_packages(c.root))
    checks.append(result('skills/contracts-and-release-records','passed'))
    for t in c.targets.values():
        load_adapter(c.root, c.platforms[t['platform']]).validate_target(c.root, t)
        checks.append(result(t['id']+'/contract', 'passed'))
    for pattern in ('platforms/*/platform.json', 'devices/*/targets/target.json', 'skills/*/contract.json', 'tools/skill_packages/packages/*.json'):
        for path in c.root.glob(pattern):
            canonical=json.dumps(read_json(path),ensure_ascii=False,indent=2,sort_keys=True)+'\n'
            require(path.read_text()==canonical,f'{path}: noncanonical metadata formatting')
    for area in ('skills','tools','platforms','devices','tests'):
        for path in source_files(c.root/area):
            if area != 'tests':
                require(not {'test', 'tests', '_tests'}.intersection(path.relative_to(c.root/area).parts[:-1]),
                        f'{path.relative_to(c.root)}: maintained tests must live under tests/')
            if path.suffix=='.py':
                ast.parse(path.read_text(),filename=str(path))
    require('Apache License' in (c.root/'LICENSE').read_text() and (c.root/'NOTICE').read_text().strip(), 'Repository license/attribution missing')
    for path in c.root.glob('platforms/*/components/*'):
        if path.is_dir():
            texts=[p.read_text(errors='replace') for p in source_files(path) if p.suffix in ('.h','.c','.md','.txt')]
            require(any('Copyright' in text or 'SPDX-License-Identifier' in text or 'Apache License' in text for text in texts), f'{path}: component attribution missing')
    checks += [result('source/format-and-python-syntax','passed'),result('source/licenses-and-attribution','passed')]
    # Engineering entry points are required once, in English.
    entries = [c.root/'AGENTS.md', c.root/'CONTRIBUTION.md', c.root/'README.md', c.root/'CLAUDE.md', c.root/'devices/AGENTS.md', c.root/'skills/AGENTS.md', c.root/'tools/AGENTS.md', c.root/'platforms/AGENTS.md', c.root/'tests/AGENTS.md']
    entries += list(c.root.glob('devices/*/AGENTS.md')) + list(c.root.glob('platforms/*/AGENTS.md'))
    entries += list(c.root.glob('skills/*/SKILL.md'))
    for entry in entries:
        require(entry.is_file(), f'{entry}: missing required documentation entry')
    check_documentation(c.root)
    for device in c.devices.values():
        for tid in c.device_targets(device['id']):
            c.context(tid)
            c.support(tid)
    drift = generate(c, check=True)
    require(not drift, 'generated files are stale: '+', '.join(drift))
    # Scan source files and untracked additions too; split the pattern so this
    # validator's own source is not mistaken for a private key.
    key = re.compile('-----BEGIN '+'(?:RSA |EC |OPENSSH )?PRIVATE KEY-----')
    for area in ('devices','platforms','skills','tools','tests','docs','.github'):
        for path in source_files(c.root/area):
            if path.suffix in ('.md','.py','.c','.h','.json','.yml','.yaml','.sh'):
                require(not key.search(path.read_text(errors='replace')), f'private-key material in {path.relative_to(c.root)}')
    checks += [result('knowledge/closure','passed'), result('generated/check','passed'), result('documentation/language-and-links','passed'), result('source/private-keys','passed')]
    return checks


def target_receipt(c, tid):
    target=c.target(tid)
    adapter=load_adapter(c.root,c.platforms[target['platform']])
    return {'source_digest':c.graph(tid).fingerprint(),'toolchain':adapter.probe(c.root,target),'expected_cases':adapter.cases(c.root,target)}


def validate_project(c, tid, example=None, variant=None):
    """Build the selected example in a clean external directory via its public entry."""
    target=c.target(tid)
    example=example or target['examples'][0]
    require(example in target['examples'], 'unknown example')
    log_name='generated_'+tid.replace('/','_')+'_'+example
    if target.get('variants') and variant is None:
        projects={name:validate_project(c,tid,example=example,variant=name) for name in target['variants']}
        status='passed' if all(item['status']=='passed' for item in projects.values()) else ('failed' if any(item['status']=='failed' for item in projects.values()) else 'not_run')
        log=c.root/'.codecraft/logs'/(log_name+'.log')
        log.write_text('\n'.join(name+'\n'+(c.root/item['log']).read_text() for name,item in projects.items()))
        return result(tid+'/generated-project',status,'Clean locked exports cover every registered variant.',
                      kind='variant-project-set',example=example,variant_projects=projects,
                      source_digest=object_digest({name:item.get('source_digest') for name,item in projects.items()}),
                      origin_source_digest=c.graph(tid).fingerprint(),log=str(log.relative_to(c.root)),log_sha256=digest(log),
                      build_results=[row for item in projects.values() for row in item.get('build_results',[])],
                      provenance='passed' if all(item.get('provenance')=='passed' for item in projects.values()) else 'failed',
                      toolchain=projects[target['default_variant']].get('toolchain'))
    with tempfile.TemporaryDirectory(prefix='codecraft-generated-check-') as folder:
        output=Path(folder)/'project'
        log=c.root/'.codecraft/logs'/(log_name+('_'+variant if variant is not None else '')+'.log')
        log.parent.mkdir(parents=True,exist_ok=True)
        log.write_text('')
        try:
            generated=generate_project(c,tid,example,output,variant=variant)
            require(verify_project(output)['status']=='passed','fresh project lock mismatch')
            check_documentation(output)
            run=subprocess.run([sys.executable,str(output/'codecraft.py'),'build-project','.'],cwd=output,capture_output=True,text=True,timeout=1900)
            transcript=run.stdout+'\n'+run.stderr
            for child in sorted((output/'.codecraft/logs').glob('*.log')):
                transcript+='\n'+child.name+'\n'+child.read_text(errors='replace')
            log.write_text(transcript)
            response=json.loads(run.stdout)
            require(response.get('schema_version') == 2 and type(response.get('ok')) is bool and isinstance(response.get('data'), dict), 'Invalid public CLI build response')
            payload=response['data']
            require(response['ok'] == payload.get('ok'), 'Public CLI and native build status differ')
            imported=Catalog(output)
            native=load_adapter(output,imported.platforms[target['platform']])
            checks=check_envelope(payload,tid,'build',imported.graph(tid).fingerprint(),native.cases(output,imported.target(tid),example),run.returncode)
            provenance=verify_project(output)
            passed=all(item['status']=='passed' for item in checks) and provenance['status']=='passed'
            firmware_check=None
            if passed and imported.platforms[target['platform']]['operations']['firmware-plan']['supported']:
                plan_path=output/'.codecraft/firmware/gate-plan.json'
                release=output/'.codecraft/firmware/gate-release'
                for arguments in [
                    ['firmware','plan','--project','.', '--output',str(plan_path)],
                    ['firmware','merge','--plan',str(plan_path),'--output',str(release)],
                    ['firmware','verify',str(release)]]:
                    run=subprocess.run([sys.executable,str(output/'codecraft.py'),*arguments],cwd=output,
                                       capture_output=True,text=True,timeout=120)
                    with log.open('a') as stream:
                        stream.write('\nFirmware '+arguments[1]+':\n'+run.stdout+'\n'+run.stderr)
                    response=json.loads(run.stdout)
                    if response.get('data',{}).get('status') in ('not_run','not_supported'):
                        raise OperationUnavailable(response['data']['status'],response['data']['reason'])
                    require(run.returncode==0 and response.get('schema_version')==2 and response.get('ok') is True,
                            'Exported firmware '+arguments[1]+' failed')
                    firmware_check=response['data']
            status='passed' if passed else ('failed' if provenance['drift'] or any(item['status']=='failed' for item in checks) else 'not_run')
            return result(tid+'/generated-project',status,'Clean external import, locked environment, native build and provenance check.',example=example,
                          source_digest=generated['source_digest'],origin_source_digest=generated['origin_source_digest'],variant=generated.get('variant'),
                          log=str(log.relative_to(c.root)),log_sha256=digest(log),build_results=checks,provenance=provenance['status'],toolchain=payload['resolved_toolchain'],firmware=firmware_check)
        except OperationUnavailable as exc:
            with log.open('a') as output_log:
                output_log.write(f'\n{exc.status}: {exc}\n')
            return result(tid+'/generated-project',exc.status,str(exc),log=str(log.relative_to(c.root)),log_sha256=digest(log))
        except (ValueError,KeyError,TypeError,ContractError,OSError,subprocess.TimeoutExpired) as exc:
            with log.open('a') as output_log:
                output_log.write(f'\n{type(exc).__name__}: {exc}\n')
            return result(tid+'/generated-project','failed',str(exc),log=str(log.relative_to(c.root)),log_sha256=digest(log))


def record_evidence(c,source):
    record=read_json(source)
    t=validate_evidence(c,record,current=True)
    commit=subprocess.run(['git','cat-file','-e',record['repository_commit']+'^{commit}'],cwd=c.root,capture_output=True)
    require(commit.returncode==0,'repository_commit does not exist in this repository')
    identity=hashlib.sha256(json.dumps(record,sort_keys=True).encode()).hexdigest()[:16]
    out=device_evidence_dir(c,t)/'records'/f'{record["kind"]}-{identity}.json'
    require(not out.exists(),'evidence record exists; do not overwrite prior observations')
    out.parent.mkdir(parents=True,exist_ok=True)
    out.write_text(json.dumps(record,ensure_ascii=False,indent=2)+'\n')
    return {'path':str(out.relative_to(c.root)),'result':record['result']}


def main(argv=None):
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root',type=Path,default=ROOT)
    parser.add_argument('--version',action='version',version='CodeCraft CLI contract 3')
    sub=parser.add_subparsers(dest='command',required=True)
    sub.add_parser('catalog')
    external=sub.add_parser('check-cloud-skills');external.add_argument('--skills-root',type=Path,help='Installed Skill parent; defaults to this repository’s skills directory')
    inspect=sub.add_parser('context');inspect.add_argument('--target',required=True);inspect.add_argument('--example');inspect.add_argument('--task',default='develop');inspect.add_argument('--skills',type=Path,help='JSON list of explicit Skill selections')
    inspect.add_argument('--variant',help='Declared hardware build variant; physical revision remains separate')
    discover=sub.add_parser('discover');discover.add_argument('--interface',action='append',default=[]);discover.add_argument('--platform');discover.add_argument('--constraints',type=Path)
    gen=sub.add_parser('generate');gen.add_argument('--check',action='store_true')
    val=sub.add_parser('validate');val.add_argument('--static',action='store_true');val.add_argument('--target');val.add_argument('--example');val.add_argument('--projects',action='store_true');val.add_argument('--report',type=Path)
    val.add_argument('--variant',help='Build one declared variant; the full gate builds all registered variants')
    tests=sub.add_parser('test',help='List or run host tests at their owning paths')
    tests.add_argument('--path',help='Repository-relative test file or owning directory')
    tests.add_argument('--list',action='store_true',help='Collect test IDs without executing test bodies')
    new=sub.add_parser('new');new.add_argument('--target',required=True);new.add_argument('--example',required=True);new.add_argument('--output',type=Path,required=True);new.add_argument('--skills',type=Path)
    new.add_argument('--brief',type=Path,help='JSON project brief: name, purpose, target_users, scenarios and bom')
    new.add_argument('--variant',help='Explicit variant selection; otherwise use the declared build default')
    verify=sub.add_parser('verify-project');verify.add_argument('path',type=Path)
    refresh=sub.add_parser('lock-project');refresh.add_argument('path',type=Path)
    build=sub.add_parser('build-project');build.add_argument('path',type=Path)
    pack=sub.add_parser('package');pack.add_argument('path',type=Path);pack.add_argument('--output',type=Path,required=True)
    firmware=sub.add_parser('firmware',help='Plan, merge or verify native firmware images; never flash')
    image_commands=firmware.add_subparsers(dest='image_command',required=True)
    image_plan=image_commands.add_parser('plan')
    image_plan.add_argument('--project',type=Path);image_plan.add_argument('--target');image_plan.add_argument('--example')
    image_plan.add_argument('--variant');image_plan.add_argument('--format',default='bin');image_plan.add_argument('--output',type=Path,required=True)
    image_merge=image_commands.add_parser('merge');image_merge.add_argument('--plan',type=Path,required=True);image_merge.add_argument('--output',type=Path,required=True)
    image_verify=image_commands.add_parser('verify');image_verify.add_argument('bundle',type=Path)
    evidence=sub.add_parser('record-evidence');evidence.add_argument('--file',type=Path,required=True)
    release=sub.add_parser('skill-release',help='Prepare, check or explicitly apply standalone Skill candidates')
    release.add_argument('release_args',nargs=argparse.REMAINDER)
    ci=sub.add_parser('ci',help='Plan affected native CI Targets or check completed CI jobs')
    ci.add_argument('ci_args',nargs=argparse.REMAINDER)
    reference=sub.add_parser('skill-reference',help='Check or regenerate the maintained reCamera SDK API reference')
    reference.add_argument('--skill',required=True,choices=['codecraft-development-recamera-pysdk'])
    reference.add_argument('--sdk-root',type=Path,required=True)
    reference.add_argument('--revision')
    reference_mode=reference.add_mutually_exclusive_group(required=True)
    reference_mode.add_argument('--check',action='store_true')
    reference_mode.add_argument('--write',action='store_true')
    args=parser.parse_args(argv)
    try:
        if args.command=='ci':
            if not (args.root/'tools/codecraft/ci.py').is_file():
                require((args.root/'project.yaml').is_file() and (args.root/'codecraft.lock').is_file(),
                        'Missing repository CI implementation')
                raise OperationUnavailable('not_supported','CI planning requires the full Git repository.')
            import ci as ci_flow
            return ci_flow.main(args.ci_args,root=args.root)
        if args.command=='firmware':
            import firmware as image_flow
            if args.image_command=='plan':
                data=image_flow.plan(Catalog(args.project or args.root),args.target,args.example,args.variant,args.format,args.output)
            elif args.image_command=='merge':
                data=image_flow.merge(args.plan,args.output)
            else:
                data=image_flow.verify(Catalog(args.root),args.bundle)
            print(json.dumps({'schema_version':2,'ok':True,'data':data},ensure_ascii=False,indent=2))
            return 0
        if args.command=='test':
            if args.list:
                data=test_inventory(args.root,selection=args.path)
                code=1 if data['collection_errors'] else 0
            else:
                checks=run_tests(args.root,selection=args.path)
                data={'results':checks,'scope':'host-only','device_verification':'not_run','api_execution':'not_run'}
                code=exit_code(checks)
            print(json.dumps({'schema_version':2,'ok':code==0,'data':data},ensure_ascii=False,indent=2))
            return code
        if args.command=='skill-reference':
            path=args.root/'tools/skill_packages/recamera/api_reference.py'
            if not path.is_file():
                require((args.root/'project.yaml').is_file() and (args.root/'codecraft.lock').is_file(),
                        'Missing repository Skill reference implementation')
                raise OperationUnavailable('not_supported','Skill reference maintenance requires the full repository.')
            spec=importlib.util.spec_from_file_location('codecraft_skill_reference',path)
            module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
            data=module.reference(args.root,args.sdk_root,write=args.write,revision=args.revision)
            ok=data['status']=='passed'
            print(json.dumps({'schema_version':2,'ok':ok,'data':data},ensure_ascii=False,indent=2))
            return 0 if ok else 1
        if args.command=='skill-release':
            path=args.root/'tools/skill_packages/release.py'
            if not path.is_file():
                require((args.root/'project.yaml').is_file() and (args.root/'codecraft.lock').is_file(),
                        'Missing repository Skill release implementation; restore the maintained source before retrying.')
                print(json.dumps({'schema_version':2,'ok':False,'data':{'status':'not_supported','reason':'Skill release maintenance requires the full repository; it is not exported into applications.'}}))
                return 2
            spec=importlib.util.spec_from_file_location('codecraft_skill_release',path)
            module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
            return module.main(args.release_args,root=args.root)
        if args.command=='check-cloud-skills':
            if not (args.root/'tools/codecraft/skill_contracts.py').is_file():
                require((args.root/'project.yaml').is_file() and (args.root/'codecraft.lock').is_file(),
                        'Missing maintained Skill contract implementation')
                from project import profile
                require(not profile(args.root)['skills'], 'Missing verification code for selected Skills; restore the locked source')
                raise OperationUnavailable('not_supported','This native-only application contains no Skill packages or release records to check.')
            from skill_contracts import check_packages
            checks=check_packages(args.root,args.skills_root)
            ok=all(item['status']=='passed' for item in checks)
            print(json.dumps({'schema_version':2,'ok':ok,'data':{'results':checks,'api_execution':'not_run'}},indent=2))
            return 0 if ok else 1
        if args.command=='generate' and (args.root/'project.yaml').is_file():
            raise OperationUnavailable('not_supported','Reference index generation requires the repository; use lock-project to update application guidance.')
        application_commands={'verify-project','lock-project','build-project','package'}
        c=None if args.command in application_commands else Catalog(args.root)
        data={};ok=True;code=0
        if args.command=='catalog':data={'schema_version':3,'devices':c.devices,'platforms':c.platforms,'targets':c.targets}
        elif args.command=='context':
            selections=read_json(args.skills) if args.skills else []
            require(isinstance(selections,list),'Skill selections must be a list')
            data=c.context(args.target,args.example,args.task,selections,variant=args.variant)
        elif args.command=='discover':data=c.resolve(args.interface,args.platform,read_json(args.constraints) if args.constraints else None)
        elif args.command=='generate':
            changed=generate(c,args.check);data={'changed':changed,'check':args.check};ok=not args.check or not changed
        elif args.command=='new':
            selections=read_json(args.skills) if args.skills else []
            require(isinstance(selections,list),'Skill selections must be a list')
            data=generate_project(c,args.target,args.example,args.output,selections,variant=args.variant,brief=read_json(args.brief) if args.brief else None)
        elif args.command=='lock-project':data=lock_project(args.path)
        elif args.command=='build-project':
            data=build_project(args.path);ok=data['ok'];code=exit_code(data['results'])
        elif args.command=='verify-project':data=verify_project(args.path);ok=data['status']=='passed'
        elif args.command=='package':data=package_project(args.path,args.output)
        elif args.command=='record-evidence':data=record_evidence(c,args.file)
        elif args.command=='validate':
            require(not (c.root/'project.yaml').exists() and not (c.root/'codecraft.lock').exists(),
                    'Application builds require `python3 codecraft.py build-project .`; validate checks the reference repository.')
            require(not(args.static and args.target),'choose --static or --target')
            require(not args.example or args.target,'--example requires --target')
            require(not args.variant or args.target,'--variant requires --target')
            checks=[]
            receipts={}
            if args.target:
                receipts[args.target]=target_receipt(c,args.target)
                checks=run_target(c,args.target,args.example,args.variant)
                if args.projects:
                    checks.append(validate_project(c,args.target,example=args.example,variant=args.variant))
            else:
                checks=static_checks(c)+run_tests(c.root)
                if not args.static and all(x['status']=='passed' for x in checks):
                    for tid in c.targets:
                        receipts[tid]=target_receipt(c,tid)
                        checks+=run_target(c,tid)
                        checks.append(validate_project(c,tid))
            ok=all(x['status']=='passed' for x in checks)
            code=1 if any(x['status']=='failed' for x in checks) else (0 if ok else 2)
            data={'scope':'static+host' if args.static else (args.target or 'complete-software-gate'),'results':checks,'device_verification':'not_run','generated_at':datetime.now(timezone.utc).isoformat(),'targets':receipts,'simulation':{pid:p['operations']['simulate'] for pid,p in c.platforms.items()}}
            if args.report:
                args.report.parent.mkdir(parents=True,exist_ok=True)
                args.report.write_text(json.dumps({'schema_version':2,'ok':ok,'data':data},ensure_ascii=False,indent=2)+'\n')
        print(json.dumps({'schema_version':2,'ok':ok,'data':data},ensure_ascii=False,indent=2))
        return code or (0 if ok else 1)
    except OperationUnavailable as exc:
        print(json.dumps({'schema_version':2,'ok':False,'data':{'status':exc.status,'reason':str(exc)}},ensure_ascii=False))
        return 2
    except (ContractError,OSError,ValueError,KeyError,TypeError) as exc:
        print(json.dumps({'schema_version':2,'ok':False,'error':{'type':'contract','message':str(exc)}},ensure_ascii=False))
        return 1
