#!/usr/bin/env python3
import hashlib
import json
from pathlib import Path
import shlex
import subprocess
import sys
import tarfile
import report_reference

ROOT=Path(__file__).resolve().parents[1]
TARGET='/reviewer/aov-reference-profile-20261106-20'
BUILD=TARGET+'/build/reference-ready-20260915T131710-627617Z/manifest.json'
WAIT=r'''
import os,select,json
from pathlib import Path
root=Path('/reviewer/aov-reference-profile-20261106-20');matches=[]
for p in Path('/proc').iterdir():
 if not p.name.isdigit() or int(p.name) in {os.getpid(),os.getppid()}:continue
 try:
  if p.stat().st_uid!=os.getuid():continue
  args=(p/'cmdline').read_bytes().split(bytes([0]))
 except OSError:continue
 if str(root/'tools/run_reference.py').encode() in args and b'--smoke' in args:matches.append(int(p.name))
if len(matches)>1:raise ValueError('ambiguous owned smoke process')
if matches:
 fd=os.pidfd_open(matches[0]);select.select([fd],[],[]);os.close(fd)
paths=[p for p in root.glob('raw/smoke-*/manifest.json') if json.loads(p.read_text())['status']=='smoke_complete']
if len(paths)!=1:raise ValueError('exactly one complete representative smoke run required')
m=json.loads(paths[0].read_text());records=[]
if len(m['cases'])!=15:raise ValueError('five families by three operations smoke coverage required')
for c in m['cases']:
 if c['status']!='complete':raise ValueError('smoke case incomplete')
 for p in c['passes']:
  if p['returncode']:raise ValueError('smoke failed pass')
  folder=root/p['path']
  if p['kind']=='sample':records.append({'case':[c['algorithm'],c['parameter'],c['operation']],**{n:(folder/n).read_text() for n in ['report-self.txt','report-children.txt','events.txt','header.txt','report-commands.json']}})
print(json.dumps({'status':'smoke_complete','path':str(paths[0].relative_to(root)),'cases':len(m['cases']),'sample_exports':records}))
'''


def ssh(script):return ['ssh','-o','BatchMode=yes','-o','StrictHostKeyChecking=yes','-o','ConnectTimeout=15','-o','ServerAliveInterval=20','-o','ServerAliveCountMax=6','REDACTED_HOST','python3 -c '+shlex.quote(script)]


def sha(path):
    h=hashlib.sha256()
    with path.open('rb') as f:
        for b in iter(lambda:f.read(1048576),b''):h.update(b)
    return h.hexdigest()


def main():
    status=ROOT/'results/performance-pipeline-v2.json'
    if status.exists():raise FileExistsError(status)
    state={'status':'waiting_for_selfverified_phase_smoke','user_policy':'KAT not an execution gate; preserve known mismatch, do not claim conformance','steps':[]}
    def save():p=status.with_suffix('.tmp');p.write_text(json.dumps(state,indent=2)+'\n');p.replace(status)
    save()
    try:
        p=subprocess.run(ssh(WAIT),capture_output=True,text=True,timeout=1200)
        (ROOT/'logs/smoke-readback-v2.log').write_text(p.stdout+p.stderr)
        if p.returncode:raise ValueError('representative smoke did not complete')
        smoke=json.loads(p.stdout)
        if len(smoke['sample_exports'])!=15:raise ValueError('smoke sample inventory')
        summaries=[]
        for record in smoke['sample_exports']:
            own=report_reference.parse_report(record['report-self.txt'],'self');children=report_reference.parse_report(record['report-children.txt'],'children');safety=report_reference.sample_safety(record['events.txt'])
            summaries.append({'case':record['case'],'self_functions':len(own['rows']),'children_functions':len(children['rows']),'cycle_period':own['total_period'],'safety':safety})
        (ROOT/'evidence/real-smoke-parser-check.json').write_text(json.dumps({'status':'PASS','samples':summaries,'source_smoke':smoke['path'],'source_output_sha256':hashlib.sha256(p.stdout.encode()).hexdigest()},indent=2)+'\n')
        state['status']='collecting_sixty_operation_cases';state['smoke_parser']='PASS';save();print(json.dumps({'event':'all_phase_smoke_and_parser_passed','cases':15}),flush=True)
        script="from pathlib import Path; import subprocess,sys,json; r=Path(%r); assert not list(r.glob('raw/profile-*/manifest.json')), 'existing formal run requires manual review'; p=subprocess.run([sys.executable,str(r/'tools/run_reference.py'),'--build',%r],cwd=r); raise SystemExit(p.returncode)"%(TARGET,BUILD)
        proc=subprocess.Popen(ssh(script),stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True)
        with (ROOT/'logs/formal-profiles-v2.log').open('x') as log:
            for line in proc.stdout:log.write(line);log.flush();print(line.rstrip(),flush=True)
            code=proc.wait()
        if code:raise ValueError('formal data collection failed; original files retained')
        state['status']='archiving_completed_target_data';save()
        script="from pathlib import Path; import json,subprocess,sys; r=Path(%r); ps=[p for p in r.glob('raw/profile-*/manifest.json') if json.loads(p.read_text())['status']=='complete']; assert len(ps)==1; p=subprocess.run([sys.executable,str(r/'tools/capture_reference.py'),'--build',%r,'--run',str(ps[0])],cwd=r); raise SystemExit(p.returncode)"%(TARGET,BUILD)
        p=subprocess.run(ssh(script),capture_output=True,text=True,timeout=600);(ROOT/'logs/final-capture-v2.log').write_text(p.stdout+p.stderr)
        if p.returncode:raise ValueError('capture failed')
        capture=next(json.loads(line) for line in p.stdout.splitlines() if line.startswith('{') and json.loads(line).get('event')=='reference_capture_complete');state['capture']=capture;save()
        archive=ROOT/'archive/complete-capture.tar.gz'
        if archive.exists():raise FileExistsError(archive)
        subprocess.run(['scp','-o','BatchMode=yes','-o','StrictHostKeyChecking=yes','-o','ServerAliveInterval=20','REDACTED_HOST:'+capture['archive'],str(archive)],check=True)
        if sha(archive)!=capture['sha256']:raise ValueError('archive transfer checksum failed')
        received=ROOT/'results/received';received.mkdir()
        with tarfile.open(archive) as t:t.extractall(received,filter='data')
        index=json.loads((received/'_capture.json').read_text())
        for x in index['files']:
            p=(received/x['path']).resolve()
            if not p.is_relative_to(received) or p.stat().st_size!=x['bytes'] or sha(p)!=x['sha256']:raise ValueError('captured bytes differ')
        state.update(status='summarizing_locally',received_files=len(index['files']));save()
        (received/'reports').mkdir(exist_ok=True)
        subprocess.run([sys.executable,str(ROOT/'tools/report_reference.py'),str(received),'--run',index['run'],'--output','reports/final'],check=True)
        report=received/'reports/final'
        state.update(status='reported_pending_final_review',reports=str(report.relative_to(ROOT)));save()
        print(json.dumps({'event':'reference_data_and_reports_ready','status':state['status'],'reports':str(report)}),flush=True)
    except BaseException as exc:state.update(status='failed',error=repr(exc));save();raise


if __name__=='__main__':main()
