# Xmpk hardware artifact

SystemVerilog hardware for **Xmpk: A Cross-Scheme RISC-V Extension for Multivariate Post-Quantum Signatures**.

This source snapshot contains the adopted 64-bit accelerator design, including QGEMM request overlap, finite-field arithmetic, matrix engines, and the CV-X-IF coprocessor interface. It contains 37 Xmpk instructions plus the inherited NOP entry; `rtl/include/cvxif_instr_pkg.sv` defines the encodings and operand-read masks.

This repository provides the anonymous artifact for peer review. We will formally open-source Xmpk upon acceptance.

## Quick Start

Run the instruction-level correctness suite first. Its requirements are Linux, Python 3, a C++ compiler, GNU make, and Verilator 5.008.

```sh
python3 scripts/regression.py
```

Run this command from the repository root, with `verilator` on `PATH`. If Verilator is installed elsewhere, pass `--verilator /path/to/verilator`. A successful run exits with status 0 and finishes with:

```text
ALL_REGRESSIONS_PASS
```

Build and test logs are written to `build/regression/<suite>/build.log` and `build/regression/<suite>/test.log`. The seven suites are `DECODER`, `EXT`, `Q`, `SOLVE`, `MQ`, `LIN`, and `ARITH`.

| Stage | Toolchain |
|---|---|
| Standalone correctness | Linux, Python 3, C++ compiler, GNU make, Verilator 5.008 |
| CVA6 simulation and firmware | Verilator 5.008, C++17 compiler, RV64GC GCC 12.2.0, compatible Spike libraries/headers and YAML-CPP |
| FPGA implementation | Vivado 2025.2 |

For CPU setup, firmware compilation, cycle collection, and FPGA commands, follow the [reproduction guide](docs/REPRODUCE.md).

## Validation and paper results

The [validation report](results/VALIDATION.md) records the completed checks and measurement provenance.

| Check | Recorded result | Evidence |
|---|---|---|
| Instruction-level correctness | Seven suites passed | [regression.json](results/regression.json) |
| CVA6 build and firmware execution | CPU model built; all 19 targets passed (one example and 18 timed kernels) | [cpu-build.json](results/cpu-build.json), [firmware-build.json](results/firmware-build.json), [benchmark-public-rerun.json](results/benchmark-public-rerun.json) |
| Vivado implementation | Synthesis, placement and routing completed at the 80 MHz clock constraint; 13,392 LUTs, 8,021 FFs, four BRAM tiles and eight DSPs; estimated total power 0.224 W | [ppa-public-rerun.json](results/ppa-public-rerun.json) |

Use the following files to locate the data behind the paper's instruction, kernel, and coprocessor results:

| Paper result | Repository files |
|---|---|
| Instruction encodings and operand mapping | [ENCODINGS.md](docs/ENCODINGS.md), [ISA.md](docs/ISA.md), `software/xmpk_asm.h` |
| QGEMM kernel cycle results | [qgemm-reference.json](results/qgemm-reference.json), `software/qgemm_benchmark.c` |
| SOLVE and EXT kernel cycle results | [field-kernel-reference.json](results/field-kernel-reference.json), `software/field_benchmark.c` |
| Coprocessor utilization, timing and power | [ppa-reference.json](results/ppa-reference.json), [ppa-public-rerun.json](results/ppa-public-rerun.json), `fpga/build.tcl` |

The public firmware rerun also has a [CSV export](results/benchmark-public-rerun.csv). Reference measurements and rerun measurements retain their respective compiler/model/log hashes and timing boundaries in the validation report.

## Reproduction materials

| Directory | Material |
|---|---|
| `tests/` | Instruction-level C++ reference tests, random/boundary cases, backpressure, faults and reset recovery |
| `software/` | All-instruction assembly header, a small example, standalone startup/linker and QGEMM/SOLVE/EXT benchmarks |
| `integration/` | Patch against CVA6 commit `742dc82a0c706e0a3bfb98b709fd3ee06d50d12a` and coherent memory adapter |
| `scripts/` | ISA generation, regression, CPU integration/build, firmware compilation and cycle collection |
| `fpga/` | Vivado OOC implementation Tcl, Artix-7 part and 80 MHz clock constraint |
| `results/` | Concise release validation and measured reference data with scope/provenance |

Start with [reproduction instructions](docs/REPRODUCE.md). [ISA usage](docs/ISA.md) and the [37-instruction encoding table](docs/ENCODINGS.md) specify operands, layouts, SETUP/RUN and completion status.

## Integration

Use `cvxif_example_coprocessor` for the typed CV-X-IF interface, or `copro_alu` for a direct opcode/operand interface. The adopted configuration uses `XLEN=64` and three register-read ports. Several instructions consume the old destination register through operand 3.

The integrating CPU supplies CV-X-IF packed structure types and connects the accelerator memory port to its memory system. `integration/` contains the pinned CVA6 patch and coherent memory adapter; the upstream CPU and its submodules are fetched separately.

See [integration notes](docs/INTEGRATION.md) for operand mapping, handshakes, completion handling, and configuration requirements. A standalone engine can be elaborated with Verilator from the repository root:

```sh
verilator --lint-only --assert -Wno-fatal \
  --top-module xmpk_qgemm -f rtl/files.f
```

Other engine entry points include `xmpk_mqeval`, `xmpk_solveq`, `xmpk_linxfm`, and `xmpk_extgemm`. Elaborating the typed wrapper requires the host's concrete type parameters; its default `logic` parameters are placeholders.

## RTL file reference

Paths in this table are relative to `rtl/`.

| Files | Purpose |
|---|---|
| `gf2n_mul.sv`, `gf256_inv8.sv`, `gf256_inv_rom.sv`, `gf_assist.sv` | Packed finite-field arithmetic and helpers |
| `xmpk_alu.sv`, `xmpk_slow_mul.sv` | Arithmetic execution paths |
| `xmpk_ger_trf.sv` | Tile register storage and outer-product engines |
| `xmpk_qgemm.sv` | Rectangular packed matrix multiplication |
| `xmpk_mqeval.sv` | Multivariate quadratic evaluation |
| `xmpk_solveq.sv` | GF(16)/AES-GF(256) linear-system solver |
| `xmpk_linxfm.sv` | Extension-field linear transformation |
| `xmpk_extgemm.sv` | Extension-field matrix multiplication |
| `copro_alu.sv` | Dispatch, completion, and accelerator memory arbitration |
| `instr_decoder.sv`, `compressed_instr_decoder.sv` | Instruction decode |
| `cvxif_example_coprocessor.sv` | Typed CV-X-IF wrapper |
| `include/cvxif_instr_pkg.sv` | Instruction encodings and response types |

`rtl/files.f` lists all 17 RTL files in compilation order. Paths are relative to the repository root. No generated ROM data files are required: inverse tables are initialized in the RTL.

## Snapshot and validation

`snapshot.json` records the public CVA6 base revision, adopted source hashes, release hashes, and file-level changes. RTL behavior is preserved from the adopted source. Three modified upstream files carry an additional modification notice; their original copyright and SPDX notices remain intact.

`SHA256SUMS` covers the release contents. The [validation report](results/VALIDATION.md) records the current checks and measurements; [source integrity notes](docs/VALIDATION.md) describe the initial RTL snapshot.

This is the adopted solver implementation. Its pivot handling and singular-input behavior are data dependent. Fixed-address constant-time solver candidates, physical side-channel protection, and sensitive-state erasure are separate work.

## Licensing

Xmpk is licensed under `Apache-2.0 WITH SHL-2.0`, matching the included CVA6 CV-X-IF interface files. See [LICENSE](LICENSE), [licensing details](LICENSE.md) and [attribution](NOTICE). Existing upstream notices and dependency licences remain applicable.
