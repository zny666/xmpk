#!/usr/bin/env python3
"""Conditional E2E predictions, retaining empirical-shape/transfer distinctions."""
import argparse
import csv
from pathlib import Path

p = argparse.ArgumentParser()
p.add_argument("--root", type=Path, default=Path("/artifact/xmpk-arm64-native/other-short"))
p.add_argument("--e2e", type=Path, default=Path("/artifact/xmpk-dac2027-paper/generated/e2e_before_refinements.csv"))
p.add_argument("--output", type=Path, required=True)
a = p.parse_args()
with (a.root/"estimated_hotspot_share.csv").open() as f:
    shares = {(r["scheme"], r["operation"]): r for r in csv.DictReader(f)
              if r["scope"] != "V*L/P2 only"}
with a.e2e.open() as f:
    saved = {(r["scheme"], r["operation"]): int(r["xmpk_cycles"])
             for r in csv.DictReader(f) if r["xmpk_cycles"]}
rows = []


def local_body(algo, level, flags):
    with (a.root/f"a{algo}/results/metrics.csv").open() as f:
        r = next(r for r in csv.DictReader(f) if int(r["flags"]) == flags)
    return int(r[f"total_{level}"])-int(r[f"cleanup_{level}"])


def stage(scheme, op, reduction, reduction_low, reduction_high, assumption):
    s = shares[scheme, op]
    old = int(s["denominator_cycles"])
    fraction = float(s["estimated_share_pct"])/100
    low = float(s["engineering_low_pct"])/100*reduction_low
    high = float(s["engineering_high_pct"])/100*reduction_high
    drop = fraction*reduction
    rows.append(dict(scheme=scheme, operation=op, old_cycles=old,
                     predicted_new_cycles=round(old*(1-drop)),
                     stage_cycle_reduction_pct=100*drop,
                     stage_cycle_reduction_low_pct=100*low,
                     stage_cycle_reduction_high_pct=100*high,
                     speedup=1/(1-drop), speedup_low=1/(1-low),
                     speedup_high=1/(1-high),
                     denominator_source=s["denominator_source"],
                     status="CONDITIONAL_ESTIMATE", assumption=assumption))


for scheme, level in (("MAYO-1", "l1"), ("MAYO-5", "l5")):
    r = 1-local_body(1, level, 4)/local_body(1, level, 0)
    stage(scheme, "sign", r, r, r,
          "row-major no-copy/no-clear; measured width scaling extended to both products")
for scheme, level in (("MQOM-L1", "l1"), ("MQOM-L5", "l5")):
    r = 1-local_body(2, level, 1)/local_body(2, level, 0)
    for op in ("sign", "verify"):
        stage(scheme, op, r, r, r, "triangular MUL8; partial batch cost scaled")
r = 1-local_body(3, "l5", 3)/local_body(3, "l5", 0)
stage("SNOVA-V", "keygen", r, r, r, "prepared packing/no-clear; same measured shape")
stage("SNOVA-V", "sign", r, r, r,
      "IF batch1 and cols6 retain measured batch5/cols5 relative gain; untested transfer")
uov_rs = [1-local_body(4, l, 3)/local_body(4, l, 0) for l in ("l1", "l5")]
stage("UOV-Is", "keygen", sum(uov_rs)/2, min(uov_rs), max(uov_rs),
      "IF inner1..96 retain reductions bounded by measured offset0/3 shapes")

# Whole request means exactly one KeyGen + Sign + positive Verify.
# Unmodeled stages are held constant: results are scoped estimates, not every
# possible improvement from applying the generic helper to all other call sites.
for scheme in ("MAYO-1", "MAYO-5", "MQOM-L1", "MQOM-L5", "SNOVA-V", "UOV-Is"):
    selected = [r for r in rows if r["scheme"] == scheme]
    parts = {}
    for op in ("keygen", "sign", "verify"):
        found = next((r for r in selected if r["operation"] == op), None)
        if found:
            parts[op] = found["old_cycles"]
        elif (scheme, op) in saved:
            parts[op] = saved[scheme, op]
    # No L5 MQOM KeyGen denominator: do not silently manufacture a full request.
    coverage = "whole-run" if len(parts) == 3 else "sign+verify"
    old = sum(parts.values())
    saved_cycles = sum(r["old_cycles"]*r["stage_cycle_reduction_pct"]/100 for r in selected)
    lower = sum(r["old_cycles"]*r["stage_cycle_reduction_low_pct"]/100 for r in selected)
    upper = sum(r["old_cycles"]*r["stage_cycle_reduction_high_pct"]/100 for r in selected)
    drop = saved_cycles/old
    rows.append(dict(scheme=scheme, operation=coverage, old_cycles=old,
                     predicted_new_cycles=round(old-saved_cycles),
                     stage_cycle_reduction_pct=100*drop,
                     stage_cycle_reduction_low_pct=100*lower/old,
                     stage_cycle_reduction_high_pct=100*upper/old,
                     speedup=1/(1-drop), speedup_low=1/(1-lower/old),
                     speedup_high=1/(1-upper/old),
                     denominator_source=("modeled_L1_perf_transfer" if scheme=="MQOM-L5"
                                         else "saved_optimized_E2E"),
                     status="CONDITIONAL_ESTIMATE",
                     assumption="weighted stage sum; unmodeled stages held constant"))
# SNOVA Sign includes untested shape transfer. Also expose the KeyGen-only
# scenario rather than allowing the conditional Sign contribution to look proven.
kg = next(r for r in rows if r["scheme"] == "SNOVA-V" and r["operation"] == "keygen")
old = sum(saved["SNOVA-V", op] for op in ("keygen", "sign", "verify"))
drop = kg["old_cycles"]*kg["stage_cycle_reduction_pct"]/100/old
low = kg["old_cycles"]*kg["stage_cycle_reduction_low_pct"]/100/old
high = kg["old_cycles"]*kg["stage_cycle_reduction_high_pct"]/100/old
rows.append(dict(scheme="SNOVA-V", operation="whole-keygen-only", old_cycles=old,
                 predicted_new_cycles=round(old*(1-drop)),
                 stage_cycle_reduction_pct=100*drop,
                 stage_cycle_reduction_low_pct=100*low,
                 stage_cycle_reduction_high_pct=100*high,
                 speedup=1/(1-drop), speedup_low=1/(1-low),
                 speedup_high=1/(1-high), denominator_source="saved_optimized_E2E",
                 status="CONDITIONAL_ESTIMATE",
                 assumption="KeyGen contribution only; no credit for untested Sign shapes"))
with a.output.open("w") as f:
    w = csv.DictWriter(f, fieldnames=list(rows[0]))
    w.writeheader()
    w.writerows(rows)
for r in rows:
    print(f'{r["scheme"]:9s} {r["operation"]:11s} cycles '
          f'{r["old_cycles"]:,} -> {r["predicted_new_cycles"]:,}; '
          f'-{r["stage_cycle_reduction_pct"]:.2f}% '
          f'[{r["stage_cycle_reduction_low_pct"]:.2f}, '
          f'{r["stage_cycle_reduction_high_pct"]:.2f}]%; '
          f'{r["speedup"]:.4f}x')
