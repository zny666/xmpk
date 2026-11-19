set_param general.maxThreads 3
set CLKNS $::env(CLK_NS)
set OUT $::env(OUT_DIR)
set SYNTH_DCP $::env(SYNTH_DCP)

file mkdir $OUT
open_checkpoint $SYNTH_DCP
if {[expr {abs([get_property PERIOD [get_clocks core_clk]] - $CLKNS)}] > 0.001} {
  create_clock -name core_clk -period $CLKNS [get_ports clk_i]
}

opt_design
place_design -directive Explore
phys_opt_design -directive AggressiveExplore
route_design -directive Explore
report_utilization -hierarchical -file $OUT/util_route.rpt
report_timing_summary -file $OUT/timing_route.rpt
report_timing -delay_type max -max_paths 20 -file $OUT/timing_paths.rpt
write_checkpoint -force $OUT/cva6_vanilla_route.dcp
puts "CVA6_VANILLA_FINAL_IMPL_DONE"
exit
