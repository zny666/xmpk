#!/bin/bash
set -euo pipefail

if [ "$#" -ne 4 ]; then
  echo "usage: $0 ELF MAX_CYCLES OUTPUT_DIR TAG" >&2
  exit 2
fi

ELF="$(realpath "$1")"
MAX_CYCLES="$2"
OUT="$(realpath -m "$3")"
TAG="$4"
ROOT="${XMPK_E2E_ROOT:-$HOME/riscv/xmpk-e2e-five}"
STATE_DIR="$ROOT/results/supervisor"
LOG="$OUT/$TAG.log"

mkdir -p "$OUT" "$STATE_DIR"
exec 9>"$STATE_DIR/$TAG.lock"
flock -n 9 || {
  echo "supervisor already active for $TAG"
  exit 0
}

state() {
  printf '%s %s\n' "$(date --iso-8601=seconds)" "$*" \
    >>"$STATE_DIR/$TAG.state"
}

has_success() {
  grep -q 'E2E_SUCCESS' "$LOG" 2>/dev/null
}

find_sim_pid() {
  pgrep -u "$(id -u)" -f \
    "Variane_testharness .*$(basename "$ELF")" | head -n 1
}

if has_success; then
  state "already-complete"
  exit 0
fi

state "watching-existing"
while sim_pid="$(find_sim_pid)"; do
  state "running pid=$sim_pid"
  sleep 60
done

if has_success; then
  state "completed-existing"
  exit 0
fi

if [ -s "$LOG" ]; then
  interrupted="$LOG.interrupted.$(date +%Y%m%dT%H%M%S)"
  mv "$LOG" "$interrupted"
  state "preserved-incomplete-log path=$interrupted"
fi

state "starting-detached-recovery"
if [ -x "$HOME/riscv/cva6-e2e-five/work-ver/Variane_testharness" ]; then
  CVA6_ROOT="$HOME/riscv/cva6-e2e-five" \
    /bin/bash "$ROOT/work/scripts/run_verilator.sh" \
    "$ELF" "$MAX_CYCLES" "$OUT" "$TAG"
else
  bundle="$ROOT/portable-harness"
  "$bundle/ld-linux-x86-64.so.2" \
    --library-path "$bundle" \
    "$bundle/Variane_testharness" "$ELF" \
    +debug_disable=1 \
    +rvfi_trace_disable=1 \
    +time_out="$MAX_CYCLES" \
    ++"$ELF" \
    +elf_file="$ELF" \
    +core_name=cv64a6_imafdc_sv39 \
    +tohost_addr=0000000080001000 \
    +signature="$OUT/$TAG.signature" \
    +UVM_TESTNAME=uvmt_cva6_firmware_test_c \
    +report_file="$OUT/$TAG.yaml" \
    >"$LOG" 2>&1
fi

if has_success; then
  state "completed-recovery"
  exit 0
fi

state "failed-recovery"
exit 1
