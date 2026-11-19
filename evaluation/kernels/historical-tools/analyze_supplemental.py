#!/usr/bin/env python3
"""Validate sweep counts and summarize independently checked hardware observations."""
import csv
import json
import pathlib
import collections
import hashlib
import subprocess

ROOT = pathlib.Path(__file__).resolve().parents[1]
OUT = ROOT / "results/supplemental-20261114"
manifest = json.loads((OUT / "manifest.json").read_text())
assert "finished" in manifest, "Sweep has not finished"
for source, expected_hash in manifest["sources"].items():
    assert hashlib.sha256(pathlib.Path(source).read_bytes()).hexdigest() == expected_hash
assert hashlib.sha256((ROOT / "tests/harness.cpp").read_bytes()).hexdigest() == manifest["original_harness_sha256"]
manifest["compiler"] = subprocess.check_output(["g++", "--version"], text=True).splitlines()[0]
manifest["scripts_sha256"] = {
    str(p): hashlib.sha256(p.read_bytes()).hexdigest()
    for p in (ROOT / "scripts/supplemental.py", pathlib.Path(__file__).resolve())
}
(OUT / "manifest.json").write_text(json.dumps(manifest, indent=2))
rows = []
for engine, expected in (("EXT", 240), ("Q", 1050), ("SOLVE", 600)):
    lines = (OUT / engine / "test.log").read_text().splitlines()
    assert lines[-1] == "ALL_PASS", engine
    parsed = []
    for line in lines[:-1]:
        words = line.split()
        assert words[-1] == "PASS", line
        item = {"engine": engine, "kind": words[0]}
        for pair in words[1:-1]:
            key, value = pair.split("=")
            item[key] = value if key == "output_hash" else int(value)
        item["transactions"] = item["reads"] + item["writes"]
        m, k, n, mode = (item[x] for x in ("m", "k", "n", "mode"))
        if engine == "Q":
            lanes = 16 if mode == 0 else 8
            assert item["reads"] == m * ((k + lanes - 1) // lanes) + m*k*n
            assert item["b_reads"] == m*k*n
            assert item["writes"] == m*n
        elif engine == "EXT":
            words_per_element = 2 if mode == 10 else 1
            assert item["reads"] == 2*m*k*n*words_per_element
            assert item["b_reads"] == m*k*n*words_per_element
            assert item["writes"] == m*n*words_per_element
        else:
            lanes = 64 // mode
            assert item["reads"] == m*k
            assert item["writes"] == (m + lanes - 1) // lanes
        parsed.append(item)
    assert len(parsed) == expected, (engine, len(parsed))
    rows += parsed

keys = ("engine", "kind", "m", "k", "n", "mode", "seed")
groups = collections.defaultdict(list)
for r in rows:
    groups[tuple(r[k] for k in keys)].append(r)
effects = []
for key, group in groups.items():
    assert len(group) == 15
    assert len({r["output_hash"] for r in group}) == 1, key
    assert len({(r["reads"], r["writes"]) for r in group}) == 1, key
    base = next(r for r in group if r["latency"] == 1 and r["period"] == 0)
    for r in group:
        if r["period"] == 0:
            assert r["cycles"] - base["cycles"] == (
                r["latency"] - 1) * r["transactions"], (key, r)
    slow = next(r for r in group if r["latency"] == 16 and r["period"] == 0)
    pressure = next(r for r in group if r["latency"] == 1 and r["period"] == 2)
    effect = dict(zip(keys, key))
    effect.update(base_cycles=base["cycles"], latency16_cycles=slow["cycles"],
                  latency16_ratio=slow["cycles"]/base["cycles"],
                  half_ready_cycles=pressure["cycles"],
                  half_ready_ratio=pressure["cycles"]/base["cycles"],
                  transactions=base["transactions"],
                  base_reads=base["reads"], base_writes=base["writes"],
                  base_b_reads=base["b_reads"])
    effects.append(effect)
def save(name, data):
    with (OUT / name).open("w", newline="") as f:
        writer = csv.DictWriter(f, fieldnames=list(data[0]))
        writer.writeheader()
        writer.writerows(data)
save("measurements.csv", rows)
save("effects.csv", effects)
summary = {
    "cases": len(rows), "configurations": len(groups), "all_oracles_pass": True,
    "profile_output_hashes_equal": True, "transaction_formulas_verified": True,
    "latency_cycle_formula_verified": "C(L)=C(1)+(L-1)*(reads+writes), period=0",
    "engines": {},
}
for engine in ("EXT", "Q", "SOLVE"):
    e = [r for r in effects if r["engine"] == engine]
    summary["engines"][engine] = {
        "latency16_ratio_range": [min(r["latency16_ratio"] for r in e),
                                  max(r["latency16_ratio"] for r in e)],
        "half_ready_ratio_range": [min(r["half_ready_ratio"] for r in e),
                                   max(r["half_ready_ratio"] for r in e)],
    }
solve_deltas = []
lookup = {tuple(r[k] for k in keys) + (r["latency"], r["period"]): r for r in rows}
for r in rows:
    if r["kind"] == "SOLVE_DENSE":
        key = list(tuple(r[k] for k in keys))
        key[1] = "SOLVE_ID"
        other = lookup[tuple(key) + (r["latency"], r["period"])]
        solve_deltas.append(r["cycles"] - other["cycles"])
summary["solve_dense_minus_identity_cycles"] = [min(solve_deltas), max(solve_deltas)]
(OUT / "analysis.json").write_text(json.dumps(summary, indent=2))
report = [
    "# Xmpk supplemental hardware experiments — 2026-11-14",
    "",
    f"{len(rows)} oracle-checked runs across {len(groups)} operand/configuration groups passed.",
    "Every group used identical inputs across 15 memory profiles; complete output-region hashes match.",
    "",
    "## Method",
    "",
    "Frozen decoder + copro_alu + engines, Verilator 5.008, standalone instruction harness.",
    "Cycles count RUN issue through completion consumption; SETUP is excluded. These are not full-CVA6 mcycle or end-to-end API measurements.",
    "One outstanding 8-byte request. Read and write acknowledgements arrive 1/2/4/8/16 cycles after acceptance.",
    "Request-ready patterns: always ready; reject every fifth tick; reject every second tick. Two reproducible seeds.",
    "Request phase uses global simulation ticks, so periodic backpressure includes phase effects.",
    "",
    "## Verified findings",
    "",
    "For every tested shape without backpressure, `C(L) = C(1) + (L-1) × (reads+writes)` exactly.",
    "This interface serializes acknowledgements; a one-cycle response increase costs one cycle per transaction.",
    "",
    "| Engine | Response 16 vs 1 cycle, ratio range | 50% ready vs always ready, ratio range |",
    "|---|---:|---:|",
]
for engine, s in summary["engines"].items():
    a, b = s["latency16_ratio_range"]
    c, d = s["half_ready_ratio_range"]
    report.append(f"| {engine} | {a:.3f}–{b:.3f}× | {c:.3f}–{d:.3f}× |")
report += [
    "",
    "### Cache and word boundaries",
    "",
    "Frozen EXTGEMM has no B cache: reads = 2mnk for L3 or 4mnk for L10; B reads scale directly with m.",
    "The 256/272-element B shapes therefore probe size scaling rather than a cache hit/miss transition.",
    "QGEMM caches one A row in 32 words; k is limited to 255, so GF256/Fp k=249 reaches the 32-word limit but cannot exceed it.",
    "Q reads = m ceil(k/lanes) + mnk. All five field modes verified this formula at k=248/249/255 and nwords=256/257.",
    "",
    "### Selected observations (seed 0, always ready)",
    "",
    "| Engine / kind | m,k,n / mode | C(1) | C(16) | Reads | Writes |",
    "|---|---|---:|---:|---:|---:|",
]
for r in effects:
    chosen = (
        r["engine"] == "EXT" and r["m"] == 8 and r["n"] == 16
        or r["engine"] == "Q" and r["mode"] in (0, 1, 4) and r["k"] in (248, 249)
        or r["kind"] == "SOLVE_DENSE" and r["m"] in (64, 96))
    if chosen and r["seed"] == 0:
        report.append(
            f'| {r["kind"]} | {r["m"]},{r["k"]},{r["n"]} / {r["mode"]} '
            f'| {r["base_cycles"]} | {r["latency16_cycles"]} | {r["base_reads"]} | {r["base_writes"]} |')
report += [
    "",
    f'Dense minus identity SOLVE cycles range: {summary["solve_dense_minus_identity_cycles"]}.',
    "This observation covers two sampled matrices per size; it does not establish constant-time behavior for all operands.",
    "",
    "## Artifacts",
    "",
    "`manifest.json`: tools, machine, exact build commands and source/executable SHA-256.",
    "`harness.cpp`: independent sweep harness; `EXT/`, `Q/`, `SOLVE/`: raw build and test logs.",
    "`measurements.csv`: all raw measurements; `effects.csv`: matched profile comparisons; `analysis.json`: validation summary.",
    "Reproduce inside xmpk-arm64-build: run scripts/supplemental.py, then scripts/analyze_supplemental.py.",
]
(OUT / "report.md").write_text("\n".join(report) + "\n")
print(json.dumps(summary, indent=2))
