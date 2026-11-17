// SPDX-License-Identifier: Apache-2.0 WITH SHL-2.0
// Combinational MPKC assist primitives used to build ger (rank-1 update /
// dot-product) and solve (Gaussian pivot handling) out of the packed MACs.
// No multipliers here: these are only broadcast / reduction / find-first.
//
//   op 0  VBROAD16 : broadcast nibble a[idx] to all 16 GF(2^4) lanes
//   op 1  VBROAD256: broadcast byte   a[idx] to all  8 GF(2^8) lanes
//   op 2  XORFOLD16: XOR-reduce 16 nibble lanes -> result in lane 0
//   op 3  XORFOLD256: XOR-reduce 8 byte lanes -> result in lane 0
//   op 4  FNZ16    : first non-zero nibble index (0..15), or 16 if all zero
//   op 5  FNZ256   : first non-zero byte   index (0..7),  or 8  if all zero
//
// idx comes from b_i (low bits).  fnz returns a small integer in z_o[4:0].
module gf_assist #(
    parameter int unsigned XLEN = 64
) (
    input  logic [2:0]   op_i,
    input  logic [XLEN-1:0] a_i,
    input  logic [XLEN-1:0] b_i,
    output logic [XLEN-1:0] z_o
);
  // ---- broadcast ---------------------------------------------------------
  logic [3:0] nib;
  logic [7:0] byt;
  logic [XLEN-1:0] broad16, broad256;
  logic [3:0] idx16;
  logic [2:0] idx256;

  assign idx16 = b_i[3:0];
  assign idx256 = b_i[2:0];
  assign nib = a_i[4*idx16 +: 4];
  assign byt = a_i[8*idx256 +: 8];

  always_comb begin
    broad16 = '0;
    for (int i = 0; i < 16; i++) broad16[4*i +: 4] = nib;
    broad256 = '0;
    for (int i = 0; i < 8; i++) broad256[8*i +: 8] = byt;
  end

  // ---- horizontal XOR reduction ------------------------------------------
  logic [3:0] xred16;
  logic [7:0] xred256;
  always_comb begin
    xred16 = 4'h0;
    for (int i = 0; i < 16; i++) xred16 = xred16 ^ a_i[4*i +: 4];
  end
  always_comb begin
    xred256 = 8'h00;
    for (int i = 0; i < 8; i++) xred256 = xred256 ^ a_i[8*i +: 8];
  end

  // ---- first non-zero (priority, LSB-first) ------------------------------
  logic [4:0] fnz16, fnz256;
  always_comb begin
    fnz16 = 5'd16;                 // sentinel: all zero
    for (int i = 15; i >= 0; i--)
      if (a_i[4*i +: 4] != 4'h0) fnz16 = 5'(i);
  end
  always_comb begin
    fnz256 = 5'd8;
    for (int i = 7; i >= 0; i--)
      if (a_i[8*i +: 8] != 8'h00) fnz256 = 5'(i);
  end

  always_comb begin
    unique case (op_i)
      3'd0:    z_o = broad16;
      3'd1:    z_o = broad256;
      3'd2:    z_o = {{(XLEN-4){1'b0}}, xred16};
      3'd3:    z_o = {{(XLEN-8){1'b0}}, xred256};
      3'd4:    z_o = {{(XLEN-5){1'b0}}, fnz16};
      3'd5:    z_o = {{(XLEN-5){1'b0}}, fnz256};
      default: z_o = '0;
    endcase
  end
endmodule
