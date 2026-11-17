#!/usr/bin/env bash
set -euo pipefail
: "${CVA6_REPO_DIR:?Set CVA6_REPO_DIR to the integrated checkout}"
: "${SPIKE_INSTALL_DIR:?Set SPIKE_INSTALL_DIR to the installed Spike prefix}"
: "${VERILATOR_INSTALL_DIR:?Set VERILATOR_INSTALL_DIR to the Verilator prefix}"
export TARGET_CFG=cv64a6_imafdc_sv39
export HPDCACHE_DIR="$CVA6_REPO_DIR/core/cache_subsystem/hpdcache"
export RISCV="$SPIKE_INSTALL_DIR"
make -C "$CVA6_REPO_DIR" verilate \
  verilator="$VERILATOR_INSTALL_DIR/bin/verilator" \
  NUM_JOBS="${JOBS:-2}" ver-library="${CPU_BUILD_DIR:-$CVA6_REPO_DIR/build-xmpk}" \
  CFLAGS="-O2 -std=c++17 -I$SPIKE_INSTALL_DIR/include -I$CVA6_REPO_DIR/corev_apu/tb/dpi -I$VERILATOR_INSTALL_DIR/share/verilator/include/vltstd"
