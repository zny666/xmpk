#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0 WITH SHL-2.0
set -euo pipefail
if (( BASH_VERSINFO[0] < 4 || (BASH_VERSINFO[0] == 4 && BASH_VERSINFO[1] < 4) )); then
  echo "API builds require Linux with Bash 4.4 or newer." >&2
  exit 2
fi
if [[ $# != 2 || ! "$2" =~ ^[01]$ ]]; then
  echo "usage: bash scripts/build_api.sh VARIANT ACCEL(0|1)" >&2
  exit 2
fi
case "$1" in
  mayo1|mayo5|mqom1|mqom5|snova1|snova5|uov_is|uov_v|qruov_l3|qruov_l5) ;;
  *) echo "Unknown API variant: $1" >&2; exit 2 ;;
esac
repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
archive="$repo/evaluation/api/integrated-20261112"
export ROOT="$archive/e2e"
export CVA6_ROOT="${CVA6_ROOT:-$archive/build-support}"
export ACCEL="$2"
if [[ -d /usr/lib/picolibc/riscv64-unknown-elf/include ]]; then
  export PICOLIBC="${PICOLIBC:-/usr/lib/picolibc/riscv64-unknown-elf}"
  export C_INCLUDE_PATH="$PICOLIBC/include${C_INCLUDE_PATH:+:$C_INCLUDE_PATH}"
  export LIBRARY_PATH="$PICOLIBC/lib/rv64imafdc/lp64d${LIBRARY_PATH:+:$LIBRARY_PATH}"
fi
bash "$ROOT/work/scripts/build_${1}_baseline.sh"
