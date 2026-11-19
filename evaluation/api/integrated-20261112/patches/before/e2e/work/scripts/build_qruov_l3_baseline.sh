#!/bin/bash
set -euo pipefail

ROOT="${ROOT:-$HOME/riscv/xmpk-e2e-five}"
CVA6="${CVA6_ROOT:-$HOME/riscv/cva6-e2e-five}"
R20="$ROOT/source/r20"
QR="$R20/results/received/upstream/qruov/Reference_Implementation/ref"
UOV_UTILS="$R20/results/received/upstream/uov/utils"
WORK="$ROOT/work"
ACCEL="${ACCEL:-0}"
if [ "$ACCEL" = 1 ]; then
  OUT="$ROOT/build/qruov-l3-xmpk"
  output_name="qruov-l3-xmpk"
  accel_flags=(-DXMPK_ACCEL)
else
  OUT="$ROOT/build/qruov-l3-baseline"
  output_name="qruov-l3-baseline"
  accel_flags=()
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
  "$QR/sign.c"
  "$QR/qruov_common.c"
  "$QR/qrop.c"
  "$QR/prg.c"
  "$WORK/runtime/qruov_prim_baremetal.c"
  "$QR/linsys.c"
  "$QR/qruov.c"
  "$UOV_UTILS/fips202.c"
  "$UOV_UTILS/aes128_4r_ffs.c"
  "$WORK/runtime/qruov_baremetal_adapter.c"
  "$WORK/runtime/e2e_runtime.c"
  "$WORK/runtime/e2e_harness.c"
  "$WORK/runtime/syscalls.c"
  "$WORK/runtime/picolibc_shim.c"
  "$WORK/runtime/crt.S"
)
if [ "$ACCEL" = 1 ]; then
  sources+=("$WORK/runtime/xmpk_asm.c")
fi

"$CC" \
  -std=gnu11 -O3 -g -fno-builtin -fno-lto -fno-strict-aliasing \
  -fno-tree-vectorize -fno-tree-slp-vectorize -static -mcmodel=medany \
  -fvisibility=hidden -ffunction-sections -fdata-sections \
  -nostdlib -nostartfiles -march=rv64gc_zba_zbb_zbs_zbc -mabi=lp64d \
  -T"$CVA6/config/gen_from_riscv_config/linker/link.ld" \
  -DXMPK_BAREMETAL -DQRUOV_PARAM_1q127L3 -DPRG_IS_AES=1 \
  "${accel_flags[@]}" \
  -Drandombytes=qruov_randombytes \
  -I"$WORK/runtime" -I"$R20/native" -I"$QR" -I"$UOV_UTILS" \
  -I"$CVA6/verif/tests/custom/common" -I"$CVA6/verif/tests/custom/env" \
  "${sources[@]}" \
  -Wl,--gc-sections -L"$PICOLIBC/lib" -lc -lgcc \
  -o "$OUT/$output_name.elf"

"$SIZE" "$OUT/$output_name.elf"
test -z "$("$NM" -u "$OUT/$output_name.elf")"
echo QRUOV_L3_BUILD_PASS variant="$output_name"
