#!/bin/bash
set -euo pipefail
ROOT="${ROOT:-$HOME/riscv/xmpk-e2e-five}"
CVA6="${CVA6_ROOT:-$HOME/riscv/cva6-e2e-five}"
QR="$ROOT/source/r20/results/received/upstream/qruov/Reference_Implementation/ref"
UOV="$ROOT/source/r20/results/received/upstream/uov/utils"
WORK="$ROOT/work"
CC="${CC:-$HOME/riscv/toolchain/bin/riscv64-unknown-elf-gcc}"
PICOLIBC="${PICOLIBC:-/usr/lib/picolibc/riscv64-unknown-elf}"
OUT="$ROOT/build/signverify-short"
mkdir -p "$OUT"
for flags in ${MODES:-0 1 2 7}; do
  elf="$OUT/signverify-e${EQS:-2}-b${EQ_BASE:-0}-f${flags}.elf"
  "$CC" -std=gnu11 -O3 -g -fno-builtin -fno-lto -fno-strict-aliasing \
    -fno-tree-vectorize -fno-tree-slp-vectorize -static -mcmodel=medany \
    -fvisibility=hidden -ffunction-sections -fdata-sections \
    -nostdlib -nostartfiles -march=rv64gc_zba_zbb_zbs_zbc -mabi=lp64d \
    -T"$CVA6/config/gen_from_riscv_config/linker/link.ld" \
    -DXMPK_BAREMETAL -DXMPK_ACCEL -DQRUOV_PARAM_1q127L3 -DPRG_IS_AES=1 \
    -DTRIAL_FLAGS="$flags" -DTRIAL_EQ_COUNT="${EQS:-2}" -DTRIAL_EQ_BASE="${EQ_BASE:-0}" \
    -I"$WORK/runtime" -I"$ROOT/source/r20/native" -I"$QR" -I"$UOV" \
    -I"$CVA6/verif/tests/custom/common" -I"$CVA6/verif/tests/custom/env" \
    "$WORK/tests/qruov_signverify_short.c" "$QR/qrop.c" "$QR/prg.c" \
    "$WORK/runtime/qruov_prim_baremetal.c" "$UOV/fips202.c" "$UOV/aes128_4r_ffs.c" \
    "$WORK/runtime/xmpk_asm.c" "$WORK/runtime/syscalls.c" \
    "$WORK/runtime/picolibc_shim.c" "$WORK/runtime/crt.S" \
    -Wl,--gc-sections -L"$PICOLIBC/lib" -lc -lgcc -o "$elf"
  test -z "$("$HOME/riscv/toolchain/bin/riscv64-unknown-elf-nm" -u "$elf")"
  echo "BUILD_PASS $elf"
done
