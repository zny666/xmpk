# Run: vivado -mode batch -source fpga/build.tcl
set root [file dirname [file dirname [file normalize [info script]]]]
set out "$root/build/ppa"
if {[info exists ::env(XMPK_PPA_OUT)]} {set out $::env(XMPK_PPA_OUT)}
file mkdir $out
set_param general.maxThreads 2
create_project -in_memory -part xc7a200tfbg676-2
set f [open "$root/rtl/files.f" r]
foreach line [split [read $f] "\n"] {
    if {$line ne "" && ![string match "#*" $line]} {
        read_verilog -sv "$root/$line"
    }
}
close $f
read_verilog -sv "$root/fpga/xmpk_copro_top.sv"
read_xdc "$root/fpga/clock.xdc"
synth_design -top xmpk_copro_top -part xc7a200tfbg676-2 -mode out_of_context
report_utilization -file "$out/synth-util.rpt"
opt_design
place_design
phys_opt_design
route_design
report_utilization -file "$out/route-util.rpt"
report_utilization -hierarchical -file "$out/route-hierarchy.rpt"
report_timing_summary -report_unconstrained -file "$out/timing.rpt"
report_drc -file "$out/drc.rpt"
report_power -file "$out/power.rpt"
write_checkpoint -force "$out/routed.dcp"
puts "XMPK_OOC_IMPLEMENTATION_COMPLETE"
