#!/bin/bash
set -euo pipefail
ROOT="$(dirname "$(dirname "$(realpath "$0")")")"
CC="$HOME/riscv/toolchain/bin/riscv64-unknown-elf-gcc"
COMMON="$HOME/riscv/cva6-p8-fields/verif/tests/custom/common"
PICOLIBC=/usr/lib/picolibc/riscv64-unknown-elf
mkdir -p "$ROOT/kernel-elf"
compile() {
  local name="$1"
  shift
  "$CC" -O2 -fno-builtin -static -mcmodel=medany -fvisibility=hidden \
    -nostdlib -nostartfiles -march=rv64gc -mabi=lp64d \
    -I"$COMMON" -I"$HOME/riscv/cva6-p8-fields/verif/tests/custom/env" \
    -I"$PICOLIBC/include" "$@" \
    "$ROOT/tests/xmpk_p8_field_perf.c" "$COMMON/syscalls.c" \
    "$ROOT/tests/picolibc_shim.c" "$COMMON/crt.S" \
    -T"$HOME/riscv/cva6-p8-fields/config/gen_from_riscv_config/linker/link.ld" \
    -L"$PICOLIBC/lib/rv64imafdc/lp64d" -lc -lgcc \
    -o "$ROOT/kernel-elf/$name.elf"
}
for n in 45 72 96; do
  compile "solve-$n-hardware" -DPERF_SOLVE256 -DN="$n"
  compile "solve-$n-scalar" -DPERF_SOLVE256 -DN="$n" -DPERF_SCALAR
done
for spec in "3 52" "10 54"; do
  read -r degree dimension <<< "$spec"
  for mode in hardware scalar layout; do
    extra=()
    if [[ "$mode" == scalar ]]; then extra+=(-DPERF_SCALAR); fi
    if [[ "$mode" == layout ]]; then extra+=(-DPERF_LAYOUT); fi
    compile "ext-$degree-$mode" -DPERF_EXTGEMM -DL="$degree" \
      -DM="$dimension" -DK="$dimension" -DNCOL=1 "${extra[@]}"
  done
done
sha256sum "$ROOT/kernel-elf/"*.elf >"$ROOT/kernel-elf/SHA256SUMS"
echo FINAL_KERNEL_ELF_COMPILE_PASS
