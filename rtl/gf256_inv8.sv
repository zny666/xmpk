// 8 parallel GF(2^8) inverses in one combinational cycle, AES poly 0x11b.
// Each lane is an independent 256x8 constant ROM (inv(0)=0).  This preserves
// the single-cycle CV-X-IF result contract; area is ~8 small LUT-ROMs.
module gf256_inv8 #(
    parameter int unsigned XLEN = 64
) (
    input  logic [XLEN-1:0] a_i,
    output logic [XLEN-1:0] z_o
);
  function automatic logic [7:0] gf256_mul(input logic [7:0] a, input logic [7:0] b);
    logic [15:0] g; int j;
    g = 16'h0;
    for (j = 0; j < 8; j++) if (b[j]) g = g ^ ({8'b0,a} << j);
    for (j = 14; j >= 8; j--) if (g[j]) g = g ^ (16'h11b << (j-8));
    return g[7:0];
  endfunction

  function automatic logic [7:0] inv_lut(input logic [7:0] x);
    int y; logic [7:0] r;
    r = 8'h00;
    if (x != 0)
      for (y = 1; y < 256; y++)
        if (gf256_mul(x, 8'(y)) == 8'h01) r = 8'(y);
    return r;
  endfunction

  for (genvar i = 0; i < XLEN/8; i++) begin : g_inv
    assign z_o[8*i +: 8] = inv_lut(a_i[8*i +: 8]);
  end
endmodule
