#!/bin/bash
set -euo pipefail
ROOT="${ROOT:-/artifact/xmpk-final/e2e}"
QR="$ROOT/source/r20/results/received/upstream/qruov/Reference_Implementation/ref"
NATIVE="$ROOT/work/tests/native_keygen"
OPENSSL_ROOT="${OPENSSL_ROOT:-/opt/homebrew/opt/openssl@3}"
OUT="$ROOT/work/native-check"
mkdir -p "$OUT"
COMMON=(-O2 -g -fsanitize=address,undefined -DXMPK_ACCEL -DQRUOV_PARAM_1q127L3
  -DQRUOV_KEYGEN_SHORT_TEST -DKEYGEN_NATIVE_CHECK -include "$NATIVE/compat.h"
  -I"$NATIVE" -I"$QR" -I"$OPENSSL_ROOT/include")
clang "${COMMON[@]}" -Dmain=unused_keygen_main -c "$NATIVE/check.c" -o "$OUT/emu.o"
for spec in 0:2 52:2 0:54; do
  base="${spec%:*}"
  count="${spec#*:}"
  clang "${COMMON[@]}" -DTRIAL_EQ_BASE="$base" -DTRIAL_EQ_COUNT="$count" \
    "$ROOT/work/tests/qruov_signverify_short.c" "$OUT/emu.o" \
    "$QR/keygen_trial.c" "$QR/qruov.c" "$QR/qrop.c" \
    "$QR/qruov_common.c" "$QR/prg.c" "$QR/prim.c" "$QR/linsys.c" \
    -L"$OPENSSL_ROOT/lib" -lcrypto -o "$OUT/signverify-native-$base-$count"
  "$OUT/signverify-native-$base-$count" "$OUT/golden-signverify-b$base-e$count.bin"
done
