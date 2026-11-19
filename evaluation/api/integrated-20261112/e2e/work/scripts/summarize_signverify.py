#!/usr/bin/env python3
"""Summarize measured short-test cycles and validate the native golden."""
import csv
from pathlib import Path
import sys

root = Path(sys.argv[1])
with (root / "results/metrics.csv").open() as source:
    rows = list(csv.DictReader(source))
golden = (root / "golden.bin").read_bytes()
for row in rows:
    actual = (root / "results" / row["tag"] / "serialized.bin").read_bytes()
    if actual != golden:
        raise SystemExit(f"Native golden mismatch: {row['tag']}")
    row["native_golden"] = "PASS"
reference = next(row for row in rows if int(row["flags"]) == 0)
for row in rows:
    for stage in ("sign", "verify"):
        old, new = int(reference[stage+"_total"]), int(row[stage+"_total"])
        row[stage+"_speedup"] = f"{old/new:.6f}"
        row[stage+"_cycle_savings_pct"] = f"{100*(1-new/old):.6f}"
with (root / "results/ablation.csv").open("w") as out:
    writer = csv.DictWriter(out, fieldnames=list(rows[0]))
    writer.writeheader()
    writer.writerows(rows)
for row in rows:
    print(f"flags={row['flags']}: Sign {row['sign_total']} "
          f"({row['sign_speedup']}x, {row['sign_cycle_savings_pct']}% cycles saved); "
          f"Verify {row['verify_total']} "
          f"({row['verify_speedup']}x, {row['verify_cycle_savings_pct']}% cycles saved)")
