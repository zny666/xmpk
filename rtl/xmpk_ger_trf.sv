// SPDX-License-Identifier: Apache-2.0 WITH SHL-2.0
// Xmpk Stage-2 micro tensor register file and GF(16) GER engine.
//
// A GER command updates a contiguous tile entirely inside the TRF:
//   TRF[c_base+i] ^= broadcast16(a_word[nibble]) * TRF[b_base+i]
// for i=0..len-1, one packed 64-bit word per cycle.
//
// Command encoding:
//   op=0 WRITE: a_i=data, b_i[4:0]=TRF address
//   op=1 READ : b_i[4:0]=TRF address, response=data
//               bit3 selects C, bit4 is the C address high bit
//   op=2 GER16: a_i=packed A word
//               b_i[3:0]   nibble index
//               b_i[6:4]   B-bank tile base
//               b_i[11:8]  C-bank tile base
//               b_i[14:12] length minus one (1..8 words)
//   op=3 GER16R: a_i[31:0]=up to 8 packed row scalars
//               b_i[2:0]   words per row minus one
//               b_i[6:4]   rows minus one
//               b_i[10:8]  B-bank tile base
//               b_i[15:12] C-bank tile base
//   op=4 BGER16R: a_i=B packed word, c_i[31:0]=packed row scalars
//                b_i[2:0]   word index within each C row
//                b_i[6:4]   words per C row minus one
//                b_i[10:8]  rows minus one
//                b_i[15:12] C-bank tile base
//   op=5 ATRF_LOAD: a_i=packed A row word
//                   b_i[2:0]=row, b_i[3]=word index
//   op=6 AGER16R: a_i=B packed word
//                 b_i[2:0]   C word index
//                 b_i[6:4]   words per C row minus one
//                 b_i[10:8]  rows minus one
//                 b_i[15:12] C-bank tile base
//                 b_i[20:16] A column index
//   op=7 ATRF_LOAD16: a_i=packed A row word
//                     b_i[3:0]=row, b_i[4]=word index
//   op=8 AGER16X16: a_i=B packed word
//                   b_i[2:0]   C word index
//                   b_i[6:4]   words per C row minus one
//                   b_i[11:8]  rows minus one
//                   b_i[16:12] C-bank tile base
//                   b_i[21:17] A column index
//   op=9 AGER16R2: a_i=first B word, c_i=second B word
//                  b_i uses the AGER16X16 configuration
module xmpk_trf_bank #(
    parameter int unsigned XLEN = 64,
    parameter int unsigned ADDR_W = 3
) (
    input  logic            clk_i,
    input  logic            we_i,
    input  logic [ADDR_W-1:0] addr_i,
    input  logic [XLEN-1:0] wdata_i,
    output logic [XLEN-1:0] rdata_o
);
`ifdef SYNTHESIS
  // RAM32M has 32 entries, so widening a bank from 8 to 16 entries does not
  // add primitives.
  for (genvar byte_idx = 0; byte_idx < XLEN/8; byte_idx++) begin : g_ram
    RAM32M i_ram (
        .DOA(rdata_o[8*byte_idx +: 2]),
        .DOB(rdata_o[8*byte_idx+2 +: 2]),
        .DOC(rdata_o[8*byte_idx+4 +: 2]),
        .DOD(rdata_o[8*byte_idx+6 +: 2]),
        .ADDRA({{(5-ADDR_W){1'b0}}, addr_i}),
        .ADDRB({{(5-ADDR_W){1'b0}}, addr_i}),
        .ADDRC({{(5-ADDR_W){1'b0}}, addr_i}),
        .ADDRD({{(5-ADDR_W){1'b0}}, addr_i}),
        .DIA(wdata_i[8*byte_idx +: 2]),
        .DIB(wdata_i[8*byte_idx+2 +: 2]),
        .DIC(wdata_i[8*byte_idx+4 +: 2]),
        .DID(wdata_i[8*byte_idx+6 +: 2]),
        .WCLK(clk_i),
        .WE(we_i)
    );
  end
`else
  logic [XLEN-1:0] mem [0:(1<<ADDR_W)-1];
  assign rdata_o = mem[addr_i];
  always_ff @(posedge clk_i)
    if (we_i) mem[addr_i] <= wdata_i;
`endif
endmodule

// Thirty-two rows by 32 GF(16) elements. Four column banks allow four
// nibbles from one packed row word to be transposed every cycle.
module xmpk_atrf4_32 (
    input  logic         clk_i,
    input  logic         we_i,
    input  logic [4:0]   wrow_i,
    input  logic         wword_i,
    input  logic [1:0]   wphase_i,
    input  logic [63:0]  wdata_i,
    input  logic [4:0]   raddr0_i,
    input  logic [4:0]   raddr1_i,
    output logic [127:0] scalars0_o,
    output logic [127:0] scalars1_o
);
`ifdef SYNTHESIS
  logic [3:0][127:0] bank_data0, bank_data1;
  logic [2:0] ram_addr;
  assign ram_addr = we_i ? {wword_i, wphase_i} : raddr0_i[4:2];
  for (genvar row = 0; row < 32; row++) begin : g_row
    for (genvar bank = 0; bank < 4; bank++) begin : g_bank
      for (genvar bit_idx = 0; bit_idx < 4; bit_idx++) begin : g_bit
        RAM32X1D i_ram (
            .SPO(bank_data0[bank][4*row+bit_idx]),
            .DPO(bank_data1[bank][4*row+bit_idx]),
            .A0(ram_addr[0]),
            .A1(ram_addr[1]),
            .A2(ram_addr[2]),
            .A3(1'b0),
            .A4(1'b0),
            .DPRA0(raddr1_i[2]),
            .DPRA1(raddr1_i[3]),
            .DPRA2(raddr1_i[4]),
            .DPRA3(1'b0),
            .DPRA4(1'b0),
            .D(wdata_i[16*wphase_i + 4*bank + bit_idx]),
            .WCLK(clk_i),
            .WE(we_i && wrow_i == row)
        );
      end
    end
  end
  always_comb begin
    scalars0_o = bank_data0[raddr0_i[1:0]];
    scalars1_o = bank_data1[raddr1_i[1:0]];
  end
`else
  logic [3:0] mem [0:31][0:31];
  always_comb begin
    for (int row = 0; row < 32; row++) begin
      scalars0_o[4*row +: 4] = mem[row][raddr0_i];
      scalars1_o[4*row +: 4] = mem[row][raddr1_i];
    end
  end
  always_ff @(posedge clk_i) begin
    if (we_i) begin
      for (int bank = 0; bank < 4; bank++)
        mem[wrow_i][{wword_i, wphase_i, bank[1:0]}] <=
            wdata_i[16*wphase_i + 4*bank +: 4];
    end
  end
`endif
endmodule

// Sixteen rows by 32 GF(16) elements, stored transposed by column. A packed
// 64-bit row word is serialized over 16 cycles on load; all sixteen row
// scalars are then available in parallel for one column index.
module xmpk_atrf (
    input  logic        clk_i,
    input  logic        we_i,
    input  logic [3:0]  wrow_i,
    input  logic [4:0]  addr_i,
    input  logic [3:0]  wdata_i,
    output logic [63:0] scalars_o
);
`ifdef SYNTHESIS
  for (genvar row = 0; row < 16; row++) begin : g_row
    for (genvar bit_idx = 0; bit_idx < 4; bit_idx++) begin : g_bit
      RAM32X1S i_ram (
          .O(scalars_o[4*row+bit_idx]),
          .A0(addr_i[0]),
          .A1(addr_i[1]),
          .A2(addr_i[2]),
          .A3(addr_i[3]),
          .A4(addr_i[4]),
          .D(wdata_i[bit_idx]),
          .WCLK(clk_i),
          .WE(we_i && wrow_i == row)
      );
    end
  end
`else
  logic [3:0] mem [0:15][0:31];
  always_comb
    for (int row = 0; row < 16; row++)
      scalars_o[4*row +: 4] = mem[row][addr_i];
  always_ff @(posedge clk_i)
    if (we_i) mem[wrow_i][addr_i] <= wdata_i;
`endif
endmodule

module xmpk_auto_aggressive #(
    parameter int unsigned XLEN = 64
) (
    input  logic            clk_i,
    input  logic            rst_ni,
    input  logic            cmd_valid_i,
    output logic            cmd_ready_o,
    input  logic [3:0]      cmd_op_i,
    input  logic [XLEN-1:0] a_i,
    input  logic [XLEN-1:0] b_i,
    input  logic [XLEN-1:0] c_i,
    output logic            rsp_valid_o,
    output logic [XLEN-1:0] rsp_data_o,
    output logic            busy_o,
    output logic            mem_req_valid_o,
    input  logic            mem_req_ready_i,
    output logic [XLEN-1:0] mem_req_addr_o,
    output logic            mem_req_write_o,
    output logic [XLEN-1:0] mem_req_wdata_o,
    output logic            mem_req_kill_o,
    input  logic            mem_rsp_valid_i,
    input  logic [XLEN-1:0] mem_rsp_data_i,
    input  logic            mem_rsp_error_i
);
  localparam logic [3:0] OP_WRITE       = 4'd0;
  localparam logic [3:0] OP_READ        = 4'd1;
  localparam logic [3:0] OP_GER16       = 4'd2;
  localparam logic [3:0] OP_GER16R      = 4'd3;
  localparam logic [3:0] OP_BGER16R     = 4'd4;
  localparam logic [3:0] OP_ATRF_LOAD   = 4'd5;
  localparam logic [3:0] OP_AGER16R     = 4'd6;
  localparam logic [3:0] OP_ATRF_LOAD16 = 4'd7;
  localparam logic [3:0] OP_AGER16X16   = 4'd8;
  localparam logic [3:0] OP_AGER16R2    = 4'd9;
  localparam logic [3:0] OP_SETUP       = 4'd10;
  localparam logic [3:0] OP_RUN         = 4'd11;
  localparam logic [3:0] OP_XMEM_STORE  = 4'd12;

  typedef enum logic [4:0] {
    S_IDLE,
    S_CLEAR,
    S_A_REQ,
    S_A_WAIT,
    S_A_STORE,
    S_A_NEXT_WAIT,
    S_B0_REQ,
    S_B0_WAIT,
    S_B1_REQ,
    S_B1_WAIT,
    S_B2_REQ,
    S_B2_WAIT,
    S_B3_REQ,
    S_B3_WAIT,
    S_GER,
    S_WAIT_PAIR,
    S_STORE_REQ,
    S_STORE_WAIT,
    S_DONE,
    S_ERROR
  } state_t;
  typedef enum logic [3:0] {
    PF_IDLE,
    PF_B0_REQ,
    PF_B0_WAIT,
    PF_B1_REQ,
    PF_B1_WAIT,
    PF_B2_REQ,
    PF_B2_WAIT,
    PF_B3_REQ,
    PF_B3_WAIT,
    PF_READY,
    PF_ERROR
  } pf_state_t;

  state_t state_q;
  pf_state_t pf_state_q;
  logic [XLEN-1:0] a_base_q, b_base_q;
  logic [XLEN-1:0] store_addr_q, store_data_q;
  logic [5:0] n_q;
  logic two_words_q;
  logic [5:0] clear_q, a_idx_q, k_q, pf_k_q;
  logic [4:0] row_q, a_wrow_q;
  logic a_wword_q;
  logic [1:0] a_phase_q;
  logic [63:0] a_wdata_q;
  logic a_next_pending_q, a_next_valid_q;
  logic [63:0] a_next_data_q;
  logic [5:0] a_next_idx;
  logic a_bg_req, a_mem_req;
  logic [5:0] b_req_k;
  logic b_req_word1;
  logic [5:0] pf_req_k;
  logic pf_req_word1;
  logic [63:0] b0_q, b1_q, b2_q, b3_q;
  logic [63:0] pf_b0_q, pf_b1_q, pf_b2_q, pf_b3_q;
  logic [127:0] scalars_q, scalars1_q;
  logic [127:0] pf_scalars_q, pf_scalars1_q;
  logic [127:0] atrf_scalars0, atrf_scalars1;
  logic [4:0] atrf_raddr0, atrf_raddr1;
  logic atrf_we;
  logic [4:0] atrf_wrow;
  logic atrf_wword;
  logic [1:0] atrf_wphase;
  logic [63:0] atrf_wdata;

  logic [63:0] broad0, broad1;
  logic [63:0] product0, product1, product2, product3;
  logic [3:0] scalar0, scalar1;
  logic [4:0] c_bank_addr;
  logic [63:0] c_even_word, c_odd_word;
  logic c_even_we, c_odd_we;
  logic [63:0] c_even_wdata, c_odd_wdata;
  logic [5:0] read_linear;
  logic [63:0] read_data;
  logic main_mem_req, pf_mem_req;
  logic pf_start, pf_consume;

  logic auto_mode_q;
  logic legacy_busy_q, legacy_load_q;
  logic [2:0] legacy_b_base_q, legacy_words_m1_q, legacy_word_q;
  logic [3:0] legacy_rows_m1_q, legacy_row_q;
  logic [2:0] legacy_row_words_m1_q;
  logic [5:0] legacy_c_addr_q, legacy_pair_c_addr_q;
  logic [63:0] legacy_scalars_q, legacy_pair_scalars_q;
  logic [63:0] legacy_pair_b_q;
  logic legacy_direct_q, legacy_pair_q, legacy_pair_phase_q;
  logic [3:0] legacy_load_row_q;
  logic legacy_load_word_q;
  logic [1:0] legacy_load_phase_q;
  logic [63:0] legacy_load_data_q;
  logic [4:0] legacy_atrf_raddr;
  logic [2:0] legacy_b_raddr;
  logic [63:0] legacy_b_word, legacy_b_operand;
  logic legacy_b_we;
  logic [63:0] legacy_b_wdata;
  logic legacy_write_pending_q, legacy_write_c_q;
  logic [5:0] legacy_write_linear_q;
  logic [2:0] legacy_write_b_addr_q;
  logic [63:0] legacy_write_data_q;
  logic legacy_read_pending_q, legacy_read_auto_q;
  logic legacy_read_c_q, legacy_read_counter_q;
  logic [1:0] legacy_read_counter_sel_q;
  logic [5:0] legacy_read_linear_q;
  logic [2:0] legacy_read_b_addr_q;
  logic [6:0] legacy_tile_words;
  logic [3:0] legacy_words_count;
  logic [4:0] legacy_rows_count;
  logic [63:0] mul_b0;
  logic [3:0] mul_scalar;
  logic auto_rsp_valid_q, legacy_rsp_valid_q;
  logic [63:0] auto_rsp_data_q, legacy_rsp_data_q;
  logic [31:0] a_load_cycles_q, ger_cycles_q, pf_wait_cycles_q;

  assign busy_o = state_q != S_IDLE || legacy_busy_q || legacy_load_q ||
                  legacy_write_pending_q || legacy_read_pending_q;
  assign cmd_ready_o = state_q == S_IDLE && !legacy_busy_q &&
                       !legacy_load_q && !legacy_write_pending_q &&
                       !legacy_read_pending_q;
  assign rsp_valid_o = auto_rsp_valid_q | legacy_rsp_valid_q;
  assign rsp_data_o = auto_rsp_valid_q ? auto_rsp_data_q :
                                         legacy_rsp_data_q;
  assign atrf_we = state_q == S_A_STORE || legacy_load_q;
  assign atrf_wrow = legacy_load_q ? {1'b0, legacy_load_row_q} : a_wrow_q;
  assign atrf_wword = legacy_load_q ? legacy_load_word_q : a_wword_q;
  assign atrf_wphase = legacy_load_q ? legacy_load_phase_q : a_phase_q;
  assign atrf_wdata = legacy_load_q ? legacy_load_data_q : a_wdata_q;
  assign legacy_atrf_raddr =
      (cmd_op_i == OP_AGER16X16 || cmd_op_i == OP_AGER16R2) ?
      b_i[21:17] : b_i[20:16];
  assign atrf_raddr0 = pf_state_q != PF_IDLE ? pf_k_q[4:0] :
                       state_q != S_IDLE ? k_q[4:0] :
                                           legacy_atrf_raddr;
  assign atrf_raddr1 = pf_state_q != PF_IDLE ? pf_k_q[4:0] + 1'b1 :
                       state_q != S_IDLE ? k_q[4:0] + 1'b1 :
                                           legacy_atrf_raddr;

  xmpk_atrf4_32 i_atrf (
      .clk_i(clk_i),
      .we_i(atrf_we),
      .wrow_i(atrf_wrow),
      .wword_i(atrf_wword),
      .wphase_i(atrf_wphase),
      .wdata_i(atrf_wdata),
      .raddr0_i(atrf_raddr0),
      .raddr1_i(atrf_raddr1),
      .scalars0_o(atrf_scalars0),
      .scalars1_o(atrf_scalars1)
  );

  assign read_linear = auto_mode_q ? {b_i[6:4], b_i[2:0]} :
                                     {1'b0, b_i[5:4], b_i[2:0]};
  assign c_bank_addr = (state_q == S_GER) ?
                       (two_words_q ? row_q : {1'b0, row_q[4:1]}) :
                       (state_q == S_CLEAR) ?
                       (two_words_q ? clear_q[4:0] :
                                      {1'b0, clear_q[4:1]}) :
                       legacy_busy_q ? legacy_c_addr_q[5:1] :
                       (legacy_write_pending_q && legacy_write_c_q) ?
                       legacy_write_linear_q[5:1] :
                       (legacy_read_pending_q && legacy_read_c_q) ?
                       legacy_read_linear_q[5:1] : '0;
  assign read_data = legacy_read_linear_q[0] ? c_odd_word : c_even_word;

  xmpk_trf_bank #(.XLEN(64), .ADDR_W(5)) i_c_even (
      .clk_i(clk_i), .we_i(c_even_we), .addr_i(c_bank_addr),
      .wdata_i(c_even_wdata), .rdata_o(c_even_word)
  );
  xmpk_trf_bank #(.XLEN(64), .ADDR_W(5)) i_c_odd (
      .clk_i(clk_i), .we_i(c_odd_we), .addr_i(c_bank_addr),
      .wdata_i(c_odd_wdata), .rdata_o(c_odd_word)
  );

  assign legacy_b_raddr = legacy_write_pending_q && !legacy_write_c_q ?
                          legacy_write_b_addr_q :
                          legacy_read_pending_q && !legacy_read_c_q ?
                          legacy_read_b_addr_q :
                          legacy_busy_q ?
                          legacy_b_base_q + legacy_word_q :
                          '0;
  assign legacy_b_wdata = legacy_write_pending_q ?
                          legacy_write_data_q : a_i;
  assign legacy_b_we = (legacy_write_pending_q && !legacy_write_c_q) ||
                       (cmd_valid_i && cmd_ready_o &&
                       (cmd_op_i == OP_BGER16R ||
                        cmd_op_i == OP_AGER16R ||
                        cmd_op_i == OP_AGER16X16 ||
                        cmd_op_i == OP_AGER16R2));
  xmpk_trf_bank #(.XLEN(64), .ADDR_W(3)) i_legacy_b (
      .clk_i(clk_i), .we_i(legacy_b_we), .addr_i(legacy_b_raddr),
      .wdata_i(legacy_b_wdata), .rdata_o(legacy_b_word)
  );

  assign legacy_b_operand =
      legacy_pair_q && legacy_pair_phase_q ? legacy_pair_b_q :
                                             legacy_b_word;
  assign mul_b0 = legacy_busy_q ? legacy_b_operand : b0_q;
  assign mul_scalar = legacy_busy_q ? legacy_scalars_q[3:0] :
                                     scalars_q[3:0];
  assign scalar0 = mul_scalar;
  assign scalar1 = scalars1_q[3:0];
  always_comb begin
    for (int lane = 0; lane < 16; lane++) begin
      broad0[4*lane +: 4] = scalar0;
      broad1[4*lane +: 4] = scalar1;
    end
  end
  for (genvar lane = 0; lane < 16; lane++) begin : g_dual_mul
    gf2n_mul #(.W(4)) i_mul0 (
        .a_i(broad0[4*lane +: 4]),
        .b_i(mul_b0[4*lane +: 4]),
        .poly_i(4'h3),
        .z_o(product0[4*lane +: 4])
    );
    gf2n_mul #(.W(4)) i_mul1 (
        .a_i(broad0[4*lane +: 4]),
        .b_i(b1_q[4*lane +: 4]),
        .poly_i(4'h3),
        .z_o(product1[4*lane +: 4])
    );
    gf2n_mul #(.W(4)) i_mul2 (
        .a_i(broad1[4*lane +: 4]),
        .b_i(b2_q[4*lane +: 4]),
        .poly_i(4'h3),
        .z_o(product2[4*lane +: 4])
    );
    gf2n_mul #(.W(4)) i_mul3 (
        .a_i(broad1[4*lane +: 4]),
        .b_i(b3_q[4*lane +: 4]),
        .poly_i(4'h3),
        .z_o(product3[4*lane +: 4])
    );
  end

  always_comb begin
    c_even_we = 1'b0;
    c_odd_we = 1'b0;
    c_even_wdata = '0;
    c_odd_wdata = '0;
    if (state_q == S_CLEAR) begin
      if (two_words_q) begin
        c_even_we = 1'b1;
        c_odd_we = 1'b1;
      end else if (clear_q[0]) begin
        c_odd_we = 1'b1;
      end else begin
        c_even_we = 1'b1;
      end
    end else if (state_q == S_GER) begin
      if (two_words_q) begin
        c_even_we = 1'b1;
        c_odd_we = 1'b1;
        c_even_wdata = c_even_word ^ product0 ^ product2;
        c_odd_wdata = c_odd_word ^ product1 ^ product3;
      end else if (row_q[0]) begin
        c_odd_we = 1'b1;
        c_odd_wdata = c_odd_word ^ product0 ^ product2;
      end else begin
        c_even_we = 1'b1;
        c_even_wdata = c_even_word ^ product0 ^ product2;
      end
    end else if (legacy_busy_q) begin
      if (legacy_c_addr_q[0]) begin
        c_odd_we = 1'b1;
        c_odd_wdata = c_odd_word ^ product0;
      end else begin
        c_even_we = 1'b1;
        c_even_wdata = c_even_word ^ product0;
      end
    end else if (legacy_write_pending_q && legacy_write_c_q) begin
      if (legacy_write_linear_q[0]) begin
        c_odd_we = 1'b1;
        c_odd_wdata = legacy_write_data_q;
      end else begin
        c_even_we = 1'b1;
        c_even_wdata = legacy_write_data_q;
      end
    end
  end

  assign legacy_words_count = {1'b0, legacy_words_m1_q} + 1'b1;
  assign legacy_rows_count = {1'b0, legacy_rows_m1_q} + 1'b1;
  assign legacy_tile_words = legacy_rows_count *
                             (legacy_pair_q ? 4'd2 :
                                              legacy_words_count);

  assign a_next_idx = a_idx_q + 1'b1;
  assign a_bg_req = state_q == S_A_STORE &&
                    a_idx_q != (two_words_q ? 6'd63 : 6'd15) &&
                    !a_next_pending_q && !a_next_valid_q;
  assign a_mem_req = state_q == S_A_REQ || a_bg_req;
  assign main_mem_req = a_mem_req ||
                        state_q == S_B0_REQ ||
                         state_q == S_B1_REQ ||
                         state_q == S_B2_REQ ||
                         state_q == S_B3_REQ ||
                         state_q == S_STORE_REQ;
  assign pf_mem_req = pf_state_q == PF_B0_REQ ||
                      pf_state_q == PF_B1_REQ ||
                      pf_state_q == PF_B2_REQ ||
                      pf_state_q == PF_B3_REQ;
  assign b_req_k = (state_q == S_B2_REQ || state_q == S_B3_REQ) ?
                   k_q + 1'b1 : k_q;
  assign b_req_word1 = state_q == S_B1_REQ || state_q == S_B3_REQ;
  assign pf_req_k = (pf_state_q == PF_B2_REQ ||
                     pf_state_q == PF_B3_REQ) ?
                    pf_k_q + 1'b1 : pf_k_q;
  assign pf_req_word1 = pf_state_q == PF_B1_REQ ||
                        pf_state_q == PF_B3_REQ;
  assign mem_req_valid_o = main_mem_req | pf_mem_req;
  assign mem_req_addr_o = state_q == S_STORE_REQ ?
                          store_addr_q :
                          a_mem_req ?
                          a_base_q +
                          (XLEN'(a_bg_req ? a_next_idx : a_idx_q) << 3) :
                          main_mem_req ?
                          b_base_q +
                          ((XLEN'(b_req_k) << (two_words_q ? 4 : 3)) +
                           (b_req_word1 ? XLEN'(8) : '0)) :
                          b_base_q +
                          ((XLEN'(pf_req_k) << (two_words_q ? 4 : 3)) +
                           (pf_req_word1 ? XLEN'(8) : '0));
  assign mem_req_write_o = state_q == S_STORE_REQ;
  assign mem_req_wdata_o = store_data_q;
  assign mem_req_kill_o = 1'b0;

  assign pf_start = mem_rsp_valid_i && !mem_rsp_error_i &&
                    k_q + 2 < n_q &&
                    ((!two_words_q && state_q == S_B2_WAIT) ||
                     (two_words_q && state_q == S_B3_WAIT));
  assign pf_consume = state_q == S_WAIT_PAIR &&
                      pf_state_q == PF_READY;

  always_ff @(posedge clk_i or negedge rst_ni) begin
    if (!rst_ni) begin
      pf_state_q <= PF_IDLE;
      pf_k_q <= '0;
      pf_b0_q <= '0;
      pf_b1_q <= '0;
      pf_b2_q <= '0;
      pf_b3_q <= '0;
      pf_scalars_q <= '0;
      pf_scalars1_q <= '0;
    end else begin
      if (pf_start) begin
        pf_k_q <= k_q + 2'd2;
        pf_state_q <= PF_B0_REQ;
      end else if (pf_consume) begin
        if (pf_k_q + 2 < n_q) begin
          pf_k_q <= pf_k_q + 2'd2;
          pf_state_q <= PF_B0_REQ;
        end else begin
          pf_state_q <= PF_IDLE;
        end
      end else begin
        unique case (pf_state_q)
          PF_B0_REQ:
            if (mem_req_ready_i) pf_state_q <= PF_B0_WAIT;
          PF_B0_WAIT:
            if (mem_rsp_valid_i) begin
              if (mem_rsp_error_i) begin
                pf_state_q <= PF_ERROR;
              end else begin
                pf_b0_q <= mem_rsp_data_i;
                if (two_words_q) begin
                  pf_state_q <= PF_B1_REQ;
                end else begin
                    pf_b1_q <= '0;
                    pf_state_q <= PF_B2_REQ;
                end
              end
            end
          PF_B1_REQ:
            if (mem_req_ready_i) pf_state_q <= PF_B1_WAIT;
          PF_B1_WAIT:
            if (mem_rsp_valid_i) begin
              if (mem_rsp_error_i) begin
                pf_state_q <= PF_ERROR;
              end else begin
                pf_b1_q <= mem_rsp_data_i;
                  pf_state_q <= PF_B2_REQ;
                end
              end
          PF_B2_REQ:
            if (mem_req_ready_i) pf_state_q <= PF_B2_WAIT;
          PF_B2_WAIT:
            if (mem_rsp_valid_i) begin
              if (mem_rsp_error_i) begin
                pf_state_q <= PF_ERROR;
              end else begin
                pf_b2_q <= mem_rsp_data_i;
                if (two_words_q) begin
                  pf_state_q <= PF_B3_REQ;
                end else begin
                  pf_b3_q <= '0;
                pf_scalars_q <= atrf_scalars0;
                  pf_scalars1_q <= atrf_scalars1;
                pf_state_q <= PF_READY;
              end
            end
            end
          PF_B3_REQ:
            if (mem_req_ready_i) pf_state_q <= PF_B3_WAIT;
          PF_B3_WAIT:
            if (mem_rsp_valid_i) begin
              if (mem_rsp_error_i) begin
                pf_state_q <= PF_ERROR;
              end else begin
                pf_b3_q <= mem_rsp_data_i;
                pf_scalars_q <= atrf_scalars0;
                pf_scalars1_q <= atrf_scalars1;
                pf_state_q <= PF_READY;
              end
            end
          PF_ERROR:
            if (state_q == S_IDLE) pf_state_q <= PF_IDLE;
          default: begin
          end
        endcase
      end
      if (state_q == S_DONE || state_q == S_ERROR)
        pf_state_q <= PF_IDLE;
    end
  end

  always_ff @(posedge clk_i or negedge rst_ni) begin
    if (!rst_ni) begin
      state_q <= S_IDLE;
      a_base_q <= '0;
      b_base_q <= '0;
      store_addr_q <= '0;
      store_data_q <= '0;
      n_q <= '0;
      two_words_q <= 1'b0;
      clear_q <= '0;
      a_idx_q <= '0;
      k_q <= '0;
      row_q <= '0;
      a_wrow_q <= '0;
      a_wword_q <= 1'b0;
      a_phase_q <= '0;
      a_wdata_q <= '0;
      a_next_pending_q <= 1'b0;
      a_next_valid_q <= 1'b0;
      a_next_data_q <= '0;
      b0_q <= '0;
      b1_q <= '0;
      b2_q <= '0;
      b3_q <= '0;
      scalars_q <= '0;
      scalars1_q <= '0;
      a_load_cycles_q <= '0;
      ger_cycles_q <= '0;
      pf_wait_cycles_q <= '0;
      auto_rsp_valid_q <= 1'b0;
      auto_rsp_data_q <= '0;
    end else begin
      auto_rsp_valid_q <= 1'b0;
      if (state_q == S_A_REQ || state_q == S_A_WAIT ||
          state_q == S_A_STORE || state_q == S_A_NEXT_WAIT)
        a_load_cycles_q <= a_load_cycles_q + 1'b1;
      if (state_q == S_GER)
        ger_cycles_q <= ger_cycles_q + 1'b1;
      if (state_q == S_WAIT_PAIR)
        pf_wait_cycles_q <= pf_wait_cycles_q + 1'b1;
      unique case (state_q)
        S_IDLE: begin
          if (legacy_read_pending_q && legacy_read_auto_q) begin
            if (legacy_read_counter_q) begin
              auto_rsp_valid_q <= 1'b1;
              unique case (legacy_read_counter_sel_q)
                2'd0: auto_rsp_data_q <= {{32{1'b0}},
                                           a_load_cycles_q};
                2'd1: auto_rsp_data_q <= {{32{1'b0}},
                                           ger_cycles_q};
                default: auto_rsp_data_q <= {{32{1'b0}},
                                              pf_wait_cycles_q};
              endcase
            end else if (legacy_read_c_q) begin
              auto_rsp_valid_q <= 1'b1;
              auto_rsp_data_q <= read_data;
            end
          end else if (cmd_valid_i) begin
            unique case (cmd_op_i)
              OP_SETUP: begin
                a_base_q <= a_i;
                b_base_q <= b_i;
              end
              OP_RUN: begin
                if (a_i == XLEN'(16) || a_i == XLEN'(32)) begin
                  n_q <= a_i[5:0];
                  two_words_q <= a_i == XLEN'(32);
                  clear_q <= '0;
                a_next_pending_q <= 1'b0;
                a_next_valid_q <= 1'b0;
                a_load_cycles_q <= '0;
                ger_cycles_q <= '0;
                pf_wait_cycles_q <= '0;
                  state_q <= S_CLEAR;
                end else begin
                  auto_rsp_valid_q <= 1'b1;
                  auto_rsp_data_q <= {1'b1, {(XLEN-2){1'b0}}, 1'b1};
                end
              end
              OP_XMEM_STORE: begin
                store_addr_q <= a_i;
                store_data_q <= b_i;
                state_q <= S_STORE_REQ;
              end
              OP_READ: begin
              end
              default: begin
              end
            endcase
          end
        end
        S_CLEAR: begin
          if (clear_q == n_q - 1'b1) begin
            a_idx_q <= '0;
            state_q <= S_A_REQ;
          end else begin
            clear_q <= clear_q + 1'b1;
          end
        end
        S_A_REQ:
          if (mem_req_ready_i) state_q <= S_A_WAIT;
        S_A_WAIT:
          if (mem_rsp_valid_i) begin
            if (mem_rsp_error_i) begin
              state_q <= S_ERROR;
            end else begin
              a_wdata_q <= mem_rsp_data_i;
              a_wrow_q <= two_words_q ? a_idx_q[5:1] : a_idx_q[4:0];
              a_wword_q <= two_words_q && a_idx_q[0];
              a_phase_q <= '0;
              a_next_pending_q <= 1'b0;
              a_next_valid_q <= 1'b0;
              state_q <= S_A_STORE;
            end
          end
        S_A_STORE: begin
          if (a_bg_req && mem_req_ready_i)
            a_next_pending_q <= 1'b1;
          if (mem_rsp_valid_i && mem_rsp_error_i) begin
            a_next_pending_q <= 1'b0;
            state_q <= S_ERROR;
          end else if (a_phase_q == 2'd3) begin
            if (a_idx_q == (two_words_q ? 6'd63 : 6'd15)) begin
              a_next_pending_q <= 1'b0;
              a_next_valid_q <= 1'b0;
              k_q <= '0;
              state_q <= S_B0_REQ;
            end else if (a_next_valid_q) begin
              a_idx_q <= a_next_idx;
              a_wdata_q <= a_next_data_q;
              a_wrow_q <= two_words_q ?
                          a_next_idx[5:1] : a_next_idx[4:0];
              a_wword_q <= two_words_q && a_next_idx[0];
              a_phase_q <= '0;
              a_next_valid_q <= 1'b0;
            end else if (mem_rsp_valid_i) begin
              a_idx_q <= a_next_idx;
              a_wdata_q <= mem_rsp_data_i;
              a_wrow_q <= two_words_q ?
                          a_next_idx[5:1] : a_next_idx[4:0];
              a_wword_q <= two_words_q && a_next_idx[0];
              a_phase_q <= '0;
              a_next_pending_q <= 1'b0;
            end else if (a_next_pending_q ||
                         (a_bg_req && mem_req_ready_i)) begin
              state_q <= S_A_NEXT_WAIT;
            end
          end else begin
            a_phase_q <= a_phase_q + 1'b1;
            if (mem_rsp_valid_i) begin
              a_next_pending_q <= 1'b0;
              a_next_valid_q <= 1'b1;
              a_next_data_q <= mem_rsp_data_i;
            end
          end
        end
        S_A_NEXT_WAIT:
          if (mem_rsp_valid_i) begin
            a_next_pending_q <= 1'b0;
            if (mem_rsp_error_i) begin
              state_q <= S_ERROR;
            end else begin
              a_idx_q <= a_next_idx;
              a_wdata_q <= mem_rsp_data_i;
              a_wrow_q <= two_words_q ?
                          a_next_idx[5:1] : a_next_idx[4:0];
              a_wword_q <= two_words_q && a_next_idx[0];
              a_phase_q <= '0;
              state_q <= S_A_STORE;
            end
          end
        S_B0_REQ:
          if (mem_req_ready_i) state_q <= S_B0_WAIT;
        S_B0_WAIT:
          if (mem_rsp_valid_i) begin
            if (mem_rsp_error_i) begin
              state_q <= S_ERROR;
            end else begin
              b0_q <= mem_rsp_data_i;
              if (two_words_q) begin
                state_q <= S_B1_REQ;
              end else begin
                b1_q <= '0;
                  state_q <= S_B2_REQ;
              end
            end
          end
        S_B1_REQ:
          if (mem_req_ready_i) state_q <= S_B1_WAIT;
        S_B1_WAIT:
          if (mem_rsp_valid_i) begin
            if (mem_rsp_error_i) begin
              state_q <= S_ERROR;
            end else begin
              b1_q <= mem_rsp_data_i;
                state_q <= S_B2_REQ;
            end
          end
        S_B2_REQ:
          if (mem_req_ready_i) state_q <= S_B2_WAIT;
        S_B2_WAIT:
          if (mem_rsp_valid_i) begin
            if (mem_rsp_error_i) begin
              state_q <= S_ERROR;
            end else begin
              b2_q <= mem_rsp_data_i;
              if (two_words_q) begin
                state_q <= S_B3_REQ;
              end else begin
                b3_q <= '0;
                scalars_q <= atrf_scalars0;
                scalars1_q <= atrf_scalars1;
                row_q <= '0;
                state_q <= S_GER;
              end
            end
          end
        S_B3_REQ:
          if (mem_req_ready_i) state_q <= S_B3_WAIT;
        S_B3_WAIT:
          if (mem_rsp_valid_i) begin
            if (mem_rsp_error_i) begin
              state_q <= S_ERROR;
            end else begin
              b3_q <= mem_rsp_data_i;
              scalars_q <= atrf_scalars0;
              scalars1_q <= atrf_scalars1;
              row_q <= '0;
              state_q <= S_GER;
            end
          end
        S_GER: begin
          if (row_q == n_q - 1'b1) begin
            if (k_q + 2 >= n_q) begin
              state_q <= S_DONE;
            end else begin
              state_q <= S_WAIT_PAIR;
            end
          end else begin
            row_q <= row_q + 1'b1;
            scalars_q <= {scalars_q[3:0], scalars_q[127:4]};
            scalars1_q <= {scalars1_q[3:0], scalars1_q[127:4]};
          end
        end
        S_WAIT_PAIR: begin
          if (pf_state_q == PF_ERROR) begin
            state_q <= S_ERROR;
          end else if (pf_consume) begin
            k_q <= pf_k_q;
            b0_q <= pf_b0_q;
            b1_q <= pf_b1_q;
            b2_q <= pf_b2_q;
            b3_q <= pf_b3_q;
            scalars_q <= pf_scalars_q;
            scalars1_q <= pf_scalars1_q;
            row_q <= '0;
            state_q <= S_GER;
          end
        end
        S_STORE_REQ:
          if (mem_req_ready_i) state_q <= S_STORE_WAIT;
        S_STORE_WAIT:
          if (mem_rsp_valid_i) begin
            auto_rsp_valid_q <= 1'b1;
            auto_rsp_data_q <= mem_rsp_error_i ?
                               {1'b1, {(XLEN-1){1'b0}}} : '0;
            state_q <= S_IDLE;
          end
        S_DONE: begin
            auto_rsp_valid_q <= 1'b1;
            auto_rsp_data_q <= XLEN'(n_q) * XLEN'(n_q);
          state_q <= S_IDLE;
        end
        S_ERROR: begin
            auto_rsp_valid_q <= 1'b1;
            auto_rsp_data_q <= {1'b1, {(XLEN-1){1'b0}}};
          state_q <= S_IDLE;
        end
        default: state_q <= S_ERROR;
      endcase
    end
  end

  always_ff @(posedge clk_i or negedge rst_ni) begin
    if (!rst_ni) begin
      auto_mode_q <= 1'b0;
      legacy_busy_q <= 1'b0;
      legacy_load_q <= 1'b0;
      legacy_b_base_q <= '0;
      legacy_words_m1_q <= '0;
      legacy_word_q <= '0;
      legacy_rows_m1_q <= '0;
      legacy_row_q <= '0;
      legacy_row_words_m1_q <= '0;
      legacy_c_addr_q <= '0;
      legacy_pair_c_addr_q <= '0;
      legacy_scalars_q <= '0;
      legacy_pair_scalars_q <= '0;
      legacy_pair_b_q <= '0;
      legacy_direct_q <= 1'b0;
      legacy_pair_q <= 1'b0;
      legacy_pair_phase_q <= 1'b0;
      legacy_load_row_q <= '0;
      legacy_load_word_q <= 1'b0;
      legacy_load_phase_q <= '0;
      legacy_load_data_q <= '0;
      legacy_write_pending_q <= 1'b0;
      legacy_write_c_q <= 1'b0;
      legacy_write_linear_q <= '0;
      legacy_write_b_addr_q <= '0;
      legacy_write_data_q <= '0;
      legacy_read_pending_q <= 1'b0;
      legacy_read_auto_q <= 1'b0;
      legacy_read_c_q <= 1'b0;
      legacy_read_counter_q <= 1'b0;
      legacy_read_counter_sel_q <= '0;
      legacy_read_linear_q <= '0;
      legacy_read_b_addr_q <= '0;
      legacy_rsp_valid_q <= 1'b0;
      legacy_rsp_data_q <= '0;
    end else begin
      legacy_rsp_valid_q <= 1'b0;
      legacy_write_pending_q <= 1'b0;
      legacy_read_pending_q <= 1'b0;
      if (cmd_valid_i && cmd_ready_o && cmd_op_i == OP_SETUP)
        auto_mode_q <= 1'b1;
      else if (cmd_valid_i && cmd_ready_o && cmd_op_i == OP_WRITE)
        auto_mode_q <= 1'b0;

      if (legacy_read_pending_q) begin
        if (!legacy_read_auto_q) begin
          legacy_rsp_valid_q <= 1'b1;
          legacy_rsp_data_q <= legacy_read_c_q ?
                               read_data : legacy_b_word;
        end
      end else if (legacy_load_q) begin
        if (legacy_load_phase_q == 2'd3) begin
          legacy_load_q <= 1'b0;
          legacy_load_phase_q <= '0;
          legacy_rsp_valid_q <= 1'b1;
          legacy_rsp_data_q <= XLEN'(16);
        end else begin
          legacy_load_phase_q <= legacy_load_phase_q + 1'b1;
        end
      end else if (legacy_busy_q) begin
        if (legacy_word_q == legacy_words_m1_q) begin
          legacy_word_q <= '0;
          if (legacy_row_q == legacy_rows_m1_q) begin
            if (legacy_pair_q && !legacy_pair_phase_q) begin
              legacy_pair_phase_q <= 1'b1;
              legacy_row_q <= '0;
              legacy_scalars_q <= legacy_pair_scalars_q;
              legacy_c_addr_q <= legacy_pair_c_addr_q;
            end else begin
              legacy_busy_q <= 1'b0;
              legacy_rsp_valid_q <= 1'b1;
              legacy_rsp_data_q <= {{(XLEN-7){1'b0}},
                                    legacy_tile_words};
            end
          end else begin
            legacy_row_q <= legacy_row_q + 1'b1;
            legacy_scalars_q <= {legacy_scalars_q[3:0],
                                 legacy_scalars_q[63:4]};
            legacy_c_addr_q <= legacy_direct_q ?
                              legacy_c_addr_q +
                              {3'b0, legacy_row_words_m1_q} + 1'b1 :
                              legacy_c_addr_q + 1'b1;
          end
        end else begin
          legacy_word_q <= legacy_word_q + 1'b1;
          legacy_c_addr_q <= legacy_c_addr_q + 1'b1;
        end
      end else if (cmd_valid_i && cmd_ready_o) begin
        unique case (cmd_op_i)
          OP_WRITE: begin
            legacy_write_pending_q <= 1'b1;
            legacy_write_c_q <= b_i[3];
            legacy_write_linear_q <= {1'b0, b_i[5:4], b_i[2:0]};
            legacy_write_b_addr_q <= b_i[2:0];
            legacy_write_data_q <= a_i;
          end
          OP_READ: begin
            legacy_read_pending_q <= 1'b1;
            legacy_read_auto_q <= auto_mode_q;
            legacy_read_c_q <= b_i[3];
            legacy_read_counter_q <= b_i[7];
            legacy_read_counter_sel_q <= b_i[1:0];
            legacy_read_linear_q <= read_linear;
            legacy_read_b_addr_q <= b_i[2:0];
          end
          OP_GER16: begin
            legacy_scalars_q <= {8{a_i[4*b_i[3:0] +: 4]}};
            legacy_b_base_q <= b_i[6:4];
            legacy_words_m1_q <= b_i[14:12];
            legacy_row_words_m1_q <= b_i[14:12];
            legacy_rows_m1_q <= '0;
            legacy_word_q <= '0;
            legacy_row_q <= '0;
            legacy_c_addr_q <= {2'b0, b_i[11:8]};
            legacy_direct_q <= 1'b0;
            legacy_pair_q <= 1'b0;
            legacy_pair_phase_q <= 1'b0;
            legacy_busy_q <= 1'b1;
          end
          OP_GER16R: begin
            legacy_scalars_q <= {{32{1'b0}}, a_i[31:0]};
            legacy_b_base_q <= b_i[10:8];
            legacy_words_m1_q <= b_i[2:0];
            legacy_row_words_m1_q <= b_i[2:0];
            legacy_rows_m1_q <= b_i[6:4];
            legacy_word_q <= '0;
            legacy_row_q <= '0;
            legacy_c_addr_q <= {2'b0, b_i[15:12]};
            legacy_direct_q <= 1'b0;
            legacy_pair_q <= 1'b0;
            legacy_pair_phase_q <= 1'b0;
            legacy_busy_q <= 1'b1;
          end
          OP_BGER16R: begin
            legacy_scalars_q <= {{32{1'b0}}, c_i[31:0]};
            legacy_b_base_q <= b_i[2:0];
            legacy_words_m1_q <= '0;
            legacy_row_words_m1_q <= b_i[6:4];
            legacy_rows_m1_q <= b_i[10:8];
            legacy_word_q <= '0;
            legacy_row_q <= '0;
            legacy_c_addr_q <= {2'b0, b_i[15:12]} +
                               {3'b0, b_i[2:0]};
            legacy_direct_q <= 1'b1;
            legacy_pair_q <= 1'b0;
            legacy_pair_phase_q <= 1'b0;
            legacy_busy_q <= 1'b1;
          end
          OP_ATRF_LOAD, OP_ATRF_LOAD16: begin
            legacy_load_data_q <= a_i;
            legacy_load_row_q <= cmd_op_i == OP_ATRF_LOAD ?
                                 {1'b0, b_i[2:0]} : b_i[3:0];
            legacy_load_word_q <= cmd_op_i == OP_ATRF_LOAD ?
                                  b_i[3] : b_i[4];
            legacy_load_phase_q <= '0;
            legacy_load_q <= 1'b1;
          end
          OP_AGER16R, OP_AGER16X16, OP_AGER16R2: begin
            legacy_scalars_q <= cmd_op_i == OP_AGER16R ?
                                {{32{1'b0}}, atrf_scalars0[31:0]} :
                                atrf_scalars0[63:0];
            legacy_pair_scalars_q <= atrf_scalars0[63:0];
            legacy_pair_b_q <= c_i;
            legacy_b_base_q <= b_i[2:0];
            legacy_words_m1_q <= '0;
            legacy_row_words_m1_q <= b_i[6:4];
            legacy_rows_m1_q <= cmd_op_i == OP_AGER16R ?
                                {1'b0, b_i[10:8]} : b_i[11:8];
            legacy_word_q <= '0;
            legacy_row_q <= '0;
            legacy_c_addr_q <= cmd_op_i == OP_AGER16R ?
                               {2'b0, b_i[15:12]} +
                               {3'b0, b_i[2:0]} :
                               {1'b0, b_i[16:12]} +
                               {3'b0, b_i[2:0]};
            legacy_pair_c_addr_q <= {1'b0, b_i[16:12]} +
                                    {3'b0, b_i[2:0]} + 1'b1;
            legacy_direct_q <= 1'b1;
            legacy_pair_q <= cmd_op_i == OP_AGER16R2;
            legacy_pair_phase_q <= 1'b0;
            legacy_busy_q <= 1'b1;
          end
          default: begin
          end
        endcase
      end
    end
  end
endmodule

module xmpk_ger_trf #(
    parameter int unsigned XLEN = 64
) (
    input  logic            clk_i,
    input  logic            rst_ni,
    input  logic            cmd_valid_i,
    output logic            cmd_ready_o,
    input  logic [3:0]      cmd_op_i,
    input  logic [XLEN-1:0] a_i,
    input  logic [XLEN-1:0] b_i,
    input  logic [XLEN-1:0] c_i,
    output logic            rsp_valid_o,
    output logic [XLEN-1:0] rsp_data_o,
    output logic            busy_o,
    output logic            mem_req_valid_o,
    input  logic            mem_req_ready_i,
    output logic [XLEN-1:0] mem_req_addr_o,
    output logic            mem_req_write_o,
    output logic [XLEN-1:0] mem_req_wdata_o,
    output logic            mem_req_kill_o,
    input  logic            mem_rsp_valid_i,
    input  logic [XLEN-1:0] mem_rsp_data_i,
    input  logic            mem_rsp_error_i
);
  xmpk_auto_aggressive #(.XLEN(XLEN)) i_aggressive (
      .clk_i(clk_i),
      .rst_ni(rst_ni),
      .cmd_valid_i(cmd_valid_i),
      .cmd_ready_o(cmd_ready_o),
      .cmd_op_i(cmd_op_i),
      .a_i(a_i),
      .b_i(b_i),
      .c_i(c_i),
      .rsp_valid_o(rsp_valid_o),
      .rsp_data_o(rsp_data_o),
      .busy_o(busy_o),
      .mem_req_valid_o(mem_req_valid_o),
      .mem_req_ready_i(mem_req_ready_i),
      .mem_req_addr_o(mem_req_addr_o),
      .mem_req_write_o(mem_req_write_o),
      .mem_req_wdata_o(mem_req_wdata_o),
      .mem_req_kill_o(mem_req_kill_o),
      .mem_rsp_valid_i(mem_rsp_valid_i),
      .mem_rsp_data_i(mem_rsp_data_i),
      .mem_rsp_error_i(mem_rsp_error_i)
  );
endmodule

module xmpk_ger_trf_legacy #(
    parameter int unsigned XLEN = 64
) (
    input  logic            clk_i,
    input  logic            rst_ni,
    input  logic            cmd_valid_i,
    output logic            cmd_ready_o,
    input  logic [3:0]      cmd_op_i,
    input  logic [XLEN-1:0] a_i,
    input  logic [XLEN-1:0] b_i,
    input  logic [XLEN-1:0] c_i,
    output logic            rsp_valid_o,
    output logic [XLEN-1:0] rsp_data_o,
    output logic            busy_o,
    output logic            mem_req_valid_o,
    input  logic            mem_req_ready_i,
    output logic [XLEN-1:0] mem_req_addr_o,
    output logic            mem_req_write_o,
    output logic [XLEN-1:0] mem_req_wdata_o,
    output logic            mem_req_kill_o,
    input  logic            mem_rsp_valid_i,
    input  logic [XLEN-1:0] mem_rsp_data_i,
    input  logic            mem_rsp_error_i
);
  localparam logic [3:0] OP_WRITE       = 4'd0;
  localparam logic [3:0] OP_READ        = 4'd1;
  localparam logic [3:0] OP_GER16       = 4'd2;
  localparam logic [3:0] OP_GER16R      = 4'd3;
  localparam logic [3:0] OP_BGER16R     = 4'd4;
  localparam logic [3:0] OP_ATRF_LOAD   = 4'd5;
  localparam logic [3:0] OP_AGER16R     = 4'd6;
  localparam logic [3:0] OP_ATRF_LOAD16 = 4'd7;
  localparam logic [3:0] OP_AGER16X16   = 4'd8;
  localparam logic [3:0] OP_AGER16R2    = 4'd9;
  localparam logic [3:0] OP_GER_SETUP   = 4'd10;
  localparam logic [3:0] OP_GER_RUN     = 4'd11;

  typedef enum logic [3:0] {
    AUTO_IDLE,
    AUTO_CLEAR,
    AUTO_A_REQ,
    AUTO_A_WAIT,
    AUTO_A_STORE,
    AUTO_B0_REQ,
    AUTO_B0_WAIT,
    AUTO_B1_REQ,
    AUTO_B1_WAIT,
    AUTO_GER_WAIT,
    AUTO_DONE,
    AUTO_ERROR
  } auto_state_t;

  typedef enum logic [2:0] {
    PF_IDLE,
    PF_B0_REQ,
    PF_B0_WAIT,
    PF_B1_REQ,
    PF_B1_WAIT,
    PF_READY,
    PF_ERROR
  } pf_state_t;

  logic [2:0] b_base_q;
  logic [4:0] c_base_q;
  logic [2:0] words_m1_q, word_q;
  logic [3:0] rows_m1_q, row_q;
  logic [2:0] row_words_m1_q, c_word_q;
  logic [4:0] c_addr_q;
  logic [4:0] pair_c_addr_q;
  logic [XLEN-1:0] b_pair_q;
  logic       pair_mode_q, pair_phase_q;
  logic [63:0] scalar_pack_q;
  logic       direct_mode_q;
  logic       atrf_load_q;
  logic [3:0] atrf_row_q;
  logic       atrf_word_q;
  logic [3:0] atrf_lane_q;
  logic [XLEN-1:0] atrf_load_data_q;
  logic       atrf_we;
  logic [4:0] atrf_addr;
  logic [63:0] atrf_scalars;
  logic [3:0] scalar_q;
  logic       busy_q;
  auto_state_t auto_state_q;
  logic [XLEN-1:0] auto_a_base_q, auto_b_base_q;
  logic [5:0] auto_n_q;
  logic       auto_two_words_q;
  logic [5:0] auto_clear_idx_q, auto_a_idx_q, auto_k_q;
  logic [XLEN-1:0] auto_b0_q;
  logic       auto_ger_q;
  pf_state_t  pf_state_q;
  logic [5:0] pf_k_q;
  logic [XLEN-1:0] pf_b0_q, pf_b1_q;
  logic [63:0] pf_scalars_q;
  logic pf_start, pf_consume;
  logic main_mem_req, pf_mem_req;

  logic [2:0] b_addr;
  logic [4:0] c_addr;
  logic [3:0] words_count, row_words_count;
  logic [4:0] rows_count;
  logic [6:0] tile_words;
  logic [2:0] b_raddr;
  logic [4:0] c_raddr;
  logic [XLEN-1:0] b_word, b_operand, c_word, broad, product;
  logic b_we, c_we;
  logic [XLEN-1:0] b_wdata, c_wdata;

  assign b_addr = b_base_q + word_q;
  assign words_count = {1'b0, words_m1_q} + 1'b1;
  assign row_words_count = {1'b0, row_words_m1_q} + 1'b1;
  assign rows_count = {1'b0, rows_m1_q} + 1'b1;
  assign c_addr = c_addr_q;
  assign tile_words = rows_count *
                      (pair_mode_q ? 4'd2 : words_count);
  assign b_operand = auto_ger_q ?
                     (pair_mode_q && pair_phase_q ? b_pair_q : auto_b0_q) :
                     (pair_mode_q && pair_phase_q ? b_pair_q : b_word);
  assign b_raddr = busy_q ? b_addr : b_i[2:0];
  assign c_raddr = busy_q ? c_addr :
                   auto_state_q == AUTO_CLEAR ? auto_clear_idx_q[4:0] :
                   {b_i[5:4], b_i[2:0]};
  assign busy_o = busy_q | atrf_load_q | (auto_state_q != AUTO_IDLE);
  assign cmd_ready_o = ~(busy_q | atrf_load_q) && auto_state_q == AUTO_IDLE;
  assign main_mem_req = auto_state_q == AUTO_A_REQ ||
                        auto_state_q == AUTO_B0_REQ ||
                        auto_state_q == AUTO_B1_REQ;
  assign pf_mem_req = pf_state_q == PF_B0_REQ ||
                      pf_state_q == PF_B1_REQ;
  assign mem_req_valid_o = main_mem_req | pf_mem_req;
  assign mem_req_addr_o = auto_state_q == AUTO_A_REQ ?
                          auto_a_base_q + (XLEN'(auto_a_idx_q) << 3) :
                          main_mem_req ?
                          auto_b_base_q +
                          ((XLEN'(auto_k_q) <<
                            (auto_two_words_q ? 4 : 3)) +
                           (auto_state_q == AUTO_B1_REQ ? XLEN'(8) : '0)) :
                          auto_b_base_q +
                          ((XLEN'(pf_k_q) <<
                            (auto_two_words_q ? 4 : 3)) +
                           (pf_state_q == PF_B1_REQ ? XLEN'(8) : '0));
  assign mem_req_kill_o = 1'b0;
  assign mem_req_write_o = 1'b0;
  assign mem_req_wdata_o = '0;
  assign atrf_we = atrf_load_q;
  assign atrf_addr = atrf_load_q ? {atrf_word_q, atrf_lane_q} :
                     pf_state_q != PF_IDLE ? pf_k_q[4:0] :
                     auto_state_q != AUTO_IDLE ? auto_k_q[4:0] :
                     ((cmd_op_i == OP_AGER16X16 ||
                       cmd_op_i == OP_AGER16R2) ? b_i[21:17] :
                                                b_i[20:16]);

  xmpk_trf_bank #(.XLEN(XLEN), .ADDR_W(3)) i_b_bank (
      .clk_i(clk_i), .we_i(b_we), .addr_i(b_raddr),
      .wdata_i(b_wdata), .rdata_o(b_word)
  );
  xmpk_trf_bank #(.XLEN(XLEN), .ADDR_W(5)) i_c_bank (
      .clk_i(clk_i), .we_i(c_we), .addr_i(c_raddr),
      .wdata_i(c_wdata), .rdata_o(c_word)
  );
  xmpk_atrf i_a_bank (
      .clk_i(clk_i),
      .we_i(atrf_we),
      .wrow_i(atrf_row_q),
      .addr_i(atrf_addr),
      .wdata_i(atrf_load_data_q[4*atrf_lane_q +: 4]),
      .scalars_o(atrf_scalars)
  );

  assign pf_start = mem_rsp_valid_i && !mem_rsp_error_i &&
                    auto_n_q > 1 &&
                    ((!auto_two_words_q &&
                      auto_state_q == AUTO_B0_WAIT) ||
                     (auto_two_words_q &&
                      auto_state_q == AUTO_B1_WAIT));
  assign pf_consume = auto_state_q == AUTO_GER_WAIT &&
                      !busy_q && pf_state_q == PF_READY;

  always_ff @(posedge clk_i or negedge rst_ni) begin
    if (!rst_ni) begin
      pf_state_q   <= PF_IDLE;
      pf_k_q       <= '0;
      pf_b0_q      <= '0;
      pf_b1_q      <= '0;
      pf_scalars_q <= '0;
    end else begin
      if (pf_start) begin
        pf_k_q     <= 6'd1;
        pf_state_q <= PF_B0_REQ;
      end else if (pf_consume) begin
        if (pf_k_q + 1'b1 < auto_n_q) begin
          pf_k_q     <= pf_k_q + 1'b1;
          pf_state_q <= PF_B0_REQ;
        end else begin
          pf_state_q <= PF_IDLE;
        end
      end else begin
        unique case (pf_state_q)
          PF_B0_REQ:
            if (mem_req_ready_i) pf_state_q <= PF_B0_WAIT;
          PF_B0_WAIT: begin
            if (mem_rsp_valid_i) begin
              if (mem_rsp_error_i) begin
                pf_state_q <= PF_ERROR;
              end else begin
                pf_b0_q <= mem_rsp_data_i;
                if (auto_two_words_q) begin
                  pf_state_q <= PF_B1_REQ;
                end else begin
                  pf_scalars_q <= atrf_scalars;
                  pf_state_q <= PF_READY;
                end
              end
            end
          end
          PF_B1_REQ:
            if (mem_req_ready_i) pf_state_q <= PF_B1_WAIT;
          PF_B1_WAIT: begin
            if (mem_rsp_valid_i) begin
              if (mem_rsp_error_i) begin
                pf_state_q <= PF_ERROR;
              end else begin
                pf_b1_q <= mem_rsp_data_i;
                pf_scalars_q <= atrf_scalars;
                pf_state_q <= PF_READY;
              end
            end
          end
          PF_ERROR: begin
            if (auto_state_q == AUTO_IDLE) pf_state_q <= PF_IDLE;
          end
          default: begin
          end
        endcase
      end
      if (auto_state_q == AUTO_ERROR || auto_state_q == AUTO_DONE)
        pf_state_q <= PF_IDLE;
    end
  end

  always_comb begin
    scalar_q = scalar_pack_q[3:0];
    for (int lane = 0; lane < 16; lane++)
      broad[4*lane +: 4] = scalar_q;
  end

  for (genvar lane = 0; lane < 16; lane++) begin : g_mul16
    gf2n_mul #(.W(4)) i_mul (
        .a_i   (broad[4*lane +: 4]),
        .b_i   (b_operand[4*lane +: 4]),
        .poly_i(4'h3),
        .z_o   (product[4*lane +: 4])
    );
  end

  always_comb begin
    b_we = 1'b0;
    c_we = 1'b0;
    b_wdata = a_i;
    c_wdata = a_i;
    if (busy_q) begin
      c_we = 1'b1;
      c_wdata = c_word ^ product;
    end else if (auto_state_q == AUTO_CLEAR) begin
      c_we = 1'b1;
      c_wdata = '0;
    end else if (cmd_valid_i) begin
      if (cmd_op_i == OP_WRITE) begin
        if (b_i[3]) c_we = 1'b1;
        else        b_we = 1'b1;
      end else if (cmd_op_i == OP_BGER16R ||
                   cmd_op_i == OP_AGER16R ||
                   cmd_op_i == OP_AGER16X16 ||
                   cmd_op_i == OP_AGER16R2) begin
        b_we = 1'b1;
      end
    end
  end

  always_ff @(posedge clk_i or negedge rst_ni) begin
    if (!rst_ni) begin
      busy_q     <= 1'b0;
      b_base_q   <= '0;
      c_base_q   <= '0;
      words_m1_q <= '0;
      row_words_m1_q <= '0;
      c_word_q    <= '0;
      c_addr_q <= '0;
      pair_c_addr_q <= '0;
      b_pair_q <= '0;
      pair_mode_q <= 1'b0;
      pair_phase_q <= 1'b0;
      rows_m1_q  <= '0;
      word_q     <= '0;
      row_q      <= '0;
      scalar_pack_q <= '0;
      direct_mode_q <= 1'b0;
      atrf_load_q <= 1'b0;
      atrf_row_q <= '0;
      atrf_word_q <= 1'b0;
      atrf_lane_q <= '0;
      atrf_load_data_q <= '0;
      auto_state_q <= AUTO_IDLE;
      auto_a_base_q <= '0;
      auto_b_base_q <= '0;
      auto_n_q <= '0;
      auto_two_words_q <= 1'b0;
      auto_clear_idx_q <= '0;
      auto_a_idx_q <= '0;
      auto_k_q <= '0;
      auto_b0_q <= '0;
      auto_ger_q <= 1'b0;
      rsp_valid_o <= 1'b0;
      rsp_data_o  <= '0;
    end else begin
      rsp_valid_o <= 1'b0;

      if (atrf_load_q) begin
        if (atrf_lane_q == 4'd15) begin
          atrf_load_q <= 1'b0;
          atrf_lane_q <= '0;
          if (auto_state_q == AUTO_A_STORE) begin
            if (auto_a_idx_q ==
                (auto_two_words_q ? 6'd31 : 6'd15)) begin
              auto_k_q <= '0;
              auto_state_q <= AUTO_B0_REQ;
            end else begin
              auto_a_idx_q <= auto_a_idx_q + 1'b1;
              auto_state_q <= AUTO_A_REQ;
            end
          end else begin
            rsp_valid_o <= 1'b1;
            rsp_data_o <= 64'd16;
          end
        end else begin
          atrf_lane_q <= atrf_lane_q + 1'b1;
        end
      end else if (busy_q) begin
        if (word_q == words_m1_q) begin
          word_q <= '0;
          if (row_q == rows_m1_q) begin
            if (pair_mode_q && !pair_phase_q) begin
              pair_phase_q <= 1'b1;
              row_q <= '0;
              scalar_pack_q <= {scalar_pack_q[3:0],
                                scalar_pack_q[63:4]};
              c_addr_q <= pair_c_addr_q;
            end else if (auto_ger_q) begin
              busy_q <= 1'b0;
              auto_ger_q <= 1'b0;
              if (auto_k_q == auto_n_q - 1'b1) begin
                auto_state_q <= AUTO_DONE;
              end else begin
                auto_state_q <= AUTO_GER_WAIT;
              end
            end else begin
              busy_q      <= 1'b0;
              rsp_valid_o <= 1'b1;
              rsp_data_o  <= {{(XLEN-7){1'b0}}, tile_words};
            end
          end else begin
            row_q <= row_q + 1'b1;
            scalar_pack_q <= {scalar_pack_q[3:0],
                              scalar_pack_q[63:4]};
            c_addr_q <= direct_mode_q ? c_addr_q + row_words_count :
                                           c_addr_q + 1'b1;
          end
        end else begin
          word_q <= word_q + 1'b1;
          c_addr_q <= c_addr_q + 1'b1;
        end
      end else if (auto_state_q != AUTO_IDLE) begin
        unique case (auto_state_q)
          AUTO_CLEAR: begin
            if (auto_clear_idx_q ==
                (auto_two_words_q ? 6'd31 : 6'd15)) begin
              auto_a_idx_q <= '0;
              auto_state_q <= AUTO_A_REQ;
            end else begin
              auto_clear_idx_q <= auto_clear_idx_q + 1'b1;
            end
          end
          AUTO_A_REQ:
            if (mem_req_ready_i) auto_state_q <= AUTO_A_WAIT;
          AUTO_A_WAIT: begin
            if (mem_rsp_valid_i) begin
              if (mem_rsp_error_i) begin
                auto_state_q <= AUTO_ERROR;
              end else begin
                atrf_load_data_q <= mem_rsp_data_i;
                atrf_row_q <= auto_two_words_q ?
                              auto_a_idx_q[4:1] : auto_a_idx_q[3:0];
                atrf_word_q <= auto_two_words_q && auto_a_idx_q[0];
                atrf_lane_q <= '0;
                atrf_load_q <= 1'b1;
                auto_state_q <= AUTO_A_STORE;
              end
            end
          end
          AUTO_A_STORE: begin
          end
          AUTO_B0_REQ:
            if (mem_req_ready_i) auto_state_q <= AUTO_B0_WAIT;
          AUTO_B0_WAIT: begin
            if (mem_rsp_valid_i) begin
              if (mem_rsp_error_i) begin
                auto_state_q <= AUTO_ERROR;
              end else begin
                auto_b0_q <= mem_rsp_data_i;
                if (auto_two_words_q) begin
                  auto_state_q <= AUTO_B1_REQ;
                end else begin
                  scalar_pack_q <= atrf_scalars;
                  direct_mode_q <= 1'b1;
                  pair_mode_q <= 1'b0;
                  pair_phase_q <= 1'b0;
                  c_addr_q <= '0;
                  words_m1_q <= '0;
                  row_words_m1_q <= '0;
                  rows_m1_q <= 4'd15;
                  word_q <= '0;
                  row_q <= '0;
                  busy_q <= 1'b1;
                  auto_ger_q <= 1'b1;
                  auto_state_q <= AUTO_GER_WAIT;
                end
              end
            end
          end
          AUTO_B1_REQ:
            if (mem_req_ready_i) auto_state_q <= AUTO_B1_WAIT;
          AUTO_B1_WAIT: begin
            if (mem_rsp_valid_i) begin
              if (mem_rsp_error_i) begin
                auto_state_q <= AUTO_ERROR;
              end else begin
                scalar_pack_q <= atrf_scalars;
                direct_mode_q <= 1'b1;
                pair_mode_q <= 1'b1;
                pair_phase_q <= 1'b0;
                b_pair_q <= mem_rsp_data_i;
                c_addr_q <= '0;
                pair_c_addr_q <= 5'd1;
                words_m1_q <= '0;
                row_words_m1_q <= 3'd1;
                rows_m1_q <= 4'd15;
                word_q <= '0;
                row_q <= '0;
                busy_q <= 1'b1;
                auto_ger_q <= 1'b1;
                auto_state_q <= AUTO_GER_WAIT;
              end
            end
          end
          AUTO_GER_WAIT: begin
            if (pf_state_q == PF_ERROR) begin
              auto_state_q <= AUTO_ERROR;
            end else if (pf_consume) begin
              auto_k_q <= pf_k_q;
              auto_b0_q <= pf_b0_q;
              scalar_pack_q <= pf_scalars_q;
              direct_mode_q <= 1'b1;
              pair_mode_q <= auto_two_words_q;
              pair_phase_q <= 1'b0;
              b_pair_q <= pf_b1_q;
              c_addr_q <= '0;
              pair_c_addr_q <= 5'd1;
              words_m1_q <= '0;
              row_words_m1_q <= auto_two_words_q ? 3'd1 : 3'd0;
              rows_m1_q <= 4'd15;
              word_q <= '0;
              row_q <= '0;
              busy_q <= 1'b1;
              auto_ger_q <= 1'b1;
            end
          end
          AUTO_DONE: begin
            rsp_valid_o <= 1'b1;
            rsp_data_o <= XLEN'(auto_n_q) *
                          (auto_two_words_q ? XLEN'(32) : XLEN'(16));
            auto_state_q <= AUTO_IDLE;
          end
          AUTO_ERROR: begin
              rsp_valid_o <= 1'b1;
              rsp_data_o <= {1'b1, {(XLEN-1){1'b0}}};
            auto_state_q <= AUTO_IDLE;
          end
          default: auto_state_q <= AUTO_ERROR;
        endcase
      end else if (cmd_valid_i) begin
        unique case (cmd_op_i)
          OP_WRITE: begin
            rsp_valid_o <= 1'b0;
          end
          OP_READ: begin
            rsp_valid_o <= 1'b1;
            rsp_data_o  <= b_i[3] ? c_word : b_word;
          end
          OP_GER16: begin
            scalar_pack_q <= {8{a_i[4*b_i[3:0] +: 4]}};
            b_base_q <= b_i[6:4];
            c_base_q <= b_i[11:8];
            c_addr_q <= {1'b0, b_i[11:8]};
            words_m1_q <= b_i[14:12];
            row_words_m1_q <= b_i[14:12];
            rows_m1_q  <= '0;
            word_q     <= '0;
            row_q      <= '0;
            direct_mode_q <= 1'b0;
            pair_mode_q <= 1'b0;
            busy_q     <= 1'b1;
          end
          OP_GER16R: begin
            scalar_pack_q <= {{32{1'b0}}, a_i[31:0]};
            words_m1_q <= b_i[2:0];
            row_words_m1_q <= b_i[2:0];
            rows_m1_q  <= b_i[6:4];
            b_base_q   <= b_i[10:8];
            c_base_q   <= b_i[15:12];
            c_addr_q   <= {1'b0, b_i[15:12]};
            word_q     <= '0;
            row_q      <= '0;
            direct_mode_q <= 1'b0;
            pair_mode_q <= 1'b0;
            busy_q     <= 1'b1;
          end
          OP_BGER16R: begin
            scalar_pack_q <= {{32{1'b0}}, c_i[31:0]};
            direct_mode_q <= 1'b1;
            pair_mode_q <= 1'b0;
            c_word_q <= b_i[2:0];
            c_addr_q <= {1'b0, b_i[15:12]} + {2'b0, b_i[2:0]};
            b_base_q <= b_i[2:0];
            words_m1_q <= '0;
            row_words_m1_q <= b_i[6:4];
            rows_m1_q <= b_i[10:8];
            c_base_q <= b_i[15:12];
            word_q <= '0;
            row_q <= '0;
            busy_q <= 1'b1;
          end
          OP_ATRF_LOAD: begin
            atrf_load_data_q <= a_i;
            atrf_row_q <= {1'b0, b_i[2:0]};
            atrf_word_q <= b_i[3];
            atrf_lane_q <= '0;
            atrf_load_q <= 1'b1;
          end
          OP_AGER16R: begin
            scalar_pack_q <= {{32{1'b0}}, atrf_scalars[31:0]};
            direct_mode_q <= 1'b1;
            pair_mode_q <= 1'b0;
            c_word_q <= b_i[2:0];
            c_addr_q <= {1'b0, b_i[15:12]} + {2'b0, b_i[2:0]};
            b_base_q <= b_i[2:0];
            words_m1_q <= '0;
            row_words_m1_q <= b_i[6:4];
            rows_m1_q <= b_i[10:8];
            c_base_q <= b_i[15:12];
            word_q <= '0;
            row_q <= '0;
            busy_q <= 1'b1;
          end
          OP_ATRF_LOAD16: begin
            atrf_load_data_q <= a_i;
            atrf_row_q <= b_i[3:0];
            atrf_word_q <= b_i[4];
            atrf_lane_q <= '0;
            atrf_load_q <= 1'b1;
          end
          OP_AGER16X16: begin
            scalar_pack_q <= atrf_scalars;
            direct_mode_q <= 1'b1;
            pair_mode_q <= 1'b0;
            c_word_q <= b_i[2:0];
            c_addr_q <= b_i[16:12] + {2'b0, b_i[2:0]};
            b_base_q <= b_i[2:0];
            words_m1_q <= '0;
            row_words_m1_q <= b_i[6:4];
            rows_m1_q <= b_i[11:8];
            c_base_q <= b_i[16:12];
            word_q <= '0;
            row_q <= '0;
            busy_q <= 1'b1;
          end
          OP_AGER16R2: begin
            scalar_pack_q <= atrf_scalars;
            direct_mode_q <= 1'b1;
            pair_mode_q <= 1'b1;
            pair_phase_q <= 1'b0;
            b_pair_q <= c_i;
            c_word_q <= b_i[2:0];
            c_addr_q <= b_i[16:12] + {2'b0, b_i[2:0]};
            pair_c_addr_q <= b_i[16:12] + {2'b0, b_i[2:0]} +
                             1'b1;
            b_base_q <= b_i[2:0];
            words_m1_q <= '0;
            row_words_m1_q <= b_i[6:4];
            rows_m1_q <= b_i[11:8];
            c_base_q <= b_i[16:12];
            word_q <= '0;
            row_q <= '0;
            busy_q <= 1'b1;
          end
          OP_GER_SETUP: begin
            auto_a_base_q <= a_i;
            auto_b_base_q <= b_i;
          end
          OP_GER_RUN: begin
            if (a_i == XLEN'(16) || a_i == XLEN'(32)) begin
              auto_n_q <= a_i[5:0];
              auto_two_words_q <= a_i == XLEN'(32);
              auto_clear_idx_q <= '0;
              auto_state_q <= AUTO_CLEAR;
            end else begin
              rsp_valid_o <= 1'b1;
              rsp_data_o <= {1'b1, {(XLEN-2){1'b0}}, 1'b1};
            end
          end
          default: begin
            rsp_valid_o <= 1'b1;
            rsp_data_o  <= '0;
          end
        endcase
      end
    end
  end
endmodule
