// Public instruction-level regression using the released decoder and ALU.
module instruction_top
  import cvxif_instr_pkg::*;
#(parameter int TEST_KIND = 0) (
  input logic clk_i, rst_ni, cmd_valid_i, cmd_run_i,
  input logic [5:0] cmd_opcode_i,
  input logic [63:0] a_i, b_i, c_i,
  output logic cmd_ready_o, rsp_valid_o,
  output logic [63:0] rsp_data_o,
  output logic mem_req_valid_o, mem_req_write_o, mem_req_kill_o,
  input logic mem_req_ready_i,
  output logic [63:0] mem_req_addr_o, mem_req_wdata_o,
  input logic mem_rsp_valid_i, mem_rsp_error_i,
  input logic [63:0] mem_rsp_data_i
);
  typedef logic [2:0][63:0] regs_t;
  typedef struct packed {
    logic hartid;
    logic [3:0] id;
    logic [31:0] instr;
  } req_t;
  typedef struct packed {
    logic [2:0] rs_valid;
    regs_t rs;
  } reg_t;
  req_t req;
  reg_t reg_in;
  issue_resp_t resp;
  regs_t regs;
  opcode_t op, wanted;
  logic dec_ready, busy, hart, result_hart, we;
  logic [3:0] id, result_id;
  logic [4:0] rd, result_rd;
  always_comb begin
    wanted = TEST_KIND == 5 ? opcode_t'(cmd_opcode_i) :
             TEST_KIND == 0 ? (cmd_run_i ? EXTGEMM_RUN : EXTGEMM_SETUP) :
             TEST_KIND == 1 ? (cmd_run_i ? QGEMM_RUN : QGEMM_SETUP) :
             TEST_KIND == 2 ? (cmd_run_i ? SOLVEQ_RUN : SOLVEQ_SETUP) :
             TEST_KIND == 3 ? (cmd_run_i ? MQEVAL_RUN : MQEVAL_SETUP) :
                             (cmd_run_i ? LINXFM_RUN : LINXFM_SETUP);
    req = '0;
    req.hartid = 1;
    req.id = 5;
    for (int i=0; i<NbInstr; i++)
      if (CoproInstr[i].opcode == wanted)
        req.instr = CoproInstr[i].instr | (32'd7 << 7) |
                    (32'd9 << 15) | (32'd11 << 20);
    reg_in.rs = {c_i,b_i,a_i};
    reg_in.rs_valid = '1;
  end
  assign cmd_ready_o = !busy;
  instr_decoder #(
    .copro_issue_resp_t(copro_issue_resp_t), .opcode_t(opcode_t),
    .NbInstr(NbInstr), .CoproInstr(CoproInstr), .NrRgprPorts(3),
    .hartid_t(logic), .id_t(logic [3:0]), .x_issue_req_t(req_t),
    .x_issue_resp_t(issue_resp_t), .x_register_t(reg_t), .registers_t(regs_t)
  ) decoder (
    .clk_i, .rst_ni, .issue_valid_i(cmd_valid_i && !busy), .issue_req_i(req),
    .issue_ready_o(dec_ready), .issue_resp_o(resp),
    .register_valid_i(1'b1), .register_i(reg_in), .registers_o(regs),
    .opcode_o(op), .hartid_o(hart), .id_o(id), .rd_o(rd)
  );
  copro_alu #(.NrRgprPorts(3), .XLEN(64), .hartid_t(logic),
    .id_t(logic [3:0]), .registers_t(regs_t)) alu (
    .clk_i, .rst_ni, .registers_i(regs), .opcode_i(op), .hartid_i(hart),
    .id_i(id), .rd_i(rd), .issue_valid_i(cmd_valid_i && !busy && dec_ready && resp.accept),
    .result_o(rsp_data_o), .hartid_o(result_hart), .id_o(result_id),
    .rd_o(result_rd), .valid_o(rsp_valid_o), .we_o(we), .busy_o(busy),
    .mem_req_valid_o, .mem_req_ready_i, .mem_req_addr_o, .mem_req_write_o,
    .mem_req_wdata_o, .mem_req_kill_o, .mem_rsp_valid_i, .mem_rsp_data_i, .mem_rsp_error_i
  );
  always @(posedge clk_i) if (rst_ni) begin
    if (cmd_valid_i && !busy) begin
      assert (dec_ready && resp.accept && op == wanted)
        else $fatal(1, "instruction decode failure");
    end
    if (rsp_valid_o) begin
      assert (result_hart == 1 && result_id == 5)
        else $fatal(1, "completion tag failure");
      if (we) assert (result_rd == 7) else $fatal(1, "RUN rd failure");
    end
  end
endmodule
