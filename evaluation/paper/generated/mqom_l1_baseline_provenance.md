# MQOM-L1 baseline arrival — 2026-11-12

Parameter: `mqom3_cat1_gf16_fast_ct`; implementation:
`cva6-reference-C-portable`.

Raw evidence archive:
`/artifact/xmpk-arm64-native/formal-results-secondary/mqom1-baseline-counter-clean-recovered-20261112/`.

The guest-output log contains three complete `E2E_METRIC` lines and
`E2E_SUCCESS`; the simulator log confirms `tohost = 0`.
The output service was reattached after clearing disposable trace files.
Harness total cycles include host-output waits and are not used as API cycles.

| API | Baseline measured cycles | Preserved original Xmpk cycles | Confirmed additional speedup | Refined Xmpk cycles | Final baseline / refined speedup |
|---|---:|---:|---:|---:|---:|
| KeyGen | 10,950,253 | 10,956,327 | — | 10,956,327 | 0.999446× |
| Sign | 105,733,402 | 98,638,644 | 1.3356498456425776× | 73,850,676 | 1.431719× |
| Verify | 102,629,625 | 95,724,974 | 1.32373843693937× | 72,314,115 | 1.419220× |
| API sum | 219,313,280 | 205,319,945 | Per-stage factors | 157,121,118 | 1.395823× |

The final speedup includes the additional factor once:
`baseline / refined = (baseline / original Xmpk) × additional speedup`,
subject to integer cycle rounding. Original Xmpk cycles remain unchanged
in `e2e_before_refinements.csv`; factors remain unchanged in
`e2e_incremental_factors.csv`. Refined cycles retain the paper's dagger
notation for externally confirmed factor-derived values.
