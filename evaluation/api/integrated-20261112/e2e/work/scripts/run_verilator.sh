#!/bin/bash
set -euo pipefail

if [ "$#" -lt 3 ] || [ "$#" -gt 4 ]; then
  echo "usage: $0 ELF MAX_CYCLES OUTPUT_DIR [TAG]" >&2
  exit 2
fi

ELF="$(realpath "$1")"
MAX_CYCLES="$2"
OUT="$(realpath -m "$3")"
TAG="${4:-$(basename "${ELF%.elf}")}"
CVA6="${CVA6_ROOT:-$HOME/riscv/cva6-e2e-five}"
HARNESS="${HARNESS:-$CVA6/work-ver/Variane_testharness}"

progress() {
  printf '[%s] E2E_SIM tag=%s %s\n' \
    "$(date --iso-8601=seconds)" "$TAG" "$*"
}

mkdir -p "$OUT"
test -x "$HARNESS"
test -f "$ELF"

progress "START elf=$ELF max_cycles=$MAX_CYCLES harness=$HARNESS"
"$HARNESS" "$ELF" \
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
  >"$OUT/$TAG.log" 2>&1 &
sim_pid=$!
last=""
while kill -0 "$sim_pid" 2>/dev/null; do
  current=$(grep -E 'E2E_BEGIN|E2E_METRIC|E2E_SUCCESS' \
    "$OUT/$TAG.log" 2>/dev/null | tail -1 || true)
  if [ -n "$current" ] && [ "$current" != "$last" ]; then
    progress "PROGRESS $current"
    last="$current"
  fi
  sleep 60
done
wait "$sim_pid"

if ! grep -q 'E2E_SUCCESS' "$OUT/$TAG.log"; then
  progress "FAIL reason=E2E_SUCCESS_missing log=$OUT/$TAG.log"
  exit 1
fi
progress "PASS log=$OUT/$TAG.log"
