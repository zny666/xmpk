#!/usr/bin/env bash
set -euo pipefail
ARCHIVE_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
export ROOT="$ARCHIVE_ROOT/e2e"
export ACCEL=1
# Uses an existing RISC-V toolchain and CVA6 linker/headers. Never runs an ELF.
export CVA6_ROOT="${CVA6_ROOT:-$ARCHIVE_ROOT/build-support}"
# Debian GCC ships separately from picolibc. Prefer the installed headers and
# rv64gc/lp64d library when present; leave other existing SDKs configurable.
if [ -d /usr/lib/picolibc/riscv64-unknown-elf/include ]; then
  export PICOLIBC="${PICOLIBC:-/usr/lib/picolibc/riscv64-unknown-elf}"
  export C_INCLUDE_PATH="$PICOLIBC/include${C_INCLUDE_PATH:+:$C_INCLUDE_PATH}"
  export LIBRARY_PATH="$PICOLIBC/lib/rv64imafdc/lp64d${LIBRARY_PATH:+:$LIBRARY_PATH}"
fi
mkdir -p "$ARCHIVE_ROOT/validation"
variants=(mayo1 mayo5 mqom1 mqom5 snova1 snova5 uov_is uov_v qruov_l3 qruov_l5)
for variant in "${variants[@]}"; do
  bash "$ROOT/work/scripts/build_${variant}_baseline.sh" \
    > "$ARCHIVE_ROOT/validation/build_${variant}.log" 2>&1
  echo "$variant COMPILE_LINK_PASS"
done
echo "BUILD_ONLY_COMPLETE: 10 variants; no algorithm or simulator executed"
