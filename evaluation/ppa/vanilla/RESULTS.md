# Vanilla CVA6 and Xmpk P4 implementation results

## Measurement boundary

- CVA6 commit: `742dc82a0c706e0a3bfb98b709fd3ee06d50d12a`
- Config: `cv64a6_imafdc_sv39`
- Coprocessor: `COPRO_NONE`
- FPGA: Artix-7 `xc7a200tfbg676-2`
- Tool: Vivado 2025.2
- Full-core SRAMs: repository FPGA wrappers, not synthesis black boxes

The vanilla result is a complete routed `cva6` out-of-context implementation.
P3b and P4 are routed Xmpk datapath measurements plus the separately measured
410-LUT LSU MMU/PMP reuse path. Their totals are conservative incremental
costs, not a claim that a full integrated build is exactly additive.

## Vanilla core

| Stage | LUT | FF | RAMB36 | DSP | Constraint | WNS |
|---|---:|---:|---:|---:|---:|---:|
| Synthesis | 53,772 | 23,828 | 36 | 27 | 5 ns | -17.761 ns |
| Route | 52,360 | 23,828 | 36 | 27 | 24 ns | -0.808 ns |
| Same route, verified | 52,360 | 23,828 | 36 | 27 | 25 ns | +0.192 ns |

The routed critical period is 24.808 ns, giving an estimated limit of
40.31 MHz. The same physical implementation closes at 40 MHz with 0.192 ns
setup slack and 0.091 ns hold slack.

The worst path is unrelated to Xmpk. It runs from an FP cast pipeline register
to `vaddr_to_be_flushed_reg[30]`, with 46 logic levels and 24.706 ns data-path
delay. Routing contributes 16.841 ns, or 68.2%.

## Xmpk comparison

| Design | N16 cycles | N32 cycles | Incremental LUT | FF | LUTRAM | DSP | OOC Fmax |
|---|---:|---:|---:|---:|---:|---:|---:|
| P3b adopted | 932 | 4,670 | 2,702 | 1,514 | 128 | 8 | 200.8 MHz |
| P4 candidate | 740 | 2,688 | 4,278 | 1,951 | 704 | 8 | 189.1 MHz |

P4 is 1.259x faster at N16 and 1.737x faster at N32. At the verified 40 MHz
system clock, N32 sustains an effective 487.6 million GF16 MAC/s, versus
280.7 million for P3b.

P4 adds 1,576 LUT over P3b. Its conservative incremental area is 8.17% of the
52,360-LUT vanilla core and 35.7% of the 12k Xmpk budget. The Xmpk datapath has
at least 4.7x frequency headroom over the full core, so system throughput is
limited by vanilla CVA6, not by P4.

## Decision

P4 validates the aggressive direction: 32-row tiling and dual-column GER
produce a material N32 gain while remaining far above the system frequency.
It is not automatically adopted. P3b remains the canonical final baseline
until the area tradeoff is explicitly accepted.

The P4 OOC critical path is in the retained legacy C-bank path. A production
follow-up should first evaluate an aggressive-only build or physical
separation of legacy and autonomous banks. That can recover area and timing
without changing the successful P4 compute schedule.

## Artifacts

- `reports/synth_fpga_complete_5ns/`: complete FPGA-mapped synthesis reports
  and checkpoint.
- `reports/impl_fpga_24ns/`: routed utilization, 24 ns and 25 ns timing
  reports, detailed paths, and routed checkpoint.
- `reports/impl_fpga_5ns/vivado.log`: 5 ns implementation evidence.
- `metrics.csv`: machine-readable summary.
