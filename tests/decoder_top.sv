// SPDX-License-Identifier: Apache-2.0 WITH SHL-2.0
module decoder_top
  import cvxif_instr_pkg::*;
(
  input logic clk_i, rst_ni,
  input logic [31:0] instr_i,
  input logic [2:0] rs_valid_i,
  input logic [63:0] a_i,b_i,c_i,
  output logic accept_o,writeback_o,ready_o,
  output logic [2:0] read_o,
  output logic [5:0] opcode_o,
  output logic [63:0] a_o,b_o,c_o
);
  typedef logic [2:0][63:0] regs_t;
  typedef struct packed {
    logic hartid;
    logic [3:0] id;
    logic [31:0] instr;
  } req_t;
  typedef struct packed { logic [2:0] rs_valid; regs_t rs; } reg_t;
  req_t req;
  reg_t reg_in;
  issue_resp_t resp;
  regs_t regs;
  opcode_t op;
  logic ready,hart;
  logic [3:0] id;
  logic [4:0] rd;
  assign req='{hartid:1'b1,id:4'd5,instr:instr_i};
  assign reg_in='{rs_valid:rs_valid_i,rs:{c_i,b_i,a_i}};
  assign accept_o=resp.accept;
  assign writeback_o=resp.writeback;
  assign read_o=resp.register_read;
  assign opcode_o=op;
  assign ready_o=ready;
  assign {c_o,b_o,a_o}=regs;
  instr_decoder #(
    .copro_issue_resp_t(copro_issue_resp_t),.opcode_t(opcode_t),
    .NbInstr(NbInstr),.CoproInstr(CoproInstr),.NrRgprPorts(3),
    .hartid_t(logic),.id_t(logic [3:0]),.x_issue_req_t(req_t),
    .x_issue_resp_t(issue_resp_t),.x_register_t(reg_t),.registers_t(regs_t)
  ) decoder (
    .clk_i,.rst_ni,.issue_valid_i(1'b1),.issue_req_i(req),
    .issue_ready_o(ready),.issue_resp_o(resp),
    .register_valid_i(1'b1),.register_i(reg_in),.registers_o(regs),
    .opcode_o(op),.hartid_o(hart),.id_o(id),.rd_o(rd)
  );
endmodule
