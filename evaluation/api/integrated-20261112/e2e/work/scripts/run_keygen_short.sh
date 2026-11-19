#!/bin/bash
set -euo pipefail
ELF="$1"
MAX_CYCLES="$2"
OUT="$3"
TAG="$4"
mkdir -p "$OUT"
test -x "$HARNESS"
test -f "$HTIF_SERVICE"
test -f "$ELF"
service_pid=""
sim_pid=""
cleanup() {
  if [ -n "$service_pid" ]; then kill "$service_pid" 2>/dev/null || true; fi
  if [ -n "$sim_pid" ]; then kill "$sim_pid" 2>/dev/null || true; fi
}
trap cleanup EXIT
"$HARNESS" "$ELF" +debug_disable=1 +rvfi_trace_disable=1 \
  +time_out="$MAX_CYCLES" ++"$ELF" +elf_file="$ELF" \
  +core_name=cv64a6_imafdc_sv39 +tohost_addr=0000000080001000 \
  +signature="$OUT/$TAG.signature" +UVM_TESTNAME=uvmt_cva6_firmware_test_c \
  +report_file="$OUT/$TAG.yaml" >"$OUT/$TAG.log" 2>&1 &
sim_pid=$!
root_pointer="$(python3 - "$sim_pid" <<'PY'
import sys, time
pid = int(sys.argv[1])
for _ in range(100):
    candidates = []
    try:
        with open(f"/proc/{pid}/maps") as maps:
            for line in maps:
                fields = line.split()
                lo, hi = [int(x, 16) for x in fields[0].split("-")]
                if fields[1] == "rw-p" and len(fields) == 5 and hi-lo > 0x20000000:
                    candidates.append((hi-lo, lo))
    except FileNotFoundError:
        break
    if candidates:
        print(hex(max(candidates)[1] + 0x100))
        break
    time.sleep(0.05)
else:
    raise SystemExit("root pointer not found")
if not candidates:
    raise SystemExit("simulator exited before HTIF attach")
PY
)"
python3 "$HTIF_SERVICE" "$sim_pid" "$root_pointer" \
  "$OUT/$TAG.guest-output.log" --merge-log "$OUT/$TAG.log" \
  >"$OUT/$TAG.htif-service.log" 2>&1 &
service_pid=$!
wait "$sim_pid"
wait "$service_pid" || true
sim_pid=""
service_pid=""
grep -q "${SUCCESS_MARKER:-KEYGEN_V2_SUCCESS}" "$OUT/$TAG.guest-output.log"
