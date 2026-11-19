#!/bin/bash
set -euo pipefail

ROOT="${ROOT:-$HOME/riscv/xmpk-e2e-five}"
CVA6="${CVA6_ROOT:-$HOME/riscv/cva6-e2e-five}"
WORK="$ROOT/work"
OUT="$ROOT/build/uov-gf256-batch-perf"
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
for mode in scalar-rows batch; do
  batch=0
  if [ "$mode" = batch ]; then
    batch=1
  fi
  "$CC" \
    -std=gnu11 -O3 -g -fno-builtin -fno-lto -fno-strict-aliasing \
    -fno-tree-vectorize -fno-tree-slp-vectorize -static -mcmodel=medany \
    -fvisibility=hidden -ffunction-sections -fdata-sections \
    -nostdlib -nostartfiles -march=rv64gc_zba_zbb_zbs_zbc -mabi=lp64d \
    -T"$CVA6/config/gen_from_riscv_config/linker/link.ld" \
    -DPERF_BATCH="$batch" \
    -I"$WORK/runtime" -I"$CVA6/verif/tests/custom/common" \
    -I"$CVA6/verif/tests/custom/env" \
    "$WORK/tests/uov_gf256_batch_perf.c" \
    "$WORK/runtime/xmpk_asm.c" \
    "$WORK/runtime/syscalls.c" \
    "$WORK/runtime/picolibc_shim.c" \
    "$WORK/runtime/crt.S" \
    -Wl,--gc-sections -L"$PICOLIBC/lib" -lc -lgcc \
    -o "$OUT/uov-gf256-$mode.elf"
  test -z "$("$NM" -u "$OUT/uov-gf256-$mode.elf")"
done

echo UOV_GF256_BATCH_PERF_BUILD_PASS
