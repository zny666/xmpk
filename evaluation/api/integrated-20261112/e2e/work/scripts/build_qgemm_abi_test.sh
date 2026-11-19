#!/bin/bash
set -euo pipefail

ROOT="${ROOT:-$HOME/riscv/xmpk-e2e-five}"
CVA6="${CVA6_ROOT:-$HOME/riscv/cva6-e2e-five}"
WORK="$ROOT/work"
OUT="$ROOT/build/xmpk-qgemm-abi-test"
TOOLCHAIN_BIN="${TOOLCHAIN_BIN:-$HOME/riscv/toolchain/bin}"
CC="${CC:-$TOOLCHAIN_BIN/riscv64-unknown-elf-gcc}"
NM="${NM:-$TOOLCHAIN_BIN/riscv64-unknown-elf-nm}"
PICOLIBC="${PICOLIBC:-$("$CC" -print-sysroot)}"
if [ -z "$PICOLIBC" ]; then
  PICOLIBC="$(cd "$TOOLCHAIN_BIN/.." && pwd)/riscv64-unknown-elf"
fi

mkdir -p "$OUT"
"$CC" \
  -std=gnu11 -O3 -g -fno-builtin -fno-lto -static -mcmodel=medany \
  -nostdlib -nostartfiles -march=rv64gc_zba_zbb_zbs_zbc -mabi=lp64d \
  -T"$CVA6/config/gen_from_riscv_config/linker/link.ld" \
  -I"$CVA6/verif/tests/custom/common" \
  -I"$CVA6/verif/tests/custom/env" \
  "$WORK/tests/cva6/xmpk_qgemm_test.c" \
  "$WORK/runtime/syscalls.c" \
  "$WORK/runtime/picolibc_shim.c" \
  "$WORK/runtime/crt.S" \
  -Wl,--gc-sections -L"$PICOLIBC/lib" -lc -lgcc \
  -o "$OUT/xmpk-qgemm-abi-test.elf"

test -z "$("$NM" -u "$OUT/xmpk-qgemm-abi-test.elf")"
echo XMPK_QGEMM_ABI_TEST_BUILD_PASS
