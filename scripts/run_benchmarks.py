#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0 WITH SHL-2.0
"""Measure public ELFs on CVA6; tohost in simulator output is already decoded."""
import argparse
import csv
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
ROOT = Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser()
p.add_argument("--model", type=Path, required=True)
p.add_argument("--elf-dir", type=Path, default=ROOT/"build/elf")
p.add_argument("--output", type=Path, default=ROOT/"build/cycles")
p.add_argument("--filter", default="")
p.add_argument("--timeout", type=int, default=900)
args=p.parse_args()
out=args.output.resolve()
out.mkdir(parents=True,exist_ok=True)
model=args.model.resolve()
elfdir=args.elf_dir.resolve()
manifest=json.loads((elfdir/"manifest.json").read_text())
rows=[]
for entry in manifest["entries"]:
    name=entry["case"]
    if args.filter and args.filter not in name: continue
    elf=elfdir/entry["elf"]
    assert hashlib.sha256(elf.read_bytes()).hexdigest()==entry["sha256"]
    logfile=out/(name+".log")
    if logfile.exists(): raise SystemExit(f"Existing log: {logfile}; choose a fresh output directory")
    command=[str(model),str(elf),"+debug_disable=1","+rvfi_trace_disable=1",
             "+time_out=30000000","++"+str(elf),"+elf_file="+str(elf),
             "+core_name=cv64a6_imafdc_sv39","+tohost_addr=0000000080001000",
             "+signature="+str(out/(name+".signature")),
             "+UVM_TESTNAME=uvmt_cva6_firmware_test_c",
             "+report_file="+str(out/(name+".yaml"))]
    with logfile.open("w") as log:
        process=subprocess.run(command,cwd=out,stdout=log,stderr=subprocess.STDOUT,timeout=args.timeout)
    text=logfile.read_text(errors="replace")
    values=re.findall(r"tohost = (-?\d+)",text)
    if not values or "Timeout[" in text or "Wall clock time passed:" not in text:
        raise SystemExit(f"Incomplete simulator run: {name}")
    value=int(values[-1])
    if name=="example":
        assert value==0 and process.returncode==0, (name,value,process.returncode)
        cycles=0
    else:
        assert 1<value<30000001 and value not in (890,1337,0x3fffffff), (name,value)
        assert process.returncode==(value&255), (name,value,process.returncode)
        cycles=value-1
    rows.append({"case":name,"cycles":cycles,"scope":entry["scope"],
                 "decoded_exit_code":value,"process_exit":process.returncode,
                 "elf_sha256":entry["sha256"],
                 "log_sha256":hashlib.sha256(logfile.read_bytes()).hexdigest()})
    (out/"cycles.json").write_text(json.dumps({
        "compiler":manifest["compiler"],
        "model_sha256":hashlib.sha256(model.read_bytes()).hexdigest(),
        "exit_encoding":"decoded exit_code = rdcycle delta + 1 (example uses exit 0)",
        "rows":rows},indent=2)+"\n")
    print(name,cycles,"PASS",flush=True)
if not rows: raise SystemExit("No matching benchmarks")
with (out/"cycles.csv").open("w",newline="") as f:
    writer=csv.DictWriter(f,fieldnames=list(rows[0]))
    writer.writeheader();writer.writerows(rows)
