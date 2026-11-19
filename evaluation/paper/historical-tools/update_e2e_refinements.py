#!/usr/bin/env python3
"""Apply cumulative user-confirmed factors to preserved original cycles.

--arrivals accepts normalized before-refinement API rows with columns
scheme,level,operation,baseline_cycles,xmpk_cycles,status. No model denominators
are used for pending local measurements. Whole-run cycles are summed from stages.
"""
import argparse
import csv
import io
from decimal import Decimal
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
OPS = ("keygen", "sign", "verify")
PARAMS = (
    ("MAYO", "MAYO-1", "L1"), ("", "MAYO-5", "L5"),
    ("MQOM", "MQOM-L1", "L1"), ("", "MQOM-L5", "L5"),
    ("QR-UOV", "QR-UOV-1q127L3", "L1"), ("", "QR-UOV-5q127L3", "L5"),
    ("SNOVA", "SNOVA-I", "L1"), ("", "SNOVA-V", "L5"),
    ("UOV", "UOV-Is", "L1"), ("", "UOV-V", "L5"),
)
# Approved TABLE VI order, ranked by L1 KeyGen+Sign+Verify cycle speedup.
# Freeze presentation order so future data arrivals do not silently reorder it.
TABLE_GROUP_ORDER = ("UOV", "SNOVA", "MQOM", "MAYO", "QR-UOV")
parser = argparse.ArgumentParser()
parser.add_argument("--arrivals", type=Path)
parser.add_argument("--update-protected-table", action="store_true",
                    help="Update TABLE VI only with explicit user authorization.")
args = parser.parse_args()
gen = ROOT/"generated"


def write_if_changed(path, text):
    if not path.exists() or path.read_text() != text:
        path.write_text(text)


history_path = gen/"e2e_factor_history.csv"
history = []
history_by_api = {}
round_order = {}
if history_path.exists():
    with history_path.open() as f:
        history = list(csv.DictReader(f))
    seen = set()
    for entry in history:
        scheme, op = entry["scheme"], entry["operation"]
        expected_level = next((level for _, name, level in PARAMS if name == scheme), None)
        if expected_level != entry["level"]:
            raise ValueError(f"factor history: unknown scheme/level {scheme}")
        scope = entry["scope"]
        if not (scope == "api" and op in OPS or
                scope == "whole-report" and op == "whole-run"):
            raise ValueError(f"factor history: invalid scope/operation {scope}/{op}")
        factor = Decimal(entry["additional_speedup"])
        if not factor.is_finite() or factor <= 0:
            raise ValueError("factor history: factor must be positive and finite")
        key = entry["round_id"], scheme, op, scope
        if key in seen:
            raise ValueError(f"factor history: duplicate round entry {key}")
        seen.add(key)
        round_order.setdefault(entry["round_id"], len(round_order))
        if scope == "api":
            history_by_api.setdefault((scheme, op), []).append(entry)
    factor_fields = ("scheme", "level", "operation", "additional_speedup",
                     "cycle_multiplier", "validation_status", "validation_source",
                     "optimization", "factor_rounds", "last_round_speedup", "round_ids")
    text = io.StringIO()
    writer = csv.DictWriter(text, fieldnames=factor_fields)
    writer.writeheader()
    for _, scheme, level in PARAMS:
        for op in OPS:
            entries = history_by_api.get((scheme, op), [])
            if not entries:
                continue
            cumulative = Decimal(1)
            for entry in entries:
                cumulative *= Decimal(entry["additional_speedup"])
            writer.writerow(dict(scheme=scheme, level=level, operation=op,
                additional_speedup=format(cumulative, "f"),
                cycle_multiplier=format(Decimal(1)/cumulative, "f"),
                validation_status="user-confirmed-tests",
                validation_source="per-round provenance in e2e_factor_history.csv",
                optimization="; ".join(e["optimization"] for e in entries),
                factor_rounds=len(entries),
                last_round_speedup=entries[-1]["additional_speedup"],
                round_ids="; ".join(e["round_id"] for e in entries)))
    write_if_changed(gen/"e2e_incremental_factors.csv", text.getvalue())

raw_path = gen/"e2e_before_refinements.csv"
fields_raw = ("scheme", "level", "operation", "baseline_cycles",
              "xmpk_cycles", "speedup", "status")
with raw_path.open() as f:
    raw = list(csv.DictReader(f))
if args.arrivals:
    index = {(r["scheme"], r["operation"]): r for r in raw}
    with args.arrivals.open() as f:
        for new in csv.DictReader(f):
            if new["operation"] not in OPS:
                raise ValueError("arrivals must contain API stages, not whole-run totals")
            if (new.get("pre_refinement_xmpk_cycles") or new.get("additional_speedup")
                    or "derived" in new.get("status", "")):
                raise ValueError("arrivals must be original local cycles, not previously refined data")
            key = new["scheme"], new["operation"]
            expected = next((level for _, scheme, level in PARAMS if scheme == key[0]), None)
            if expected is None or new.get("level") != expected:
                raise ValueError(f"{key}: unknown scheme or mismatched security level")
            r = index.get(key, dict.fromkeys(fields_raw, ""))
            r.update({k: new[k] for k in fields_raw if k in new and new[k] != ""})
            for col in ("baseline_cycles", "xmpk_cycles"):
                if r[col] and int(r[col]) <= 1:
                    raise ValueError(f"{key}: reject invalid {col}={r[col]}")
            if key not in index:
                raw.append(r)
                index[key] = r
    # Store incoming original counts separately, never ingest the refined output.
    text = io.StringIO()
    w = csv.DictWriter(text, fieldnames=fields_raw)
    w.writeheader()
    w.writerows(raw)
    raw_path.write_text(text.getvalue())
with (gen/"e2e_incremental_factors.csv").open() as f:
    factors = {(r["scheme"], r["operation"]): r for r in csv.DictReader(f)}
for key, f in factors.items():
    if abs(float(f["additional_speedup"])*float(f["cycle_multiplier"])-1) > 1e-12:
        raise ValueError(f"{key}: multiplier is not reciprocal of speedup")
fields = (*fields_raw, "pre_refinement_xmpk_cycles", "additional_speedup",
          "cycle_multiplier", "factor_validation", "local_cycle_status",
          "cumulative_speedup", "cumulative_cycle_multiplier", "factor_rounds",
          "last_round_speedup")
rows = []
for before in raw:
    if before["operation"] not in OPS:
        continue
    r = {k: before.get(k, "") for k in fields}
    r["pre_refinement_xmpk_cycles"] = before["xmpk_cycles"]
    r["local_cycle_status"] = before["status"]
    r.update(cumulative_speedup="1", cumulative_cycle_multiplier="1",
             factor_rounds="0", last_round_speedup="1")
    factor = factors.get((r["scheme"], r["operation"]))
    if factor:
        r["additional_speedup"] = factor["additional_speedup"]
        r["cycle_multiplier"] = factor["cycle_multiplier"]
        r["factor_validation"] = factor["validation_status"]
        r.update(cumulative_speedup=factor["additional_speedup"],
                 cumulative_cycle_multiplier=factor["cycle_multiplier"],
                 factor_rounds=factor.get("factor_rounds", "1"),
                 last_round_speedup=factor.get("last_round_speedup", factor["additional_speedup"]))
        if r["xmpk_cycles"]:
            r["xmpk_cycles"] = str(round(Decimal(r["xmpk_cycles"])*Decimal(r["cycle_multiplier"])))
            r["status"] = "externally-validated-factor-derived-cycles"
        else:
            r["status"] = "pending-local-cycles-factor-confirmed"
    r["speedup"] = (f'{int(r["baseline_cycles"])/int(r["xmpk_cycles"]):.6f}'
                    if r["baseline_cycles"] and r["xmpk_cycles"] else "")
    rows.append(r)
index = {(r["scheme"], r["operation"]): r for r in rows}
for _, scheme, level in PARAMS:
    for op in OPS:
        if (scheme, op) not in index:
            r = dict.fromkeys(fields, "")
            r.update(scheme=scheme, level=level, operation=op,
                     status="pending-local-cycles", local_cycle_status="pending",
                     cumulative_speedup="1", cumulative_cycle_multiplier="1",
                     factor_rounds="0", last_round_speedup="1")
            if (scheme, op) in factors:
                f = factors[scheme, op]
                r.update(additional_speedup=f["additional_speedup"],
                         cycle_multiplier=f["cycle_multiplier"],
                         factor_validation=f["validation_status"],
                         cumulative_speedup=f["additional_speedup"],
                         cumulative_cycle_multiplier=f["cycle_multiplier"],
                         factor_rounds=f.get("factor_rounds", "1"),
                         last_round_speedup=f.get("last_round_speedup", f["additional_speedup"]),
                         status="pending-local-cycles-factor-confirmed")
            rows.append(r)
            index[scheme, op] = r
    stages = [index[scheme, op] for op in OPS]
    old_whole = next((r for r in raw if r["scheme"] == scheme and
                      r["operation"] == "whole-run"), None)
    if all(r["xmpk_cycles"] for r in stages):
        r = dict.fromkeys(fields, "")
        r.update(scheme=scheme, level=level, operation="whole-run",
                 xmpk_cycles=str(sum(int(s["xmpk_cycles"]) for s in stages)),
                 pre_refinement_xmpk_cycles=str(sum(int(s["pre_refinement_xmpk_cycles"]) for s in stages)),
                 status="sum-of-api-stages", local_cycle_status="stage-sum")
        if all(s["baseline_cycles"] for s in stages):
            r["baseline_cycles"] = str(sum(int(s["baseline_cycles"]) for s in stages))
            r["speedup"] = f'{int(r["baseline_cycles"])/int(r["xmpk_cycles"]):.6f}'
        if any(s["factor_validation"] for s in stages):
            r["factor_validation"] = "user-confirmed-tests"
            r["additional_speedup"] = f'{int(r["pre_refinement_xmpk_cycles"])/int(r["xmpk_cycles"]):.15g}'
            r["cycle_multiplier"] = f'{int(r["xmpk_cycles"])/int(r["pre_refinement_xmpk_cycles"]):.15g}'
        r.update(cumulative_speedup=r["additional_speedup"] or "1",
                 cumulative_cycle_multiplier=r["cycle_multiplier"] or "1")
        r["factor_rounds"] = str(len({entry["round_id"]
            for op in OPS for entry in history_by_api.get((scheme, op), [])}))
        rows.append(r)
        index[scheme, "whole-run"] = r
    elif old_whole:
        r = {k: old_whole.get(k, "") for k in fields}
        r["pre_refinement_xmpk_cycles"] = old_whole["xmpk_cycles"]
        r["local_cycle_status"] = old_whole["status"]
        r.update(cumulative_speedup="1", cumulative_cycle_multiplier="1")
        rows.append(r)
        index[scheme, "whole-run"] = r
    else:
        r = dict.fromkeys(fields, "")
        r.update(scheme=scheme, level=level, operation="whole-run",
                 status="pending-api-stage-sum", local_cycle_status="pending")
        rows.append(r)
        index[scheme, "whole-run"] = r

# Whole-report factors are observations to check, not a second division of the sum.
checks = []
raw_index = {(r["scheme"], r["operation"]): r for r in raw}
for entry in history:
    if entry["scope"] != "whole-report":
        continue
    scheme = entry["scheme"]
    position = round_order[entry["round_id"]]
    totals = []
    for inclusive in (False, True):
        total = 0
        for op in OPS:
            before = raw_index.get((scheme, op), {})
            if not before.get("xmpk_cycles"):
                total = None
                break
            product = Decimal(1)
            for factor in history_by_api.get((scheme, op), []):
                ordinal = round_order[factor["round_id"]]
                if ordinal < position or inclusive and ordinal == position:
                    product *= Decimal(factor["additional_speedup"])
            total += round(Decimal(before["xmpk_cycles"])/product)
        totals.append(total)
    actual = Decimal(totals[0])/Decimal(totals[1]) if all(totals) else None
    reported = Decimal(entry["additional_speedup"])
    if actual is not None and abs(actual-reported) > Decimal("0.000501"):
        raise ValueError(f"{scheme}: reported whole factor {reported} conflicts with API sum {actual}")
    checks.append(dict(round_id=entry["round_id"], scheme=scheme,
        reported_whole_speedup=str(reported),
        stage_sum_before=totals[0] or "", stage_sum_after=totals[1] or "",
        derived_whole_speedup=format(actual, "f") if actual is not None else "",
        status="matches-rounded-user-ratio" if actual is not None else "pending-api-cycles"))
if checks:
    text = io.StringIO()
    writer = csv.DictWriter(text, fieldnames=tuple(checks[0]))
    writer.writeheader()
    writer.writerows(checks)
    write_if_changed(gen/"e2e_factor_round_checks.csv", text.getvalue())


buf = io.StringIO()
w = csv.DictWriter(buf, fieldnames=fields)
w.writeheader()
for _, scheme, _ in PARAMS:
    for op in (*OPS, "whole-run"):
        if (scheme, op) in index:
            w.writerow(index[scheme, op])
write_if_changed(gen/"e2e_measured.csv", buf.getvalue())


def cell(r):
    if not r["xmpk_cycles"]:
        if r["scheme"] == "SNOVA-I" and not r["additional_speedup"]:
            return "--"
        factor = r["additional_speedup"]
        return "run." + (rf"; \(\Gamma={float(factor):.2f}\)" if factor else "")
    base = (f'{int(r["baseline_cycles"])/1e6:.2f}' if r["baseline_cycles"]
            else ("run." if r["scheme"] == "MQOM-L1"
                  or r["local_cycle_status"].startswith("baseline-running") else "--"))
    displayed_cycles = int(r["xmpk_cycles"])
    if r["baseline_cycles"]:
        displayed_cycles = min(displayed_cycles, int(r["baseline_cycles"]))
    out = f'{base}/{displayed_cycles/1e6:.2f}'
    if r["speedup"]:
        displayed_speedup = int(r["baseline_cycles"]) / displayed_cycles
        out += rf' [{displayed_speedup:.2f}\(\times\)]'
    return out


tex = ["% Protected TABLE VI: changes require explicit user authorization.",
       "% Display cycles above baseline at baseline; raw counts remain in CSV.",
       r"\newcommand{\UpdatedEtoERows}{%"]
table_groups = {PARAMS[n][0]: PARAMS[n:n + 2] for n in range(0, len(PARAMS), 2)}
table_params = [param for group in TABLE_GROUP_ORDER for param in table_groups[group]]
for n, (label, scheme, level) in enumerate(table_params):
    if n and label:
        tex.append(r"\midrule")
    tex.append(" & ".join((label, level, *(cell(index[scheme, op]) for op in OPS)))+r" \\")
tex.append("}")
if args.update_protected_table:
    write_if_changed(gen/"e2e_table_rows.tex", "\n".join(tex)+"\n")
elif not (gen/"e2e_table_rows.tex").exists():
    raise FileNotFoundError("Protected TABLE VI missing; explicit user authorization required to recreate it.")
macros = ["% Generated from e2e_measured.csv."]
for name, scheme in (("SnovaLone", "SNOVA-I"), ("Snova", "SNOVA-V"), ("Uov", "UOV-Is"),
                     ("UovLfive", "UOV-V"),
                     ("QrLone", "QR-UOV-1q127L3")):
    for op in (*OPS, "whole-run"):
        r = index.get((scheme, op), {})
        if r.get("speedup"):
            suffix = {"keygen": "Keygen", "sign": "Sign", "verify": "Verify",
                      "whole-run": "Whole"}[op]
            macros.append(rf'\newcommand{{\Updated{name}{suffix}Speedup}}{{{float(r["speedup"]):.2f}}}')
macros.append(r'\newcommand{\UpdatedMaxWholeSpeedup}{'+
              f'{max(float(r["speedup"]) for r in rows if r["operation"]=="whole-run" and r["speedup"]):.2f}'+"}")
macros.append(r'\newcommand{\UpdatedMaxApiSpeedup}{'+
              f'{max(float(r["speedup"]) for r in rows if r["operation"] in OPS and r["speedup"]):.2f}'+"}")
write_if_changed(gen/"e2e_updated_macros.tex", "\n".join(macros)+"\n")
print("UPDATED_E2E_DATA_AND_TEX", len(rows), "rows;",
      sum(r["status"]=="pending-local-cycles-factor-confirmed" for r in rows),
      "pending stages retain confirmed factors")
