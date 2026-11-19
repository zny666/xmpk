# API measurements confirmed by the author

The author reported an 8% reduction in QR-UOV L1 KeyGen cycles and an
approximately 1.2% reduction in UOV-Is Sign cycles on 2026-11-13.
These are reductions in API time/cycles, so the applicable multipliers
are 0.92 and 0.988, respectively.

| Parameter set | API | Cycle multiplier | Reported cycles | Software baseline / reported cycles |
|---|---|---:|---:|---:|
| QR-UOV-1q127L3 | KeyGen | 0.92 | 144,838,382 | 1.440880× |
| UOV-Is | Sign | approximately 0.988 | 823,862 | 5.662935× |

The preserved baseline counts are unchanged. The generated data applies
the factors to the canonical archived counts, then rounds once to an
integer cycle. Whole-run counts sum the three API stages.

The author supplied reduction ratios, without external raw counter logs.
The CSV and manuscript therefore retain the externally validated
factor-derived status and the dagger marker. No reduction is transferred
to another API, parameter set, or security level.

The implementation uses degree-specific EXTGEMM arithmetic with registered
accumulation, SOLVEQ phase-specific row-word traversal, and QGEMM's
established datapath.
