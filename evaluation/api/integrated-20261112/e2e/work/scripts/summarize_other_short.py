#!/usr/bin/env python3
"""Aggregate current-helper-relative cycle reductions, retaining ablation controls."""
import argparse
import csv
from pathlib import Path

parser = argparse.ArgumentParser()
parser.add_argument("root", type=Path)
args = parser.parse_args()
names = {1: "MAYO", 2: "MQOM", 3: "SNOVA", 4: "UOV-Is"}
variants = {
    1: {0: "current rowwise", 1: "zero-copy rowwise + clear",
        2: "current batched", 3: "zero-copy batched no clear",
        4: "zero-copy rowwise no clear"},
    2: {0: "current QGEMM batch3", 1: "triangular MUL8"},
    3: {0: "current prepared LINXFM", 1: "word packing + clear",
        3: "word packing no clear"},
    4: {0: "current GF16 batch64", 1: "packed windows + clear",
        3: "packed windows no clear"},
}
rows = []
for algo in names:
    with (args.root / f"a{algo}/results/metrics.csv").open() as f:
        records = list(csv.DictReader(f))
    base = next(r for r in records if r["flags"] == "0")
    for r in records:
        for level in ("l1", "l5"):
            total = int(r[f"total_{level}"])
            original = int(base[f"total_{level}"])
            c = int(r[f"compute_{level}"])
            clean = int(r[f"cleanup_{level}"])
            rows.append(dict(
                algorithm=names[algo], case=level if algo != 4 else
                    ("V96-offset0" if level == "l1" else "V96-offset3"),
                flags=r["flags"], variant=variants[algo][int(r["flags"])],
                total_cycles=total, engine_run_cycles=c, cleanup_cycles=clean,
                prepare_cycles=int(r[f"prepare_{level}"]),
                other_cycles=total-c-clean,
                relative_speedup=original/total,
                cycle_reduction_percent=100*(1-total/original),
                correctness=r["correctness"], sha256=r["sha256"]))
with (args.root / "ablation.csv").open("w") as f:
    writer = csv.DictWriter(f, fieldnames=list(rows[0]))
    writer.writeheader()
    writer.writerows(rows)
for r in rows:
    print(f'{r["algorithm"]:7s} {r["case"]:12s} f{r["flags"]} '
          f'{r["total_cycles"]:9,d} cycles '
          f'{r["relative_speedup"]:.5f}x '
          f'{r["cycle_reduction_percent"]:+.3f}%')
