// Copyright 2021 Thales DIS design services SAS
//
// Licensed under the Solderpad Hardware Licence, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// SPDX-License-Identifier: Apache-2.0 WITH SHL-2.0
// You may obtain a copy of the License at https://solderpad.org/licenses/
//
// Original Author: Guillaume Chauvon (guillaume.chauvon@thalesgroup.com)



package cvxif_instr_pkg;

  typedef enum logic [5:0] {
    ILLEGAL = 4'b0000,
    NOP = 4'b0001,
    ADD = 4'b0010,
    DOUBLE_RS1 = 4'b0011,
    DOUBLE_RS2 = 4'b0100,
    ADD_MULTI = 4'b0101,
    MADD_RS3_R4 = 4'b0110,
    MSUB_RS3_R4 = 4'b0111,
    NMADD_RS3_R4 = 4'b1000,
    NMSUB_RS3_R4 = 4'b1001,
    GF16_MUL     = 4'b1010,
    GF16_MACC    = 4'b1011,
    GF256_MUL    = 4'b1100,
    GF127_MUL    = 4'b1101,
    ADD_RS3_R = 5'b01111,
    VBROAD16   = 5'b10000,
    VBROAD256  = 5'b10001,
    XORFOLD16  = 5'b10010,
    XORFOLD256 = 5'b10011,
    GF256_INV  = 5'b10100,
    FNZ16      = 5'b10101,
    FNZ256     = 5'b10110,
    TRF_WRITE  = 5'b10111,
    TRF_READ   = 5'b11000,
    GER16      = 5'b11001,
    GER16R     = 5'b11010,
    BGER16R    = 5'b11011,
    ATRF_LOAD  = 5'b11100,
    AGER16R    = 5'b11101,
    ATRF_LOAD16 = 5'b11110,
    AGER16X16   = 5'b11111,
    AGER16R2    = 6'b100000,
    GF16_DOT    = 6'b100001,
    PIVOT16     = 6'b100010,
    ROWELIM16   = 6'b100011,
    GER_SETUP   = 6'b100100,
    GER_RUN     = 6'b100101,
    XMEM_STORE  = 6'b100110,
    QGEMM_SETUP = 6'b100111,
    QGEMM_RUN   = 6'b101000,
    MQEVAL_SETUP = 6'b101001,
    MQEVAL_RUN   = 6'b101010,
    SOLVEQ_SETUP = 6'b101011,
    SOLVEQ_RUN   = 6'b101100,
    LINXFM_SETUP = 6'b101101,
    LINXFM_RUN   = 6'b101110,
    EXTGEMM_SETUP = 6'b101111,
    EXTGEMM_RUN   = 6'b110000
  } opcode_t;


  typedef struct packed {
    logic accept;
    logic writeback;  // TODO depends on dualwrite
    logic [2:0] register_read;  // TODO Nr read ports
  } issue_resp_t;

  typedef struct packed {
    logic        accept;
    logic [31:0] instr;
  } compressed_resp_t;

  typedef struct packed {
    logic [31:0] instr;
    logic [31:0] mask;
    issue_resp_t resp;
    opcode_t     opcode;
  } copro_issue_resp_t;


  typedef struct packed {
    logic [15:0]      instr;
    logic [15:0]      mask;
    compressed_resp_t resp;
  } copro_compressed_resp_t;

  // 4 Possible RISCV instructions for Coprocessor
  parameter int unsigned NbInstr = 38;
  parameter copro_issue_resp_t CoproInstr[NbInstr] = '{
      '{
          // Custom Nop
          instr:
          32'b00000_00_00000_00000_0_00_00000_1111011,  // custom3 opcode
          mask: 32'b11111_11_00000_00000_1_11_00000_1111111,
          resp : '{accept : 1'b1, writeback : 1'b0, register_read : {1'b0, 1'b0, 1'b0}},
          opcode : NOP
      },
      '{  // x.gf16mul  rd,rs1,rs2  (16 packed GF(2^4) mults)
          instr: 32'b00001_00_00000_00000_0_01_00000_1111011,
          mask : 32'b11111_11_00000_00000_1_11_00000_1111111,
          resp : '{accept : 1'b1, writeback : 1'b1, register_read : {1'b0, 1'b1, 1'b1}},
          opcode : GF16_MUL
      },
      '{  // x.gf16macc rd,rs1,rs2 (rd = old(rd) ^ packgf16(rs1*rs2))
          // CVA6 custom offload maps operand 3 to the old rd (MUX_RD_RS3).
          instr: 32'b00000_01_00000_00000_0_01_00000_1111011,
          mask : 32'b00000_11_00000_00000_1_11_00000_1111111,
          resp : '{accept : 1'b1, writeback : 1'b1, register_read : {1'b1, 1'b1, 1'b1}},
          opcode : GF16_MACC
      },
      '{  // x.gf256mul rd,rs1,rs2 (8 packed GF(2^8) mults)
          instr: 32'b00010_00_00000_00000_0_01_00000_1111011,
          mask : 32'b11111_11_00000_00000_1_11_00000_1111111,
          resp : '{accept : 1'b1, writeback : 1'b1, register_read : {1'b0, 1'b1, 1'b1}},
          opcode : GF256_MUL
      },
      '{  // x.gf127mul rd,rs1,rs2 (8 packed GF(127) mults)
          instr: 32'b00011_00_00000_00000_0_01_00000_1111011,
          mask : 32'b11111_11_00000_00000_1_11_00000_1111111,
          resp : '{accept : 1'b1, writeback : 1'b1, register_read : {1'b0, 1'b1, 1'b1}},
          opcode : GF127_MUL
      },
      '{  // x.vbroad16 rd,rs1,rs2(idx): nibble rs1[idx] -> all 16 lanes
          instr: 32'b00100_00_00000_00000_0_01_00000_1111011,
          mask : 32'b11111_11_00000_00000_1_11_00000_1111111,
          resp : '{accept : 1'b1, writeback : 1'b1, register_read : {1'b0, 1'b1, 1'b1}},
          opcode : VBROAD16
      },
      '{  // x.vbroad256 rd,rs1,rs2(idx): byte rs1[idx] -> all 8 lanes
          instr: 32'b00101_00_00000_00000_0_01_00000_1111011,
          mask : 32'b11111_11_00000_00000_1_11_00000_1111111,
          resp : '{accept : 1'b1, writeback : 1'b1, register_read : {1'b0, 1'b1, 1'b1}},
          opcode : VBROAD256
      },
      '{  // x.xorfold16 rd,rs1 : XOR-reduce 16 nibble lanes -> lane0
          instr: 32'b00110_00_00000_00000_0_01_00000_1111011,
          mask : 32'b11111_11_00000_00000_1_11_00000_1111111,
          resp : '{accept : 1'b1, writeback : 1'b1, register_read : {1'b0, 1'b0, 1'b1}},
          opcode : XORFOLD16
      },
      '{  // x.xorfold256 rd,rs1 : XOR-reduce 8 byte lanes -> lane0
          instr: 32'b00111_00_00000_00000_0_01_00000_1111011,
          mask : 32'b11111_11_00000_00000_1_11_00000_1111111,
          resp : '{accept : 1'b1, writeback : 1'b1, register_read : {1'b0, 1'b0, 1'b1}},
          opcode : XORFOLD256
      },
      '{  // x.gf256inv rd,rs1 : 8 packed GF(2^8) inverses (AES poly)
          instr: 32'b01000_00_00000_00000_0_01_00000_1111011,
          mask : 32'b11111_11_00000_00000_1_11_00000_1111111,
          resp : '{accept : 1'b1, writeback : 1'b1, register_read : {1'b0, 1'b0, 1'b1}},
          opcode : GF256_INV
      },
      '{  // x.fnz16 rd,rs1 : first non-zero nibble index (16 if all zero)
          instr: 32'b01001_00_00000_00000_0_01_00000_1111011,
          mask : 32'b11111_11_00000_00000_1_11_00000_1111111,
          resp : '{accept : 1'b1, writeback : 1'b1, register_read : {1'b0, 1'b0, 1'b1}},
          opcode : FNZ16
      },
      '{  // x.fnz256 rd,rs1 : first non-zero byte index (8 if all zero)
          instr: 32'b01010_00_00000_00000_0_01_00000_1111011,
          mask : 32'b11111_11_00000_00000_1_11_00000_1111111,
          resp : '{accept : 1'b1, writeback : 1'b1, register_read : {1'b0, 1'b0, 1'b1}},
          opcode : FNZ256
      },
      '{  // x.trf.write rd,rs1(data),rs2(addr)
          instr: 32'b01011_00_00000_00000_0_01_00000_1111011,
          mask : 32'b11111_11_00000_00000_1_11_00000_1111111,
          resp : '{accept : 1'b1, writeback : 1'b0, register_read : {1'b0, 1'b1, 1'b1}},
          opcode : TRF_WRITE
      },
      '{  // x.trf.read rd,rs2(addr)
          instr: 32'b01100_00_00000_00000_0_01_00000_1111011,
          mask : 32'b11111_11_00000_00000_1_11_00000_1111111,
          resp : '{accept : 1'b1, writeback : 1'b1, register_read : {1'b0, 1'b1, 1'b0}},
          opcode : TRF_READ
      },
      '{  // x.ger16 rd,rs1(A word),rs2(tile configuration)
          instr: 32'b01101_00_00000_00000_0_01_00000_1111011,
          mask : 32'b11111_11_00000_00000_1_11_00000_1111111,
          resp : '{accept : 1'b1, writeback : 1'b1, register_read : {1'b0, 1'b1, 1'b1}},
          opcode : GER16
      },
      '{  // x.ger16r rd,rs1(packed row scalars),rs2(tile configuration)
          instr: 32'b01110_00_00000_00000_0_01_00000_1111011,
          mask : 32'b11111_11_00000_00000_1_11_00000_1111111,
          resp : '{accept : 1'b1, writeback : 1'b1, register_read : {1'b0, 1'b1, 1'b1}},
          opcode : GER16R
      },
      '{  // x.bger16r rd(scalars),rs1(B word),rs2(configuration)
          // The old rd is operand 3 and is replaced by the completion result.
          instr: 32'b01111_00_00000_00000_0_01_00000_1111011,
          mask : 32'b11111_11_00000_00000_1_11_00000_1111111,
          resp : '{accept : 1'b1, writeback : 1'b1, register_read : {1'b1, 1'b1, 1'b1}},
          opcode : BGER16R
      },
      '{  // x.atrf.load rd,rs1(A row word),rs2(row/word configuration)
          instr: 32'b10000_00_00000_00000_0_01_00000_1111011,
          mask : 32'b11111_11_00000_00000_1_11_00000_1111111,
          resp : '{accept : 1'b1, writeback : 1'b1, register_read : {1'b0, 1'b1, 1'b1}},
          opcode : ATRF_LOAD
      },
      '{  // x.ager16r rd,rs1(B word),rs2(configuration)
          instr: 32'b10001_00_00000_00000_0_01_00000_1111011,
          mask : 32'b11111_11_00000_00000_1_11_00000_1111111,
          resp : '{accept : 1'b1, writeback : 1'b1, register_read : {1'b0, 1'b1, 1'b1}},
          opcode : AGER16R
      },
      '{  // x.atrf.load16 rd,rs1(A row word),rs2(row/word configuration)
          instr: 32'b10010_00_00000_00000_0_01_00000_1111011,
          mask : 32'b11111_11_00000_00000_1_11_00000_1111111,
          resp : '{accept : 1'b1, writeback : 1'b1, register_read : {1'b0, 1'b1, 1'b1}},
          opcode : ATRF_LOAD16
      },
      '{  // x.ager16x16 rd,rs1(B word),rs2(configuration)
          instr: 32'b10011_00_00000_00000_0_01_00000_1111011,
          mask : 32'b11111_11_00000_00000_1_11_00000_1111111,
          resp : '{accept : 1'b1, writeback : 1'b1, register_read : {1'b0, 1'b1, 1'b1}},
          opcode : AGER16X16
      },
      '{  // x.ager16r2 rd(B1),rs1(B0),rs2(configuration)
          // The old rd supplies B1 and is replaced by the completion count.
          instr: 32'b10100_00_00000_00000_0_01_00000_1111011,
          mask : 32'b11111_11_00000_00000_1_11_00000_1111111,
          resp : '{accept : 1'b1, writeback : 1'b1, register_read : {1'b1, 1'b1, 1'b1}},
          opcode : AGER16R2
      },
      '{  // x.gf16dot rd,rs1,rs2
          instr: 32'b10101_00_00000_00000_0_01_00000_1111011,
          mask : 32'b11111_11_00000_00000_1_11_00000_1111111,
          resp : '{accept : 1'b1, writeback : 1'b1, register_read : {1'b0, 1'b1, 1'b1}},
          opcode : GF16_DOT
      },
      '{  // x.pivot16 rd,rs1(row word),rs2(lane index)
          instr: 32'b10110_00_00000_00000_0_01_00000_1111011,
          mask : 32'b11111_11_00000_00000_1_11_00000_1111111,
          resp : '{accept : 1'b1, writeback : 1'b1, register_read : {1'b0, 1'b1, 1'b1}},
          opcode : PIVOT16
      },
      '{  // x.rowelim16 rd(target),rs1(pivot),rs2(scalar)
          instr: 32'b10111_00_00000_00000_0_01_00000_1111011,
          mask : 32'b11111_11_00000_00000_1_11_00000_1111111,
          resp : '{accept : 1'b1, writeback : 1'b1, register_read : {1'b1, 1'b1, 1'b1}},
          opcode : ROWELIM16
      },
      '{  // x.ger.setup rs1(A tile physical base),rs2(B physical base)
          instr: 32'b11000_00_00000_00000_0_01_00000_1111011,
          mask : 32'b11111_11_00000_00000_1_11_00000_1111111,
          resp : '{accept : 1'b1, writeback : 1'b0, register_read : {1'b0, 1'b1, 1'b1}},
          opcode : GER_SETUP
      },
      '{  // x.ger.run rd,rs1(N): autonomous 16-row GER tile
          instr: 32'b11001_00_00000_00000_0_01_00000_1111011,
          mask : 32'b11111_11_00000_00000_1_11_00000_1111111,
          resp : '{accept : 1'b1, writeback : 1'b1, register_read : {1'b0, 1'b0, 1'b1}},
          opcode : GER_RUN
      },
      '{  // x.xmem.store rd,rs1(address),rs2(data): coherent 64-bit store
          instr: 32'b11010_00_00000_00000_0_01_00000_1111011,
          mask : 32'b11111_11_00000_00000_1_11_00000_1111111,
          resp : '{accept : 1'b1, writeback : 1'b1, register_read : {1'b0, 1'b1, 1'b1}},
          opcode : XMEM_STORE
      },
      '{  // x.qgemm.setup rd(C base),rs1(A base),rs2(B base)
          instr: 32'b11011_00_00000_00000_0_01_00000_1111011,
          mask : 32'b11111_11_00000_00000_1_11_00000_1111111,
          resp : '{accept : 1'b1, writeback : 1'b0, register_read : {1'b1, 1'b1, 1'b1}},
          opcode : QGEMM_SETUP
      },
      '{  // x.qgemm.run rd,rs1({N_words,K,M})
          instr: 32'b11100_00_00000_00000_0_01_00000_1111011,
          mask : 32'b11111_11_00000_00000_1_11_00000_1111111,
          resp : '{accept : 1'b1, writeback : 1'b1, register_read : {1'b0, 1'b0, 1'b1}},
          opcode : QGEMM_RUN
      },
      '{  // x.mqeval.setup rd(Y base),rs1(Q base),rs2(X base)
          instr: 32'b11101_00_00000_00000_0_01_00000_1111011,
          mask : 32'b11111_11_00000_00000_1_11_00000_1111111,
          resp : '{accept : 1'b1, writeback : 1'b0, register_read : {1'b1, 1'b1, 1'b1}},
          opcode : MQEVAL_SETUP
      },
      '{  // x.mqeval.run rd,rs1({M,N})
          instr: 32'b11110_00_00000_00000_0_01_00000_1111011,
          mask : 32'b11111_11_00000_00000_1_11_00000_1111111,
          resp : '{accept : 1'b1, writeback : 1'b1, register_read : {1'b0, 1'b0, 1'b1}},
          opcode : MQEVAL_RUN
      },
      '{  // x.solveq.setup rs1(augmented matrix),rs2(solution)
          instr: 32'b00001_00_00000_00000_0_10_00000_1111011,
          mask : 32'b11111_11_00000_00000_1_11_00000_1111111,
          resp : '{accept : 1'b1, writeback : 1'b0, register_read : {1'b0, 1'b1, 1'b1}},
          opcode : SOLVEQ_SETUP
      },
      '{  // x.solveq.run rd,rs1(N)
          instr: 32'b00010_00_00000_00000_0_10_00000_1111011,
          mask : 32'b11111_11_00000_00000_1_11_00000_1111111,
          resp : '{accept : 1'b1, writeback : 1'b1, register_read : {1'b0, 1'b0, 1'b1}},
          opcode : SOLVEQ_RUN
      },
      '{  // x.linxfm.setup rd(Y base),rs1(T base),rs2(X base)
          instr: 32'b00011_00_00000_00000_0_10_00000_1111011,
          mask : 32'b11111_11_00000_00000_1_11_00000_1111111,
          resp : '{accept : 1'b1, writeback : 1'b0, register_read : {1'b1, 1'b1, 1'b1}},
          opcode : LINXFM_SETUP
      },
      '{  // x.linxfm.run rd,rs1({count,L})
          instr: 32'b00100_00_00000_00000_0_10_00000_1111011,
          mask : 32'b11111_11_00000_00000_1_11_00000_1111111,
          resp : '{accept : 1'b1, writeback : 1'b1, register_read : {1'b0, 1'b0, 1'b1}},
          opcode : LINXFM_RUN
      },
      '{  // x.extgemm.setup rd(C base),rs1(A base),rs2(B base)
          instr: 32'b00101_00_00000_00000_0_10_00000_1111011,
          mask : 32'b11111_11_00000_00000_1_11_00000_1111111,
          resp : '{accept : 1'b1, writeback : 1'b0, register_read : {1'b1, 1'b1, 1'b1}},
          opcode : EXTGEMM_SETUP
      },
      '{  // x.extgemm.run rd,rs1({L10,N,K,M})
          instr: 32'b00110_00_00000_00000_0_10_00000_1111011,
          mask : 32'b11111_11_00000_00000_1_11_00000_1111111,
          resp : '{accept : 1'b1, writeback : 1'b1, register_read : {1'b0, 1'b0, 1'b1}},
          opcode : EXTGEMM_RUN
      }
  };

  parameter int unsigned NbCompInstr = 1;
  parameter copro_compressed_resp_t CoproCompInstr[NbCompInstr] = '{
      // C_NOP
      '{
          instr : 16'b111_0_00000_00000_00,
          mask : 16'b111_1_00000_00000_11,
          resp : '{accept : 1'b1, instr : 32'b00000_00_00000_00000_0_00_00000_1111011}
      }
  };

endpackage
