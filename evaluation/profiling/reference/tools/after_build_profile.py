#!/usr/bin/env python3
import hashlib
import json
from pathlib import Path
import shlex
import subprocess
import sys
import tarfile

ROOT=Path(__file__).resolve().parents[1]
REMOTE=r'''
import os,select,json,subprocess,sys
from pathlib import Path
root=Path('/reviewer/aov-reference-profile-20261106-20');matches=[]
for p in Path('/proc').iterdir():
 if not p.name.isdigit() or int(p.name) in {os.getpid(),os.getppid()}:continue
 try:
  if p.stat().st_uid!=os.getuid():continue
  args=(p/'cmdline').read_bytes().split(bytes([0]))
 except OSError:continue
 if str(root/'tools/build_reference.py').encode() in args:matches.append(int(p.name))
if len(matches)>1:raise ValueError('ambiguous owned build process')
if matches:
 fd=os.pidfd_open(matches[0]);print(json.dumps({'event':'reference_build_wait','pid':matches[0]}),flush=True);select.select([fd],[],[]);os.close(fd)
valid=[p for p in root.glob('build/reference-*/manifest.json') if json.loads(p.read_text())['status']=='reference_build_complete']
if len(valid)!=1:raise ValueError('one fully validated reference build required')
build=valid[0]
os.sched_setaffinity(0,{3,7})
def run(args,label):
 proc=subprocess.run(args,cwd=root);print(json.dumps({'event':'reference_pipeline_step','label':label,'returncode':proc.returncode}),flush=True)
 if proc.returncode:raise SystemExit(proc.returncode)
run([sys.executable,'tools/check_perf_gate.py'],'gate-selftest')
run([sys.executable,'tools/check_public_vectors.py','--build',str(build)],'known-public-vectors')
run([sys.executable,'tools/run_reference.py','--build',str(build),'--smoke'],'representative-stage-smoke')
run([sys.executable,'tools/run_reference.py','--build',str(build)],'full-reference-profiling')
runs=[p for p in root.glob('raw/profile-*/manifest.json') if json.loads(p.read_text())['status']=='complete']
if len(runs)!=1:raise ValueError('unique completed profiling run required')
run([sys.executable,'tools/capture_reference.py','--build',str(build),'--run',str(runs[0])],'capture')
'''


def sha(path):
    h=hashlib.sha256()
    with path.open('rb') as f:
        for b in iter(lambda:f.read(1048576),b''):h.update(b)
    return h.hexdigest()


def main():
    status=ROOT/'results/pipeline.json'
    if status.exists():raise FileExistsError(status)
    state={'status':'waiting_for_reference_build','capture':None}
    def save():p=status.with_suffix('.tmp');p.write_text(json.dumps(state,indent=2)+'\n');p.replace(status)
    save()
    try:
        args=['ssh','-o','BatchMode=yes','-o','StrictHostKeyChecking=yes','-o','ConnectTimeout=15','-o','ServerAliveInterval=20','-o','ServerAliveCountMax=6','REDACTED_HOST','python3 -c '+shlex.quote(REMOTE)]
        proc=subprocess.Popen(args,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True)
        with (ROOT/'logs/profiling-pipeline.log').open('x') as log:
            for line in proc.stdout:
                log.write(line);log.flush();print(line.rstrip(),flush=True)
                if line.startswith('{'):
                    event=json.loads(line)
                    if event.get('event')=='reference_capture_complete':state['capture']=event;save()
            code=proc.wait()
        if code or not state['capture']:raise RuntimeError('target reference pipeline failed; evidence retained')
        state['status']='receiving_archive';save();archive=ROOT/'archive/complete-capture.tar.gz'
        subprocess.run(['scp','-o','BatchMode=yes','-o','StrictHostKeyChecking=yes','-o','ServerAliveInterval=20','REDACTED_HOST:'+state['capture']['archive'],str(archive)],check=True)
        if sha(archive)!=state['capture']['sha256']:raise ValueError('raw archive checksum mismatch')
        received=ROOT/'results/received';received.mkdir()
        with tarfile.open(archive) as tar:tar.extractall(received,filter='data')
        index=json.loads((received/'_capture.json').read_text())
        for item in index['files']:
            p=(received/item['path']).resolve()
            if not p.is_relative_to(received) or p.stat().st_size!=item['bytes'] or sha(p)!=item['sha256']:raise ValueError('captured file mismatch')
        state['status']='generating_local_reports';save()
        subprocess.run([sys.executable,str(ROOT/'tools/report_reference.py'),'--received',str(received),'--run',index['run'],'--output',str(ROOT/'reports/final')],check=True)
        state.update(status='complete_pending_review',received_files=len(index['files']),reports='reports/final');save()
        print(json.dumps({'event':'reference_profile_complete','status':state['status'],'reports':str(ROOT/'reports/final')}),flush=True)
    except BaseException as exc:state.update(status='failed',error=repr(exc));save();raise


if __name__=='__main__':main()
