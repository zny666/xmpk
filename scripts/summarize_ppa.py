#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0 WITH SHL-2.0
"""Extract concise OOC results without copying host paths from Vivado reports."""
import argparse
import hashlib
import json
from pathlib import Path
import re
p=argparse.ArgumentParser()
p.add_argument("reports", type=Path)
p.add_argument("--output", type=Path)
args=p.parse_args()
r=args.reports
files={name:(r/name).read_text() for name in ("route-util.rpt","timing.rpt","power.rpt")}
def metric(text,label):
    match=re.search(r"\|\s*"+re.escape(label)+r"\s*\|\s*([0-9.]+)",text)
    if not match: raise SystemExit("Missing report field: "+label)
    return float(match[1])
match=re.search(r"WNS\(ns\)[^\n]*\n[ \t-]+\n([^\n]+)",files["timing.rpt"])
if not match: raise SystemExit("Missing timing summary")
numbers=match[1].split()
timing={"WNS_ns":float(numbers[0]),"WHS_ns":float(numbers[4]),"WPWS_ns":float(numbers[8])}
tool=re.search(r"Tool Version\s*:\s*([^|\n]+)",files["route-util.rpt"])
part=re.search(r"Device\s*:\s*([^|\s]+)",files["route-util.rpt"])
confidence=re.search(r"\|\s*Confidence Level\s*\|\s*([^|\n]+)",files["power.rpt"])
data={
    "tool":tool[1].strip() if tool else "unknown",
    "part":part[1].strip() if part else "unknown",
    "scope":"Coprocessor OOC, 80 MHz core clock; external I/O unconstrained",
    "timing":timing,
    "internal_timing_nonnegative":all(x>=0 for x in timing.values()),
    "post_route":{key:metric(files["route-util.rpt"],label) for key,label in
                  (("LUT","Slice LUTs"),("FF","Slice Registers"),("BRAM_tiles","Block RAM Tile"),("DSP","DSPs"))},
    "power":{"method":"post-route vectorless estimate",
             "confidence":confidence[1].strip() if confidence else "unknown",
             "ambient_C":metric(files["power.rpt"],"Ambient Temp (C)"),
             "junction_C":metric(files["power.rpt"],"Junction Temperature (C)"),
             "airflow_LFM":metric(files["power.rpt"],"Airflow (LFM)"),
             **{key:metric(files["power.rpt"],label) for key,label in
                (("total_W","Total On-Chip Power (W)"),("dynamic_W","Dynamic (W)"),("static_W","Device Static (W)"))}},
    "report_sha256":{name:hashlib.sha256((r/name).read_bytes()).hexdigest() for name in files}}
text=json.dumps(data,indent=2)+"\n"
if args.output: args.output.write_text(text)
else: print(text,end="")
