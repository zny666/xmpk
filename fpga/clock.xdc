# Coprocessor out-of-context reference at 80 MHz.
create_clock -name core_clk -period 12.500 [get_ports clk_i]
# Board pin placement and external input/output budgets belong to the host design.
