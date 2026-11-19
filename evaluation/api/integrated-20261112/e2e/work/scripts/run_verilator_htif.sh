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
HARNESS="${HARNESS:-$HOME/riscv/xmpk-e2e-worker-final/bin/Variane_testharness}"
HTIF_SERVICE="${HTIF_SERVICE:-/tmp/service_htif_writes.py}"

mkdir -p "$OUT"
test -x "$HARNESS"
test -f "$ELF"
test -f "$HTIF_SERVICE"

export LD_LIBRARY_PATH="$(dirname "$HARNESS")/../lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"

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

root_pointer="$(
  python3 - "$sim_pid" <<'PY'
import sys
import time

pid = int(sys.argv[1])
for _ in range(100):
    try:
        candidates = []
        with open(f"/proc/{pid}/maps", encoding="ascii") as maps:
            for line in maps:
                fields = line.split()
                lo, hi = (int(value, 16) for value in fields[0].split("-"))
                if fields[1] == "rw-p" and len(fields) == 5 and hi - lo > 0x20000000:
                    candidates.append((hi - lo, lo))
        if candidates:
            print(hex(max(candidates)[1] + 0x100))
            break
    except FileNotFoundError:
        pass
    time.sleep(0.05)
else:
    raise SystemExit("root pointer mapping not found")
PY
)"

python3 "$HTIF_SERVICE" "$sim_pid" "$root_pointer" \
  "$OUT/$TAG.guest-output.log" --merge-log "$OUT/$TAG.log" \
  >"$OUT/$TAG.htif-service.log" 2>&1 &
service_pid=$!

set +e
wait "$sim_pid"
status=$?
set -e
wait "$service_pid" || true

if [ "$status" -ne 0 ]; then
  exit "$status"
fi
grep -q E2E_SUCCESS "$OUT/$TAG.log"
