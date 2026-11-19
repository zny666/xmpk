#!/bin/bash
set -euo pipefail

ROOT="${ROOT:-$HOME/riscv/xmpk-e2e-five}"
CVA6="${CVA6_ROOT:-$HOME/riscv/cva6-e2e-five}"
WORK="$ROOT/work"
OUT="$ROOT/build/qruov-prepare-perf"
TOOLCHAIN_BIN="${TOOLCHAIN_BIN:-$HOME/riscv/toolchain/bin}"
CC="${CC:-$TOOLCHAIN_BIN/riscv64-unknown-elf-gcc}"
NM="${NM:-$TOOLCHAIN_BIN/riscv64-unknown-elf-nm}"
PICOLIBC="${PICOLIBC:-$("$CC" -print-sysroot)}"

if [ -z "$PICOLIBC" ] || [ ! -f "$PICOLIBC/lib/libc.a" ]; then
  if [ -f /usr/lib/picolibc/riscv64-unknown-elf/lib/libc.a ]; then
    PICOLIBC=/usr/lib/picolibc/riscv64-unknown-elf
  else
    PICOLIBC="$(cd "$TOOLCHAIN_BIN/.." && pwd)/riscv64-unknown-elf"
  fi
fi

mkdir -p "$OUT"
for mode in unprepared prepared; do
  prepared=0
  if [ "$mode" = prepared ]; then
    prepared=1
  fi
  "$CC" \
    -std=gnu11 -O3 -g -fno-builtin -fno-lto -fno-strict-aliasing \
    -fno-tree-vectorize -fno-tree-slp-vectorize -static -mcmodel=medany \
    -fvisibility=hidden -ffunction-sections -fdata-sections \
    -nostdlib -nostartfiles -march=rv64gc_zba_zbb_zbs_zbc -mabi=lp64d \
    -T"$CVA6/config/gen_from_riscv_config/linker/link.ld" \
    -DPERF_PREPARED="$prepared" \
    -I"$WORK/runtime" -I"$CVA6/verif/tests/custom/common" \
    -I"$CVA6/verif/tests/custom/env" \
    "$WORK/tests/qruov_prepare_perf.c" \
    "$WORK/runtime/xmpk_asm.c" \
    "$WORK/runtime/syscalls.c" \
    "$WORK/runtime/picolibc_shim.c" \
    "$WORK/runtime/crt.S" \
    -Wl,--gc-sections -L"$PICOLIBC/lib" -lc -lgcc \
    -o "$OUT/qruov-prepare-$mode.elf"
  test -z "$("$NM" -u "$OUT/qruov-prepare-$mode.elf")"
done

echo QRUOV_PREPARE_PERF_BUILD_PASS
