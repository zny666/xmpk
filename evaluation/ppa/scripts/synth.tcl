set_param general.maxThreads 2
if {[info exists ::env(XMPK_THREADS)]} {set_param general.maxThreads $::env(XMPK_THREADS)}
set root [file dirname [file dirname [file normalize [info script]]]]
set part xc7a200tfbg676-2
set cases $::env(XMPK_CASES)

proc set_period {period} {
    create_clock -name core_clk -period $period \
        -waveform [list 0 [expr {$period/2.0}]] [get_ports clk_i]
}
proc metrics {out} {
    set text [report_timing_summary -max_paths 1 -return_string]
    set f [open $out w]; puts $f $text; close $f
    if {![regexp {WNS\(ns\)[^\n]*\n[ \t-]+\n([^\n]+)} $text -> row]} {
        error "Timing summary not found"
    }
    set numbers [regexp -all -inline {\S+} $row]
    return [list [lindex $numbers 0] [lindex $numbers 4] [lindex $numbers 8]]
}
proc passes {values} {
    foreach x $values {if {$x eq "N/A" || $x < 0} {return 0}}
    return 1
}

foreach item $cases {
    lassign $item flavor kind
    set out "$root/results/$flavor-$kind"
    file mkdir $out
    create_project -in_memory -part $part
    set rtl "$root/$flavor/cva6/core/cvxif_example"
    if {$kind eq "system"} {
        set data [exec python3 "$root/scripts/flist.py" $root $flavor]
        set lines [split $data "\n"]
        set sources {}; set incdirs {}
        foreach line $lines {
            if {[string match "+incdir+*" $line]} {
                lappend incdirs [string range $line 8 end]
            } elseif {$line ne ""} {lappend sources $line}
        }
        add_files -fileset sources_1 -norecurse $sources
        set_property include_dirs $incdirs [current_fileset]
        set top xmpk_cva6_system_top
        set period 25.0
        # Match the established full-system timing-driven synthesis strategy.
        set synthesis_period 5.0
    } elseif {$kind eq "copro"} {
        read_verilog -sv "$rtl/include/cvxif_instr_pkg.sv"
        foreach name {gf2n_mul gf256_inv8 gf256_inv_rom gf_assist xmpk_alu
                      xmpk_ger_trf xmpk_qgemm xmpk_mqeval xmpk_solveq
                      xmpk_linxfm xmpk_extgemm xmpk_slow_mul copro_alu} {
            read_verilog -sv "$rtl/$name.sv"
        }
        read_verilog -sv "$root/tops/xmpk_copro_top.sv"
        set top xmpk_copro_top
        set period 12.5
        set synthesis_period $period
    } else {
        set top $kind
        read_verilog -sv "$rtl/$kind.sv"
        read_verilog -sv "$rtl/gf2n_mul.sv"
        set period 12.5
        set synthesis_period $period
    }
    if {[catch {
        # Units retain the previous unconstrained synthesis, followed by 80MHz implementation.
        if {$kind eq "system" || $kind eq "copro"} {
            set f [open "$out/synthesis-clock.xdc" w]
            puts $f "create_clock -name core_clk -period $synthesis_period \[get_ports clk_i\]"
            close $f
            read_xdc "$out/synthesis-clock.xdc"
        }
        synth_design -top $top -part $part -mode out_of_context
        set_period $period
        report_utilization -file "$out/synth-util.rpt"
        report_utilization -hierarchical -file "$out/synth-hierarchy.rpt"
        write_checkpoint -force "$out/synth.dcp"
        opt_design
        if {$kind eq "system"} {
            place_design -directive Explore
            phys_opt_design -directive AggressiveExplore
            route_design -directive Explore
            phys_opt_design -directive AggressiveExplore
            route_design -directive Explore
        } else {
            place_design
            phys_opt_design
            route_design
        }
        report_utilization -file "$out/route-util.rpt"
        report_utilization -hierarchical -file "$out/route-hierarchy.rpt"
        report_drc -file "$out/drc.rpt"
        report_timing_summary -check_timing_verbose -file "$out/timing-coverage.rpt"
        set target [metrics "$out/timing-target.rpt"]
        report_clocks -file "$out/clocks-target.rpt"
        write_checkpoint -force "$out/routed-target.dcp"
        report_power -file "$out/power-target.rpt"
        # Re-clock this exact routed design. Confirm the largest passing integer MHz
        # and retain both its report and the failing next-MHz report.
        set mhz [expr {max(1,int(floor(1000.0/($period-[lindex $target 0]+0.005))))}]
        set_period [expr {1000.0/$mhz}]
        set valid [metrics "$out/timing-probe.rpt"]
        while {![passes $valid] && $mhz > 1} {
            incr mhz -1
            set_period [expr {1000.0/$mhz}]
            set valid [metrics "$out/timing-probe.rpt"]
        }
        if {![passes $valid]} {error "No passing frequency"}
        while {1} {
            set next [expr {$mhz+1}]
            set_period [expr {1000.0/$next}]
            set edge [metrics "$out/timing-next-mhz.rpt"]
            if {![passes $edge]} {break}
            set mhz $next
            if {$mhz > 500} {error "Unexpected frequency; inspect timing coverage"}
        }
        set_period [expr {1000.0/$mhz}]
        set final [metrics "$out/timing-max-supported.rpt"]
        report_clocks -file "$out/clocks-max-supported.rpt"
        report_timing -delay_type max -max_paths 10 -file "$out/critical-paths.rpt"
        write_checkpoint -force "$out/routed-max-supported.dcp"
        set f [open "$out/frequency.json" w]
        puts $f [format {{"target_MHz":%.6f,"target_WNS_ns":%.6f,"target_WHS_ns":%.6f,"target_WPWS_ns":%.6f,"STA_max_integer_MHz":%d,"max_WNS_ns":%.6f,"max_WHS_ns":%.6f,"max_WPWS_ns":%.6f,"next_MHz":%d,"next_WNS_ns":%.6f,"next_WHS_ns":%.6f,"next_WPWS_ns":%.6f}} \
            [expr {1000.0/$period}] {*}$target $mhz {*}$final $next {*}$edge]
        close $f
        set f [open "$out/PASS" w]; puts $f "completed"; close $f
        puts "CASE_COMPLETE $flavor $kind MHz=$mhz TARGET=$target FINAL=$final"
    } err]} {
        set f [open "$out/ERROR" w]; puts $f $err; close $f
        puts "CASE_ERROR $flavor $kind: $err"
    }
    close_project
}
