# Public artifact validation

The release preserves the logic of all 17 RTL files from the first public snapshot. The checks below exercise the supporting materials added to that snapshot.

The subsequent licensing update adds SPDX comments and updates the generator's licence headers. Existing source hashes in validation/build records refer to the tested pre-licensing snapshot. `licensing-update.json` maps those files to their licensed bytes; removing the added SPDX comment recovers the tested source. Measurement values and binary/log hashes are preserved.

| Check | Result | Evidence |
|---|---|---|
| Instruction encodings and operand readiness | PASS | All 38 entries × 32 variable-field patterns × 8 valid-register masks; unknown encodings rejected |
| EXTGEMM | PASS | 34 measured cases, degrees 3/10, boundary shapes, stalls, fault recovery and reset |
| QGEMM | PASS | 72 measured cases, five fields, cache boundaries, stalls, fault recovery and reset |
| SOLVEQ | PASS | 86 measured cases, GF(16)/AES-GF(256), dense invertible/singular systems, stalls, faults and reset |
| MQEVAL | PASS | 12 measured cases, N up to 128, packed equation/output boundaries, stalls, faults and reset |
| LINXFM | PASS | 38 measured cases, L up to 16/count up to 255, padding, stalls, faults and reset |
| Arithmetic and TRF/GER | PASS | Random/zero/all-one arithmetic, old-rd MAC/elimination, TRF banks, each legacy GER path, autonomous 16/32 products, coherent store and fault recovery |
| Clean pinned CVA6 overlay | PASS | Patch applied to a clean checkout at `742dc82a0c706e0a3bfb98b709fd3ee06d50d12a` |
| Full CPU source build | PASS | Public `build_cpu.sh`, Verilator 5.008, matching upstream hardware submodule sources, installed compatible Spike/YAML-CPP |
| Public firmware compilation | PASS | GCC 12.2.0, 19 ELF targets and compile-only calls to every assembly entry point |
| Public firmware execution and cycle collection | PASS | All 19 targets passed on the newly built CPU model; complete JSON/CSV cycle records retained |
| Public Vivado implementation | PASS | Complete synthesis/place/route using `fpga/build.tcl`; fresh area, clocked timing and vectorless power reproduce the adopted reference |

`regression.json` records suite results, test-source hashes and log digests. `cpu-build.json` records the successfully built model, public build-entry/patch hashes and smoke test. The C++ random seed is `0x225613`. Case counts refer to emitted engine metrics; negative tests and arithmetic loops add coverage beyond those counts. Existing RTL width/optimization warnings remain under `-Wno-fatal`.

## Reference measurements

`qgemm-reference.json` contains the archived request-overlap CVA6 results: hardware 218/2,320/15,760 cycles versus scalar 18,555/368,582/2,836,367 for (M,K,N)=(3,5,16)/(7,19,32)/(16,64,32). SETUP is excluded. These rows carry their original ELF, log and model digests.

`field-kernel-reference.json` contains archived SOLVE/EXT results with the individual adopted engine sources and their original model hash. SOLVE/EXT count SETUP; EXT layout also includes packing/unpacking. Compiler/startup/memory layout changes can alter architectural cycles. Fresh public firmware measurements are saved separately from these archived references.

`benchmark-public-rerun.json` and `.csv` contain the complete public-firmware rerun: one correctness example and 18 timed scalar/hardware/layout kernels. The runner validated complete termination, decoded status, process exit and ELF/log hashes for every target. The model hash matches `cpu-build.json`. `firmware-build.json` records the exact per-target compiler flags/definitions with portable paths and the measured ELF hashes. Timings retain the boundaries stated in each row; they are architectural cycles, with some targets run in independent host processes.

`ppa-reference.json` records the adopted Q-overlap coprocessor OOC results: 13,392 LUT, 8,021 FF, 4 BRAM tiles, 8 DSP; 80 MHz WNS=0.144 ns/WHS=0.098 ns. Vectorless post-route estimated power is 0.224 W total, 0.102 W dynamic and 0.122 W static, medium confidence. This reference uses `xc7a200tfbg676-2` and a clock-only constraint. Its external I/O paths require host timing budgets for board signoff.

`ppa-public-rerun.json` records the completed public-flow rerun. It reproduces all of those utilization, timing and power values and records separate report digests. The routed checkpoint was generated successfully. Existing OOC port-placement warnings correspond to the external I/O timing scope described above.

All reference measurements have explicit scopes and provenance digests. The published FPGA flow regenerates timing/utilization/power reports and a routed checkpoint. Whole-signature API benchmarks, fixed-time solver candidates, physical side-channel protection and complete board implementation have their own validation scope.
