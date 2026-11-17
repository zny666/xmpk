#include <cstdint>
#include <cstdio>
#include <random>
#include <stdexcept>
#include "verilated.h"
#include "Vdecoder_top.h"
#include "decoder_cases.h"
static void check(bool ok,const char*message){if(!ok)throw std::runtime_error(message);}
int main(int argc,char**argv) {
  Verilated::commandArgs(argc,argv);
  Vdecoder_top d;
  std::mt19937 rng(0x225613);
  try {
    d.clk_i=0;d.rst_ni=1;
    for(const auto &c:decode_cases)for(int iteration=0;iteration<32;iteration++)
      for(unsigned valid=0;valid<8;valid++){
        d.instr_i=c.base|(rng()&~c.mask);
        d.rs_valid_i=valid;
        d.a_i=(uint64_t(rng())<<32)|rng();
        d.b_i=(uint64_t(rng())<<32)|rng();
        d.c_i=(uint64_t(rng())<<32)|rng();
        d.eval();
        check(d.accept_o&&d.opcode_o==c.op,"decode opcode");
        check(d.read_o==c.reads&&d.writeback_o==c.wb,"decode response");
        check(d.ready_o==((valid&c.reads)==c.reads),"operand ready gating");
        check(d.a_o==((c.reads&1)?d.a_i:0),"rs1 mapping");
        check(d.b_o==((c.reads&2)?d.b_i:0),"rs2 mapping");
        check(d.c_o==((c.reads&4)?d.c_i:0),"old rd mapping");
      }
    for(uint32_t instruction:{0u,0x13u,0xffffffffu}){
      d.instr_i=instruction;d.eval();
      check(!d.accept_o&&d.opcode_o==0,"unknown encoding accepted");
    }
    puts("ALL_38_ENCODINGS_AND_OPERAND_MASKS_PASS");
    return 0;
  }catch(const std::exception&e){fprintf(stderr,"FAIL: %s\n",e.what());return 1;}
}
