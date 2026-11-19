set DCP $::env(ROUTED_DCP)
set OUT $::env(OUT_DIR)
set CLKNS $::env(CLK_NS)

file mkdir $OUT
open_checkpoint $DCP
create_clock -name core_clk -period $CLKNS [get_ports clk_i]
report_timing_summary -file $OUT/timing_route_${CLKNS}ns.rpt
report_timing -delay_type max -max_paths 20 -file $OUT/timing_paths_${CLKNS}ns.rpt
puts "CVA6_VANILLA_RETIMED"
exit
