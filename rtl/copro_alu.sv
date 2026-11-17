// Modified for Xmpk: custom instruction dispatch, operand handling,
// and/or accelerator integration. Original notices are retained.
// Copyright 2024 Thales DIS France SAS
//
// Licensed under the Solderpad Hardware Licence, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// SPDX-License-Identifier: Apache-2.0 WITH SHL-2.0
// You may obtain a copy of the License at https://solderpad.org/licenses/
//
// Original Author: Guillaume Chauvon

module copro_alu
  import cvxif_instr_pkg::*;
#(
    parameter int unsigned NrRgprPorts = 2,
    parameter int unsigned XLEN = 32,
    parameter type hartid_t = logic,
    parameter type id_t = logic,
    parameter type registers_t = logic

) (
    input  logic                  clk_i,
    input  logic                  rst_ni,
    input  registers_t            registers_i,
    input  opcode_t               opcode_i,
    input  hartid_t               hartid_i,
    input  id_t                   id_i,
    input  logic       [     4:0] rd_i,
    input  logic                  issue_valid_i,
    output logic       [XLEN-1:0] result_o,
    output hartid_t               hartid_o,
    output id_t                   id_o,
    output logic       [     4:0] rd_o,
    output logic                  valid_o,
    output logic                  we_o,
    output logic                  busy_o,
    output logic                  mem_req_valid_o,
    input  logic                  mem_req_ready_i,
    output logic       [XLEN-1:0] mem_req_addr_o,
    output logic                  mem_req_write_o,
    output logic       [XLEN-1:0] mem_req_wdata_o,
    output logic                  mem_req_kill_o,
    input  logic                  mem_rsp_valid_i,
    input  logic       [XLEN-1:0] mem_rsp_data_i,
    input  logic                  mem_rsp_error_i
);

  logic [XLEN-1:0] result_n, result_q;
  hartid_t hartid_n, hartid_q;
  id_t id_n, id_q;
  logic valid_n, valid_q;
  logic [4:0] rd_n, rd_q;
  logic we_n, we_q;
  logic stateful_op, fast_op, eng_cmd_valid, eng_cmd_ready;
  logic [3:0] eng_cmd_op;
  logic eng_rsp_valid, eng_busy;
  logic [XLEN-1:0] eng_rsp_data;
  logic qg_cmd_valid, qg_cmd_ready, qg_cmd_run;
  logic qg_rsp_valid, qg_busy;
  logic [XLEN-1:0] qg_rsp_data;
  logic ger_mem_req_valid, ger_mem_req_write, ger_mem_req_kill;
  logic [XLEN-1:0] ger_mem_req_addr, ger_mem_req_wdata;
  logic qg_mem_req_valid, qg_mem_req_write, qg_mem_req_kill;
  logic [XLEN-1:0] qg_mem_req_addr, qg_mem_req_wdata;
  logic qg_opcode;
  logic mq_cmd_valid, mq_cmd_ready, mq_cmd_run;
  logic mq_rsp_valid, mq_busy;
  logic [XLEN-1:0] mq_rsp_data;
  logic mq_mem_req_valid, mq_mem_req_write, mq_mem_req_kill;
  logic [XLEN-1:0] mq_mem_req_addr, mq_mem_req_wdata;
  logic mq_opcode;
  logic sq_cmd_valid, sq_cmd_ready, sq_cmd_run;
  logic sq_rsp_valid, sq_busy;
  logic [XLEN-1:0] sq_rsp_data;
  logic sq_mem_req_valid, sq_mem_req_write, sq_mem_req_kill;
  logic [XLEN-1:0] sq_mem_req_addr, sq_mem_req_wdata;
  logic sq_opcode;
  logic lx_cmd_valid, lx_cmd_ready, lx_cmd_run;
  logic lx_rsp_valid, lx_busy;
  logic [XLEN-1:0] lx_rsp_data;
  logic lx_mem_req_valid, lx_mem_req_write, lx_mem_req_kill;
  logic [XLEN-1:0] lx_mem_req_addr, lx_mem_req_wdata;
  logic lx_opcode;
  logic ex_cmd_valid, ex_cmd_ready, ex_cmd_run;
  logic ex_rsp_valid, ex_busy;
  logic [XLEN-1:0] ex_rsp_data;
  logic ex_mem_req_valid, ex_mem_req_write, ex_mem_req_kill;
  logic [XLEN-1:0] ex_mem_req_addr, ex_mem_req_wdata;
  logic ex_opcode;
  logic inv_start, inv_busy, inv_done;
  logic [XLEN-1:0] inv_data;
  logic incoming_slow, slow_start, slow_busy, slow_done;
  logic [1:0] slow_op;
  logic [XLEN-1:0] slow_data;
  hartid_t slow_hartid_s1_q,slow_hartid_s2_q;
  id_t slow_id_s1_q,slow_id_s2_q;
  logic [4:0] slow_rd_s1_q,slow_rd_s2_q;
  logic state_rsp_valid;
  logic [XLEN-1:0] state_rsp_data;
  logic pending_q;
  hartid_t pending_hartid_q;
  id_t pending_id_q;
  logic [4:0] pending_rd_q;
  logic dispatch_pending_q;
  registers_t dispatch_registers_q;
  opcode_t dispatch_opcode_q;
  hartid_t dispatch_hartid_q;
  id_t dispatch_id_q;
  logic [4:0] dispatch_rd_q;
  logic dispatch_stateful_q;
  logic dispatch_fast_q, dispatch_slow_q, dispatch_inv_q;

  assign result_o = result_q;
  assign hartid_o = hartid_q;
  assign id_o     = id_q;
  assign valid_o  = valid_q;
  assign rd_o     = rd_q;
  assign we_o     = we_q;
  assign busy_o   = dispatch_pending_q | pending_q | eng_busy | qg_busy |
                    mq_busy | sq_busy | lx_busy | ex_busy | inv_busy |
                    slow_busy;

  always_comb begin
    stateful_op = 1'b1;
    unique case (opcode_i)
      cvxif_instr_pkg::TRF_WRITE,
      cvxif_instr_pkg::TRF_READ,
      cvxif_instr_pkg::GER16,
      cvxif_instr_pkg::GER16R,
      cvxif_instr_pkg::BGER16R,
      cvxif_instr_pkg::ATRF_LOAD,
      cvxif_instr_pkg::AGER16R,
      cvxif_instr_pkg::ATRF_LOAD16,
      cvxif_instr_pkg::AGER16X16,
      cvxif_instr_pkg::AGER16R2,
      cvxif_instr_pkg::GER_SETUP,
      cvxif_instr_pkg::GER_RUN,
      cvxif_instr_pkg::XMEM_STORE,
      cvxif_instr_pkg::QGEMM_SETUP,
      cvxif_instr_pkg::QGEMM_RUN,
      cvxif_instr_pkg::MQEVAL_SETUP,
      cvxif_instr_pkg::MQEVAL_RUN,
      cvxif_instr_pkg::SOLVEQ_SETUP,
      cvxif_instr_pkg::SOLVEQ_RUN,
      cvxif_instr_pkg::LINXFM_SETUP,
      cvxif_instr_pkg::LINXFM_RUN,
      cvxif_instr_pkg::EXTGEMM_SETUP,
      cvxif_instr_pkg::EXTGEMM_RUN: begin
      end
      default: stateful_op = 1'b0;
    endcase

    fast_op = 1'b1;
    unique case (opcode_i)
      cvxif_instr_pkg::GF16_MUL,
      cvxif_instr_pkg::GF16_MACC,
      cvxif_instr_pkg::VBROAD16,
      cvxif_instr_pkg::VBROAD256,
      cvxif_instr_pkg::XORFOLD16,
      cvxif_instr_pkg::XORFOLD256,
      cvxif_instr_pkg::FNZ16,
      cvxif_instr_pkg::FNZ256,
      cvxif_instr_pkg::PIVOT16,
      cvxif_instr_pkg::ROWELIM16: begin
      end
      default: fast_op = 1'b0;
    endcase

    unique case (dispatch_opcode_q)
      cvxif_instr_pkg::TRF_WRITE: eng_cmd_op = 3'd0;
      cvxif_instr_pkg::TRF_READ : eng_cmd_op = 3'd1;
      cvxif_instr_pkg::GER16    : eng_cmd_op = 3'd2;
      cvxif_instr_pkg::GER16R   : eng_cmd_op = 3'd3;
      cvxif_instr_pkg::BGER16R  : eng_cmd_op = 3'd4;
      cvxif_instr_pkg::ATRF_LOAD: eng_cmd_op = 3'd5;
      cvxif_instr_pkg::AGER16R  : eng_cmd_op = 3'd6;
      cvxif_instr_pkg::ATRF_LOAD16: eng_cmd_op = 4'd7;
      cvxif_instr_pkg::AGER16X16  : eng_cmd_op = 4'd8;
      cvxif_instr_pkg::AGER16R2   : eng_cmd_op = 4'd9;
      cvxif_instr_pkg::GER_SETUP  : eng_cmd_op = 4'd10;
      cvxif_instr_pkg::GER_RUN    : eng_cmd_op = 4'd11;
      cvxif_instr_pkg::XMEM_STORE : eng_cmd_op = 4'd12;
      cvxif_instr_pkg::QGEMM_SETUP,
      cvxif_instr_pkg::QGEMM_RUN  : eng_cmd_op = 4'd0;
      cvxif_instr_pkg::MQEVAL_SETUP,
      cvxif_instr_pkg::MQEVAL_RUN  : eng_cmd_op = 4'd0;
      cvxif_instr_pkg::SOLVEQ_SETUP,
      cvxif_instr_pkg::SOLVEQ_RUN  : eng_cmd_op = 4'd0;
      cvxif_instr_pkg::LINXFM_SETUP,
      cvxif_instr_pkg::LINXFM_RUN  : eng_cmd_op = 4'd0;
      cvxif_instr_pkg::EXTGEMM_SETUP,
      cvxif_instr_pkg::EXTGEMM_RUN : eng_cmd_op = 4'd0;
      default: eng_cmd_op = 4'd0;
    endcase
  end
  assign qg_opcode =
      dispatch_opcode_q == cvxif_instr_pkg::QGEMM_SETUP ||
      dispatch_opcode_q == cvxif_instr_pkg::QGEMM_RUN;
  assign mq_opcode =
      dispatch_opcode_q == cvxif_instr_pkg::MQEVAL_SETUP ||
      dispatch_opcode_q == cvxif_instr_pkg::MQEVAL_RUN;
  assign sq_opcode =
      dispatch_opcode_q == cvxif_instr_pkg::SOLVEQ_SETUP ||
      dispatch_opcode_q == cvxif_instr_pkg::SOLVEQ_RUN;
  assign lx_opcode =
      dispatch_opcode_q == cvxif_instr_pkg::LINXFM_SETUP ||
      dispatch_opcode_q == cvxif_instr_pkg::LINXFM_RUN;
  assign ex_opcode =
      dispatch_opcode_q == cvxif_instr_pkg::EXTGEMM_SETUP ||
      dispatch_opcode_q == cvxif_instr_pkg::EXTGEMM_RUN;
  assign eng_cmd_valid = dispatch_pending_q && dispatch_stateful_q &&
                         !qg_opcode && !mq_opcode && !sq_opcode &&
                         !lx_opcode && !ex_opcode;
  assign qg_cmd_valid = dispatch_pending_q && dispatch_stateful_q &&
                        qg_opcode;
  assign qg_cmd_run =
      dispatch_opcode_q == cvxif_instr_pkg::QGEMM_RUN;
  assign mq_cmd_valid = dispatch_pending_q && dispatch_stateful_q &&
                        mq_opcode;
  assign mq_cmd_run =
      dispatch_opcode_q == cvxif_instr_pkg::MQEVAL_RUN;
  assign sq_cmd_valid = dispatch_pending_q && dispatch_stateful_q &&
                        sq_opcode;
  assign sq_cmd_run =
      dispatch_opcode_q == cvxif_instr_pkg::SOLVEQ_RUN;
  assign lx_cmd_valid = dispatch_pending_q && dispatch_stateful_q &&
                        lx_opcode;
  assign lx_cmd_run =
      dispatch_opcode_q == cvxif_instr_pkg::LINXFM_RUN;
  assign ex_cmd_valid = dispatch_pending_q && dispatch_stateful_q &&
                        ex_opcode;
  assign ex_cmd_run =
      dispatch_opcode_q == cvxif_instr_pkg::EXTGEMM_RUN;

  xmpk_ger_trf #(.XLEN(XLEN)) i_ger_trf (
      .clk_i(clk_i),
      .rst_ni(rst_ni),
      .cmd_valid_i(eng_cmd_valid),
      .cmd_ready_o(eng_cmd_ready),
      .cmd_op_i(eng_cmd_op),
      .a_i(dispatch_registers_q[0]),
      .b_i(dispatch_registers_q[1]),
      .c_i(NrRgprPorts == 3 ? dispatch_registers_q[2] : '0),
      .rsp_valid_o(eng_rsp_valid),
      .rsp_data_o(eng_rsp_data),
      .busy_o(eng_busy),
      .mem_req_valid_o(ger_mem_req_valid),
      .mem_req_ready_i(mem_req_ready_i),
      .mem_req_addr_o(ger_mem_req_addr),
      .mem_req_write_o(ger_mem_req_write),
      .mem_req_wdata_o(ger_mem_req_wdata),
      .mem_req_kill_o(ger_mem_req_kill),
      .mem_rsp_valid_i(mem_rsp_valid_i),
      .mem_rsp_data_i(mem_rsp_data_i),
      .mem_rsp_error_i(mem_rsp_error_i)
  );
  xmpk_qgemm #(.XLEN(XLEN)) i_qgemm (
      .clk_i(clk_i),
      .rst_ni(rst_ni),
      .cmd_valid_i(qg_cmd_valid),
      .cmd_ready_o(qg_cmd_ready),
      .cmd_run_i(qg_cmd_run),
      .a_i(dispatch_registers_q[0]),
      .b_i(dispatch_registers_q[1]),
      .c_i(NrRgprPorts == 3 ? dispatch_registers_q[2] : '0),
      .rsp_valid_o(qg_rsp_valid),
      .rsp_data_o(qg_rsp_data),
      .busy_o(qg_busy),
      .mem_req_valid_o(qg_mem_req_valid),
      .mem_req_ready_i(mem_req_ready_i),
      .mem_req_addr_o(qg_mem_req_addr),
      .mem_req_write_o(qg_mem_req_write),
      .mem_req_wdata_o(qg_mem_req_wdata),
      .mem_req_kill_o(qg_mem_req_kill),
      .mem_rsp_valid_i(mem_rsp_valid_i),
      .mem_rsp_data_i(mem_rsp_data_i),
      .mem_rsp_error_i(mem_rsp_error_i)
  );
  xmpk_mqeval #(.XLEN(XLEN)) i_mqeval (
      .clk_i(clk_i),
      .rst_ni(rst_ni),
      .cmd_valid_i(mq_cmd_valid),
      .cmd_ready_o(mq_cmd_ready),
      .cmd_run_i(mq_cmd_run),
      .a_i(dispatch_registers_q[0]),
      .b_i(dispatch_registers_q[1]),
      .c_i(NrRgprPorts == 3 ? dispatch_registers_q[2] : '0),
      .rsp_valid_o(mq_rsp_valid),
      .rsp_data_o(mq_rsp_data),
      .busy_o(mq_busy),
      .mem_req_valid_o(mq_mem_req_valid),
      .mem_req_ready_i(mem_req_ready_i),
      .mem_req_addr_o(mq_mem_req_addr),
      .mem_req_write_o(mq_mem_req_write),
      .mem_req_wdata_o(mq_mem_req_wdata),
      .mem_req_kill_o(mq_mem_req_kill),
      .mem_rsp_valid_i(mem_rsp_valid_i),
      .mem_rsp_data_i(mem_rsp_data_i),
      .mem_rsp_error_i(mem_rsp_error_i)
  );
  xmpk_solveq #(.XLEN(XLEN)) i_solveq (
      .clk_i(clk_i),
      .rst_ni(rst_ni),
      .cmd_valid_i(sq_cmd_valid),
      .cmd_ready_o(sq_cmd_ready),
      .cmd_run_i(sq_cmd_run),
      .a_i(dispatch_registers_q[0]),
      .b_i(dispatch_registers_q[1]),
      .rsp_valid_o(sq_rsp_valid),
      .rsp_data_o(sq_rsp_data),
      .busy_o(sq_busy),
      .mem_req_valid_o(sq_mem_req_valid),
      .mem_req_ready_i(mem_req_ready_i),
      .mem_req_addr_o(sq_mem_req_addr),
      .mem_req_write_o(sq_mem_req_write),
      .mem_req_wdata_o(sq_mem_req_wdata),
      .mem_req_kill_o(sq_mem_req_kill),
      .mem_rsp_valid_i(mem_rsp_valid_i),
      .mem_rsp_data_i(mem_rsp_data_i),
      .mem_rsp_error_i(mem_rsp_error_i)
  );
  xmpk_linxfm #(.XLEN(XLEN)) i_linxfm (
      .clk_i(clk_i),
      .rst_ni(rst_ni),
      .cmd_valid_i(lx_cmd_valid),
      .cmd_ready_o(lx_cmd_ready),
      .cmd_run_i(lx_cmd_run),
      .a_i(dispatch_registers_q[0]),
      .b_i(dispatch_registers_q[1]),
      .c_i(NrRgprPorts == 3 ? dispatch_registers_q[2] : '0),
      .rsp_valid_o(lx_rsp_valid),
      .rsp_data_o(lx_rsp_data),
      .busy_o(lx_busy),
      .mem_req_valid_o(lx_mem_req_valid),
      .mem_req_ready_i(mem_req_ready_i),
      .mem_req_addr_o(lx_mem_req_addr),
      .mem_req_write_o(lx_mem_req_write),
      .mem_req_wdata_o(lx_mem_req_wdata),
      .mem_req_kill_o(lx_mem_req_kill),
      .mem_rsp_valid_i(mem_rsp_valid_i),
      .mem_rsp_data_i(mem_rsp_data_i),
      .mem_rsp_error_i(mem_rsp_error_i)
  );
  xmpk_extgemm #(.XLEN(XLEN)) i_extgemm (
      .clk_i(clk_i),
      .rst_ni(rst_ni),
      .cmd_valid_i(ex_cmd_valid),
      .cmd_ready_o(ex_cmd_ready),
      .cmd_run_i(ex_cmd_run),
      .a_i(dispatch_registers_q[0]),
      .b_i(dispatch_registers_q[1]),
      .c_i(NrRgprPorts == 3 ? dispatch_registers_q[2] : '0),
      .rsp_valid_o(ex_rsp_valid),
      .rsp_data_o(ex_rsp_data),
      .busy_o(ex_busy),
      .mem_req_valid_o(ex_mem_req_valid),
      .mem_req_ready_i(mem_req_ready_i),
      .mem_req_addr_o(ex_mem_req_addr),
      .mem_req_write_o(ex_mem_req_write),
      .mem_req_wdata_o(ex_mem_req_wdata),
      .mem_req_kill_o(ex_mem_req_kill),
      .mem_rsp_valid_i(mem_rsp_valid_i),
      .mem_rsp_data_i(mem_rsp_data_i),
      .mem_rsp_error_i(mem_rsp_error_i)
  );
  assign mem_req_valid_o = ex_busy ? ex_mem_req_valid :
                           lx_busy ? lx_mem_req_valid :
                           sq_busy ? sq_mem_req_valid :
                           mq_busy ? mq_mem_req_valid :
                           qg_busy ? qg_mem_req_valid : ger_mem_req_valid;
  assign mem_req_addr_o  = ex_busy ? ex_mem_req_addr :
                           lx_busy ? lx_mem_req_addr :
                           sq_busy ? sq_mem_req_addr :
                           mq_busy ? mq_mem_req_addr :
                           qg_busy ? qg_mem_req_addr : ger_mem_req_addr;
  assign mem_req_write_o = ex_busy ? ex_mem_req_write :
                           lx_busy ? lx_mem_req_write :
                           sq_busy ? sq_mem_req_write :
                           mq_busy ? mq_mem_req_write :
                           qg_busy ? qg_mem_req_write : ger_mem_req_write;
  assign mem_req_wdata_o = ex_busy ? ex_mem_req_wdata :
                           lx_busy ? lx_mem_req_wdata :
                           sq_busy ? sq_mem_req_wdata :
                           mq_busy ? mq_mem_req_wdata :
                           qg_busy ? qg_mem_req_wdata : ger_mem_req_wdata;
  assign mem_req_kill_o  = ex_busy ? ex_mem_req_kill :
                           lx_busy ? lx_mem_req_kill :
                           sq_busy ? sq_mem_req_kill :
                           mq_busy ? mq_mem_req_kill :
                           qg_busy ? qg_mem_req_kill : ger_mem_req_kill;
  assign inv_start = dispatch_pending_q && dispatch_inv_q;
  gf256_inv_rom #(.XLEN(XLEN)) i_inv_rom (
      .clk_i(clk_i),
      .rst_ni(rst_ni),
      .start_i(inv_start),
      .a_i(dispatch_registers_q[0]),
      .busy_o(inv_busy),
      .done_o(inv_done),
      .z_o(inv_data)
  );
  assign incoming_slow = opcode_i == cvxif_instr_pkg::GF256_MUL ||
                         opcode_i == cvxif_instr_pkg::GF127_MUL ||
                         opcode_i == cvxif_instr_pkg::GF16_DOT;
  assign slow_start = dispatch_pending_q && dispatch_slow_q;
  assign slow_op =
      dispatch_opcode_q == cvxif_instr_pkg::GF16_DOT ? 2'd2 :
      dispatch_opcode_q == cvxif_instr_pkg::GF127_MUL ? 2'd1 : 2'd0;
  xmpk_slow_mul #(.XLEN(XLEN)) i_slow_mul (
      .clk_i(clk_i),
      .rst_ni(rst_ni),
      .start_i(slow_start),
      .op_i(slow_op),
      .a_i(dispatch_registers_q[0]),
      .b_i(dispatch_registers_q[1]),
      .busy_o(slow_busy),
      .done_o(slow_done),
      .z_o(slow_data)
  );
  assign state_rsp_valid = eng_rsp_valid | qg_rsp_valid | mq_rsp_valid |
                           sq_rsp_valid | lx_rsp_valid | ex_rsp_valid |
                           inv_done;
  assign state_rsp_data = inv_done ? inv_data :
                          ex_rsp_valid ? ex_rsp_data :
                          lx_rsp_valid ? lx_rsp_data :
                          sq_rsp_valid ? sq_rsp_data :
                          mq_rsp_valid ? mq_rsp_data :
                          qg_rsp_valid ? qg_rsp_data : eng_rsp_data;


  logic [3:0]      xmpk_op;
  logic [XLEN-1:0] xmpk_z;
  always_comb begin
    unique case (dispatch_opcode_q)
      cvxif_instr_pkg::GF16_MUL  : xmpk_op = 4'd0;
      cvxif_instr_pkg::GF16_MACC : xmpk_op = 4'd1;
      cvxif_instr_pkg::GF256_MUL : xmpk_op = 4'd2;
      cvxif_instr_pkg::GF127_MUL : xmpk_op = 4'd3;
      cvxif_instr_pkg::VBROAD16  : xmpk_op = 4'd4;
      cvxif_instr_pkg::VBROAD256 : xmpk_op = 4'd5;
      cvxif_instr_pkg::XORFOLD16 : xmpk_op = 4'd6;
      cvxif_instr_pkg::XORFOLD256: xmpk_op = 4'd7;
      cvxif_instr_pkg::GF256_INV : xmpk_op = 4'd8;
      cvxif_instr_pkg::FNZ16     : xmpk_op = 4'd9;
      cvxif_instr_pkg::FNZ256    : xmpk_op = 4'd10;
      cvxif_instr_pkg::PIVOT16   : xmpk_op = 4'd11;
      cvxif_instr_pkg::ROWELIM16 : xmpk_op = 4'd12;
      default                    : xmpk_op = 4'd0;
    endcase
  end
  xmpk_alu #(.XLEN(XLEN)) i_xmpk_alu (
      .op_i(xmpk_op),
      .a_i (dispatch_registers_q[0]),
      .b_i (dispatch_registers_q[1]),
      .c_i (NrRgprPorts == 3 ? dispatch_registers_q[2] : '0),
      .z_o (xmpk_z)
  );
  always_comb begin
    case (opcode_i)
      cvxif_instr_pkg::NOP: begin
        result_n = '0;
        hartid_n = hartid_i;
        id_n     = id_i;
        valid_n  = 1'b1;
        rd_n     = '0;
        we_n     = '0;
      end
      cvxif_instr_pkg::TRF_WRITE,
      cvxif_instr_pkg::GER_SETUP,
      cvxif_instr_pkg::QGEMM_SETUP,
      cvxif_instr_pkg::MQEVAL_SETUP,
      cvxif_instr_pkg::SOLVEQ_SETUP,
      cvxif_instr_pkg::LINXFM_SETUP,
      cvxif_instr_pkg::EXTGEMM_SETUP: begin
        result_n = '0;
        hartid_n = hartid_i;
        id_n     = id_i;
        valid_n  = 1'b1;
        rd_n     = '0;
        we_n     = 1'b0;
      end
      cvxif_instr_pkg::TRF_READ,
      cvxif_instr_pkg::GER16,
      cvxif_instr_pkg::GER16R,
      cvxif_instr_pkg::BGER16R,
      cvxif_instr_pkg::ATRF_LOAD,
      cvxif_instr_pkg::AGER16R,
      cvxif_instr_pkg::ATRF_LOAD16,
      cvxif_instr_pkg::AGER16X16,
      cvxif_instr_pkg::AGER16R2,
      cvxif_instr_pkg::GER_RUN,
      cvxif_instr_pkg::XMEM_STORE,
      cvxif_instr_pkg::QGEMM_RUN,
      cvxif_instr_pkg::MQEVAL_RUN,
      cvxif_instr_pkg::SOLVEQ_RUN,
      cvxif_instr_pkg::LINXFM_RUN,
      cvxif_instr_pkg::EXTGEMM_RUN,
      cvxif_instr_pkg::GF256_INV,
      cvxif_instr_pkg::GF256_MUL,
      cvxif_instr_pkg::GF127_MUL,
      cvxif_instr_pkg::GF16_DOT: begin
        result_n = '0;
        hartid_n = '0;
        id_n     = '0;
        valid_n  = 1'b0;
        rd_n     = '0;
        we_n     = 1'b0;
      end
      default: begin
        result_n = '0;
        hartid_n = '0;
        id_n     = '0;
        valid_n  = '0;
        rd_n     = '0;
        we_n     = '0;
      end
    endcase
    if (!issue_valid_i) valid_n = 1'b0;
    if (dispatch_pending_q && dispatch_fast_q) begin
      result_n = xmpk_z;
      hartid_n = dispatch_hartid_q;
      id_n     = dispatch_id_q;
      valid_n  = 1'b1;
      rd_n     = dispatch_rd_q;
      we_n     = 1'b1;
    end
    if (state_rsp_valid) begin
      result_n = state_rsp_data;
      hartid_n = pending_hartid_q;
      id_n     = pending_id_q;
      valid_n  = 1'b1;
      rd_n     = pending_rd_q;
      we_n     = 1'b1;
    end
      if (slow_done) begin
        result_n = slow_data;
        hartid_n = slow_hartid_s2_q;
        id_n     = slow_id_s2_q;
        valid_n  = 1'b1;
        rd_n     = slow_rd_s2_q;
        we_n     = 1'b1;
      end
  end

  always_ff @(posedge clk_i, negedge rst_ni) begin
    if (~rst_ni) begin
      result_q <= '0;
      hartid_q <= '0;
      id_q     <= '0;
      valid_q  <= '0;
      rd_q     <= '0;
      we_q     <= '0;
      pending_q        <= 1'b0;
      pending_hartid_q <= '0;
      pending_id_q     <= '0;
      pending_rd_q     <= '0;
      dispatch_pending_q <= 1'b0;
      dispatch_registers_q <= '0;
      dispatch_opcode_q <= cvxif_instr_pkg::NOP;
      dispatch_hartid_q <= '0;
      dispatch_id_q <= '0;
      dispatch_rd_q <= '0;
      dispatch_stateful_q <= 1'b0;
      dispatch_fast_q <= 1'b0;
      dispatch_slow_q <= 1'b0;
      dispatch_inv_q <= 1'b0;
      slow_hartid_s1_q <= '0;
      slow_hartid_s2_q <= '0;
      slow_id_s1_q     <= '0;
      slow_id_s2_q     <= '0;
      slow_rd_s1_q     <= '0;
      slow_rd_s2_q     <= '0;
    end else begin
      result_q <= result_n;
      hartid_q <= hartid_n;
      id_q     <= id_n;
      valid_q  <= valid_n;
      rd_q     <= rd_n;
      we_q     <= we_n;
      if (issue_valid_i &&
          (stateful_op || fast_op || incoming_slow ||
           opcode_i == cvxif_instr_pkg::GF256_INV) &&
          !pending_q && !slow_busy && !inv_busy &&
          !dispatch_pending_q) begin
        dispatch_pending_q <= 1'b1;
        dispatch_registers_q <= registers_i;
        dispatch_opcode_q <= opcode_i;
        dispatch_hartid_q <= hartid_i;
        dispatch_id_q <= id_i;
        dispatch_rd_q <= rd_i;
        dispatch_stateful_q <= stateful_op;
        dispatch_fast_q <= fast_op;
        dispatch_slow_q <= incoming_slow;
        dispatch_inv_q <= opcode_i == cvxif_instr_pkg::GF256_INV;
      end else if ((eng_cmd_valid && eng_cmd_ready) ||
                   (qg_cmd_valid && qg_cmd_ready) ||
                   (mq_cmd_valid && mq_cmd_ready) ||
                   (sq_cmd_valid && sq_cmd_ready) ||
                   (lx_cmd_valid && lx_cmd_ready) ||
                   (ex_cmd_valid && ex_cmd_ready) ||
                   (dispatch_pending_q && dispatch_fast_q) ||
                   slow_start || inv_start) begin
        dispatch_pending_q <= 1'b0;
      end
      if ((eng_cmd_valid && eng_cmd_ready &&
           dispatch_opcode_q != cvxif_instr_pkg::TRF_WRITE &&
           dispatch_opcode_q != cvxif_instr_pkg::GER_SETUP) ||
          (qg_cmd_valid && qg_cmd_ready &&
           dispatch_opcode_q != cvxif_instr_pkg::QGEMM_SETUP) ||
          (mq_cmd_valid && mq_cmd_ready &&
           dispatch_opcode_q != cvxif_instr_pkg::MQEVAL_SETUP) ||
          (sq_cmd_valid && sq_cmd_ready &&
           dispatch_opcode_q != cvxif_instr_pkg::SOLVEQ_SETUP) ||
          (lx_cmd_valid && lx_cmd_ready &&
           dispatch_opcode_q != cvxif_instr_pkg::LINXFM_SETUP) ||
          (ex_cmd_valid && ex_cmd_ready &&
           dispatch_opcode_q != cvxif_instr_pkg::EXTGEMM_SETUP) ||
           inv_start) begin
        pending_q        <= 1'b1;
        pending_hartid_q <= dispatch_hartid_q;
        pending_id_q     <= dispatch_id_q;
        pending_rd_q     <= dispatch_rd_q;
      end else if (state_rsp_valid) begin
        pending_q <= 1'b0;
      end
      slow_hartid_s2_q <= slow_hartid_s1_q;
      slow_id_s2_q     <= slow_id_s1_q;
      slow_rd_s2_q     <= slow_rd_s1_q;
      if (slow_start) begin
        slow_hartid_s1_q <= dispatch_hartid_q;
        slow_id_s1_q <= dispatch_id_q;
        slow_rd_s1_q <= dispatch_rd_q;
      end
    end
  end

endmodule
