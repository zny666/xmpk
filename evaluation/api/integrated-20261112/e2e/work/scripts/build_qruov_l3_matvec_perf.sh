#!/bin/bash
set -euo pipefail

ROOT="${ROOT:-$HOME/riscv/xmpk-e2e-five}"
CVA6="${CVA6_ROOT:-$HOME/riscv/cva6-e2e-five}"
WORK="$ROOT/work"
OUT="$ROOT/build/qruov-l3-matvec-perf"
CC="${CC:-$HOME/riscv/toolchain/bin/riscv64-unknown-elf-gcc}"
NM="${NM:-$HOME/riscv/toolchain/bin/riscv64-unknown-elf-nm}"
PICOLIBC="${PICOLIBC:-/usr/lib/picolibc/riscv64-unknown-elf}"

mkdir -p "$OUT"
for mode in scalar mul8 striped prepacked; do
  perf_mode=0
  if [ "$mode" = mul8 ]; then
    perf_mode=1
  elif [ "$mode" = striped ]; then
    perf_mode=2
  elif [ "$mode" = prepacked ]; then
    perf_mode=3
  fi
  "$CC" \
    -std=gnu11 -O3 -g -fno-builtin -fno-lto -fno-strict-aliasing \
    -fno-tree-vectorize -fno-tree-slp-vectorize -static -mcmodel=medany \
    -fvisibility=hidden -ffunction-sections -fdata-sections \
    -nostdlib -nostartfiles -march=rv64gc_zba_zbb_zbs_zbc -mabi=lp64d \
    -T"$CVA6/config/gen_from_riscv_config/linker/link.ld" \
    -DPERF_MODE="$perf_mode" \
    -I"$WORK/runtime" -I"$CVA6/verif/tests/custom/common" \
    -I"$CVA6/verif/tests/custom/env" \
    "$WORK/tests/qruov_l3_matvec_perf.c" \
    "$WORK/runtime/syscalls.c" \
    "$WORK/runtime/picolibc_shim.c" \
    "$WORK/runtime/crt.S" \
    -Wl,--gc-sections -L"$PICOLIBC/lib" -lc -lgcc \
    -o "$OUT/qruov-l3-matvec-$mode.elf"
  test -z "$("$NM" -u "$OUT/qruov-l3-matvec-$mode.elf")"
done

echo QRUOV_L3_MATVEC_PERF_BUILD_PASS
