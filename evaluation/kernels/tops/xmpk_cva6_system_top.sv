`include "rvfi_types.svh"

module xmpk_cva6_system_top #(
    parameter config_pkg::cva6_cfg_t CVA6Cfg =
        build_config_pkg::build_config(cva6_config_pkg::cva6_cfg)
) (
    input  logic                    clk_i,
    input  logic                    rst_ni,
    input  logic [CVA6Cfg.VLEN-1:0] boot_addr_i,
    input  logic [CVA6Cfg.XLEN-1:0] hart_id_i,
    input  logic [1:0]              irq_i,
    input  logic                    ipi_i,
    input  logic                    time_irq_i,
    input  logic                    debug_req_i,
    output ariane_axi::req_t        noc_req_o,
    input  ariane_axi::resp_t       noc_resp_i
);
  localparam type rvfi_probes_instr_t = `RVFI_PROBES_INSTR_T(CVA6Cfg);
  localparam type rvfi_probes_csr_t = `RVFI_PROBES_CSR_T(CVA6Cfg);
  localparam type rvfi_probes_t = struct packed {
    rvfi_probes_csr_t   csr;
    rvfi_probes_instr_t instr;
  };

  rvfi_probes_t rvfi_unused;

  ariane #(
      .CVA6Cfg(CVA6Cfg),
      .rvfi_probes_instr_t(rvfi_probes_instr_t),
      .rvfi_probes_csr_t(rvfi_probes_csr_t),
      .rvfi_probes_t(rvfi_probes_t),
      .noc_req_t(ariane_axi::req_t),
      .noc_resp_t(ariane_axi::resp_t)
  ) i_ariane (
      .clk_i(clk_i),
      .rst_ni(rst_ni),
      .boot_addr_i(boot_addr_i),
      .hart_id_i(hart_id_i),
      .irq_i(irq_i),
      .ipi_i(ipi_i),
      .time_irq_i(time_irq_i),
      .debug_req_i(debug_req_i),
      .rvfi_probes_o(rvfi_unused),
      .noc_req_o(noc_req_o),
      .noc_resp_i(noc_resp_i)
  );
endmodule
