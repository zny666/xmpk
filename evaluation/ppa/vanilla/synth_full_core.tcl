set_param general.maxThreads 3
set PART xc7a200tfbg676-2
set CLKNS $::env(CLK_NS)
set OUT $::env(OUT_DIR)
set FLIST $::env(FLIST)

file mkdir $OUT
create_project -force cva6_vanilla $OUT/proj -part $PART -in_memory
set sources {}
set incdirs {}
set fh [open $FLIST r]
while {[gets $fh line] >= 0} {
  set line [string trim $line]
  if {$line eq ""} {
    continue
  } elseif {[string match "+incdir+*" $line]} {
    lappend incdirs [string range $line 8 end]
  } else {
    lappend sources $line
  }
}
close $fh
add_files -fileset sources_1 -norecurse $sources
set_property include_dirs $incdirs [current_fileset]
set xdc [open $OUT/clock.xdc w]
puts $xdc "create_clock -name core_clk -period $CLKNS \[get_ports clk_i\]"
close $xdc
read_xdc $OUT/clock.xdc

synth_design -top cva6 -part $PART -mode out_of_context
report_utilization -hierarchical -file $OUT/util_synth.rpt
report_timing_summary -file $OUT/timing_synth.rpt
write_checkpoint -force $OUT/cva6_vanilla_synth.dcp
puts "CVA6_VANILLA_SYNTH_DONE"
exit
