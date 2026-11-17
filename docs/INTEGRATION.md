# Integration

## Source order and configuration

Compile `rtl/include/cvxif_instr_pkg.sv` before importing modules. `rtl/files.f` includes the complete coprocessor source closure. The design uses SystemVerilog packages, packed structures, type parameters, generated arithmetic lanes, and initialized inverse tables.

The adopted configuration is RV64 with three register-read ports:

- `XLEN = 64`
- `NrRgprPorts = 3`
- `registers_t = logic [2:0][63:0]`
- Concrete `id_t` and `hartid_t` types matching the host.

The RTL retains some upstream RV32/two-port defaults for interface compatibility. Those defaults do not describe the evaluated Xmpk configuration.

## Entry points

`copro_alu` accepts an `opcode_t` value, three operands, destination register, hart ID, transaction ID, and `issue_valid_i`. Assert issue only while `busy_o` is low. Completion carries `valid_o`, `result_o`, `we_o`, `rd_o`, and the original transaction/hart tags. `we_o` determines architectural register writeback; some setup and tile-write instructions update accelerator state without register writeback.

`cvxif_example_coprocessor` connects instruction decode to `copro_alu`. The host must pass its concrete CV-X-IF type parameters. The wrapper accesses the following members:

| Type | Members used |
|---|---|
| `x_compressed_req_t` | `instr` |
| `x_compressed_resp_t` | `accept`, `instr` |
| `x_issue_req_t` | `instr`, `id`, `hartid` |
| `x_issue_resp_t` | `accept`, `writeback`, `register_read` |
| `x_register_t` | `rs`, `rs_valid` |
| `x_result_t` | `hartid`, `id`, `data`, `rd`, `we` |
| `cvxif_req_t` | `compressed_req`, `compressed_valid`, `issue_req`, `issue_valid`, `register`, `register_valid` |
| `cvxif_resp_t` | `compressed_ready`, `compressed_resp`, `issue_ready`, `issue_resp`, `register_ready`, `result_valid`, `result` |

Supply the remaining wrapper type parameters consistently with the host's definitions. Issue/register handling follows a coupled interface: register operands must be present and valid when the matching instruction is accepted.

The current wrapper produces a completion pulse and does not queue results under result-interface backpressure. The host must accept that completion or add a result buffer. The wrapper also does not implement commit/kill-based cancellation of accepted instructions; integrate with the host's established non-speculative offload policy.

## Operand mapping

Operand 0 is rs1, operand 1 is rs2, and operand 2 is the additional source selected by the host. Instructions such as GF16_MACC, BGER16R, ROWELIM16, and the three-address engine SETUP operations require old-rd forwarding. Configure the CPU's additional-source selection accordingly. The decoder's `register_read` masks and comments in `cvxif_instr_pkg.sv` are authoritative.

The primary custom opcode is `0x7b` (custom-3). The instruction package lists matching masks and encoded functions. Enum values used at the internal `copro_alu` interface are distinct from machine-code encodings.

## Accelerator memory interface

The interface transfers one XLEN-sized data word per request:

- A request is accepted when `mem_req_valid_o && mem_req_ready_i`.
- `mem_req_addr_o` is a byte address; `mem_req_write_o` selects a store.
- `mem_req_wdata_o` provides store data.
- `mem_rsp_valid_i` completes the outstanding operation; loads use `mem_rsp_data_i`.
- `mem_rsp_error_i` reports an access failure to the active engine.

Responses have no transaction ID or response-ready signal. The adapter must preserve response ordering and the request/response contract of the selected engine. It must provide responses for accepted writes as well as reads. QGEMM can issue the next B-word load during its MAC state; the adapter must support that schedule and must not assume requests occur only in states named REQ.

The adapter is also responsible for the host's address translation, access permissions, and visibility/coherence requirements. Those policies are outside this RTL snapshot. Follow each engine's alignment and layout checks; software must allocate suitably sized input/output regions.

## State and timing

`rst_ni` is an active-low asynchronous control reset. Internal tile/work RAM and caches are not fully erased by control reset or completion. Sensitive-state lifecycle management must be supplied by the integration.

The adopted solver uses data-dependent pivot addresses and an early singular failure path. The design does not claim whole-signature constant time or physical side-channel resistance.

FPGA timing must be checked in the integrating top level with its actual launch/capture clock network, memory adapter, and I/O constraints. The earlier 80 MHz OOC evaluation had passing internal register paths and external input hold violations under 0 ns input-delay assumptions.
