#!/usr/bin/env python3
import csv
import re
import sys
from pathlib import Path

CASES = (
    ("MAYO", "MAYO-5", "mayo5-baseline",
     ("mayo5-xmpk", "mayo5-wide")),
    ("SNOVA", "SNOVA_V_B_AES", "snova5-baseline",
     ("snova5-xmpk", "snova5-qgemm")),
    ("MQOM", "mqom3_cat5_gf16_fast_ct", "mqom5-baseline",
     ("mqom5-xmpk",)),
    ("UOV", "UOV-V", "uov-v-baseline",
     ("uov-v-xmpk",)),
    ("QRUOV", "5q127L3", "qruov-l5-baseline-final",
     ("qruov-l5-xmpk",)),
)
OPERATIONS = ("keygen", "sign", "verify")


def parse_log(path: Path) -> dict:
    text = path.read_text(errors="replace")
    if "E2E_SUCCESS" not in text:
        raise ValueError(f"{path}: E2E_SUCCESS missing")
    begin = re.search(
        r"E2E_BEGIN algorithm=(\S+) parameter=(\S+) implementation=(\S+)",
        text,
    )
    if not begin:
        raise ValueError(f"{path}: E2E_BEGIN missing")
    metrics = {
        operation: (int(cycles, 16), int(instret, 16))
        for operation, cycles, instret in re.findall(
            r"E2E_METRIC (\S+) cycles=0x([0-9a-fA-F]+) "
            r"instret=0x([0-9a-fA-F]+)",
            text,
        )
    }
    if set(metrics) != set(OPERATIONS):
        raise ValueError(f"{path}: incomplete metric set {sorted(metrics)}")
    return {
        "algorithm": begin.group(1),
        "parameter": begin.group(2),
        "implementation": begin.group(3),
        "metrics": metrics,
    }


def validate(result: dict, algorithm: str, parameter: str, tag: str) -> None:
    if result["algorithm"] != algorithm:
        raise ValueError(
            f"{tag}: algorithm {result['algorithm']} != {algorithm}"
        )
    if result["parameter"] != parameter:
        raise ValueError(
            f"{tag}: parameter {result['parameter']} != {parameter}"
        )


def main() -> int:
    root = Path(sys.argv[1] if len(sys.argv) > 1 else ".").resolve()
    variants = []
    comparisons = []
    for algorithm, parameter, baseline_tag, optimized_tags in CASES:
        baseline = parse_log(root / f"{baseline_tag}.log")
        validate(baseline, algorithm, parameter, baseline_tag)
        optimized = []
        for tag in optimized_tags:
            result = parse_log(root / f"{tag}.log")
            validate(result, algorithm, parameter, tag)
            optimized.append((tag, result))

        for tag, result in ((baseline_tag, baseline), *optimized):
            for operation in OPERATIONS:
                cycles, instret = result["metrics"][operation]
                variants.append({
                    "algorithm": algorithm,
                    "parameter": parameter,
                    "variant": tag,
                    "implementation": result["implementation"],
                    "operation": operation,
                    "cycles": cycles,
                    "instret": instret,
                })

        for operation in OPERATIONS:
            base_cycles, base_instret = baseline["metrics"][operation]
            selected_tag, selected = min(
                optimized,
                key=lambda item: item[1]["metrics"][operation][0],
            )
            opt_cycles, opt_instret = selected["metrics"][operation]
            comparisons.append({
                "algorithm": algorithm,
                "parameter": parameter,
                "operation": operation,
                "baseline_cycles": base_cycles,
                "selected_variant": selected_tag,
                "optimized_cycles": opt_cycles,
                "cycle_speedup": f"{base_cycles / opt_cycles:.6f}",
                "baseline_instret": base_instret,
                "optimized_instret": opt_instret,
                "instret_reduction_pct": (
                    f"{100.0 * (base_instret - opt_instret) / base_instret:.3f}"
                ),
                "optimized_implementation": selected["implementation"],
            })

    variants_path = root / "l5_e2e_variants.csv"
    with variants_path.open("w", newline="") as stream:
        writer = csv.DictWriter(stream, fieldnames=variants[0].keys())
        writer.writeheader()
        writer.writerows(variants)
    comparisons_path = root / "l5_e2e_metrics.csv"
    with comparisons_path.open("w", newline="") as stream:
        writer = csv.DictWriter(stream, fieldnames=comparisons[0].keys())
        writer.writeheader()
        writer.writerows(comparisons)
    print(variants_path)
    print(comparisons_path)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
