// SPDX-License-Identifier: Apache-2.0 WITH SHL-2.0
// Autonomous rectangular packed finite-field matrix-multiply tile.
//
// Mode 0: GF16, 16 lanes, x^4+x+1.
// Mode 1: AES-GF256, 8 lanes, x^8+x^4+x^3+x+1.
// Mode 2/3/4: Fp7/Fp31/Fp127, 8 byte lanes.
module xmpk_qgemm #(
    parameter int unsigned XLEN = 64
) (
    input  logic            clk_i,
    input  logic            rst_ni,
    input  logic            cmd_valid_i,
    output logic            cmd_ready_o,
    input  logic            cmd_run_i,
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
  typedef enum logic [3:0] {
    Q_IDLE,
    Q_A_REQ,
    Q_A_WAIT,
    Q_B_REQ,
    Q_B_WAIT,
    Q_MAC,
    Q_C_REQ,
    Q_C_WAIT,
    Q_DONE,
    Q_ERROR
  } state_t;

  state_t state_q;
  logic [XLEN-1:0] a_base_q, b_base_q, c_base_q;
  logic [XLEN-1:0] a_row_base_q, b_col_base_q, b_addr_q, c_addr_q;
  logic [7:0] m_q, k_dim_q, awords_q;
  logic [11:0] nwords_q, colword_q;
  logic [7:0] row_q, k_q;
  logic [4:0] a_fill_q;
  logic [2:0] mode_q;
  logic prime_phase_q;
  logic prime_mul_stage_q;
  logic [63:0] a_word_q, a0_q, b_word_q, acc_q;
  (* ram_style = "distributed" *) logic [63:0] a_cache_q [0:31];
  logic [XLEN-1:0] mac_count_q;
  logic [3:0] scalar16;
  logic [7:0] scalar8;
  logic [63:0] product16, product256;
  logic [31:0] product_prime;
  (* use_dsp = "yes" *) logic [15:0] prime_product_raw [0:3];
  logic [15:0] prime_product_q [0:3];
  logic [63:0] acc_next;
  logic [4:0] a_lane;
  logic [4:0] a_next_word;

  function automatic logic [7:0] prime_reduce(
      input logic [15:0] product,
      input logic [2:0] mode
  );
    logic [8:0] folded;
    begin
      folded = '0;
      unique case (mode)
        3'd2: begin
          // Canonical Fp7 inputs make product[15:6] zero; 8 == 1 mod 7.
          folded = {6'b0, product[2:0]} + {6'b0, product[5:3]};
          prime_reduce = folded >= 9'd7 ? 8'(folded - 9'd7) :
                                         folded[7:0];
        end
        3'd3: begin
          // Canonical Fp31 inputs make product[15:10] zero; 32 == 1 mod 31.
          folded = {4'b0, product[4:0]} + {4'b0, product[9:5]};
          prime_reduce = folded >= 9'd31 ? 8'(folded - 9'd31) :
                                          folded[7:0];
        end
        default: begin
          // Canonical Fp127 inputs make product[15:14] zero; 128 == 1 mod 127.
          folded = {2'b0, product[6:0]} + {2'b0, product[13:7]};
          prime_reduce = folded >= 9'd127 ? 8'(folded - 9'd127) :
                                           folded[7:0];
        end
      endcase
    end
  endfunction

  function automatic logic [7:0] prime_add(
      input logic [7:0] lhs,
      input logic [7:0] rhs,
      input logic [2:0] mode
  );
    logic [8:0] sum;
    logic [7:0] modulus;
    begin
      modulus = mode == 3'd2 ? 8'd7 :
                mode == 3'd3 ? 8'd31 : 8'd127;
      sum = {1'b0, lhs} + {1'b0, rhs};
      prime_add = 8'(sum >= {1'b0, modulus} ?
                     sum - {1'b0, modulus} : sum);
    end
  endfunction

  assign cmd_ready_o = state_q == Q_IDLE;
  assign busy_o = state_q != Q_IDLE;
  assign mem_req_valid_o = state_q == Q_A_REQ ||
                           state_q == Q_B_REQ ||
                           state_q == Q_C_REQ ||
                           (state_q == Q_MAC && mode_q < 2 && k_q != k_dim_q - 1'b1);
  assign mem_req_write_o = state_q == Q_C_REQ;
  assign mem_req_wdata_o = acc_q;
  assign mem_req_kill_o = 1'b0;
  assign mem_req_addr_o = state_q == Q_A_REQ ?
                          a_row_base_q + (XLEN'(a_fill_q) << 3) :
                          state_q == Q_B_REQ ? b_addr_q :
                          state_q == Q_MAC ? b_addr_q + (XLEN'(nwords_q) << 3) : c_addr_q;

  assign a_lane = mode_q == 0 ? {1'b0, k_q[3:0]} :
                                {2'b0, k_q[2:0]};
  assign a_next_word = mode_q == 0 ?
                       5'((k_q + 1'b1) >> 4) :
                       5'((k_q + 1'b1) >> 3);
  assign scalar16 = a_word_q[4*a_lane +: 4];
  assign scalar8 = a_word_q[8*a_lane +: 8];
  for (genvar lane = 0; lane < 16; lane++) begin : g_mul16
    gf2n_mul #(.W(4)) i_mul (
        .a_i(scalar16),
        .b_i(b_word_q[4*lane +: 4]),
        .poly_i(4'h3),
        .z_o(product16[4*lane +: 4])
    );
  end
  for (genvar lane = 0; lane < 8; lane++) begin : g_mul256
    gf2n_mul #(.W(8)) i_mul (
        .a_i(scalar8),
        .b_i(b_word_q[8*lane +: 8]),
        .poly_i(8'h1b),
        .z_o(product256[8*lane +: 8])
    );
  end
  for (genvar lane = 0; lane < 4; lane++) begin : g_mul_prime
    assign prime_product_raw[lane] =
        scalar8 *
        b_word_q[8*(lane + (prime_phase_q ? 4 : 0)) +: 8];
  end
  always_comb begin
    product_prime = '0;
    for (int lane = 0; lane < 4; lane++)
      product_prime[8*lane +: 8] = prime_reduce(prime_product_q[lane],
                                                mode_q);
    acc_next = acc_q;
    if (mode_q == 0) begin
      acc_next = acc_q ^ product16;
    end else if (mode_q == 1) begin
      acc_next = acc_q ^ product256;
    end else begin
      for (int lane = 0; lane < 4; lane++)
        acc_next[8*(lane + (prime_phase_q ? 4 : 0)) +: 8] =
            prime_add(
                acc_q[8*(lane + (prime_phase_q ? 4 : 0)) +: 8],
                product_prime[8*lane +: 8], mode_q);
    end
  end

  always_ff @(posedge clk_i)
    if (state_q == Q_A_WAIT && mem_rsp_valid_i && !mem_rsp_error_i &&
        a_fill_q != 0)
      a_cache_q[a_fill_q] <= mem_rsp_data_i;

  always_ff @(posedge clk_i or negedge rst_ni) begin
    if (!rst_ni) begin
      state_q <= Q_IDLE;
      a_base_q <= '0;
      b_base_q <= '0;
      c_base_q <= '0;
      a_row_base_q <= '0;
      b_col_base_q <= '0;
      b_addr_q <= '0;
      c_addr_q <= '0;
      m_q <= '0;
      k_dim_q <= '0;
      nwords_q <= '0;
      awords_q <= '0;
      row_q <= '0;
      k_q <= '0;
      colword_q <= '0;
      a_fill_q <= '0;
      mode_q <= '0;
      prime_phase_q <= 1'b0;
      prime_mul_stage_q <= 1'b0;
      for (int lane = 0; lane < 4; lane++)
        prime_product_q[lane] <= '0;
      a_word_q <= '0;
      a0_q <= '0;
      b_word_q <= '0;
      acc_q <= '0;
      mac_count_q <= '0;
      rsp_valid_o <= 1'b0;
      rsp_data_o <= '0;
    end else begin
      rsp_valid_o <= 1'b0;
      unique case (state_q)
        Q_IDLE: begin
          if (cmd_valid_i) begin
            if (!cmd_run_i) begin
              a_base_q <= a_i;
              b_base_q <= b_i;
              c_base_q <= c_i;
            end else if (a_i[7:0] == 0 || a_i[15:8] == 0 ||
                         a_i[27:16] == 0 || a_i[30:28] > 3'd4 ||
                         a_i[XLEN-1:31] != 0) begin
              rsp_valid_o <= 1'b1;
              rsp_data_o <= {1'b1, {(XLEN-2){1'b0}}, 1'b1};
            end else begin
              m_q <= a_i[7:0];
              k_dim_q <= a_i[15:8];
              nwords_q <= a_i[27:16];
              mode_q <= a_i[30:28];
              awords_q <= a_i[30:28] == 0 ?
                          8'(({1'b0, a_i[15:8]} + 9'd15) >> 4) :
                          8'(({1'b0, a_i[15:8]} + 9'd7) >> 3);
              row_q <= '0;
              k_q <= '0;
              colword_q <= '0;
              a_fill_q <= '0;
              prime_phase_q <= 1'b0;
              prime_mul_stage_q <= 1'b0;
              a_row_base_q <= a_base_q;
              b_col_base_q <= b_base_q;
              b_addr_q <= b_base_q;
              c_addr_q <= c_base_q;
              acc_q <= '0;
              mac_count_q <= '0;
              state_q <= Q_A_REQ;
            end
          end
        end
        Q_A_REQ:
          if (mem_req_ready_i) state_q <= Q_A_WAIT;
        Q_A_WAIT:
          if (mem_rsp_valid_i) begin
            if (mem_rsp_error_i) begin
              state_q <= Q_ERROR;
            end else begin
              if (a_fill_q == 0)
                a0_q <= mem_rsp_data_i;
              if (a_fill_q == awords_q[4:0] - 1'b1) begin
                a_fill_q <= '0;
                a_word_q <= a_fill_q == 0 ?
                            mem_rsp_data_i : a0_q;
                state_q <= Q_B_REQ;
              end else begin
                a_fill_q <= a_fill_q + 1'b1;
                state_q <= Q_A_REQ;
              end
            end
          end
        Q_B_REQ:
          if (mem_req_ready_i) state_q <= Q_B_WAIT;
        Q_B_WAIT:
          if (mem_rsp_valid_i) begin
            if (mem_rsp_error_i) begin
              state_q <= Q_ERROR;
            end else begin
              b_word_q <= mem_rsp_data_i;
              state_q <= Q_MAC;
            end
          end
        Q_MAC: begin
          if (mode_q >= 2 && !prime_mul_stage_q) begin
            for (int lane = 0; lane < 4; lane++)
              prime_product_q[lane] <= prime_product_raw[lane];
            prime_mul_stage_q <= 1'b1;
          end else begin
            acc_q <= acc_next;
            prime_mul_stage_q <= 1'b0;
            if (mode_q >= 2 && !prime_phase_q) begin
              prime_phase_q <= 1'b1;
            end else begin
              prime_phase_q <= 1'b0;
              mac_count_q <= mac_count_q + (mode_q == 0 ? XLEN'(16) :
                                                            XLEN'(8));
              if (k_q == k_dim_q - 1'b1) begin
                state_q <= Q_C_REQ;
              end else begin
                k_q <= k_q + 1'b1;
                b_addr_q <= b_addr_q + (XLEN'(nwords_q) << 3);
                if ((mode_q == 0 && k_q[3:0] == 4'hf) ||
                    (mode_q != 0 && k_q[2:0] == 3'h7))
                  a_word_q <= a_cache_q[a_next_word];
                state_q <= mode_q < 2 && mem_req_ready_i ? Q_B_WAIT : Q_B_REQ;
              end
            end
          end
        end
        Q_C_REQ:
          if (mem_req_ready_i) state_q <= Q_C_WAIT;
        Q_C_WAIT:
          if (mem_rsp_valid_i) begin
            if (mem_rsp_error_i) begin
              state_q <= Q_ERROR;
            end else begin
              k_q <= '0;
              acc_q <= '0;
              c_addr_q <= c_addr_q + XLEN'(8);
              if (colword_q == nwords_q - 1'b1) begin
                colword_q <= '0;
                if (row_q == m_q - 1'b1) begin
                  state_q <= Q_DONE;
                end else begin
                  row_q <= row_q + 1'b1;
                  a_fill_q <= '0;
                  a_row_base_q <=
                      a_row_base_q + (XLEN'(awords_q) << 3);
                  b_col_base_q <= b_base_q;
                  b_addr_q <= b_base_q;
                  state_q <= Q_A_REQ;
                end
              end else begin
                colword_q <= colword_q + 1'b1;
                b_col_base_q <= b_col_base_q + XLEN'(8);
                b_addr_q <= b_col_base_q + XLEN'(8);
                a_word_q <= a0_q;
                state_q <= Q_B_REQ;
              end
            end
          end
        Q_DONE: begin
          rsp_valid_o <= 1'b1;
          rsp_data_o <= mac_count_q;
          state_q <= Q_IDLE;
        end
        Q_ERROR: begin
          rsp_valid_o <= 1'b1;
          rsp_data_o <= {1'b1, {(XLEN-1){1'b0}}};
          state_q <= Q_IDLE;
        end
        default: state_q <= Q_ERROR;
      endcase
    end
  end
endmodule
