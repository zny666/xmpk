#!/usr/bin/env python3
from datetime import datetime,timezone
import hashlib
import json
from pathlib import Path
import tarfile

ROOT=Path(__file__).resolve().parents[1]


def sha(path):
    h=hashlib.sha256()
    with path.open('rb') as f:
        for b in iter(lambda:f.read(1048576),b''):h.update(b)
    return h.hexdigest()


def main():
    archive=ROOT/'archive/delivery-complete.tar.gz';manifest_path=ROOT/'archive/delivery-file-manifest.json';receipt_path=ROOT/'archive/delivery-receipt.json'
    if any(p.exists() for p in [archive,manifest_path,receipt_path]):raise FileExistsError('delivery artifacts already exist')
    verification=json.loads((ROOT/'results/final-verification.json').read_text())
    if verification['status']!='complete_local_data_and_reports' or verification['coverage']['complete_cases']!=60:raise ValueError('complete local report required')
    files=[]
    exclude={'archive/complete-capture.tar.gz','archive/reference-payload.tar.gz'}
    for path in sorted(ROOT.rglob('*')):
        rel=path.relative_to(ROOT)
        if '__pycache__' in rel.parts or str(rel) in exclude:continue
        if path.is_symlink():raise ValueError('unexpected delivery symlink')
        if not path.is_file():continue
        if rel.parts[0] in ['upstream','build','raw']:continue
        if path.name.endswith('.key') or path.name=='key.bin' or path.name.startswith('core.'):raise ValueError('private runtime file')
        with path.open('rb') as f:prefix=f.read(8)
        if prefix==b'R20KEY\x01\0' or prefix[:4]==b'AOK1':raise ValueError('private experiment key envelope')
        files.append({'path':str(rel),'bytes':path.stat().st_size,'sha256':sha(path)})
    manifest={'scope':'Complete local delivery; extracted captured upstream sources included once under results/received. Original official packages retained. Redundant transport tarballs remain on disk but are not nested into this package.','files':files,'private_runtime_keys':0,'raw_capture_sha256':verification['raw_capture_sha256'],'latest_detailed_report':'results/received/reports/final-v2/REPORT.md','overview':'reports/SUMMARY.md'}
    manifest_path.write_text(json.dumps(manifest,indent=2)+'\n')
    with tarfile.open(archive,'x:gz',compresslevel=5) as tar:
        for item in files:tar.add(ROOT/item['path'],arcname=item['path'],recursive=False)
        tar.add(manifest_path,arcname='archive/delivery-file-manifest.json',recursive=False)
    expected={item['path']:item for item in files};seen=set()
    with tarfile.open(archive) as tar:
        for member in tar:
            if not member.isfile() or member.name.startswith('/') or '..' in Path(member.name).parts:raise ValueError('invalid archive member')
            if member.name in seen:raise ValueError('duplicate archive member')
            seen.add(member.name);stream=tar.extractfile(member);h=hashlib.sha256()
            for data in iter(lambda:stream.read(1048576),b''):h.update(data)
            if member.name=='archive/delivery-file-manifest.json':
                if h.hexdigest()!=sha(manifest_path):raise ValueError('manifest bytes mismatch')
            else:
                item=expected[member.name]
                if h.hexdigest()!=item['sha256'] or member.size!=item['bytes']:raise ValueError('delivery file bytes mismatch')
    if seen!=set(expected)|{'archive/delivery-file-manifest.json'}:raise ValueError('delivery inventory incomplete')
    receipt={'status':'complete_archive_roundtrip_verified','checked_utc':datetime.now(timezone.utc).isoformat(),'archive':'archive/delivery-complete.tar.gz','archive_sha256':sha(archive),'archive_bytes':archive.stat().st_size,'files_verified':len(seen),'manifest_sha256':sha(manifest_path),'raw_perf_records':120,'complete_operation_cases':60,'overview':'reports/SUMMARY.md','detailed_report':'results/received/reports/final-v2/REPORT.md','no_repository_push':True}
    receipt_path.write_text(json.dumps(receipt,indent=2)+'\n');print(json.dumps(receipt))


if __name__=='__main__':main()
