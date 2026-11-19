#!/usr/bin/env python3
"""Summarize local RTL logs; reject mismatching serialized results."""
import csv
import hashlib
from pathlib import Path
import re
import sys

root = Path(sys.argv[1])
rows = []
for log in (root / "results").glob("*/*.log"):
    if log.name.endswith((".guest-output.log", ".htif-service.log")):
        continue
    text = log.read_text(errors="replace")
    if "KEYGEN_V2_SUCCESS" not in text:
        continue
    exits = re.findall(r"tohost\s*=\s*(-?\d+)", text)
    if not exits or int(exits[-1]) != 0:
        raise SystemExit(f"Invalid exit: {log}")
    guest_log = log.parent / (log.parent.name + ".guest-output.log")
    guest_text = guest_log.read_text()
    metrics = {k: int(v, 16) for k, v in
               re.findall(r"^(\w+)=0x([0-9a-fA-F]+)$", guest_text, re.M)}
    match = re.search(r"^SERIALIZED_HEX=([0-9a-f]+)$", guest_text, re.M)
    if not match:
        raise SystemExit(f"Output missing: {log}")
    data = bytes.fromhex(match.group(1))
    metrics.update(tag=log.parent.name,
                   sha256=hashlib.sha256(data).hexdigest())
    rows.append((metrics, data))
references = {(r["eq_base"], r["eq_count"]): (r, data)
              for r, data in rows if r["flags"] == 0}
if not references:
    raise SystemExit("Reference absent")
for row, data in rows:
    reference, expected = references[row["eq_base"], row["eq_count"]]
    if data != expected:
        raise SystemExit(f"Mismatch: {row['tag']}")
    row.update(correctness="PASS",
               speedup=round(reference["total"] / row["total"], 6),
               cycle_savings_pct=round(
                   100 * (1 - row["total"] / reference["total"]), 6))
results = sorted((r for r, _ in rows),
                 key=lambda r: (r["eq_base"], r["eq_count"], r["flags"]))
with (root / "results" / "ablation.csv").open("w") as out:
    writer = csv.DictWriter(out, fieldnames=list(results[0]))
    writer.writeheader()
    writer.writerows(results)
for r in results:
    print(f"mask={r['flags']:2d} cycles={r['total']:,} "
          f"speedup={r['speedup']:.6f}x "
          f"cycle_savings={r['cycle_savings_pct']:.3f}% PASS")
