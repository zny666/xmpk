// SPDX-License-Identifier: Apache-2.0 WITH SHL-2.0
module xmpk_copro_top
  import cvxif_instr_pkg::*;
(
  input logic clk_i,input logic rst_ni,input logic valid_i,
  input logic [5:0] op_i,input logic [4:0] rd_i,
  input logic [63:0] a_i,input logic [63:0] b_i,input logic [63:0] c_i,
  output logic valid_o,output logic busy_o,output logic [63:0] result_o,
  output logic mem_req_valid_o,input logic mem_req_ready_i,
  output logic [63:0] mem_req_addr_o,output logic mem_req_write_o,
  output logic [63:0] mem_req_wdata_o,output logic mem_req_kill_o,
  input logic mem_rsp_valid_i,input logic [63:0] mem_rsp_data_i,
  input logic mem_rsp_error_i
);
  typedef logic hart_t;
  typedef logic [3:0] id_t;
  typedef logic [2:0][63:0] regs_t;
  regs_t regs_q;
  opcode_t op_q;
  logic valid_q;
  logic [4:0] rd_q;
  logic [3:0] issue_id_q;
  hart_t hart_o;
  id_t id_o;
  logic [4:0] rd_o;
  logic we_o;
  always_ff @(posedge clk_i or negedge rst_ni)begin
    if(!rst_ni)begin
      regs_q<='0;op_q<=ILLEGAL;valid_q<=0;rd_q<=0;issue_id_q<=0;
    end else begin
      regs_q[0]<=a_i;regs_q[1]<=b_i;regs_q[2]<=c_i;
      op_q<=opcode_t'(op_i);valid_q<=valid_i;rd_q<=rd_i;
      if(valid_i)issue_id_q<=issue_id_q+1'b1;
    end
  end
  copro_alu #(
    .NrRgprPorts(3),.XLEN(64),.hartid_t(hart_t),.id_t(id_t),.registers_t(regs_t)
  ) dut (
    .clk_i(clk_i),.rst_ni(rst_ni),.registers_i(regs_q),.opcode_i(op_q),
    .hartid_i('0),.id_i(issue_id_q),.rd_i(rd_q),.issue_valid_i(valid_q),
    .result_o(result_o),.hartid_o(hart_o),.id_o(id_o),.rd_o(rd_o),
    .valid_o(valid_o),.we_o(we_o),.busy_o(busy_o),
    .mem_req_valid_o(mem_req_valid_o),.mem_req_ready_i(mem_req_ready_i),
    .mem_req_addr_o(mem_req_addr_o),.mem_req_write_o(mem_req_write_o),
    .mem_req_wdata_o(mem_req_wdata_o),.mem_req_kill_o(mem_req_kill_o),
    .mem_rsp_valid_i(mem_rsp_valid_i),.mem_rsp_data_i(mem_rsp_data_i),
    .mem_rsp_error_i(mem_rsp_error_i)
  );
endmodule
