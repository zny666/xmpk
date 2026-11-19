#!/bin/bash
set -euo pipefail

ROOT="${ROOT:-$HOME/riscv/xmpk-e2e-five}"
CVA6="${CVA6_ROOT:-$HOME/riscv/cva6-e2e-five}"
WORK="$ROOT/work"
OUT="$ROOT/build/software-opt-perf"
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

build_one() {
  local source="$1"
  local output="$2"
  shift 2
  "$CC" \
    -std=gnu11 -O3 -g -fno-builtin -fno-lto -fno-strict-aliasing \
    -fno-tree-vectorize -fno-tree-slp-vectorize -static -mcmodel=medany \
    -fvisibility=hidden -ffunction-sections -fdata-sections \
    -nostdlib -nostartfiles -march=rv64gc_zba_zbb_zbs_zbc -mabi=lp64d \
    -T"$CVA6/config/gen_from_riscv_config/linker/link.ld" \
    "$@" \
    -I"$WORK/runtime" -I"$CVA6/verif/tests/custom/common" \
    -I"$CVA6/verif/tests/custom/env" \
    "$WORK/tests/$source" \
    "$WORK/runtime/xmpk_asm.c" \
    "$WORK/runtime/syscalls.c" \
    "$WORK/runtime/picolibc_shim.c" \
    "$WORK/runtime/crt.S" \
    -Wl,--gc-sections -L"$PICOLIBC/lib" -lc -lgcc \
    -o "$OUT/$output.elf"
  test -z "$("$NM" -u "$OUT/$output.elf")"
}

for batch in 1 2 3; do
  build_one mqom_matvec_batch_perf.c "mqom-batch-$batch" \
    -DPERF_BATCH="$batch"
done

build_one snova_prepare_perf.c snova-unprepared -DPERF_PREPARED=0
build_one snova_prepare_perf.c snova-prepared -DPERF_PREPARED=1
build_one mayo_packed_rows_perf.c mayo-rowwise -DPERF_BATCH_ROWS=0
build_one mayo_packed_rows_perf.c mayo-batch-rows -DPERF_BATCH_ROWS=1
build_one mayo_packed_rows_perf.c mayo-rowwise-3 \
  -DPERF_BATCH_ROWS=0 -DPERF_ROWS=3
build_one mayo_packed_rows_perf.c mayo-batch-rows-3 \
  -DPERF_BATCH_ROWS=1 -DPERF_ROWS=3

echo SOFTWARE_OPT_PERF_BUILD_PASS
