// Print ABI offsets from the exact generated header; no simulator attachment.
#include <cstddef>
#include <cstdio>
#include "Variane_testharness___024root.h"
using Root = Variane_testharness___024root;
#define OFFSET(label, member) \
  std::printf("%s %zu\n", label, offsetof(Root, member))
int main() {
  OFFSET("cycle", ariane_testharness__DOT__i_ariane__DOT__i_cva6__DOT__csr_regfile_i__DOT__cycle_q);
  OFFSET("instret", ariane_testharness__DOT__i_ariane__DOT__i_cva6__DOT__csr_regfile_i__DOT__instret_q);
  OFFSET("pc", ariane_testharness__DOT__i_ariane__DOT__i_cva6__DOT__pc_id_ex);
  OFFSET("sram", ariane_testharness__DOT__i_sram__DOT__gen_cut__BRA__0__KET____DOT__gen_mem_user__DOT__i_tc_sram_wrapper_user__DOT__i_tc_sram__DOT__sram);
}
