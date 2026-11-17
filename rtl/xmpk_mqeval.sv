// SPDX-License-Identifier: Apache-2.0 WITH SHL-2.0
// Autonomous GF(16) multivariate-quadratic evaluator.
//
// Q stores one upper-triangular matrix per equation. Each equation starts on
// a 64-bit boundary and packs sixteen coefficients per word. X and Y use the
// same sixteen-nibble packing.
module xmpk_mqeval #(
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
    M_IDLE,
    M_X_REQ,
    M_X_WAIT,
    M_Q_REQ,
    M_Q_WAIT,
    M_TERM,
    M_Y_REQ,
    M_Y_WAIT,
    M_DONE,
    M_ERROR
  } state_t;

  state_t state_q;
  logic [XLEN-1:0] q_base_q, x_base_q, y_base_q;
  logic [XLEN-1:0] x_addr_q, q_addr_q, y_addr_q;
  logic [7:0] n_q, m_q, eq_q, i_q, j_q;
  logic [3:0] xwords_q;
  logic [2:0] xword_q;
  logic [3:0] q_lane_q;
  logic [63:0] x_words_q [0:7];
  logic [63:0] q_word_q, y_pack_q;
  logic [3:0] accum_q;
  logic [XLEN-1:0] term_count_q;
  logic final_store_q;
  logic [7:0] next_i, next_j;
  logic [3:0] xi, xj, qcoeff, xprod, term;
  logic [3:0] xi_next, xj_next, qcoeff_next;
  logic [3:0] xprod_next, term_next;
  logic current_final, second_final, dual_term;

  assign cmd_ready_o = state_q == M_IDLE;
  assign busy_o = state_q != M_IDLE;
  assign mem_req_valid_o = state_q == M_X_REQ ||
                           state_q == M_Q_REQ ||
                           state_q == M_Y_REQ;
  assign mem_req_write_o = state_q == M_Y_REQ;
  assign mem_req_wdata_o = y_pack_q;
  assign mem_req_kill_o = 1'b0;
  assign mem_req_addr_o = state_q == M_X_REQ ? x_addr_q :
                          state_q == M_Q_REQ ? q_addr_q : y_addr_q;

  assign xi = x_words_q[i_q[6:4]][4*i_q[3:0] +: 4];
  assign xj = x_words_q[j_q[6:4]][4*j_q[3:0] +: 4];
  assign qcoeff = q_word_q[4*q_lane_q +: 4];
  assign current_final = i_q == n_q - 1'b1 && j_q == n_q - 1'b1;
  assign next_i = current_final ? i_q :
                  j_q == n_q - 1'b1 ? i_q + 1'b1 : i_q;
  assign next_j = current_final ? j_q :
                  j_q == n_q - 1'b1 ? i_q + 1'b1 : j_q + 1'b1;
  assign second_final = next_i == n_q - 1'b1 &&
                        next_j == n_q - 1'b1;
  assign dual_term = q_lane_q != 4'hf && !current_final;
  assign xi_next = x_words_q[next_i[6:4]][4*next_i[3:0] +: 4];
  assign xj_next = x_words_q[next_j[6:4]][4*next_j[3:0] +: 4];
  assign qcoeff_next = q_word_q[4*(q_lane_q + 1'b1) +: 4];
  gf2n_mul #(.W(4)) i_xprod (
      .a_i(xi), .b_i(xj), .poly_i(4'h3), .z_o(xprod)
  );
  gf2n_mul #(.W(4)) i_term (
      .a_i(xprod), .b_i(qcoeff), .poly_i(4'h3), .z_o(term)
  );
  gf2n_mul #(.W(4)) i_xprod_next (
      .a_i(xi_next), .b_i(xj_next), .poly_i(4'h3), .z_o(xprod_next)
  );
  gf2n_mul #(.W(4)) i_term_next (
      .a_i(xprod_next), .b_i(qcoeff_next), .poly_i(4'h3),
      .z_o(term_next)
  );

  always_ff @(posedge clk_i or negedge rst_ni) begin
    if (!rst_ni) begin
      state_q <= M_IDLE;
      q_base_q <= '0;
      x_base_q <= '0;
      y_base_q <= '0;
      x_addr_q <= '0;
      q_addr_q <= '0;
      y_addr_q <= '0;
      n_q <= '0;
      m_q <= '0;
      eq_q <= '0;
      i_q <= '0;
      j_q <= '0;
      xwords_q <= '0;
      xword_q <= '0;
      q_lane_q <= '0;
      q_word_q <= '0;
      y_pack_q <= '0;
      accum_q <= '0;
      term_count_q <= '0;
      final_store_q <= 1'b0;
      rsp_valid_o <= 1'b0;
      rsp_data_o <= '0;
      for (int word = 0; word < 8; word++)
        x_words_q[word] <= '0;
    end else begin
      rsp_valid_o <= 1'b0;
      unique case (state_q)
        M_IDLE: begin
          if (cmd_valid_i) begin
            if (!cmd_run_i) begin
              q_base_q <= a_i;
              x_base_q <= b_i;
              y_base_q <= c_i;
            end else if (a_i[7:0] == 0 || a_i[7:0] > 8'd128 ||
                         a_i[15:8] == 0) begin
              rsp_valid_o <= 1'b1;
              rsp_data_o <= {1'b1, {(XLEN-2){1'b0}}, 1'b1};
            end else begin
              n_q <= a_i[7:0];
              m_q <= a_i[15:8];
              xwords_q <= (a_i[7:0] + 8'd15) >> 4;
              xword_q <= '0;
              x_addr_q <= x_base_q;
              q_addr_q <= q_base_q;
              y_addr_q <= y_base_q;
              eq_q <= '0;
              i_q <= '0;
              j_q <= '0;
              q_lane_q <= '0;
              y_pack_q <= '0;
              accum_q <= '0;
              term_count_q <= '0;
              final_store_q <= 1'b0;
              state_q <= M_X_REQ;
            end
          end
        end
        M_X_REQ:
          if (mem_req_ready_i) state_q <= M_X_WAIT;
        M_X_WAIT:
          if (mem_rsp_valid_i) begin
            if (mem_rsp_error_i) begin
              state_q <= M_ERROR;
            end else begin
              x_words_q[xword_q] <= mem_rsp_data_i;
              x_addr_q <= x_addr_q + XLEN'(8);
              if (xword_q == xwords_q - 1'b1) begin
                state_q <= M_Q_REQ;
              end else begin
                xword_q <= xword_q + 1'b1;
                state_q <= M_X_REQ;
              end
            end
          end
        M_Q_REQ:
          if (mem_req_ready_i) state_q <= M_Q_WAIT;
        M_Q_WAIT:
          if (mem_rsp_valid_i) begin
            if (mem_rsp_error_i) begin
              state_q <= M_ERROR;
            end else begin
              q_word_q <= mem_rsp_data_i;
              q_addr_q <= q_addr_q + XLEN'(8);
              state_q <= M_TERM;
            end
          end
        M_TERM: begin
          if (current_final || (dual_term && second_final)) begin
            y_pack_q[4*eq_q[3:0] +: 4] <=
                accum_q ^ term ^ (dual_term ? term_next : 4'b0);
            accum_q <= '0;
            term_count_q <= term_count_q + (dual_term ? 2 : 1);
            i_q <= '0;
            j_q <= '0;
            q_lane_q <= '0;
            if (eq_q == m_q - 1'b1) begin
              final_store_q <= 1'b1;
              state_q <= M_Y_REQ;
            end else begin
              eq_q <= eq_q + 1'b1;
              if (eq_q[3:0] == 4'hf) begin
                final_store_q <= 1'b0;
                state_q <= M_Y_REQ;
              end else begin
                state_q <= M_Q_REQ;
              end
            end
          end else if (dual_term) begin
            accum_q <= accum_q ^ term ^ term_next;
            term_count_q <= term_count_q + 2;
            if (next_j == n_q - 1'b1) begin
              i_q <= next_i + 1'b1;
              j_q <= next_i + 1'b1;
            end else begin
              i_q <= next_i;
              j_q <= next_j + 1'b1;
            end
            if (q_lane_q == 4'he) begin
              q_lane_q <= '0;
              state_q <= M_Q_REQ;
            end else begin
              q_lane_q <= q_lane_q + 2;
            end
          end else begin
            accum_q <= accum_q ^ term;
            term_count_q <= term_count_q + 1'b1;
            i_q <= next_i;
            j_q <= next_j;
            q_lane_q <= '0;
            state_q <= M_Q_REQ;
          end
        end
        M_Y_REQ:
          if (mem_req_ready_i) state_q <= M_Y_WAIT;
        M_Y_WAIT:
          if (mem_rsp_valid_i) begin
            if (mem_rsp_error_i) begin
              state_q <= M_ERROR;
            end else begin
              y_addr_q <= y_addr_q + XLEN'(8);
              y_pack_q <= '0;
              if (final_store_q)
                state_q <= M_DONE;
              else
                state_q <= M_Q_REQ;
            end
          end
        M_DONE: begin
          rsp_valid_o <= 1'b1;
          rsp_data_o <= term_count_q;
          state_q <= M_IDLE;
        end
        M_ERROR: begin
          rsp_valid_o <= 1'b1;
          rsp_data_o <= {1'b1, {(XLEN-1){1'b0}}};
          state_q <= M_IDLE;
        end
        default: state_q <= M_ERROR;
      endcase
    end
  end
endmodule
