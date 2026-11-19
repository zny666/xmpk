# Manuscript result index

Table labels are stable across this export even if a subsequent typesetting
pass changes numbering.

| Table / result | Command | Original evidence in export | Summary / displayed rows |
|---|---|---|---|
| TABLE I, workload parameters (`tab:workloads`) | API build variants; native metadata drivers | Five scheme sources and parameter headers, profiling build metadata | `paper/table-blocks.tex` |
| TABLE II, PMU (`tab:profile`) | `python3 scripts/recompute_profile.py --output /tmp/xmpk-pmu` | Profiling bundle: per-pass `result.json`, `perf-stat.txt`, build commands | All 60 PMU CSV rows, `paper/generated/profile_rows.tex`; byte-identical offline reconstruction |
| Related work (`tab:related`) | Inspect cited upstream paper/standard | `paper/sources/prior-work-numeric-evidence.json`; reference identifiers retained | `paper/generated/prior_work_comparison.csv`, `prior_work_rows.tex` |
| ISA (`tab:isa`) | `python3 scripts/generate_isa.py` | Released instruction package and source encodings | Root `docs/ENCODINGS.md`, `docs/ISA.md`, `paper/table-blocks.tex` |
| Kernel performance (`tab:perf`) | Original commands in kernel status/`cycles.json`; public rerun in root reproduction guide | Six QGEMM and twelve SOLVE/EXT logs | `kernel-cycles.csv`; `paper/generated/q-overlap-kernel-measured.csv`; `paper/sources/final-kernel-cycles.json` |
| TABLE VI, API / energy (`tab:e2e`) | `scripts/build_api.sh`, `scripts/run_api.sh`; energy recomputation via `scripts/evaluation.py` | `api/raw`; archived source/binary/model hashes and commands | `traceability.csv`, `submitted-table.csv`, `paper/generated/algorithm_energy.csv`; final API provenance gaps are explicit |
| Resources/timing (`tab:ppa`) | `XMPK_CASES='{{q_overlap system}}' vivado -mode batch -source evaluation/ppa/scripts/synth.tcl` | Exact 331-input source closure; route utilization, clocks, timing, DRC and logs; vanilla archival reports | `paper/generated/results.tex`, `paper/table-blocks.tex`, `ppa/input-closure.json` |
| System power (`tab:power`) | Same Vivado flow; vectorless `report_power` | `paper/sources/*power*.rpt`, `ppa/results/*/power-target.rpt` | `power.csv`, `paper/generated/results.tex`; estimated on-chip power/energy |

Paths in this table are relative to `evaluation/` unless explicitly described
as root files. Profiling member references in `pmu-traceability.csv` refer to
the restored gzip/tar bundle, not to loose files.

QGEMM excludes SETUP; SOLVE/EXT use their recorded setup/run boundaries.
Simulator `tohost` text is already a decoded exit code; measured cycles are
that value minus one. Process exit status can therefore be nonzero for a
successful timing program, and is not itself the benchmark cycle count.

For every redacted original file, `export-manifest.json` records the original
digest, a stable experiment-local source ID, and the exported digest.
Profiling members use `../profiling/member-index.json`. These manifests bind
the public bytes to their original archives without exposing private paths.
