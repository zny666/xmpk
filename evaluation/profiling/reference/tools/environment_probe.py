#!/usr/bin/env python3
import argparse
from datetime import datetime, timezone
import json
import os
from pathlib import Path
import time


def read(path):
    try:return Path(path).read_text().strip()
    except OSError:return None


def cgroups():
    value=read('/proc/self/cgroup');result=[]
    if value and value.startswith('0::'):
        p=Path('/sys/fs/cgroup')/value.split('::',1)[1].lstrip('/')
        while p.is_relative_to('/sys/fs/cgroup'):
            result.append({'path':str(p),'cpu_max':read(p/'cpu.max'),'cpu_stat':read(p/'cpu.stat'),'cpuset':read(p/'cpuset.cpus.effective')})
            if p==Path('/sys/fs/cgroup'):break
            p=p.parent
    return result


def snapshot():
    cpus={}
    for line in Path('/proc/stat').read_text().splitlines():
        words=line.split()
        if words[0].startswith('cpu') and words[0][3:].isdigit():cpus[words[0][3:]]=list(map(int,words[1:9]))
    return {'monotonic_ns':time.monotonic_ns(),'cpus':cpus,'interrupts':read('/proc/interrupts'),'softirqs':read('/proc/softirqs'),
            'cpu_pressure':read('/proc/pressure/cpu'),'memory_pressure':read('/proc/pressure/memory'),'loadavg':read('/proc/loadavg'),
            'frequencies':{p.parent.name:read(p) for p in Path('/sys/devices/system/cpu/cpufreq').glob('policy*/scaling_cur_freq')},
            'temperatures':{p.parent.name:read(p) for p in Path('/sys/class/thermal').glob('thermal_zone*/temp')},
            'cooling_states':{p.parent.name:read(p) for p in Path('/sys/class/thermal').glob('cooling_device*/cur_state')},
            'cgroups':cgroups(),'clock_ticks_per_second':os.sysconf('SC_CLK_TCK')}


def metadata():
    thermal=[]
    for zone in Path('/sys/class/thermal').glob('thermal_zone*'):
        trips=[];devices=[]
        for p in zone.glob('trip_point_*_type'):
            stem=str(p).removesuffix('_type');trips.append({'type':read(p),'temperature':read(stem+'_temp'),'hysteresis':read(stem+'_hyst'),'index':p.name.split('_')[2]})
        for p in zone.glob('cdev[0-9]*'):
            if p.is_symlink():devices.append({'device':p.resolve().name,'type':read(p/'type'),'trip_point':read(str(p)+'_trip_point')})
        thermal.append({'zone':zone.name,'type':read(zone/'type'),'trips':trips,'devices':devices})
    policies=[]
    for policy in Path('/sys/devices/system/cpu/cpufreq').glob('policy*'):
        policies.append({'policy':policy.name,**{name:read(policy/name) for name in ['related_cpus','scaling_governor','scaling_min_freq','scaling_max_freq','cpuinfo_max_freq']}})
    cgroup=read('/proc/self/cgroup');cgroup_paths=[]
    if cgroup and cgroup.startswith('0::'):
        p=Path('/sys/fs/cgroup')/cgroup.split('::',1)[1].lstrip('/')
        while p.is_relative_to('/sys/fs/cgroup'):
            cgroup_paths.append({'path':str(p),'cpu_max':read(p/'cpu.max'),'cpu_stat':read(p/'cpu.stat'),'cpuset':read(p/'cpuset.cpus.effective')})
            if p==Path('/sys/fs/cgroup'):break
            p=p.parent
    return {'architecture':os.uname().machine,'kernel':os.uname().release,'clock_ticks_per_second':os.sysconf('SC_CLK_TCK'),'affinity':sorted(os.sched_getaffinity(0)),
            'online':read('/sys/devices/system/cpu/online'),'thermal':thermal,'policies':policies,'cgroup':cgroup_paths,
            'perf_event_paranoid':read('/proc/sys/kernel/perf_event_paranoid'),'local_wallclock':datetime.now(timezone.utc).isoformat()}


def assess(samples,meta,cpu):
    problems=[];policy=next((p for p in meta['policies'] if str(cpu) in (p['related_cpus'] or '').split()),None)
    if policy is None:return {'status':'unqualified','reasons':['frequency_policy_unavailable']}
    values=[int(s['frequencies'][policy['policy']]) for s in samples if s['frequencies'].get(policy['policy'])]
    if len(values)!=len(samples) or len(set(values))!=1:problems.append('frequency_not_stable')
    cpu_devices={d['device'] for t in meta['thermal'] for d in t['devices'] if d['type']=='cpufreq-cpu'+str(min(map(int,policy['related_cpus'].split())))}
    if not cpu_devices:problems.append('CPU_cooling_binding_unavailable')
    for dev in cpu_devices:
        if any(s['cooling_states'].get(dev)!='0' for s in samples):problems.append('CPU_frequency_cooling_active')
    for t in meta['thermal']:
        if any(d['device'] in cpu_devices for d in t['devices']):
            temps=[int(s['temperatures'][t['zone']]) for s in samples if s['temperatures'].get(t['zone'])]
            limits=[int(x['temperature']) for x in t['trips'] if x['type'] in ['passive','hot','critical'] and x['temperature']]
            if len(temps)!=len(samples) or not limits:problems.append('relevant_temperature_unavailable')
            elif max(temps)>=min(limits)-5000:problems.append('within_5C_of_CPU_passive_trip')
    first,last=samples[0]['cpus'].get(str(cpu)),samples[-1]['cpus'].get(str(cpu))
    busy=None
    if first is None or last is None:problems.append('CPU_accounting_missing')
    else:
        delta=[b-a for a,b in zip(first,last)];total=sum(delta)
        if total<=0:problems.append('CPU_accounting_not_progressing')
        else:
            busy=(total-delta[3]-delta[4])/total
            if busy>.005:problems.append('target_CPU_idle_busy_above_0.5pct')
    return {'status':'observed_idle_gate_passed' if not problems else 'unqualified','reasons':sorted(set(problems)),
            'target_cpu':cpu,'idle_busy_fraction':busy,'frequency_khz_range':[min(values),max(values)] if values else None,
            'CPU_cooling_devices':sorted(cpu_devices),'not_a_run_qualification':True,
            'scope':'Only observed idle conditions. PWM fan trip activation is not treated as CPU throttling; per-phase preemption, frequency stability and PMU scheduling remain required during real measurements.'}


def main():
    p=argparse.ArgumentParser();p.add_argument('--out',type=Path,required=True);p.add_argument('--cpu',type=int,default=3);p.add_argument('--monitor-cpu',type=int,default=7);p.add_argument('--seconds',type=float,default=30);args=p.parse_args()
    if args.out.exists():raise FileExistsError(args.out)
    if args.cpu==args.monitor_cpu or not {args.cpu,args.monitor_cpu}.issubset(os.sched_getaffinity(0)):raise ValueError('distinct permitted CPUs required')
    meta=metadata();os.sched_setaffinity(0,{args.monitor_cpu});samples=[];deadline=time.monotonic()+args.seconds
    while True:
        samples.append(snapshot())
        if time.monotonic()>=deadline:break
        time.sleep(.5)
    result={'metadata':meta,'samples':samples,'assessment':assess(samples,meta,args.cpu)}
    with args.out.open('x') as f:json.dump(result,f,indent=2);f.write('\n')
    print(json.dumps(result['assessment']))


if __name__=='__main__':main()
