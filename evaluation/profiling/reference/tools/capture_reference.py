#!/usr/bin/env python3
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import tarfile

ROOT=Path(__file__).resolve().parents[1]


def sha(path):
    h=hashlib.sha256()
    with path.open('rb') as f:
        for b in iter(lambda:f.read(1048576),b''):h.update(b)
    return h.hexdigest()


def main():
    p=argparse.ArgumentParser();p.add_argument('--build',type=Path,required=True);p.add_argument('--run',type=Path,required=True);args=p.parse_args()
    data=json.loads(args.run.read_text());build=json.loads(args.build.read_text())
    if data['status']!='complete' or len(data['cases'])!=60:raise ValueError('complete 20-by-3 study required before final capture')
    symbols=ROOT/'symbols';symbols.mkdir();bindings=[]
    paths=set();private_library_paths=set()
    for e in build['entries']:
        paths.add((ROOT/e['binary']).resolve())
        for name in e.get('library_dependencies',[]):
            library=Path(name);library=library if library.is_absolute() else ROOT/library
            if not library.resolve().is_relative_to(ROOT):raise ValueError('unmapped private library outside experiment')
            private_library_paths.add(str(library.relative_to(ROOT)))
        for line in e['dependencies'].splitlines():
            for word in line.split():
                if word.startswith('/') and Path(word).is_file():paths.add(Path(word))
    for source in sorted(paths):
        dest=symbols/'sysroot'/str(source).lstrip('/');dest.parent.mkdir(parents=True,exist_ok=True)
        shutil.copyfile(source,dest)
        if sha(source)!=sha(dest):raise ValueError('symbol artifact copy mismatch')
        notes=subprocess.run(['readelf','-n',str(source)],capture_output=True,text=True,timeout=15)
        bindings.append({'original_path':str(source),'local_symfs_path':str(dest.relative_to(ROOT)),'sha256':sha(dest),'readelf_notes':notes.stdout,'readelf_returncode':notes.returncode})
    (symbols/'bindings.json').write_text(json.dumps(bindings,indent=2)+'\n')
    files=[]
    for p in sorted(ROOT.rglob('*')):
        rel=p.relative_to(ROOT)
        if '__pycache__' in rel.parts or rel.parts[0]=='archive' or p.name in ['reference-payload.tar.gz','complete-capture.tar.gz','capture-index.json']:continue
        if rel.parts[0]=='deps' and str(rel) not in private_library_paths:continue
        if p.is_symlink():raise ValueError('unexpected archive symlink '+str(rel))
        if p.is_file():
            if p.name.endswith('.key') or p.name=='key.bin' or p.name.startswith('core.'):raise ValueError('private runtime material in capture')
            if p.suffix=='.data':
                attr=p.parent/'events.txt'
                if not attr.is_file():raise ValueError('sample data without audited event metadata')
                text=attr.read_text()
                if any(x in text for x in ['STACK_USER','REGS_USER','REGS_INTR','PHYS_ADDR','DATA_SRC']):raise ValueError('sensitive sample payload')
            files.append({'path':str(rel),'sha256':sha(p),'bytes':p.stat().st_size})
    index=ROOT/'capture-index.json'
    with index.open('x') as f:json.dump({'files':files,'run':str(args.run.parent.relative_to(ROOT)),'build':str(args.build.relative_to(ROOT)),'symbol_files':len(bindings),'excluded':'runtime keys outside experiment; dependency staging and source archives retained separately on local host'},f,indent=2)
    archive=ROOT/'complete-capture.tar.gz'
    with tarfile.open(archive,'x:gz') as tar:
        for e in files:tar.add(ROOT/e['path'],arcname=e['path'],recursive=False)
        tar.add(index,arcname='_capture.json',recursive=False)
    print(json.dumps({'event':'reference_capture_complete','archive':str(archive),'sha256':sha(archive),'files':len(files),'symbol_bindings':len(bindings),'run':str(args.run.parent.relative_to(ROOT))}),flush=True)


if __name__=='__main__':main()
