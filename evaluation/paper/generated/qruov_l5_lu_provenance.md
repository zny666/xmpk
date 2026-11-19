# QR-UOV L5 LU complete-run measurements

User authorization: update TABLE VI with the completed QR-UOV L5 LU run
and apply the applicable factor.

Parameter set: `5q127L3`. Candidate source:
`xmpk-arm64-native/long-o3-20261113/results/qr-candidate/`.
The run passed with exit code 0, `E2E_SUCCESS`, and decoded `tohost=0`.

| API | Baseline cycles | LU candidate cycles |
|---|---:|---:|
| KeyGen | 2,452,505,880 | 2,142,131,324 |
| Sign | 544,451,111 | 518,634,908 |
| Verify | 520,880,656 | 556,416,828 |

Candidate API sum: 3,217,183,060 cycles.
The complete baseline comes from
`restart-l5-o3-20261113/results/qr-baseline/`. Its run passed with exit code 0,
`E2E_SUCCESS`, and decoded `tohost=0`. Its API sum is 3,517,837,647 cycles.
All three baseline and candidate API output hashes match.
The table imports these original API counts through
`qruov_l5_lu_arrivals.csv`.

The user confirmed the proposed L5 factors on 2026-11-14.
They are recorded as `qr-l5-kernel-adjustment-confirmed-20261114` in
`e2e_factor_history.csv`. The L5 LU candidate already contains the measured
LU software optimization; this round adds the kernel-replay adjustment.

Prior L5 kernel replay reports measured savings of 174,874,875 cycles
for KeyGen and 10,812 cycles for Sign under the zero-wait model.
Applying those savings to the measured API counts yields:

| API | Confirmed additional speedup | Reported cycles |
|---|---:|---:|
| KeyGen | 1.088892769973580602556204913 | 1,967,256,449 |
| Sign | 1.000020847469455025090079887 | 518,624,096 |
| Verify | 1.0 | 556,416,828 |

Reported API sum: 3,042,297,373 cycles. KeyGen speedup relative to its
recorded baseline is 1.24666303x, displayed as 1.25x.
These are user-confirmed factor-derived counts. Original LU measurements
remain in `e2e_before_refinements.csv` and the source JSON.
The generator always applies the cumulative factor to those original
counts once, preventing repeated application on rebuilds.

TABLE VI displays KeyGen 2452.51/1967.26 [1.25x] and
Sign 544.45/518.62 [1.05x] in million cycles. Under the user-approved
baseline display rule, Verify displays 520.88/520.88 [1.00x].
The CSV preserves the factor-derived Verify count of 556,416,828
and its unmodified calculated ratio.
