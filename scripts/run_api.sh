#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0 WITH SHL-2.0
set -euo pipefail
if [[ $# != 5 ]]; then
  echo "usage: bash scripts/run_api.sh MODEL ELF MAX_CYCLES NEW_OUTPUT_DIR HTIF_SERVICE" >&2
  exit 2
fi
repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
[[ ! -e "$4" ]] || { echo "Choose a new output directory" >&2; exit 2; }
export HARNESS="$1"
export HTIF_SERVICE="$5"
bash "$repo/evaluation/api/integrated-20261112/e2e/work/scripts/run_verilator_htif.sh" \
  "$2" "$3" "$4" api
