#!/usr/bin/env python3
"""Independent latency/backpressure sweep; preserves the frozen RTL and harness."""
import hashlib
import json
import pathlib
import re
import subprocess
import time
import os

ROOT = pathlib.Path(__file__).resolve().parents[1]
OUT = ROOT / "results" / "supplemental-20261114"
OUT.mkdir(exist_ok=True)
original = (ROOT / "tests/harness.cpp").read_text()
text = original[:original.index("int main(int argc,char**argv)")]
text = text.replace("static int stall, fault_at=-1;",
                    "static int stall, fault_at=-1, latency=1, period=0, seed_id=0;")
text = text.replace("!stall||ticks%5!=0", "!period||ticks%period!=0")
text = text.replace("due=ticks+1+(stall?3:0)", "due=ticks+latency")
text = text.replace("stall=%d cycles=", "stall=%d latency=%d period=%d seed=%d cycles=")
text = text.replace("kind,m,k,n,mode,stall,", "kind,m,k,n,mode,stall,latency,period,seed_id,")
text = text.replace("static void metrics(", r'''
static uint64_t output_hash() {
  uint64_t h=14695981039346656037ULL;
  for(unsigned i=0x60000;i<0x70000;i++)h=(h^mem[i])*1099511628211ULL;
  return h;
}
static void metrics(''')
text = text.replace('b_reads=%llu PASS', 'b_reads=%llu output_hash=%016llx PASS')
text = text.replace('(unsigned long long)(b_reads-br));',
                    '(unsigned long long)(b_reads-br),(unsigned long long)output_hash());')
assert "ticks+latency" in text and "seed_id," in text
text += r'''
int main(int argc,char**argv) {
  Verilated::commandArgs(argc,argv);
  try {
    for(latency: {1,2,4,8,16}) {} // replaced below with valid C++ loop
    return 0;
  } catch(const std::exception&e) {
    fprintf(stderr,"FAIL tick=%llu: %s\n",(unsigned long long)ticks,e.what());
    return 1;
  }
}
'''
text = text.replace(
    'for(latency: {1,2,4,8,16}) {} // replaced below with valid C++ loop',
    r'''
    for(int delay: {1,2,4,8,16}) for(int gap: {0,5,2})
      for(seed_id=0;seed_id<2;seed_id++) {
        latency=delay;period=gap;stall=gap!=0;
        ticks=reads=writes=b_reads=0;pending=false;fault_at=-1;
        rng.seed(0x225613+seed_id);
        d.rst_ni=0;for(int i=0;i<4;i++)tick();d.rst_ni=1;tick();
#ifdef EXT
        for(int l: {3,10})
          for(auto shape: std::vector<std::vector<int>>{
              {1,16,16},{8,16,16},{8,17,16},{8,16,17}})
            ext(shape[0],shape[1],shape[2],l,false);
#endif
#ifdef Q
        for(int mode: {0,1,2,3,4})
          for(auto shape: std::vector<std::vector<int>>{
              {4,16,16},{4,17,16},{4,248,2},{4,249,2},{4,255,2},
              {2,1,256},{2,1,257}})
            qtest(shape[0],shape[1],shape[2],mode);
#endif
#ifdef SOLVE
        for(int bits: {4,8}) for(int n: {16,32,45,64})
          for(int kind: {0,1})solve(n,bits,kind);
        for(int n: {72,96})for(int kind: {0,1})solve(n,8,kind);
#endif
      }
    puts("ALL_PASS");
''')
(OUT / "harness.cpp").write_text(text)
rtl = ROOT / "final/cva6/core/cvxif_example"
sources = [rtl / "include/cvxif_instr_pkg.sv"]
sources += [rtl / (name + ".sv") for name in (
    "gf2n_mul gf256_inv8 gf256_inv_rom gf_assist xmpk_alu xmpk_ger_trf "
    "xmpk_qgemm xmpk_mqeval xmpk_solveq xmpk_linxfm xmpk_extgemm "
    "xmpk_slow_mul copro_alu instr_decoder").split()]
sources += [ROOT / "tests/instruction_top.sv", OUT / "harness.cpp"]
sha = lambda p: hashlib.sha256(p.read_bytes()).hexdigest()
manifest = {
    "started": time.time(), "host": dict(zip(
        ("sysname", "nodename", "release", "version", "machine"), os.uname())),
    "sources": {str(p): sha(p) for p in sources},
    "original_harness_sha256": sha(ROOT / "tests/harness.cpp"),
    "timing": "standalone rising-edge ticks: RUN issue through completion consumption; SETUP excluded",
    "memory": "one outstanding 8-byte request; response at accepted_tick + latency; writes also acknowledged",
    "backpressure": "period=0 always ready; otherwise not ready when tick modulo period is zero",
    "seeds": [0x225613, 0x225614],
}
verilator = pathlib.Path.home() / "toolchain/verilator-v5.008/bin/verilator"
manifest["verilator"] = subprocess.check_output([str(verilator), "--version"], text=True).strip()
(OUT / "manifest.json").write_text(json.dumps(manifest, indent=2))
for kind, index in (("EXT", 0), ("Q", 1), ("SOLVE", 2)):
    dest = OUT / kind
    dest.mkdir(exist_ok=True)
    cmd = ["nice", "-n", "10", str(verilator), "--cc", "--exe", "--build",
           "--assert", "-j", "1", "-Wno-fatal", "--top-module", "instruction_top",
           f"-GTEST_KIND={index}", "--Mdir", str(dest / "obj"),
           "-CFLAGS", f'-O2 -D{kind} -DDUT_CLASS=Vinstruction_top -DHEADER=\\"Vinstruction_top.h\\"',
           *map(str, sources)]
    manifest.setdefault("commands", {})[kind] = cmd
    (OUT / "manifest.json").write_text(json.dumps(manifest, indent=2))
    print(f"BUILD {kind}", flush=True)
    with (dest / "build.log").open("w") as log:
        subprocess.run(cmd, stdout=log, stderr=subprocess.STDOUT, check=True)
    exe = dest / "obj/Vinstruction_top"
    manifest.setdefault("executable_sha256", {})[kind] = sha(exe)
    print(f"RUN {kind}", flush=True)
    with (dest / "test.log").open("w") as log:
        subprocess.run(["nice", "-n", "10", str(exe)], stdout=log,
                       stderr=subprocess.STDOUT, check=True)
    print(f"PASS {kind}", flush=True)
manifest["finished"] = time.time()
assert all(sha(p) == manifest["sources"][str(p)] for p in sources)
(OUT / "manifest.json").write_text(json.dumps(manifest, indent=2))
print("ALL_DONE", flush=True)
