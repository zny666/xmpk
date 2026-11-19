#!/bin/bash
set -euo pipefail

ROOT="${ROOT:-$HOME/riscv/xmpk-e2e-five}"
CVA6="${CVA6_ROOT:-$HOME/riscv/cva6-e2e-five}"
R20="$ROOT/source/r20"
UOV="$R20/results/received/upstream/uov"
WORK="$ROOT/work"
ACCEL="${ACCEL:-0}"
if [ "$ACCEL" = 1 ]; then
  OUT="$ROOT/build/uov-is-xmpk"
  output_name="uov-is-xmpk"
  implementation="cva6-xmpk-qgemm-solveq"
  accel_flags=(-DXMPK_ACCEL)
  accel_sources=("$WORK/runtime/xmpk_asm.c")
else
  OUT="$ROOT/build/uov-is-baseline"
  output_name="uov-is-baseline"
  implementation="cva6-ref-portable"
  accel_flags=()
  accel_sources=()
fi
TOOLCHAIN_BIN="${TOOLCHAIN_BIN:-$HOME/riscv/toolchain/bin}"
CC="${CC:-$TOOLCHAIN_BIN/riscv64-unknown-elf-gcc}"
NM="${NM:-$TOOLCHAIN_BIN/riscv64-unknown-elf-nm}"
SIZE="${SIZE:-$TOOLCHAIN_BIN/riscv64-unknown-elf-size}"
LINKER="$CVA6/config/gen_from_riscv_config/linker/link.ld"
COMMON="$CVA6/verif/tests/custom/common"
ENV="$CVA6/verif/tests/custom/env"
PICOLIBC="${PICOLIBC:-$("$CC" -print-sysroot)}"
if [ -z "$PICOLIBC" ]; then
  PICOLIBC="$(cd "$TOOLCHAIN_BIN/.." && pwd)/riscv64-unknown-elf"
fi

mkdir -p "$OUT"

sources=(
  "$UOV/src/blas_matrix.c"
  "$UOV/src/ov.c"
  "$UOV/src/ov_keypair.c"
  "$UOV/src/ov_keypair_computation.c"
  "$UOV/src/ov_publicmap.c"
  "$UOV/src/parallel_matrix_op.c"
  "$UOV/src/sign.c"
  "$UOV/src/ref/blas_matrix_ref.c"
  "$UOV/utils/aes128_4r_ffs.c"
  "$UOV/utils/fips202.c"
  "$UOV/utils/utils_hash.c"
  "$UOV/utils/utils_prng.c"
  "$R20/native/uov_adapter.c"
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
  -nostdlib -nostartfiles -march=rv64gc_zba_zbb_zbs_zbc \
  -mabi=lp64d -T"$LINKER" \
  -DXMPK_BAREMETAL -D_OV16_160_64 -D_OV_CLASSIC \
  "${accel_flags[@]}" \
  -DPROFILE_PARAMETER='"UOV-Is"' \
  -DPROFILE_IMPLEMENTATION="\"$implementation\"" \
  -U_OV256_119_45 -U_OV256_193_72 -U_OV256_259_96 \
  -U_OV_PKC -U_OV_PKC_SKC -U_BLAS_UINT64_ -U_BLAS_SSE_ \
  -U_BLAS_AVX2_ -U_BLAS_GFNI_ -U_BLAS_NEON_ -U_BLAS_M4F_ \
  -U_MUL_WITH_MULTAB_ -U_UTILS_OPENSSL_ -U_UTILS_AESNI_ \
  -U_UTILS_NEONAES_ -U_UTILS_NEONBSAES_ \
  -I"$WORK/runtime" -I"$R20/native" -I"$UOV/src" -I"$UOV/src/ref" \
  -I"$UOV/utils" -I"$COMMON" -I"$ENV" \
  "${sources[@]}" \
  -Wl,--gc-sections -L"$PICOLIBC/lib" -lc -lgcc \
  -o "$OUT/$output_name.elf"

"$SIZE" "$OUT/$output_name.elf"
"$NM" -u "$OUT/$output_name.elf" > "$OUT/unresolved.txt"
test ! -s "$OUT/unresolved.txt"
echo UOV_IS_BUILD_PASS variant="$output_name"
