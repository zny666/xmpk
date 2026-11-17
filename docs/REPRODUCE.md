# Reproducing the artifact

## Standalone correctness

Requirements: Linux, Python 3, C++ compiler and Verilator 5.008 (the validated version).

```sh
python3 scripts/regression.py --verilator /path/to/verilator
```

Logs and generated models go to `build/regression`. Seed `0x225613` is fixed in the C++ oracle. The instruction-level fixture supplies a 1 MiB byte-addressed memory, request backpressure every fifth cycle, and delayed responses. EXT, QGEMM, SOLVEQ, MQEVAL and LINXFM tests cover field results, output packing, consecutive calls, malformed commands, response-error recovery and reset during traffic. SOLVEQ includes constructed dense invertible systems, pivot swaps and singular cases. QGEMM and EXT include cache-capacity boundaries. Arithmetic tests check random/zero/all-one words and old-rd MAC behavior. Tile tests exercise all legacy GER paths, autonomous 16/32 products and coherent stores. Decoder tests exercise all 38 encodings and register-read masks. This fixture measures coprocessor cycles under its synthetic memory latency.

## CVA6 integration

```sh
git clone https://github.com/openhwgroup/cva6.git cva6
git -C cva6 checkout 742dc82a0c706e0a3bfb98b709fd3ee06d50d12a
git -C cva6 submodule update --init --recursive
python3 scripts/integrate.py cva6
```

The overlay requires a clean pinned checkout. It adds the coherent memory adapter, connects the LSU/MMU and read/write cache ports, enables the example coprocessor and configures three operand reads. Superscalar configurations inherit six read ports but the evaluated configuration is single issue. The existing upstream old-rd mapping is used. A simulation-only patch permits RVFI trace suppression; architectural RTL behavior is unaffected by that option.

The upstream Makefile needs its recursively pinned submodules, GNU make, Verilator, a C++ compiler, YAML-CPP and a compatible Spike installation (libfesvr/libriscv/libdisasm and headers). Build Spike using the version selected by the pinned CVA6 tree. No running or prebuilt model is needed:

```sh
export CVA6_REPO_DIR="$PWD/cva6"
export SPIKE_INSTALL_DIR=/path/to/spike
export VERILATOR_INSTALL_DIR=/path/to/verilator-prefix
bash scripts/build_cpu.sh
```

Generated output defaults to `cva6/build-xmpk`; `CPU_BUILD_DIR` and `JOBS` override it. Keep a custom CPU build directory directly under the CVA6 checkout, because the pinned upstream Makefile uses relative C++ source paths. See `results/VALIDATION.md` for the checks actually completed on this release.

## Firmware and architectural cycles

The included startup and linker script provide M-mode execution, BSS initialization, a stack, and the `tohost` protocol. They require RV64GC GCC and libgcc; there is no external libc dependency.

```sh
python3 scripts/compile_benchmarks.py --cc /path/to/riscv64-unknown-elf-gcc
export LD_LIBRARY_PATH=/path/to/spike/lib
python3 scripts/run_benchmarks.py --model cva6/build-xmpk/Variane_testharness
```

Compilation also instantiates all 38 inline assembly entry points. `manifest.json` saves the compiler version, complete commands and ELF hashes. The runner checks the ELF hash, complete simulator termination, decoded status and process exit status, then emits JSON/CSV with model/log hashes. Use `--filter example` for a short smoke test. Choose a new output directory for reruns.

Cases can run in independent processes using an exact case-name `--filter` and a separate `--output` directory for each case. Each process owns its model state and output files. Architectural `rdcycle` results remain distinct from host wall-clock execution time. The complete serial suite can take tens of minutes because it includes large scalar kernels and bare-metal initialization.

QGEMM measures RUN including its fence, with SETUP excluded. SOLVEQ and EXT include SETUP and RUN. EXT layout additionally includes conversion into/out of 16-byte slots. Initialization and post-timing correctness checks are outside the interval. QGEMM performance-only hardware checks the MAC count; the instruction regression checks the full output independently. Scalar QGEMM uses the bit-serial reference. The simulator prints the already decoded exit code `cycles+1`; the runner subtracts one once. The ordinary example returns status zero. Architectural cycles depend on firmware layout, compiler and cache state; archived rows are labeled with their own provenance.

The upstream simulator labels nonzero exits as `FAILED`; kernel benchmarks intentionally carry cycle counts in that exit field. The runner validates the benchmark-specific contract, process status and failure sentinels before accepting a sample.

## FPGA implementation

```sh
vivado -mode batch -source fpga/build.tcl
python3 scripts/summarize_ppa.py build/ppa
```

The supplied Tcl implements the coprocessor OOC on `xc7a200tfbg676-2` at 80 MHz and emits post-route timing, utilization, DRC and vectorless power reports. `XMPK_PPA_OUT` changes the output directory. The constraint establishes the core clock; external I/O timing budgets and package pin locations belong to a host board design. Reports explicitly include unconstrained paths. The artifact's reproducible scope is the coprocessor OOC, with board-level CVA6/DDR/clock/reset constraints supplied by the integrator.
