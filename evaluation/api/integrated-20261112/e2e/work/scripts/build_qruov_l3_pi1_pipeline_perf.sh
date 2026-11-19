#!/bin/bash
set -euo pipefail

ROOT="${ROOT:-$HOME/riscv/xmpk-e2e-five}"
CVA6="${CVA6_ROOT:-$HOME/riscv/cva6-e2e-five}"
R20="$ROOT/source/r20"
QR="$R20/results/received/upstream/qruov/Reference_Implementation/ref"
UOV_UTILS="$R20/results/received/upstream/uov/utils"
WORK="$ROOT/work"
OUT="$ROOT/build/qruov-l3-pi1-pipeline-perf"
TOOLCHAIN_BIN="${TOOLCHAIN_BIN:-$HOME/riscv/toolchain/bin}"
CC="${CC:-$TOOLCHAIN_BIN/riscv64-unknown-elf-gcc}"
NM="${NM:-$TOOLCHAIN_BIN/riscv64-unknown-elf-nm}"
SIZE="${SIZE:-$TOOLCHAIN_BIN/riscv64-unknown-elf-size}"
PICOLIBC="${PICOLIBC:-$("$CC" -print-sysroot)}"

mkdir -p "$OUT"
sources=(
  "$WORK/tests/qruov_l3_pi1_pipeline_perf.c"
  "$QR/qrop.c"
  "$QR/prg.c"
  "$WORK/runtime/qruov_prim_baremetal.c"
  "$UOV_UTILS/fips202.c"
  "$UOV_UTILS/aes128_4r_ffs.c"
  "$WORK/runtime/xmpk_asm.c"
  "$WORK/runtime/syscalls.c"
  "$WORK/runtime/picolibc_shim.c"
  "$WORK/runtime/crt.S"
)

for mode in old striped; do
  perf_mode=0
  if [ "$mode" = striped ]; then
    perf_mode=1
  fi
  "$CC" \
    -std=gnu11 -O3 -g -fno-builtin -fno-lto -fno-strict-aliasing \
    -fno-tree-vectorize -fno-tree-slp-vectorize -static -mcmodel=medany \
    -fvisibility=hidden -ffunction-sections -fdata-sections \
    -nostdlib -nostartfiles -march=rv64gc_zba_zbb_zbs_zbc -mabi=lp64d \
    -T"$CVA6/config/gen_from_riscv_config/linker/link.ld" \
    -DXMPK_BAREMETAL -DXMPK_ACCEL -DQRUOV_PARAM_1q127L3 -DPRG_IS_AES=1 \
    -DPERF_MODE="$perf_mode" \
    -I"$WORK/runtime" -I"$R20/native" -I"$QR" -I"$UOV_UTILS" \
    -I"$CVA6/verif/tests/custom/common" -I"$CVA6/verif/tests/custom/env" \
    "${sources[@]}" \
    -Wl,--gc-sections -L"$PICOLIBC/lib" -lc -lgcc \
    -o "$OUT/qruov-l3-pi1-$mode.elf"
  "$SIZE" "$OUT/qruov-l3-pi1-$mode.elf"
  test -z "$("$NM" -u "$OUT/qruov-l3-pi1-$mode.elf")"
done

echo QRUOV_L3_PI1_PIPELINE_BUILD_PASS
