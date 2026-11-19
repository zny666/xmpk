"""Update both paper tables and kernel claims from final full-CPU measurements."""
from pathlib import Path
import json
import re
import shutil

root = Path(__file__).resolve().parents[1]
paper = root.parent.parent / "xmpk-dac2027-paper"
data = json.loads((root / "results/final-kernels/cycles.json").read_text())
cycles = {row["case"]: row["cycles"] for row in data["rows"]}
assert len(cycles) == 12
replacements = {}
speeds = {}
for n, old_scalar, old_hw, old_speed in (
    (45, "391,961", "11,662", "33.61"),
    (72, "1,535,642", "40,172", "38.23"),
    (96, "3,572,061", "84,953", "42.05"),
):
    scalar = cycles[f"solve-{n}-scalar"]
    hardware = cycles[f"solve-{n}-hardware"]
    speeds[f"solve{n}"] = scalar / hardware
    replacements.update({old_scalar: f"{scalar:,}", old_hw: f"{hardware:,}",
                         old_speed: f"{scalar / hardware:.2f}"})
for degree, old_scalar, old_hw, old_layout, old_core_speed, old_layout_speed in (
    (3, "238,085", "34,669", "50,226", "6.87", "4.74"),
    (10, "1,999,044", "99,482", "226,001", "20.09", "8.85"),
):
    scalar = cycles[f"ext-{degree}-scalar"]
    hardware = cycles[f"ext-{degree}-hardware"]
    layout = cycles[f"ext-{degree}-layout"]
    speeds[f"ext{degree}"] = scalar / hardware
    speeds[f"layout{degree}"] = scalar / layout
    replacements.update({old_scalar: f"{scalar:,}", old_hw: f"{hardware:,}",
                         old_layout: f"{layout:,}", old_core_speed: f"{scalar/hardware:.2f}",
                         old_layout_speed: f"{scalar/layout:.2f}"})
for file in ("main.tex", "main_zh.tex"):
    path = paper / file
    text = path.read_text()
    # Replace numeric tokens simultaneously to avoid cascading replacement.
    pattern = r"(?<![\d.,])(" + "|".join(re.escape(x) for x in
               sorted(replacements, key=len, reverse=True)) + r")(?![\d.,])"
    text = re.sub(pattern, lambda match: replacements[match[0]], text)
    text = text.replace("33.6--42.0", f"{speeds['solve45']:.1f}--{speeds['solve96']:.1f}")
    text = text.replace("33.6\\(", f"{speeds['solve45']:.1f}\\(")
    text = text.replace("42.0\\(", f"{speeds['solve96']:.1f}\\(")
    text = text.replace("4.7--8.8", f"{speeds['layout3']:.1f}--{speeds['layout10']:.1f}")
    path.write_text(text)
shutil.copy2(root / "results/final-kernels/cycles.json",
             paper / "sources/final-kernel-cycles.json")
print("FINAL_KERNEL_TABLE_UPDATED", json.dumps(speeds))
