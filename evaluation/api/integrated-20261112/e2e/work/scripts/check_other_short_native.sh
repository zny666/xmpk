#!/bin/bash
set -euo pipefail
ROOT="${ROOT:-/artifact/xmpk-final/e2e}"
OUT="$ROOT/work/other-short-native"
mkdir -p "$OUT"
for algo in 1 2 3 4; do
  modes="0 1 3"
  if [ "$algo" = 1 ]; then modes="0 1 2 3 4"; fi
  if [ "$algo" = 2 ]; then modes="0 1"; fi
  for flags in $modes; do
    clang -O2 -g -fsanitize=address,undefined -DNATIVE_CHECK \
      -DALGORITHM="$algo" -DFLAGS="$flags" \
      "$ROOT/work/tests/other_algorithms_short.c" -o "$OUT/a$algo-f$flags"
    "$OUT/a$algo-f$flags" "$OUT/a$algo-f$flags.bin"
    cmp "$OUT/a$algo-f0.bin" "$OUT/a$algo-f$flags.bin"
  done
done
