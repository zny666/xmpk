# Release validation

The release was prepared from the adopted Xmpk RTL snapshot with QGEMM request overlap.

## Source integrity

All 17 adopted RTL files were compared across the frozen design, the local development tree, and the design used for the corresponding implementation evaluation. Their adopted hashes match.

Fourteen released RTL files are byte-identical to the adopted source. Three upstream-derived files have an added two-line modification notice: the instruction package, `copro_alu`, and the CV-X-IF wrapper. Removing that notice restores the exact adopted source bytes. The release changes no RTL expressions, states, modules, ports, or instruction encodings.

`snapshot.json` records adopted, released, and available upstream hashes. `SHA256SUMS` verifies the prepared release files.

## Elaboration

Tool: Verilator 5.008 (rev v5.008).

Two checks completed with exit code 0 and no `%Error` diagnostics:

1. All files from `rtl/files.f`, instantiated through the complete `cvxif_example_coprocessor` wrapper with explicit RV64, three-register-port, and packed host-interface types.
2. The public standalone QGEMM command shown in the README.

The typed wrapper check uses an external elaboration fixture. The fixture and tool-generated outputs are outside the RTL release.

Existing warnings were retained with `-Wno-fatal`; they were not treated as a warning-free lint result:

| Check | WIDTHEXPAND | WIDTHTRUNC | SYMRSVDWORD | UNOPTFLAT |
|---|---:|---:|---:|---:|
| Full wrapper | 67 | 8 | 1 | 1 |
| Standalone QGEMM | 31 | 0 | 0 | 0 |

`SYMRSVDWORD` refers to the fixture's host member `register`, which matches a C++ keyword. `UNOPTFLAT` refers to the packed response structure where `register_ready` reads `issue_ready` from the same structure. The width diagnostics belong to the retained RTL. Integration lint policy and any subsequent changes should be checked in the concrete host configuration.

## Scope

This release check establishes source preservation and elaboration of the packaged dependencies. It adds no new synthesis, FPGA timing, physical side-channel, whole-signature, or arbitrary-parameter verification claims. The RTL supports the adopted RV64 configuration described in the integration notes.

The prepared directory contains design RTL, source/attribution manifests, licence texts, and concise documentation. CPU sources, test programs, firmware, binaries, synthesis checkpoints, implementation logs, internal host paths, and remote machine identifiers are excluded.
