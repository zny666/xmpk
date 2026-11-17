# Release validation

The release was prepared from the adopted Xmpk RTL snapshot with QGEMM request overlap.

## Source integrity

All 17 adopted RTL files were compared across the frozen design, the local development tree, and the design used for the corresponding implementation evaluation. Their adopted hashes match.

The initial RTL snapshot preserved fourteen adopted files byte for byte and added a two-line modification notice to three upstream-derived files. The licensing update additionally adds SPDX comments to the twelve new RTL files. Removing these release-only comments restores the adopted source bytes. RTL expressions, states, modules, ports and instruction encodings remain unchanged.

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

## Extended reproduction materials

The release now includes instruction-level tests, assembly interfaces, firmware sources, a pinned CPU integration patch, standalone build/measurement scripts and an OOC FPGA flow. Current checks and reference measurements are recorded in [release results](../results/VALIDATION.md).

## Scope of the initial RTL checks

This release check establishes source preservation and elaboration of the packaged dependencies. It adds no new synthesis, FPGA timing, physical side-channel, whole-signature, or arbitrary-parameter verification claims. The RTL supports the adopted RV64 configuration described in the integration notes.

The initial check covered the RTL snapshot. The extended artifact adds source-level reproduction materials and concise results. Generated binaries/checkpoints, private environment paths and remote machine identifiers remain outside the release.
