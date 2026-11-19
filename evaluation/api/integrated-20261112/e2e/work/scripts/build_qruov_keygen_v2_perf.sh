#!/bin/bash
set -euo pipefail
ROOT="${ROOT:-$HOME/riscv/xmpk-e2e-five}"
CVA6="${CVA6_ROOT:-$HOME/riscv/cva6-e2e-five}"
R20="$ROOT/source/r20"
QR="$R20/results/received/upstream/qruov/Reference_Implementation/ref"
UOV="$R20/results/received/upstream/uov/utils"
WORK="$ROOT/work"
OUT="$ROOT/build/qruov-keygen-v2"
CC="${CC:-$HOME/riscv/toolchain/bin/riscv64-unknown-elf-gcc}"
NM="${NM:-$HOME/riscv/toolchain/bin/riscv64-unknown-elf-nm}"
PICOLIBC="${PICOLIBC:-$("$CC" -print-sysroot)}"
PARAM="${PARAM:-1q127L3}"
EQS="${EQS:-2}"
EQ_BASE="${EQ_BASE:-0}"
MODES="${MODES:-0 1 3 5 7 9 11 13 15}"
mkdir -p "$OUT"
for flags in $MODES; do
  elf="$OUT/keygen-v2-${PARAM}-e${EQS}-b${EQ_BASE}-f${flags}.elf"
  "$CC" -std=gnu11 -O3 -g -fno-builtin -fno-lto -fno-strict-aliasing \
    -fno-tree-vectorize -fno-tree-slp-vectorize -static -mcmodel=medany \
    -fvisibility=hidden -ffunction-sections -fdata-sections \
    -nostdlib -nostartfiles -march=rv64gc_zba_zbb_zbs_zbc -mabi=lp64d \
    -T"$CVA6/config/gen_from_riscv_config/linker/link.ld" \
    -DXMPK_BAREMETAL -DXMPK_ACCEL -DQRUOV_PARAM_"$PARAM" -DPRG_IS_AES=1 \
    -DQRUOV_KEYGEN_SHORT_TEST -DTRIAL_FLAGS="$flags" \
    -DTRIAL_EQ_COUNT="$EQS" -DTRIAL_EQ_BASE="$EQ_BASE" \
    -I"$WORK/runtime" -I"$R20/native" -I"$QR" -I"$UOV" \
    -I"$CVA6/verif/tests/custom/common" -I"$CVA6/verif/tests/custom/env" \
    "$WORK/tests/qruov_keygen_v2_perf.c" "$QR/keygen_trial.c" \
    "$QR/qruov.c" "$QR/qrop.c" "$QR/prg.c" \
    "$WORK/runtime/qruov_prim_baremetal.c" \
    "$UOV/fips202.c" "$UOV/aes128_4r_ffs.c" \
    "$WORK/runtime/xmpk_asm.c" "$WORK/runtime/syscalls.c" \
    "$WORK/runtime/picolibc_shim.c" "$WORK/runtime/crt.S" \
    -Wl,--gc-sections -L"$PICOLIBC/lib" -lc -lgcc -o "$elf"
  test -z "$("$NM" -u "$elf")"
  echo "BUILD_PASS $elf"
done
