#!/usr/bin/env python3
import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path
import re
from report_reference import parse_report


def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()


def main():
    parser=argparse.ArgumentParser();parser.add_argument('root',type=Path);args=parser.parse_args();root=args.root.resolve();folder=root/'evidence/symbol-address-review'
    source=json.loads((folder/'source.json').read_text())
    for item in source['files']:
        p=folder/item['path']
        if sha(p)!=item['sha256'] or p.stat().st_size!=item['bytes']:raise ValueError('supplemental text hash mismatch')
    binary=root/source['binary']
    if sha(binary)!=source['binary_sha256']:raise ValueError('ELF identity mismatch')
    segments=[]
    for line in (folder/'elf-segments.txt').read_text().splitlines():
        fields=line.split()
        if fields and fields[0]=='LOAD':segments.append(tuple(int(fields[i],16) for i in [1,2,4]))
    symbols=[]
    for line in (folder/'elf-symbols.txt').read_text().splitlines():
        m=re.match(r'\s*\d+:\s+([0-9a-f]+)\s+(\d+)\s+FUNC\s+LOCAL\s+\S+\s+\S+\s+(\S+)\s*$',line)
        if m:symbols.append((int(m[1],16),int(m[2]),m[3]))
    result={'status':'resolved_from_recorded_mappings_and_ELF','source_record_sha256':sha(folder/'source.json'),'binary':source['binary'],'binary_sha256':source['binary_sha256'],'passes':[],'note':'Raw histograms unchanged. Same-named static function instances use stable ELF symbol addresses; inclusive rows are not blindly added.'}
    for number,item in enumerate(source['passes']):
        p=root/item['path']
        if sha(p/'perf.data')!=item['perf_sha256']:raise ValueError('perf data mismatch')
        maps=[]
        for line in (folder/f'sample-{number}-mappings.txt').read_text().splitlines():
            if not line.endswith('/'+source['binary']):continue
            m=re.search(r'\[(0x[0-9a-f]+)\((0x[0-9a-f]+)\) @ (0x[0-9a-f]+|[0-9a-f]+) ',line)
            if m:maps.append(tuple(int(m[i],16) for i in [1,2,3]))
        if not maps:raise ValueError('missing recorded ELF mapping')
        output={'path':item['path'],'perf_sha256':item['perf_sha256'],'reports':{}}
        for mode in ['self','children']:
            raw=p/f'report-{mode}.txt'
            if sha(raw)!=item[mode+'_sha256']:raise ValueError('raw histogram mismatch')
            original=parse_report(raw.read_text(),mode,str(raw));verbose=parse_report((folder/f'sample-{number}-{mode}-addresses.txt').read_text(),mode)
            if len(original['rows'])!=len(verbose['rows']):raise ValueError('reexport row count changed')
            replacements=[]
            for row,address_row in zip(original['rows'],verbose['rows']):
                for field in ['samples','period','self_pct','overhead_pct','children_pct']:
                    if row.get(field)!=address_row.get(field):raise ValueError('reexport numerical row changed')
                if row['symbol']!='enc_encrypt_pub_ecb':continue
                m=re.match(r'0x([0-9a-f]+)\s+\S+\s+\[[^]]+\]\s+(.+)',address_row['symbol'])
                if not m or m[2]!=row['symbol']:raise ValueError('unexpected verbose symbol')
                address=int(m[1],16);candidates=[]
                if mode=='self':
                    for start,size_,name in symbols:
                        if name==row['symbol'] and start<=address<start+size_:candidates.append((start,size_))
                for base,size,offset in maps:
                    if not base<=address<base+size:continue
                    file_offset=address-base+offset
                    for seg_offset,seg_vaddr,seg_size in segments:
                        if seg_offset<=file_offset<seg_offset+seg_size:
                            virtual=file_offset-seg_offset+seg_vaddr
                            candidates.extend((start,size_) for start,size_,name in symbols if name==row['symbol'] and start<=virtual<start+size_)
                candidates=set(candidates)
                if len(candidates)!=1:raise ValueError('symbol instance not uniquely resolved')
                start,size=next(iter(candidates));replacements.append({'line':row['line'],'dso':row['dso'],'original_symbol':row['symbol'],'symbol':row['symbol']+f' [ELF+0x{start:x}]','ELF_symbol_start':start,'ELF_symbol_bytes':size,'recorded_address':address,'samples':row['samples'],'period':row['period'],'children_pct':row.get('children_pct')})
            output['reports'][mode]={'sha256':sha(raw),'replacements':replacements}
        result['passes'].append(output)
    path=folder/'resolutions.json'
    with path.open('x') as f:json.dump(result,f,indent=2);f.write('\n')
    print(json.dumps({'resolved_passes':len(result['passes']),'resolved_rows':sum(len(x['replacements']) for p in result['passes'] for x in p['reports'].values()),'unchanged_raw_data':True}))


if __name__=='__main__':main()
