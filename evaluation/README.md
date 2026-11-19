# Xmpk evaluation evidence

This branch exports research sources and evidence behind the manuscript. Start
with [the evidence index](provenance/table-index.md) and
[the coverage report](provenance/coverage.json). The original manuscript is not
modified by this export.

```sh
# Run from the repository root; Python 3 standard library only.
python3 scripts/evaluation.py verify
python3 scripts/evaluation.py recompute --output /tmp/xmpk-evidence
python3 scripts/recompute_profile.py --output /tmp/xmpk-pmu
python3 scripts/evaluation.py restore-profile --output /tmp/xmpk-profile
```

`provenance/traceability.csv` maps each displayed API/energy entry to the
recorded counts, available logs, commands and summary files. `kernel-cycles.csv`
recomputes all 18 final timed kernels from simulator completion records.
`profiling/member-index.json` identifies every file in the compressed text
bundle with its original and exported SHA-256. Individual collection commands
are retained in each profiling `result.json` and in the build manifests.

## API source and execution

`api/integrated-20261112` contains complete scalar/Xmpk C paths, shared runtime,
bare-metal harness, source changes, prior versions, linker/header support,
and archived compile/static-check logs. `ACCEL=0` selects the scalar path and
`ACCEL=1` selects Xmpk, with the same API wrapper. Supported variants:

```text
mayo1 mayo5 mqom1 mqom5 snova1 snova5 uov_is uov_v qruov_l3 qruov_l5
```

```sh
export TOOLCHAIN_BIN=/path/to/existing/riscv-toolchain/bin
export CC="$TOOLCHAIN_BIN/riscv64-unknown-elf-gcc"
export NM="$TOOLCHAIN_BIN/riscv64-unknown-elf-nm"
export SIZE="$TOOLCHAIN_BIN/riscv64-unknown-elf-size"
bash scripts/build_api.sh mayo1 0
bash scripts/build_api.sh mayo1 1
```

API builds require Linux and Bash 4.4 or newer. The archived toolchain was
GCC 12.2.0 with picolibc. Build scripts record the
complete compiler/linker flags. They write ELFs below the snapshot's `e2e/build`
directory. No dependencies are installed automatically.

To reconstruct the complete source tree before the integration changes:

```sh
python3 scripts/evaluation.py restore-before --output /tmp/xmpk-before
```

This restores every modified file and removes the two added QR-UOV helper
files. The `before` version is an earlier integration snapshot, not a claim
that it was the exact scalar or optimized ELF used in the final manuscript.

On Linux, using a compatible CVA6 model with the required HTIF layout:

```sh
bash scripts/run_api.sh /absolute/path/to/Variane_testharness \
  /absolute/path/to/mayo1-xmpk.elf 4294967295 /tmp/xmpk-api-new \
  "$PWD/evaluation/api/historical-tools/service_htif_writes.py"
```

The historical HTIF service uses `/proc` and requires ptrace/process-memory
access. Its simulator compatibility must be checked before reuse. The public
CPU build in `docs/REPRODUCE.md` reproduces the instruction/kernel release;
this export does not assert that its HTIF memory layout matches every historical
API simulator. A successful full API run must report `E2E_SUCCESS`: KeyGen,
Sign, Verify, message equality, and rejection of a modified signature.
Historical short/native tests have their own narrower coverage.

## Profiling reproduction

The pinned official reference sources, native drivers and collection scripts
are in `profiling/reference`. The original environment was native Linux
RISC-V, GCC 15.2.0, `-O3 -march=rv64gc`, OpenSSL, binutils and perf.
Build/collection scripts require the RISC-V machine and CPU affinity entries
3 and 7. On that machine, from the repository root:

```sh
mkdir -p evaluation/profiling/reference/build evaluation/profiling/reference/raw
python3 evaluation/profiling/reference/tools/build_reference.py \
  --openssl-prefix /absolute/path/to/openssl
# Use the newly printed build manifest:
python3 evaluation/profiling/reference/tools/run_reference.py \
  --build "$PWD/evaluation/profiling/reference/build/BUILD-ID/manifest.json"
```

The text bundle includes all original calibration, seven timing batches per
case, core/branch/L1D stat batches, full self/children reports, text samples,
build commands and final report summaries. `scripts/recompute_profile.py`
checks the raw perf-stat event counts and actual iterations, reconstructs
medians for all 60 cases, selects the slowest API by median measured time, and
regenerates the five TABLE II rows byte-for-byte.

Binary `perf.data`, ELF/DSO files and private memory fixtures are not exported.
The text supports offline numeric verification and call-chain inspection;
fresh perf symbolization or replay of the original random signatures needs
new measurements. Full self sample shares are sampling-period shares, while
children shares overlap. They are not exact wall-time fractions.

## FPGA reproduction

`ppa/q_overlap/cva6` contains the exact 331-input source snapshot for the
recorded full-system implementation; every original source hash was verified.
Its source/include order is in `ppa/source-list.json`; dependency license
texts are preserved in `ppa/licenses/cva6`.

```sh
XMPK_CASES='{{q_overlap system}}' XMPK_THREADS=2 \
  vivado -mode batch -source evaluation/ppa/scripts/synth.tcl
```

The script derives its root from its own path. It uses Vivado 2025.2,
`xc7a200tfbg676-2`, a 5 ns synthesis constraint and a 25 ns implementation
constraint for the full system. It retains reports for target timing,
the maximum passing integer MHz, the failing next MHz, utilization, DRC,
clock coverage, and vectorless power. Existing reports are archival evidence;
copy the evaluation directory to a fresh location before a new FPGA run.
Checkpoints are omitted from publication.

The original Tcl's `frequency.json` names `target_MHz` using the re-clocked
period; use `clocks-target.rpt` and `timing-target.rpt` for the actual target.
This historical metadata issue is preserved and documented rather than
silently rewriting the recorded results.

Vanilla CVA6 implementation reports and scripts are in `ppa/vanilla`.
Its historical scripts require the separately recorded source setup and
synthesis checkpoint. The final Xmpk source closure is not substituted as
a vanilla input.

## Measurement and provenance limits

All current displayed API values are author-confirmed measurements.
The available local archives provide numeric matches for 20 of the 60
baseline/Xmpk API entries. For the other 40 entries the final counter log
has not been found in this export. Even a numeric match requires the exact
source/ELF/model identity before declaring the complete final experiment
reproducible. The integrated C snapshot was compile/link validated for ten
optimized variants in its original run; it has not been established as
the source of every final measured API value.

Historical factor/projection classifications remain identifiable in their
original CSV/JSON records. They do not reinterpret the author-confirmed
submitted table and are not used to fabricate missing final logs.
Displayed cycles can clamp an optimized value at the scalar baseline;
energy uses recorded unclamped counts and vectorless power.
API energy is an on-chip estimate using 40 MHz and 0.221/0.269 W, with
external memory excluded.

Publication validation is offline: hashes, raw counter/event parsing,
table/energy arithmetic, source closure, script syntax and privacy checks.
It does not claim new full API executions, RTL simulations or Vivado runs.

## Redaction and licenses

Public logs preserve timing, counters, errors, source digests and commands,
with private path prefixes, user/host identifiers and credentials removed.
Sanitized paths in archival commands are logical placeholders. Original and
exported hashes distinguish redacted bytes; no reverse identity map is
published. Instruction traces, register/memory dumps and binary perf data
are excluded.

See [third-party licenses](licenses/README.md). Upstream source/header notices
and complete license texts remain applicable. QR-UOV's official intellectual
property statement is retained for NIST public evaluation and standardized
implementation purposes; it is not relabeled as Apache-licensed source.
