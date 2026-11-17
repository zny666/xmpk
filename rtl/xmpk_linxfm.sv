// Streamed small GF(16) linear transforms for extension-field basis changes.
module xmpk_linxfm #(
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
    L_IDLE,
    L_T_REQ,
    L_T_WAIT,
    L_X_REQ,
    L_X_WAIT,
    L_ROW,
    L_Y_REQ,
    L_Y_WAIT,
    L_DONE,
    L_ERROR
  } state_t;

  state_t state_q;
  logic [XLEN-1:0] t_base_q, x_base_q, y_base_q;
  logic [XLEN-1:0] t_addr_q, x_addr_q, y_addr_q;
  logic [4:0] l_q, t_row_q, row_q;
  logic [7:0] count_q, vector_q;
  (* ram_style = "distributed" *) logic [63:0] transform_q [0:15];
  logic [63:0] x_word_q, y_word_q;
  logic [XLEN-1:0] work_count_q;
  logic [3:0] row_next;
  logic [63:0] products0, products1;
  logic [3:0] dot0, dot1;
  logic second_row;

  assign cmd_ready_o = state_q == L_IDLE;
  assign busy_o = state_q != L_IDLE;
  assign mem_req_valid_o = state_q == L_T_REQ ||
                           state_q == L_X_REQ ||
                           state_q == L_Y_REQ;
  assign mem_req_write_o = state_q == L_Y_REQ;
  assign mem_req_wdata_o = y_word_q;
  assign mem_req_kill_o = 1'b0;
  assign mem_req_addr_o = state_q == L_T_REQ ? t_addr_q :
                          state_q == L_X_REQ ? x_addr_q : y_addr_q;

  assign second_row = row_q + 1'b1 < l_q;
  assign row_next = second_row ? row_q[3:0] + 1'b1 : row_q[3:0];
  for (genvar lane = 0; lane < 16; lane++) begin : g_lane
    gf2n_mul #(.W(4)) i_mul0 (
        .a_i(transform_q[row_q[3:0]][4*lane +: 4]),
        .b_i(x_word_q[4*lane +: 4]),
        .poly_i(4'h3),
        .z_o(products0[4*lane +: 4])
    );
    gf2n_mul #(.W(4)) i_mul1 (
        .a_i(transform_q[row_next][4*lane +: 4]),
        .b_i(x_word_q[4*lane +: 4]),
        .poly_i(4'h3),
        .z_o(products1[4*lane +: 4])
    );
  end
  always_comb begin
    dot0 = '0;
    dot1 = '0;
    for (int lane = 0; lane < 16; lane++) begin
      if (lane < l_q) begin
        dot0 ^= products0[4*lane +: 4];
        dot1 ^= products1[4*lane +: 4];
      end
    end
  end

  always_ff @(posedge clk_i)
    if (state_q == L_T_WAIT && mem_rsp_valid_i && !mem_rsp_error_i)
      transform_q[t_row_q[3:0]] <= mem_rsp_data_i;

  always_ff @(posedge clk_i or negedge rst_ni) begin
    if (!rst_ni) begin
      state_q <= L_IDLE;
      t_base_q <= '0;
      x_base_q <= '0;
      y_base_q <= '0;
      t_addr_q <= '0;
      x_addr_q <= '0;
      y_addr_q <= '0;
      l_q <= '0;
      t_row_q <= '0;
      row_q <= '0;
      count_q <= '0;
      vector_q <= '0;
      x_word_q <= '0;
      y_word_q <= '0;
      work_count_q <= '0;
      rsp_valid_o <= 1'b0;
      rsp_data_o <= '0;
    end else begin
      rsp_valid_o <= 1'b0;
      unique case (state_q)
        L_IDLE: begin
          if (cmd_valid_i) begin
            if (!cmd_run_i) begin
              t_base_q <= a_i;
              x_base_q <= b_i;
              y_base_q <= c_i;
            end else if (a_i[4:0] == 0 || a_i[4:0] > 5'd16 ||
                         a_i[15:8] == 0 ||
                         a_i[XLEN-1:16] != 0) begin
              rsp_valid_o <= 1'b1;
              rsp_data_o <= {1'b1, {(XLEN-2){1'b0}}, 1'b1};
            end else begin
              l_q <= a_i[4:0];
              count_q <= a_i[15:8];
              t_addr_q <= t_base_q;
              x_addr_q <= x_base_q;
              y_addr_q <= y_base_q;
              t_row_q <= '0;
              vector_q <= '0;
              work_count_q <= '0;
              state_q <= L_T_REQ;
            end
          end
        end
        L_T_REQ:
          if (mem_req_ready_i) state_q <= L_T_WAIT;
        L_T_WAIT:
          if (mem_rsp_valid_i) begin
            if (mem_rsp_error_i) begin
              state_q <= L_ERROR;
            end else begin
              t_addr_q <= t_addr_q + XLEN'(8);
              if (t_row_q == l_q - 1'b1) begin
                state_q <= L_X_REQ;
              end else begin
                t_row_q <= t_row_q + 1'b1;
                state_q <= L_T_REQ;
              end
            end
          end
        L_X_REQ:
          if (mem_req_ready_i) state_q <= L_X_WAIT;
        L_X_WAIT:
          if (mem_rsp_valid_i) begin
            if (mem_rsp_error_i) begin
              state_q <= L_ERROR;
            end else begin
              x_word_q <= mem_rsp_data_i;
              x_addr_q <= x_addr_q + XLEN'(8);
              y_word_q <= '0;
              row_q <= '0;
              state_q <= L_ROW;
            end
          end
        L_ROW: begin
          y_word_q[4*row_q[3:0] +: 4] <= dot0;
          if (second_row)
            y_word_q[4*row_next +: 4] <= dot1;
          work_count_q <= work_count_q + (second_row ? 2 : 1);
          if (!second_row || row_q + 1'b1 == l_q - 1'b1) begin
            state_q <= L_Y_REQ;
          end else begin
            row_q <= row_q + 2;
          end
        end
        L_Y_REQ:
          if (mem_req_ready_i) state_q <= L_Y_WAIT;
        L_Y_WAIT:
          if (mem_rsp_valid_i) begin
            if (mem_rsp_error_i) begin
              state_q <= L_ERROR;
            end else begin
              y_addr_q <= y_addr_q + XLEN'(8);
              if (vector_q == count_q - 1'b1) begin
                state_q <= L_DONE;
              end else begin
                vector_q <= vector_q + 1'b1;
                state_q <= L_X_REQ;
              end
            end
          end
        L_DONE: begin
          rsp_valid_o <= 1'b1;
          rsp_data_o <= work_count_q;
          state_q <= L_IDLE;
        end
        L_ERROR: begin
          rsp_valid_o <= 1'b1;
          rsp_data_o <= {1'b1, {(XLEN-1){1'b0}}};
          state_q <= L_IDLE;
        end
        default: state_q <= L_ERROR;
      endcase
    end
  end
endmodule
