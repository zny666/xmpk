"""Publish final routed resources and power with report and source provenance."""
from pathlib import Path
import hashlib
import json
import re
import shutil

root = Path(__file__).resolve().parents[1]
paper = root.parent.parent / "xmpk-dac2027-paper"

def cell(text, name):
    match = re.search(r"^\|\s*" + re.escape(name) + r"\s*\|\s*([0-9.+-]+)", text, re.M)
    if not match:
        raise ValueError(f"Missing report cell {name}")
    return float(match[1])

manifest = json.loads((root / "source-hashes.json").read_text())["final"]
for name, digest in manifest.items():
    assert hashlib.sha256((root / "final" / name).read_bytes()).hexdigest() == digest, name
for name in ("xmpk_extgemm.sv", "xmpk_solveq.sv", "xmpk_qgemm.sv"):
    relative = Path("cva6/core/cvxif_example") / name
    assert (root / "final" / relative).read_bytes() == (root.parent / relative).read_bytes(), name
q = Path("cva6/core/cvxif_example/xmpk_qgemm.sv")
assert (root / "final" / q).read_bytes() == (root.parent / "hardware-adopted-20261113/baseline" / q).read_bytes()
macros = {}
rows = []
for kind, prefix, target, bram, dsp in (
    ("copro", "Xmpk", 80, 4, 8), ("system", "System", 40, 40, 35)
):
    folder = root / "results" / f"final-{kind}"
    assert (folder / "PASS").exists() and not (folder / "ERROR").exists()
    util = (folder / "route-util.rpt").read_text()
    timing = json.loads((folder / "frequency.json").read_text())
    power = (folder / "power-target.rpt").read_text()
    assert all(timing[k] >= 0 for k in ("target_WNS_ns", "target_WHS_ns", "target_WPWS_ns"))
    assert cell(util, "Block RAM Tile") == bram
    assert cell(util, "DSPs") == dsp
    period = 1000 / target
    path = period - timing["target_WNS_ns"]
    lut_name = "Slice LUTs*" if re.search(r"\|\s*Slice LUTs\*\s*\|", util) else "Slice LUTs"
    row = {
        "kind": kind, "LUT": int(cell(util, lut_name)),
        "FF": int(cell(util, "Slice Registers")), "BRAM36": bram, "DSP": dsp,
        "target_MHz": target, "critical_path_ns": path, "effective_Fmax_MHz": 1000 / path,
        "timing": timing,
        "power_total_W": cell(power, "Total On-Chip Power (W)"),
        "power_dynamic_W": cell(power, "Dynamic (W)"),
        "power_static_W": cell(power, "Device Static (W)"),
        "report_sha256": {p.name: hashlib.sha256(p.read_bytes()).hexdigest()
                          for p in folder.glob("*.rpt")},
    }
    assert re.search(r"\|\s*core_clk\s*\|", power), "Check power clock coverage"
    rows.append(row)
    for key in ("LUT", "FF"):
        macros[prefix + key] = f"{row[key]:,}"
    macros.update({
        prefix + "BRAM": str(bram), prefix + "DSP": str(dsp),
        prefix + "Fmax": f"{1000 / path:.2f}",
        prefix + "CriticalPath": f"{path:.3f}",
        prefix + ("OOCWNS" if kind == "copro" else "WNS"): f"{timing['target_WNS_ns']:+.3f}",
        prefix + "PowerTotal": f"{row['power_total_W']:.3f}",
        prefix + "PowerDynamic": f"{row['power_dynamic_W']:.3f}",
        prefix + "PowerStatic": f"{row['power_static_W']:.3f}",
    })
    if kind == "copro":
        macros["XmpkLUTRAM"] = str(int(cell(util, "LUT as Memory")))
        macros["XmpkDynamicEnergyPerCycle"] = f"{row['power_dynamic_W'] * 1000 / target:.2f}"
    else:
        macros["SystemEnergyPerCycle"] = f"{row['power_total_W'] * 1000 / target:.2f}"
    for report, destination in (
        ("power-target.rpt", "xmpk-ooc-80mhz-power.rpt" if kind == "copro" else "cva6-xmpk-40mhz-power.rpt"),
        ("route-util.rpt", f"final-{kind}-route-util.rpt"),
        ("timing-target.rpt", f"final-{kind}-timing-target.rpt"),
        ("clocks-target.rpt", f"final-{kind}-clocks-target.rpt"),
    ):
        shutil.copy2(folder / report, paper / "sources" / destination)
base = cell((paper / "sources/cva6-vanilla-40mhz-power.rpt").read_text(), "Total On-Chip Power (W)")
dynamic_base = cell((paper / "sources/cva6-vanilla-40mhz-power.rpt").read_text(), "Dynamic (W)")
system = rows[1]
macros["SystemPowerIncrease"] = f"{100 * (system['power_total_W'] / base - 1):.1f}"
macros["SystemDynamicPowerIncrease"] = f"{100 * (system['power_dynamic_W'] / dynamic_base - 1):.1f}"
q_results = json.loads((root / "results/final-qgemm/cycles.json").read_text())
q_index = {(r["m"], r["k"], r["n"], r["implementation"]): r["cycles"]
           for r in q_results["rows"]}
for name, shape in (
    ("Small", (3, 5, 16)), ("Medium", (7, 19, 32)), ("Large", (16, 64, 32))
):
    baseline = q_index[(*shape, "scalar")]
    accelerated = q_index[(*shape, "hardware")]
    macros["QgemmScalar" + name] = f"{baseline:,}"
    macros["QgemmCycles" + name] = f"{accelerated:,}"
    macros["QgemmSpeedup" + name] = f"{baseline / accelerated:.2f}"
    macros["QgemmEnergy" + name] = f"{baseline / accelerated * base / system['power_total_W']:.1f}"
macros["QgemmMaxSpeedup"] = f"{q_index[16, 64, 32, 'scalar'] / q_index[16, 64, 32, 'hardware']:.1f}"
path = paper / "generated/results.tex"
text = path.read_text()
for name, value in macros.items():
    pattern = r"\\newcommand\{\\" + re.escape(name) + r"\}\{[^}]*\}"
    replacement = "\\newcommand{\\" + name + "}{" + value + "}"
    if re.search(pattern, text):
        text = re.sub(pattern, lambda _: replacement, text)
    else:
        text += replacement + "\n"
path.write_text(text)
shutil.copy2(root / "source-hashes.json", paper / "sources/final-hardware-source-hashes.json")
(root / "results/final-paper-resources.json").write_text(json.dumps(rows, indent=2) + "\n")
(paper / "sources/final-hardware-provenance.json").write_text(json.dumps({
    "source_manifest": "final-hardware-source-hashes.json",
    "selection": json.loads((root / "selection.json").read_text()),
    "resources": rows,
}, indent=2) + "\n")
print(json.dumps(rows, indent=2))
