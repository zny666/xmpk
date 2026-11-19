#!/usr/bin/env python3
import csv
import re
import sys
from pathlib import Path

CASES = (
    ("MAYO", "MAYO-1", "mayo1"),
    ("MQOM", "mqom3_cat1_gf16_fast_ct", "mqom1"),
    ("QRUOV", "1q127L3", "qruov-l3"),
    ("SNOVA", "SNOVA_I_B_AES", "snova1"),
    ("UOV", "UOV-Is", "uov-is"),
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
    metrics = {}
    for operation, cycles, instret in re.findall(
        r"E2E_METRIC (\S+) cycles=0x([0-9a-fA-F]+) "
        r"instret=0x([0-9a-fA-F]+)",
        text,
    ):
        metrics[operation] = (int(cycles, 16), int(instret, 16))
    if set(metrics) != set(OPERATIONS):
        raise ValueError(f"{path}: incomplete metric set {sorted(metrics)}")
    return {
        "algorithm": begin.group(1),
        "parameter": begin.group(2),
        "implementation": begin.group(3),
        "metrics": metrics,
    }


def main() -> int:
    root = Path(sys.argv[1] if len(sys.argv) > 1 else ".").resolve()
    out = root / "results" / "e2e_metrics.csv"
    rows = []
    for expected_algorithm, expected_parameter, tag in CASES:
        baseline = parse_log(
            root / "results" / "baseline" / f"{tag}-baseline.log"
        )
        optimized = parse_log(
            root / "results" / "optimized" / f"{tag}-xmpk.log"
        )
        for result in (baseline, optimized):
            if result["algorithm"] != expected_algorithm:
                raise ValueError(
                    f"{tag}: algorithm {result['algorithm']} != "
                    f"{expected_algorithm}"
                )
            if result["parameter"] != expected_parameter:
                raise ValueError(
                    f"{tag}: parameter {result['parameter']} != "
                    f"{expected_parameter}"
                )
        for operation in OPERATIONS:
            base_cycles, base_instret = baseline["metrics"][operation]
            opt_cycles, opt_instret = optimized["metrics"][operation]
            rows.append(
                {
                    "algorithm": expected_algorithm,
                    "parameter": expected_parameter,
                    "operation": operation,
                    "baseline_cycles": base_cycles,
                    "optimized_cycles": opt_cycles,
                    "cycle_speedup": f"{base_cycles / opt_cycles:.6f}",
                    "baseline_instret": base_instret,
                    "optimized_instret": opt_instret,
                    "instret_reduction_pct": (
                        f"{100.0 * (base_instret - opt_instret) / base_instret:.3f}"
                    ),
                    "optimized_implementation": optimized["implementation"],
                }
            )
    out.parent.mkdir(parents=True, exist_ok=True)
    with out.open("w", newline="") as stream:
        writer = csv.DictWriter(stream, fieldnames=rows[0].keys())
        writer.writeheader()
        writer.writerows(rows)
    print(out)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
