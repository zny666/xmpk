#!/usr/bin/env python3
import argparse
import copy
from datetime import datetime,timezone
import hashlib
import json
from pathlib import Path
import re
import subprocess

ROOT=Path(__file__).resolve().parents[1]


def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser=argparse.ArgumentParser();parser.add_argument('--original-build',type=Path,required=True);args=parser.parse_args()
    old=json.loads(args.original_build.read_text())
    if old['status']!='reference_build_complete' or len(old['entries'])!=20:raise ValueError('complete original build required')
    out=ROOT/'build'/datetime.now(timezone.utc).strftime('reference-driver-%Y%m%dT%H%M%S-%fZ');out.mkdir();(out/'logs').mkdir()
    result=copy.deepcopy(old);result['entries']=[];result['status']='relinking_measurement_driver';result['commands']=[]
    result['instrumentation_revision']={'previous_manifest':str(args.original_build.relative_to(ROOT)),'previous_sha256':sha(args.original_build),'reason':'Read five-byte perf control ACK including NUL; algorithm object bytes unchanged.','old_driver_sha256':old['source_files']['native/bench_api.c'],'new_driver_sha256':sha(ROOT/'native/bench_api.c')}
    def save():(out/'manifest.json').write_text(json.dumps(result,indent=2)+'\n')
    def run(argv,label):
        p=subprocess.run(argv,cwd=ROOT,capture_output=True,text=True,timeout=300)
        log=out/'logs'/f'{len(result["commands"]):05d}.log';log.write_text(p.stdout+p.stderr)
        result['commands'].append({'label':label,'argv':argv,'returncode':p.returncode,'log':str(log.relative_to(ROOT)),'sha256':sha(log)});save()
        if p.returncode:raise ValueError('driver relink failed '+label+': '+str(log))
        return p.stdout
    save()
    try:
        for old_entry in old['entries']:
            e=copy.deepcopy(old_entry);d=out/(e['algorithm'].lower()+'-'+e['parameter']);d.mkdir()
            driver=next(o for o,s in zip(e['objects'],e['sources']) if s=='native/bench_api.c');before=ROOT/driver;obj=d/'bench_api.o';binary=d/'bench'
            compiler=next(c['argv'] for c in old['commands'] if '-c' in c['argv'] and '-o' in c['argv'] and c['argv'][c['argv'].index('-o')+1]==str(before))
            cmd=compiler[:]
            for option,new in [('-o',obj),('-MF',obj.with_suffix('.d')),('-MT',obj)]:
                if option in cmd:cmd[cmd.index(option)+1]=str(new)
            run(cmd,'recompile-driver-'+e['parameter'])
            link=next(c['argv'] for c in old['commands'] if '-c' not in c['argv'] and '-o' in c['argv'] and c['argv'][c['argv'].index('-o')+1]==str(ROOT/e['binary']))
            link=[str(obj) if x==str(before) else str(binary) if x==str(ROOT/e['binary']) else x for x in link]
            run(link,'relink-'+e['parameter']);e['previous_binary']=e['binary'];e['binary']=str(binary.relative_to(ROOT));e['sha256']=sha(binary)
            e['objects']=[str(obj.relative_to(ROOT)) if x==driver else x for x in e['objects']]
            audits=[]
            for entry in e['object_audit']:
                if entry['object']!=driver:
                    if sha(ROOT/entry['object'])!=entry['sha256']:raise ValueError('algorithm object changed')
                    audits.append(entry)
                else:
                    asm=run(['objdump','-d','--no-show-raw-insn',str(obj)],'driver-instructions-'+e['parameter']);mn=re.findall(r'^\s*[0-9a-f]+:\s+([a-z][a-z0-9_.]*)(?:\s|$)',asm,re.M|re.I)
                    if not mn or any(x.startswith('v') for x in mn):raise ValueError('invalid scalar driver')
                    audits.append({'object':str(obj.relative_to(ROOT)),'sha256':sha(obj),'ISA_attributes':run(['readelf','-A',str(obj)],'driver-ISA'),'decoded_instructions':len(mn),'vector_mnemonics':[]})
            e['object_audit']=audits;e['metadata']=json.loads(run([str(binary),'--metadata'],'driver-metadata-'+e['parameter']).splitlines()[-1])
            if e['metadata']!=old_entry['metadata']:raise ValueError('metadata changed with ACK fix')
            e['build_id']=run(['readelf','-n',str(binary)],'new-buildid');e['dependencies']=run(['ldd',str(binary)],'new-dependencies');e['algorithm_objects_unchanged']=True
            result['entries'].append(e);save();print('Driver relinked',e['algorithm'],e['parameter'],flush=True)
        for name,h in old['source_files'].items():
            if name.startswith('upstream/') or (name.startswith('native/') and name!='native/bench_api.c'):
                if sha(ROOT/name)!=h:raise ValueError('non-instrumentation source changed '+name)
        result['source_files']={str(p.relative_to(ROOT)):sha(p) for group in ['upstream','native','tools'] for p in sorted((ROOT/group).rglob('*')) if p.is_file() and '__pycache__' not in p.parts}
        result['status']='reference_build_complete'
    except BaseException as e:result.update(status='failed',error=repr(e));raise
    finally:save()
    print(out/'manifest.json',flush=True)


if __name__=='__main__':main()
