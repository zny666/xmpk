// Coherent 64-bit memory port for Xmpk autonomous engines.
// Virtual addresses are translated and PMP-checked by the existing LSU.
module xmpk_mem_adapter
#(
    parameter config_pkg::cva6_cfg_t CVA6Cfg = config_pkg::cva6_cfg_empty,
    parameter type dcache_req_i_t = logic,
    parameter type dcache_req_o_t = logic
) (
    input logic clk_i,
    input logic rst_ni,

    input  logic                    req_valid_i,
    output logic                    req_ready_o,
    input  logic [CVA6Cfg.XLEN-1:0] req_addr_i,
    input  logic                    req_write_i,
    input  logic [CVA6Cfg.XLEN-1:0] req_wdata_i,
    input  logic                    req_kill_i,
    output logic                    rsp_valid_o,
    output logic [CVA6Cfg.XLEN-1:0] rsp_data_o,
    output logic                    rsp_error_o,

    output logic                    xlate_req_o,
    output logic [CVA6Cfg.VLEN-1:0] xlate_vaddr_o,
    output logic                    xlate_store_o,
    input  logic                    xlate_valid_i,
    input  logic [CVA6Cfg.PLEN-1:0] xlate_paddr_i,
    input  logic                    xlate_error_i,

    output dcache_req_i_t dcache_req_o,
    input  dcache_req_o_t dcache_rsp_i,
    input logic dcache_wbuffer_empty_i
);
  typedef enum logic [2:0] {
    IDLE,
    TRANSLATE,
    REQ,
    TAG,
    WAIT_RSP
  } state_t;

  state_t state_q;
  logic [CVA6Cfg.XLEN-1:0] addr_q;
  logic write_q;
  logic [CVA6Cfg.XLEN-1:0] wdata_q;
  logic store_sync_q;
  logic killed_q;
  logic req_aligned;
  logic req_cacheable;

  assign req_aligned   = req_addr_i[2:0] == 3'b000;
  assign req_cacheable = config_pkg::is_inside_cacheable_regions(
      CVA6Cfg, CVA6Cfg.XLEN'(xlate_paddr_i)
  );
  assign req_ready_o   = state_q == IDLE && dcache_wbuffer_empty_i;
  assign xlate_req_o   = state_q == TRANSLATE;
  assign xlate_vaddr_o = addr_q[CVA6Cfg.VLEN-1:0];
  assign xlate_store_o = write_q;

  always_comb begin
    dcache_req_o               = '0;
    dcache_req_o.address_index = addr_q[CVA6Cfg.DCACHE_INDEX_WIDTH-1:0];
    dcache_req_o.address_tag   =
        addr_q[CVA6Cfg.DCACHE_INDEX_WIDTH +: CVA6Cfg.DCACHE_TAG_WIDTH];
    dcache_req_o.data_be       = '1;
    dcache_req_o.data_size     = 2'b11;
    dcache_req_o.data_id       = '0;
    dcache_req_o.data_we       = write_q;
    dcache_req_o.data_wdata    = wdata_q;

    if (state_q == REQ) begin
      dcache_req_o.data_req = !req_kill_i;
    end else if (state_q == TAG) begin
      dcache_req_o.tag_valid = 1'b1;
      dcache_req_o.kill_req  = req_kill_i;
    end else if (state_q == WAIT_RSP && req_kill_i) begin
      dcache_req_o.kill_req  = 1'b1;
      dcache_req_o.tag_valid = 1'b1;
    end
  end

  always_ff @(posedge clk_i or negedge rst_ni) begin
    if (!rst_ni) begin
      state_q     <= IDLE;
      addr_q      <= '0;
      write_q     <= 1'b0;
      wdata_q     <= '0;
      store_sync_q <= 1'b0;
      killed_q    <= 1'b0;
      rsp_valid_o <= 1'b0;
      rsp_data_o  <= '0;
      rsp_error_o <= 1'b0;
    end else begin
      rsp_valid_o <= 1'b0;

      case (state_q)
        IDLE: begin
          killed_q <= 1'b0;
          store_sync_q <= 1'b0;
          if (req_valid_i && req_ready_o) begin
            if (!req_aligned || req_kill_i) begin
              rsp_valid_o <= 1'b1;
              rsp_data_o  <= '0;
              rsp_error_o <= 1'b1;
            end else begin
              addr_q <= req_addr_i;
              write_q <= req_write_i;
              wdata_q <= req_wdata_i;
              state_q <= TRANSLATE;
            end
          end
        end
        TRANSLATE: begin
          if (req_kill_i) begin
            rsp_valid_o <= 1'b1;
            rsp_data_o  <= '0;
            rsp_error_o <= 1'b1;
            state_q     <= IDLE;
          end else if (xlate_valid_i) begin
            if (xlate_error_i || !req_cacheable) begin
              rsp_valid_o <= 1'b1;
              rsp_data_o  <= '0;
              rsp_error_o <= 1'b1;
              state_q     <= IDLE;
            end else begin
              addr_q <= CVA6Cfg.XLEN'(xlate_paddr_i);
              state_q <= REQ;
            end
          end
        end
        REQ: begin
          if (req_kill_i) begin
            rsp_valid_o <= 1'b1;
            rsp_data_o  <= '0;
            rsp_error_o <= 1'b1;
            state_q     <= IDLE;
          end else if (dcache_rsp_i.data_gnt) begin
            if (write_q) begin
              // A store miss is granted before the AXI write is globally
              // visible. Follow it with a same-address load as a coherent
              // completion barrier before reporting success.
              write_q     <= 1'b0;
              store_sync_q <= 1'b1;
              state_q     <= REQ;
            end else begin
              state_q <= TAG;
            end
          end
        end
        TAG: begin
          killed_q <= req_kill_i;
          if (dcache_rsp_i.data_rvalid) begin
            rsp_valid_o <= 1'b1;
            rsp_data_o  <= store_sync_q ? '0 : dcache_rsp_i.data_rdata;
            rsp_error_o <= dcache_rsp_i.data_error || req_kill_i;
            state_q     <= IDLE;
          end else begin
            state_q <= WAIT_RSP;
          end
        end
        WAIT_RSP: begin
          killed_q <= killed_q || req_kill_i;
          if (dcache_rsp_i.data_rvalid) begin
            rsp_valid_o <= 1'b1;
            rsp_data_o  <= store_sync_q ? '0 : dcache_rsp_i.data_rdata;
            rsp_error_o <= dcache_rsp_i.data_error || killed_q || req_kill_i;
            state_q     <= IDLE;
          end
        end
        default: state_q <= IDLE;
      endcase
    end
  end
endmodule
