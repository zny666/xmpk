// SPDX-License-Identifier: Apache-2.0 WITH SHL-2.0
// Autonomous GF(127^L) matrix multiply for QR-UOV L=3 and L=10.
//
// A, B and C are row-major matrices. Every extension-field element occupies
// one aligned 16-byte slot; only the low L bytes are significant.
module xmpk_extgemm #(
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
  typedef enum logic [4:0] {
    E_IDLE,
    E_A0_REQ,
    E_A0_WAIT,
    E_A1_REQ,
    E_A1_WAIT,
    E_B0_REQ,
    E_B0_WAIT,
    E_B1_REQ,
    E_B1_WAIT,
    E_MUL,
    E_MUL_FLUSH,
    E_REDUCE,
    E_PACK_REDUCE,
    E_PACK,
    E_STORE_REQ,
    E_STORE_WAIT,
    E_DONE,
    E_ERROR
  } state_t;

  state_t state_q;
  logic [15:0] kar_q [0:5];

  logic [XLEN-1:0] a_base_q, b_base_q, c_base_q;
  logic [XLEN-1:0] a_row_base_q, a_addr_q;
  logic [XLEN-1:0] b_col_base_q, b_addr_q, c_addr_q;
  logic [7:0] m_q, k_dim_q, n_q;
  logic [7:0] row_q, col_q, k_q;
  logic l10_q;
  logic [4:0] degree_q, reduce_degree_q, pack_coeff_q;
  logic [1:0] chunk_q;
  logic store_word_q;
  logic [127:0] a_elem_q, b_elem_q;
  logic [27:0] accum_q [0:18];
  logic [31:0] product_sum_q;
  logic [4:0] product_degree_q;
  logic mul_pipe_valid_q;
  logic [7:0] pack_value_q;
  logic [63:0] output_lo_q, output_hi_q;
  logic [XLEN-1:0] mac_count_q;

  logic [4:0] degree_low, degree_high;
  logic [3:0] degree_terms;
  logic [1:0] degree_chunks;
  logic [15:0] product_raw [0:3];
  logic product_valid [0:3];
  logic [31:0] product_sum;

  function automatic logic [7:0] mod127(input logic [31:0] value);
    logic [15:0] folded;
    begin
      folded = {9'b0, value[6:0]} +
               {9'b0, value[13:7]} +
               {9'b0, value[20:14]} +
               {9'b0, value[27:21]} +
               {12'b0, value[31:28]};
      folded = {9'b0, folded[6:0]} + (folded >> 7);
      folded = {9'b0, folded[6:0]} + (folded >> 7);
      mod127 = folded >= 16'd127 ? 8'(folded - 16'd127) :
                                   folded[7:0];
    end
  endfunction

  always_comb begin
    logic [4:0] extension_degree;
    extension_degree = l10_q ? 5'd10 : 5'd3;
    degree_low = degree_q >= extension_degree ?
                 degree_q - extension_degree + 1'b1 : '0;
    degree_high = degree_q < extension_degree ?
                  degree_q : extension_degree - 1'b1;
    degree_terms = 4'(degree_high - degree_low + 1'b1);
    degree_chunks = 2'((degree_terms + 4'd3) >> 2);
    product_sum = '0;
    for (int lane = 0; lane < 4; lane++) begin
      logic [5:0] j;
      logic [5:0] k;
      j = {1'b0, degree_low} + {2'b0, chunk_q, 2'b0} + 6'(lane);
      k = {1'b0, degree_q} - j;
      product_valid[lane] = j <= {1'b0, degree_high};
      if (!l10_q) begin
        if (degree_q == 0) begin
          if (lane < 3)
            product_raw[lane] = a_elem_q[8*lane +:8] * b_elem_q[8*lane +:8];
          else
            product_raw[lane] = (9'(a_elem_q[7:0])+9'(a_elem_q[15:8])) *
                                (9'(b_elem_q[7:0])+9'(b_elem_q[15:8]));
        end else begin
          if (lane < 2)
            product_raw[lane] = (9'(a_elem_q[8*lane +:8])+9'(a_elem_q[23:16])) *
                                (9'(b_elem_q[8*lane +:8])+9'(b_elem_q[23:16]));
          else product_raw[lane] = 0;
        end
      end else if (product_valid[lane])
        product_raw[lane] = a_elem_q[8*j +:8] * b_elem_q[8*k +:8];
      else product_raw[lane] = 0;
      product_sum += {16'b0, product_raw[lane]};
    end
  end

  assign cmd_ready_o = state_q == E_IDLE;
  assign busy_o = state_q != E_IDLE;
  assign mem_req_valid_o = state_q == E_A0_REQ ||
                           state_q == E_A1_REQ ||
                           state_q == E_B0_REQ ||
                           state_q == E_B1_REQ ||
                           state_q == E_STORE_REQ;
  assign mem_req_write_o = state_q == E_STORE_REQ;
  assign mem_req_wdata_o = store_word_q ? output_hi_q : output_lo_q;
  assign mem_req_kill_o = 1'b0;
  assign mem_req_addr_o =
      state_q == E_A0_REQ ? a_addr_q :
      state_q == E_A1_REQ ? a_addr_q + XLEN'(8) :
      state_q == E_B0_REQ ? b_addr_q :
      state_q == E_B1_REQ ? b_addr_q + XLEN'(8) :
      c_addr_q + (store_word_q ? XLEN'(8) : XLEN'(0));

  always_ff @(posedge clk_i or negedge rst_ni) begin
    if (!rst_ni) begin
      state_q <= E_IDLE;
      a_base_q <= '0;
      b_base_q <= '0;
      c_base_q <= '0;
      a_row_base_q <= '0;
      a_addr_q <= '0;
      b_col_base_q <= '0;
      b_addr_q <= '0;
      c_addr_q <= '0;
      m_q <= '0;
      k_dim_q <= '0;
      n_q <= '0;
      row_q <= '0;
      col_q <= '0;
      k_q <= '0;
      l10_q <= 1'b0;
      degree_q <= '0;
      reduce_degree_q <= '0;
      pack_coeff_q <= '0;
      chunk_q <= '0;
      store_word_q <= 1'b0;
      a_elem_q <= '0;
      b_elem_q <= '0;
      product_sum_q <= '0;
      product_degree_q <= '0;
      mul_pipe_valid_q <= 1'b0;
      pack_value_q <= '0;
      output_lo_q <= '0;
      output_hi_q <= '0;
      mac_count_q <= '0;
      rsp_valid_o <= 1'b0;
      rsp_data_o <= '0;
      for (int i = 0; i < 19; i++)
        accum_q[i] <= '0;
    end else begin
      rsp_valid_o <= 1'b0;
      unique case (state_q)
        E_IDLE: begin
          if (cmd_valid_i) begin
            if (!cmd_run_i) begin
              a_base_q <= a_i;
              b_base_q <= b_i;
              c_base_q <= c_i;
            end else if (a_i[7:0] == 0 || a_i[15:8] == 0 ||
                         a_i[23:16] == 0 || a_i[XLEN-1:25] != 0 ||
                         a_base_q[3:0] != 0 || b_base_q[3:0] != 0 ||
                         c_base_q[3:0] != 0) begin
              rsp_valid_o <= 1'b1;
              rsp_data_o <= {1'b1, {(XLEN-2){1'b0}}, 1'b1};
            end else begin
              m_q <= a_i[7:0];
              k_dim_q <= a_i[15:8];
              n_q <= a_i[23:16];
              l10_q <= a_i[24];
              row_q <= '0;
              col_q <= '0;
              k_q <= '0;
              a_row_base_q <= a_base_q;
              a_addr_q <= a_base_q;
              b_col_base_q <= b_base_q;
              b_addr_q <= b_base_q;
              c_addr_q <= c_base_q;
              mac_count_q <= '0;
              mul_pipe_valid_q <= 1'b0;
              for (int i = 0; i < 19; i++)
                accum_q[i] <= '0;
              state_q <= E_A0_REQ;
            end
          end
        end
        E_A0_REQ:
          if (mem_req_ready_i) state_q <= E_A0_WAIT;
        E_A0_WAIT:
          if (mem_rsp_valid_i) begin
            if (mem_rsp_error_i) begin
              state_q <= E_ERROR;
            end else begin
              a_elem_q[63:0] <= mem_rsp_data_i;
              a_elem_q[127:64] <= '0;
              state_q <= l10_q ? E_A1_REQ : E_B0_REQ;
            end
          end
        E_A1_REQ:
          if (mem_req_ready_i) state_q <= E_A1_WAIT;
        E_A1_WAIT:
          if (mem_rsp_valid_i) begin
            if (mem_rsp_error_i)
              state_q <= E_ERROR;
            else begin
              a_elem_q[127:64] <= mem_rsp_data_i;
              state_q <= E_B0_REQ;
            end
          end
        E_B0_REQ:
          if (mem_req_ready_i) state_q <= E_B0_WAIT;
        E_B0_WAIT:
          if (mem_rsp_valid_i) begin
            if (mem_rsp_error_i) begin
              state_q <= E_ERROR;
            end else begin
              b_elem_q[63:0] <= mem_rsp_data_i;
              b_elem_q[127:64] <= '0;
              state_q <= l10_q ? E_B1_REQ : E_MUL;
              degree_q <= '0;
              chunk_q <= '0;
              mul_pipe_valid_q <= 1'b0;
            end
          end
        E_B1_REQ:
          if (mem_req_ready_i) state_q <= E_B1_WAIT;
        E_B1_WAIT:
          if (mem_rsp_valid_i) begin
            if (mem_rsp_error_i)
              state_q <= E_ERROR;
            else begin
              b_elem_q[127:64] <= mem_rsp_data_i;
              degree_q <= '0;
              chunk_q <= '0;
              mul_pipe_valid_q <= 1'b0;
              state_q <= E_MUL;
            end
          end
        E_MUL: begin
          if (!l10_q) begin
            if (degree_q == 0) begin
              for (int lane=0;lane<4;lane++) kar_q[lane] <= product_raw[lane];
              degree_q <= 1;
            end else begin
              kar_q[4] <= product_raw[0];
              kar_q[5] <= product_raw[1];
              degree_q <= 0;
              state_q <= E_MUL_FLUSH;
            end
          end else begin
          if (mul_pipe_valid_q)
            accum_q[product_degree_q] <=
                accum_q[product_degree_q] + product_sum_q;
          product_sum_q <= product_sum;
          product_degree_q <= degree_q;
          mul_pipe_valid_q <= 1'b1;
          if (chunk_q == degree_chunks - 1'b1) begin
            chunk_q <= '0;
            if (degree_q == (l10_q ? 5'd18 : 5'd4)) begin
              degree_q <= '0;
              state_q <= E_MUL_FLUSH;
            end else begin
              degree_q <= degree_q + 1'b1;
            end
          end else begin
            chunk_q <= chunk_q + 1'b1;
          end
          end
        end
        E_MUL_FLUSH: begin
          if (!l10_q) begin
            accum_q[0] <= accum_q[0] + 32'(kar_q[0]);
            accum_q[1] <= accum_q[1] + 32'(kar_q[3])-32'(kar_q[0])-32'(kar_q[1]);
            accum_q[2] <= accum_q[2] + 32'(kar_q[4])-32'(kar_q[0])-32'(kar_q[2])+32'(kar_q[1]);
            accum_q[3] <= accum_q[3] + 32'(kar_q[5])-32'(kar_q[1])-32'(kar_q[2]);
            accum_q[4] <= accum_q[4] + 32'(kar_q[2]);
          end else accum_q[product_degree_q] <=
              accum_q[product_degree_q] + product_sum_q;
          mul_pipe_valid_q <= 1'b0;
          mac_count_q <= mac_count_q + (l10_q ? XLEN'(100) :
                                                 XLEN'(9));
          if (k_q == k_dim_q - 1'b1) begin
            reduce_degree_q <= l10_q ? 5'd18 : 5'd4;
            state_q <= E_REDUCE;
          end else begin
            k_q <= k_q + 1'b1;
            a_addr_q <= a_addr_q + XLEN'(16);
            b_addr_q <= b_addr_q + (XLEN'(n_q) << 4);
            state_q <= E_A0_REQ;
          end
        end
        E_REDUCE: begin
          if (l10_q) begin
            accum_q[0] <= accum_q[0] + accum_q[10] + (accum_q[17] << 1);
            accum_q[1] <= accum_q[1] + accum_q[11] + (accum_q[18] << 1);
            accum_q[2] <= accum_q[2] + accum_q[12];
            accum_q[3] <= accum_q[3] + (accum_q[10] << 1) + accum_q[13] + (accum_q[17] << 2);
            accum_q[4] <= accum_q[4] + (accum_q[11] << 1) + accum_q[14] + (accum_q[18] << 2);
            accum_q[5] <= accum_q[5] + (accum_q[12] << 1) + accum_q[15];
            accum_q[6] <= accum_q[6] + (accum_q[13] << 1) + accum_q[16];
            accum_q[7] <= accum_q[7] + (accum_q[14] << 1) + accum_q[17];
            accum_q[8] <= accum_q[8] + (accum_q[15] << 1) + accum_q[18];
            accum_q[9] <= accum_q[9] + (accum_q[16] << 1);
          end else begin
            accum_q[0] <= accum_q[0] + accum_q[3];
            accum_q[1] <= accum_q[1] + accum_q[3] + accum_q[4];
            accum_q[2] <= accum_q[2] + accum_q[4];
          end
          pack_coeff_q <= 0;
          output_lo_q <= 0;
          output_hi_q <= 0;
          state_q <= E_PACK_REDUCE;
        end
        E_PACK_REDUCE: begin
          pack_value_q <= mod127(accum_q[pack_coeff_q]);
          state_q <= E_PACK;
        end
        E_PACK: begin
          if (pack_coeff_q < 8)
            output_lo_q[8*pack_coeff_q +: 8] <= pack_value_q;
          else
            output_hi_q[8*(pack_coeff_q-8) +: 8] <= pack_value_q;
          if (pack_coeff_q == (l10_q ? 5'd9 : 5'd2)) begin
            store_word_q <= 1'b0;
            state_q <= E_STORE_REQ;
          end else begin
            pack_coeff_q <= pack_coeff_q + 1'b1;
            state_q <= E_PACK_REDUCE;
          end
        end
        E_STORE_REQ:
          if (mem_req_ready_i) state_q <= E_STORE_WAIT;
        E_STORE_WAIT:
          if (mem_rsp_valid_i) begin
            if (mem_rsp_error_i) begin
              state_q <= E_ERROR;
            end else if (l10_q && !store_word_q) begin
              store_word_q <= 1'b1;
              state_q <= E_STORE_REQ;
            end else begin
              k_q <= '0;
              store_word_q <= 1'b0;
              for (int i = 0; i < 19; i++)
                accum_q[i] <= '0;
              if (col_q != n_q - 1'b1) begin
                col_q <= col_q + 1'b1;
                a_addr_q <= a_row_base_q;
                b_col_base_q <= b_col_base_q + XLEN'(16);
                b_addr_q <= b_col_base_q + XLEN'(16);
                c_addr_q <= c_addr_q + XLEN'(16);
                state_q <= E_A0_REQ;
              end else if (row_q != m_q - 1'b1) begin
                row_q <= row_q + 1'b1;
                col_q <= '0;
                a_row_base_q <=
                    a_row_base_q + (XLEN'(k_dim_q) << 4);
                a_addr_q <= a_row_base_q + (XLEN'(k_dim_q) << 4);
                b_col_base_q <= b_base_q;
                b_addr_q <= b_base_q;
                c_addr_q <= c_addr_q + XLEN'(16);
                state_q <= E_A0_REQ;
              end else begin
                state_q <= E_DONE;
              end
            end
          end
        E_DONE: begin
          rsp_valid_o <= 1'b1;
          rsp_data_o <= mac_count_q;
          state_q <= E_IDLE;
        end
        E_ERROR: begin
          rsp_valid_o <= 1'b1;
          rsp_data_o <= {1'b1, {(XLEN-1){1'b0}}};
          state_q <= E_IDLE;
        end
        default: state_q <= E_ERROR;
      endcase
    end
  end
endmodule
