#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0 WITH SHL-2.0
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
ROOT = Path(__file__).resolve().parents[1]
p = argparse.ArgumentParser()
p.add_argument("--cc", default=os.environ.get("RISCV_CC", "riscv64-unknown-elf-gcc"))
p.add_argument("--output", type=Path, default=ROOT / "build/elf")
args = p.parse_args()
out = args.output.resolve()
out.mkdir(parents=True, exist_ok=True)
sw = ROOT / "software"
flags = ["-O2", "-fno-builtin", "-fno-tree-loop-distribute-patterns", "-static",
         "-mcmodel=medany", "-nostdlib", "-nostartfiles", "-march=rv64gc", "-mabi=lp64d",
         "-I", str(sw), "-T", str(sw / "link.ld")]
cases = [("example", "example.c", [], "correctness")]
for n in (45, 72, 96):
    for kind in ("hardware", "scalar"):
        defs = ["-DPERF_SOLVE256", f"-DN={n}"] + (["-DPERF_SCALAR"] if kind == "scalar" else [])
        cases.append((f"solve-{n}-{kind}", "field_benchmark.c", defs, "SETUP+RUN" if kind=="hardware" else "scalar solve"))
for degree, n in ((3, 52), (10, 54)):
    for kind in ("hardware", "scalar", "layout"):
        defs = ["-DPERF_EXTGEMM", f"-DL={degree}", f"-DM={n}", f"-DK={n}", "-DNCOL=1"]
        if kind != "hardware": defs += ["-DPERF_" + kind.upper()]
        cases.append((f"ext-{degree}-{kind}", "field_benchmark.c", defs,
                      "layout+SETUP+RUN+layout" if kind=="layout" else "scalar EXT" if kind=="scalar" else "SETUP+RUN"))
for m,k,w in ((3,5,1),(7,19,2),(16,64,2)):
    for kind in ("hardware","scalar"):
        defs = ["-DPERF_ONLY",f"-DPERF_M={m}",f"-DPERF_K={k}",f"-DPERF_NWORDS={w}"]
        if kind == "scalar": defs += ["-DPERF_SCALAR"]
        cases.append((f"qgemm-{m}-{k}-{16*w}-{kind}","qgemm_benchmark.c",defs,
                      "RUN; SETUP excluded" if kind=="hardware" else "scalar QGEMM"))
rows = []
with (out / "compile.log").open("w") as log:
    subprocess.run([args.cc, "-O2", "-march=rv64gc", "-mabi=lp64d", "-I", str(sw), "-c",
                    str(ROOT / "tests/header_probe.c"), "-o", str(out / "header_probe.o")],
                   stdout=log, stderr=subprocess.STDOUT, check=True)
    for name,source,defs,scope in cases:
        elf = out / (name + ".elf")
        command = [args.cc,*flags,*defs,str(sw/source),str(sw/"crt.S"),"-lgcc","-o",str(elf)]
        subprocess.run(command, stdout=log, stderr=subprocess.STDOUT, check=True)
        rows.append({"case":name,"elf":elf.name,"scope":scope,"command":command,
                     "sha256":hashlib.sha256(elf.read_bytes()).hexdigest()})
(out/"manifest.json").write_text(json.dumps({
    "compiler":subprocess.check_output([args.cc,"--version"],text=True).splitlines()[0],
    "entries":rows},indent=2)+"\n")
print("BENCHMARK_COMPILE_PASS", len(rows))
