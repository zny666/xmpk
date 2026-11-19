#!/bin/bash
set -euo pipefail
ROOT="$(dirname "$(dirname "$(realpath "$0")")")"
V="$HOME/toolchain/verilator-v5.008/bin/verilator"
export XMPK_CACHE_EDGES=1
rtl="$ROOT/final/cva6/core/cvxif_example"
for entry in "EXT 0" "Q 1" "SOLVE 2"; do
  read -r kind index <<< "$entry"
  out="$ROOT/results/instruction-final-$kind"
  mkdir -p "$out"
  sources=("$rtl/include/cvxif_instr_pkg.sv")
  for name in gf2n_mul gf256_inv8 gf256_inv_rom gf_assist xmpk_alu xmpk_ger_trf \
    xmpk_qgemm xmpk_mqeval xmpk_solveq xmpk_linxfm xmpk_extgemm xmpk_slow_mul \
    copro_alu instr_decoder; do sources+=("$rtl/$name.sv"); done
  "$V" --cc --exe --build --assert -j 1 -Wno-fatal --top-module instruction_top \
    -GTEST_KIND="$index" --Mdir "$out/obj" \
    -CFLAGS "-O2 -D$kind -DDUT_CLASS=Vinstruction_top -DHEADER=\\\"Vinstruction_top.h\\\"" \
    "${sources[@]}" "$ROOT/tests/instruction_top.sv" "$ROOT/tests/harness.cpp" >"$out/build.log" 2>&1
  "$out/obj/Vinstruction_top" >"$out/test.log" 2>&1
  tail -n 1 "$out/test.log"
done
