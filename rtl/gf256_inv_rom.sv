// GF(2^8) multiplicative inverse, AES polynomial x^8+x^4+x^3+x+1 (0x11b).
//
// One SHARED 256x8 constant LUT serves all 8 byte lanes: the byte address is
// muxed onto the table one lane at a time over 8 cycles and results are
// shifted into a packed register.  inv(0)=0 (UOV/MAYO convention).  Inverse is
// rare (once per Gaussian pivot vs O(n^3) elimination MACs), so an 8-cycle
// latency costs nothing end-to-end while synthesizing only one table instead
// of eight.  The table is built at elaboration from a closed-form clmul
// reduction, so it is a pure constant ROM (no per-cycle search).
// Must be clocked: drive start_i for one cycle, poll done_o / z_o.
module gf256_inv_rom #(
    parameter int unsigned XLEN = 64
) (
    input  logic               clk_i,
    input  logic               rst_ni,
    input  logic               start_i,
    input  logic [XLEN-1:0]    a_i,
    output logic               busy_o,
    output logic               done_o,
    output logic [XLEN-1:0]    z_o
);
  localparam int unsigned NL = XLEN / 8;        // 8 lanes

  // ---- constant inverse table -------------------------------------------
  function automatic logic [7:0] gf256_mul(input logic [7:0] a, input logic [7:0] b);
    logic [15:0] g;
    int j;
    g = 16'h0;
    for (j = 0; j < 8; j++)
        if (b[j]) g = g ^ ({8'b0, a} << j);
    for (j = 14; j >= 8; j--)
      if (g[j]) g = g ^ (16'h11b << (j - 8));
    return g[7:0];
  endfunction

  logic [7:0] inv_rom [256];
  integer x, y;
  initial begin
    inv_rom[0] = 8'h00;
    for (x = 1; x < 256; x++) begin
      inv_rom[x] = 8'h00;
      for (y = 1; y < 256; y++)
        if (gf256_mul(8'(x), 8'(y)) == 8'h01) inv_rom[x] = 8'(y);
    end
  end

  logic [$clog2(NL+1)-1:0] cnt;
  logic [XLEN-1:0]        a_q, z_q;
  logic [7:0]             invbyte;

  always_comb invbyte = inv_rom[a_q[8*cnt +: 8]];

  always_ff @(posedge clk_i or negedge rst_ni) begin
    if (!rst_ni) begin
      cnt <= '0; busy_o <= 1'b0; done_o <= 1'b0; a_q <= '0; z_q <= '0;
    end else begin
      done_o <= 1'b0;
      if (start_i && !busy_o) begin
        a_q <= a_i; z_q <= '0; cnt <= '0; busy_o <= 1'b1;
      end else if (busy_o) begin
        z_q[8*cnt +: 8] <= invbyte;
        if (cnt == NL-1) begin
          busy_o <= 1'b0; done_o <= 1'b1;
        end else cnt <= cnt + 1'b1;
      end
    end
  end

  assign z_o = z_q;
endmodule
