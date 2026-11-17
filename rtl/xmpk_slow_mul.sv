// SPDX-License-Identifier: Apache-2.0 WITH SHL-2.0
// Multi-cycle packed GF256/GF127 engine.
//
// start_i op_i=0: 8 packed GF(2^8) multiplications
// start_i op_i=1: 8 packed GF(127) multiplications
//
// Both operations use one registered multiplication/product stage and one
// registered reduction stage. Only one command may be in flight.
module xmpk_slow_mul #(
    parameter int unsigned XLEN = 64
) (
    input  logic            clk_i,
    input  logic            rst_ni,
    input  logic            start_i,
    input  logic [1:0]      op_i,
    input  logic [XLEN-1:0] a_i,
    input  logic [XLEN-1:0] b_i,
    output logic            busy_o,
    output logic            done_o,
    output logic [XLEN-1:0] z_o
);
  logic [1:0] op_q;
  logic valid_q;
  logic gf127_phase_q;
  logic [XLEN-1:0] a127_q, b127_q;
  logic [15:0][3:0] p16_q, p16_comb;
  logic [7:0][14:0] p256_q,p256_comb;
  logic [3:0][15:0] p127_lo_q;
  (* use_dsp = "yes" *) logic [3:0][15:0] p127_batch_q;
  (* use_dsp = "yes" *) logic [3:0][15:0] p127_comb;
  logic [XLEN-1:0] z_q;

  function automatic logic [14:0] clprod8(
      input logic [7:0] a,input logic [7:0] b);
    logic [14:0] p;
    p='0;
    for(int i=0;i<8;i++)
      for(int j=0;j<8;j++)
        p[i+j]=p[i+j]^(a[i]&b[j]);
    return p;
  endfunction

  logic [7:0] red256[0:7],red127[0:7];
  logic [3:0] red16;
  for (genvar lane = 0; lane < 16; lane++) begin : g_dot16
    gf2n_mul #(.W(4)) i_dot_mul (
        .a_i(a_i[4*lane +: 4]),
        .b_i(b_i[4*lane +: 4]),
        .poly_i(4'h3),
        .z_o(p16_comb[lane])
    );
  end
  always_comb begin
    red16 = '0;
    for (int lane = 0; lane < 16; lane++)
      red16 ^= p16_q[lane];
  end
  for (genvar lane = 0; lane < 4; lane++) begin : g_gf127_mul
    assign p127_comb[lane] =
        gf127_phase_q ?
        a127_q[8*(lane+4) +: 8] * b127_q[8*(lane+4) +: 8] :
        a_i[8*lane +: 8] * b_i[8*lane +: 8];
  end
  for(genvar lane=0;lane<8;lane++)begin:g_lane
    logic [9:0] u127,w127;
    logic [15:0] p127_lane;
    if (lane < 4)
      assign p127_lane = p127_lo_q[lane];
    else
      assign p127_lane = p127_batch_q[lane-4];
    assign p256_comb[lane]=clprod8(a_i[8*lane+:8],b_i[8*lane+:8]);
    always_comb begin
      red256[lane][0]=p256_q[lane][0]^p256_q[lane][8]^p256_q[lane][12]^p256_q[lane][13];
      red256[lane][1]=p256_q[lane][1]^p256_q[lane][8]^p256_q[lane][9]^
                      p256_q[lane][12]^p256_q[lane][14];
      red256[lane][2]=p256_q[lane][2]^p256_q[lane][9]^p256_q[lane][10]^p256_q[lane][13];
      red256[lane][3]=p256_q[lane][3]^p256_q[lane][8]^p256_q[lane][10]^p256_q[lane][11]^
                      p256_q[lane][12]^p256_q[lane][13]^p256_q[lane][14];
      red256[lane][4]=p256_q[lane][4]^p256_q[lane][8]^p256_q[lane][9]^
                      p256_q[lane][11]^p256_q[lane][14];
      red256[lane][5]=p256_q[lane][5]^p256_q[lane][9]^p256_q[lane][10]^p256_q[lane][12];
      red256[lane][6]=p256_q[lane][6]^p256_q[lane][10]^p256_q[lane][11]^p256_q[lane][13];
      red256[lane][7]=p256_q[lane][7]^p256_q[lane][11]^p256_q[lane][12]^p256_q[lane][14];

      u127=10'((p127_lane&16'h007f))+10'(p127_lane>>7);
      w127=10'((u127&10'h07f))+10'(u127>>7);
      red127[lane]=(w127>=10'd127)?8'(w127-10'd127):8'(w127);
    end
  end

  always_ff @(posedge clk_i or negedge rst_ni)begin
    if(!rst_ni)begin
      done_o<=1'b0;
      op_q<=1'b0;
      valid_q<=1'b0;
      gf127_phase_q<=1'b0;
      a127_q<='0;
      b127_q<='0;
      z_q<='0;
      p256_q<='0;
      p127_lo_q<='0;
      p127_batch_q<='0;
      p16_q<='0;
    end else begin
      done_o<=valid_q;
      valid_q<=1'b0;
      if(start_i)begin
        op_q<=op_i;
        if(op_i == 2'd2)begin
          p16_q<=p16_comb;
          valid_q<=1'b1;
        end else if(op_i == 2'd1)begin
          a127_q<=a_i;
          b127_q<=b_i;
          p127_batch_q<=p127_comb;
          gf127_phase_q<=1'b1;
        end else begin
          p256_q<=p256_comb;
          valid_q<=1'b1;
        end
      end else if(gf127_phase_q)begin
        p127_lo_q<=p127_batch_q;
        p127_batch_q<=p127_comb;
        gf127_phase_q<=1'b0;
        valid_q<=1'b1;
      end
      if(valid_q)begin
        if(op_q == 2'd2)begin
          z_q <= {{(XLEN-4){1'b0}}, red16};
        end else begin
          for(int lane=0;lane<8;lane++)
            z_q[8*lane+:8]<=op_q == 2'd1 ? red127[lane] :
                                                 red256[lane];
        end
      end
    end
  end
  assign busy_o=gf127_phase_q|valid_q|done_o;
  assign z_o=z_q;
endmodule
