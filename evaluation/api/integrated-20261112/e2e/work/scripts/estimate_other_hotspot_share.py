#!/usr/bin/env python3
"""Engineering estimates, NOT measured profiles or statistical intervals.

Use observed current-helper short-test costs, static API invocation counts,
and previously saved optimized E2E denominators. Exclude short-test-only erase.
SNOVA batch1/cols6 and UOV varying inner dimensions require explicit models.
"""
import argparse
import csv
from pathlib import Path

p = argparse.ArgumentParser()
p.add_argument("--short-root", type=Path,
               default=Path("/artifact/xmpk-arm64-native/other-short"))
p.add_argument("--e2e", type=Path,
               default=Path("/artifact/xmpk-dac2027-paper/generated/e2e_before_refinements.csv"))
p.add_argument("--output", type=Path, required=True)
p.add_argument("--perf-root", type=Path, default=Path(
    "/artifact/multiprime/reruns/r20-reference-profile-212/reports"))
a = p.parse_args()

with a.e2e.open() as f:
    denominators = {(r["scheme"], r["operation"]): int(r["xmpk_cycles"])
                    for r in csv.DictReader(f) if r["xmpk_cycles"]}
short = {}
for algo in (1, 2, 3, 4):
    with (a.short_root / f"a{algo}/results/metrics.csv").open() as f:
        short[algo] = next(r for r in csv.DictReader(f) if r["flags"] == "0")


def body(algo, level):
    r = short[algo]
    return int(r[f"total_{level}"]) - int(r[f"cleanup_{level}"])


rows = []


def record(scheme, operation, scope, cost, low, high, basis, modeled_den=None):
    den = denominators[scheme, operation] if modeled_den is None else modeled_den
    rows.append(dict(scheme=scheme, operation=operation, scope=scope,
                     estimated_hotspot_cycles=round(cost),
                     denominator_cycles=round(den),
                     denominator_source=("saved_optimized_E2E" if modeled_den is None
                                         else "modeled_L1_perf_transfer"),
                     estimated_share_pct=100*cost/den,
                     engineering_low_pct=100*low/den,
                     engineering_high_pct=100*high/den,
                     status="ESTIMATE_NOT_PROFILE", basis=basis))


# Both V*L/P2 and V*Pv use the same row-major packed helper.
# Wider V*Pv is scaled by k/o. Retry count is a heuristic, not an observed count.
for level, scheme, k, oil, retries in (
        ("l1", "MAYO-1", 10, 8, 1.07),
        ("l5", "MAYO-5", 12, 12, 1.001)):
    one = body(1, level)
    total = one*(1+k/oil)*retries
    record(scheme, "sign", "two row-major packed GEMMs per attempt",
           total, total*.7, total*1.5,
           f"body={one}; widths factor={1+k/oil}; assumed retries={retries}; "
           "word-width scaling and version uncertainty")
    record(scheme, "sign", "V*L/P2 only", one*retries,
           one*.8, one*retries*1.3, "one tested-width GEMM per attempt")

# L1: mu=2, M=64 -> 32 matrices. tau=17.
# t0 is recomputed for each repetition; witness t1 is cached after first use.
per_matrix = body(2, "l1")/3
for op, count in (("sign", (17+1)*32), ("verify", 17*32)):
    total = count*per_matrix
    record("MQOM-L1", op, "PIOP lower-triangular GF256 matvecs",
           total, total*.75, total*1.25,
           f"{count} matrix-vector products; batch3 body/3={per_matrix}; "
           "partial batch and saved-build uncertainty")

# L5 has no complete saved optimized E2E denominator. Transfer only the L1
# non-target residual using the R20 non-matmul time ratio, then add measured-cost
# L5 target work. This is a separate modeled denominator, never marked measured.
with (a.perf_root/"operation-summary.csv").open() as f:
    timings = {r["parameter"]: r for r in csv.DictReader(f)}
fractions = {}
with (a.perf_root/"all-self-hotspots.csv").open() as f:
    for r in csv.DictReader(f):
        if r["symbol"] == "gf256_mat_mult_ref.constprop.0":
            fractions[r["parameter"], r["operation"]] = float(r["self_percent"])/100
for op, count1, count5 in (("sign", 576, 2304), ("verify", 544, 2240)):
    param1, param5 = "mqom3_cat1_gf16_fast_ct", "mqom3_cat5_gf16_fast_ct"
    non_target1 = denominators["MQOM-L1", op] - count1*per_matrix
    ratio = (float(timings[param5][f"{op}_median_ms"])*(1-fractions[param5, op]) /
             (float(timings[param1][f"{op}_median_ms"])*(1-fractions[param1, op])))
    non_target5 = non_target1*ratio
    hot5 = count5*body(2, "l5")/3
    total5 = hot5+non_target5
    lower = .75*hot5/(.75*hot5+1.25*non_target5)
    upper = 1.25*hot5/(1.25*hot5+.75*non_target5)
    record("MQOM-L5", op, "PIOP lower-triangular GF256 matvecs",
           hot5, lower*total5, upper*total5,
           f"{count5} matvecs; non-target L1 residual times R20 non-matmul "
           f"time ratio={ratio}; separate +/-25% hotspot/residual sensitivity",
           modeled_den=total5)

# SNOVA-V: v=40, o=5, m1=6, l=5; r=6.
# KeyGen three loops: m1*v^2 + 2*m1*v*o apply calls, plus right preparations.
prep = int(short[3]["prepare_l5"])
apply25 = (body(3, "l5")-prep)/24
calls = 6*40*(40+2*5)
prep_calls = (2+6)*40
keygen = calls*apply25 + prep_calls*prep
record("SNOVA-V", "keygen", "prepared LINXFM KeyGen loops",
       keygen, keygen*.8, keygen*1.15,
       f"{calls} apply25 at {apply25} cycles; {prep_calls} right preparations "
       f"at {prep} cycles; all l=cols=batch=5")


def snova_sign(shared, retries):
    # Affine model: body = shared transform/dispatch cost + per-vector cost.
    # shared is an assumed 500--1500 cycles, NOT measured by a batch1 trial.
    slope = (apply25-shared)/25
    apply5 = shared+5*slope
    apply30 = shared+30*slope
    f21 = 6*40*40*5*apply5 + 6*40*40*(prep/5)
    f12 = 6*40*40*apply25 + 40*prep
    repeated = (6*40*40 + 6*5*40)*apply30 + 2*40*prep*(6/5)
    return f21+f12+retries*repeated


sign = snova_sign(1000, 1.07)
record("SNOVA-V", "sign", "prepared LINXFM F21/F12 and right-map products",
       sign, snova_sign(500, 1)*.9, snova_sign(1500, 1.1)*1.05,
       "affine batch/column model shared=1000 (range 500--1500); "
       "cols6 and batch1 unmeasured; retries=1.07 (range 1--1.1)")

# UOV classic KeyGen: one upper and one transposed upper multiply.
# Two passes contain inner=1..96, 192 helper calls, sum(inner)=9312.
c96 = body(4, "l1")


def uov_keygen(fixed):
    slope = (c96-fixed)/96
    return 9312*slope + 192*fixed


uov = uov_keygen(10000)
record("UOV-Is", "keygen", "two batch64 triangular passes",
       uov, uov_keygen(5000)*.85, uov_keygen(15000)*1.15,
       f"c96={c96}; sum(inner)=9312; 192 calls; assumed fixed=10000 "
       "(range 5000--15000); extra +/-15% varying-shape uncertainty")

with a.output.open("w") as f:
    w = csv.DictWriter(f, fieldnames=list(rows[0]))
    w.writeheader()
    w.writerows(rows)
for r in rows:
    print(f'{r["scheme"]:9s} {r["operation"]:6s} '
          f'{r["scope"]}: {r["estimated_share_pct"]:.2f}% '
          f'[{r["engineering_low_pct"]:.1f}, {r["engineering_high_pct"]:.1f}] '
          f'hot={r["estimated_hotspot_cycles"]:,} / '
          f'denominator={r["denominator_cycles"]:,} ({r["denominator_source"]})')
