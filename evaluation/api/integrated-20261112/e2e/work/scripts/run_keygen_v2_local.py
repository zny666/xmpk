#!/usr/bin/env python3
"""Run up to three local RTL short tests and compare serialized bytes exactly."""
import argparse
import concurrent.futures
import csv
import hashlib
import json
from pathlib import Path
import re
import shlex
import subprocess
import time


def run_one(elf, root, vm, marker):
    tag = elf.stem
    guest = r'''
set -u
ROOT="$HOME/xmpk-arm64"
TAG=__TAG__
ELF=__ELF__
OUT="$ROOT/formal/results/$TAG"
mkdir -p "$OUT"
export HARNESS="$ROOT/cva6/work-ver-arm64/Variane_testharness"
export HTIF_SERVICE="$ROOT/scripts/service_htif_writes.py"
export LD_LIBRARY_PATH="$HOME/toolchain/spike-exact/lib"
export SUCCESS_MARKER=__MARKER__
# The shared runner validates the selected short-test success marker.
bash __RUNNER__ "$ELF" 100000000 "$OUT" "$TAG"
status=$?
mkdir -p __DEST__
cp -a "$OUT/." __DEST__/
exit "$status"
'''
    guest = (guest.replace("__TAG__", shlex.quote(tag))
             .replace("__ELF__", shlex.quote(str(elf)))
             .replace("__RUNNER__", shlex.quote(str(root / "run_short.sh")))
             .replace("__MARKER__", shlex.quote(marker))
             .replace("__DEST__", shlex.quote(str(root / "results" / tag))))
    start = time.monotonic()
    proc = subprocess.run(
        ["limactl", "shell", vm, "--", "bash", "-lc", guest],
        capture_output=True, text=True)
    log = root / "results" / tag / (tag + ".log")
    text = log.read_text(errors="replace") if log.exists() else ""
    guest_log = log.parent / (tag + ".guest-output.log")
    guest_text = guest_log.read_text() if guest_log.exists() else ""
    if proc.returncode or marker not in guest_text:
        raise RuntimeError(f"{tag}: rc={proc.returncode} {proc.stderr}\n{text[-2000:]}")
    exits = re.findall(r"tohost\s*=\s*(-?\d+)", text)
    if not exits or int(exits[-1]) != 0:
        raise RuntimeError(f"{tag}: missing clean tohost=0 exit")
    # The harness may overwrite the start of the merged log at exit.
    # Dedicated guest output is authoritative for metrics and bytes.
    metrics = {k: int(v, 16) for k, v in
               re.findall(r"^(\w+)=0x([0-9a-fA-F]+)$", guest_text, re.M)}
    serialized = re.search(r"^SERIALIZED_HEX=([0-9a-f]+)$", guest_text, re.M)
    if not serialized:
        raise RuntimeError(f"{tag}: serialized output missing")
    data = bytes.fromhex(serialized.group(1))
    (root / "results" / tag / "serialized.bin").write_bytes(data)
    metrics.update(tag=tag, sha256=hashlib.sha256(data).hexdigest(),
                   wall_seconds=round(time.monotonic() - start, 3))
    print(json.dumps(metrics), flush=True)
    return metrics, data


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("root", type=Path)
    parser.add_argument("--vm", default="xmpk-arm64-build")
    parser.add_argument("--jobs", type=int, default=3)
    parser.add_argument("--pattern", default="*.elf")
    parser.add_argument("--marker", default="KEYGEN_V2_SUCCESS")
    parser.add_argument("--golden", type=Path)
    args = parser.parse_args()
    files = sorted((args.root / "elf").glob(args.pattern),
                   key=lambda p: int(re.search(r"-f(\d+)", p.stem).group(1)))
    if not files:
        raise SystemExit("No ELF variants")
    results = []
    with concurrent.futures.ThreadPoolExecutor(max_workers=args.jobs) as pool:
        futures = [pool.submit(run_one, elf, args.root, args.vm, args.marker) for elf in files]
        for future in concurrent.futures.as_completed(futures):
            results.append(future.result())
    reference = {}
    for metrics, data in results:
        if metrics["flags"] == 0:
            reference[metrics["eq_base"], metrics["eq_count"]] = data
    for metrics, data in results:
        key = metrics["eq_base"], metrics["eq_count"]
        if key not in reference:
            raise RuntimeError(f"{metrics['tag']}: matching reference absent")
        if data != reference[key]:
            raise RuntimeError(f"{metrics['tag']}: serialized bytes differ")
        if args.golden and data != args.golden.read_bytes():
            raise RuntimeError(f"{metrics['tag']}: native golden bytes differ")
        metrics["correctness"] = "PASS"
    rows = sorted((r for r, _ in results),
                  key=lambda r: (r["eq_base"], r["eq_count"], r["flags"]))
    with (args.root / "results" / "metrics.csv").open("w") as out:
        writer = csv.DictWriter(out, fieldnames=list(rows[0]))
        writer.writeheader()
        writer.writerows(rows)
    print("ALL_SERIALIZED_OUTPUTS_MATCH", flush=True)


if __name__ == "__main__":
    main()
