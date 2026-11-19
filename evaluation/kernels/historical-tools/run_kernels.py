"""Measure architectural cycles with matched ELFs on the final CVA6 model."""
from pathlib import Path
import hashlib
import json
import os
import re
import subprocess

root = Path(__file__).resolve().parents[1]
model = json.loads((root / "results/final-cpu-model.json").read_text())
binary = Path(model["binary"])
assert hashlib.sha256(binary.read_bytes()).hexdigest() == model["sha256"]
output = root / "results/final-kernels"
output.mkdir(exist_ok=True)
rows = []
for elf in sorted((root / "kernel-elf").glob("*.elf")):
    name = elf.stem
    logpath = output / (name + ".log")
    args = [str(binary), str(elf), "+debug_disable=1", "+rvfi_trace_disable=1",
            "+time_out=30000000", "++" + str(elf), "+elf_file=" + str(elf),
            "+core_name=cv64a6_imafdc_sv39", "+tohost_addr=0000000080001000",
            "+signature=" + str(output / (name + ".signature")),
            "+UVM_TESTNAME=uvmt_cva6_firmware_test_c",
            "+report_file=" + str(output / (name + ".yaml"))]
    env = dict(os.environ, LD_LIBRARY_PATH=str(Path.home() / "toolchain/spike-exact/lib"))
    # ariane_tb.cpp prints an already decoded exit_code(), not raw tohost.
    # Reuse completed logs only when they belong to this unchanged ELF/model run.
    text = logpath.read_text(errors="replace") if logpath.exists() else ""
    completed = re.search(r"tohost = (-?\d+)", text) and "Wall clock time passed:" in text
    if not completed:
        with logpath.open("w") as log:
            process = subprocess.run(["nice", "-n", "10"] + args, cwd=output,
                                     env=env, stdout=log, stderr=subprocess.STDOUT, timeout=900)
        returncode = process.returncode
    else:
        returncode = int(re.findall(r"tohost = (-?\d+)", text)[-1]) & 255
    text = logpath.read_text(errors="replace")
    hosts = re.findall(r"tohost = (-?\d+)", text)
    assert hosts and "Timeout[" not in text, name
    host = int(hosts[-1])
    assert host > 1 and host not in (1337, 0x3fffffff, 2147483647), (name, host)
    cycles = host - 1
    assert 0 < cycles < 30000000, (name, cycles)
    row = {"case": name, "cycles": cycles, "tohost": host,
           "process_exit": returncode, "exit_encoding": "decoded exit_code = rdcycle delta + 1",
           "elf_sha256": hashlib.sha256(elf.read_bytes()).hexdigest(),
           "log_sha256": hashlib.sha256(logpath.read_bytes()).hexdigest()}
    rows.append(row)
    (output / "cycles.json").write_text(json.dumps({
        "model": model, "compiler_flags": "-O2 -fno-builtin -march=rv64gc -mabi=lp64d",
        "measurement": "rdcycle; full word-by-word output validation before encoded tohost",
        "rows": rows,
    }, indent=2) + "\n")
    print(name, cycles, "PASS", flush=True)
assert len(rows) == 12
print("FINAL_KERNEL_MEASUREMENT_PASS", flush=True)
