"""Build an isolated final CVA6 simulation; leave running model inputs intact."""
from pathlib import Path
import hashlib
import json
import os
import shlex
import shutil
import subprocess

root = Path(__file__).resolve().parents[1]
home = Path.home()
original = home / "xmpk-arm64/cva6"
workspace = home / "xmpk-final-kernels-20261113"
workspace.mkdir(exist_ok=True)
cva6 = workspace / "cva6"
assert not (workspace / "model").exists(), "Do not overwrite a generated model"
if not cva6.exists():
    shutil.copytree(original, cva6, symlinks=True,
                    ignore=shutil.ignore_patterns("work-ver-arm64", ".git"))
manifest = json.loads((root / "source-hashes.json").read_text())["final"]
for name, digest in manifest.items():
    source = root / "final" / name
    destination = cva6 / name.removeprefix("cva6/")
    assert hashlib.sha256(source.read_bytes()).hexdigest() == digest
    assert destination.exists(), name
    shutil.copy2(source, destination)
out = workspace / "model"
out.mkdir()
(out / "corev_apu").symlink_to(cva6 / "corev_apu", target_is_directory=True)
line = next(x for x in (original / "work-ver-arm64/Variane_testharness__verFiles.dat").read_text().splitlines()
            if x.startswith("C "))
tokens = shlex.split(shlex.split(line)[1])
lf, cf, cc = (tokens.index(x) for x in ("-LDFLAGS", "-CFLAGS", "--cc"))
flags = [x for x in tokens[cf+1:cc] if x != "-DVL_DEBUG"]
command = tokens[:lf] + ["-LDFLAGS", " ".join(tokens[lf+1:cf]),
                         "-CFLAGS", " ".join(flags)] + tokens[cc:]
command = [x.replace(str(original), str(cva6)) for x in command]
command[command.index("--Mdir")+1] = str(out)
env = dict(os.environ, CVA6_REPO_DIR=str(cva6), TARGET_CFG="cv64a6_imafdc_sv39",
           HPDCACHE_DIR=str(cva6 / "core/cache_subsystem/hpdcache"),
           VERILATOR_ROOT=str(home / "toolchain/verilator-v5.008/share/verilator"),
           SPIKE_INSTALL_DIR=str(home / "toolchain/spike-exact"),
           RISCV=str(home / "toolchain/spike-exact"))
logfile = root / "results/final-cpu-build.log"
with logfile.open("w") as log:
    subprocess.run([str(home / "toolchain/verilator-v5.008/bin/verilator_bin")] + command,
                   cwd=cva6, env=env, stdout=log, stderr=subprocess.STDOUT, check=True)
    subprocess.run(["nice", "-n", "10", "make", "-j2", "-f", "Variane_testharness.mk",
                    "CXX=g++", "OPT_FAST=-O3 -march=native", "OPT_SLOW=-O3 -march=native",
                    "OPT_GLOBAL=-O3 -march=native",
                    "VM_USER_CFLAGS=" + " ".join(x.replace(str(original), str(cva6)) for x in flags),
                    "VM_USER_DIR=" + str(cva6 / "corev_apu/tb") + " " + str(cva6 / "corev_apu/tb/dpi")],
                   cwd=out, env=env, stdout=log, stderr=subprocess.STDOUT, check=True)
binary = out / "Variane_testharness"
(root / "results/final-cpu-model.json").write_text(json.dumps({
    "binary": str(binary), "sha256": hashlib.sha256(binary.read_bytes()).hexdigest(),
    "source_manifest": str(root / "source-hashes.json"), "command": command,
}, indent=2) + "\n")
print("FINAL_CPU_MODEL_BUILD_PASS", flush=True)
