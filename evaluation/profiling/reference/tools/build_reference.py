#!/usr/bin/env python3
import argparse
from datetime import datetime,timezone
import hashlib
import json
import os
from pathlib import Path
import platform
import re
import subprocess
import sys

ROOT=Path(__file__).resolve().parents[1]


def sha(path):
    h=hashlib.sha256()
    with path.open('rb') as f:
        for b in iter(lambda:f.read(1048576),b''):h.update(b)
    return h.hexdigest()


def main():
    parser=argparse.ArgumentParser();parser.add_argument('--openssl-prefix',type=Path,required=True);args=parser.parse_args()
    if platform.machine()!='riscv64':raise ValueError('native reference run requires RISC-V target')
    out=ROOT/'build'/datetime.now(timezone.utc).strftime('reference-%Y%m%dT%H%M%S-%fZ');out.mkdir();(out/'logs').mkdir()
    flags=['-std=c11','-D_GNU_SOURCE','-D_DEFAULT_SOURCE','-D_POSIX_C_SOURCE=200809L','-O3','-g','-fno-omit-frame-pointer','-fno-optimize-sibling-calls','-fno-lto','-fno-strict-aliasing','-fno-tree-vectorize','-fno-tree-slp-vectorize','-fstack-usage','-march=rv64gc','-mabi=lp64d']
    env=os.environ.copy()
    for key in ['CFLAGS','CPPFLAGS','LDFLAGS','CPATH','C_INCLUDE_PATH','LIBRARY_PATH','LD_PRELOAD','LD_LIBRARY_PATH']:env.pop(key,None)
    env['LC_ALL']='C';env['PYTHONDONTWRITEBYTECODE']='1';state={'status':'building','commands':[],'entries':[],'flags':flags,'compiler':subprocess.check_output(['gcc','--version'],text=True),'source_lock_sha256':sha(ROOT/'source-lock.json')}
    inputs={str(p.relative_to(ROOT)):sha(p) for folder in ['upstream','native','tools'] for p in sorted((ROOT/folder).rglob('*')) if p.is_file() and '__pycache__' not in p.parts};state['source_files']=inputs
    def save():(out/'manifest.json').write_text(json.dumps(state,indent=2)+'\n')
    def run(argv,label):
        cmd=list(map(str,argv));log=out/'logs'/f'{len(state["commands"]):05d}.log'
        result=subprocess.run(cmd,cwd=ROOT,env=env,capture_output=True,text=True,timeout=600)
        log.write_text(result.stdout+result.stderr);state['commands'].append({'label':label,'argv':cmd,'returncode':result.returncode,'log':str(log.relative_to(ROOT)),'sha256':sha(log)});save()
        if result.returncode:raise RuntimeError('build step failed '+label+': '+str(log))
        return result.stdout
    save()
    try:
        for module_name in ['build_mus','build_qm']:
            module=__import__(module_name)
            entries=module.build(ROOT,out,'gcc',args.openssl_prefix,flags,run)
            for entry in entries:
                binary=ROOT/entry['binary'];entry['sha256']=sha(binary)
                meta=json.loads(run([binary,'--metadata'],'metadata-'+entry['algorithm']+'-'+entry['parameter']).splitlines()[-1]);entry['metadata']=meta
                if (meta['algorithm'],meta['parameter'])!=(entry['algorithm'],entry['parameter']):raise ValueError('adapter identity mismatch')
                sanity=json.loads(run([binary,'--sanity'],'sanity-'+entry['algorithm']+'-'+entry['parameter']).splitlines()[-1]);entry['sanity']=sanity
                if sanity['status']!='PASS':raise ValueError('sanity rejected')
                audit=[]
                for item in entry['objects']:
                    obj=ROOT/item;elf=run(['readelf','-A',obj],'ISA-'+obj.name);asm=run(['objdump','-d','--no-show-raw-insn',obj],'instructions-'+obj.name)
                    mnemonics=re.findall(r'^\s*[0-9a-f]+:\s+([a-z][a-z0-9_.]*)(?:\s|$)',asm,re.M|re.I)
                    if any(x.lower().startswith('v') for x in mnemonics):raise ValueError('vector instruction in reference project object')
                    audit.append({'object':item,'sha256':sha(obj),'ISA_attributes':elf,'decoded_instructions':len(mnemonics),'vector_mnemonics':[]})
                entry['object_audit']=audit;entry['build_id']=run(['readelf','-n',binary],'build-id-'+entry['parameter']);entry['dependencies']=run(['ldd',binary],'dependencies-'+entry['parameter'])
                entry['status']='validated';state['entries'].append(entry);save();print('Validated reference',entry['algorithm'],entry['parameter'],flush=True)
        if len(state['entries'])!=20:raise ValueError('incomplete twenty reference parameter variants')
        for name,h in inputs.items():
            if sha(ROOT/name)!=h:raise ValueError('source changed during build')
        state['status']='reference_build_complete'
    except BaseException as exc:state.update(status='failed',error=repr(exc));raise
    finally:save()
    print(out/'manifest.json',flush=True)


if __name__=='__main__':main()
