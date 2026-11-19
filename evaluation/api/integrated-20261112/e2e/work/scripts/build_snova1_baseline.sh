#!/bin/bash
set -euo pipefail

ROOT="${ROOT:-$HOME/riscv/xmpk-e2e-five}"
CVA6="${CVA6_ROOT:-$HOME/riscv/cva6-e2e-five}"
R20="$ROOT/source/r20"
SNOVA="$R20/results/received/upstream/snova/Reference_Implementation/SNOVA_I_B_AES"
WORK="$ROOT/work"
ACCEL="${ACCEL:-0}"
if [ "$ACCEL" = 1 ]; then
  SNOVA_ACCEL="${SNOVA_ACCEL:-qgemm}"
  if [ "$SNOVA_ACCEL" = batch ]; then
    OUT="$ROOT/build/snova1-batch"
    output_name="snova1-batch"
    implementation="cva6-xmpk-gf16-linxfm-batch"
    accel_flags=(-DXMPK_ACCEL -DXMPK_SNOVA_LINXFM)
  elif [ "$SNOVA_ACCEL" = linxfm ]; then
    OUT="$ROOT/build/snova1-linxfm"
    output_name="snova1-linxfm"
    implementation="cva6-xmpk-gf16-linxfm"
    accel_flags=(-DXMPK_ACCEL -DXMPK_SNOVA_LINXFM)
  else
    OUT="$ROOT/build/snova1-xmpk"
    output_name="snova1-xmpk"
    implementation="cva6-xmpk-gf16-qgemm"
    accel_flags=(-DXMPK_ACCEL)
  fi
  snova_source="$WORK/source/snova/snova_ref.c"
  accel_sources=("$WORK/runtime/xmpk_asm.c")
else
  OUT="$ROOT/build/snova1-baseline"
  output_name="snova1-baseline"
  implementation="cva6-ref-portable"
  snova_source="$SNOVA/snova_ref.c"
  accel_flags=()
  accel_sources=()
fi
TOOLCHAIN_BIN="${TOOLCHAIN_BIN:-$HOME/riscv/toolchain/bin}"
CC="${CC:-$TOOLCHAIN_BIN/riscv64-unknown-elf-gcc}"
NM="${NM:-$TOOLCHAIN_BIN/riscv64-unknown-elf-nm}"
SIZE="${SIZE:-$TOOLCHAIN_BIN/riscv64-unknown-elf-size}"
PICOLIBC="${PICOLIBC:-$("$CC" -print-sysroot)}"
if [ -z "$PICOLIBC" ]; then
  PICOLIBC="$(cd "$TOOLCHAIN_BIN/.." && pwd)/riscv64-unknown-elf"
fi

mkdir -p "$OUT"
sources=(
  "$SNOVA/sign.c"
  "$snova_source"
  "$SNOVA/symmetric_ref.c"
  "$SNOVA/aes.c"
  "$R20/native/snova_adapter.c"
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
  -DSNOVA_REF=1 -Drandombytes=e2e_randombytes \
  "${accel_flags[@]}" \
  -DPROFILE_PARAMETER='"SNOVA_I_B_AES"' \
  -DPROFILE_IMPLEMENTATION="\"$implementation\"" \
  -I"$WORK/runtime" -I"$R20/native" -I"$SNOVA" \
  -I"$CVA6/verif/tests/custom/common" -I"$CVA6/verif/tests/custom/env" \
  "${sources[@]}" \
  -Wl,--gc-sections -L"$PICOLIBC/lib" -lc -lgcc \
  -o "$OUT/$output_name.elf"

"$SIZE" "$OUT/$output_name.elf"
test -z "$("$NM" -u "$OUT/$output_name.elf")"
echo SNOVA1_BUILD_PASS variant="$output_name"
