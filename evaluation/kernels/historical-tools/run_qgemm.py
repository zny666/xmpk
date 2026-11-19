"""Measure archived QGEMM benchmark ELFs on the final matched CVA6 model."""
from pathlib import Path
import hashlib
import json
import os
import re
import subprocess

root = Path(__file__).resolve().parents[1]
archive = root / "qgemm-elf"
elfs = sorted(archive.glob("*.elf"))
assert len(elfs) == 6, f"Expected six archived ELFs visible in {archive}"
model = json.loads((root / "results/final-cpu-model.json").read_text())
binary = Path(model["binary"])
assert hashlib.sha256(binary.read_bytes()).hexdigest() == model["sha256"]
assert "ALL_PASS" in (root / "results/instruction-final-Q/test.log").read_text()
out = root / "results/final-qgemm"
out.mkdir(exist_ok=True)
rows = []
for elf in elfs:
    match = re.fullmatch(r"xmpk_qgemm_perf_(\d+)_(\d+)_(\d+)_(hw|scalar)", elf.stem)
    assert match, elf
    m, k, words = map(int, match.groups()[:3])
    kind = "hardware" if match[4] == "hw" else "scalar"
    name = f"qgemm-{m}-{k}-{16*words}-{kind}"
    logpath = out / (name + ".log")
    assert not logpath.exists(), f"Refusing to replace an existing run: {name}"
    args = [str(binary), str(elf), "+debug_disable=1", "+rvfi_trace_disable=1",
            "+time_out=30000000", "++" + str(elf), "+elf_file=" + str(elf),
            "+core_name=cv64a6_imafdc_sv39", "+tohost_addr=0000000080001000",
            "+signature=" + str(out / (name + ".signature")),
            "+UVM_TESTNAME=uvmt_cva6_firmware_test_c",
            "+report_file=" + str(out / (name + ".yaml"))]
    env = dict(os.environ, LD_LIBRARY_PATH=str(Path.home() / "toolchain/spike-exact/lib"))
    with logpath.open("w") as log:
        process = subprocess.run(["nice", "-n", "10"] + args, cwd=out,
                                 env=env, stdout=log, stderr=subprocess.STDOUT, timeout=900)
    text = logpath.read_text(errors="replace")
    hosts = re.findall(r"tohost = (-?\d+)", text)
    assert hosts and "Timeout[" not in text, name
    host = int(hosts[-1])
    assert host > 1 and host not in (890, 1337, 0x3fffffff, 2147483647), (name, host)
    assert process.returncode == (host & 255), (name, process.returncode, host)
    cycles = host - 1
    assert 0 < cycles < 30000000
    rows.append({"case": name, "m": m, "k": k, "n": 16*words,
                 "implementation": kind, "cycles": cycles,
                 "decoded_exit_code": host, "process_exit": process.returncode,
                 "elf": str(elf), "elf_sha256": hashlib.sha256(elf.read_bytes()).hexdigest(),
                 "log_sha256": hashlib.sha256(logpath.read_bytes()).hexdigest()})
    (out / "cycles.json").write_text(json.dumps({
        "model": model,
        "compiler_flags": "-O2 -fno-builtin; archived ELF compiler command in qgemm run logs",
        "measurement": "rdcycle delta; QGEMM SETUP excluded; decoded exit_code = cycles + 1",
        "correctness": "hardware PERF_ONLY checks returned MAC count; full payload checked by final instruction-Q regression",
        "rows": rows,
    }, indent=2) + "\n")
    print(name, cycles, "PASS", flush=True)
assert len(rows) == 6
print("FINAL_QGEMM_MEASUREMENT_PASS", flush=True)
