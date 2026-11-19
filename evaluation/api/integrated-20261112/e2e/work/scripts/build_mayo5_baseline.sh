#!/bin/bash
set -euo pipefail

ROOT="${ROOT:-$HOME/riscv/xmpk-e2e-five}"
CVA6="${CVA6_ROOT:-$HOME/riscv/cva6-e2e-five}"
R20="$ROOT/source/r20"
MAYO="$R20/results/received/upstream/mayo"
WORK="$ROOT/work"
ACCEL="${ACCEL:-0}"
if [ "$ACCEL" = 1 ]; then
  MAYO_ACCEL="${MAYO_ACCEL:-packed}"
  if [ "$MAYO_ACCEL" = wide ]; then
    OUT="$ROOT/build/mayo5-wide"
    output_name="mayo5-wide"
    implementation="cva6-xmpk-gf16-qgemm-wide"
    accel_flags=(-DXMPK_ACCEL -DXMPK_MAYO_QGEMM)
  else
    OUT="$ROOT/build/mayo5-xmpk"
    output_name="mayo5-xmpk"
    implementation="cva6-xmpk-gf16-qgemm-packed"
    accel_flags=(-DXMPK_ACCEL)
  fi
  arithmetic_source="$WORK/source/mayo/src/arithmetic.c"
  accel_sources=("$WORK/runtime/xmpk_asm.c")
else
  OUT="$ROOT/build/mayo5-baseline"
  output_name="mayo5-baseline"
  implementation="cva6-ref-portable"
  arithmetic_source="$MAYO/src/arithmetic.c"
  accel_flags=()
  accel_sources=()
fi
TOOLCHAIN_BIN="${TOOLCHAIN_BIN:-$HOME/riscv/toolchain/bin}"
CC="${CC:-$TOOLCHAIN_BIN/riscv64-unknown-elf-gcc}"
NM="${NM:-$TOOLCHAIN_BIN/riscv64-unknown-elf-nm}"
SIZE="${SIZE:-$TOOLCHAIN_BIN/riscv64-unknown-elf-size}"
PICOLIBC="${PICOLIBC:-$("$CC" -print-sysroot)}"
if [ -z "$PICOLIBC" ]; then
  if [ -d /usr/lib/picolibc/riscv64-unknown-elf/lib ]; then
    PICOLIBC=/usr/lib/picolibc/riscv64-unknown-elf
  else
    PICOLIBC="$(cd "$TOOLCHAIN_BIN/.." && pwd)/riscv64-unknown-elf"
  fi
fi

mkdir -p "$OUT"
sources=(
  "$MAYO/src/mayo.c"
  "$MAYO/src/params.c"
  "$arithmetic_source"
  "$MAYO/src/common/aes_c.c"
  "$MAYO/src/common/aes128ctr.c"
  "$MAYO/src/common/fips202.c"
  "$MAYO/src/common/mem.c"
  "$MAYO/src/mayo_5/api.c"
  "$R20/native/mayo_adapter.c"
  "${accel_sources[@]}"
  "$WORK/runtime/e2e_runtime.c"
  "$WORK/runtime/e2e_harness.c"
  "$WORK/runtime/syscalls.c"
  "$WORK/runtime/picolibc_shim.c"
  "$WORK/runtime/crt.S"
)

"$CC" \
  -std=gnu11 -O3 -g -fno-builtin -fno-lto -fno-strict-aliasing \
  -fno-tree-vectorize -fno-tree-slp-vectorize -static -mcmodel=medany \
  -fvisibility=hidden -ffunction-sections -fdata-sections \
  -nostdlib -nostartfiles -march=rv64gc_zba_zbb_zbs_zbc -mabi=lp64d \
  -T"$CVA6/config/gen_from_riscv_config/linker/link.ld" \
  -DMAYO_BUILD_TYPE_REF -DENABLE_PARAMS_DYNAMIC -DTARGET_OTHER \
  -DTARGET_OS_UNIX -Drandombytes=e2e_randombytes \
  "${accel_flags[@]}" \
  -DPROFILE_PARAMETER='"MAYO-5"' \
  -DPROFILE_IMPLEMENTATION="\"$implementation\"" \
  -UMAYO_VARIANT -UMAYO_AVX -UMAYO_GFNI -UMAYO_NEON \
  -DENABLE_PARAMS_DYNAMIC \
  -I"$WORK/runtime" -I"$R20/native" \
  -I"$WORK/source/mayo/src" -I"$WORK/source/mayo/src/generic" \
  -I"$MAYO/src/mayo_5" \
  -I"$MAYO/include" -I"$MAYO/src" -I"$MAYO/src/common" \
  -I"$MAYO/src/generic" \
  -I"$CVA6/verif/tests/custom/common" -I"$CVA6/verif/tests/custom/env" \
  "${sources[@]}" \
  -Wl,--gc-sections -L"$PICOLIBC/lib" -lc -lgcc \
  -o "$OUT/$output_name.elf"

"$SIZE" "$OUT/$output_name.elf"
test -z "$("$NM" -u "$OUT/$output_name.elf")"
echo MAYO5_BUILD_PASS variant="$output_name"
