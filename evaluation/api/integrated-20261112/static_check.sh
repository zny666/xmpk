#!/usr/bin/env bash
set -euo pipefail
ARCHIVE_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
QR="$ARCHIVE_ROOT/e2e/source/r20/results/received/upstream/qruov/Reference_Implementation/ref"
RUNTIME="$ARCHIVE_ROOT/e2e/work/runtime"
CC="${CC:-$HOME/riscv/toolchain/bin/riscv64-unknown-elf-gcc}"
if [ -d /usr/lib/picolibc/riscv64-unknown-elf/include ]; then
  export C_INCLUDE_PATH="/usr/lib/picolibc/riscv64-unknown-elf/include${C_INCLUDE_PATH:+:$C_INCLUDE_PATH}"
fi
flags=(-std=gnu11 -fsyntax-only -march=rv64gc_zba_zbb_zbs_zbc -mabi=lp64d
       -DXMPK_BAREMETAL -DPRG_IS_AES=1 -I"$RUNTIME" -I"$QR"
       -I"$ARCHIVE_ROOT/e2e/source/r20/results/received/upstream/uov/utils"
       -I"$ARCHIVE_ROOT/e2e/source/r20/results/received/upstream/uov/src")
"$CC" "${flags[@]}" -Wall -Wextra -Werror "$RUNTIME/xmpk_asm.c"
for param in 1q127L3 5q127L3 1q127L10; do
  for accel in 0 1; do
    defs=("-DQRUOV_PARAM_$param")
    if [ "$accel" = 1 ]; then defs+=(-DXMPK_ACCEL); fi
    "$CC" "${flags[@]}" "${defs[@]}" -Wall -Wextra -Werror \
      "$QR/qruov.c" "$QR/qruov_common.c" "$QR/xmpk_refined.c"
    echo "$param accel=$accel SYNTAX_PASS"
  done
done
for script in "$ARCHIVE_ROOT"/{build_only,static_check}.sh \
              "$ARCHIVE_ROOT"/e2e/work/scripts/build_*_baseline.sh; do
  bash -n "$script"
done
echo "STATIC_ONLY_COMPLETE: runtime + QR L1/L5 + degree-10 fallback; no execution"
