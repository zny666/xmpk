#!/usr/bin/env python3
"""Apply the Xmpk overlay to an otherwise clean, pinned CVA6 checkout."""
import argparse
from pathlib import Path
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[1]
BASE = "742dc82a0c706e0a3bfb98b709fd3ee06d50d12a"
p = argparse.ArgumentParser()
p.add_argument("cva6", type=Path)
args = p.parse_args()
cpu = args.cva6.resolve()
def git(*args):
    return subprocess.check_output(["git", "-C", str(cpu), *args], text=True).strip()
if git("rev-parse", "HEAD") != BASE:
    raise SystemExit("CVA6 checkout does not match the pinned base")
if git("status", "--porcelain", "--untracked-files=no", "--ignore-submodules=all"):
    raise SystemExit("CVA6 tracked files must be clean before applying the overlay")
patch = str(ROOT / "integration/cva6.patch")
subprocess.run(["git", "-C", str(cpu), "apply", "--check", patch], check=True)
subprocess.run(["git", "-C", str(cpu), "apply", patch], check=True)
for source in (ROOT / "rtl").glob("*.sv"):
    shutil.copy2(source, cpu / "core/cvxif_example" / source.name)
shutil.copy2(ROOT / "rtl/include/cvxif_instr_pkg.sv",
             cpu / "core/cvxif_example/include/cvxif_instr_pkg.sv")
shutil.copy2(ROOT / "integration/xmpk_mem_adapter.sv", cpu / "core/xmpk_mem_adapter.sv")
print("CVA6_OVERLAY_PASS")
