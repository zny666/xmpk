#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0 WITH SHL-2.0
"""Restore, recompute and verify exported evidence using Python's standard library."""
import argparse
import csv
import hashlib
import io
import json
from pathlib import Path
import re
import shutil
import tarfile

ROOT = Path(__file__).resolve().parents[1]
E = ROOT / "evaluation"
def sha(data): return hashlib.sha256(data).hexdigest()
def read_json(path): return json.loads(path.read_text())
def write_json(path, data):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(data, indent=2)+"\n")
def write_csv(path, rows):
    if not rows: return
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", newline="") as f:
        writer=csv.DictWriter(f, fieldnames=list(rows[0]))
        writer.writeheader(); writer.writerows(rows)
def bundle():
    manifest=read_json(E/"profiling/bundle.json")
    chunks=[]
    for entry in manifest["parts"]:
        data=(E/entry["file"]).read_bytes()
        assert sha(data)==entry["sha256"], entry["file"]
        chunks.append(data)
    data=b"".join(chunks)
    assert sha(data)==manifest["sha256"]
    return data
def restore(dest):
    if dest.exists(): raise SystemExit("Choose a new output directory.")
    data=bundle()
    dest.mkdir(parents=True)
    with tarfile.open(fileobj=io.BytesIO(data), mode="r:gz") as archive:
        for member in archive.getmembers():
            name=Path(member.name)
            if name.is_absolute() or ".." in name.parts or not member.isfile():
                raise SystemExit("Unsafe archive member: "+member.name)
            out=dest/name
            out.parent.mkdir(parents=True,exist_ok=True)
            out.write_bytes(archive.extractfile(member).read())
    print("PROFILE_RESTORE_PASS", dest)
def restore_before(dest):
    if dest.exists(): raise SystemExit("Choose a new output directory.")
    source=E/"api/integrated-20261112"
    shutil.copytree(source,dest)
    for row in read_json(source/"CHANGES.json"):
        path=dest/row["path"]
        if row["before"] is None:
            path.unlink()
        else:
            shutil.copyfile(source/"patches/before"/row["path"],path)
    print("BEFORE_SOURCE_RESTORE_PASS",dest)
def kernel_rows():
    result=[]
    status=read_json(E/"kernels/qgemm/status.json")
    entries=[(r,E/"kernels/qgemm/raw"/r["case"]/"run.log") for r in status["rows"]]
    field=read_json(E/"kernels/field/raw/cycles.json")
    entries += [(r,E/"kernels/field/raw"/(r["case"]+".log")) for r in field["rows"]]
    for row, log in entries:
        text=log.read_text()
        values=re.findall(r"tohost = (-?\d+)",text)
        assert values and "Wall clock time passed:" in text and "Timeout[" not in text, log
        decoded=int(values[-1])
        cycles=decoded-1  # Simulator has already decoded raw HTIF.
        assert cycles==int(row["cycles"]), (log,cycles,row["cycles"])
        result.append({"case":row["case"],"cycles":cycles,
                       "decoded_exit_code":decoded,
                       "raw_log":str(log.relative_to(ROOT)),
                       "log_sha256":sha(log.read_bytes()),
                       "recorded_command":"evaluation/kernels/qgemm/status.json" if "qgemm" in row["case"] else "evaluation/kernels/field/raw/cycles.json",
                       "table":"tab:perf"})
    return result
def observations():
    result=[]
    for p in sorted((E/"api/raw").rglob("*.log")):
        text=p.read_text(errors="replace")
        identity=re.search(r"E2E_BEGIN algorithm=(\S+) parameter=(\S+) implementation=(\S+)",text)
        for match in re.finditer(r"E2E_METRIC\s+(keygen|sign|verify)\s+cycles=(0x[0-9a-fA-F]+|\d+)\s+instret=(0x[0-9a-fA-F]+|\d+)",text):
            result.append(dict(operation=match[1],cycles=int(match[2],0),instret=int(match[3],0),
                evidence=str(p.relative_to(ROOT)),identity=identity.group(0) if identity else "",
                completed="E2E_SUCCESS" in text,kind="guest-counter"))
    for p in sorted((E/"api/raw").rglob("*.json")):
        if p.name not in ("api-stages.json","job.json"): continue
        data=read_json(p)
        entries=data if isinstance(data,list) else data.get("metrics",data.get("saved_api_stages",[]))
        if not isinstance(entries,list): continue
        for row in entries:
            if row.get("operation") not in ("keygen","sign","verify") or "cycles" not in row: continue
            result.append(dict(operation=row["operation"],cycles=int(row["cycles"]),instret=row.get("instret",""),
                evidence=str(p.relative_to(ROOT)),identity="",
                completed=(data.get("state")=="passed") if isinstance(data,dict) else None,
                kind="stage-record"))
    return result
def table_rows():
    tex=(E/"paper/generated/e2e_energy_table_rows.tex").read_text()
    result=[]; family=""
    for line in tex.splitlines():
        if "&" not in line or not line.rstrip().endswith("\\\\"): continue
        cells=[s.strip() for s in line.rsplit("\\\\",1)[0].split("&")]
        family=cells[0] or family
        for operation,cell in zip(("keygen","sign","verify","energy"),cells[2:]):
            match=re.search(r"([0-9.]+)/\\textbf\{([0-9.]+)\}",cell)
            assert match, cell
            result.append(dict(family=family,level=cells[1],operation=operation,
                baseline_display=match[1],xmpk_display=match[2],
                unit="J" if operation=="energy" else "million_cycles",
                table="tab:e2e",table_file="evaluation/paper/generated/e2e_energy_table_rows.tex"))
    assert len(result)==40
    return result
def recompute(dest):
    kernels=kernel_rows()
    obs=observations()
    display=table_rows()
    counts=list(csv.DictReader((E/"paper/generated/e2e_measured.csv").open()))
    trace=[]
    for row in display:
        if row["operation"]=="energy":
            trace.append(dict(**row,variant="both",recorded_cycles="",matched_evidence="",
                status="estimated-from-recorded-cycles-and-vectorless-power",
                command="python3 scripts/evaluation.py recompute --output /tmp/xmpk-evidence",
                summary="evaluation/paper/generated/algorithm_energy.csv"))
            continue
        historical=next(r for r in counts if r["scheme"].startswith(row["family"])
            and r["level"]==row["level"] and r["operation"]==row["operation"])
        for variant in ["baseline","xmpk"]:
            number=int(historical[variant+"_cycles"])
            # Numeric equality alone is deliberately not declared a complete provenance chain.
            matching=[r["evidence"] for r in obs if r["operation"]==row["operation"] and r["cycles"]==number]
            trace.append(dict(**row,variant=variant,recorded_cycles=number,
                matched_evidence=";".join(sorted(set(matching))),
                status="numeric-match-needs-version-and-identity-confirmation" if matching else "final-counter-log-not-found-in-export",
                command="bash scripts/build_api.sh "+api_variant(row["family"],row["level"])+" "+("0" if variant=="baseline" else "1"),
                summary="evaluation/paper/generated/e2e_measured.csv (historical metadata; not final-source attestation)"))
    write_csv(dest/"kernel-cycles.csv",kernels)
    write_csv(dest/"api-observations.csv",obs)
    write_csv(dest/"submitted-table.csv",display)
    write_csv(dest/"traceability.csv",trace)
    ppa=[]
    for p in sorted((E/"paper/sources").glob("*power*.rpt")):
        text=p.read_text()
        values={}
        for key,label in [("total_w","Total On-Chip Power (W)"),("dynamic_w","Dynamic (W)"),("static_w","Device Static (W)")]:
            m=re.search(r"\|\s*"+re.escape(label)+r"\s*\|\s*([0-9.]+)",text)
            if m: values[key]=float(m[1])
        if len(values)==3:
            ppa.append(dict(report=str(p.relative_to(ROOT)),**values,method="post-route vectorless estimate"))
    write_csv(dest/"power.csv",ppa)
    energy=read_json(E/"paper/generated/algorithm_energy.json")
    for r in energy["results"]:
        for side in ("baseline","xmpk"):
            expected=float(r[side+"_power_w"])*int(r[side+"_cycles"])/float(r["frequency_hz"])
            assert abs(expected-float(r[side+"_energy_j"]))<1e-8
    write_json(dest/"coverage.json",{
        "kernel_logs_recomputed":len(kernels),"api_observations":len(obs),
        "submitted_display_entries":len(display),"api_variant_entries":60,
        "api_entries_with_numeric_matches":sum(r["status"].startswith("numeric-match") for r in trace),
        "api_entries_without_matching_final_counter":sum(r["status"]=="final-counter-log-not-found-in-export" for r in trace),
        "complete_final_api_command_source_log_chains":0,
        "limitation":"All displayed API values are author-confirmed measurements. This export does not establish the exact final source/ELF/log linkage for all 60 API variants. Numeric matches alone do not establish that linkage.",
        "energy_arithmetic":"PASS; energy estimates use recorded unclamped cycles and archived vectorless power.",
        "publication_validation":"Offline only; no fresh full API, RTL or Vivado execution."})
    print("RECOMPUTE_PASS",dest)
def api_variant(family,level):
    return {"MAYO":"mayo1" if level=="L1" else "mayo5",
            "MQOM":"mqom1" if level=="L1" else "mqom5",
            "SNOVA":"snova1" if level=="L1" else "snova5",
            "UOV":"uov_is" if level=="L1" else "uov_v",
            "QR-UOV":"qruov_l3" if level=="L1" else "qruov_l5"}[family]
def verify():
    for r in read_json(E/"provenance/export-manifest.json"):
        p=ROOT/r["file"]
        assert sha(p.read_bytes())==r["exported_sha256"],p
        if r["license_notice_preserved"]:
            assert r["original_sha256"]==r["exported_sha256"],p
    with tarfile.open(fileobj=io.BytesIO(bundle()),mode="r:gz") as archive:
        index={r["member"]:r for r in read_json(E/"profiling/member-index.json")}
        for member in archive.getmembers():
            assert sha(archive.extractfile(member).read())==index[member.name]["exported_sha256"]
    kernel_rows(); table_rows()
    source_list=read_json(E/"ppa/source-list.json")
    for name in source_list["sources"]:
        assert (E/"ppa/tops/xmpk_cva6_system_top.sv" if name=="@SYSTEM_TOP@" else E/"ppa/q_overlap/cva6"/name).is_file(),name
    for name in source_list["include_dirs"]:
        assert (E/"ppa/q_overlap/cva6"/name).is_dir(),name
    print("EVALUATION_VERIFY_PASS")
parser=argparse.ArgumentParser(description=__doc__)
sub=parser.add_subparsers(dest="mode",required=True)
sub.add_parser("verify")
p=sub.add_parser("restore-profile");p.add_argument("--output",type=Path,required=True)
p=sub.add_parser("restore-before");p.add_argument("--output",type=Path,required=True)
p=sub.add_parser("recompute");p.add_argument("--output",type=Path,required=True)
args=parser.parse_args()
if args.mode=="verify":verify()
elif args.mode=="restore-profile":restore(args.output)
elif args.mode=="restore-before":restore_before(args.output)
else:recompute(args.output)
