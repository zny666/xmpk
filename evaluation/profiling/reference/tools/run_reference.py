#!/usr/bin/env python3
import argparse
from datetime import datetime,timezone
import hashlib
import json
import math
import os
from pathlib import Path
import re
import resource
import signal
import shutil
import subprocess
import tempfile
import threading
import time
from environment_probe import snapshot,metadata

ROOT=Path(__file__).resolve().parents[1]


def sha(path):
    h=hashlib.sha256()
    with path.open('rb') as f:
        for chunk in iter(lambda:f.read(1048576),b''):h.update(chunk)
    return h.hexdigest()


def save(path,data):
    p=path.with_suffix('.tmp');p.write_text(json.dumps(data,indent=2)+'\n');p.replace(path)


class Observe:
    def __init__(self):self.samples=[];self.errors=[];self.stop=threading.Event()
    def loop(self):
        try:
            while not self.stop.is_set():self.samples.append(snapshot());self.stop.wait(1)
            self.samples.append(snapshot())
        except Exception as e:self.errors.append(repr(e))
    def __enter__(self):self.thread=threading.Thread(target=self.loop);self.thread.start();return self
    def __exit__(self,*args):self.stop.set();self.thread.join()


def preexec():
    os.setsid();resource.setrlimit(resource.RLIMIT_CORE,(0,0));resource.setrlimit(resource.RLIMIT_AS,(4*1024**3,4*1024**3))


def collect(binary,op,fixture,count,directory,kind,events=None,frequency=199):
    directory.mkdir();env=os.environ.copy();env['LC_ALL']='C';fds=[];controls=[]
    target=['taskset','-c','3',str(binary),op,str(fixture),str(count)]
    if kind in ['stat','sample']:
        cr,cw=os.pipe();ar,aw=os.pipe();fds=[cr,cw,ar,aw];controls=[cr,aw]
        env['BENCH_CONTROL_FD']=str(cw);env['BENCH_ACK_FD']=str(ar)
        if kind=='stat':argv=['perf','stat','-D','-1','--control',f'fd:{cr},{aw}','--no-big-num','-x','|','-o',str(directory/'perf-stat.txt'),'-e',events,'--',*target]
        else:argv=['perf','record','-D','-1','--control',f'fd:{cr},{aw}','-e','cycles:u','-F',str(frequency),'-g','--call-graph','fp','--user-callchains','--timestamp','--sample-cpu','-o',str(directory/'perf.data'),'--',*target]
    else:argv=target
    started=time.monotonic_ns();observation=None
    try:
        with Observe() as observer:
            process=subprocess.Popen(argv,cwd=ROOT,env=env,pass_fds=tuple(fds),stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True,preexec_fn=preexec)
            try:stdout,stderr=process.communicate(timeout=600)
            except subprocess.TimeoutExpired:
                os.killpg(process.pid,signal.SIGKILL);stdout,stderr=process.communicate();stderr+='\nController timeout; only the newly created owned process group was terminated.\n'
            proc=subprocess.CompletedProcess(argv,process.returncode,stdout,stderr)
        (directory/'stdout.txt').write_text(proc.stdout);(directory/'stderr.txt').write_text(proc.stderr)
        record={'kind':kind,'operation':op,'iterations':count,'events':events,'argv':[x if x!=str(fixture) else '<private-memory-fixture>' for x in argv],'returncode':proc.returncode,'whole_process_wall_ns':time.monotonic_ns()-started,'observation':None,'environment':{'samples':observer.samples,'errors':observer.errors}}
        try:
            if proc.returncode==0:
                lines=[line for line in proc.stdout.splitlines() if line.startswith('{')]
                if len(lines)!=1:raise ValueError('worker JSON output missing despite profiler exit zero')
                observation=json.loads(lines[0])
                if observation['status']!='ok' or observation['iterations']!=count or observation['cpu_start']!=3 or observation['cpu_end']!=3:raise ValueError('worker measurement contract failed')
                if kind in ['stat','sample'] and not observation['perf_gated']:raise ValueError('perf not controlled by worker')
                record['observation']=observation
        except (ValueError,KeyError,TypeError) as error:
            record['parse_error']=str(error);record['worker_validated']=False;save(directory/'result.json',record);raise
        record['worker_validated']=proc.returncode==0 and observation is not None
        save(directory/'result.json',record)
        return record
    finally:
        for fd in fds:os.close(fd)


def perf_text(directory,argv,name,timeout=90):
    p=subprocess.run(argv,cwd=ROOT,capture_output=True,text=True,timeout=timeout)
    (directory/name).write_text(p.stdout);(directory/(name+'.stderr')).write_text(p.stderr)
    return {'argv':argv,'returncode':p.returncode,'output':name,'stderr':name+'.stderr'}


def sample_reports(directory):
    data=directory/'perf.data';commands=[]
    for name,args in [
        ('events.txt',['evlist','-v']),
        ('buildids.txt',['buildid-list']),
        ('report-self.txt',['report','--stdio','--no-children','--call-graph','none','--percent-limit','0','--sort','dso,symbol','--show-nr-samples','--show-total-period','--field-separator','|']),
        ('report-children.txt',['report','--stdio','--children','--call-graph','none','--percent-limit','0','--sort','dso,symbol','--show-nr-samples','--show-total-period','--field-separator','|']),
        ('samples.txt',['script','-F','comm,pid,tid,cpu,time,period,event,ip,sym,dso']),
        ('header.txt',['report','--header-only','--stdio'])]:
        commands.append(perf_text(directory,['perf',*args,'-i',str(data)],name))
    sample_types=(directory/'events.txt').read_text()
    if any(token in sample_types for token in ['STACK_USER','REGS_USER','REGS_INTR','PHYS_ADDR','DATA_SRC']):raise ValueError('forbidden secret-bearing sample data type')
    save(directory/'report-commands.json',commands)
    if any(x['returncode'] for x in commands[:5]):raise ValueError('perf export failed')
    text=(directory/'report-self.txt').read_text();top=None;symbol_index=None
    for line in text.splitlines():
        if '|' not in line:continue
        parts=[x.strip() for x in line.lstrip('#').split('|')]
        if 'Symbol' in parts:symbol_index=parts.index('Symbol');continue
        if line.lstrip().startswith('#') or symbol_index is None or len(parts)<=symbol_index:continue
        symbol=re.sub(r'^\s*\[[^]]+\]\s*','',parts[symbol_index]).strip()
        if symbol and symbol not in ['[unknown]','0x0'] and not symbol.startswith('0x'):top=symbol;break
    if top:
        commands.append(perf_text(directory,['perf','annotate','--stdio','--no-source','-i',str(data),'--symbol',top],'annotate-top.txt'))
        save(directory/'report-commands.json',commands)


def main():
    p=argparse.ArgumentParser();p.add_argument('--build',type=Path,required=True);p.add_argument('--smoke',action='store_true');args=p.parse_args()
    build=json.loads(args.build.read_text());protocol=json.loads((ROOT/'protocol.json').read_text())
    if build['status']!='reference_build_complete' or len(build['entries'])!=20:raise ValueError('complete validated reference build required')
    for name,h in build['source_files'].items():
        if sha(ROOT/name)!=h:raise ValueError('reference source changed')
    if not {3,7}<=set(os.sched_getaffinity(0)):raise ValueError('CPU3/7 unavailable')
    host=metadata();os.sched_setaffinity(0,{7});out=ROOT/'raw'/datetime.now(timezone.utc).strftime(('smoke-' if args.smoke else 'profile-')+'%Y%m%dT%H%M%S-%fZ');out.mkdir()
    state={'status':'running','development':args.smoke,'build_manifest':str(args.build.relative_to(ROOT)),'build_sha256':sha(args.build),'protocol':protocol,'host':host,'cases':[],'errors':[],'perf_capabilities':[]}
    save(out/'manifest.json',state)
    def probe(event):
        p=subprocess.run(['perf','stat','-e',event,'--','true'],capture_output=True,text=True,timeout=15)
        ok=p.returncode==0 and '<not supported>' not in p.stderr and '<not counted>' not in p.stderr
        state['perf_capabilities'].append({'event':event,'supported':ok,'returncode':p.returncode,'stdout':p.stdout,'stderr':p.stderr});save(out/'manifest.json',state);return ok
    core=probe('{cycles:u,instructions:u}')
    aux=[]
    for event in ['{branches:u,branch-misses:u}','{cpu/L1-dcache-loads/u,cpu/L1-dcache-load-misses/u}']:
        if probe(event):aux.append(event)
    if not core:raise ValueError('cycles/instructions PMU unavailable; no profiling started')
    if args.smoke:
        entries=[]
        for a in ['mayo','uov','qruov','snova','mqom']:entries.append(next(e for e in build['entries'] if e['algorithm'].lower().replace('-','')==a))
    else:entries=build['entries']
    try:
        for e in entries:
            binary=ROOT/e['binary']
            if sha(binary)!=e['sha256']:raise ValueError('reference binary changed')
            family=out/(e['algorithm']+'-'+e['parameter']);family.mkdir()
            with tempfile.TemporaryDirectory(prefix='r20-private-') as temp:
                fixture=Path(temp)/'fixture.key'
                result=subprocess.run(['taskset','-c','3',str(binary),'--fixture',str(fixture)],capture_output=True,text=True,timeout=600,preexec_fn=preexec)
                if result.returncode:raise ValueError('fixture generation failed '+e['parameter']+': '+result.stderr[-800:])
                fixture_id=sha(fixture)
                for op in protocol['operations']:
                    case=family/op;case.mkdir();case_state={'algorithm':e['algorithm'],'parameter':e['parameter'],'operation':op,'binary':e['binary'],'binary_sha256':e['sha256'],'metadata':e['metadata'],'same_fixture_commitment_sha256':fixture_id,'status':'running','passes':[]};state['cases'].append(case_state)
                    save(out/'manifest.json',state)
                    calibration=collect(binary,op,fixture,1,case/'calibration','timing')
                    if calibration['returncode']:raise ValueError('calibration failed '+e['parameter']+' '+op)
                    per_op=calibration['observation']['wall_ns']/1e9
                    m=e['metadata'];stride=m['signature_max_bytes']+32 if op=='sign' else m['public_bytes']+m['secret_bytes'] if op=='keygen' else 1
                    cap=min(100000,max(1,134217728//stride));n=max(1,min(cap,int(.5/max(per_op,1e-9))));ns=max(1,min(cap,int((.3 if args.smoke else 10)/max(per_op,1e-9))))
                    design={'calibration_seconds_per_op':per_op,'baseline_and_stat_iterations':n,'record_iterations':ns,'max_retained_output_bytes':134217728,'timing_trials':1 if args.smoke else 7,'core_stat_trials':1 if args.smoke else 3,'record_passes':1 if args.smoke else 2,'frozen_before_repetitions':True}
                    save(case/'design.json',design);case_state['design']=design
                    jobs=[('timing',None,i,n) for i in range(design['timing_trials'])]+[('stat','{cycles:u,instructions:u}',i,n) for i in range(design['core_stat_trials'])]
                    if not args.smoke:jobs += [('stat',event,10+i,n) for i,event in enumerate(aux)]
                    jobs += [('sample','cycles:u',i,ns) for i in range(design['record_passes'])]
                    for kind,event,i,count in jobs:
                        folder=case/f'{kind}-{i}';rec=collect(binary,op,fixture,count,folder,kind,event)
                        case_state['passes'].append({'kind':kind,'event':event,'iterations':count,'path':str(folder.relative_to(ROOT)),'returncode':rec['returncode']});save(out/'manifest.json',state)
                        if rec['returncode']:raise ValueError('phase run failed '+str(folder))
                        if kind=='sample':sample_reports(folder)
                    case_state['status']='complete';save(out/'manifest.json',state)
                    print('Profiled original',e['algorithm'],e['parameter'],op,flush=True)
        state['status']='smoke_complete' if args.smoke else 'complete'
        for name,h in build['source_files'].items():
            if sha(ROOT/name)!=h:raise ValueError('source changed during profiling')
    except BaseException as exc:state['status']='failed';state['errors'].append(repr(exc));raise
    finally:save(out/'manifest.json',state)
    print(out/'manifest.json',flush=True)


if __name__=='__main__':main()
