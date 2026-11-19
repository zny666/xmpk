#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0 WITH SHL-2.0
"""Check raw perf events and regenerate all 60 per-API PMU rows and TABLE II."""
import argparse
import csv
import hashlib
import io
import json
from pathlib import Path
from statistics import median
import tarfile

ROOT=Path(__file__).resolve().parents[1]
E=ROOT/"evaluation"
p=argparse.ArgumentParser(description=__doc__)
p.add_argument("--output",type=Path,required=True)
args=p.parse_args()
manifest=json.loads((E/"profiling/bundle.json").read_text())
data=b"".join((E/x["file"]).read_bytes() for x in manifest["parts"])
assert hashlib.sha256(data).hexdigest()==manifest["sha256"]
t=tarfile.open(fileobj=io.BytesIO(data),mode="r:gz")
def raw(name):return t.extractfile("results/received/"+name).read()
summary=json.loads(raw("reports/final-v2/summary.json"))
names={"cycles:u":"cycles_per_op","instructions:u":"instructions_per_op",
       "branches:u":"branches_per_op","branch-misses:u":"branch_misses_per_op",
       "cpu/L1-dcache-loads/u":"l1d_loads_per_op",
       "cpu/L1-dcache-load-misses/u":"l1d_load_misses_per_op"}
ratios=("ipc","branch_miss_rate","l1d_load_miss_rate")
rows=[];links=[];batches=0;events=0
for case in summary["cases"]:
    passes=[];walls=[]
    for recorded in case["passes"]:
        if recorded["name"]=="calibration":continue
        if recorded["kind"] not in ("stat","timing"):continue
        result=json.loads(raw(recorded["result_path"]))
        obs=result["observation"]
        assert result["returncode"]==0 and obs["status"]=="ok" and obs["iterations"]>0
        if recorded["kind"]=="timing":
            walls.append(obs["wall_ns"]/obs["iterations"])
            continue
        batches+=1
        parsed={}
        for line in raw(recorded["path"]+"/perf-stat.txt").decode().splitlines():
            fields=line.split("|")
            if len(fields)<5 or fields[2] not in names:continue
            assert float(fields[4])>=99.99, (recorded["path"],fields)
            parsed[fields[2]]=float(fields[0]);events+=1
        assert parsed
        values={names[k]:v/obs["iterations"] for k,v in parsed.items()}
        for metric,a,b in [("ipc","instructions:u","cycles:u"),
                           ("branch_miss_rate","branch-misses:u","branches:u"),
                           ("l1d_load_miss_rate","cpu/L1-dcache-load-misses/u","cpu/L1-dcache-loads/u")]:
            if a in parsed and b in parsed:
                values[metric]=parsed[a]/parsed[b]
        for old in recorded["stat_events"]:
            assert parsed[old["event"]]==old["count"]
        passes.append(values)
        links.append(dict(algorithm=case["algorithm"],parameter=case["parameter"],
            operation=case["operation"],command_member="results/received/"+recorded["result_path"],
            raw_stat_member="results/received/"+recorded["path"]+"/perf-stat.txt",
            iterations=obs["iterations"],table="tab:profile"))
    assert len(walls)==7, (case["id"],len(walls))
    row={k:case[k] for k in ("algorithm","parameter","operation")}
    row["wall_ms"]=median(walls)/1e6
    assert abs(row["wall_ms"]-case["baseline_wall_ns_per_op"]["median"]/1e6)<1e-8
    for metric in list(names.values())+list(ratios):
        row[metric]=median(x[metric] for x in passes if metric in x)
    rows.append(row)
assert len(rows)==60
expected=list(csv.DictReader((E/"paper/generated/reference_pmu_all_apis.csv").open()))
for row in rows:
    old=next(x for x in expected if all(x[k]==row[k] for k in ("algorithm","parameter","operation")))
    for k in list(names.values())+list(ratios):
        assert abs(float(old[k])-row[k])<1e-6,(row,k)
representatives=[("MAYO","mayo","MAYO-1"),("MQOM","mqom","mqom3_cat1_gf16_fast_ct"),
                 ("QR-UOV","qruov","1q127L3"),("SNOVA","snova","SNOVA_I_B_AES"),("UOV","uov","UOV-Is")]
table=["% Generated PMU for the slowest API at each representative parameter.",
       "% API selection uses the maximum of the three median measured wall times.",
       r"\newcommand{\ReferenceProfileRows}{%"]
for label,algorithm,parameter in representatives:
    selected=max((r for r in rows if r["algorithm"]==algorithm and r["parameter"]==parameter),key=lambda r:r["wall_ms"])
    api={"keygen":"KeyGen","sign":"Sign","verify":"Verify"}[selected["operation"]]
    table.append(f'{label} & {api} & {selected["ipc"]:.3f} & {selected["branches_per_op"]/1e6:.2f} & {selected["l1d_loads_per_op"]/1e6:.2f} & {selected["branch_miss_rate"]*100:.3f}'+r"\% & "+f'{selected["l1d_load_miss_rate"]*100:.3f}'+r"\% \\")
table.append("}")
tex="\n".join(table)+"\n"
assert tex==(E/"paper/generated/profile_rows.tex").read_text()
args.output.mkdir(parents=True,exist_ok=True)
for name,items in [("pmu-all-apis.csv",rows),("pmu-traceability.csv",links)]:
    with (args.output/name).open("w",newline="") as f:
        w=csv.DictWriter(f,fieldnames=list(items[0]));w.writeheader();w.writerows(items)
(args.output/"profile_rows.tex").write_text(tex)
(args.output/"verification.json").write_text(json.dumps({
    "cases":len(rows),"timing_batches":420,"stat_batches":batches,"raw_events":events,
    "all_archived_PMU_values_match":True,"TABLE_II_byte_identical":True,
    "scope":"Medians of user-mode PMU counts per actual API iteration. Independent event pairs; self sampling shares are not wall-time fractions."},indent=2)+"\n")
print("PROFILE_RECOMPUTE_PASS",len(rows),"cases;",batches,"stat batches;",events,"raw events; TABLE II identical")
