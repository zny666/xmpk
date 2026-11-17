# Xmpk RTL

SystemVerilog hardware for **Xmpk: A Cross-Scheme RISC-V Extension for Multivariate Post-Quantum Signatures**.

This source snapshot contains the adopted 64-bit accelerator design, including QGEMM request overlap, finite-field arithmetic, matrix engines, and the CV-X-IF coprocessor interface. It contains 37 Xmpk instructions plus the inherited NOP entry; `rtl/include/cvxif_instr_pkg.sv` defines the encodings and operand-read masks.

## Contents

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

## Integration

Use `cvxif_example_coprocessor` for the typed CV-X-IF interface, or `copro_alu` for a direct opcode/operand interface. The adopted configuration uses `XLEN=64` and three register-read ports. Several instructions consume the old destination register through operand 3.

The integrating CPU supplies CV-X-IF packed structure types and connects the accelerator memory port to its memory system. The CPU RTL and its accelerator-to-memory adapter are external dependencies; this snapshot contains the coprocessor RTL.

See [integration notes](docs/INTEGRATION.md) for operand mapping, handshakes, completion handling, and configuration requirements. A standalone engine can be elaborated with Verilator from the repository root:

```sh
verilator --lint-only --assert -Wno-fatal \
  --top-module xmpk_qgemm -f rtl/files.f
```

Other engine entry points include `xmpk_mqeval`, `xmpk_solveq`, `xmpk_linxfm`, and `xmpk_extgemm`. Elaborating the typed wrapper requires the host's concrete type parameters; its default `logic` parameters are placeholders.

## Snapshot and validation

`snapshot.json` records the public CVA6 base revision, adopted source hashes, release hashes, and file-level changes. RTL behavior is preserved from the adopted source. Three modified upstream files carry an additional modification notice; their original copyright and SPDX notices remain intact.

`SHA256SUMS` covers the release contents. [Validation](docs/VALIDATION.md) records the checks performed on this snapshot.

This is the adopted solver implementation. Its pivot handling and singular-input behavior are data dependent. Fixed-address constant-time solver candidates, physical side-channel protection, and sensitive-state erasure are separate work.

## Licensing

Five upstream-derived files retain `Apache-2.0 WITH SHL-2.0`. The twelve added RTL files currently have no selected release license. This directory is a release candidate pending that selection; see [licensing status](LICENSE.md) and [attribution](NOTICE).
