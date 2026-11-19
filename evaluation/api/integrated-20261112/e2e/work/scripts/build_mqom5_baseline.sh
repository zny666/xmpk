#!/bin/bash
set -euo pipefail

ROOT="${ROOT:-$HOME/riscv/xmpk-e2e-five}"
CVA6="${CVA6_ROOT:-$HOME/riscv/cva6-e2e-five}"
R20="$ROOT/source/r20"
MQOM="$R20/results/received/upstream/mqom/Reference_Implementation/mqom3_cat5_gf16_fast_ct"
WORK="$ROOT/work"
ACCEL="${ACCEL:-0}"
if [ "$ACCEL" = 1 ]; then
  OUT="$ROOT/build/mqom5-xmpk"
  output_name="mqom5-xmpk"
  implementation="cva6-xmpk-gf256-qgemm"
  accel_flags=(-DXMPK_ACCEL)
  accel_sources=("$WORK/runtime/xmpk_asm.c")
else
  OUT="$ROOT/build/mqom5-baseline"
  output_name="mqom5-baseline"
  implementation="cva6-reference-C-portable"
  accel_flags=()
  accel_sources=()
fi
TOOLCHAIN_BIN="${TOOLCHAIN_BIN:-$HOME/riscv/toolchain/bin}"
CC="${CC:-$TOOLCHAIN_BIN/riscv64-unknown-elf-gcc}"
NM="${NM:-$TOOLCHAIN_BIN/riscv64-unknown-elf-nm}"
SIZE="${SIZE:-$TOOLCHAIN_BIN/riscv64-unknown-elf-size}"
PICOLIBC="${PICOLIBC:-$("$CC" -print-sysroot)}"
if [ -z "$PICOLIBC" ]; then
  if [ -d /usr/lib/picolibc/riscv64-unknown-elf/lib ]; then
    PICOLIBC=/usr/lib/picolibc/riscv64-unknown-elf
  else
    PICOLIBC="$(cd "$TOOLCHAIN_BIN/.." && pwd)/riscv64-unknown-elf"
  fi
fi

mkdir -p "$OUT"

mapfile -t upstream_sources < <(python3 - "$R20" <<'PY'
import json
import sys
from pathlib import Path

root = Path(sys.argv[1])
manifest = root / "results/received/build/reference-20260915T125215-028512Z/manifest.json"
data = json.loads(manifest.read_text())
for entry in data["entries"]:
    if entry["parameter"] == "mqom3_cat5_gf16_fast_ct":
        for source in entry["sources"]:
            if source.startswith("upstream/"):
                print(root / "results/received" / source)
        break
else:
    raise SystemExit("MQOM Cat5 source manifest missing")
PY
)

sources=(
  "${upstream_sources[@]}"
  "$R20/native/mqom_adapter.c"
  "${accel_sources[@]}"
  "$WORK/runtime/e2e_runtime.c"
  "$WORK/runtime/e2e_harness.c"
  "$WORK/runtime/syscalls.c"
  "$WORK/runtime/picolibc_shim.c"
  "$WORK/runtime/crt.S"
)

"$CC" \
  -std=gnu11 -O3 -g -fno-builtin -fno-lto -fno-strict-aliasing \
  -fno-tree-vectorize -fno-tree-slp-vectorize -static -mcmodel=medany \
  -fvisibility=hidden -ffunction-sections -fdata-sections \
  -nostdlib -nostartfiles -march=rv64gc_zba_zbb_zbs_zbc -mabi=lp64d \
  -T"$CVA6/config/gen_from_riscv_config/linker/link.ld" \
  -DFIELDS_REF -DRIJNDAEL_BITSLICE -DNO_GFNI \
  -DNO_RIJNDAEL_VAES_GFNI_KEYSCHED -DNO_MISALIGNED_ACCESSES \
  -DKECCAK_PLATFORM=opt64 -DNO_ALLOC_PROBE -DNDEBUG \
  -DUSE_PIOP_CACHE -DUSE_XOF_X4 -DAES_GRINDING_X4 -DUSE_ENC_X8 \
  -DLARGE_TREE_BFS -DLARGE_TREE_BFS_X8 \
  -DMQOM3_PARAM_SECURITY=256 -DMQOM3_PARAM_BASE_FIELD=4 \
  -DMQOM3_PARAM_TRADEOFF=0 -DMQOM3_PARAM_OT_VARIANT=0 \
  -DREF_PARAMETER='"mqom3_cat5_gf16_fast_ct"' \
  -DREF_IMPLEMENTATION="\"$implementation\"" \
  "${accel_flags[@]}" \
  -Drandombytes=e2e_randombytes \
  -I"$WORK/runtime" -I"$R20/native" -I"$MQOM" -I"$MQOM/sha3" \
  -I"$MQOM/sha3/opt64" -I"$MQOM/rijndael" -I"$MQOM/ggm_tree" \
  -I"$MQOM/blc" -I"$MQOM/piop" -I"$MQOM/fields" \
  -I"$MQOM/fields_bitsliced" \
  -I"$CVA6/verif/tests/custom/common" -I"$CVA6/verif/tests/custom/env" \
  "${sources[@]}" \
  -Wl,--gc-sections -L"$PICOLIBC/lib" -lc -lm -lgcc \
  -o "$OUT/$output_name.elf"

"$SIZE" "$OUT/$output_name.elf"
test -z "$("$NM" -u "$OUT/$output_name.elf")"
echo MQOM5_BUILD_PASS variant="$output_name"
