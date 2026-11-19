# Vanilla CVA6 Artix-7 baseline

- CVA6 commit: `742dc82a0c706e0a3bfb98b709fd3ee06d50d12a`
- Config: `cv64a6_imafdc_sv39`
- Coprocessor: `COPRO_NONE`
- Part: `xc7a200tfbg676-2`
- Tool: Vivado 2025.2
- Top: complete `cva6`, out-of-context

The source worktree is independent from all Xmpk branches. Reports include
post-synthesis and post-route utilization, timing, checkpoints, logs, and the
exact expanded file list.

For FPGA implementation, the repository's synthesis-only black-box
`tc_sram_wrapper.sv` is replaced by its provided
`tc_sram_fpga_wrapper.sv` plus `SyncSpRamBeNx64.sv`. Core RTL, cache sizes,
configuration fields, and commit remain unchanged.

See `RESULTS.md` for the final 40 MHz timing closure and the P3b/P4 comparison.
