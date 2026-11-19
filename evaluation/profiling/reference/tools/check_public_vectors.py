#!/usr/bin/env python3
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess
import tempfile

ROOT=Path(__file__).resolve().parents[1]
HARNESS=r'''
#include "ref_api.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned char *load(const char *p,size_t *n){FILE*f=fopen(p,"rb");if(!f)exit(2);fseek(f,0,SEEK_END);long x=ftell(f);if(x<0||x>20000000)exit(2);*n=x;rewind(f);unsigned char*b=malloc(*n+1);if(fread(b,1,*n,f)!=*n)exit(2);fclose(f);return b;}
int main(int c,char**v){if(c!=4)return 2;size_t pn,sn,mn,on=0;unsigned char*p=load(v[1],&pn),*s=load(v[2],&sn),*m=load(v[3],&mn),*o=malloc(sn+1);if(pn!=ref_public_bytes())return 3;int r=ref_open(o,&on,s,sn,p);if(r||on!=mn||memcmp(o,m,mn))return 4;s[0]^=1;on=0;if(ref_open(o,&on,s,sn,p)==0)return 5;puts("official public KAT signature accepted; corrupted signature rejected");free(p);free(s);free(m);free(o);return 0;}
'''


def main():
    p=argparse.ArgumentParser();p.add_argument('--build',type=Path,required=True);args=p.parse_args();build=json.loads(args.build.read_text())
    if build['status']!='reference_build_complete':raise ValueError('validated build required')
    out=args.build.parent/'public-kat-checks';out.mkdir();source=out/'public_vector_check.c';source.write_text(HARNESS);results=[]
    for e in build['entries']:
        name=e['algorithm'].lower();vectors=[]
        if name=='mayo':vectors=list((ROOT/'upstream/mayo/KAT').glob('*'+e['parameter'].replace('-','_')+'.rsp'))
        if name=='snova':vectors=list((ROOT/'upstream/snova/KAT'/e['parameter']).glob('*.rsp'))
        if name=='mqom':vectors=list((ROOT/'public-kat'/e['parameter']).glob('*.json'))
        if not vectors:results.append({'algorithm':e['algorithm'],'parameter':e['parameter'],'status':'no_matching_official_rsp_available','not_claimed_as_KAT':True});continue
        vector=vectors[0]
        if vector.suffix=='.json':d=json.loads(vector.read_text())
        else:
            d={}
            for line in vector.read_text().splitlines():
                if line.startswith('count = ') and d:break
                if '=' in line:
                    k,v=line.split('=',1);d[k.strip()]=v.strip()
        folder=out/e['parameter'];folder.mkdir()
        for key in ['pk','sm','msg']:(folder/key).write_bytes(bytes.fromhex(d[key]))
        objects=[ROOT/o for o,s in zip(e['objects'],e['sources']) if not s.endswith('/bench_api.c')]
        binary=folder/'verify';argv=['gcc',*build['flags'],'-I'+str(ROOT/'native'),str(source),*map(str,objects),'-Wl,--build-id','-o',str(binary)]
        if name in ['mayo','snova']:pass
        if e.get('library_dependencies'):argv+=list(map(str,e['library_dependencies']))
        argv+=e.get('system_libraries',[])
        compile_result=subprocess.run(argv,capture_output=True,text=True,timeout=120);(folder/'build.log').write_text(compile_result.stdout+compile_result.stderr)
        if compile_result.returncode:raise ValueError('KAT checker build failed '+e['parameter'])
        run=subprocess.run(['taskset','-c','3',str(binary),str(folder/'pk'),str(folder/'sm'),str(folder/'msg')],capture_output=True,text=True,timeout=120)
        (folder/'check.log').write_text(run.stdout+run.stderr)
        results.append({'algorithm':e['algorithm'],'parameter':e['parameter'],'status':'PASS' if run.returncode==0 else 'FAIL','returncode':run.returncode,'public_vector_source':str(vector.relative_to(ROOT)),'source_sha256':hashlib.sha256(vector.read_bytes()).hexdigest(),'verify_binary_sha256':hashlib.sha256(binary.read_bytes()).hexdigest(),'type':'Official known-answer public signature verification, not a deterministic key-generation transcript check.'})
        if run.returncode:raise ValueError('official KAT verification failed '+e['parameter'])
    (out/'result.json').write_text(json.dumps({'results':results,'official_vectors_verified':sum(x['status']=='PASS' for x in results)},indent=2)+'\n');print(json.dumps({'official_vectors_verified':sum(x['status']=='PASS' for x in results),'other_schemes':'functional positive/negative checks only, not mislabeled official KAT'}))


if __name__=='__main__':main()
