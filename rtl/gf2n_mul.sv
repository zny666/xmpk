// SPDX-License-Identifier: Apache-2.0 WITH SHL-2.0
// Generic binary-field multiplier: carry-less product followed by modular
// reduction modulo the degree-W polynomial whose low coefficients are `poly`.
// One instance serves GF(16) (x^4+x+1) and GF(256) (AES x^8+x^4+x^3+x+1),
// which is why Xmpk needs only one multiplier datapath for the even-char family.
module gf2n_mul #(
    parameter int unsigned W = 8
) (
    input  logic [W-1:0]   a_i,
    input  logic [W-1:0]   b_i,
    input  logic [W-1:0]   poly_i,   // low W bits of reduction polynomial
    output logic [W-1:0]   z_o
);
  localparam int unsigned PW = 2 * W;
  logic [PW-1:0] t;

  always_comb begin
    // schoolbook carry-less multiplication
    t = '0;
    for (int i = 0; i < W; i++) begin
      for (int j = 0; j < W; j++) begin
        t[i+j] = t[i+j] ^ (a_i[i] & b_i[j]);
      end
    end
    // reduce from the highest degree down; bit W of the full polynomial
    // cancels the bit being reduced, so only `poly_i` (low W bits) is XORed
    for (int i = PW - 2; i >= W; i--) begin
      if (t[i]) t[i-:W+1] = t[i-:W+1] ^ {1'b1, poly_i};
    end
  end

  assign z_o = t[W-1:0];
endmodule
