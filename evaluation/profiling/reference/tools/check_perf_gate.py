#!/usr/bin/env python3
import json
from datetime import datetime,timezone
from pathlib import Path
import re
import subprocess
import os
from run_reference import collect,sample_reports,save

ROOT=Path(__file__).resolve().parents[1]


def main():
    out=ROOT/'evidence'/datetime.now(timezone.utc).strftime('perf-gate-check-%Y%m%dT%H%M%S-%fZ');out.mkdir();binary=out/'gate-check'
    os.sched_setaffinity(0,{7})
    p=subprocess.run(['gcc','-O3','-g','-fno-omit-frame-pointer','-fno-optimize-sibling-calls','-fno-tree-vectorize','-fno-tree-slp-vectorize','-march=rv64gc','-mabi=lp64d',str(ROOT/'native/gate_test.c'),'-Wl,--build-id','-o',str(binary)],capture_output=True,text=True)
    (out/'build.log').write_text(p.stdout+p.stderr)
    if p.returncode:raise ValueError('gate self-test did not compile')
    stat=collect(binary,'gate',out/'unused',1,out/'stat','stat','{cycles:u,instructions:u}')
    if stat['returncode']:raise ValueError('gate stat failed')
    sample=collect(binary,'gate',out/'unused',1,out/'sample','sample','cycles:u')
    if sample['returncode']:raise ValueError('gate record failed')
    sample_reports(out/'sample');text=(out/'sample/report-self.txt').read_text()
    if 'inside_gate_marker' not in text:raise ValueError('no inside-gate samples')
    if 'outside_gate_marker' in text:raise ValueError('out-of-window work leaked into profile')
    attrs=(out/'sample/events.txt').read_text()
    if any(x in attrs for x in ['STACK_USER','REGS_USER','REGS_INTR','PHYS_ADDR']):raise ValueError('unexpected sensitive sample payload')
    save(out/'result.json',{'status':'PASS','gate_control_ACK_worked':True,'inside_gate_samples_observed':True,'outside_gate_samples_observed':False,'no_user_stack_or_register_dump':True,'scope':'Synthetic bounded gate test, not algorithm performance or a guarantee against all profiling perturbation.'})
    print(json.dumps({'status':'PASS','perf_gate_selftest':str(out.relative_to(ROOT))}))


if __name__=='__main__':main()
