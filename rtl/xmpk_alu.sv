// SPDX-License-Identifier: Apache-2.0 WITH SHL-2.0
// Xmpk multi-field arithmetic + MPKC kernel assist unit (minimal-area).
//
// Packed multipliers (one 64-bit word services all five MPKC schemes):
//   op 0  GF16_MUL   : 16 parallel GF(2^4) mults   (MAYO nibble lanes)
//   op 1  GF16_MACC  : 16 GF(2^4) mults XOR-fold c (outer-product / dot MAC)
//   op 2  GF256_MUL  :  8 parallel GF(2^8) mults   (UOV / SNOVA byte lanes)
//   op 3  GF127_MUL  :  8 parallel GF(127) mults   (QR-UOV coefficient lanes)
//
// Kernel assists (ger / solve / sample), no extra multiplier array:
//   op 4  VBROAD16  : broadcast nibble a[b[3:0]]  to 16 lanes
//   op 5  VBROAD256 : broadcast byte   a[b[2:0]]  to 8 lanes
//   op 6  XORFOLD16 : XOR-reduce 16 nibble lanes into lane 0
//   op 7  XORFOLD256: XOR-reduce 8 byte lanes into lane 0
//   op 8  GF256_INV : handled by the multi-cycle shared ROM in copro_alu
//   op 9  FNZ16     : first non-zero nibble index (16 if all zero)
//   op 10 FNZ256    : first non-zero byte index   (8  if all zero)
//   op 11 PIVOT16   : selected value/inverse plus first non-zero index
//   op 12 ROWELIM16 : c ^= a * broadcast(b[3:0])
module xmpk_alu #(
    parameter int unsigned XLEN = 64
) (
    input  logic [3:0]      op_i,
    input  logic [XLEN-1:0] a_i,
    input  logic [XLEN-1:0] b_i,
    input  logic [XLEN-1:0] c_i,
    output logic [XLEN-1:0] z_o
);
  localparam logic [7:0] P16  = 8'h03;

  // ---- packed field multipliers -----------------------------------------
  logic [XLEN-1:0] mul16, mul16_b;
  always_comb begin
    mul16_b = b_i;
    if (op_i == 4'd12)
      for (int i = 0; i < 16; i++) mul16_b[4*i +: 4] = b_i[3:0];
  end
  for (genvar i = 0; i < 16; i++) begin : g_mul16
    gf2n_mul #(.W(4)) u_mul16 (
      .a_i(a_i[4*i+:4]), .b_i(mul16_b[4*i+:4]),
      .poly_i(P16[3:0]), .z_o(mul16[4*i+:4]));
  end

  // ---- broadcast / fold / find-first assists ----------------------------
  logic [XLEN-1:0] assist;
  // map internal op 4..10 -> gf_assist op 0..5
  logic [2:0] assist_op;
  always_comb begin
    unique case (op_i)
      4'd4: assist_op = 3'd0;  // VBROAD16
      4'd5: assist_op = 3'd1;  // VBROAD256
      4'd6: assist_op = 3'd2;  // XORFOLD16
      4'd7: assist_op = 3'd3;  // XORFOLD256
      4'd9: assist_op = 3'd4;  // FNZ16
      4'd10:assist_op = 3'd5;  // FNZ256
      4'd11:assist_op = 3'd4;  // PIVOT16 also reports FNZ16
      default: assist_op = 3'd0;
    endcase
  end
  gf_assist #(.XLEN(XLEN)) u_assist (.op_i(assist_op), .a_i(a_i), .b_i(b_i), .z_o(assist));

  function automatic logic [3:0] inv16(input logic [3:0] a);
    unique case (a)
      4'h0: inv16 = 4'h0;
      4'h1: inv16 = 4'h1;
      4'h2: inv16 = 4'h9;
      4'h3: inv16 = 4'he;
      4'h4: inv16 = 4'hd;
      4'h5: inv16 = 4'hb;
      4'h6: inv16 = 4'h7;
      4'h7: inv16 = 4'h6;
      4'h8: inv16 = 4'hf;
      4'h9: inv16 = 4'h2;
      4'ha: inv16 = 4'hc;
      4'hb: inv16 = 4'h5;
      4'hc: inv16 = 4'ha;
      4'hd: inv16 = 4'h4;
      4'he: inv16 = 4'h3;
      default: inv16 = 4'h8;
    endcase
  endfunction

  logic [3:0] pivot_value, pivot_inverse;
  assign pivot_value = a_i[4*b_i[3:0] +: 4];
  assign pivot_inverse = inv16(pivot_value);

  always_comb begin
    unique case (op_i)
      4'd0:  z_o = mul16;
      4'd1:  z_o = c_i ^ mul16;
      4'd2:  z_o = '0; // GF256 multiply is handled by a multi-cycle pipeline
      4'd3:  z_o = '0; // GF127 multiply is handled by a multi-cycle pipeline
      4'd8:  z_o = '0; // stateful shared inverse is outside this combinational ALU
      4'd4, 4'd5, 4'd6, 4'd7, 4'd9, 4'd10: z_o = assist;
      4'd11: z_o = {{(XLEN-13){1'b0}}, assist[4:0],
                    pivot_value, pivot_inverse};
      4'd12: z_o = c_i ^ mul16;
      default: z_o = '0;
    endcase
  end
endmodule
