// SPDX-License-Identifier: Apache-2.0 WITH SHL-2.0
module xmpk_solveq_ram (
    input  logic        clk_i,
    input  logic        a_we_i,
    input  logic [10:0] a_addr_i,
    input  logic [63:0] a_wdata_i,
    output logic [63:0] a_rdata_o,
    input  logic        b_we_i,
    input  logic [10:0] b_addr_i,
    input  logic [63:0] b_wdata_i,
    output logic [63:0] b_rdata_o
);
  (* ram_style = "block" *) logic [31:0] mem_lo [0:2047];
  (* ram_style = "block" *) logic [31:0] mem_hi [0:2047];
  always_ff @(posedge clk_i) begin
    if (a_we_i) begin
      mem_lo[a_addr_i] <= a_wdata_i[31:0];
      mem_hi[a_addr_i] <= a_wdata_i[63:32];
    end
    a_rdata_o <= {mem_hi[a_addr_i], mem_lo[a_addr_i]};
  end
  always_ff @(posedge clk_i) begin
    if (b_we_i) begin
      mem_lo[b_addr_i] <= b_wdata_i[31:0];
      mem_hi[b_addr_i] <= b_wdata_i[63:32];
    end
    b_rdata_o <= {mem_hi[b_addr_i], mem_lo[b_addr_i]};
  end
endmodule

// Constant-schedule GF(16)/AES-GF(256) Gaussian solver.
module xmpk_solveq #(
    parameter int unsigned XLEN = 64
) (
    input  logic            clk_i,
    input  logic            rst_ni,
    input  logic            cmd_valid_i,
    output logic            cmd_ready_o,
    input  logic            cmd_run_i,
    input  logic [XLEN-1:0] a_i,
    input  logic [XLEN-1:0] b_i,
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
    S_IDLE,
    S_LOAD_REQ,
    S_LOAD_WAIT,
    S_SCAN_READ,
    S_SCAN_CHECK,
    S_SWAP_READ,
    S_SWAP_WRITE,
    S_INV_READ,
    S_INV_CAPTURE,
    S_NORM_READ,
    S_NORM_WRITE,
    S_FACTOR_READ,
    S_FACTOR_CAPTURE,
    S_ELIM_READ,
    S_ELIM_WRITE,
    S_PACK_READ,
    S_PACK_CAPTURE,
    S_STORE_REQ,
    S_STORE_WAIT,
    S_DONE,
    S_ERROR
  } state_t;

  state_t state_q;
  logic backward_q;
  logic [3:0] pivot_word;
  assign pivot_word = field256_q ? pivot_q[6:3] : {1'b0,pivot_q[6:4]};

  logic [XLEN-1:0] matrix_base_q, solution_base_q;
  logic [XLEN-1:0] load_addr_q, store_addr_q;
  logic [6:0] n_q, load_row_q, pivot_q, scan_row_q, swap_row_q;
  logic [6:0] target_row_q, solution_row_q;
  logic [3:0] row_words_q, load_word_q, word_q;
  logic found_q, final_store_q, field256_q;
  logic [7:0] inv_q, factor_q;
  logic [63:0] solution_pack_q;
  (* ram_style = "distributed" *) logic [63:0] pivot_words_q [0:12];

  logic ram_a_we, ram_b_we;
  logic [10:0] ram_a_addr, ram_b_addr;
  logic [63:0] ram_a_wdata, ram_b_wdata;
  logic [63:0] ram_a_rdata, ram_b_rdata;
  logic [63:0] lane_product, lane_product16, lane_product256;
  logic [63:0] mul_word;
  logic [7:0] mul_b;
  logic [7:0] scan_coeff;
  logic [7:0] gf256_inv_table [0:255];

  function automatic logic [10:0] matrix_index(
      input logic [6:0] row,
      input logic [3:0] word
  );
    matrix_index = field256_q ?
        (11'(row) << 3) + (11'(row) << 2) + 11'(row) + 11'(word) :
        (11'(row) << 2) + 11'(row) + 11'(word);
  endfunction

  function automatic logic [3:0] gf16_inv(input logic [3:0] value);
    unique case (value)
      4'h1: gf16_inv = 4'h1;
      4'h2: gf16_inv = 4'h9;
      4'h3: gf16_inv = 4'he;
      4'h4: gf16_inv = 4'hd;
      4'h5: gf16_inv = 4'hb;
      4'h6: gf16_inv = 4'h7;
      4'h7: gf16_inv = 4'h6;
      4'h8: gf16_inv = 4'hf;
      4'h9: gf16_inv = 4'h2;
      4'ha: gf16_inv = 4'hc;
      4'hb: gf16_inv = 4'h5;
      4'hc: gf16_inv = 4'ha;
      4'hd: gf16_inv = 4'h4;
      4'he: gf16_inv = 4'h3;
      4'hf: gf16_inv = 4'h8;
      default: gf16_inv = 4'h0;
    endcase
  endfunction

  function automatic logic [7:0] gf256_mul_fn(
      input logic [7:0] lhs,
      input logic [7:0] rhs
  );
    logic [15:0] product;
    begin
      product = '0;
      for (int i = 0; i < 8; i++)
        if (rhs[i])
          product ^= {8'b0, lhs} << i;
      for (int i = 14; i >= 8; i--)
        if (product[i])
          product ^= 16'h11b << (i - 8);
      gf256_mul_fn = product[7:0];
    end
  endfunction

  initial begin
    gf256_inv_table[0] = 8'h00;
    for (int x = 1; x < 256; x++) begin
      gf256_inv_table[x] = 8'h00;
      for (int y = 1; y < 256; y++)
        if (gf256_mul_fn(8'(x), 8'(y)) == 8'h01)
          gf256_inv_table[x] = 8'(y);
    end
  end

  assign cmd_ready_o = state_q == S_IDLE;
  assign busy_o = state_q != S_IDLE;
  assign mem_req_valid_o = state_q == S_LOAD_REQ ||
                           state_q == S_STORE_REQ;
  assign mem_req_write_o = state_q == S_STORE_REQ;
  assign mem_req_wdata_o = solution_pack_q;
  assign mem_req_kill_o = 1'b0;
  assign mem_req_addr_o = state_q == S_LOAD_REQ ?
                          load_addr_q : store_addr_q;

  always_comb begin
    ram_a_we = 1'b0;
    ram_b_we = 1'b0;
    ram_a_addr = '0;
    ram_b_addr = '0;
    ram_a_wdata = '0;
    ram_b_wdata = '0;
    unique case (state_q)
      S_LOAD_WAIT: begin
        ram_a_we = mem_rsp_valid_i && !mem_rsp_error_i;
        ram_a_addr = matrix_index(load_row_q, load_word_q);
        ram_a_wdata = mem_rsp_data_i;
      end
      S_SCAN_READ:
        ram_a_addr = matrix_index(
            scan_row_q, field256_q ? pivot_q[6:3] :
                                     {1'b0, pivot_q[6:4]});
      S_SWAP_READ: begin
        ram_a_addr = matrix_index(pivot_q, word_q);
        ram_b_addr = matrix_index(swap_row_q, word_q);
      end
      S_SWAP_WRITE: begin
        if (swap_row_q != pivot_q) begin
          ram_a_we = 1'b1;
          ram_b_we = 1'b1;
          ram_a_addr = matrix_index(pivot_q, word_q);
          ram_b_addr = matrix_index(swap_row_q, word_q);
          ram_a_wdata = ram_b_rdata;
          ram_b_wdata = ram_a_rdata;
        end
      end
      S_INV_READ:
        ram_a_addr = matrix_index(
            pivot_q, field256_q ? pivot_q[6:3] :
                                  {1'b0, pivot_q[6:4]});
      S_NORM_READ:
        ram_b_addr = matrix_index(pivot_q, word_q);
      S_NORM_WRITE: begin
        ram_a_we = 1'b1;
        ram_a_addr = matrix_index(pivot_q, word_q);
        ram_a_wdata = lane_product;
        ram_b_addr = matrix_index(
            pivot_q,
            word_q == row_words_q - 1'b1 ? word_q : word_q + 1'b1);
      end
      S_FACTOR_READ:
        ram_a_addr = matrix_index(
            target_row_q, field256_q ? pivot_q[6:3] :
                                      {1'b0, pivot_q[6:4]});
      S_ELIM_READ:
        ram_b_addr = matrix_index(target_row_q, word_q);
      S_ELIM_WRITE: begin
        if (target_row_q != pivot_q) begin
          ram_a_we = 1'b1;
          ram_a_addr = matrix_index(target_row_q, word_q);
          ram_a_wdata = ram_b_rdata ^ lane_product;
        end
        ram_b_addr = matrix_index(
            target_row_q,
            word_q == row_words_q - 1'b1 ? word_q : word_q + 1'b1);
      end
      S_PACK_READ:
        ram_a_addr = matrix_index(
            solution_row_q, field256_q ? n_q[6:3] :
                                        {1'b0, n_q[6:4]});
      default: begin
      end
    endcase
  end

  assign mul_b = state_q == S_NORM_WRITE ? inv_q : factor_q;
  assign mul_word = state_q == S_NORM_WRITE ?
                    ram_b_rdata : pivot_words_q[word_q];
  for (genvar lane = 0; lane < 16; lane++) begin : g_lane
    gf2n_mul #(.W(4)) i_mul (
        .a_i(mul_word[4*lane +: 4]),
        .b_i(mul_b[3:0]),
        .poly_i(4'h3),
        .z_o(lane_product16[4*lane +: 4])
    );
  end
  for (genvar lane = 0; lane < 8; lane++) begin : g_lane256
    gf2n_mul #(.W(8)) i_mul (
        .a_i(mul_word[8*lane +: 8]),
        .b_i(mul_b),
        .poly_i(8'h1b),
        .z_o(lane_product256[8*lane +: 8])
    );
  end
  assign lane_product = field256_q ? lane_product256 : lane_product16;
  assign scan_coeff = field256_q ?
      ram_a_rdata[8*pivot_q[2:0] +: 8] :
      {4'b0, ram_a_rdata[4*pivot_q[3:0] +: 4]};

  xmpk_solveq_ram i_matrix (
      .clk_i(clk_i),
      .a_we_i(ram_a_we),
      .a_addr_i(ram_a_addr),
      .a_wdata_i(ram_a_wdata),
      .a_rdata_o(ram_a_rdata),
      .b_we_i(ram_b_we),
      .b_addr_i(ram_b_addr),
      .b_wdata_i(ram_b_wdata),
      .b_rdata_o(ram_b_rdata)
  );

  always_ff @(posedge clk_i)
    if (state_q == S_NORM_WRITE)
      pivot_words_q[word_q] <= lane_product;

  always_ff @(posedge clk_i or negedge rst_ni) begin
    if (!rst_ni) begin
      state_q <= S_IDLE;
      backward_q <= 0;
      matrix_base_q <= '0;
      solution_base_q <= '0;
      load_addr_q <= '0;
      store_addr_q <= '0;
      n_q <= '0;
      load_row_q <= '0;
      pivot_q <= '0;
      scan_row_q <= '0;
      swap_row_q <= '0;
      target_row_q <= '0;
      solution_row_q <= '0;
      row_words_q <= '0;
      load_word_q <= '0;
      word_q <= '0;
      found_q <= 1'b0;
      final_store_q <= 1'b0;
      field256_q <= 1'b0;
      inv_q <= '0;
      factor_q <= '0;
      solution_pack_q <= '0;
      rsp_valid_o <= 1'b0;
      rsp_data_o <= '0;
    end else begin
      rsp_valid_o <= 1'b0;
      unique case (state_q)
        S_IDLE: begin
          if (cmd_valid_i) begin
            if (!cmd_run_i) begin
              matrix_base_q <= a_i;
              solution_base_q <= b_i;
            end else if (a_i[6:0] == 0 ||
                         (!a_i[8] && a_i[6:0] > 7'd64) ||
                         (a_i[8] && a_i[6:0] > 7'd96) ||
                         a_i[7] || a_i[XLEN-1:9] != 0 ||
                         matrix_base_q[2:0] != 0 ||
                         solution_base_q[2:0] != 0) begin
              rsp_valid_o <= 1'b1;
              rsp_data_o <= {1'b1, {(XLEN-2){1'b0}}, 1'b1};
            end else begin
              backward_q <= 0;
              n_q <= a_i[6:0];
              field256_q <= a_i[8];
              row_words_q <= a_i[8] ?
                  4'(({1'b0, a_i[6:0]} + 8'd8) >> 3) :
                  4'(({1'b0, a_i[6:0]} + 8'd16) >> 4);
              load_addr_q <= matrix_base_q;
              store_addr_q <= solution_base_q;
              load_row_q <= '0;
              load_word_q <= '0;
              solution_pack_q <= '0;
              state_q <= S_LOAD_REQ;
            end
          end
        end
        S_LOAD_REQ:
          if (mem_req_ready_i) state_q <= S_LOAD_WAIT;
        S_LOAD_WAIT:
          if (mem_rsp_valid_i) begin
            if (mem_rsp_error_i) begin
              state_q <= S_ERROR;
            end else begin
              load_addr_q <= load_addr_q + XLEN'(8);
              if (load_word_q == row_words_q - 1'b1) begin
                load_word_q <= '0;
                if (load_row_q == n_q - 1'b1) begin
                  pivot_q <= '0;
                  scan_row_q <= '0;
                  found_q <= 1'b0;
                  state_q <= S_SCAN_READ;
                end else begin
                  load_row_q <= load_row_q + 1'b1;
                  state_q <= S_LOAD_REQ;
                end
              end else begin
                load_word_q <= load_word_q + 1'b1;
                state_q <= S_LOAD_REQ;
              end
            end
          end
        S_SCAN_READ:
          state_q <= S_SCAN_CHECK;
        S_SCAN_CHECK: begin
          if (!found_q && scan_coeff != 0) begin
            found_q <= 1'b1;
            swap_row_q <= scan_row_q;
          end
          if (scan_row_q == n_q - 1'b1) begin
            if (!found_q && scan_coeff == 0) begin
              state_q <= S_ERROR;
            end else begin
              if (!found_q)
                swap_row_q <= scan_row_q;
              word_q <= pivot_word;
              state_q <= S_SWAP_READ;
            end
          end else begin
            scan_row_q <= scan_row_q + 1'b1;
            state_q <= S_SCAN_READ;
          end
        end
        S_SWAP_READ:
          state_q <= S_SWAP_WRITE;
        S_SWAP_WRITE:
          if (word_q == row_words_q - 1'b1) begin
            word_q <= '0;
            state_q <= S_INV_READ;
          end else begin
            word_q <= word_q + 1'b1;
            state_q <= S_SWAP_READ;
          end
        S_INV_READ:
          state_q <= S_INV_CAPTURE;
        S_INV_CAPTURE: begin
          inv_q <= field256_q ? gf256_inv_table[scan_coeff] :
                                {4'b0, gf16_inv(scan_coeff[3:0])};
          word_q <= pivot_word;
          state_q <= S_NORM_READ;
        end
        S_NORM_READ:
          state_q <= S_NORM_WRITE;
        S_NORM_WRITE: begin
          if (word_q == row_words_q - 1'b1) begin
            if (!backward_q && pivot_q != n_q-1) begin
              target_row_q <= pivot_q+1;
              state_q <= S_FACTOR_READ;
            end else if (!backward_q && n_q > 1) begin
              backward_q <= 1;
              target_row_q <= 0;
              state_q <= S_FACTOR_READ;
            end else if (backward_q) begin
              target_row_q <= 0;
              state_q <= S_FACTOR_READ;
            end else begin
              solution_row_q <= 0;
              final_store_q <= 0;
              solution_pack_q <= 0;
              state_q <= S_PACK_READ;
            end
            word_q <= 0;
          end else begin
            word_q <= word_q + 1'b1;
          end
        end
        S_FACTOR_READ:
          state_q <= S_FACTOR_CAPTURE;
        S_FACTOR_CAPTURE: begin
          factor_q <= scan_coeff;
          word_q <= backward_q ? row_words_q-1 : pivot_word;
          state_q <= S_ELIM_READ;
        end
        S_ELIM_READ:
          state_q <= S_ELIM_WRITE;
        S_ELIM_WRITE:
          if (word_q == row_words_q - 1'b1) begin
            word_q <= 0;
            if (target_row_q == (backward_q ? pivot_q-1 : n_q-1)) begin
              if (backward_q && pivot_q == 1) begin
                solution_row_q <= 0;
                solution_pack_q <= 0;
                final_store_q <= 0;
                state_q <= S_PACK_READ;
              end else if (backward_q) begin
                pivot_q <= pivot_q-1;
                // Capture this already-normalized row's RHS-containing word.
                word_q <= row_words_q-1;
                state_q <= S_NORM_READ;
                inv_q <= 1;
              end else begin
                pivot_q <= pivot_q+1;
                scan_row_q <= pivot_q+1;
                found_q <= 0;
                state_q <= S_SCAN_READ;
              end
            end else begin
              target_row_q <= target_row_q+1;
              state_q <= S_FACTOR_READ;
            end
          end else word_q <= word_q+1;
        S_PACK_READ:
          state_q <= S_PACK_CAPTURE;
        S_PACK_CAPTURE: begin
          if (field256_q)
            solution_pack_q[8*solution_row_q[2:0] +: 8] <=
                ram_a_rdata[8*n_q[2:0] +: 8];
          else
            solution_pack_q[4*solution_row_q[3:0] +: 4] <=
                ram_a_rdata[4*n_q[3:0] +: 4];
          if (solution_row_q == n_q - 1'b1) begin
            final_store_q <= 1'b1;
            state_q <= S_STORE_REQ;
          end else begin
            solution_row_q <= solution_row_q + 1'b1;
            if ((field256_q && solution_row_q[2:0] == 3'h7) ||
                (!field256_q && solution_row_q[3:0] == 4'hf)) begin
              final_store_q <= 1'b0;
              state_q <= S_STORE_REQ;
            end else begin
              state_q <= S_PACK_READ;
            end
          end
        end
        S_STORE_REQ:
          if (mem_req_ready_i) state_q <= S_STORE_WAIT;
        S_STORE_WAIT:
          if (mem_rsp_valid_i) begin
            if (mem_rsp_error_i) begin
              state_q <= S_ERROR;
            end else begin
              store_addr_q <= store_addr_q + XLEN'(8);
              solution_pack_q <= '0;
              if (final_store_q)
                state_q <= S_DONE;
              else
                state_q <= S_PACK_READ;
            end
          end
        S_DONE: begin
          rsp_valid_o <= 1'b1;
          rsp_data_o <= XLEN'(n_q);
          state_q <= S_IDLE;
        end
        S_ERROR: begin
          rsp_valid_o <= 1'b1;
          rsp_data_o <= {1'b1, {(XLEN-1){1'b0}}};
          state_q <= S_IDLE;
        end
        default: state_q <= S_ERROR;
      endcase
    end
  end
endmodule
