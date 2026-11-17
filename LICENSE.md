# Licensing status

This is a hardware artifact release candidate. A license for the added Xmpk RTL and newly authored supporting code has not yet been selected.

The following files retain their original `Apache-2.0 WITH SHL-2.0` notices:

- `rtl/include/cvxif_instr_pkg.sv`
- `rtl/copro_alu.sv`
- `rtl/cvxif_example_coprocessor.sv`
- `rtl/instr_decoder.sv`
- `rtl/compressed_instr_decoder.sv`

Copies of Apache License 2.0 and the Solderpad Hardware Licence 2.0 extension are in `LICENSES/`. File-level notices are authoritative; the CVA6 repository's older root license is not substituted for the licence named in these files.

The other twelve RTL files have no release licence declaration in the adopted source. Their original content is preserved. Newly authored tests, software, scripts, integration adapter and FPGA support also await the owner's licensing selection. The integration patch preserves upstream notices and licensing in the target files. Adding this licensing-status document does not grant a licence to these materials.

A consistent hardware-oriented choice is Solderpad Hardware Licence 2.0 (`Apache-2.0 WITH SHL-2.0`), matching the interface files. After that choice, add the applicable declaration and root licence statement and regenerate release hashes.
