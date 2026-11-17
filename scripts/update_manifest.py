#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0 WITH SHL-2.0
"""Refresh source manifests after adding supporting reproduction materials."""
import hashlib
import json
from pathlib import Path
import subprocess
ROOT=Path(__file__).resolve().parents[1]
def digest(path): return hashlib.sha256(path.read_bytes()).hexdigest()
snapshot=json.loads((ROOT/"snapshot.json").read_text())
for entry in snapshot["rtl_files"]:
    if digest(ROOT/entry["file"])!=entry["release_sha256"]:
        raise SystemExit("Released RTL changed: "+entry["file"])
names=sorted(set(subprocess.check_output(
    ["git","ls-files","--cached","--others","--exclude-standard"],cwd=ROOT,text=True).splitlines()))
names=[name for name in names if (ROOT/name).is_file() and name!="SHA256SUMS"]
snapshot["scope"]="Adopted RTL, instruction tests/ISA software, pinned CVA6 integration, kernel measurements and coprocessor OOC FPGA reproduction"
snapshot["support_files"]=[{"file":name,"sha256":digest(ROOT/name)}
                           for name in names if not name.startswith("rtl/") and name!="snapshot.json"]
(ROOT/"snapshot.json").write_text(json.dumps(snapshot,indent=2)+"\n")
(ROOT/"SHA256SUMS").write_text("".join(f"{digest(ROOT/name)}  {name}\n" for name in names))
print("RELEASE_MANIFEST_PASS",len(names),"files; 17 unchanged RTL files")
