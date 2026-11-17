#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0 WITH SHL-2.0
"""Build and run instruction-level regressions from this repository."""
import argparse
import os
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]
p = argparse.ArgumentParser()
p.add_argument("--verilator", default=os.environ.get("VERILATOR", "verilator"))
p.add_argument("--output", type=Path, default=ROOT / "build/regression")
p.add_argument("--jobs", type=int, default=2)
args = p.parse_args()
out = args.output.resolve() / "DECODER"
out.mkdir(parents=True, exist_ok=True)
with (out / "build.log").open("w") as log:
    subprocess.run([args.verilator,"--cc","--exe","--build","--assert","-j",str(args.jobs),
                    "-Wno-fatal","--top-module","decoder_top","--Mdir",str(out/"obj"),
                    "-CFLAGS","-O2 -I"+str(ROOT/"tests"),
                    str(ROOT/"rtl/include/cvxif_instr_pkg.sv"),str(ROOT/"rtl/instr_decoder.sv"),
                    str(ROOT/"tests/decoder_top.sv"),str(ROOT/"tests/decoder_harness.cpp")],
                   stdout=log,stderr=subprocess.STDOUT,check=True)
with (out/"test.log").open("w") as log:
    subprocess.run([str(out/"obj/Vdecoder_top")],stdout=log,stderr=subprocess.STDOUT,check=True)
print("DECODER PASS",flush=True)
sources = [str(ROOT / line.strip()) for line in (ROOT / "rtl/files.f").read_text().splitlines()
           if line.strip() and not line.startswith("#")]
for kind, index in [("EXT", 0), ("Q", 1), ("SOLVE", 2), ("MQ", 3), ("LIN", 4), ("ARITH", 5)]:
    out = args.output.resolve() / kind
    out.mkdir(parents=True, exist_ok=True)
    command = [args.verilator, "--cc", "--exe", "--build", "--assert", "-j", str(args.jobs),
               "-Wno-fatal", "--top-module", "instruction_top", f"-GTEST_KIND={index}",
               "--Mdir", str(out / "obj"), "-CFLAGS",
               f'-O2 -D{kind} -DDUT_CLASS=Vinstruction_top -DHEADER=\\"Vinstruction_top.h\\"',
               *sources, str(ROOT / "tests/instruction_top.sv"),
               str(ROOT / "tests/engine_harness.cpp")]
    with (out / "build.log").open("w") as log:
        subprocess.run(command, stdout=log, stderr=subprocess.STDOUT, check=True)
    for layout in [""]:
        with (out / ("test" + ("-packed" if layout else "") + ".log")).open("w") as log:
            subprocess.run([str(out / "obj/Vinstruction_top"), *([layout] if layout else [])],
                           env=dict(os.environ, XMPK_CACHE_EDGES="1"),
                           stdout=log, stderr=subprocess.STDOUT, check=True, timeout=300)
        print(kind, layout, "PASS", flush=True)
print("ALL_REGRESSIONS_PASS")
