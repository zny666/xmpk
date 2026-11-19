#!/bin/bash
set -euo pipefail
ROOT="${ROOT:-$HOME/riscv/xmpk-e2e-five}"
CVA6="${CVA6_ROOT:-$HOME/riscv/cva6-e2e-five}"
CC="${CC:-$HOME/riscv/toolchain/bin/riscv64-unknown-elf-gcc}"
PICOLIBC=/usr/lib/picolibc/riscv64-unknown-elf
WORK="$ROOT/work"
OUT="$ROOT/build/other-short"
mkdir -p "$OUT"
for algo in 1 2 3 4; do
  modes="0 1 3"
  if [ "$algo" = 1 ]; then modes="0 1 2 3 4"; fi
  if [ "$algo" = 2 ]; then modes="0 1"; fi
  for flags in $modes; do
    "$CC" -std=gnu11 -O3 -g -fno-builtin -fno-lto -fno-strict-aliasing \
      -fno-tree-vectorize -fno-tree-slp-vectorize -static -mcmodel=medany \
      -fvisibility=hidden -ffunction-sections -fdata-sections \
      -nostdlib -nostartfiles -march=rv64gc_zba_zbb_zbs_zbc -mabi=lp64d \
      -T"$CVA6/config/gen_from_riscv_config/linker/link.ld" \
      -DALGORITHM="$algo" -DFLAGS="$flags" \
      -I"$WORK/runtime" -I"$CVA6/verif/tests/custom/common" -I"$CVA6/verif/tests/custom/env" \
      "$WORK/tests/other_algorithms_short.c" "$WORK/runtime/syscalls.c" \
      "$WORK/runtime/picolibc_shim.c" "$WORK/runtime/crt.S" \
      -Wl,--gc-sections -L"$PICOLIBC/lib" -lc -lgcc -o "$OUT/a$algo-f$flags.elf"
    echo "BUILD_PASS a$algo-f$flags"
  done
done
