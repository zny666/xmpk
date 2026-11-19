#!/bin/bash
set -euo pipefail
ROOT="${ROOT:-$(python3 -c 'from pathlib import Path; import sys; print(Path(sys.argv[1]).resolve().parents[2])' "$0")}"
QR="$ROOT/source/r20/results/received/upstream/qruov/Reference_Implementation/ref"
NATIVE="$ROOT/work/tests/native_keygen"
OPENSSL_ROOT="${OPENSSL_ROOT:-/opt/homebrew/opt/openssl@3}"
OUT="${OUT:-$ROOT/work/native-check}"
mkdir -p "$OUT"
clang -O2 -g -fsanitize=address,undefined \
  -DXMPK_ACCEL -DQRUOV_PARAM_1q127L3 -DQRUOV_KEYGEN_SHORT_TEST \
  -DKEYGEN_NATIVE_CHECK -include "$NATIVE/compat.h" \
  -I"$NATIVE" -I"$QR" -I"$OPENSSL_ROOT/include" \
  "$NATIVE/check.c" "$QR/keygen_trial.c" "$QR/qruov.c" \
  "$QR/qrop.c" "$QR/qruov_common.c" "$QR/prg.c" "$QR/prim.c" "$QR/linsys.c" \
  -L"$OPENSSL_ROOT/lib" -lcrypto -o "$OUT/keygen-native-check"
"$OUT/keygen-native-check" "$@"
